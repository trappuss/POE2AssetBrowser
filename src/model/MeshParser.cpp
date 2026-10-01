#include "model/MeshParser.h"

#include <QtEndian>
#include <cmath>
#include <cstring>
#include <new>

namespace {

// A bounds-checked little-endian reader over a QByteArray. Every read that would run past the end
// throws Underrun, which the parsers turn into a clean "truncated" error instead of a crash — the
// whole point of a browser for opaque binary is that one bad file reports rather than takes the app
// down (docs/ASSETBROWSER_TEMPLATE §1, §2 SEH-guard rule; this is the cheap first line).
struct Underrun {};
struct Reader {
    const uint8_t* p; size_t n; size_t off = 0;
    Reader(const QByteArray& b) : p(reinterpret_cast<const uint8_t*>(b.constData())), n(size_t(b.size())) {}
    void need(size_t k) const { if (off + k > n) throw Underrun{}; }
    size_t remaining() const { return off <= n ? n - off : 0; }
    uint8_t  u8()  { need(1); return p[off++]; }
    uint16_t u16() { need(2); uint16_t v; std::memcpy(&v, p+off, 2); off += 2; return qFromLittleEndian(v); }
    uint32_t u32() { need(4); uint32_t v; std::memcpy(&v, p+off, 4); off += 4; return qFromLittleEndian(v); }
    float    f32() { uint32_t v = u32(); float f; std::memcpy(&f, &v, 4); return f; }
    uint16_t f16bits() { return u16(); }
    void skip(size_t k) { need(k); off += k; }
    void seek(size_t a) { if (a > n) throw Underrun{}; off = a; }
    QString utf16(size_t bytes) { need(bytes); QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(p+off), int(bytes/2)); off += bytes; return s; }
};

float halfToFloat(uint16_t h)
{
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1F, mant = h & 0x3FF, bits;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else { exp = 127 - 15 + 1; while (!(mant & 0x400)) { mant <<= 1; --exp; } mant &= 0x3FF; bits = sign | (exp << 23) | (mant << 13); }
    } else if (exp == 0x1F) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float f; std::memcpy(&f, &bits, 4); return f;
}

// Decode one vertex from the raw stride according to `vf`, honouring the measured field order and
// the version-dependent normal encoding (v3/.fmt: signed int8/127; v1/v2: biased (b-128)/127).
void decodeVertex(Reader& r, uint32_t vf, bool biasedNormals, MeshVertex& out)
{
    auto sn = [&](uint8_t b) { return biasedNormals ? (float(b) - 128.0f) / 127.0f : float(int8_t(b)) / 127.0f; };
    if (vf & 0x20) { out.px = r.f32(); out.py = r.f32(); out.pz = r.f32(); }
    if (vf & 0x10) {
        uint8_t nx = r.u8(), ny = r.u8(), nz = r.u8(), nw = r.u8();
        uint8_t tx = r.u8(), ty = r.u8(), tz = r.u8(), tw = r.u8();
        out.nx = sn(nx); out.ny = sn(ny); out.nz = sn(nz);
        out.tx = sn(tx); out.ty = sn(ty); out.tz = sn(tz);
        out.tw = biasedNormals ? ((tw >= 128) ? 1.0f : -1.0f) : ((int8_t(tw) >= 0) ? 1.0f : -1.0f);
        (void)nw;
    }
    if (vf & 0x08) { out.u = halfToFloat(r.f16bits()); out.v = halfToFloat(r.f16bits()); }
    if (vf & 0x04) {
        out.joints[0] = r.u8(); out.joints[1] = r.u8(); out.joints[2] = r.u8(); out.joints[3] = r.u8();
        float w0 = r.u8(), w1 = r.u8(), w2 = r.u8(), w3 = r.u8();
        const float s = w0 + w1 + w2 + w3;
        const float inv = s > 0 ? 1.0f / s : 0.0f;
        out.weights[0] = w0 * inv; out.weights[1] = w1 * inv; out.weights[2] = w2 * inv; out.weights[3] = w3 * inv;
    }
    if (vf & 0x02) { out.color[0] = r.u8(); out.color[1] = r.u8(); out.color[2] = r.u8(); out.color[3] = r.u8(); }
    if (vf & 0x40) { r.skip(4); }   // unverified per-vertex u32 (docs/FORMATS.md §3.2)
    if (vf & 0x01) { out.u2 = halfToFloat(r.f16bits()); out.v2 = halfToFloat(r.f16bits()); }
}

