// Container-only research: validate the NPC → MonsterVarieties → .ao chain and confirm which NPC
// column holds the display name. Prints, for a sample of NPCs: col@8, col@56, the col@67 FK index,
// and the MonsterVariety it resolves to (Name col@272 + AO array col@56).
//
// usage: npc_link <bundlesDir>

#include "store/AssetStore.h"
#include "store/DatFile.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: npc_link <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }

    DatFile npc, mv;
    if (!npc.load(store.readFile(QStringLiteral("data/balance/npcs.datc64"), nullptr)) || !npc.isValid()) { fprintf(stderr, "npc load\n"); return 1; }
    if (!mv.load(store.readFile(QStringLiteral("data/balance/monstervarieties.datc64"), nullptr)) || !mv.isValid()) { fprintf(stderr, "mv load\n"); return 1; }
    printf("NPCs=%u rows, MonsterVarieties=%u rows\n\n", npc.rowCount(), mv.rowCount());

    // Link by Id string: an NPC's Id (col@0) is often a MonsterVariety Id (col@0) — that monster's
    // AO array is the NPC's model, and the NPC's col@8 is the nicer display name.
    QHash<QString, uint32_t> mvById;
    for (uint32_t r = 0; r < mv.rowCount(); ++r) { const QString id = mv.str(r, 0); if (!id.isEmpty()) mvById.insert(id.toLower(), r); }

    int matched = 0, shown = 0;
    for (uint32_t r = 0; r < npc.rowCount(); ++r) {
        const QString name = npc.str(r, 8);
        if (name.isEmpty()) continue;
        const QString id = npc.str(r, 0);
        auto it = mvById.find(id.toLower());
        if (it == mvById.end()) continue;
        ++matched;
        const uint32_t m = it.value();
        const uint64_t aoCnt = mv.arrayCount(m, 56);
        const QString ao = aoCnt ? mv.strFromArray(mv.u64(m, 64), 0) : QString();
        if (shown++ < 24)
            printf("  NPC '%s'  id=%s\n      -> MV name='%s'  ao=%s\n", qPrintable(name), qPrintable(id), qPrintable(mv.str(m, 272)), qPrintable(ao));
    }
    printf("\n%d NPCs matched a MonsterVariety by Id\n", matched);
    return 0;
}
