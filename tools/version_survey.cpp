// Container-only research: tally the format-version byte (byte 0) of every model-like asset, so a
// decision to parse more versions rests on what the game actually ships, not a guess.
//
// usage: version_survey <bundlesDir>

#include "store/AssetStore.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <QMap>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: version_survey <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();

    // Every extension in the index whose name looks model-ish, plus a full extension census so we
    // don't miss a format we didn't think of.
    QMap<QString,int> extCensus;
    for (uint32_t i = 0; i < idx.files().size(); ++i) {
        const auto& f = idx.files()[i];
        const QString e = f.extId < idx.extensions().size() ? idx.extensions()[f.extId] : QStringLiteral("(none)");
        extCensus[e]++;
    }
    printf("=== extension census (top model/mesh-ish) ===\n");
    for (auto it = extCensus.begin(); it != extCensus.end(); ++it) {
        const QString& e = it.key();
        if (e.contains(QStringLiteral("md")) || e.contains(QStringLiteral("fmt")) || e.contains(QStringLiteral("fm"))
            || e.contains(QStringLiteral("mesh")) || e.contains(QStringLiteral("tmd")) || e.contains(QStringLiteral("sm")))
            printf("  %-10s %8d\n", qPrintable(e), it.value());
    }

    auto surveyExt = [&](const char* extName) {
        const int ext = idx.extensions().indexOf(QString::fromLatin1(extName));
        if (ext < 0) { printf("\n%s: not present in index\n", extName); return; }
        QMap<int,int> vers; int n = 0, empty = 0;
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            if (idx.files()[i].extId != uint16_t(ext)) continue;
            ++n;
            const QByteArray d = store.readFile(i, nullptr);
            if (d.isEmpty()) { ++empty; continue; }
            vers[uint8_t(d[0])]++;
        }
        printf("\n%s: %d files (%d unreadable) — first-byte (version) histogram:\n", extName, n, empty);
        for (auto it = vers.begin(); it != vers.end(); ++it)
            printf("    version %-3d : %8d\n", it.key(), it.value());
    };

    surveyExt(".smd");
    surveyExt(".fmt");
    surveyExt(".tmd");
    surveyExt(".fm");
    return 0;
}
