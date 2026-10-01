#include "model/GlbExporter.h"
#include "model/RigMath.h"

#include <QBuffer>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {

// A growable little-endian binary buffer for the glTF BIN chunk. Every accessor records its byte
// offset; the buffer is 4-byte aligned between views as glTF requires.
struct Bin {
    QByteArray data;
    int append(const void* p, int n) { const int off = data.size(); data.append(reinterpret_cast<const char*>(p), n); return off; }
    void align4() { while (data.size() & 3) data.append('\0'); }
};

// Native (x,y,z) → glTF Y-up (x, -z, y). Determinant +1 so winding is preserved.
inline void toYUp(float x, float y, float z, float s, float out[3]) { out[0] = x*s; out[1] = -z*s; out[2] = y*s; }

// The same rotation applied to a direction (no scale).
inline void dirToYUp(float x, float y, float z, float out[3]) { out[0] = x; out[1] = -z; out[2] = y; }

QJsonObject accessor(int bufferView, int compType, int count, const QString& type,
                     const QVector<double>& mn = {}, const QVector<double>& mx = {}, int byteOffset = 0)
{
    QJsonObject a;
    a[QStringLiteral("bufferView")] = bufferView;
    if (byteOffset) a[QStringLiteral("byteOffset")] = byteOffset;
    a[QStringLiteral("componentType")] = compType;
    a[QStringLiteral("count")] = count;
    a[QStringLiteral("type")] = type;
    if (!mn.isEmpty()) { QJsonArray j; for (double v : mn) j.append(v); a[QStringLiteral("min")] = j; }
    if (!mx.isEmpty()) { QJsonArray j; for (double v : mx) j.append(v); a[QStringLiteral("max")] = j; }
    return a;
}

}  // namespace

