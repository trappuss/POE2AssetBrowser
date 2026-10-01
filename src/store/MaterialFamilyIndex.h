#pragma once
#include "model/AssetText.h"
#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <cstdint>

class AssetStore;

// A cached classification of every asset by its authored shader family / PBR workflow / effect
// layers — the data that lives INSIDE the .mat files (docs/FORMATS.md §6), not in any path segment,
// so it needs its own sweep (the shader-family follow-up noted in index/Facets.h).
//
// Two passes over the index, both reading only small text descriptors:
//   1. every .mat  → AssetText::parseMat → {family stem, workflow, effect flags}, keyed by path hash
//   2. every .sm   → AssetText::parseSm  → its .smd + material list; the model inherits the union of
//      its materials' families / workflows / effects.
// (.sm-side, NOT AssetStore::findSmForSmd per model — that scans all 1.4M records each call, which is
//  fine for one model but O(models × files) for a sweep.)
//
// The result feeds the Models list two ways: a per-model metadata haystack for the shared `#`
// query tokens (#family:…, #workflow:…, #effect:…) and a workflow bitmask for the funnel chips.
// Cached to data/cache/material_index_v<N>.bin, keyed on the bundle index's fingerprint, exactly
// like BundleIndex's own cache — so it is swept once per game build and loaded instantly after.
//
// Validated against real data (tools/mat_survey.cpp, all 202,606 .mat): 50,088 carry a Materials/*
// family; the rest are genuine effect/VFX materials (no surface family), never surface materials
// mis-classified. So an empty family is a correct answer, not a gap.
class MaterialFamilyIndex {
public:
    static constexpr uint32_t kCacheVersion = 1;

    // Workflow bits for the funnel chips (a model may carry several).
    enum WorkflowBit : uint8_t {
        WfMetalRough  = 1 << 0,
        WfDielectric  = 1 << 1,   // DielectricSpecGloss
        WfSpecGloss   = 1 << 2,   // SpecGlossSpecMask
        WfOther       = 1 << 3,   // a surface material with no canonical workflow, OR an effect material
    };
    // Effect-layer bits.
    enum EffectBit : uint8_t {
        FxSSS          = 1 << 0,
        FxTranslucency = 1 << 1,
        FxFur          = 1 << 2,
        FxAlphaTest    = 1 << 3,
    };

    struct MatRec { quint16 familyId = 0; quint8 workflow = 0; quint8 effects = 0; };  // per .mat
    struct ModelRec { quint8 workflowMask = 0; quint8 effectMask = 0; QString meta; };  // per model

    // Sweep the store's (already-open) index. `progress` receives short status lines. Returns false
    // only if the index is not open; per-file read/parse failures are skipped and counted.
    bool build(AssetStore& store, const std::function<void(const QString&)>& progress = {});
    bool isBuilt() const { return m_built; }

    // Cache. `cacheDir` empty disables; `fingerprint` is BundleIndex::fingerprint().
    bool load(const QString& cacheDir, const QString& fingerprint, QString* why = nullptr);
    bool save(const QString& cacheDir, const QString& fingerprint, QString* why = nullptr) const;

    // The `#`-token metadata haystack for a model path (lowercase). Empty when the model is not
    // classified (no .sm found, or an unswept format) — the caller then falls back to the path.
    QString metaForModel(const QString& modelPath) const;
    // The same for a raw .mat path (its own family/workflow/effects).
    QString metaForMaterial(const QString& matPath) const;

    // Funnel-chip queries for a model path.
    quint8 modelWorkflowMask(const QString& modelPath) const;
    quint8 modelEffectMask(const QString& modelPath) const;

    int materialCount() const { return m_mat.size(); }
    int modelCount() const { return m_models.size(); }
    const QStringList& families() const { return m_families; }

    static QString selfTest();

private:
    quint16 internFamily(const QString& stem);
    static quint8 workflowBit(AssetText::Workflow w);
    static QString buildMeta(const QStringList& familyStems, quint8 wfMask, quint8 fxMask);

    QStringList m_families{QString()};             // familyId → stem; id 0 = "" (no family)
    QHash<QString, quint16> m_familyIds{{QString(), 0}};
    QHash<quint64, MatRec> m_mat;                  // path-hash → material record
    QHash<quint64, ModelRec> m_models;             // model path-hash → model record
    bool m_built = false;
};
