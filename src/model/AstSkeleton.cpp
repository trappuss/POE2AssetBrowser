#include "model/AstSkeleton.h"
#include "model/RigMath.h"
#include "bundle/Bundle.h"

#include <QHash>
#include <QtEndian>
#include <cstring>
#include <functional>

namespace {
struct Underrun {};
struct Reader {
    const uint8_t* p; size_t n; size_t off = 0;
    Reader(const QByteArray& b) : p(reinterpret_cast<const uint8_t*>(b.constData())), n(size_t(b.size())) {}
    void need(size_t k) const { if (off + k > n) throw Underrun{}; }
    uint8_t  u8()  { need(1); return p[off++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, p+off, 2); off += 2; return qFromLittleEndian(v); }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, p+off, 4); off += 4; return qFromLittleEndian(v); }
    float    f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    QString  ascii(size_t k) { need(k); QString s = QString::fromUtf8(reinterpret_cast<const char*>(p+off), int(k)); off += k; return s; }
    void seek(size_t a) { if (a > n) throw Underrun{}; off = a; }
};

void resolveParents(QVector<AstSkeleton::Bone>& bones)
{
    // Walk child chains: bone i's firstChild begins a sibling list whose members all have parent i.
    // A malformed .ast can point a child/sibling link back into its own chain; without a visited guard
    // that is an infinite sibling loop or unbounded recursion → hang / stack overflow (template §2,
    // fail closed on a bad asset). visited[] breaks any revisit.
    QVector<char> visited(bones.size(), 0);
    std::function<void(int,int)> walk = [&](int i, int parent) {
        while (i != 255 && i >= 0 && i < bones.size()) {
            if (visited[i]) break;      // cycle in the sibling/child graph — stop
            visited[i] = 1;
            bones[i].parent = parent;
            walk(bones[i].rawChild, i);
            i = bones[i].rawSibling;
        }
    };
    // The root (index 0) has parent -1; its own child chain descends from there.
    if (!bones.isEmpty()) { bones[0].parent = -1; visited[0] = 1; walk(bones[0].rawChild, 0); }
}
}  // namespace