QByteArray GlbExporter::build(const ModelGeometry& geo, const AstSkeleton::Skeleton& skeleton,
                              const QVector<ExportMaterial>& materials, const Options& opt, QString* error,
                              QVector<LooseImage>* looseOut)
{
    const bool loose = opt.looseTextures && looseOut != nullptr;   // write images as sibling files
    const bool writeTex = opt.embedTextures || loose;              // emit the texture set either way
    if (geo.isEmpty()) { if (error) *error = QStringLiteral("nothing to export (no geometry)"); return QByteArray(); }
    const bool skinned = opt.includeSkeleton && !skeleton.bones.isEmpty() && geo.skinned;
    Bin bin;
    QJsonArray bufferViews, accessors, meshPrims, jMaterials, jTextures, jImages, jSamplers;

    const float s = opt.unitScale;

    // POSITION
    QVector<float> pos; pos.reserve(geo.vertices.size() * 3);
    double mn[3] = {1e30, 1e30, 1e30}, mx[3] = {-1e30, -1e30, -1e30};
    for (const MeshVertex& v : geo.vertices) {
        float o[3]; toYUp(v.px, v.py, v.pz, s, o);
        for (int i = 0; i < 3; ++i) { pos.append(o[i]); mn[i] = std::min(mn[i], double(o[i])); mx[i] = std::max(mx[i], double(o[i])); }
    }
    bin.align4();
    const int posOff = bin.append(pos.constData(), pos.size() * 4);
    const int posView = bufferViews.size();
    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),posOff},{QStringLiteral("byteLength"),pos.size()*4},{QStringLiteral("target"),34962}});
    const int posAcc = accessors.size();
    accessors.append(accessor(posView, 5126, geo.vertices.size(), QStringLiteral("VEC3"),
                              {mn[0],mn[1],mn[2]}, {mx[0],mx[1],mx[2]}));

    // NORMAL
    QVector<float> nrm; nrm.reserve(geo.vertices.size() * 3);
    for (const MeshVertex& v : geo.vertices) { float o[3]; dirToYUp(v.nx, v.ny, v.nz, o);
        float len = std::sqrt(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]); if (len < 1e-6f) { o[0]=0;o[1]=0;o[2]=1;len=1; }
        nrm.append(o[0]/len); nrm.append(o[1]/len); nrm.append(o[2]/len); }
    bin.align4();
    const int nrmOff = bin.append(nrm.constData(), nrm.size() * 4);
    const int nrmView = bufferViews.size();
    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),nrmOff},{QStringLiteral("byteLength"),nrm.size()*4},{QStringLiteral("target"),34962}});
    const int nrmAcc = accessors.size();
    accessors.append(accessor(nrmView, 5126, geo.vertices.size(), QStringLiteral("VEC3")));

    // TANGENT (VEC4, w = handedness)
    QVector<float> tan; tan.reserve(geo.vertices.size() * 4);
    for (const MeshVertex& v : geo.vertices) { float o[3]; dirToYUp(v.tx, v.ty, v.tz, o);
        float len = std::sqrt(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]); if (len < 1e-6f) { o[0]=1;o[1]=0;o[2]=0;len=1; }
        tan.append(o[0]/len); tan.append(o[1]/len); tan.append(o[2]/len); tan.append(v.tw >= 0 ? 1.f : -1.f); }
    bin.align4();
    const int tanOff = bin.append(tan.constData(), tan.size() * 4);
    const int tanView = bufferViews.size();
    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),tanOff},{QStringLiteral("byteLength"),tan.size()*4},{QStringLiteral("target"),34962}});
    const int tanAcc = accessors.size();
    accessors.append(accessor(tanView, 5126, geo.vertices.size(), QStringLiteral("VEC4")));

    // TEXCOORD_0
    QVector<float> uv; uv.reserve(geo.vertices.size() * 2);
    for (const MeshVertex& v : geo.vertices) { uv.append(v.u); uv.append(v.v); }
    bin.align4();
    const int uvOff = bin.append(uv.constData(), uv.size() * 4);
    const int uvView = bufferViews.size();
    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),uvOff},{QStringLiteral("byteLength"),uv.size()*4},{QStringLiteral("target"),34962}});
    const int uvAcc = accessors.size();
    accessors.append(accessor(uvView, 5126, geo.vertices.size(), QStringLiteral("VEC2")));

    // Skin attributes.
    int jointsAcc = -1, weightsAcc = -1, skinIndex = -1;
    QVector<int> jointNodes;
    if (skinned) {
        QVector<uint16_t> joints; joints.reserve(geo.vertices.size() * 4);
        QVector<float> weights; weights.reserve(geo.vertices.size() * 4);
        for (const MeshVertex& v : geo.vertices) {
            for (int k = 0; k < 4; ++k) joints.append(uint16_t(v.joints[k] < skeleton.bones.size() ? v.joints[k] : 0));
            float sum = v.weights[0]+v.weights[1]+v.weights[2]+v.weights[3];
            const float inv = sum > 0 ? 1.f/sum : 0.f;
            for (int k = 0; k < 4; ++k) weights.append(v.weights[k] * inv);
        }
        bin.align4();
        const int jo = bin.append(joints.constData(), joints.size() * 2);
        const int jv = bufferViews.size();
        bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),jo},{QStringLiteral("byteLength"),joints.size()*2},{QStringLiteral("target"),34962}});
        jointsAcc = accessors.size(); accessors.append(accessor(jv, 5123, geo.vertices.size(), QStringLiteral("VEC4")));
        bin.align4();
        const int wo = bin.append(weights.constData(), weights.size() * 4);
        const int wv = bufferViews.size();
        bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),wo},{QStringLiteral("byteLength"),weights.size()*4},{QStringLiteral("target"),34962}});
        weightsAcc = accessors.size(); accessors.append(accessor(wv, 5126, geo.vertices.size(), QStringLiteral("VEC4")));
    }

    // Indices (one bufferView per part so each primitive can reference its own range).
    QVector<QJsonObject> primAccessors;
    // Which roster materials the VISIBLE parts actually reference. Only those are written (and
    // only their images embedded): a hidden part's material used to be written anyway, so a
    // one-part export of a 20-material body carried 20 materials' worth of PNGs. glTF material
    // indices are therefore remapped after the roster is pruned (primMatIdx → matRemap).
    QVector<bool> matUsed(materials.size(), false);
    QVector<int>  primMatIdx;   // roster index per emitted primitive, -1 = none
    {
        QVector<uint32_t> idx = geo.indices;   // shared buffer
        bin.align4();
        const int io = bin.append(idx.constData(), idx.size() * 4);
        const int iv = bufferViews.size();
        bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),io},{QStringLiteral("byteLength"),idx.size()*4},{QStringLiteral("target"),34963}});
        for (const MeshPart& part : geo.parts) {
            if (!part.visible || part.indexCount == 0) continue;
            const int acc = accessors.size();
            accessors.append(accessor(iv, 5125, part.indexCount, QStringLiteral("SCALAR"), {}, {}, int(part.indexStart) * 4));
            QJsonObject prim;
            QJsonObject attrs{{QStringLiteral("POSITION"),posAcc},{QStringLiteral("NORMAL"),nrmAcc},{QStringLiteral("TANGENT"),tanAcc},{QStringLiteral("TEXCOORD_0"),uvAcc}};
            if (skinned) { attrs[QStringLiteral("JOINTS_0")] = jointsAcc; attrs[QStringLiteral("WEIGHTS_0")] = weightsAcc; }
            prim[QStringLiteral("attributes")] = attrs;
            prim[QStringLiteral("indices")] = acc;
            prim[QStringLiteral("mode")] = 4;
            const bool hasMat = part.materialIndex >= 0 && part.materialIndex < materials.size();
            if (hasMat) matUsed[part.materialIndex] = true;
            primMatIdx.append(hasMat ? part.materialIndex : -1);
            meshPrims.append(prim);
        }
    }
    if (meshPrims.isEmpty()) { if (error) *error = QStringLiteral("nothing visible to export"); return QByteArray(); }

    // Materials + embedded PNG textures — used ones only, in roster order.
    QSet<QString> extUsed;
    QVector<int> matRemap(materials.size(), -1);
    for (int mi = 0; mi < materials.size(); ++mi) {
        if (!matUsed[mi]) continue;
        matRemap[mi] = jMaterials.size();
        const ExportMaterial& m = materials[mi];
        QJsonObject mat; mat[QStringLiteral("name")] = m.name.isEmpty() ? QStringLiteral("material_%1").arg(mi) : m.name;
        QJsonObject pbr, matExt;
        auto embed = [&](const QImage& img, const char* role) -> int {
            if (img.isNull()) return -1;
            QByteArray png; QBuffer buf(&png); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG");
            const int im = jImages.size();
            if (loose) {
                // Sibling .png referenced by URI; name is stable/unique per material + channel.
                QString stem = m.name.isEmpty() ? QStringLiteral("material_%1").arg(mi) : m.name;
                stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
                const QString fname = QStringLiteral("%1_%2_%3.png").arg(stem).arg(mi).arg(QLatin1String(role));
                looseOut->append({fname, png});
                jImages.append(QJsonObject{{QStringLiteral("uri"),fname}});
            } else {
                bin.align4();
                const int off = bin.append(png.constData(), png.size());
                const int bv = bufferViews.size();
                bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),off},{QStringLiteral("byteLength"),png.size()}});
                jImages.append(QJsonObject{{QStringLiteral("bufferView"),bv},{QStringLiteral("mimeType"),QStringLiteral("image/png")}});
            }
            const int tx = jTextures.size();
            jTextures.append(QJsonObject{{QStringLiteral("source"),im},{QStringLiteral("sampler"),0}});
            return tx;
        };
        bool hasMr = false;
        if (writeTex) {
            const int base = embed(m.baseColor, "base");
            if (base >= 0) pbr[QStringLiteral("baseColorTexture")] = QJsonObject{{QStringLiteral("index"),base}};
            const int nt = embed(m.normal, "normal");
            if (nt >= 0) mat[QStringLiteral("normalTexture")] = QJsonObject{{QStringLiteral("index"),nt}};
            const int mr = embed(m.metallicRoughness, "orm");   // ORM: R=occlusion, G=roughness, B=metalness
            if (mr >= 0) {
                pbr[QStringLiteral("metallicRoughnessTexture")] = QJsonObject{{QStringLiteral("index"),mr}}; hasMr = true;
                // The same ORM texture's R channel is the occlusion map (glTF default occlusion channel).
                if (m.hasOcclusion) {
                    QJsonObject occ{{QStringLiteral("index"),mr}};
                    if (m.occlusionStrength < 1.0f) occ[QStringLiteral("strength")] = double(m.occlusionStrength);
                    mat[QStringLiteral("occlusionTexture")] = occ;
                }
            }
            const int em = embed(m.emissive, "emissive");
            if (em >= 0) mat[QStringLiteral("emissiveTexture")] = QJsonObject{{QStringLiteral("index"),em}};
        }
        // Metalness/roughness come from the ORM texture (G,B) scaled by these factors: metal-rough
        // uses 1/1; dielectric & spec-gloss force metalness 0 so glTF renders them non-metallic.
        pbr[QStringLiteral("metallicFactor")] = double(m.metalFactor);
        pbr[QStringLiteral("roughnessFactor")] = double(m.roughFactor);
        mat[QStringLiteral("pbrMetallicRoughness")] = pbr;
        if (m.doubleSided) mat[QStringLiteral("doubleSided")] = true;

        if (!m.emissive.isNull()) {
            mat[QStringLiteral("emissiveFactor")] = QJsonArray{ 1.0, 1.0, 1.0 };   // the texture carries the colour
            if (m.emissiveStrength > 1.0f) { QJsonObject es{{QStringLiteral("emissiveStrength"),double(m.emissiveStrength)}}; matExt[QStringLiteral("KHR_materials_emissive_strength")] = es; extUsed.insert(QStringLiteral("KHR_materials_emissive_strength")); }
        }

        // Spec-gloss / dielectric families → KHR_materials_specular (non-metal, spec-mask-driven).
        if (m.dielectricSpec) {
            QJsonObject spec;
            if (writeTex && !m.specularColor.isNull()) {
                const int sc = embed(m.specularColor, "spec");
                if (sc >= 0) spec[QStringLiteral("specularColorTexture")] = QJsonObject{{QStringLiteral("index"),sc}};
            }
            matExt[QStringLiteral("KHR_materials_specular")] = spec;   // defaults (factor 1) when no map
            extUsed.insert(QStringLiteral("KHR_materials_specular"));
        }
        // Translucency → KHR_materials_transmission (approximation; PoE2 translucency is not a true
        // refractive transmission, but this is the closest portable glTF representation).
        if (m.transmissionFactor > 0.0f) {
            matExt[QStringLiteral("KHR_materials_transmission")] = QJsonObject{{QStringLiteral("transmissionFactor"),double(m.transmissionFactor)}};
            extUsed.insert(QStringLiteral("KHR_materials_transmission"));
        }
        if (!matExt.isEmpty()) mat[QStringLiteral("extensions")] = matExt;

        if      (m.alphaCutout) { mat[QStringLiteral("alphaMode")] = QStringLiteral("MASK"); mat[QStringLiteral("alphaCutoff")] = double(m.alphaCutoff); }
        else if (m.alphaBlend)  { mat[QStringLiteral("alphaMode")] = QStringLiteral("BLEND"); }
        jMaterials.append(mat);
    }
    for (int pi = 0; pi < meshPrims.size(); ++pi) {
        const int mi = primMatIdx.at(pi);
        if (mi < 0) continue;
        QJsonObject prim = meshPrims.at(pi).toObject();
        prim[QStringLiteral("material")] = matRemap.at(mi);
        meshPrims[pi] = prim;
    }
    if (!jTextures.isEmpty()) jSamplers.append(QJsonObject{{QStringLiteral("magFilter"),9729},{QStringLiteral("minFilter"),9987},{QStringLiteral("wrapS"),10497},{QStringLiteral("wrapT"),10497}});

    // Nodes: mesh node + skeleton nodes.
    QJsonArray nodes, sceneNodes;
    const int meshNodeIndex = 0;
    nodes.append(QJsonObject{{QStringLiteral("mesh"),0}});
    sceneNodes.append(meshNodeIndex);

    QJsonArray jSkins, jAnimations;
    if (skinned) {
        const int nodeBase = nodes.size();
        // One node per bone, written as TRS (translation/rotation/scale) — NOT a matrix. glTF forbids
        // a node that is an animation target from carrying a `matrix`, and every bone here IS animated
        // when clips are exported; a matrix + TRS animation channels on the same node is invalid and
        // some importers (e.g. Blender 4.4) then fail to reconstruct the rest pose, so the mesh
        // deforms wildly while others (Blender 5.0) tolerate it. TRS is the portable, spec-valid form.
        // The bone's parent-relative local (bind * inverse(parentBind)) is taken into the Y-up frame
        // via R * local * R^-1, its translation scaled by unitScale, then decomposed to T/R/S.
        static const RigMath::Mat4 R  = {1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
        static const RigMath::Mat4 Ri = {1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1};
        for (int bi = 0; bi < skeleton.bones.size(); ++bi) {
            const AstSkeleton::Bone& b = skeleton.bones[bi];
            RigMath::Mat4 local = b.bind;
            if (b.parent >= 0) local = RigMath::mul(b.bind, skeleton.bones[b.parent].inverseBind);
            RigMath::Mat4 lm = RigMath::mul(RigMath::mul(R, local), Ri);
            lm[12] *= s; lm[13] *= s; lm[14] *= s;
            float t[3], q[4], sc[3];
            RigMath::decomposeTRS(lm, t, q, sc);
            QJsonObject n; n[QStringLiteral("name")] = b.name;
            n[QStringLiteral("translation")] = QJsonArray{ t[0], t[1], t[2] };
            n[QStringLiteral("rotation")]    = QJsonArray{ q[0], q[1], q[2], q[3] };   // xyzw
            n[QStringLiteral("scale")]       = QJsonArray{ sc[0], sc[1], sc[2] };
            nodes.append(n);
            jointNodes.append(nodeBase + bi);
        }
        // Wire children.
        for (int bi = 0; bi < skeleton.bones.size(); ++bi) {
            QJsonArray kids;
            for (int cj = 0; cj < skeleton.bones.size(); ++cj) if (skeleton.bones[cj].parent == bi) kids.append(nodeBase + cj);
            if (!kids.isEmpty()) { QJsonObject n = nodes[nodeBase + bi].toObject(); n[QStringLiteral("children")] = kids; nodes[nodeBase + bi] = n; }
        }
        // Root bones become scene roots.
        for (int bi = 0; bi < skeleton.bones.size(); ++bi) if (skeleton.bones[bi].parent < 0) sceneNodes.append(nodeBase + bi);

        // inverseBindMatrices in the Y-up frame (reuses R/Ri declared above).
        // CRITICAL: RigMath is row-major / row-vector (v·M); glTF is column-major / column-vector
        // (M·v). Converting a row-vector matrix M_row to glTF's stored layout requires storing it in
        // ROW-MAJOR element order (for r,c → inv[r*4+c]): glTF then reads it column-major as M_rowᵀ,
        // which is exactly the column-vector matrix that reproduces v·M_row. Writing it column-major
        // (the old `for c,r`) stored M_row un-transposed, so glTF skinned every vertex with the
        // TRANSPOSE of the intended inverse-bind — the mesh deformed at rest while the armature
        // (driven by the node TRS, which is a separate path) still looked correct. Verified against a
        // strict glTF-convention solver and a real Blender import (bind deviation 2.6 m → 0).
        QVector<float> ibm; ibm.reserve(skeleton.bones.size() * 16);
        for (const AstSkeleton::Bone& b : skeleton.bones) {
            RigMath::Mat4 inv = RigMath::mul(RigMath::mul(R, b.inverseBind), Ri);
            inv[12] *= s; inv[13] *= s; inv[14] *= s;
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) ibm.append(inv[r*4+c]);
        }
        bin.align4();
        const int io = bin.append(ibm.constData(), ibm.size() * 4);
        const int iv = bufferViews.size();
        bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),io},{QStringLiteral("byteLength"),ibm.size()*4}});
        const int ibmAcc = accessors.size();
        accessors.append(accessor(iv, 5126, skeleton.bones.size(), QStringLiteral("MAT4")));
        QJsonArray joints; for (int j : jointNodes) joints.append(j);
        QJsonObject skin{{QStringLiteral("inverseBindMatrices"),ibmAcc},{QStringLiteral("joints"),joints}};
        skinIndex = jSkins.size(); jSkins.append(skin);
        QJsonObject mn0 = nodes[meshNodeIndex].toObject(); mn0[QStringLiteral("skin")] = skinIndex; nodes[meshNodeIndex] = mn0;
    }

    // Optional 180° yaw (about the up axis) so characters face +Z for thumbnail pipelines. Applied
    // as a single wrapper ROOT node whose children are the existing scene roots — NOT baked into the
    // mesh POSITION, skeleton, or IBMs, so the exported geometry stays the untouched original (the
    // default; opt.yaw180 is off unless a caller sets it). Skinning naturally carries the rotation:
    // every joint's world transform gains the parent rotation, IBMs are unchanged, so the deformed
    // mesh is rigidly rotated with zero deformation. glTF quaternion for 180° about +Y = (0,1,0,0).
    if (opt.yaw180) {
        const int yawRoot = nodes.size();
        QJsonObject yn;
        yn[QStringLiteral("name")] = QStringLiteral("yaw180");
        yn[QStringLiteral("rotation")] = QJsonArray{ 0.0, 1.0, 0.0, 0.0 };
        yn[QStringLiteral("children")] = sceneNodes;      // adopt the current roots
        nodes.append(yn);
        sceneNodes = QJsonArray{ yawRoot };               // scene now has the single yaw root
    }

    // Assemble glTF JSON.
    QJsonObject root;
    root[QStringLiteral("asset")] = QJsonObject{{QStringLiteral("version"),QStringLiteral("2.0")},{QStringLiteral("generator"),QStringLiteral("POE2AssetBrowser")}};
    root[QStringLiteral("scene")] = 0;
    root[QStringLiteral("scenes")] = QJsonArray{ QJsonObject{{QStringLiteral("nodes"),sceneNodes}} };
    root[QStringLiteral("nodes")] = nodes;
    root[QStringLiteral("meshes")] = QJsonArray{ QJsonObject{{QStringLiteral("primitives"),meshPrims}} };
    // NOTE: `accessors` and `bufferViews` are assigned to `root` AFTER the animation block below,
    // because that block appends animation samplers to them. QJsonArray is a value type, so an
    // early `root["accessors"] = accessors` would freeze a copy and drop every animation accessor,
    // leaving samplers pointing at out-of-range indices (a corrupt glb that fails to import).
    if (!jMaterials.isEmpty()) root[QStringLiteral("materials")] = jMaterials;
    if (!extUsed.isEmpty()) {
        QJsonArray ext; for (const QString& e : extUsed) ext.append(e);
        root[QStringLiteral("extensionsUsed")] = ext;
    }
    if (!jTextures.isEmpty()) { root[QStringLiteral("textures")] = jTextures; root[QStringLiteral("images")] = jImages; root[QStringLiteral("samplers")] = jSamplers; }
    if (!jSkins.isEmpty()) root[QStringLiteral("skins")] = jSkins;

    // Animations.
    if (skinned && opt.includeAnimations && skeleton.clipsDecoded) {
        const int nodeBase = 1;   // bones start at node 1
        static const RigMath::Mat4 R = {1,0,0,0, 0,0,-1,0, 0,1,0,0, 0,0,0,1};
        for (int ci = 0; ci < skeleton.clips.size(); ++ci) {
            if (opt.onlyClip >= 0 && ci != opt.onlyClip) continue;   // "current clip only" selection
            const AstSkeleton::Clip& clip = skeleton.clips[ci];
            if (clip.keys.isEmpty()) continue;
            QJsonArray channels, samplers;
            for (const AstSkeleton::KeySet& ks : clip.keys) {
                if (ks.nodeId < 0 || ks.nodeId >= skeleton.bones.size()) continue;
                const int node = nodeBase + ks.nodeId;
                auto addSampler = [&](const QVector<float>& times, const QByteArray& valBytes, int comps, const QString& targetPath) {
                    if (times.isEmpty()) return;
                    bin.align4();
                    QVector<float> tt; for (float t : times) tt.append(t / float(clip.fps > 0 ? clip.fps : 30));
                    const int to = bin.append(tt.constData(), tt.size() * 4);
                    const int tv = bufferViews.size();
                    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),to},{QStringLiteral("byteLength"),tt.size()*4}});
                    double tmin = tt.isEmpty()?0:tt[0], tmax = tt.isEmpty()?0:tt[tt.size()-1];
                    const int ta = accessors.size();
                    accessors.append(accessor(tv, 5126, tt.size(), QStringLiteral("SCALAR"), {tmin}, {tmax}));
                    bin.align4();
                    const int vo = bin.append(valBytes.constData(), valBytes.size());
                    const int vv = bufferViews.size();
                    bufferViews.append(QJsonObject{{QStringLiteral("buffer"),0},{QStringLiteral("byteOffset"),vo},{QStringLiteral("byteLength"),valBytes.size()}});
                    const int va = accessors.size();
                    accessors.append(accessor(vv, 5126, times.size(), comps == 4 ? QStringLiteral("VEC4") : QStringLiteral("VEC3")));
                    const int si = samplers.size();
                    samplers.append(QJsonObject{{QStringLiteral("input"),ta},{QStringLiteral("output"),va},{QStringLiteral("interpolation"),QStringLiteral("LINEAR")}});
                    channels.append(QJsonObject{{QStringLiteral("sampler"),si},{QStringLiteral("target"),QJsonObject{{QStringLiteral("node"),node},{QStringLiteral("path"),targetPath}}}});
                };
                if (!ks.pos.isEmpty()) {
                    QByteArray vb; for (const auto& p : ks.pos) { float o[3]; float x=R[0]*p[0]+R[1]*p[1]+R[2]*p[2], y=R[4]*p[0]+R[5]*p[1]+R[6]*p[2], z=R[8]*p[0]+R[9]*p[1]+R[10]*p[2]; o[0]=x*s;o[1]=y*s;o[2]=z*s; vb.append(reinterpret_cast<char*>(o), 12); }
                    addSampler(ks.posTimes, vb, 3, QStringLiteral("translation"));
                }
                if (!ks.rot.isEmpty()) {
                    QByteArray vb; for (const auto& q : ks.rot) { // rotate the quaternion vector part by R
                        float o[4]; float vx=q[0],vy=q[1],vz=q[2]; o[0]=R[0]*vx+R[1]*vy+R[2]*vz; o[1]=R[4]*vx+R[5]*vy+R[6]*vz; o[2]=R[8]*vx+R[9]*vy+R[10]*vz; o[3]=q[3];
                        float len=std::sqrt(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]+o[3]*o[3]); if(len<1e-8f)len=1; for(int k=0;k<4;++k)o[k]/=len; vb.append(reinterpret_cast<char*>(o), 16); }
                    addSampler(ks.rotTimes, vb, 4, QStringLiteral("rotation"));
                }
                if (!ks.scale.isEmpty()) {
                    QByteArray vb; for (const auto& sc : ks.scale) { float o[3]={sc[0],sc[2],sc[1]}; vb.append(reinterpret_cast<char*>(o), 12); }
                    addSampler(ks.scaleTimes, vb, 3, QStringLiteral("scale"));
                }
            }
            if (!channels.isEmpty())
                jAnimations.append(QJsonObject{{QStringLiteral("name"),clip.name},{QStringLiteral("channels"),channels},{QStringLiteral("samplers"),samplers}});
        }
        if (!jAnimations.isEmpty()) root[QStringLiteral("animations")] = jAnimations;
    }

    // Assign these now — the animation block above appended its samplers' accessors/bufferViews.
    root[QStringLiteral("accessors")] = accessors;
    root[QStringLiteral("bufferViews")] = bufferViews;

    bin.align4();
    root[QStringLiteral("buffers")] = QJsonArray{ QJsonObject{{QStringLiteral("byteLength"),bin.data.size()}} };

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    while (json.size() & 3) json.append(' ');
    QByteArray blob = bin.data;   // already 4-aligned

    QByteArray glb;
    auto put32 = [&](uint32_t v){ v = qToLittleEndian(v); glb.append(reinterpret_cast<char*>(&v), 4); };
    put32(0x46546C67);                            // "glTF"
    put32(2);                                     // version
    put32(12 + 8 + json.size() + 8 + blob.size());// total length
    put32(json.size()); put32(0x4E4F534A);        // JSON chunk
    glb.append(json);
    put32(blob.size()); put32(0x004E4942);        // BIN chunk
    glb.append(blob);
    return glb;
}

