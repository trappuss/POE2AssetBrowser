// Container-only research: list index paths containing a substring, or census extensions. For
// finding the game's data tables (.datc64 / .dat) and their locations before designing a name index.
//
// usage: pathgrep <bundlesDir> <substr> [maxHits=60]
//        pathgrep <bundlesDir> --ext            (extension census, all)

#include "store/AssetStore.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <QMap>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: pathgrep <bundlesDir> <substr>|--ext [max]\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();
    const QString arg = QString::fromLocal8Bit(argv[2]).toLower();

    if (arg == QStringLiteral("--ext")) {
        QMap<QString,int> census;
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            const auto& f = idx.files()[i];
            census[f.extId < idx.extensions().size() ? idx.extensions()[f.extId] : QStringLiteral("(none)")]++;
        }
        for (auto it = census.begin(); it != census.end(); ++it) printf("%-14s %8d\n", qPrintable(it.key()), it.value());
        return 0;
    }

    const int maxHits = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 60;
    int hits = 0;
    for (uint32_t i = 0; i < idx.files().size() && hits < maxHits; ++i) {
        const QString p = idx.pathOf(i);
        if (p.contains(arg)) {
            const auto& f = idx.files()[i];
            const QString bundle = f.bundle < idx.bundles().size() ? idx.bundles()[f.bundle].name : QStringLiteral("?");
            printf("%s\t<- %s.bundle.bin\n", qPrintable(p), qPrintable(bundle));
            ++hits;
        }
    }
    fprintf(stderr, "(%d shown)\n", hits);
    return 0;
}