// Read a .smd DOLm mesh section into parts + a merged index buffer. `dolmOffset` points at the
// 'DOLm' tag. (The .fmt v9 table differs — parseFmt reads its geometry inline.)
bool readDolm(Reader& r, size_t dolmOffset, bool isFmt, ModelGeometry& out,
              int& outVertexFormat, QString* error)
{
    r.seek(dolmOffset);
    r.need(4);
    if (std::memcmp(r.p + r.off, "DOLm", 4) != 0) { if (error) *error = QStringLiteral("no DOLm tag at 0x%1").arg(dolmOffset, 0, 16); return false; }
    r.skip(4);
    const uint8_t layoutVersion = r.u8();
    r.u8();                         // zero
    const uint8_t lodCount = r.u8();
    const uint8_t meshCount = r.u8();
    r.u8();                         // zero
    const uint32_t vf = r.u32();
    outVertexFormat = int(vf);
    out.vertexFormat = int(vf);
    out.skinned = (vf & 0x04) != 0;

    struct Lod { uint32_t tri, nv; };
    QVector<Lod> lods(lodCount);
    for (int i = 0; i < lodCount; ++i) { lods[i].tri = r.u32(); lods[i].nv = r.u32(); }
    const int stride = MeshParser::vertexStride(vf);

    // Only LOD 0 is loaded (the visible mesh); the tool reads later LODs on demand elsewhere.
    QVector<QVector<uint32_t>> partIndices;   // per-part, local vertex indices
    QVector<uint32_t> partStarts, partCounts;
    uint32_t baseVertex = 0;
    for (int li = 0; li < lodCount; ++li) {
        const uint32_t tri = lods[li].tri, nv = lods[li].nv;
        QVector<uint32_t> starts(meshCount), counts(meshCount);
        uint32_t nidx = 0;
        for (int m = 0; m < meshCount; ++m) { starts[m] = r.u32(); counts[m] = r.u32(); nidx += counts[m]; }
        if (nidx != tri * 3) { if (error) *error = QStringLiteral("index count %1 != tri*3 %2").arg(nidx).arg(tri*3); return false; }
        const bool u32idx = nv > 65535;
        const size_t idxStart = r.off;
        // Fail closed on a header that promises more geometry than the file can hold (template §2): a
        // count larger than the bytes left cannot be real, and an unchecked one drives a multi-GB
        // QVector allocation → bad_alloc. Bound the whole index block up front; each mesh's counts sum
        // to nidx, so this bounds every per-part idx allocation below too.
        if (idxStart + size_t(nidx) * (u32idx ? 4 : 2) > r.n) throw Underrun{};
        // Read indices then vertices for LOD 0 only.
        if (li == 0) {
            baseVertex = uint32_t(out.vertices.size());
            // vertices come AFTER the index block
            const size_t vtxStart = idxStart + size_t(nidx) * (u32idx ? 4 : 2);
            // parts
            for (int m = 0; m < meshCount; ++m) {
                QVector<uint32_t> idx(counts[m]);
                r.seek(idxStart + size_t(starts[m]) * (u32idx ? 4 : 2));
                for (uint32_t k = 0; k < counts[m]; ++k) idx[k] = u32idx ? r.u32() : r.u16();
                partIndices.append(idx);
                partStarts.append(starts[m]); partCounts.append(counts[m]);
            }
            r.seek(vtxStart);
            // A vertex-bearing mesh MUST have a positive stride; a zero-stride format (e.g. vf 0x200)
            // would read 0 bytes per vertex and loop nv times appending forever (never tripping the
            // per-read Underrun) → OOM. Reject it, and require the whole vertex block to fit.
            if (nv > 0 && stride <= 0) throw Underrun{};
            if (nv > 0) r.need(size_t(nv) * size_t(stride));
            const bool biased = !isFmt && out.formatVersion < 3;   // v1/v2 smd biased; v3 + fmt signed
            for (uint32_t v = 0; v < nv; ++v) {
                MeshVertex mv; decodeVertex(r, vf, biased, mv); out.vertices.append(mv);
            }
        } else {
            r.seek(idxStart + size_t(nidx) * (u32idx ? 4 : 2) + size_t(nv) * stride);
        }
        // per-LOD extra u32 when layoutVersion >= 4 (measured; docs §3.3)
        if (layoutVersion >= 4) r.u32();
    }

    // Merge parts into the shared index buffer with the LOD-0 base offset.
    for (int m = 0; m < partIndices.size(); ++m) {
        MeshPart part;
        part.indexStart = uint32_t(out.indices.size());
        part.indexCount = uint32_t(partIndices[m].size());
        for (uint32_t k : partIndices[m]) out.indices.append(baseVertex + k);
        out.parts.append(part);
    }

    // smd name table: u32 nameBytes[meshCount] then the concatenated UTF-16 names.
    Q_UNUSED(isFmt);
    QVector<uint32_t> lens(meshCount);
    for (int m = 0; m < meshCount; ++m) lens[m] = r.u32();
    for (int m = 0; m < meshCount && m < out.parts.size(); ++m)
        out.parts[m].name = r.utf16(lens[m]);
    return true;
}

}  // namespace