bool GlbExporter::write(const ModelGeometry& geo, const AstSkeleton::Skeleton& skeleton,
                        const QVector<ExportMaterial>& materials, const Options& opt, const QString& path, QString* error)
{
    // Loose textures only make sense for text .gltf (external sibling files); a .glb is self-contained,
    // so force embedding there. Collect the loose PNGs from build() and write them beside the .gltf.
    const bool wantGltf = path.endsWith(QStringLiteral(".gltf"), Qt::CaseInsensitive);
    Options eff = opt;
    eff.looseTextures = opt.looseTextures && wantGltf;
    QVector<LooseImage> loose;
    const QByteArray glb = build(geo, skeleton, materials, eff, error, eff.looseTextures ? &loose : nullptr);
    if (glb.isEmpty()) return false;
    if (eff.looseTextures) {
        const QString dir = QFileInfo(path).absolutePath();
        for (const LooseImage& li : loose) {
            QFile tf(dir + QLatin1Char('/') + li.name);
            if (!tf.open(QIODevice::WriteOnly) || tf.write(li.png) != li.png.size()) {
                if (error) *error = QStringLiteral("cannot write texture %1").arg(li.name); return false; }
        }
    }

    // A ".gltf" path is written as text glTF + an external ".bin" buffer (some pipelines prefer it).
    // The single .glb we just built already carries JSON + one BIN chunk, so split them and re-point
    // the buffer at the sidecar file — every bufferView-referenced image still resolves through it.
    if (path.endsWith(QStringLiteral(".gltf"), Qt::CaseInsensitive)) {
        if (glb.size() < 20) { if (error) *error = QStringLiteral("internal: glb too small to split"); return false; }
        uint32_t jsonLen = 0; std::memcpy(&jsonLen, glb.constData() + 12, 4); jsonLen = qFromLittleEndian(jsonLen);
        const int binHdr = 20 + int(jsonLen);
        if (binHdr + 8 > glb.size()) { if (error) *error = QStringLiteral("internal: glb chunk layout"); return false; }
        uint32_t binLen = 0; std::memcpy(&binLen, glb.constData() + binHdr, 4); binLen = qFromLittleEndian(binLen);
        const QByteArray jsonBytes = glb.mid(20, jsonLen);
        const QByteArray binBytes  = glb.mid(binHdr + 8, binLen);

        const QString binName = QFileInfo(path).completeBaseName() + QStringLiteral(".bin");
        QJsonObject root = QJsonDocument::fromJson(jsonBytes).object();
        QJsonArray buffers = root.value(QStringLiteral("buffers")).toArray();
        if (!buffers.isEmpty()) { QJsonObject b0 = buffers[0].toObject(); b0[QStringLiteral("uri")] = binName; buffers[0] = b0; root[QStringLiteral("buffers")] = buffers; }

        const QString binPath = QFileInfo(path).absolutePath() + QLatin1Char('/') + binName;
        QFile bf(binPath);
        if (!bf.open(QIODevice::WriteOnly) || bf.write(binBytes) != binBytes.size()) {
            if (error) *error = QStringLiteral("cannot write %1").arg(binPath); return false; }
        QFile jf(path);
        const QByteArray jsonOut = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (!jf.open(QIODevice::WriteOnly) || jf.write(jsonOut) != jsonOut.size()) {
            if (error) *error = QStringLiteral("cannot write %1").arg(path); return false; }
        return true;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, f.errorString()); return false; }
    if (f.write(glb) != glb.size()) { if (error) *error = QStringLiteral("short write to %1").arg(path); return false; }
    return true;
}