namespace {
// Parse the skeleton header: version, bones (with bind accumulated to model-space), lights, and the
// clip metadata list. On success `sk.valid` is set and `blobOff` receives the byte offset of the
// nested key-data bundle (the tail). Returns false (and leaves sk.valid false) on an unsupported
// version or truncation. Shared by parse() (decode all clips) and parseWithClip() (decode one).
bool parseHeader(const QByteArray& data, AstSkeleton::Skeleton& sk, size_t& blobOff)
{
    using namespace AstSkeleton;
    Reader r(data);
    const uint8_t version = r.u8();
    sk.version = version;
    const uint8_t boneCount = r.u8();
    r.u8();                              // b2 (unknown; does not change layout)
    const uint8_t animCount = r.u8();
    r.u8(); r.u8(); r.u8();              // zeros
    const uint8_t lightCount = r.u8();

    if (version != 11 && version != 12) {
        sk.note = QStringLiteral("skeleton version %1: bones/clips not decoded (only 11/12 supported)").arg(version);
        sk.valid = false;
        return false;
    }

    sk.bones.resize(boneCount);
    for (int i = 0; i < boneCount; ++i) {
        Bone& b = sk.bones[i];
        b.rawSibling = r.u8();
        b.rawChild = r.u8();
        for (int k = 0; k < 16; ++k) b.bind[k] = r.f32();
        const uint8_t nl = r.u8();
        r.u8();                          // unk (0, rarely 1/64)
        b.name = r.ascii(nl);
    }
    resolveParents(sk.bones);

    // The .ast stores each bone's bind matrix PARENT-LOCAL (measured: a phys_beam chain has every
    // child at local (320,0,0) relative to the previous bone — model-space would pile them on one
    // point). Accumulate to MODEL-SPACE (world = local × parentWorld, v·M) so the overlay, skinning
    // and export — all of which expect a model-space bind — are correct. Bones are not guaranteed
    // parent-before-child, so resolve recursively with memoisation.
    {
        const int nb = sk.bones.size();
        QVector<RigMath::Mat4> localRaw(nb);
        for (int i = 0; i < nb; ++i) localRaw[i] = sk.bones[i].bind;
        QVector<char> done(nb, 0), inProgress(nb, 0);
        std::function<RigMath::Mat4(int)> accum = [&](int b) -> RigMath::Mat4 {
            if (done[b]) return sk.bones[b].bind;
            if (inProgress[b]) return localRaw[b];   // parent cycle — break it by treating b as a root
            inProgress[b] = 1;
            const int p = sk.bones[b].parent;
            RigMath::Mat4 w = (p >= 0 && p < nb && p != b) ? RigMath::mul(localRaw[b], accum(p)) : localRaw[b];
            sk.bones[b].bind = w; done[b] = 1; inProgress[b] = 0; return w;
        };
        for (int i = 0; i < nb; ++i) accum(i);
        for (int i = 0; i < nb; ++i) sk.bones[i].inverseBind = RigMath::inverse(sk.bones[i].bind);
    }

    for (int i = 0; i < lightCount; ++i) {
        const uint8_t nl = r.u8(); r.u8();
        for (int k = 0; k < 12; ++k) r.f32();
        r.u32(); r.u32(); r.u16();
        r.ascii(nl);
    }

    sk.clips.resize(animCount);
    for (int i = 0; i < animCount; ++i) {
        Clip& c = sk.clips[i];
        r.u16();                         // per-clip bone count
        const uint8_t fps = r.u8();      // 24 / 30 / 60 (docs §8) — was previously swallowed by a u16
        r.u8();                          // flags (0x6c | 0x6f)
        r.u8();                          // 0
        const uint8_t nl = r.u8();
        const uint8_t pl = r.u8();
        c.offset = r.u32();
        c.size = r.u32();
        c.name = r.ascii(nl);
        c.parentName = r.ascii(pl);
        c.fps = (fps == 24 || fps == 30 || fps == 60) ? int(fps) : 30;
    }

    sk.valid = true;
    blobOff = r.off;
    return true;
}

// Decode clip[ci]'s per-bone KeySets from the decompressed key buffer `raw`. One bad clip is reported
// (empty keys), never guessed (docs §8).
void decodeOneClip(const QByteArray& raw, AstSkeleton::Skeleton& sk, int ci)
{
    using namespace AstSkeleton;
    if (ci < 0 || ci >= sk.clips.size()) return;
    Clip& c = sk.clips[ci];
    c.keys.clear();
    try {
        Reader kr(raw);
        kr.seek(c.offset);
        for (int bi = 0; bi < sk.bones.size(); ++bi) {
            KeySet ks;
            kr.u8();                     // 0
            ks.nodeId = int(kr.u32());
            const uint32_t ns = kr.u32(), nr = kr.u32(), np = kr.u32();
            kr.u32(); kr.u32(); kr.u32(); kr.u32();
            for (uint32_t k = 0; k < ns; ++k) { ks.scaleTimes.append(kr.f32()); ks.scale.append({kr.f32(), kr.f32(), kr.f32()}); }
            for (uint32_t k = 0; k < nr; ++k) { ks.rotTimes.append(kr.f32()); ks.rot.append({kr.f32(), kr.f32(), kr.f32(), kr.f32()}); }
            for (uint32_t k = 0; k < np; ++k) { ks.posTimes.append(kr.f32()); ks.pos.append({kr.f32(), kr.f32(), kr.f32()}); }
            c.keys.append(ks);
        }
    } catch (Underrun&) {
        c.keys.clear();
    }
}

// Decompress the nested key bundle at the tail (from `blobOff`); empty QByteArray on failure, with the
// reason written to sk.note.
QByteArray decompressKeyBlob(const QByteArray& data, size_t blobOff, AstSkeleton::Skeleton& sk)
{
    const QByteArray blob = data.mid(int(blobOff));
    QString berr;
    auto b = Bundle::fromMemory(blob, &berr);
    if (!b) { sk.note = QStringLiteral("clip data bundle unreadable: %1").arg(berr); return {}; }
    const QByteArray raw = b->readAll(&berr);
    if (raw.isEmpty()) sk.note = QStringLiteral("clip data empty: %1").arg(berr);
    return raw;
}
}  // namespace

AstSkeleton::Skeleton AstSkeleton::parse(const QByteArray& data, bool decodeClips, QString* error)
{
    Skeleton sk;
    try {
        size_t blobOff = 0;
        if (!parseHeader(data, sk, blobOff)) return sk;

        if (decodeClips && !sk.clips.isEmpty()) {
            const QByteArray raw = decompressKeyBlob(data, blobOff, sk);
            if (raw.isEmpty()) return sk;
            for (int ci = 0; ci < sk.clips.size(); ++ci) decodeOneClip(raw, sk, ci);
            sk.clipsDecoded = true;
        }
        return sk;
    } catch (Underrun&) {
        if (error) *error = QStringLiteral("truncated .ast");
        sk.valid = false;
        return sk;
    }
}

