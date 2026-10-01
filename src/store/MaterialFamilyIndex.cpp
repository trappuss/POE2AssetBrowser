#include "store/MaterialFamilyIndex.h"
#include "store/AssetStore.h"
#include "bundle/BundleIndex.h"
#include "model/MeshParser.h"
#include "model/ModelGeometry.h"

#include <QDataStream>
#include <QDir>
#include <QFile>

namespace {
constexpr quint32 kMagic = 0x4d464958;   // 'MFIX'
}

quint8 MaterialFamilyIndex::workflowBit(AssetText::Workflow w)
{
    using W = AssetText::Workflow;
    switch (w) {
        case W::MetalRough:          return WfMetalRough;
        case W::DielectricSpecGloss: return WfDielectric;
        case W::SpecGlossSpecMask:   return WfSpecGloss;
        default:                     return WfOther;   // surface-with-no-canonical-workflow, or effect
    }
}

quint16 MaterialFamilyIndex::internFamily(const QString& stem)
{
    if (stem.isEmpty()) return 0;
    auto it = m_familyIds.find(stem);
    if (it != m_familyIds.end()) return it.value();
    const quint16 id = quint16(m_families.size());
    m_families.append(stem);
    m_familyIds.insert(stem, id);
    return id;
}

QString MaterialFamilyIndex::buildMeta(const QStringList& familyStems, quint8 wfMask, quint8 fxMask)
{
    QStringList toks;
    for (const QString& s : familyStems) if (!s.isEmpty()) toks << QStringLiteral("family:") + s.toLower();
    if (wfMask & WfMetalRough) toks << QStringLiteral("workflow:metalrough");
    if (wfMask & WfDielectric) toks << QStringLiteral("workflow:dielectricspecgloss");
    if (wfMask & WfSpecGloss)  toks << QStringLiteral("workflow:specgloss");
    if (wfMask & WfOther)      toks << QStringLiteral("workflow:other");
    if (fxMask & FxSSS)          toks << QStringLiteral("effect:sss");
    if (fxMask & FxTranslucency) toks << QStringLiteral("effect:translucency");
    if (fxMask & FxFur)          toks << QStringLiteral("effect:fur");
    if (fxMask & FxAlphaTest)    toks << QStringLiteral("effect:alphatest");
    return toks.join(QLatin1Char(' '));
}