QString GlbExporter::selfTest()
{
    ModelGeometry g;
    g.vertices = { MeshVertex{}, MeshVertex{}, MeshVertex{} };
    g.vertices[0].px = 0; g.vertices[1].px = 1; g.vertices[2].py = 1;
    g.indices = {0, 1, 2};
    MeshPart part; part.name = QStringLiteral("tri"); part.indexStart = 0; part.indexCount = 3; g.parts.append(part);
    g.skinned = false; g.computeBounds();
    QString err;
    const QByteArray glb = build(g, AstSkeleton::Skeleton{}, {}, Options{}, &err);
    if (glb.isEmpty()) return QStringLiteral("GlbExporter self-test: build failed (%1)").arg(err);
    if (glb.left(4) != QByteArray("glTF")) return QStringLiteral("GlbExporter self-test: bad magic");
    uint32_t total; std::memcpy(&total, glb.constData() + 8, 4); total = qFromLittleEndian(total);
    if (int(total) != glb.size()) return QStringLiteral("GlbExporter self-test: length field %1 != file %2").arg(total).arg(glb.size());
    uint32_t jsonLen; std::memcpy(&jsonLen, glb.constData() + 12, 4); jsonLen = qFromLittleEndian(jsonLen);
    QJsonParseError pe; QJsonDocument::fromJson(glb.mid(20, jsonLen), &pe);
    if (pe.error != QJsonParseError::NoError) return QStringLiteral("GlbExporter self-test: JSON chunk invalid");

    // Material pruning: two parts on two roster materials, the SECOND part hidden → exactly one
    // material is written and the surviving primitive references index 0, not its roster index 1.
    {
        ModelGeometry g2;
        g2.vertices = { MeshVertex{}, MeshVertex{}, MeshVertex{}, MeshVertex{}, MeshVertex{}, MeshVertex{} };
        g2.vertices[1].px = 1; g2.vertices[2].py = 1; g2.vertices[4].px = 1; g2.vertices[5].py = 1;
        g2.indices = {0, 1, 2, 3, 4, 5};
        MeshPart a; a.name = QStringLiteral("hidden"); a.indexStart = 0; a.indexCount = 3; a.materialIndex = 0; a.visible = false;
        MeshPart b; b.name = QStringLiteral("shown");  b.indexStart = 3; b.indexCount = 3; b.materialIndex = 1;
        g2.parts = {a, b}; g2.materialPaths = {QStringLiteral("m0"), QStringLiteral("m1")};
        g2.skinned = false; g2.computeBounds();
        ExportMaterial m0; m0.name = QStringLiteral("m0"); ExportMaterial m1; m1.name = QStringLiteral("m1");
        const QByteArray glb2 = build(g2, AstSkeleton::Skeleton{}, {m0, m1}, Options{}, &err);
        if (glb2.isEmpty()) return QStringLiteral("GlbExporter self-test: prune build failed (%1)").arg(err);
        uint32_t jl2; std::memcpy(&jl2, glb2.constData() + 12, 4); jl2 = qFromLittleEndian(jl2);
        const QJsonObject root = QJsonDocument::fromJson(glb2.mid(20, jl2)).object();
        const QJsonArray mats = root.value(QStringLiteral("materials")).toArray();
        if (mats.size() != 1 || mats.at(0).toObject().value(QStringLiteral("name")).toString() != QStringLiteral("m1"))
            return QStringLiteral("GlbExporter self-test: expected the one used material (m1), got %1").arg(mats.size());
        const QJsonArray prims = root.value(QStringLiteral("meshes")).toArray().at(0).toObject().value(QStringLiteral("primitives")).toArray();
        if (prims.size() != 1 || prims.at(0).toObject().value(QStringLiteral("material")).toInt(-1) != 0)
            return QStringLiteral("GlbExporter self-test: primitive material index not remapped to 0");
    }
    return QString();
}