AstSkeleton::Skeleton AstSkeleton::parseWithClip(const QByteArray& data, int clipIndex, QString* error)
{
    Skeleton sk;
    try {
        size_t blobOff = 0;
        if (!parseHeader(data, sk, blobOff)) return sk;

        if (clipIndex >= 0 && clipIndex < sk.clips.size()) {
            const QByteArray raw = decompressKeyBlob(data, blobOff, sk);
            if (raw.isEmpty()) return sk;
            decodeOneClip(raw, sk, clipIndex);
            sk.clipsDecoded = true;   // this clip is decoded; the rest are metadata-only
        }
        return sk;
    } catch (Underrun&) {
        if (error) *error = QStringLiteral("truncated .ast");
        sk.valid = false;
        return sk;
    }
}

AstSkeleton::Clip AstSkeleton::retargetClip(const Skeleton& srcSkel, const Clip& srcClip, const Skeleton& dstSkel)
{
    // name → index in the destination rig.
    QHash<QString, int> dstIndex;
    dstIndex.reserve(dstSkel.bones.size());
    for (int i = 0; i < dstSkel.bones.size(); ++i) dstIndex.insert(dstSkel.bones[i].name, i);

    Clip out;
    out.name = srcClip.name; out.parentName = srcClip.parentName;
    out.fps = srcClip.fps; out.offset = srcClip.offset; out.size = srcClip.size;
    out.keys.reserve(srcClip.keys.size());
    for (const KeySet& ks : srcClip.keys) {
        // The source KeySet's nodeId indexes srcSkel's bones; map that bone's NAME to the dst index.
        if (ks.nodeId < 0 || ks.nodeId >= srcSkel.bones.size()) continue;
        const auto it = dstIndex.constFind(srcSkel.bones[ks.nodeId].name);
        if (it == dstIndex.constEnd()) continue;     // dst rig lacks this bone → drop the channel
        KeySet copy = ks;
        copy.nodeId = it.value();
        out.keys.append(copy);
    }
    return out;
}

namespace {
// Largest keyframe time (in the clip's own frame units) across a clip's KeySets.
float maxKeyTime(const AstSkeleton::Clip& c)
{
    float m = 0;
    for (const AstSkeleton::KeySet& k : c.keys) {
        if (!k.posTimes.isEmpty())   m = qMax(m, k.posTimes.last());
        if (!k.rotTimes.isEmpty())   m = qMax(m, k.rotTimes.last());
        if (!k.scaleTimes.isEmpty()) m = qMax(m, k.scaleTimes.last());
    }
    return m;
}
// Find the segment [i,i+1] in ascending `times` bracketing frame-time ft, returning the index i and
// the 0..1 blend u. Clamps at both ends. Returns -1 when there are no times.
int seg(const QVector<float>& times, float ft, float& u)
{
    u = 0;
    const int n = times.size();
    if (n == 0) return -1;
    if (ft <= times.first() || n == 1) return 0;
    if (ft >= times.last()) return n - 1;
    int i = 0; while (i + 1 < n && times[i+1] <= ft) ++i;
    const float t0 = times[i], t1 = times[qMin(i+1, n-1)];
    u = (t1 > t0) ? (ft - t0) / (t1 - t0) : 0.0f;
    return i;
}
}  // namespace

float AstSkeleton::clipDuration(const Skeleton& sk, int clipIndex)
{
    if (clipIndex < 0 || clipIndex >= sk.clips.size()) return 0;
    const Clip& c = sk.clips[clipIndex];
    const int fps = c.fps > 0 ? c.fps : 30;
    return maxKeyTime(c) / float(fps);
}

