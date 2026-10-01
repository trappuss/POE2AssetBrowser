// Container-only: end-to-end through the real AssetListModel — the In-game name column populates and
// searching by the true name selects the model.
//
// usage: name_e2e <bundlesDir>

#include "store/AssetStore.h"
#include "index/AssetListModel.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: name_e2e <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    store.buildNameIndex([](const QString& s){ fprintf(stderr, "  %s\n", qPrintable(s)); });

    AssetListModel model;
    model.setIndex(&store.index(), {QStringLiteral(".smd"), QStringLiteral(".fmt")});
    model.setNameIndex(store.isNameIndexReady() ? &store.nameIndex() : nullptr);

    const QString axe = QStringLiteral("art/models/items/weapons/onehandweapons/onehandaxes/basetypes/dullhatchetdrop_2fb69ae7.smd");
    int fails = 0;

    // 1) The In-game name column shows the true name for the axe row.
    model.setQuery(axe);
    QString colName;
    for (int r = 0; r < model.rowCount(); ++r) if (model.pathAt(r) == axe)
        colName = model.data(model.index(r, AssetListModel::ColTrueName), Qt::DisplayRole).toString();
    const bool okCol = (colName == QStringLiteral("Dull Hatchet"));
    if (!okCol) ++fails;
    printf("In-game name column for the axe: '%s'  %s\n", qPrintable(colName), okCol ? "ok" : "FAIL");

    // 2) Searching by the true name (not in the filename) selects the model.
    model.setQuery(QStringLiteral("dull hatchet"));
    bool foundByName = false;
    for (int r = 0; r < model.rowCount(); ++r) if (model.pathAt(r) == axe) { foundByName = true; break; }
    if (!foundByName) ++fails;
    printf("search 'dull hatchet' finds the axe: %s  (%d rows matched)\n", foundByName ? "yes" : "NO", model.rowCount());

    // 3) A shared-mesh variant name also finds its model via the search set.
    model.setQuery(QStringLiteral("rusted cuirass"));
    printf("search 'rusted cuirass' → %d rows\n", model.rowCount());

    printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
