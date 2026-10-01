#include "store/AssetStore.h"
#include "model/MeshParser.h"
#include "model/RigMath.h"
#include "app/AppPaths.h"
#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>

AssetStore::AssetStore(QObject* parent) : QObject(parent) {}

bool AssetStore::open(const QString& bundlesDir, QString* error, const std::function<void(const QString&)>& progress)
{
    clearCaches();
    m_matIndex = MaterialFamilyIndex{};   // a new game build invalidates the old classification
    m_nameIndex = NameIndex{};
    { QMutexLocker l(&m_lookupMutex); m_lookupsBuilt = false; m_dirFiles.clear(); m_extFiles.clear(); }
    return m_index.load(bundlesDir, AppPaths::subDir(QStringLiteral("cache")), error, progress);
}

void AssetStore::ensureLookups()
{
    QMutexLocker lock(&m_lookupMutex);
    if (m_lookupsBuilt) return;
    const auto& files = m_index.files();
    for (uint32_t i = 0; i < files.size(); ++i) {
        m_dirFiles[files[i].dir].append(i);
        m_extFiles[files[i].extId].append(i);
    }
    m_lookupsBuilt = true;   // immutable after this: concurrent const reads below need no lock
}

bool AssetStore::buildMaterialIndex(const std::function<void(const QString&)>& progress)
{
    if (!isOpen()) return false;
    if (m_matIndex.isBuilt()) return true;
    const QString cacheDir = AppPaths::subDir(QStringLiteral("cache"));
    QString why;
    if (m_matIndex.load(cacheDir, m_index.fingerprint(), &why)) {
        if (progress) progress(QStringLiteral("materials: loaded from cache"));
        return true;
    }
    if (progress) progress(QStringLiteral("materials: sweeping .mat + .sm (first run for this game build)…"));
    if (!m_matIndex.build(*this, progress)) return false;
    if (!m_matIndex.save(cacheDir, m_index.fingerprint(), &why) && progress)
        progress(QStringLiteral("materials cache not written: %1").arg(why));
    return m_matIndex.isBuilt();
}

bool AssetStore::buildNameIndex(const std::function<void(const QString&)>& progress)
{
    if (!isOpen()) return false;
    if (m_nameIndex.isBuilt()) return true;
    const QString cacheDir = AppPaths::subDir(QStringLiteral("cache"));
    QString why;
    if (m_nameIndex.load(cacheDir, m_index.fingerprint(), &why)) {
        if (progress) progress(QStringLiteral("names: loaded from cache"));
        return true;
    }
    if (progress) progress(QStringLiteral("names: resolving item names from the data tables…"));
    if (!m_nameIndex.build(*this, progress)) return false;   // data tables absent → names disabled
    if (!m_nameIndex.save(cacheDir, m_index.fingerprint(), &why) && progress)
        progress(QStringLiteral("names cache not written: %1").arg(why));
    return m_nameIndex.isBuilt();
}

void AssetStore::clearCaches()
{
    QMutexLocker lock(&m_bundleMutex);
    m_bundleCache.clear();
    m_bundleLru.clear();
}

std::shared_ptr<Bundle> AssetStore::bundleFor(uint32_t fileIndex, QString* error)
{
    if (fileIndex >= m_index.files().size()) { if (error) *error = QStringLiteral("file index out of range"); return nullptr; }
    const uint32_t bi = m_index.files()[fileIndex].bundle;
    QMutexLocker lock(&m_bundleMutex);
    auto it = m_bundleCache.find(bi);
    if (it != m_bundleCache.end()) {
        m_bundleLru.removeOne(bi); m_bundleLru.append(bi);   // touch: most-recently-used
        return it.value();
    }
    const QString path = QDir(m_index.bundlesDir()).filePath(m_index.bundles()[bi].name + QStringLiteral(".bundle.bin"));
    auto b = Bundle::open(path, error);
    if (!b) return nullptr;
    // Bound the cache, but evict only the SINGLE least-recently-used handle rather than clearing them
    // all — with many bundles in flight (a parallel bulk run touches dozens), a clear-all would keep
    // reopening + re-parsing bundles still being read. Cap sits above the worker ceiling (32).
    constexpr int kMaxBundles = 64;
    while (m_bundleCache.size() >= kMaxBundles && !m_bundleLru.isEmpty()) {
        const uint32_t evict = m_bundleLru.takeFirst();
        m_bundleCache.remove(evict);
    }
    m_bundleCache.insert(bi, b);
    m_bundleLru.append(bi);
    return b;
}

QByteArray AssetStore::readFile(uint32_t fileIndex, QString* error)
{
    if (fileIndex >= m_index.files().size()) { if (error) *error = QStringLiteral("file index out of range"); return QByteArray(); }
    auto b = bundleFor(fileIndex, error);
    if (!b) return QByteArray();
    const BundleIndex::FileRecord& r = m_index.files()[fileIndex];
    return b->readRange(r.offset, r.size, error);
}

