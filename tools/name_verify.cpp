// Container-only: build the NameIndex over real data, round-trip its cache, and spot-check known
// items resolve to their true names.
//
// usage: name_verify <bundlesDir>

#include "store/AssetStore.h"
#include "store/NameIndex.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: name_verify <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }

    printf("selfTest: %s\n", NameIndex::selfTest().isEmpty() ? "PASS" : qPrintable(NameIndex::selfTest()));

    NameIndex nx;
    if (!nx.build(store, [](const QString& s){ fprintf(stderr, "  %s\n", qPrintable(s)); })) { fprintf(stderr, "build failed\n"); return 1; }
    printf("named models: %d\n", nx.namedModels());

    // Cache round-trip.
    QString why; const QString cacheDir = QStringLiteral("/root/work/namecache");
    if (!nx.save(cacheDir, store.index().fingerprint(), &why)) { printf("save FAILED: %s\n", qPrintable(why)); return 1; }
    NameIndex nx2;
    if (!nx2.load(cacheDir, store.index().fingerprint(), &why)) { printf("load FAILED: %s\n", qPrintable(why)); return 1; }
    printf("cache round-trip: named=%d\n", nx2.namedModels());

    // Spot-check known items (these .smd resolved by hand in tools/ao_chain.cpp).
    struct C { const char* path; const char* want; };
    const C cases[] = {
        {"art/models/items/weapons/onehandweapons/onehandaxes/basetypes/dullhatchetdrop_2fb69ae7.smd", "Dull Hatchet"},
        {"art/models/items/armours/bodyarmours/armoursfour/rustedcuirass/rustedcuirass_drop_6e7f190b.smd", "Rusted Cuirass"},
    };
    int fails = 0;
    for (const C& c : cases) {
        const QString got = nx.nameFor(QString::fromLatin1(c.path));
        const QString got2 = nx2.nameFor(QString::fromLatin1(c.path));
        const bool ok = (got == QString::fromLatin1(c.want)) && (got == got2);
        if (!ok) ++fails;
        printf("  %s\n    got='%s' (cache='%s') want='%s'  %s\n", c.path, qPrintable(got), qPrintable(got2), c.want, ok ? "ok" : "FAIL");
    }

    // Monster: a body can be shared by several monsters, so the primary is one valid monster name and
    // every monster that uses the mesh stays in the search set.
    {
        const QString fish = QStringLiteral("art/models/monsters/parasites/fshparasite/fshparasite_armour_4d3286d6.smd");
        const QString primary = nx.nameFor(fish);
        const QString search = nx.searchNamesFor(fish);
        const bool ok = !primary.isEmpty() && search.contains(QStringLiteral("chyme skitterer"));
        if (!ok) ++fails;
        printf("  monster %s\n    primary='%s'  search-has 'chyme skitterer'=%d  %s\n",
               qPrintable(QFileInfo(fish).fileName()), qPrintable(primary), search.contains(QStringLiteral("chyme skitterer")), ok ? "ok" : "FAIL");
    }

    // Item icon (.dds): named directly from ItemVisualIdentity.DDSFile → BaseItemTypes.Name.
    {
        const QString icon = QStringLiteral("art/2ditems/currency/currencyweaponquality.dds");
        const QString n = nx.nameFor(icon);
        const bool ok = (n == QStringLiteral("Blacksmith's Whetstone"));
        if (!ok) ++fails;
        printf("  icon %s => '%s'  %s\n", qPrintable(icon), qPrintable(n), ok ? "ok" : "FAIL (or icon bundle absent)");
    }

    // A sample of resolved names.
    const BundleIndex& idx = store.index();
    const int smd = idx.extensions().indexOf(QStringLiteral(".smd"));
    int shown = 0;
    printf("\nsample named models:\n");
    for (uint32_t i = 0; i < idx.files().size() && shown < 20; ++i) {
        if (idx.files()[i].extId != uint16_t(smd)) continue;
        const QString p = idx.pathOf(i);
        const QString n = nx.nameFor(p);
        if (n.isEmpty()) continue;
        printf("  %-70s => \"%s\"\n", qPrintable(QFileInfo(p).fileName()), qPrintable(n));
        ++shown;
    }
    printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