QVector<RigMath::Mat4> AstSkeleton::skinMatrices(const Skeleton& sk, int clipIndex, float t)
{
    const int nb = sk.bones.size();
    QVector<RigMath::Mat4> skin(nb, RigMath::identity());
    if (nb == 0) return skin;

    const bool haveClip = clipIndex >= 0 && clipIndex < sk.clips.size() && !sk.clips[clipIndex].keys.isEmpty();

    // Default local transform per bone: L0 = bind · inverse(parentBind) (v·M, world = local·parentWorld).
    QVector<RigMath::Mat4> local(nb);
    for (int b = 0; b < nb; ++b) {
        const Bone& bone = sk.bones[b];
        local[b] = (bone.parent >= 0 && bone.parent < nb)
                     ? RigMath::mul(bone.bind, sk.bones[bone.parent].inverseBind)
                     : bone.bind;
    }

    if (haveClip) {
        const Clip& c = sk.clips[clipIndex];
        const int fps = c.fps > 0 ? c.fps : 30;
        const float ft = t * float(fps);   // keyframe times are in frame units
        // Map nodeId → KeySet.
        for (const KeySet& k : c.keys) {
            if (k.nodeId < 0 || k.nodeId >= nb) continue;
            // Start from the bind-local's TRS so channels the clip omits keep their bind value.
            float tt[3], qq[4], ss[3];
            RigMath::decomposeTRS(local[k.nodeId], tt, qq, ss);
            float u;
            int i = seg(k.posTimes, ft, u);
            if (i >= 0) {
                const auto& a = k.pos[i]; const auto& bnext = k.pos[qMin(i+1, k.pos.size()-1)];
                for (int j = 0; j < 3; ++j) tt[j] = a[j]*(1-u) + bnext[j]*u;
            }
            i = seg(k.scaleTimes, ft, u);
            if (i >= 0) {
                const auto& a = k.scale[i]; const auto& bnext = k.scale[qMin(i+1, k.scale.size()-1)];
                for (int j = 0; j < 3; ++j) ss[j] = a[j]*(1-u) + bnext[j]*u;
            }
            i = seg(k.rotTimes, ft, u);
            if (i >= 0) {
                const float* a = k.rot[i].data(); const float* bnext = k.rot[qMin(i+1, k.rot.size()-1)].data();
                RigMath::quatNlerp(a, bnext, u, qq);
            }
            local[k.nodeId] = RigMath::composeTRS(tt, qq, ss);
        }
    }

    // Compose world = local · parentWorld, resolving parents first (bones need not be pre-sorted).
    QVector<RigMath::Mat4> world(nb);
    QVector<char> done(nb, 0), inProgress(nb, 0);
    std::function<const RigMath::Mat4&(int)> resolve = [&](int b) -> const RigMath::Mat4& {
        if (done[b]) return world[b];
        if (inProgress[b]) { world[b] = local[b]; done[b] = 1; return world[b]; }   // parent cycle → root
        inProgress[b] = 1;
        const int p = sk.bones[b].parent;
        world[b] = (p >= 0 && p < nb && p != b) ? RigMath::mul(local[b], resolve(p)) : local[b];
        done[b] = 1; inProgress[b] = 0;
        return world[b];
    };
    for (int b = 0; b < nb; ++b) resolve(b);

    // skin = inverseBind · animatedWorld (identity at the bind pose).
    for (int b = 0; b < nb; ++b) skin[b] = RigMath::mul(sk.bones[b].inverseBind, world[b]);
    return skin;
}