QByteArray AssetStore::readFile(const QString& path, QString* error)
{
    uint32_t idx = 0;
    if (!m_index.find(path, &idx)) { if (error) *error = QStringLiteral("not in index: %1").arg(path); return QByteArray(); }
    return readFile(idx, error);
}

bool AssetStore::loadModel(const QString& modelPath, ModelGeometry& geo, QString* error)
{
    const QByteArray data = readFile(modelPath, error);
    if (data.isEmpty()) return false;
    const QString lower = modelPath.toLower();
    bool ok = false;
    if (lower.endsWith(QStringLiteral(".fmt"))) ok = MeshParser::parseFmt(data, modelPath, geo, error);
    else                                        ok = MeshParser::parseSmd(data, modelPath, geo, error);
    if (!ok) return false;

    // For a .smd, attach materials from the .sm descriptor if we can find one.
    if (lower.endsWith(QStringLiteral(".smd"))) {
        const QString smPath = findSmForSmd(modelPath);
        if (!smPath.isEmpty()) {
            const QByteArray smData = readFile(smPath, nullptr);
            if (!smData.isEmpty()) {
                const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(smData);
                // The material rows map onto meshes in order, each covering `count` consecutive parts.
                int part = 0;
                for (const auto& run : sm.materials) {
                    int mi = geo.materialPaths.indexOf(run.matPath);
                    if (mi < 0) { mi = geo.materialPaths.size(); geo.materialPaths.append(run.matPath); }
                    for (int k = 0; k < run.count && part < geo.parts.size(); ++k, ++part) {
                        geo.parts[part].material = run.matPath;
                        geo.parts[part].materialIndex = mi;
                    }
                }
            }
        }
    }
    return true;
}

QString AssetStore::findSmForSmd(const QString& smdPath)
{
    // A .sm names its .smd; the .sm usually sits beside the .smd with a related stem. Rather than
    // guess the stem, look for any .sm in the same directory whose SkinnedMeshData points here.
    //
    // Match by the record's DIRECTORY ID (an int the index already stores per file), not by
    // rebuilding dirOf() into a QString for all 1.4M records — that string work per load was the bulk
    // of the "slow to open" cost. Same for the .sm extension: resolve its ext id once.
    uint32_t modelIdx = 0;
    if (!m_index.find(smdPath, &modelIdx)) return QString();
    const uint32_t targetDir = m_index.files()[modelIdx].dir;
    const int smExt = m_index.extensions().indexOf(QStringLiteral(".sm"));
    if (smExt < 0) return QString();
    const QString targetLower = smdPath.toLower();

    // A localized mesh (e.g. Atziri's `.japan` variant) is named `<stem>.<locale>.smd`, but its
    // `.<locale>.sm` descriptor references the mesh WITHOUT that locale infix (`<stem>.smd`) — so an
    // exact-string match finds no .sm and the model exports with no materials. Also accept the mesh
    // path with its pre-`.smd` filename segment removed, which delocalizes `...0234604c.japan.smd`
    // to `...0234604c.smd`. (A non-localized name like `rig_3b9cf934.smd` has no such segment, so
    // this adds no candidate and cannot mis-match.)
    QString delocalized;
    if (targetLower.endsWith(QStringLiteral(".smd"))) {
        const QString base = targetLower.left(targetLower.size() - 4);   // strip ".smd"
        const int lastDot = base.lastIndexOf(QLatin1Char('.'));
        const int lastSlash = base.lastIndexOf(QLatin1Char('/'));
        if (lastDot > lastSlash) delocalized = base.left(lastDot) + QStringLiteral(".smd");
    }

    ensureLookups();
    QString looseHit;   // a delocalized match, used only if no exact match is found (exact wins)
    // Only the files IN the mesh's own directory can be its co-located .sm — iterate that bucket, not
    // all ~1.4M records.
    for (uint32_t i : m_dirFiles.value(targetDir)) {
        const auto& f = m_index.files()[i];
        if (f.extId != uint16_t(smExt)) continue;
        const QByteArray smData = readFile(i, nullptr);
        if (smData.isEmpty()) continue;
        const QString ref = AssetText::parseSm(smData).smdPath;
        if (ref == targetLower) return m_index.pathOf(i);                 // exact match — best
        if (looseHit.isEmpty() && !delocalized.isEmpty() && ref == delocalized) looseHit = m_index.pathOf(i);
    }
    return looseHit;
}

