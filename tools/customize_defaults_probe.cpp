// Dump each class's default-outfit pieces (aliasSlot + resolved mesh) so the hair/head/fx toggles can
// key on the REAL alias tokens the data uses, not guessed ones.
#include "store/AssetStore.h"
#include "store/DatFile.h"
#include "model/AssetText.h"
#include <QCoreApplication>
#include <QSet>
#include <cstdio>

int main(int argc, char** argv){
    QCoreApplication app(argc, argv);
    if (argc < 2){ fprintf(stderr,"usage: customize_defaults_probe <bundles>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(argv[1], &err)){ fprintf(stderr,"open: %s\n", qPrintable(err)); return 1; }

    DatFile t;
    if (!t.load(store.readFile(QStringLiteral("data/balance/characters.datc64"))) || !t.isValid()){ fprintf(stderr,"no characters table\n"); return 1; }
    QSet<QString> seenAlias;
    for (uint32_t r = 0; r < t.rowCount(); ++r){
        const QString disp = t.str(r,8).trimmed();
        const QString ao   = t.str(r,16).trimmed().toLower();
        if (disp.isEmpty() || ao.isEmpty()) continue;
        AssetText::AnimatedObject a = AssetText::parseAo(store.readFile(ao));
        if (a.smPath.isEmpty()) continue;
        printf("== %s  (%s) ==\n", qPrintable(disp), qPrintable(ao));
        for (const auto& at : a.attachments){
            QString child = at.aoPath, alias;
            const int ai = child.indexOf(QStringLiteral(" alias "));
            if (ai >= 0){ alias = child.mid(ai+7).trimmed(); child = child.left(ai).trimmed(); }
            child = child.trimmed();
            AssetText::AnimatedObject cao = AssetText::parseAo(store.readFile(child));
            AssetText::SkinnedMeshDesc csm = AssetText::parseSm(store.readFile(cao.smPath));
            printf("   alias=%-22s bone=%-30s mesh=%s\n",
                   qPrintable(alias.isEmpty()?QStringLiteral("(none)"):alias),
                   qPrintable(at.bone), qPrintable(csm.smdPath));
            seenAlias.insert(alias.toLower());
        }
    }
    printf("\n--- distinct alias tokens across all classes ---\n");
    QStringList all(seenAlias.begin(), seenAlias.end()); all.sort();
    for (const QString& s : all) printf("  '%s'\n", qPrintable(s));
    return 0;
}