void ModelGeometry::computeBounds()
{
    if (vertices.isEmpty()) { for (int i = 0; i < 3; ++i) bboxMin[i] = bboxMax[i] = 0; return; }
    bboxMin[0] = bboxMax[0] = vertices[0].px;
    bboxMin[1] = bboxMax[1] = vertices[0].py;
    bboxMin[2] = bboxMax[2] = vertices[0].pz;
    for (const MeshVertex& v : vertices) {
        const float p[3] = {v.px, v.py, v.pz};
        for (int i = 0; i < 3; ++i) { if (p[i] < bboxMin[i]) bboxMin[i] = p[i]; if (p[i] > bboxMax[i]) bboxMax[i] = p[i]; }
    }
}

int MeshParser::vertexStride(uint32_t vf)
{
    int s = 0;
    if (vf & 0x20) s += 12;   // position f32x3
    if (vf & 0x10) s += 8;    // normal + tangent snorm8x4 each
    if (vf & 0x08) s += 4;    // uv f16x2
    if (vf & 0x04) s += 8;    // bone idx u8x4 + weight u8x4
    if (vf & 0x02) s += 4;    // colour rgba8
    if (vf & 0x40) s += 4;    // unverified u32
    if (vf & 0x01) s += 4;    // uv2 f16x2
    return s;
}