QStringList AssetStore::filesUnderPrefix(const QString& dirPrefix, const QString& ext)
{
    ensureLookups();
    QStringList out;
    const QString pfx = dirPrefix.toLower();
    const int extId = ext.isEmpty() ? -1 : m_index.extensions().indexOf(ext.toLower());
    if (!ext.isEmpty() && extId < 0) return out;   // an extension the index never saw
    const auto& dirs = m_index.directories();       // lowercase, no trailing slash
    for (uint32_t d = 0; d < dirs.size(); ++d) {
        const QString ds = QString::fromStdString(dirs[d]);
        if (ds != pfx && !ds.startsWith(pfx + QLatin1Char('/'))) continue;   // not under the prefix
        for (uint32_t fi : m_dirFiles.value(d)) {
            if (extId >= 0 && m_index.files()[fi].extId != uint16_t(extId)) continue;
            out << m_index.pathOf(fi);
        }
    }
    return out;
}

const QVector<AssetStore::AnimCategory>& AssetStore::playerAnimCategories()
{
    QMutexLocker lock(&m_animMutex);
    if (m_animCatBuilt) return m_animCats;
    m_animCatBuilt = true;

    // The base rig first (it carries only the static idle_01, but list it so the idle is reachable),
    // then every per-move onerig.ast under the animations tree. Display name = the sub-path between
    // "animations/" and "/onerig.ast" (the move), tidied; the base rig shows as "(base) idle".
    const QString base = QStringLiteral("art/models/charactersfour/onerig.ast");
    if (m_index.find(base)) m_animCats.append({QStringLiteral("(base) idle"), base});

    const QString animRoot = QStringLiteral("art/models/charactersfour/animations");
    QStringList asts = filesUnderPrefix(animRoot, QStringLiteral(".ast"));
    QVector<AnimCategory> moves;
    for (const QString& p : asts) {
        if (!p.endsWith(QLatin1String("/onerig.ast"))) continue;   // the move rigs (not attachment sub-rigs)
        QString mid = p.mid(animRoot.size() + 1);                  // "<move>/onerig.ast" (or deeper)
        mid.chop(int(QStringLiteral("/onerig.ast").size()));       // "<move>" (may contain '/')
        if (mid.isEmpty()) continue;
        moves.append({mid, p});
    }
    std::sort(moves.begin(), moves.end(),
              [](const AnimCategory& a, const AnimCategory& b) { return a.name < b.name; });
    m_animCats += moves;
    return m_animCats;
}

QByteArray AssetStore::animBytes(const QString& astPath)
{
    QMutexLocker lock(&m_animMutex);
    if (m_animBytesPath == astPath && !m_animBytes.isEmpty()) return m_animBytes;
    QByteArray bytes = readFile(astPath, nullptr);   // readFile is self-locking; m_animMutex is separate
    m_animBytesPath = astPath;
    m_animBytes = bytes;
    return bytes;
}

AstSkeleton::Skeleton AssetStore::playerAnimHeader(const QString& astPath)
{
    const QByteArray bytes = animBytes(astPath);
    if (bytes.isEmpty()) return {};
    return AstSkeleton::parse(bytes, /*decodeClips*/false, nullptr);
}

AstSkeleton::Skeleton AssetStore::playerAnimClip(const QString& astPath, int clipIndex)
{
    const QByteArray bytes = animBytes(astPath);
    if (bytes.isEmpty()) return {};
    return AstSkeleton::parseWithClip(bytes, clipIndex, nullptr);
}

const QSet<QString>& AssetStore::playerRigBoneNames()
{
    QMutexLocker lock(&m_animMutex);
    if (m_playerRigBonesBuilt) return m_playerRigBones;
    m_playerRigBonesBuilt = true;
    const QString base = QStringLiteral("art/models/charactersfour/onerig.ast");
    if (m_index.find(base)) {
        // readFile locks m_bundleMutex (a different mutex), so this does not re-enter m_animMutex.
        const QByteArray bytes = readFile(base, nullptr);
        if (!bytes.isEmpty()) {
            const AstSkeleton::Skeleton sk = AstSkeleton::parse(bytes, /*decodeClips*/false, nullptr);
            for (const AstSkeleton::Bone& b : sk.bones) m_playerRigBones.insert(b.name);
        }
    }
    return m_playerRigBones;
}

QImage AssetStore::loadTexture(const QString& ddsPath, QString* error, DdsImage::Info* infoOut)
{
    const QByteArray dds = readFile(ddsPath, error);
    if (dds.isEmpty()) return QImage();
    if (infoOut) *infoOut = DdsImage::probe(dds, nullptr);
    return DdsImage::decode(dds, error);
}

