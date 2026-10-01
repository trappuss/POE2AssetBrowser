#pragma once
#include "bundle/BundleIndex.h"
#include "util/QueryTerm.h"
#include <QAbstractTableModel>
#include <QCache>
#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <functional>

class MaterialFamilyIndex;
class NameIndex;
namespace Facets { struct Facet; }

// A flat, dense list over the whole index — the centerpiece and the template for every list-driven
// tab (template §4). Filters through the one shared QueryTerm matcher, so the Models list, the
// Textures list and bulk extraction can never disagree on what a query means.
//
// The model holds a filtered vector of file-record indices; QTableView virtualises the rows, so
// 1.4M assets cost nothing to display. Filtering rebuilds the vector (debounced by the tab).
class AssetListModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { ColName = 0, ColTrueName, ColExt, ColSize, ColBundle, ColCount };

    explicit AssetListModel(QObject* parent = nullptr);

    // Point the model at an open index. `extFilter` (lowercase, with dots) restricts the base set —
    // e.g. {".smd",".fmt"} for the Models tab, {".dds"} for Textures. Empty = everything named.
    void setIndex(const BundleIndex* index, const QStringList& extFilter = {});

    // Supply the shader-family classification (built in the background, arrives after the index).
    // When set, it provides the `#family:…/#workflow:…/#effect:…` search metadata per model and
    // backs the Shader funnel facets. Triggers a rebuild so the current filter re-evaluates.
    void setMaterialIndex(const MaterialFamilyIndex* mfx);

    // Supply the true-name index (built in the background). When set, the "In-game name" column is
    // populated and item names become searchable. Triggers a rebuild.
    void setNameIndex(const NameIndex* nx);

    // Apply a query string (space=AND, -=exclude, #=meta, digits=id) and the funnel facets.
    void setQuery(const QString& query);
    void setExtFacet(const QString& ext);   // single-extension facet ("" = all in the base set)

    // Funnel facets (template §9): a set of active facet ids (see index/Facets.h). Default matching
    // is AND across groups, OR within a group; matchAny ORs the whole ticked set instead.
    void setFacets(const QStringList& activeFacetIds);
    void setFacetMatchAny(bool on);
    const QStringList& facets() const { return m_facetIds; }
    bool facetMatchAny() const { return m_facetMatchAny; }

    // Set query + ext facet + funnel facets together and rebuild ONCE (avoids three passes over the
    // base on every keystroke). The tabs call this from their debounced filter slot.
    void applyFilters(const QString& query, const QString& ext, const QStringList& facetIds, bool matchAny);

    // Per-facet match counts over the current search (query + ext facet) BEFORE the facet selection
    // is applied, so the funnel shows how many of the searched assets fall in each facet. Keyed by
    // facet id. One pass, computed on demand (funnel open / filter change), not per keystroke.
    QHash<QString, int> facetCounts() const;

    int rowCount(const QModelIndex& = {}) const override { return m_rows.size(); }
    int columnCount(const QModelIndex& = {}) const override { return ColCount; }
    QVariant data(const QModelIndex&, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;
    // Click-to-sort by any column; the choice is remembered and re-applied after a filter rebuild.
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    uint32_t fileIndexAt(int row) const { return (row >= 0 && row < m_rows.size()) ? m_rows[row] : UINT32_MAX; }
    int rowOfFile(uint32_t fileIndex) const;   // display row for a file index, or -1
    QString pathAt(int row) const;
    int totalInBaseSet() const { return m_base.size(); }

    // ── Thumbnail icons (grid view) — the caching mechanism is ported from D4AssetBrowser's
    //    SnoListModel: a bounded QCache keyed by file index so a repaint (which a mouse-over is) never
    //    re-runs the provider or re-scales, and a miss is cached as a null icon so un-rendered rows
    //    don't hammer the provider every frame. The tab installs a provider that returns the rendered
    //    (or decoded) pixmap, or a null pixmap when it isn't ready yet. ─────────────────────────────
    void setIconProvider(std::function<QPixmap(uint32_t fileIndex)> fn);
    void setIconPx(int px);                    // >0 scales icons to this size (allows upscaling)
    int  iconPx() const { return m_iconPx; }
    void setGridMode(bool on);                 // also decorate the name column (IconMode QListView)
    void refreshIconForFile(uint32_t fileIndex);   // repaint just the row that owns this file
    void refreshAllIcons();                    // drop the cache + repaint (provider/size/settings changed)

private:
    void rebuild();
    QVariant iconData(uint32_t fileIndex) const;
    void sortRows();               // apply m_sortColumn/m_sortOrder to m_rows in place
    bool passesFacets(const QString& path) const;
    bool facetHit(const Facets::Facet& f, const QString& path, const QStringList& segs) const;
    // The `#`-token metadata haystack for a file: the shader classification for a model, else "".
    QString metaFor(const QString& path) const;

    const BundleIndex* m_index = nullptr;
    const MaterialFamilyIndex* m_matIndex = nullptr;
    const NameIndex* m_nameIndex = nullptr;
    QStringList m_extFilter;
    QVector<uint32_t> m_base;        // file indices matching the extension filter (the tab's universe)
    QVector<uint32_t> m_queryRows;   // after query + ext facet, BEFORE the funnel facets (for counts)
    QVector<uint32_t> m_rows;        // currently visible (after the funnel facets too)
    QString m_query;
    QString m_extFacet;
    QStringList m_facetIds;
    bool m_facetMatchAny = false;
    int m_sortColumn = -1;         // -1 = index order (no explicit sort)
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;

    // Thumbnail state (see setIconProvider). The cache is mutable because iconData() is const; every
    // mutation point drops the affected keys. Budget in KB of pixmap, holding thousands of icons.
    std::function<QPixmap(uint32_t)> m_iconProvider;
    mutable QCache<uint32_t, QIcon> m_iconCache{ 96 * 1024 };   // cost unit = KB of pixmap
    int  m_iconPx = 0;             // >0 → scale icons to this size (allows upscaling past source)
    bool m_gridMode = false;       // icon-grid layout (decoration also on the name column)
};
