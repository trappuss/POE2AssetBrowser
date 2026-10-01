// Container-only research: sweep every .mat in the index, parse it with the SAME AssetText::parseMat
// the app uses, and tally shader family / workflow / effect layers. Validates the facet's
// classification against real data (no fabricated categories) and reports the real distribution.
//
// usage: mat_survey <bundlesDir> [maxSamplesToPrintPerFamily]

#include "store/AssetStore.h"
#include "model/AssetText.h"

#include <QCoreApplication>
#include <QHash>
#include <QMap>
#include <cstdio>

static const char* wfName(AssetText::Workflow w)
{
    using W = AssetText::Workflow;
    switch (w) {
        case W::MetalRough:          return "MetalRough";
        case W::DielectricSpecGloss: return "DielectricSpecGloss";
        case W::SpecGlossSpecMask:   return "SpecGlossSpecMask";
        default:                     return "Unknown";
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: mat_survey <bundlesDir> [printPerFamily]\n"); return 2; }
    const QString bundlesDir = QString::fromLocal8Bit(argv[1]);
    const int printPer = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 0;

    AssetStore store; QString err;
    fprintf(stderr, "opening index...\n");
    if (!store.open(bundlesDir, &err)) { fprintf(stderr, "open failed: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();

    const int matExt = idx.extensions().indexOf(QStringLiteral(".mat"));
    if (matExt < 0) { fprintf(stderr, "no .mat extension in this index\n"); return 1; }

    QMap<QString, int> familyCount;                 // family stem -> count
    QMap<QString, int> workflowCount;               // workflow name -> count
    QMap<QString, QString> familyToWorkflow;        // family -> workflow (to check 1:1 mapping)
    QHash<QString, bool> familyWorkflowConflict;    // family that maps to >1 workflow
    QMap<QString, QVector<QString>> familySamples;  // family -> a few example paths
    int total = 0, parsed = 0, noFamily = 0, readFail = 0;
    int sss = 0, transl = 0, fur = 0, alphaTest = 0, usesRough = 0;
    int emptyGraphs = 0;
    // Breakdown of the (none)-family bucket, to be sure it isn't hiding surface materials.
    int noneWithTextures = 0, noneWithAlbedo = 0, noneWithNormal = 0, noneEffectsOnly = 0;
    QMap<QString,int> noneFirstGraphCat;   // for (none): category segment after "Metadata/" of first graph

    for (uint32_t i = 0; i < idx.files().size(); ++i) {
        if (idx.files()[i].extId != uint16_t(matExt)) continue;
        ++total;
        const QString path = idx.pathOf(i);
        const QByteArray data = store.readFile(i, nullptr);
        if (data.isEmpty()) { ++readFail; continue; }
        const AssetText::Material m = AssetText::parseMat(data);
        ++parsed;
        if (m.graphs.isEmpty()) ++emptyGraphs;

        const QString fam = m.family.isEmpty() ? QStringLiteral("(none)") : m.family;
        if (m.family.isEmpty()) ++noFamily;
        familyCount[fam]++;
        const QString wf = QString::fromLatin1(wfName(m.workflow));
        workflowCount[wf]++;

        if (familyToWorkflow.contains(fam) && familyToWorkflow[fam] != wf) familyWorkflowConflict[fam] = true;
        else familyToWorkflow[fam] = wf;

        if (printPer && familySamples[fam].size() < printPer) familySamples[fam].push_back(path);

        if (m.hasSSS) ++sss;
        if (m.hasTranslucency) ++transl;
        if (m.hasFur) ++fur;
        if (m.alphaTest) ++alphaTest;
        if (m.usesRoughness) ++usesRough;

        if (m.family.isEmpty()) {
            if (!m.textures.isEmpty()) ++noneWithTextures;
            if (!m.albedo.isEmpty())   ++noneWithAlbedo;
            if (!m.normalGlossAO.isEmpty()) ++noneWithNormal;
            bool effectsOnly = !m.graphs.isEmpty();
            for (const QString& g : m.graphs) if (!g.contains(QStringLiteral("/Effects/"), Qt::CaseInsensitive)) { effectsOnly = false; break; }
            if (effectsOnly) ++noneEffectsOnly;
            if (!m.graphs.isEmpty()) {
                // segment after "Metadata/" of the first graph, e.g. "Effects", "Materials", "Terrain"
                const QString g0 = m.graphs.first();
                const int mi = g0.indexOf(QStringLiteral("Metadata/"), 0, Qt::CaseInsensitive);
                QString cat = (mi >= 0) ? g0.mid(mi + 9).section(QLatin1Char('/'), 0, 0) : g0.section(QLatin1Char('/'), 0, 0);
                noneFirstGraphCat[cat]++;
                if (printPer && familySamples[QStringLiteral("(none):") + cat].size() < printPer)
                    familySamples[QStringLiteral("(none):") + cat].push_back(path + QStringLiteral("   g0=") + g0);
            }
        }
    }

    printf("=== .mat survey ===\n");
    printf("total .mat=%d  parsed=%d  readFail=%d  noFamily=%d  emptyGraphs=%d\n\n",
           total, parsed, readFail, noFamily, emptyGraphs);

    printf("--- by shader family (stem of first Metadata/Materials/*.fxgraph) ---\n");
    // Sort by count desc.
    QVector<QPair<int,QString>> byCount;
    for (auto it = familyCount.begin(); it != familyCount.end(); ++it) byCount.push_back({it.value(), it.key()});
    std::sort(byCount.begin(), byCount.end(), [](auto& a, auto& b){ return a.first > b.first; });
    for (auto& p : byCount) {
        const QString wf = familyToWorkflow.value(p.second);
        const char* conflict = familyWorkflowConflict.contains(p.second) ? "  <-- MAPS TO >1 WORKFLOW" : "";
        printf("  %-34s %7d   -> %s%s\n", qPrintable(p.second), p.first, qPrintable(wf), conflict);
    }

    printf("\n--- by workflow (what the exporter/viewport switch on) ---\n");
    for (auto it = workflowCount.begin(); it != workflowCount.end(); ++it)
        printf("  %-22s %7d\n", qPrintable(it.key()), it.value());

    printf("\n--- effect layers (counts, not exclusive) ---\n");
    printf("  SSS=%d  translucency=%d  fur=%d  alphaTest=%d  usesRoughness=%d\n",
           sss, transl, fur, alphaTest, usesRough);

    printf("\n--- the (none)-family bucket, broken down (is it hiding surface materials?) ---\n");
    printf("  total(none)=%d  withTextures=%d  withAlbedo=%d  withNormal=%d  effectsGraphsOnly=%d\n",
           noFamily, noneWithTextures, noneWithAlbedo, noneWithNormal, noneEffectsOnly);
    printf("  (none) by first-graph category under Metadata/:\n");
    QVector<QPair<int,QString>> nc;
    for (auto it = noneFirstGraphCat.begin(); it != noneFirstGraphCat.end(); ++it) nc.push_back({it.value(), it.key()});
    std::sort(nc.begin(), nc.end(), [](auto& a, auto& b){ return a.first > b.first; });
    for (auto& p : nc) printf("      %-24s %7d\n", qPrintable(p.second), p.first);

    if (printPer) {
        printf("\n--- sample paths per family ---\n");
        for (auto& p : byCount) {
            printf("  [%s]\n", qPrintable(p.second));
            for (const QString& s : familySamples[p.second]) printf("      %s\n", qPrintable(s));
        }
        printf("\n--- sample (none) paths per first-graph category ---\n");
        for (auto it = familySamples.begin(); it != familySamples.end(); ++it) {
            if (!it.key().startsWith(QStringLiteral("(none):"))) continue;
            printf("  [%s]\n", qPrintable(it.key()));
            for (const QString& s : it.value()) printf("      %s\n", qPrintable(s));
        }
    }
    return 0;
}