namespace {
// PoE2 has TWO measured normal packings in the same NormalGlossAO_TEX role (docs/FORMATS.md §6):
//   (a) RG-normal   : R,G = tangent normal x,y (Z reconstructed), B = gloss, A = ambient occlusion.
//   (b) full-normal : R,G,B = full tangent normal (z in B), A = gloss; no baked AO.
// They are told apart per-texture by whether 2·RGB−1 is unit length on average (that only holds when
// B really is normal.z). Metalness (metal-rough families) is a separate SpecularMask_TEX/Metal_TEX
// grey level. glTF wants an ORM texture (R=occlusion, G=roughness, B=metalness) + a normal texture.

// Which packing? mean |‖2·RGB−1‖ − 1| < 0.12 ⇒ full RGB normal (B=z); else RG-normal (B=gloss).
bool normalIsFullRGB(const QImage& nga)
{
    const QImage n = nga.convertToFormat(QImage::Format_RGBA8888);
    double err = 0; long cnt = 0;
    const int step = std::max(1, n.height() / 256);
    for (int y = 0; y < n.height(); y += step) {
        const uchar* p = n.constScanLine(y);
        for (int x = 0; x < n.width(); x += step) {
            const float nx = p[x*4+0]/127.5f-1.0f, ny = p[x*4+1]/127.5f-1.0f, nz = p[x*4+2]/127.5f-1.0f;
            err += std::fabs(std::sqrt(nx*nx+ny*ny+nz*nz) - 1.0f); ++cnt;
        }
    }
    return cnt > 0 && (err/cnt) < 0.12;
}

// ORM: R = occlusion, G = roughness, B = metalness (metalMask.R, else 0 = dielectric). Gloss comes
// from B (RG-normal) or A (full-normal); AO from A (RG-normal) or 1.0 (full-normal has none).
QImage buildORM(const QImage& nga, const QImage& metal, bool glossIsRoughness, bool fullNormal)
{
    if (nga.isNull()) return QImage();
    const int w = nga.width(), h = nga.height();
    const QImage n = nga.convertToFormat(QImage::Format_RGBA8888);
    const QImage m = metal.isNull() ? QImage() : metal.convertToFormat(QImage::Format_RGBA8888).scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage orm(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        const uchar* np = n.constScanLine(y);
        const uchar* mp = m.isNull() ? nullptr : m.constScanLine(y);
        uchar* o = orm.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar gloss = fullNormal ? np[x*4+3] : np[x*4+2];   // A (full) or B (RG) = gloss
            const uchar ao    = fullNormal ? 255       : np[x*4+3];   // AO only in RG-normal's A
            o[x*4+0] = ao;
            o[x*4+1] = glossIsRoughness ? gloss : uchar(255 - gloss);
            o[x*4+2] = mp ? mp[x*4+0] : 0;
            o[x*4+3] = 255;
        }
    }
    return orm;
}

// Clean tangent normal: full RGB when B is z, else RG with B = reconstructed z = sqrt(1−x²−y²).
QImage buildNormal(const QImage& nga, bool fullNormal)
{
    if (nga.isNull()) return QImage();
    const int w = nga.width(), h = nga.height();
    const QImage n = nga.convertToFormat(QImage::Format_RGBA8888);
    QImage out(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        const uchar* np = n.constScanLine(y);
        uchar* o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            o[x*4+0] = np[x*4+0];
            o[x*4+1] = np[x*4+1];
            if (fullNormal) {
                o[x*4+2] = np[x*4+2];                              // B already holds z
            } else {
                const float nx = np[x*4+0]/127.5f-1.0f, ny = np[x*4+1]/127.5f-1.0f;
                float nz = 1.0f - nx*nx - ny*ny; nz = nz > 0 ? std::sqrt(nz) : 0.0f;
                o[x*4+2] = uchar(qBound(0, int((nz*0.5f+0.5f)*255.0f+0.5f), 255));
            }
            o[x*4+3] = 255;
        }
    }
    return out;
}

// BasicColourNormalSpec packing: the tangent normal is in the A (x) and G (y) channels; R is a
// specular mask (B is unused). Reconstruct a clean RGB tangent normal (z = √(1−x²−y²)).
QImage buildNormalAG(const QImage& tex)
{
    if (tex.isNull()) return QImage();
    const int w = tex.width(), h = tex.height();
    const QImage n = tex.convertToFormat(QImage::Format_RGBA8888);
    QImage out(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        const uchar* np = n.constScanLine(y); uchar* o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar xr = np[x*4+3];   // A = normal.x
            const uchar yr = np[x*4+1];   // G = normal.y
            o[x*4+0] = xr; o[x*4+1] = yr;
            const float nx = xr/127.5f-1.0f, ny = yr/127.5f-1.0f;
            float nz = 1.0f - nx*nx - ny*ny; nz = nz > 0 ? std::sqrt(nz) : 0.0f;
            o[x*4+2] = uchar(qBound(0, int((nz*0.5f+0.5f)*255.0f+0.5f), 255));
            o[x*4+3] = 255;
        }
    }
    return out;
}

