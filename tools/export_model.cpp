// Container-only: drive the REAL production export pipeline headless — AssetStore::loadModel +
// loadSkeletonFor(decodeClips) + resolveMaterials + GlbExporter::write, exactly as the GUI does —
// so a session can reproduce and verify a user's .glb export without the GUI. Also --locate prints
// which bundle holds a path (so only the needed bundles need staging).
//
// usage: export_model <bundlesDir> <game/model/path.smd> [out.glb] [--scale S] [--no-tex] [--locate]

#include "store/AssetStore.h"
#include "model/GlbExporter.h"
#include "model/AstSkeleton.h"
#include "bundle/BundleIndex.h"

#include <QString>
#include <QFile>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: export_model <bundlesDir> <model.smd> [out.glb] [--scale S] [--no-tex] [--locate]\n"); return 2; }
    const QString bundlesDir = QString::fromLocal8Bit(argv[1]);
    const QString modelPath  = QString::fromLocal8Bit(argv[2]).toLower();
    QString outGlb; float scale = GlbExporter::Options{}.unitScale; bool tex = true, locate = false, yaw180 = false; int onlyClip = -1;
    for (int i = 3; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if      (a == "--scale" && i+1 < argc) scale = QString::fromLocal8Bit(argv[++i]).toFloat();
        else if (a == "--clip" && i+1 < argc)  onlyClip = QString::fromLocal8Bit(argv[++i]).toInt();
        else if ((a == "--forceast" || a == "--dump" || a == "--tex") && i+1 < argc) ++i;  // value consumed elsewhere
        else if (a == "--no-tex")              tex = false;
        else if (a == "--yaw")                 yaw180 = true;
        else if (a == "--locate")              locate = true;
        else if (!a.startsWith("--"))          outGlb = a;
    }

    AssetStore store; QString err;
    fprintf(stderr, "opening index...\n");
    if (!store.open(bundlesDir, &err)) { fprintf(stderr, "open failed: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();

    auto bundleOf = [&](const QString& p) -> QString {
        uint32_t fi = 0; if (!idx.find(p, &fi)) return QString("<not in index>");
        return idx.bundles()[idx.files()[fi].bundle].name;
    };

    // --dump <out>: write the decompressed bytes of <modelPath> (any asset) to <out>. For analysing
    // .mat / .sm / .dds.header etc. outside the app.
    if (!outGlb.isEmpty() && outGlb != QString()) { /* outGlb reused as dump target when --dump set */ }
    {
        for (int i = 3; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--dump" && i+1 < argc) {
            const QString dst = QString::fromLocal8Bit(argv[i+1]);
            QString e2; const QByteArray d = store.readFile(modelPath, &e2);
            if (d.isEmpty()) { fprintf(stderr, "dump: read failed: %s\n", qPrintable(e2)); return 1; }
            QFile f(dst); if (!f.open(QIODevice::WriteOnly)) { fprintf(stderr, "dump: open out failed\n"); return 1; }
            f.write(d); fprintf(stderr, "dumped %lld bytes -> %s\n", (long long)d.size(), qPrintable(dst));
            return 0;
        }
    }

    // --tex <out.png>: decode a .dds via the app's DdsImage and save RGBA PNG (for channel analysis).
    {
        for (int i = 3; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--tex" && i+1 < argc) {
            const QString dst = QString::fromLocal8Bit(argv[i+1]);
            QString e2; QImage img = store.loadTexture(modelPath, &e2, nullptr);
            if (img.isNull()) { fprintf(stderr, "tex: decode failed: %s\n", qPrintable(e2)); return 1; }
            if (!img.save(dst)) { fprintf(stderr, "tex: save failed\n"); return 1; }
            fprintf(stderr, "decoded %dx%d -> %s\n", img.width(), img.height(), qPrintable(dst));
            return 0;
        }
    }

    if (modelPath.startsWith("grep:")) {
        const QString needle = modelPath.mid(5);
        int hits = 0;
        for (uint32_t i = 0; i < idx.files().size() && hits < 40; ++i) {
            const QString p = idx.pathOf(i);
            if (p.contains(needle)) { printf("%s\n", qPrintable(p)); ++hits; }
        }
        return 0;
    }

    if (locate) {
        printf("model  %s -> bundle %s\n", qPrintable(modelPath), qPrintable(bundleOf(modelPath)));
        // also the .ast in the same dir + the .sm
        const uint32_t dir = [&]{ uint32_t fi=0; idx.find(modelPath,&fi); return idx.files()[fi].dir; }();
        const int astExt = idx.extensions().indexOf(QStringLiteral(".ast"));
        const int smExt  = idx.extensions().indexOf(QStringLiteral(".sm"));
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            const auto& f = idx.files()[i];
            if (f.dir != dir) continue;
            if (f.extId == uint16_t(astExt) || f.extId == uint16_t(smExt))
                printf("  %s -> bundle %s\n", qPrintable(idx.pathOf(i)), qPrintable(idx.bundles()[f.bundle].name));
        }
        return 0;
    }

    ModelGeometry geo;
    if (!store.loadModel(modelPath, geo, &err)) { fprintf(stderr, "loadModel failed: %s\n", qPrintable(err)); return 1; }
    QString forceAst;
    for (int i = 3; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--forceast" && i+1 < argc) forceAst = QString::fromLocal8Bit(argv[i+1]);
    AstSkeleton::Skeleton sk = forceAst.isEmpty() ? store.loadSkeletonFor(modelPath, /*decodeClips*/true, geo.jointPaletteSize())
                                                  : AstSkeleton::parse(store.readFile(forceAst, nullptr), true, nullptr);
    bool attach = false; for (int i = 3; i < argc; ++i) if (QString::fromLocal8Bit(argv[i]) == "--attach") attach = true;
    if (attach) { geo = store.assembleForExport(modelPath, sk); fprintf(stderr, "assembled with attachments: verts=%d parts=%d mats=%d\n", geo.vertices.size(), geo.parts.size(), geo.materialPaths.size()); }
    QVector<GlbExporter::ExportMaterial> mats = store.resolveMaterials(geo, /*decodeTextures*/tex);

    printf("loaded  verts=%d parts=%d mats=%d bones=%d clips=%d clipsDecoded=%d\n",
           geo.vertices.size(), geo.parts.size(), geo.materialPaths.size(),
           sk.bones.size(), sk.clips.size(), sk.clipsDecoded ? 1 : 0);
    for (const QString& mp : geo.materialPaths)
        printf("  mat: %s -> bundle %s\n", qPrintable(mp), qPrintable(bundleOf(mp)));

    if (outGlb.isEmpty()) return 0;
    GlbExporter::Options opt; opt.unitScale = scale; opt.includeAnimations = sk.clipsDecoded; opt.embedTextures = tex; opt.yaw180 = yaw180; opt.onlyClip = onlyClip;
    if (!GlbExporter::write(geo, sk, mats, opt, outGlb, &err)) { fprintf(stderr, "export failed: %s\n", qPrintable(err)); return 1; }
    QFile gf(outGlb);
    printf("wrote   %s  (%lld bytes, scale=%.4f, anims=%d)\n", qPrintable(outGlb), gf.size(), scale, opt.includeAnimations ? 1 : 0);
    return 0;
}