bool MaterialFamilyIndex::build(AssetStore& store, const std::function<void(const QString&)>& progress)
{
    if (!store.isOpen()) return false;
    const BundleIndex& idx = store.index();
    const auto say = [&](const QString& s) { if (progress) progress(s); };

    const int matExt = idx.extensions().indexOf(QStringLiteral(".mat"));
    const int smExt  = idx.extensions().indexOf(QStringLiteral(".sm"));

    // ── Pass 1: every .mat → family / workflow / effects, keyed by its path hash. ──
    int nMat = 0, matFail = 0;
    if (matExt >= 0) {
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            if (idx.files()[i].extId != uint16_t(matExt)) continue;
            const QByteArray data = store.readFile(i, nullptr);
            if (data.isEmpty()) { ++matFail; continue; }
            const AssetText::Material m = AssetText::parseMat(data);
            MatRec r;
            r.familyId = internFamily(m.family);
            r.workflow = quint8(m.workflow);
            r.effects  = quint8((m.hasSSS ? FxSSS : 0) | (m.hasTranslucency ? FxTranslucency : 0)
                              | (m.hasFur ? FxFur : 0) | (m.alphaTest ? FxAlphaTest : 0));
            m_mat.insert(idx.files()[i].hash, r);
            if ((++nMat % 20000) == 0) say(QStringLiteral("materials: %1 parsed…").arg(nMat));
        }
    }
    say(QStringLiteral("materials: %1 parsed (%2 families, %3 read-failed)").arg(nMat).arg(m_families.size() - 1).arg(matFail));

    // ── Pass 2: every .sm → the model it skins inherits its materials' families / workflows. ──
    // A single .smd can be skinned by SEVERAL .sm (LOD / localized / variant descriptors), each
    // naming a different material subset, so accumulate the UNION across all of them — an insert per
    // .sm would let the last one seen overwrite the rest and silently drop families.
    struct Acc { quint8 wf = 0, fx = 0; QVector<quint16> fams; };
    QHash<quint64, Acc> acc;
    int nSm = 0, smFail = 0;
    if (smExt >= 0) {
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            if (idx.files()[i].extId != uint16_t(smExt)) continue;
            const QByteArray data = store.readFile(i, nullptr);
            if (data.isEmpty()) { ++smFail; continue; }
            const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(data);
            if (sm.smdPath.isEmpty()) continue;

            Acc& a = acc[BundleIndex::hashPath(sm.smdPath)];
            for (const auto& run : sm.materials) {
                auto it = m_mat.find(BundleIndex::hashPath(run.matPath));
                if (it == m_mat.end()) continue;
                const MatRec& mr = it.value();
                a.wf |= workflowBit(AssetText::Workflow(mr.workflow));
                a.fx |= mr.effects;
                if (mr.familyId && !a.fams.contains(mr.familyId)) a.fams << mr.familyId;
            }
            if ((++nSm % 10000) == 0) say(QStringLiteral("descriptors: %1 read…").arg(nSm));
        }
    }
    // ── Pass 2b: static .fmt v9 meshes carry their materials in their OWN string table (no .sm), so
    // parse each and take geo.materialPaths directly. (v4–8 are a legacy pre-DOLm layout parseFmt
    // rejects; those stay unclassified — a clean gap, not a wrong guess.) ──
    const int fmtExt = idx.extensions().indexOf(QStringLiteral(".fmt"));
    int nFmt = 0, fmtFail = 0;
    if (fmtExt >= 0) {
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            if (idx.files()[i].extId != uint16_t(fmtExt)) continue;
            const QByteArray data = store.readFile(i, nullptr);
            if (data.isEmpty()) { ++fmtFail; continue; }
            const QString path = idx.pathOf(i);
            ModelGeometry geo;
            if (!MeshParser::parseFmt(data, path, geo, nullptr)) continue;   // v4–8 (and any bad file) skipped
            Acc& a = acc[BundleIndex::hashPath(path)];
            for (const QString& matPath : geo.materialPaths) {
                auto it = m_mat.find(BundleIndex::hashPath(matPath));
                if (it == m_mat.end()) continue;
                const MatRec& mr = it.value();
                a.wf |= workflowBit(AssetText::Workflow(mr.workflow));
                a.fx |= mr.effects;
                if (mr.familyId && !a.fams.contains(mr.familyId)) a.fams << mr.familyId;
            }
            if ((++nFmt % 10000) == 0) say(QStringLiteral("static meshes: %1 parsed…").arg(nFmt));
        }
    }
    say(QStringLiteral("static meshes: %1 .fmt v9 classified (%2 unreadable)").arg(nFmt).arg(fmtFail));

    // Finalize: one ModelRec per model, meta built from the merged family/workflow/effect set.
    m_models.reserve(acc.size());
    for (auto it = acc.begin(); it != acc.end(); ++it) {
        QStringList fams;
        for (quint16 id : it.value().fams) fams << m_families[id];
        m_models.insert(it.key(), ModelRec{it.value().wf, it.value().fx, buildMeta(fams, it.value().wf, it.value().fx)});
    }
    say(QStringLiteral("models: %1 classified from %2 .sm (%3 read-failed)").arg(m_models.size()).arg(nSm).arg(smFail));

    m_built = true;
    return true;
}

QString MaterialFamilyIndex::metaForModel(const QString& modelPath) const
{
    auto it = m_models.find(BundleIndex::hashPath(modelPath));
    return it == m_models.end() ? QString() : it.value().meta;
}

QString MaterialFamilyIndex::metaForMaterial(const QString& matPath) const
{
    auto it = m_mat.find(BundleIndex::hashPath(matPath));
    if (it == m_mat.end()) return QString();
    const MatRec& r = it.value();
    QStringList fams; if (r.familyId) fams << m_families[r.familyId];
    return buildMeta(fams, workflowBit(AssetText::Workflow(r.workflow)), r.effects);
}

quint8 MaterialFamilyIndex::modelWorkflowMask(const QString& modelPath) const
{
    auto it = m_models.find(BundleIndex::hashPath(modelPath));
    return it == m_models.end() ? 0 : it.value().workflowMask;
}

quint8 MaterialFamilyIndex::modelEffectMask(const QString& modelPath) const
{
    auto it = m_models.find(BundleIndex::hashPath(modelPath));
    return it == m_models.end() ? 0 : it.value().effectMask;
}

