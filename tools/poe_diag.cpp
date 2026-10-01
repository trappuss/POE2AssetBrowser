// poe_diag — container-only rig/export diagnostic. Loads a decompressed .smd/.fmt (optionally its
// .ast rig and .sm material descriptor) with the app's OWN parsers and exporter, then prints a
// rig-validation report and, with --glb, writes a .glb. It exists so a headless session can verify
// skeleton/export correctness — the numbers that used to require a Blender round-trip — without a
// second GUI. It links the same library the app does, so it can never drift from shipping behaviour.
//
// Why validate in the NATIVE frame: the exporter's node graph is consistent BY CONSTRUCTION as long
// as (1) inverseBind == inverse(bind) and (2) the parent-relative decomposition round-trips. The
// Y-up basis change (R·M·R⁻¹) and the uniform unitScale are similarities that preserve those
// invariants (proven separately), so checking them in native space is sufficient and cleaner. The
// checks below are the exact ones that would have caught the parent-local-bind regression:
//   A  inverseBind fidelity   max‖bind·inverseBind − I‖            (stale/mismatched inverse → huge)
//   B  node round-trip        recompose global from parent-locals, max‖global − bind‖
//   C  skinned rest == mesh   Σ wᵢ·(pos·IBM[jᵢ]·global[jᵢ]) vs pos (mesh must not deform at rest)
//   D  bone world spread      translation min/max + unique-position count ("ball at origin" guard)
//
// Direct-file mode only (no bundle index): pass already-decompressed files. usage:
//   poe_diag <model.smd|.fmt> [--ast rig.ast] [--sm mesh.sm] [--glb out.glb] [--scale S] [--clips]

#include "model/MeshParser.h"
#include "model/AstSkeleton.h"
#include "model/AssetText.h"
#include "model/GlbExporter.h"
#include "model/RigMath.h"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

QByteArray readAll(const QString& path, bool* ok)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *ok = false; return {}; }
    *ok = true;
    return f.readAll();
}

