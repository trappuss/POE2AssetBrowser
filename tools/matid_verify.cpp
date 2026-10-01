// Container-only: prove find-by-id through the real AssetListModel — a model's MurmurHash64 path id,
// searched in decimal and in 0x-hex, resolves to exactly that model.
//
// usage: matid_verify <bundlesDir>

#include "store/AssetStore.h"
#include "index/AssetListModel.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: matid_verify <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();

    AssetListModel model;
    model.setIndex(&store.index(), {QStringLiteral(".smd"), QStringLiteral(".fmt")});

    // Pick a handful of models spread across the base set and round-trip their id.
    int checked = 0, fails = 0;
    const int smd = idx.extensions().indexOf(QStringLiteral(".smd"));
    for (uint32_t i = 0; i < idx.files().size() && checked < 8; ++i) {
        if (idx.files()[i].extId != uint16_t(smd)) continue;
        if ((i % 997) != 0) continue;   // spread the sample
        const auto& f = idx.files()[i];
        const QString path = idx.pathOf(i);
        const quint64 id = f.hash;
        ++checked;

        const QString dec = QString::number(id);
        const QString hex = QStringLiteral("0x") + QStringLiteral("%1").arg(id, 16, 16, QLatin1Char('0'));
        auto findsOnlyThis = [&](const QString& q) {
            model.setQuery(q);
            // The exact id must select this row (and, being a 64-bit hash, essentially only this row).
            bool found = false;
            for (int r = 0; r < model.rowCount(); ++r) if (model.pathAt(r) == path) { found = true; break; }
            return found && model.rowCount() <= 2;   // allow a rare hash-substring neighbour
        };
        const bool okDec = findsOnlyThis(dec);
        const bool okHex = findsOnlyThis(hex);
        if (!okDec || !okHex) { ++fails; printf("  FAIL %s\n    dec=%s ok=%d rows=%d ; hex=%s ok=%d\n",
            qPrintable(path), qPrintable(dec), okDec, model.rowCount(), qPrintable(hex), okHex); }
        else printf("  ok  %s  (0x%016llx)\n", qPrintable(path), (unsigned long long)id);
    }
    printf("\nchecked %d ids  fails=%d\nRESULT: %s\n", checked, fails, fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