QString AstSkeleton::selfTest()
{
    // Build a minimal v12 skeleton: 2 bones (root, child), no lights, no clips.
    QByteArray d;
    auto put8 = [&](uint8_t v){ d.append(char(v)); };
    auto putf = [&](float f){ uint32_t v; std::memcpy(&v, &f, 4); v = qToLittleEndian(v); d.append(reinterpret_cast<char*>(&v), 4); };
    put8(12); put8(2); put8(0); put8(0); put8(0); put8(0); put8(0); put8(0);
    // bone 0: sibling 255, child 1, identity, name "root"
    put8(255); put8(1); for (int i=0;i<16;++i) putf(i%5==0?1.f:0.f); put8(4); put8(0); d.append("root");
    // bone 1: sibling 255, child 255, translate (1,2,3), name "hip"
    put8(255); put8(255);
    RigMath::Mat4 m = RigMath::identity(); m[12]=1; m[13]=2; m[14]=3;
    for (int i=0;i<16;++i) putf(m[i]); put8(3); put8(0); d.append("hip");
    QString err; Skeleton sk = parse(d, false, &err);
    if (!sk.valid || sk.bones.size() != 2 || sk.bones[1].parent != 0 || sk.bones[1].name != QStringLiteral("hip"))
        return QStringLiteral("AstSkeleton self-test: parse wrong (bones=%1 parent=%2)").arg(sk.bones.size()).arg(sk.bones.size()>1?sk.bones[1].parent:-99);
    float t[3]; RigMath::translationOf(sk.bones[1].bind, t);
    if (t[0] != 1 || t[1] != 2 || t[2] != 3) return QStringLiteral("AstSkeleton self-test: bind translation wrong");

    // (a) composeTRS ∘ decomposeTRS is the identity on a translate·rotate·scale matrix.
    {
        const float tr[3]={2.0f,-3.0f,0.5f}, sc[3]={1.5f,0.75f,2.0f};
        float qi[4]={0.1f,0.2f,0.3f,0.9f}; const float qn=std::sqrt(qi[0]*qi[0]+qi[1]*qi[1]+qi[2]*qi[2]+qi[3]*qi[3]);
        for (float& q : qi) q/=qn;
        const RigMath::Mat4 M = RigMath::composeTRS(tr, qi, sc);
        float t2[3],q2[4],s2[3]; RigMath::decomposeTRS(M, t2, q2, s2);
        const RigMath::Mat4 M2 = RigMath::composeTRS(t2, q2, s2);
        for (int i = 0; i < 16; ++i) if (std::fabs(M[i]-M2[i]) > 1e-4f)
            return QStringLiteral("AstSkeleton self-test: TRS roundtrip drift at %1 (%2 vs %3)").arg(i).arg(M[i]).arg(M2[i]);
    }
    // (b) Bind-pose skinning is the identity: no clip, or a clip whose keys equal the bind-local,
    //     must give skin[b] ≈ I so a skinned bind vertex equals the static one.
    {
        // Give the child a non-trivial rotated+scaled bind so the test bites.
        Skeleton s2 = sk;
        float trc[3]={1,2,3}; float qc[4]={0,0,0.3826834f,0.9238795f}; float scc[3]={1,1,1}; // 45° about Z
        // world bind: root = I, child = local(child)·rootWorld = local(child)
        RigMath::Mat4 childLocal = RigMath::composeTRS(trc, qc, scc);
        s2.bones[0].bind = RigMath::identity(); s2.bones[0].inverseBind = RigMath::identity();
        s2.bones[1].bind = childLocal; s2.bones[1].inverseBind = RigMath::inverse(childLocal);
        QVector<RigMath::Mat4> skin = skinMatrices(s2, /*clip*/-1, 0.0f);
        if (skin.size() != 2) return QStringLiteral("AstSkeleton self-test: skinMatrices size");
        for (int b = 0; b < 2; ++b) { const RigMath::Mat4 I = RigMath::identity();
            for (int i = 0; i < 16; ++i) if (std::fabs(skin[b][i]-I[i]) > 1e-4f)
                return QStringLiteral("AstSkeleton self-test: bind-pose skin[%1] not identity at %2 (%3)").arg(b).arg(i).arg(skin[b][i]);
        }
        // (c) A single-key clip that reproduces each bone's bind-local must also give identity skin.
        s2.clips.clear(); Clip c; c.name=QStringLiteral("t"); c.fps=30;
        for (int b = 0; b < 2; ++b) {
            RigMath::Mat4 L0 = (b==0) ? s2.bones[0].bind : RigMath::mul(s2.bones[1].bind, s2.bones[0].inverseBind);
            float lt[3],lq[4],ls[3]; RigMath::decomposeTRS(L0, lt, lq, ls);
            KeySet k; k.nodeId=b; k.posTimes={0}; k.pos={{lt[0],lt[1],lt[2]}};
            k.rotTimes={0}; k.rot={{lq[0],lq[1],lq[2],lq[3]}}; k.scaleTimes={0}; k.scale={{ls[0],ls[1],ls[2]}};
            c.keys.append(k);
        }
        s2.clips.append(c); s2.clipsDecoded=true;
        QVector<RigMath::Mat4> skin2 = skinMatrices(s2, 0, 0.0f);
        for (int b = 0; b < 2; ++b) { const RigMath::Mat4 I = RigMath::identity();
            for (int i = 0; i < 16; ++i) if (std::fabs(skin2[b][i]-I[i]) > 1e-3f)
                return QStringLiteral("AstSkeleton self-test: bind-key skin[%1] not identity at %2 (%3)").arg(b).arg(i).arg(skin2[b][i]);
        }
    }
    return QString();
}