// max element-wise |a − b|
float maxDiff(const RigMath::Mat4& a, const RigMath::Mat4& b)
{
    float m = 0;
    for (int i = 0; i < 16; ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

struct RigMetrics {
    float maxInvErr = 0;  int worstInv = -1;   // A: max‖bind·inverseBind − I‖
    float maxRtErr  = 0;  int worstRt  = -1;   // B: max‖recomposed global − bind‖
    float maxSkinDev = 0; long  sampled = 0;   // C: max‖skinned-rest − mesh pos‖
    float tmin[3] = {0,0,0}, tmax[3] = {0,0,0};// D: bone translation spread
    int   distinct = 0;                        // D: distinct rounded bone positions
    QVector<RigMath::Mat4> global;             // recomposed model-space globals
    bool passA = false, passB = false, passC = false, passD = false;
    bool skinned = false;
    bool allPass() const { return passA && passB && passC && passD; }
};

// The rig invariants the exporter depends on, computed in the native frame (Y-up + unitScale are
// identity-preserving similarities). `modelExtent` scales check C's tolerance to the model size.
RigMetrics validateRig(const AstSkeleton::Skeleton& sk, const ModelGeometry& geo, float modelExtent)
{
    RigMetrics r;
    const int nb = sk.bones.size();
    if (nb == 0) return r;
    r.skinned = geo.skinned;

    // A — inverseBind fidelity.
    for (int b = 0; b < nb; ++b) {
        const float e = maxDiff(RigMath::mul(sk.bones[b].bind, sk.bones[b].inverseBind), RigMath::identity());
        if (e > r.maxInvErr) { r.maxInvErr = e; r.worstInv = b; }
    }

    // B — recompose each bone's global from parent-relative locals, compare to stored bind.
    //     local[b] = bind[b] · inverse(bind[parent]); global[b] = local[b] · global[parent].
    r.global.resize(nb);
    QVector<char> done(nb, 0);
    std::function<RigMath::Mat4(int)> comp = [&](int b) -> RigMath::Mat4 {
        if (done[b]) return r.global[b];
        const int p = sk.bones[b].parent;
        RigMath::Mat4 local = sk.bones[b].bind;
        if (p >= 0 && p < nb) local = RigMath::mul(sk.bones[b].bind, sk.bones[p].inverseBind);
        r.global[b] = (p >= 0 && p < nb) ? RigMath::mul(local, comp(p)) : local;
        done[b] = 1;
        return r.global[b];
    };
    for (int b = 0; b < nb; ++b) {
        const float e = maxDiff(comp(b), sk.bones[b].bind);
        if (e > r.maxRtErr) { r.maxRtErr = e; r.worstRt = b; }
    }

    // C — skinned rest position vs mesh position, over a bounded sample of skinned vertices.
    if (geo.skinned) {
        const int step = std::max(1, int(geo.vertices.size() / 20000));
        for (int vi = 0; vi < geo.vertices.size(); vi += step) {
            const MeshVertex& v = geo.vertices[vi];
            float acc[3] = {0, 0, 0}; float wsum = 0;
            for (int k = 0; k < 4; ++k) {
                const float w = v.weights[k];
                if (w <= 0) continue;
                const int j = v.joints[k];
                if (j < 0 || j >= nb) continue;
                const RigMath::Mat4 skin = RigMath::mul(sk.bones[j].inverseBind, r.global[j]);
                float p[3]; RigMath::transformPoint(skin, v.px, v.py, v.pz, p);
                acc[0] += w * p[0]; acc[1] += w * p[1]; acc[2] += w * p[2]; wsum += w;
            }
            if (wsum > 0) {
                const float d = std::max({std::fabs(acc[0] - v.px), std::fabs(acc[1] - v.py), std::fabs(acc[2] - v.pz)});
                r.maxSkinDev = std::max(r.maxSkinDev, d);
                ++r.sampled;
            }
        }
    }

    // D — bone world-position spread + distinct-position count ("ball at origin" guard).
    for (int a = 0; a < 3; ++a) { r.tmin[a] = 1e30f; r.tmax[a] = -1e30f; }
    for (const auto& b : sk.bones)
        for (int a = 0; a < 3; ++a) { r.tmin[a] = std::min(r.tmin[a], b.bind[12 + a]); r.tmax[a] = std::max(r.tmax[a], b.bind[12 + a]); }
    QVector<std::array<int,3>> seen;
    for (const auto& b : sk.bones) {
        std::array<int,3> key{int(std::lround(b.bind[12])), int(std::lround(b.bind[13])), int(std::lround(b.bind[14]))};
        if (!seen.contains(key)) seen.append(key);
    }
    r.distinct = seen.size();

    const float extentThresh = 1e-2f * std::max(modelExtent, 1.0f);
    r.passA = r.maxInvErr < 1e-3f;
    r.passB = r.maxRtErr  < 1e-3f;
    r.passC = !geo.skinned || r.maxSkinDev < extentThresh;
    r.passD = r.distinct > std::max(2, nb / 8);
    return r;
}

// Parse a written .glb and check every accessor/bufferView reference is in range — the check my
// earlier Python validators lacked, which let a corrupt-animation glb (samplers pointing at
// accessors that were never emitted) pass unnoticed until Blender's importer crashed on it.
// Returns the number of problems (0 = valid). Prints a summary line.
int validateGlbRefs(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { printf("  glbcheck: open failed\n"); return 1; }
    const QByteArray data = f.readAll();
    if (data.size() < 20 || data.left(4) != "glTF") { printf("  glbcheck: not a glb\n"); return 1; }
    // JSON chunk starts at byte 20 (12 header + 8 chunk header); its length is at byte 12.
    const quint32 jsonLen = quint32(uchar(data[12])) | (quint32(uchar(data[13]))<<8) | (quint32(uchar(data[14]))<<16) | (quint32(uchar(data[15]))<<24);
    const QByteArray jsonBytes = data.mid(20, jsonLen);
    const QJsonObject root = QJsonDocument::fromJson(jsonBytes).object();
    const int nAcc = root.value("accessors").toArray().size();
    const int nBv  = root.value("bufferViews").toArray().size();
    int bad = 0, badAnim = 0;
    for (const QJsonValue& av : root.value("accessors").toArray())
        if (av.toObject().contains("bufferView") && av.toObject().value("bufferView").toInt() >= nBv) ++bad;
    for (const QJsonValue& anv : root.value("animations").toArray()) {
        const QJsonArray samplers = anv.toObject().value("samplers").toArray();
        for (const QJsonValue& sv : samplers) {
            const QJsonObject s = sv.toObject();
            if (s.value("input").toInt()  >= nAcc || s.value("input").toInt()  < 0) ++badAnim;
            if (s.value("output").toInt() >= nAcc || s.value("output").toInt() < 0) ++badAnim;
        }
        for (const QJsonValue& cv : anv.toObject().value("channels").toArray()) {
            const int si = cv.toObject().value("sampler").toInt();
            if (si < 0 || si >= samplers.size()) ++badAnim;
        }
    }
    const int total = bad + badAnim;
    printf("  glbcheck: accessors=%d bufferViews=%d anims=%d  bad_accessor_refs=%d bad_anim_refs=%d  %s\n",
           nAcc, nBv, root.value("animations").toArray().size(), bad, badAnim, total == 0 ? "VALID" : "CORRUPT");
    return total;
}

RigMath::Mat4 makeRT(float qx, float qy, float qz, float qw, float tx, float ty, float tz)
{
    RigMath::Mat4 m = RigMath::quatToMat(qx, qy, qz, qw);
    m[12] = tx; m[13] = ty; m[14] = tz;
    return m;
}

// Deterministic self-test: build a known-good 3-bone rig + skinned mesh, then two known-broken
// variants, and assert validateRig reports PASS on the good one and FAIL on each break. Needs no
// game data, so it is a permanent regression guard for the diagnostic's own discriminating power.
int runSelfTest()
{
    // Correct rig: parent-local transforms accumulated to model-space bind (mirrors AstSkeleton::parse
    // after the parent-local-bind fix). inverseBind = inverse(model-space bind).
    const RigMath::Mat4 L0 = makeRT(0, 0, 0, 1,               0,   0,   0);   // root
    const RigMath::Mat4 L1 = makeRT(0, 0, 0.2588f, 0.9659f,   0, 100,   0);   // 30° about Z, up 100
    const RigMath::Mat4 L2 = makeRT(0.2588f, 0, 0, 0.9659f,   0,  80,  10);   // 30° about X

    AstSkeleton::Skeleton sk; sk.version = 12; sk.valid = true;
    auto push = [&](const QString& name, int parent) { AstSkeleton::Bone b; b.name = name; b.parent = parent; sk.bones.append(b); };
    push("root", -1); push("spine", 0); push("head", 1);
    sk.bones[0].bind = L0;
    sk.bones[1].bind = RigMath::mul(L1, sk.bones[0].bind);
    sk.bones[2].bind = RigMath::mul(L2, sk.bones[1].bind);
    for (auto& b : sk.bones) b.inverseBind = RigMath::inverse(b.bind);

    // Skinned mesh: a few verts, each fully weighted to one bone, offset from that bone's origin.
    ModelGeometry geo; geo.skinned = true;
    for (int j = 0; j < sk.bones.size(); ++j)
        for (int k = 0; k < 4; ++k) {
            MeshVertex v;
            v.px = sk.bones[j].bind[12] + k * 3.0f;
            v.py = sk.bones[j].bind[13] - k * 2.0f;
            v.pz = sk.bones[j].bind[14] + k * 1.0f;
            v.joints[0] = uint8_t(j); v.weights[0] = 1.0f;
            geo.vertices.append(v);
        }
    for (int t = 0; t + 2 < geo.vertices.size(); t += 3) { geo.indices.append(t); geo.indices.append(t+1); geo.indices.append(t+2); }
    { MeshPart p; p.name = "test"; p.indexStart = 0; p.indexCount = geo.indices.size(); geo.parts.append(p); }
    geo.computeBounds();
    const float ext = std::max({geo.bboxMax[0]-geo.bboxMin[0], geo.bboxMax[1]-geo.bboxMin[1], geo.bboxMax[2]-geo.bboxMin[2]});

    int fails = 0;
    auto expect = [&](const char* what, bool got, bool want) {
        const bool ok = (got == want);
        printf("  %-42s got=%s want=%s  %s\n", what, got ? "1" : "0", want ? "1" : "0", ok ? "ok" : "MISMATCH");
        if (!ok) ++fails;
    };

    // 1) Good rig — everything passes.
    RigMetrics g = validateRig(sk, geo, ext);
    printf("case good      A=%.2g B=%.2g C=%.2g distinct=%d\n", g.maxInvErr, g.maxRtErr, g.maxSkinDev, g.distinct);
    expect("good.passA (inverseBind fidelity)", g.passA, true);
    expect("good.passB (node round-trip)",      g.passB, true);
    expect("good.passC (skinned rest)",         g.passC, true);
    expect("good.passD (bone spread)",          g.passD, true);

    // 2) Stale inverseBind on a leaf (the real pre-fix regression: inverse of the LOCAL, not the
    //    accumulated model-space bind). A must FAIL; C must FAIL (that vertex deforms at rest).
    {
        AstSkeleton::Skeleton bad = sk;
        bad.bones[2].inverseBind = RigMath::inverse(L2);          // wrong: local, not model-space
        RigMetrics r = validateRig(bad, geo, ext);
        printf("case staleInv  A=%.4g C=%.4g\n", r.maxInvErr, r.maxSkinDev);
        expect("staleInv.passA is FAIL", r.passA, false);
        expect("staleInv.passC is FAIL", r.passC, false);
    }

    // 3) "Ball at origin": every bone collapsed to (0,0,0) with a consistent inverse (so A passes).
    //    Only D distinguishes it. D must FAIL.
    {
        AstSkeleton::Skeleton bad = sk;
        for (auto& b : bad.bones) { b.bind[12] = b.bind[13] = b.bind[14] = 0; b.inverseBind = RigMath::inverse(b.bind); }
        RigMetrics r = validateRig(bad, geo, ext);
        printf("case ball      A=%.2g distinct=%d\n", r.maxInvErr, r.distinct);
        expect("ball.passA is PASS", r.passA, true);
        expect("ball.passD is FAIL", r.passD, false);
    }

    // 4) Export a glb WITH an animation clip and re-read it — reproduces the accessor-ordering bug
    //    class (animation samplers referencing accessors that were never emitted).
    {
        AstSkeleton::Skeleton anim = sk;
        AstSkeleton::Clip clip; clip.name = "test_clip"; clip.fps = 30;
        for (int j = 0; j < anim.bones.size(); ++j) {
            AstSkeleton::KeySet ks; ks.nodeId = j;
            ks.rotTimes = {0.0f, 1.0f};
            ks.rot = { {0,0,0,1}, {0,0,0.2588f,0.9659f} };
            ks.posTimes = {0.0f, 1.0f};
            ks.pos = { {anim.bones[j].bind[12], anim.bones[j].bind[13], anim.bones[j].bind[14]},
                       {anim.bones[j].bind[12], anim.bones[j].bind[13], anim.bones[j].bind[14]} };
            clip.keys.append(ks);
        }
        anim.clips.append(clip); anim.clipsDecoded = true;

        GlbExporter::Options opt; opt.embedTextures = false; opt.includeAnimations = true;
        QVector<GlbExporter::ExportMaterial> mats;
        const QString out = "/tmp/poe_diag_selftest.glb";
        QString gerr;
        if (GlbExporter::write(geo, anim, mats, opt, out, &gerr)) {
            const int refs = validateGlbRefs(out);
            expect("exported glb animation refs all in range", refs == 0, true);
        } else {
            printf("  export FAIL: %s\n", qPrintable(gerr)); ++fails;
        }
    }

    printf("SELFTEST %s (%d mismatch%s)\n", fails == 0 ? "PASS" : "FAIL", fails, fails == 1 ? "" : "es");
    return fails == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == "--selftest") return runSelfTest();
    if (argc < 2) {
        fprintf(stderr,
            "usage: poe_diag <model.smd|.fmt> [--ast rig.ast] [--sm mesh.sm] [--glb out.glb] [--scale S] [--clips]\n");
        return 2;
    }

    QString modelPath, astPath, smPath, glbPath;
    float scale = GlbExporter::Options{}.unitScale;   // report against the shipping default
    bool decodeClips = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if      (a == "--ast"   && i + 1 < argc) astPath = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--sm"    && i + 1 < argc) smPath  = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--glb"   && i + 1 < argc) glbPath = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--scale" && i + 1 < argc) scale   = QString::fromLocal8Bit(argv[++i]).toFloat();
        else if (a == "--clips")                 decodeClips = true;
        else if (!a.startsWith("--"))            modelPath = a;
    }
    if (modelPath.isEmpty()) { fprintf(stderr, "no model path\n"); return 2; }

    // ── Mesh ────────────────────────────────────────────────────────────────────────────────────
    bool ok = false;
    const QByteArray meshData = readAll(modelPath, &ok);
    if (!ok) { fprintf(stderr, "open failed: %s\n", qPrintable(modelPath)); return 1; }
    ModelGeometry geo; QString err;
    const bool isFmt = modelPath.toLower().endsWith(".fmt");
    const bool parsed = isFmt ? MeshParser::parseFmt(meshData, modelPath, geo, &err)
                              : MeshParser::parseSmd(meshData, modelPath, geo, &err);
    if (!parsed) { printf("PARSE FAIL: %s\n", qPrintable(err)); return 1; }

    // Materials from an explicit .sm (mirrors AssetStore::loadModel's run-assignment).
    int matsAssigned = 0;
    if (!smPath.isEmpty() && !isFmt) {
        const QByteArray smData = readAll(smPath, &ok);
        if (ok && !smData.isEmpty()) {
            const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(smData);
            int part = 0;
            for (const auto& run : sm.materials) {
                int mi = geo.materialPaths.indexOf(run.matPath);
                if (mi < 0) { mi = geo.materialPaths.size(); geo.materialPaths.append(run.matPath); }
                for (int k = 0; k < run.count && part < geo.parts.size(); ++k, ++part) {
                    geo.parts[part].material = run.matPath;
                    geo.parts[part].materialIndex = mi;
                    ++matsAssigned;
                }
            }
        }
    }

    // ── Skeleton ────────────────────────────────────────────────────────────────────────────────
    AstSkeleton::Skeleton sk;
    if (!astPath.isEmpty()) {
        const QByteArray astData = readAll(astPath, &ok);
        if (!ok || astData.isEmpty()) { fprintf(stderr, "ast open failed: %s\n", qPrintable(astPath)); return 1; }
        sk = AstSkeleton::parse(astData, decodeClips, &err);
        if (!sk.valid) printf("AST NOTE: %s\n", qPrintable(err.isEmpty() ? sk.note : err));
    }

    // ── Geometry report ─────────────────────────────────────────────────────────────────────────
    const float ex = geo.bboxMax[0] - geo.bboxMin[0];
    const float ey = geo.bboxMax[1] - geo.bboxMin[1];
    const float ez = geo.bboxMax[2] - geo.bboxMin[2];
    printf("model   %s\n", qPrintable(modelPath));
    printf("  fmtVer=%d vf=0x%x verts=%d tris=%d parts=%d mats=%d skinned=%d\n",
           geo.formatVersion, geo.vertexFormat, geo.vertices.size(), geo.triangleCount(),
           geo.parts.size(), geo.materialPaths.size(), geo.skinned ? 1 : 0);
    printf("  bbox native  min=(%.1f %.1f %.1f) max=(%.1f %.1f %.1f) extent=(%.1f %.1f %.1f)\n",
           geo.bboxMin[0], geo.bboxMin[1], geo.bboxMin[2], geo.bboxMax[0], geo.bboxMax[1], geo.bboxMax[2], ex, ey, ez);
    printf("  bbox @scale=%.4f  extent=(%.3f %.3f %.3f) m   (max axis %.3f m)\n",
           scale, ex * scale, ey * scale, ez * scale, std::max({ex, ey, ez}) * scale);
    if (!smPath.isEmpty()) printf("  materials assigned to %d parts from .sm\n", matsAssigned);

    // ── Rig validation ──────────────────────────────────────────────────────────────────────────
    int rigStatus = 0;   // 0 = ok / n-a, nonzero = fail
    if (!sk.bones.isEmpty()) {
        const int nb = sk.bones.size();
        printf("skeleton  version=%d bones=%d clips=%d clipsDecoded=%d\n",
               sk.version, nb, sk.clips.size(), sk.clipsDecoded ? 1 : 0);

        const RigMetrics r = validateRig(sk, geo, std::max({ex, ey, ez}));
        const float extentThresh = 1e-2f * std::max({ex, ey, ez, 1.0f});
        printf("  A inverseBind  max|bind*inv - I| = %.6g  (worst bone %d)  %s\n",
               r.maxInvErr, r.worstInv, r.passA ? "PASS" : "FAIL");
        printf("  B node graph   max|global-bind|  = %.6g  (worst bone %d)  %s\n",
               r.maxRtErr, r.worstRt, r.passB ? "PASS" : "FAIL");
        if (geo.skinned)
            printf("  C skinned rest max|skinned-pos|  = %.6g over %ld verts (thresh %.4g)  %s\n",
                   r.maxSkinDev, r.sampled, extentThresh, r.passC ? "PASS" : "FAIL");
        else
            printf("  C skinned rest  n/a (mesh not skinned)\n");
        printf("  D bone spread  X[%.1f,%.1f] Y[%.1f,%.1f] Z[%.1f,%.1f]  distinct=%d/%d  %s\n",
               r.tmin[0], r.tmax[0], r.tmin[1], r.tmax[1], r.tmin[2], r.tmax[2], r.distinct, nb, r.passD ? "PASS" : "FAIL");
        for (int b = 0; b < nb && b < 3; ++b)
            printf("    bone[%d] %-18s parent=%d pos=(%.1f %.1f %.1f)\n",
                   b, qPrintable(sk.bones[b].name), sk.bones[b].parent,
                   sk.bones[b].bind[12], sk.bones[b].bind[13], sk.bones[b].bind[14]);

        rigStatus = r.allPass() ? 0 : 1;
        printf("  RIG %s\n", rigStatus == 0 ? "OK" : "PROBLEM");
    } else if (!astPath.isEmpty()) {
        printf("skeleton  (no bones parsed)\n");
        rigStatus = 1;
    }

    // ── Optional .glb export ────────────────────────────────────────────────────────────────────
    if (!glbPath.isEmpty()) {
        GlbExporter::Options opt;
        opt.unitScale = scale;
        opt.embedTextures = false;                              // rig/scale diagnostic — skip textures
        opt.includeAnimations = sk.clipsDecoded;                // only if we actually decoded clips
        QVector<GlbExporter::ExportMaterial> mats;
        for (const QString& mp : geo.materialPaths) { GlbExporter::ExportMaterial em; em.name = mp.section('/', -1); mats.append(em); }
        QString gerr;
        if (GlbExporter::write(geo, sk, mats, opt, glbPath, &gerr)) {
            QFile gf(glbPath);
            printf("glb     wrote %s  (%lld bytes, scale=%.4f, anims=%d)\n",
                   qPrintable(glbPath), gf.exists() ? gf.size() : -1, scale, opt.includeAnimations ? 1 : 0);
            if (validateGlbRefs(glbPath) != 0) rigStatus = 1;   // corrupt refs → nonzero exit
        } else {
            printf("glb     WRITE FAIL: %s\n", qPrintable(gerr));
            return 1;
        }
    }

    return rigStatus;
}