bool MeshParser::parseSmd(const QByteArray& data, const QString& sourcePath, ModelGeometry& out, QString* error)
{
    out = ModelGeometry();
    out.sourcePath = sourcePath;
    try {
        Reader r(data);
        const uint8_t version = r.u8();
        if (version != 1 && version != 2 && version != 3) { if (error) *error = QStringLiteral("unsupported .smd version %1").arg(version); return false; }
        out.formatVersion = version;

        if (version == 3) {
            r.u8();                        // b1 (==4)
            r.u16();                       // meshCount echo
            r.u32();                       // nameBytesTotal
            for (int i = 0; i < 6; ++i) r.f32();   // interleaved bbox
            int vf = 0;
            if (!readDolm(r, 0x20, /*isFmt*/false, out, vf, error)) return false;
        } else {
            // v1/v2 legacy: fixed 32-byte vertices, own name table (docs §3.5).
            const uint32_t tri = r.u32();
            const uint32_t nv  = r.u32();
            r.u8();                        // b9 (==4)
            const uint8_t meshCount = r.u8();
            r.u8();                        // b11
            r.u32();                       // nameBytesTotal
            for (int i = 0; i < 6; ++i) r.f32();
            if (version == 2) r.u32();     // v2 extra
            // Each mesh header carries its start as a TRIANGLE offset, not a raw index offset — the
            // v3/DOLm path stores index-unit starts, but v1/v2 stores triangle-unit starts. Measured
            // ground truth (tools/skin_diag + a per-part triangle-edge test on rig_b14ad7ab "coat" and
            // sin rig_0799337d): the starts (e.g. 0/293/398) are NOT multiples of 3, so treating them
            // as index offsets splits parts mid-triangle and re-groups every following index into the
            // wrong triple — the "vertex explosion" (max triangle edge 80 in a 90-unit coat). Reading
            // them as triangle offsets (×3) yields multiple-of-3 index runs and compact triangles
            // (max edge 14.6). Single-part meshes are unaffected either way.
            struct M { uint32_t nameBytes, triStart; };
            QVector<M> meshes(meshCount);
            for (int m = 0; m < meshCount; ++m) { meshes[m].nameBytes = r.u32(); meshes[m].triStart = r.u32(); }
            QVector<QString> names(meshCount);
            for (int m = 0; m < meshCount; ++m) names[m] = r.utf16(meshes[m].nameBytes);
            const size_t idxOff = r.off;
            // Bound the file-controlled counts before allocating (template §2): tri*3 in uint32 can
            // also overflow, so compare in size_t against the bytes actually left (2 per u16 index,
            // 32 per fixed v1/v2 vertex).
            if (size_t(tri) * 3 > r.remaining() / 2) throw Underrun{};
            QVector<uint32_t> allIdx(tri * 3);
            for (uint32_t k = 0; k < tri * 3; ++k) allIdx[k] = r.u16();
            if (nv > 0) r.need(size_t(nv) * 32);
            for (uint32_t v = 0; v < nv; ++v) { MeshVertex mv; decodeVertex(r, 0x3c, /*biased*/true, mv); out.vertices.append(mv); }
            out.vertexFormat = 0x3c; out.skinned = true;
            // Part boundaries: triangle offset × 3 → index offset, clamped monotonic and within range
            // so a malformed header can never over-read the index buffer (fail-closed, template §2).
            QVector<uint32_t> bounds(meshCount + 1);
            uint32_t prev = 0;
            for (int m = 0; m < meshCount; ++m) {
                uint32_t b = meshes[m].triStart * 3;
                if (b > tri * 3) b = tri * 3;
                if (b < prev)    b = prev;      // starts must be non-decreasing
                bounds[m] = b; prev = b;
            }
            bounds[meshCount] = tri * 3;
            for (int m = 0; m < meshCount; ++m) {
                MeshPart part; part.name = names[m];
                part.indexStart = uint32_t(out.indices.size());
                for (uint32_t k = bounds[m]; k < bounds[m+1]; ++k) out.indices.append(allIdx[k]);
                part.indexCount = uint32_t(out.indices.size()) - part.indexStart;
                out.parts.append(part);
            }
            Q_UNUSED(idxOff);
        }
        out.computeBounds();
        return true;
    } catch (Underrun&) {
        if (error) *error = QStringLiteral("%1: truncated .smd (file shorter than its header promises)").arg(sourcePath);
        return false;
    } catch (const std::bad_alloc&) {
        if (error) *error = QStringLiteral("%1: rejected — malformed .smd header asked for an impossible allocation").arg(sourcePath);
        out = ModelGeometry(); return false;
    }
}