// ORM for BasicColourNormalSpec: a UNIFORM roughness from the Blinn specular_exponent (there is no
// per-texel gloss channel in this family), AO from 1, metal 0. GGX roughness from a Blinn exponent by
// the standard mapping roughness = √(2/(n+2)); clamped to a sane range.
QImage buildUniformORM(int w, int h, float exponent)
{
    if (w <= 0 || h <= 0) return QImage();
    float rough = (exponent > 0.0f) ? std::sqrt(2.0f / (exponent + 2.0f)) : 0.6f;
    rough = qBound(0.20f, rough, 0.95f);   // never mirror-shiny, never dead-flat
    const uchar rb = uchar(rough * 255.0f + 0.5f);
    QImage orm(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) { uchar* o = orm.scanLine(y);
        for (int x = 0; x < w; ++x) { o[x*4+0]=255; o[x*4+1]=rb; o[x*4+2]=0; o[x*4+3]=255; } }
    return orm;
}

// Specular colour (F0) for BasicColourNormalSpec, gated by the R spec mask and kept in the DIELECTRIC
// range (0.03–0.10) so masked metal trim reads as spec without turning the whole surface wet/plastic.
QImage buildSpecFromRMask(const QImage& tex)
{
    if (tex.isNull()) return QImage();
    const int w = tex.width(), h = tex.height();
    const QImage n = tex.convertToFormat(QImage::Format_RGBA8888);
    QImage out(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) { const uchar* np = n.constScanLine(y); uchar* o = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float mask = np[x*4+0]/255.0f;                       // R = spec mask
            const uchar f0 = uchar(qBound(0.0f, 0.03f + 0.07f*mask, 1.0f) * 255.0f + 0.5f);
            o[x*4+0]=o[x*4+1]=o[x*4+2]=f0; o[x*4+3]=255;
        } }
    return out;
}

// Grayscale specular mask (spec-gloss families store it in the albedo's alpha) → an RGB specular
// colour texture for KHR_materials_specular.
QImage buildSpecColorFromAlpha(const QImage& albedo)
{
    if (albedo.isNull()) return QImage();
    const QImage a = albedo.convertToFormat(QImage::Format_RGBA8888);
    QImage out(a.width(), a.height(), QImage::Format_RGBA8888);
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ap = a.constScanLine(y); uchar* o = out.scanLine(y);
        for (int x = 0; x < a.width(); ++x) { const uchar s = ap[x*4+3]; o[x*4+0]=o[x*4+1]=o[x*4+2]=s; o[x*4+3]=255; }
    }
    return out;
}
}  // namespace

