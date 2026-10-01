// Container-only research: resolve an item .ao to the browsable model (.smd/.fmt) via the authored
// chain — .ao (AnimatedObject: skin=.sm) → .sm (SkinnedMeshData: smdPath) → .smd. Confirms the link
// from ItemVisualIdentity.AOFile to the mesh the browser lists.
//
// usage: ao_chain <bundlesDir> <metadata/....ao> [more.ao ...]

#include "store/AssetStore.h"
#include "model/AssetText.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: ao_chain <bundlesDir> <path.ao> [...]\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }

    for (int i = 2; i < argc; ++i) {
        const QString aoPath = QString::fromLocal8Bit(argv[i]).toLower();
        printf("\n%s\n", qPrintable(aoPath));
        const QByteArray ad = store.readFile(aoPath, nullptr);
        if (ad.isEmpty()) { printf("  .ao unreadable (bundle not staged?)\n"); continue; }
        const AssetText::AnimatedObject ao = AssetText::parseAo(ad);
        printf("  extends=%s\n  skin(.sm)=%s\n  skeleton=%s\n", qPrintable(ao.extends), qPrintable(ao.smPath), qPrintable(ao.skeletonAst));
        for (const auto& at : ao.attachments) printf("  attach bone=%s ao=%s\n", qPrintable(at.bone), qPrintable(at.aoPath));
        if (ao.smPath.isEmpty()) { printf("  (no .sm in this .ao — may .extends another, or be static)\n"); continue; }
        const QByteArray sd = store.readFile(ao.smPath.toLower(), nullptr);
        if (sd.isEmpty()) { printf("  .sm unreadable\n"); continue; }
        const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(sd);
        printf("  -> .smd = %s   (%d materials)\n", qPrintable(sm.smdPath), int(sm.materials.size()));
        printf("     in index? %s\n", store.index().find(sm.smdPath) ? "YES" : "no");
    }
    return 0;
}
