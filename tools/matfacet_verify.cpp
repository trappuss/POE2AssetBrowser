// Container-only: exercise the real AssetListModel with the MaterialFamilyIndex wired in, exactly as
// the Models tab does — proving the #workflow/#family/#effect search tokens and the Shader funnel
// facets filter correctly against real data.
//
// usage: matfacet_verify <bundlesDir>

#include "store/AssetStore.h"
#include "store/MaterialFamilyIndex.h"
#include "index/AssetListModel.h"
#include "index/Facets.h"
#include "bundle/BundleIndex.h"

#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: matfacet_verify <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    if (!store.buildMaterialIndex([](const QString& s){ fprintf(stderr, "  %s\n", qPrintable(s)); })) { fprintf(stderr, "build failed\n"); return 1; }
    const MaterialFamilyIndex& mfx = store.materialIndex();

    AssetListModel model;
    model.setIndex(&store.index(), {QStringLiteral(".smd"), QStringLiteral(".fmt")});
    model.setMaterialIndex(&mfx);
    const int totalModels = model.totalInBaseSet();

    // Independent expected counts straight from the material index over the model base set.
    auto independentTokenCount = [&](const char* token) {
        int n = 0;
        for (int r = 0; r < model.totalInBaseSet(); ++r) {}   // (no direct base iterator; use the index)
        // Walk the base set via the index instead.
        const BundleIndex& idx = store.index();
        const int smd = idx.extensions().indexOf(QStringLiteral(".smd"));
        const int fmt = idx.extensions().indexOf(QStringLiteral(".fmt"));
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            const auto& f = idx.files()[i];
            if (f.nameLen == 0) continue;
            if (f.extId != uint16_t(smd) && f.extId != uint16_t(fmt)) continue;
            if (mfx.metaForModel(idx.pathOf(i)).contains(QString::fromLatin1(token))) ++n;
        }
        return n;
    };
    auto independentBitCount = [&](bool effect, quint8 bit) {
        const BundleIndex& idx = store.index();
        const int smd = idx.extensions().indexOf(QStringLiteral(".smd"));
        const int fmt = idx.extensions().indexOf(QStringLiteral(".fmt"));
        int n = 0;
        for (uint32_t i = 0; i < idx.files().size(); ++i) {
            const auto& f = idx.files()[i];
            if (f.nameLen == 0) continue;
            if (f.extId != uint16_t(smd) && f.extId != uint16_t(fmt)) continue;
            const QString p = idx.pathOf(i);
            const quint8 mask = effect ? mfx.modelEffectMask(p) : mfx.modelWorkflowMask(p);
            if (mask & bit) ++n;
        }
        return n;
    };

    struct Check { const char* label; QString query; QStringList facet; int got; int expect; };
    QVector<Check> checks;

    auto runQuery = [&](const char* label, const QString& q, const char* token) {
        model.applyFilters(q, QString(), {}, false);
        checks.push_back({label, q, {}, model.rowCount(), independentTokenCount(token)});
    };
    auto runFacet = [&](const char* label, const QString& id, bool effect, quint8 bit) {
        model.applyFilters(QString(), QString(), {id}, false);
        checks.push_back({label, QStringLiteral("[facet ") + id + QLatin1Char(']'), {id}, model.rowCount(), independentBitCount(effect, bit)});
    };

    runQuery("token #workflow:metalrough",          QStringLiteral("#workflow:metalrough"),          "workflow:metalrough");
    runQuery("token #workflow:dielectricspecgloss", QStringLiteral("#workflow:dielectricspecgloss"), "workflow:dielectricspecgloss");
    runQuery("token #workflow:specgloss",           QStringLiteral("#workflow:specgloss"),           "workflow:specgloss");
    runQuery("token #effect:sss",                   QStringLiteral("#effect:sss"),                   "effect:sss");
    runQuery("token #effect:alphatest",             QStringLiteral("#effect:alphatest"),             "effect:alphatest");
    runQuery("token #family:hair",                  QStringLiteral("#family:hair"),                  "family:hair");
    runFacet("facet wf.metalrough",   QStringLiteral("wf.metalrough"),   false, MaterialFamilyIndex::WfMetalRough);
    runFacet("facet wf.dielectric",   QStringLiteral("wf.dielectric"),   false, MaterialFamilyIndex::WfDielectric);
    runFacet("facet wf.specgloss",    QStringLiteral("wf.specgloss"),    false, MaterialFamilyIndex::WfSpecGloss);
    runFacet("facet wf.other",        QStringLiteral("wf.other"),        false, MaterialFamilyIndex::WfOther);
    runFacet("facet fx.sss",          QStringLiteral("fx.sss"),          true,  MaterialFamilyIndex::FxSSS);
    runFacet("facet fx.alphatest",    QStringLiteral("fx.alphatest"),    true,  MaterialFamilyIndex::FxAlphaTest);

    // facetCounts must agree with the funnel-facet row counts (the count you see is the set you get).
    model.applyFilters(QString(), QString(), {}, false);
    const QHash<QString,int> fc = model.facetCounts();

    printf("=== matfacet_verify (base models=%d) ===\n", totalModels);
    int fails = 0;
    for (const Check& c : checks) {
        const bool ok = (c.got == c.expect);
        if (!ok) ++fails;
        printf("  %-34s got=%-7d expect=%-7d %s\n", c.label, c.got, c.expect, ok ? "ok" : "FAIL");
    }
    // Cross-check a couple of facetCounts entries against the row-count checks.
    auto countCheck = [&](const char* id, int expect) {
        const int c = fc.value(QString::fromLatin1(id), -1);
        const bool ok = (c == expect);
        if (!ok) ++fails;
        printf("  facetCounts[%-14s] = %-7d expect=%-7d %s\n", id, c, expect, ok ? "ok" : "FAIL");
    };
    countCheck("wf.metalrough", independentBitCount(false, MaterialFamilyIndex::WfMetalRough));
    countCheck("fx.sss",        independentBitCount(true,  MaterialFamilyIndex::FxSSS));

    printf("\nRESULT: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails == 0 ? 0 : 1;
}