QVector<GlbExporter::ExportMaterial> AssetStore::resolveMaterials(const ModelGeometry& geo, bool decodeTextures)
{
    QVector<GlbExporter::ExportMaterial> out;
    out.reserve(geo.materialPaths.size());
    for (const QString& matPath : geo.materialPaths) {
        GlbExporter::ExportMaterial em;
        em.name = QFileInfo(matPath).baseName();
        const QByteArray matData = readFile(matPath, nullptr);
        if (!matData.isEmpty()) {
            const AssetText::Material mat = AssetText::parseMat(matData);
            using WF = AssetText::Workflow;
            // Workflow → PBR flags (docs §6). Metal-rough drives metalness from the metal mask; the
            // spec-gloss/dielectric families are non-metallic and get KHR_materials_specular.
            // Composite mode (authored Force* graph). Mask → glTF MASK (hard cutout); Blend and
            // Additive → glTF BLEND (glTF has no additive, so the export approximates it with straight
            // alpha-over — the viewport does render true additive). alphaMode is carried through for
            // the viewport, which needs additive distinct from blend and must NOT discard on a spec
            // mask (Opaque materials whose albedo A is a spec mask stay opaque here).
            em.alphaMode   = int(mat.alphaMode);
            em.alphaCutout = (mat.alphaMode == AssetText::AlphaMode::Mask);
            em.alphaBlend  = (mat.alphaMode == AssetText::AlphaMode::Blend || mat.alphaMode == AssetText::AlphaMode::Additive);
            em.dielectricSpec = (mat.workflow == WF::DielectricSpecGloss || mat.workflow == WF::SpecGlossSpecMask);
            em.transmissionFactor = mat.hasTranslucency ? 1.0f : 0.0f;
            em.occlusionStrength = qBound(0.0f, mat.occlusionPower, 1.0f);
            if (mat.hasSSS) for (int c = 0; c < 3; ++c) em.subsurface[c] = mat.sssTint[1][c];  // mid (G) depth tint
            if (decodeTextures) {
                if (!mat.albedo.isEmpty())        em.baseColor = loadTexture(mat.albedo, nullptr, nullptr);
                QImage nga = mat.normalGlossAO.isEmpty() ? QImage() : loadTexture(mat.normalGlossAO, nullptr, nullptr);
                QImage metalImg = mat.metalMask.isEmpty() ? QImage() : loadTexture(mat.metalMask, nullptr, nullptr);
                if (!mat.emissiveTex.isEmpty()) { em.emissive = loadTexture(mat.emissiveTex, nullptr, nullptr); em.emissiveStrength = 1.0f; }
                if (mat.normalSpecPacked) {
                    // BasicColourNormalSpec: normal in A,G; spec mask in R; uniform roughness from the
                    // Blinn exponent. Reads the right channels instead of mis-treating it as NormalGlossAO.
                    em.normal            = buildNormalAG(nga);
                    em.metallicRoughness = buildUniformORM(nga.width(), nga.height(), mat.specularExponent);
                    em.hasOcclusion      = false;                            // no baked AO in this family
                    em.specularColor     = buildSpecFromRMask(nga);          // dielectric F0, mask-gated
                    em.dielectricSpec    = true;
                } else {
                    const bool fullN     = !nga.isNull() && normalIsFullRGB(nga);
                    em.normal            = buildNormal(nga, fullN);
                    em.metallicRoughness = buildORM(nga, metalImg, /*glossIsRoughness*/ mat.usesRoughness, fullN);
                    em.hasOcclusion      = !em.metallicRoughness.isNull();   // ORM.R carries AO
                    // Specular colour for KHR_materials_specular. Prefer an explicit RGB SpecularColour_TEX
                    // (true spec-gloss workflow); otherwise, for a SpecMask family, derive a grey specular
                    // colour from the albedo's alpha mask.
                    if (!mat.specColorTex.isEmpty())
                        em.specularColor = loadTexture(mat.specColorTex, nullptr, nullptr);
                    else if (em.dielectricSpec && mat.specMaskInAlbedoAlpha && !em.baseColor.isNull())
                        em.specularColor = buildSpecColorFromAlpha(em.baseColor);
                }
                // FurV2 shell-fur inputs for the viewport preview (glTF has no fur). The strand noise
                // and the fur-length mask drive a fur look instead of the flat-grey no-albedo fallback.
                if (mat.hasFur) {
                    em.isFur    = true;
                    em.furDepth = mat.furDepth;
                    if (!mat.furNoiseTex.isEmpty()) em.furNoise = loadTexture(mat.furNoiseTex, nullptr, nullptr);
                    if (!mat.furMaskTex.isEmpty())  em.furMask  = loadTexture(mat.furMaskTex, nullptr, nullptr);
                }
            }
            // Metalness: driven by the ORM.B for metal-rough; forced 0 for dielectric/spec-gloss.
            em.metalFactor = (mat.workflow == WF::MetalRough && !em.metallicRoughness.isNull()) ? 1.0f : 0.0f;
            em.roughFactor = em.metallicRoughness.isNull() ? 0.8f : 1.0f;
        }
        out.append(em);
    }
    return out;
}

QStringList AssetStore::collectAssetFiles(const QString& modelPath, bool includeTextures, bool includeSkeleton)
{
    QStringList out;
    auto add = [&](const QString& p) {
        if (p.isEmpty()) return;
        const QString lp = p.toLower();
        if (!out.contains(lp) && m_index.find(lp)) out << lp;
        const QString hdr = lp + QStringLiteral(".header");     // sibling dimensions/format header, if any
        if (!out.contains(hdr) && m_index.find(hdr)) out << hdr;
    };
    const QString smd = modelPath.toLower();
    add(smd);
    add(findSmForSmd(smd));                                     // the .sm descriptor
    ModelGeometry geo;
    if (loadModel(smd, geo, nullptr)) {
        for (const QString& mat : geo.materialPaths) {
            add(mat);
            if (includeTextures) {
                const AssetText::Material m = AssetText::parseMat(readFile(mat, nullptr));
                for (const AssetText::MaterialTexture& t : m.textures) add(t.path);   // every referenced .dds
            }
        }
    }
    if (includeSkeleton) {
        const AssetText::AnimatedObject ao = findBodyAo(smd);
        if (ao.valid) { add(ao.selfPath); add(ao.skeletonAst); add(ao.smPath); }
    }
    return out;
}