// ── Cache ──────────────────────────────────────────────────────────────────────────────────────
bool MaterialFamilyIndex::save(const QString& cacheDir, const QString& fingerprint, QString* why) const
{
    if (cacheDir.isEmpty()) { if (why) *why = QStringLiteral("no cache dir"); return false; }
    QDir().mkpath(cacheDir);
    const QString file = QDir(cacheDir).filePath(QStringLiteral("material_index_v%1.bin").arg(kCacheVersion));
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (why) *why = QStringLiteral("open for write failed"); return false; }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kMagic << kCacheVersion << fingerprint;
    ds << m_families;
    ds << quint32(m_mat.size());
    for (auto it = m_mat.begin(); it != m_mat.end(); ++it)
        ds << quint64(it.key()) << it.value().familyId << it.value().workflow << it.value().effects;
    ds << quint32(m_models.size());
    for (auto it = m_models.begin(); it != m_models.end(); ++it)
        ds << quint64(it.key()) << it.value().workflowMask << it.value().effectMask << it.value().meta;
    ds << kMagic;   // trailer
    if (ds.status() != QDataStream::Ok) { if (why) *why = QStringLiteral("stream error"); return false; }
    return true;
}

bool MaterialFamilyIndex::load(const QString& cacheDir, const QString& fingerprint, QString* why)
{
    if (cacheDir.isEmpty()) { if (why) *why = QStringLiteral("no cache dir"); return false; }
    const QString file = QDir(cacheDir).filePath(QStringLiteral("material_index_v%1.bin").arg(kCacheVersion));
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) { if (why) *why = QStringLiteral("no cache file"); return false; }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, ver = 0; QString fp;
    ds >> magic >> ver >> fp;
    if (magic != kMagic || ver != kCacheVersion) { if (why) *why = QStringLiteral("different cache version"); return false; }
    if (fp != fingerprint) { if (why) *why = QStringLiteral("game index changed"); return false; }

    QStringList families; ds >> families;
    quint32 nMat = 0; ds >> nMat;
    QHash<quint64, MatRec> mat; mat.reserve(int(nMat));
    for (quint32 k = 0; k < nMat; ++k) {
        quint64 h; MatRec r; ds >> h >> r.familyId >> r.workflow >> r.effects;
        mat.insert(h, r);
    }
    quint32 nModel = 0; ds >> nModel;
    QHash<quint64, ModelRec> models; models.reserve(int(nModel));
    for (quint32 k = 0; k < nModel; ++k) {
        quint64 h; ModelRec r; ds >> h >> r.workflowMask >> r.effectMask >> r.meta;
        models.insert(h, r);
    }
    quint32 trailer = 0; ds >> trailer;
    if (trailer != kMagic || ds.status() != QDataStream::Ok) { if (why) *why = QStringLiteral("cache truncated"); return false; }

    m_families = families;
    m_familyIds.clear();
    for (int i = 0; i < m_families.size(); ++i) m_familyIds.insert(m_families[i], quint16(i));
    m_mat = std::move(mat);
    m_models = std::move(models);
    m_built = true;
    return true;
}

QString MaterialFamilyIndex::selfTest()
{
    // workflow → single bit
    if (workflowBit(AssetText::Workflow::MetalRough) != WfMetalRough) return QStringLiteral("MFIX: metalrough bit");
    if (workflowBit(AssetText::Workflow::DielectricSpecGloss) != WfDielectric) return QStringLiteral("MFIX: dielectric bit");
    if (workflowBit(AssetText::Workflow::SpecGlossSpecMask) != WfSpecGloss) return QStringLiteral("MFIX: specgloss bit");
    if (workflowBit(AssetText::Workflow::Unknown) != WfOther) return QStringLiteral("MFIX: unknown→other bit");
    // meta string tokens
    const QString meta = buildMeta({QStringLiteral("MetalRoughBN")}, WfMetalRough | WfOther, FxSSS | FxAlphaTest);
    if (!meta.contains(QStringLiteral("family:metalroughbn"))) return QStringLiteral("MFIX: meta family token");
    if (!meta.contains(QStringLiteral("workflow:metalrough"))) return QStringLiteral("MFIX: meta workflow token");
    if (!meta.contains(QStringLiteral("effect:sss")) || !meta.contains(QStringLiteral("effect:alphatest"))) return QStringLiteral("MFIX: meta effect token");
    // family interning dedups and reserves 0 for "".
    MaterialFamilyIndex t;
    if (t.internFamily(QString()) != 0) return QStringLiteral("MFIX: empty family must be id 0");
    const quint16 a = t.internFamily(QStringLiteral("Hair"));
    const quint16 b = t.internFamily(QStringLiteral("Hair"));
    if (a != b || a == 0) return QStringLiteral("MFIX: family intern must dedup to a stable non-zero id");
    return QString();
}