bool MeshParser::parseFmt(const QByteArray& data, const QString& sourcePath, ModelGeometry& out, QString* error)
{
    out = ModelGeometry();
    out.sourcePath = sourcePath;
    try {
        Reader r(data);
        const uint8_t version = r.u8();
        if (version != 9) { if (error) *error = QStringLiteral(".fmt version %1 not supported yet (only v9)").arg(version); return false; }
        out.formatVersion = 9;
        r.u16();                          // meshCount echo
        const uint8_t nG = r.u8();        // locator group count
        const uint16_t nP = r.u16();      // locator point count
        const uint8_t flag = r.u8();      // extra-block flag
        if (flag) { if (error) *error = QStringLiteral("%1: .fmt carries an unparsed extra block (flag %2)").arg(sourcePath).arg(flag); return false; }
        for (int i = 0; i < 6; ++i) r.f32();   // bbox

        // Geometry via the shared DOLm reader; it stops before the fmt string block.
        int vf = 0;
        // We replicate the geometry portion inline because the fmt string table differs and needs
        // the head locator counts (readDolm handles only .smd tables).
        r.seek(0x1f);
        r.need(4);
        if (std::memcmp(r.p + r.off, "DOLm", 4) != 0) { if (error) *error = QStringLiteral("%1: no DOLm at 0x1f").arg(sourcePath); return false; }
        r.skip(4);
        const uint8_t layoutVersion = r.u8();
        r.u8();
        const uint8_t lodCount = r.u8();
        const uint8_t meshCount = r.u8();
        r.u8();
        vf = int(r.u32());
        out.vertexFormat = vf; out.skinned = (vf & 0x04) != 0;
        struct Lod { uint32_t tri, nv; };
        QVector<Lod> lods(lodCount);
        for (int i = 0; i < lodCount; ++i) { lods[i].tri = r.u32(); lods[i].nv = r.u32(); }
        const int stride = vertexStride(uint32_t(vf));
        QVector<uint32_t> partStart, partCount;
        uint32_t baseVertex = 0;
        for (int li = 0; li < lodCount; ++li) {
            const uint32_t tri = lods[li].tri, nv = lods[li].nv;
            QVector<uint32_t> starts(meshCount), counts(meshCount);
            uint32_t nidx = 0;
            for (int m = 0; m < meshCount; ++m) { starts[m] = r.u32(); counts[m] = r.u32(); nidx += counts[m]; }
            if (nidx != tri * 3) { if (error) *error = QStringLiteral("%1: fmt index count mismatch").arg(sourcePath); return false; }
            const bool u32idx = nv > 65535;
            const size_t idxStart = r.off;
            const size_t vtxStart = idxStart + size_t(nidx) * (u32idx ? 4 : 2);
            // Bound the whole index block against the file before allocating/looping (template §2).
            if (vtxStart > r.n) throw Underrun{};
            if (li == 0) {
                baseVertex = 0;
                for (int m = 0; m < meshCount; ++m) {
                    MeshPart part; part.indexStart = uint32_t(out.indices.size());
                    r.seek(idxStart + size_t(starts[m]) * (u32idx ? 4 : 2));
                    for (uint32_t k = 0; k < counts[m]; ++k) out.indices.append(baseVertex + (u32idx ? r.u32() : r.u16()));
                    part.indexCount = uint32_t(out.indices.size()) - part.indexStart;
                    out.parts.append(part);
                    partStart.append(starts[m]); partCount.append(counts[m]);
                }
                r.seek(vtxStart);
                // Reject a zero-stride vertex format (would append nv times reading nothing → OOM) and
                // require the whole vertex block to fit.
                if (nv > 0 && stride <= 0) throw Underrun{};
                if (nv > 0) r.need(size_t(nv) * size_t(stride));
                for (uint32_t v = 0; v < nv; ++v) { MeshVertex mv; decodeVertex(r, uint32_t(vf), /*biased*/false, mv); out.vertices.append(mv); }
            } else {
                r.seek(vtxStart + size_t(nv) * stride);
            }
            if (vf & 0x40) r.skip(36);   // 9 floats after the vertex block (docs §4.3)
            if (layoutVersion >= 4) r.u32();
        }

        // fmt string table: u32 zero, then per-mesh matOffset (chars), locators on the last mesh,
        // regionEnd; then the UTF-16 string block. (docs §4.1–4.2)
        r.u32();   // zero
        QVector<uint32_t> matOff(meshCount), regionEnd(meshCount);
        for (int m = 0; m < meshCount; ++m) {
            matOff[m] = r.u32();
            if (m == meshCount - 1 && nG) {
                // locator groups: {u8 1; u8 pointCount; u32 nameOffChars} then f32 xyz[nP]
                r.skip(size_t(nG) * 6);
                r.skip(size_t(nP) * 12);
            }
            regionEnd[m] = r.u32();
        }
        const size_t stringsStart = r.off;
        // The string block runs to end-of-file; its total char count is the last regionEnd. A
        // malformed .fmt (a flag/locator layout this build doesn't understand) can leave regionEnd
        // garbage, so bound every read against the actual bytes and bail cleanly rather than reading
        // past the buffer (template §1: fail closed, never crash on a bad asset).
        const uint32_t availChars = uint32_t((size_t(data.size()) - stringsStart) / 2);
        uint32_t prev = 0;
        for (int m = 0; m < meshCount && m < out.parts.size(); ++m) {
            const uint32_t a = matOff[m], e = regionEnd[m];
            if (e < prev || e > availChars) break;   // out-of-range region → stop naming, keep the geometry
            const QString region = QString::fromUtf16(reinterpret_cast<const char16_t*>(r.p + stringsStart + 2*prev), int(e - prev));
            const QString name = region.left(qMin<int>(int(a - prev), region.size())).section(QLatin1Char('\0'), 0, 0);
            QString mat;
            if (a >= prev && int(a - prev) < region.size())
                mat = region.mid(int(a - prev)).section(QLatin1Char('\0'), 0, 0);
            out.parts[m].name = name;
            out.parts[m].material = mat.toLower();
            prev = e;
        }
        // De-duplicate material paths into materialPaths + materialIndex.
        for (MeshPart& part : out.parts) {
            if (part.material.isEmpty()) continue;
            int mi = out.materialPaths.indexOf(part.material);
            if (mi < 0) { mi = out.materialPaths.size(); out.materialPaths.append(part.material); }
            part.materialIndex = mi;
        }
        out.computeBounds();
        return true;
    } catch (Underrun&) {
        if (error) *error = QStringLiteral("%1: truncated .fmt").arg(sourcePath);
        return false;
    } catch (const std::bad_alloc&) {
        if (error) *error = QStringLiteral("%1: rejected — malformed .fmt header asked for an impossible allocation").arg(sourcePath);
        out = ModelGeometry(); return false;
    }
}

QString MeshParser::selfTest()
{
    // Stride table matches the documented flags.
    // Measured strides (docs/FORMATS.md §3.2): pos 12, normal+tangent 8, uv 4, bone+weight 8,
    // colour 4, unknown-u32 4, uv2 4. 0x3c = 12+8+4+8 (the skinned-mesh default, also the fixed
    // 32-byte v1/v2 layout); 0x38 drops the weights for a static fixed mesh (24).
    struct C { uint32_t vf; int want; };
    static const C kCases[] = {
        {0x3c, 32}, {0x23c, 32}, {0x3e, 36}, {0x23d, 36}, {0x200, 0}, {0x38, 24}, {0x238, 24}, {0x27a, 32},
    };
    for (const auto& c : kCases)
        if (vertexStride(c.vf) != c.want)
            return QStringLiteral("MeshParser: stride(0x%1) = %2, expected %3").arg(c.vf, 0, 16).arg(vertexStride(c.vf)).arg(c.want);
    return QString();
}