ModelGeometry AssetStore::assembleForExport(const QString& bodySmdPath, const AstSkeleton::Skeleton& skel)
{
    ModelGeometry merged; QString e;
    if (!loadModel(bodySmdPath, merged, &e)) return merged;
    const Assembly asmbl = attachmentsForModel(bodySmdPath);
    for (const AttachmentPiece& p : asmbl.pieces) {
        if (p.smdPath.isEmpty()) continue;
        int bi = -1;
        for (int i = 0; i < skel.bones.size(); ++i)
            if (skel.bones[i].name.compare(p.bone, Qt::CaseInsensitive) == 0) { bi = i; break; }
        if (bi < 0 || bi > 255) continue;                 // JOINTS_0 is u8 per vertex; skip if unplaceable
        ModelGeometry ag;
        if (!loadModel(p.smdPath, ag, nullptr) || ag.isEmpty()) continue;
        const RigMath::Mat4& T = skel.bones[bi].bind;     // parent bone bind (model-space, native)
        const uint32_t baseVert = uint32_t(merged.vertices.size());
        const int baseMat = merged.materialPaths.size();
        for (const QString& mp : ag.materialPaths) merged.materialPaths.append(mp);
        for (MeshVertex v : ag.vertices) {
            float pt[3], nn[3], tt[3];
            RigMath::transformPoint(T, v.px, v.py, v.pz, pt);
            RigMath::transformDir(T, v.nx, v.ny, v.nz, nn);
            RigMath::transformDir(T, v.tx, v.ty, v.tz, tt);
            v.px = pt[0]; v.py = pt[1]; v.pz = pt[2];
            v.nx = nn[0]; v.ny = nn[1]; v.nz = nn[2];
            v.tx = tt[0]; v.ty = tt[1]; v.tz = tt[2];
            v.joints[0] = uint8_t(bi); v.joints[1] = v.joints[2] = v.joints[3] = 0;
            v.weights[0] = 1.0f; v.weights[1] = v.weights[2] = v.weights[3] = 0.0f;   // rigid to the bone
            merged.vertices.append(v);
        }
        for (const MeshPart& part : ag.parts) {
            MeshPart np; np.name = part.name;
            np.indexStart = uint32_t(merged.indices.size());
            np.indexCount = part.indexCount;
            np.materialIndex = part.materialIndex >= 0 ? baseMat + part.materialIndex : -1;
            for (uint32_t k = 0; k < part.indexCount; ++k)
                merged.indices.append(baseVert + ag.indices[part.indexStart + k]);
            merged.parts.append(np);
        }
    }
    merged.skinned = true;   // the assembled character is skinned (body + bone-weighted attachments)
    merged.computeBounds();
    return merged;
}

AstSkeleton::Skeleton AssetStore::loadSkeletonFor(const QString& modelPath, bool decodeClips, int minBones)
{
    // For a .fmt there is no skeleton.
    if (!modelPath.toLower().endsWith(QStringLiteral(".smd"))) return AstSkeleton::Skeleton{};
    // Scan the model's own art dir for an .ast. Match by directory ID, not by rebuilding dirOf()
    // into a QString across all 1.4M records — that scan dominated the load time.
    AstSkeleton::Skeleton sk;
    uint32_t modelIdx = 0;
    if (m_index.find(modelPath, &modelIdx)) {
        const uint32_t targetDir = m_index.files()[modelIdx].dir;
        const int astExt = m_index.extensions().indexOf(QStringLiteral(".ast"));
        QString astPath;
        if (astExt >= 0) {
            ensureLookups();
            for (uint32_t i : m_dirFiles.value(targetDir)) {   // only this directory's files
                if (m_index.files()[i].extId != uint16_t(astExt)) continue;
                astPath = m_index.pathOf(i); break;
            }
        }
        if (!astPath.isEmpty()) {
            const QByteArray astData = readFile(astPath, nullptr);
            if (!astData.isEmpty()) sk = AstSkeleton::parse(astData, decodeClips, nullptr);
        }
    }

    // Many character/NPC bodies keep their rig in an `animations/` SUBDIR (or elsewhere) that the
    // co-located dir scan above misses — but the body `.ao` DECLARES it explicitly
    // (ClientAnimationController { skeleton = "…ast" }). When the dir scan came up empty, honour that
    // authored path so the body animates (and attachments can follow their bones). Evidence-based:
    // the skeleton comes from the .ao, not a guess.
    if (sk.bones.isEmpty()) {
        const AssetText::AnimatedObject bao = findBodyAo(modelPath);
        if (!bao.skeletonAst.isEmpty()) {
            const QByteArray astData = readFile(bao.skeletonAst, nullptr);
            if (!astData.isEmpty()) sk = AstSkeleton::parse(astData, decodeClips, nullptr);
        }
    }

    // Player-worn meshes (body armour, gloves, boots…) are skinned to the shared CHARACTER BASE RIG,
    // which their own folder does NOT contain and their .ao metadata chain does NOT name — the .ao
    // just `extends "Metadata/SkinBodyArmour"` → `Metadata/Skin`, none of which declare a skeleton
    // (docs/FORMATS.md §7). When the co-located rig is missing or does not cover the mesh's joint
    // palette (minBones, from the caller's geometry), fall back to onerig.ast — the player base rig.
    // Evidence: it covers the tattered robe's 46 joints with correct ordering (0 out-of-range) and
    // animates coherently (bounded, finite); a foreign rig scatters vertices ~62% of the model
    // diagonal vs onerig's ~14%. Gated to the /items/armours/ tree so monster rigs are never
    // replaced by a player rig they don't match.
    if (minBones > 0 && sk.bones.size() < minBones && modelPath.contains(QStringLiteral("/items/armours/"))) {
        static const QString kBaseRig = QStringLiteral("art/models/charactersfour/onerig.ast");
        const QByteArray bd = readFile(kBaseRig, nullptr);
        if (!bd.isEmpty()) {
            AstSkeleton::Skeleton base = AstSkeleton::parse(bd, decodeClips, nullptr);
            if (base.bones.size() >= minBones) {
                base.note = QStringLiteral("character base rig (onerig.ast) — the armour's own folder has no covering skeleton");
                return base;
            }
        }
    }
    return sk;
}

