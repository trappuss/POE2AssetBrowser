#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>
#include <cstdint>

class AssetStore;

// Resolves the true in-game display name of a browsable model (.smd) from the game's own data
// tables — never from the filename. The whole chain is authored data, measured and validated against
// the real files (tools/dat_probe.cpp, tools/dat_link.cpp, tools/ao_chain.cpp):
//
//   BaseItemTypes.Name  ──(foreign key col@124)──▶  ItemVisualIdentity
//   ItemVisualIdentity.AOFile (col@16)  ──▶  .ao  ──(skin)──▶  .sm  ──▶  .smd   (the browsed mesh)
//
// The foreign-key column (124) was found by SELF-VALIDATION — the one column whose value, across all
// 5,496 base items, links to an ItemVisualIdentity whose .ao is in the SAME item category — not by
// trusting an external schema. The Name (col@32) and AOFile (col@16) offsets were read straight off
// the heap. Spot-checks are exact: DullHatchet→"Dull Hatchet", RustedCuirass→"Rusted Cuirass".
//
// One mesh can be shared by several base types plus their rune variants (Runeforged/Runemastered …),
// so each model keeps a canonical primary name (the plain base) plus every name for search.
// Cached to data/cache/name_index_v<N>.bin, keyed on the bundle index fingerprint, and built on a
// background thread. v1 covers ITEMS (weapons/armour/…); monster/NPC names live in other tables.
class NameIndex {
public:
    // Bump whenever build() changes WHAT it produces, so a stale cache from an older build is
    // discarded and re-swept. v1 = items; v2 = + monsters + item icons; v3 = + NPCs.
    static constexpr uint32_t kCacheVersion = 3;

    // Sweep the (open) store: parse the two data tables, join, resolve each item's .ao→.sm→.smd, and
    // record the model→name mapping. Returns false if the data tables are absent/unreadable (names
    // simply unavailable — never a fabricated fallback). Per-item resolution failures are skipped.
    bool build(AssetStore& store, const std::function<void(const QString&)>& progress = {});
    bool isBuilt() const { return m_built; }

    bool load(const QString& cacheDir, const QString& fingerprint, QString* why = nullptr);
    bool save(const QString& cacheDir, const QString& fingerprint, QString* why = nullptr) const;

    // The canonical in-game name for a model path (lowercase), or "" if unknown.
    QString nameFor(const QString& modelPath) const;
    // All names for the model (base + variants), lowercased and space-joined, for the search haystack.
    QString searchNamesFor(const QString& modelPath) const;

    int namedModels() const { return m_primary.size(); }

    static QString selfTest();

private:
    // Plain base name for a shared mesh: strips variant prefixes and picks the base the model
    // filename carries (see .cpp), else the shortest plain base.
    static QString canonicalOf(const QStringList& names, const QString& modelPath);

    QHash<quint64, QString> m_primary;   // model path-hash → canonical name
    QHash<quint64, QString> m_search;    // model path-hash → all names (lowercase, space-joined)
    bool m_built = false;
};
