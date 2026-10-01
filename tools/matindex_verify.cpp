// Container-only: build the MaterialFamilyIndex over real data, round-trip its cache, and spot-check
// a few models' classification against an independent .sm→.mat resolution.
//
// usage: matindex_verify <bundlesDir>

#include "store/AssetStore.h"
#include "store/MaterialFamilyIndex.h"
#include "model/AssetText.h"
#include "model/MeshParser.h"
#include "model/ModelGeometry.h"
#include "bundle/BundleIndex.h"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: matindex_verify <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }

    printf("selfTest: %s\n", MaterialFamilyIndex::selfTest().isEmpty() ? "PASS" : qPrintable(MaterialFamilyIndex::selfTest()));

    MaterialFamilyIndex mfx;
    mfx.build(store, [](const QString& s){ fprintf(stderr, "  %s\n", qPrintable(s)); });
    printf("built: materials=%d models=%d families=%d\n", mfx.materialCount(), mfx.modelCount(), mfx.families().size() - 1);

    // Cache round-trip.
    QString why;
    const QString cacheDir = QStringLiteral("/root/work/matcache");
    if (!mfx.save(cacheDir, store.index().fingerprint(), &why)) { printf("save FAILED: %s\n", qPrintable(why)); return 1; }
    MaterialFamilyIndex mfx2;
    if (!mfx2.load(cacheDir, store.index().fingerprint(), &why)) { printf("load FAILED: %s\n", qPrintable(why)); return 1; }
    printf("cache round-trip: materials=%d models=%d families=%d\n", mfx2.materialCount(), mfx2.modelCount(), mfx2.families().size() - 1);

    // Spot-check: sample .smd models, compare metaForModel(built) vs metaForModel(loaded), and vs an
    // independent .sm-side resolution.
    const BundleIndex& idx = store.index();
    const int smdExt = idx.extensions().indexOf(QStringLiteral(".smd"));
    const int smExt  = idx.extensions().indexOf(QStringLiteral(".sm"));

    // Build an independent smd→families map straight from .sm + parseMat (no MaterialFamilyIndex).
    int checked = 0, cacheMismatch = 0, indepMismatch = 0, classified = 0, shown = 0;
    for (uint32_t i = 0; i < idx.files().size() && checked < 4000; ++i) {
        if (idx.files()[i].extId != uint16_t(smExt)) continue;
        const QByteArray sd = store.readFile(i, nullptr); if (sd.isEmpty()) continue;
        const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(sd);
        if (sm.smdPath.isEmpty()) continue;
        ++checked;

        // Independent expected family/workflow set.
        QStringList fams;
        for (const auto& run : sm.materials) {
            const QByteArray md = store.readFile(run.matPath, nullptr); if (md.isEmpty()) continue;
            const AssetText::Material m = AssetText::parseMat(md);
            if (!m.family.isEmpty() && !fams.contains(m.family)) fams << m.family;
        }
        const QString builtMeta  = mfx.metaForModel(sm.smdPath);
        const QString loadedMeta = mfx2.metaForModel(sm.smdPath);
        if (builtMeta != loadedMeta) { ++cacheMismatch; if (cacheMismatch <= 3) printf("  CACHE MISMATCH %s\n    built='%s'\n   loaded='%s'\n", qPrintable(sm.smdPath), qPrintable(builtMeta), qPrintable(loadedMeta)); }
        if (!builtMeta.isEmpty()) ++classified;

        // Every independently-found family must appear in the built meta.
        for (const QString& fstem : fams)
            if (!builtMeta.contains(QStringLiteral("family:") + fstem.toLower())) {
                ++indepMismatch;
                if (indepMismatch <= 5) printf("  INDEP MISMATCH %s missing family:%s  (meta='%s')\n",
                    qPrintable(sm.smdPath), qPrintable(fstem.toLower()), qPrintable(builtMeta));
                break;
            }
        if (shown < 4 && !fams.isEmpty()) { printf("  e.g. %s -> %s\n", qPrintable(sm.smdPath), qPrintable(builtMeta)); ++shown; }
    }
    printf("\nspot-check over %d .sm: classified=%d  cacheMismatch=%d  indepMismatch=%d\n",
           checked, classified, cacheMismatch, indepMismatch);

    // Independent .fmt v9 spot-check: parseFmt → materialPaths → parseMat family, and require the
    // index's meta to contain each family.
    const int fmtExt = idx.extensions().indexOf(QStringLiteral(".fmt"));
    int fmtChecked = 0, fmtClassified = 0, fmtIndepMismatch = 0;
    for (uint32_t i = 0; i < idx.files().size() && fmtChecked < 4000; ++i) {
        if (idx.files()[i].extId != uint16_t(fmtExt)) continue;
        const QByteArray fd = store.readFile(i, nullptr); if (fd.isEmpty()) continue;
        ModelGeometry geo;
        if (!MeshParser::parseFmt(fd, idx.pathOf(i), geo, nullptr)) continue;   // v4–8 skipped
        ++fmtChecked;
        const QString meta = mfx.metaForModel(idx.pathOf(i));
        if (!meta.isEmpty()) ++fmtClassified;
        QStringList fams;
        for (const QString& mp : geo.materialPaths) {
            const QByteArray md = store.readFile(mp, nullptr); if (md.isEmpty()) continue;
            const AssetText::Material m = AssetText::parseMat(md);
            if (!m.family.isEmpty() && !fams.contains(m.family)) fams << m.family;
        }
        for (const QString& fstem : fams)
            if (!meta.contains(QStringLiteral("family:") + fstem.toLower())) {
                ++fmtIndepMismatch;
                if (fmtIndepMismatch <= 5) printf("  FMT MISMATCH %s missing family:%s (meta='%s')\n",
                    qPrintable(idx.pathOf(i)), qPrintable(fstem.toLower()), qPrintable(meta));
                break;
            }
    }
    printf("spot-check over %d .fmt v9: classified=%d  indepMismatch=%d\n", fmtChecked, fmtClassified, fmtIndepMismatch);

    const bool ok = (cacheMismatch == 0 && indepMismatch == 0 && classified > 0 && fmtIndepMismatch == 0);
    printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    (void)smdExt;
    return ok ? 0 : 1;
}