AssetText::AnimatedObject AssetStore::findBodyAo(const QString& bodySmdPath)
{
    const QString body = bodySmdPath.toLower();
    if (!body.endsWith(QStringLiteral(".smd"))) return {};
    const QString bodySm = findSmForSmd(body);
    if (bodySm.isEmpty()) return {};

    // Narrow the .ao candidates: a body's .ao lives under the same trailing directory segments as its
    // mesh (art/models/…/<A>/<B>/x.smd ↔ metadata/…/<A>/<B>/*.ao), so match .ao paths containing the
    // last two directory segments — then CONFIRM by SkinMesh equality (ao.smPath == the body's .sm),
    // never by a name guess. This is a scoped search + validation, not a heuristic pick.
    const QStringList segs = body.section('/', 0, -2).split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString key = segs.size() >= 2 ? segs.mid(segs.size() - 2).join(QLatin1Char('/'))
                                         : (segs.isEmpty() ? QString() : segs.last());
    const int aoExt = m_index.extensions().indexOf(QStringLiteral(".ao"));
    if (aoExt < 0 || key.isEmpty()) return {};

    ensureLookups();
    // Iterate only the .ao files (a small fraction of the index), not every record.
    for (uint32_t i : m_extFiles.value(uint16_t(aoExt))) {
        const QString p = m_index.pathOf(i);
        if (!p.contains(key)) continue;
        const QByteArray d = readFile(p, nullptr);
        if (d.isEmpty()) continue;
        AssetText::AnimatedObject ao = AssetText::parseAo(d);
        if (ao.smPath == bodySm) { ao.selfPath = p; return ao; }   // validated body .ao
    }
    return {};
}

AssetStore::Assembly AssetStore::attachmentsForModel(const QString& bodySmdPath)
{
    Assembly out;
    const AssetText::AnimatedObject bodyAo = findBodyAo(bodySmdPath);
    if (!bodyAo.valid || bodyAo.selfPath.isEmpty()) return out;
    out.bodyAo = bodyAo.selfPath;
    out.skeletonAst = bodyAo.skeletonAst;

    // Resolve each attachment: child .ao → its SkinMesh .sm → the drawable .smd.
    for (const AssetText::AnimatedObject::Attach& at : bodyAo.attachments) {
        AttachmentPiece piece; piece.bone = at.bone; piece.aoPath = at.aoPath;
        const QByteArray cd = readFile(at.aoPath, nullptr);
        if (!cd.isEmpty()) {
            const AssetText::AnimatedObject cao = AssetText::parseAo(cd);
            if (!cao.smPath.isEmpty()) {
                const QByteArray sd = readFile(cao.smPath, nullptr);
                if (!sd.isEmpty()) piece.smdPath = AssetText::parseSm(sd).smdPath.toLower();
            }
        }
        // Label: the in-game name if the mesh has one; else the attachment's folder name (…/attachments
        // /<coat>/rig_x.smd → "coat"), which reads far better than the rig hash; else the .ao stem.
        QString lbl;
        if (!piece.smdPath.isEmpty() && m_nameIndex.isBuilt()) lbl = m_nameIndex.nameFor(piece.smdPath);
        if (lbl.isEmpty() && !piece.smdPath.isEmpty()) {
            const QStringList ps = piece.smdPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
            if (ps.size() >= 2) lbl = ps[ps.size() - 2];
        }
        if (lbl.isEmpty()) lbl = at.aoPath.section('/', -1).section('.', 0, 0);
        piece.label = lbl;
        out.pieces.append(piece);
    }
    return out;
}
