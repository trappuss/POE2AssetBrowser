#include "index/AssetListModel.h"
#include "util/QueryTerm.h"
#include "index/Facets.h"
#include "store/MaterialFamilyIndex.h"
#include "store/NameIndex.h"
#include <QLocale>
#include <algorithm>

AssetListModel::AssetListModel(QObject* parent) : QAbstractTableModel(parent) {}

void AssetListModel::setIndex(const BundleIndex* index, const QStringList& extFilter)
{
    beginResetModel();
    m_index = index;
    m_extFilter = extFilter;
    m_base.clear();
    m_rows.clear();
    if (m_index) {
        // Build the base set: named files whose extension is in the filter (or all named files).
        QVector<uint16_t> wantExt;
        for (const QString& e : extFilter) {
            const int id = m_index->extensions().indexOf(e);
            if (id >= 0) wantExt.append(uint16_t(id));
        }
        m_base.reserve(m_index->files().size() / 4 + 16);
        for (uint32_t i = 0; i < m_index->files().size(); ++i) {
            const auto& f = m_index->files()[i];
            if (f.nameLen == 0) continue;   // unnamed files are not listed here (counted in Health)
            if (!extFilter.isEmpty() && !wantExt.contains(f.extId)) continue;
            m_base.append(i);
        }
    }
    endResetModel();
    rebuild();
}

void AssetListModel::setMaterialIndex(const MaterialFamilyIndex* mfx) { m_matIndex = mfx; rebuild(); }
void AssetListModel::setNameIndex(const NameIndex* nx) { m_nameIndex = nx; rebuild(); }

QString AssetListModel::metaFor(const QString& path) const
{
    QString m = m_matIndex ? m_matIndex->metaForModel(path) : QString();
    if (m_nameIndex) {
        const QString names = m_nameIndex->searchNamesFor(path);   // true in-game names, for search
        if (!names.isEmpty()) { if (!m.isEmpty()) m += QLatin1Char(' '); m += names; }
    }
    return m;
}

void AssetListModel::setQuery(const QString& q) { m_query = q; rebuild(); }
void AssetListModel::setExtFacet(const QString& ext) { m_extFacet = ext; rebuild(); }
void AssetListModel::setFacets(const QStringList& ids) { m_facetIds = ids; rebuild(); }
void AssetListModel::setFacetMatchAny(bool on) { m_facetMatchAny = on; rebuild(); }
void AssetListModel::applyFilters(const QString& query, const QString& ext, const QStringList& facetIds, bool matchAny)
{
    m_query = query; m_extFacet = ext; m_facetIds = facetIds; m_facetMatchAny = matchAny;
    rebuild();
}

// Grouped AND-of-ORs (default), or OR of the whole ticked set (matchAny). An empty selection passes
// everything. Unknown ids resolve to a match-all empty facet and are harmless.
// A facet hit: path-structure facets test the path segments; Shader facets test the material
// classification (MaterialFamilyIndex). While the classification is still building, a Shader facet
// is a no-op (matches everything) so it never hides assets it cannot yet classify.
bool AssetListModel::facetHit(const Facets::Facet& f, const QString& path, const QStringList& segs) const
{
    if (!f.shader) return Facets::matches(f, segs);
    if (!m_matIndex || !m_matIndex->isBuilt()) return true;
    const quint8 mask = f.effect ? m_matIndex->modelEffectMask(path) : m_matIndex->modelWorkflowMask(path);
    return (mask & f.bit) != 0;
}

bool AssetListModel::passesFacets(const QString& path) const
{
    if (m_facetIds.isEmpty()) return true;
    const QStringList segs = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (m_facetMatchAny) {
        for (const QString& id : m_facetIds) {
            const Facets::Facet f = Facets::byId(id);
            if (!f.id.isEmpty() && facetHit(f, path, segs)) return true;
        }
        return false;
    }
    // AND across groups, OR within a group.
    QHash<QString, bool> groupHas, groupHit;
    for (const QString& id : m_facetIds) {
        const Facets::Facet f = Facets::byId(id);
        if (f.id.isEmpty()) continue;
        groupHas[f.group] = true;
        if (!groupHit.value(f.group, false) && facetHit(f, path, segs)) groupHit[f.group] = true;
    }
    for (auto it = groupHas.constBegin(); it != groupHas.constEnd(); ++it)
        if (!groupHit.value(it.key(), false)) return false;
    return true;
}

void AssetListModel::rebuild()
{
    beginResetModel();
    m_queryRows.clear();
    m_rows.clear();
    if (m_index) {
        const QueryTerm::Query q = QueryTerm::parse(m_query);
        // Only build the haystacks a given query actually consults (test() reads idText solely for
        // idTerms, and meta solely for and/not/metaTerms). A pure text search then skips the id
        // formatting; a pure id search skips metaFor()'s per-row index lookups entirely.
        const bool needId   = !q.idTerms.isEmpty();
        const bool needMeta = !q.andTerms.isEmpty() || !q.notTerms.isEmpty() || !q.metaTerms.isEmpty();
        uint16_t facetId = UINT16_MAX;
        if (!m_extFacet.isEmpty()) { const int id = m_index->extensions().indexOf(m_extFacet); facetId = id >= 0 ? uint16_t(id) : UINT16_MAX; }
        m_queryRows.reserve(m_base.size());
        for (uint32_t fi : m_base) {
            const auto& f = m_index->files()[fi];
            if (facetId != UINT16_MAX && f.extId != facetId) continue;
            if (!q.isEmpty()) {
                const QString path = m_index->pathOf(fi);
                // The id is the MurmurHash64 of the path (no SNO in PoE2). Offer BOTH forms so
                // "find by id" works from a decimal token or a 0x-hex token (as GGPK/bundle tools
                // print it): decimal, then the zero-padded 16-digit hex.
                const QString idText = needId ? QStringLiteral("%1 %2").arg(f.hash).arg(f.hash, 16, 16, QLatin1Char('0'))
                                              : QString();
                // Metadata haystack for `#` terms: the path (so `#armour` still works) plus the
                // shader classification (`#family:…`, `#workflow:…`, `#effect:…`) when available.
                QString meta;
                if (needMeta) {
                    meta = path;
                    const QString sm = metaFor(path);
                    if (!sm.isEmpty()) { meta.reserve(path.size() + sm.size() + 1); meta += QLatin1Char(' '); meta += sm; }
                }
                if (!QueryTerm::test(q, path, meta, idText)) continue;
            }
            m_queryRows.append(fi);
        }
        // Apply the funnel facets on top (the count you see is the set you get, template §9/§14).
        if (m_facetIds.isEmpty()) {
            m_rows = m_queryRows;
        } else {
            m_rows.reserve(m_queryRows.size());
            for (uint32_t fi : m_queryRows)
                if (passesFacets(m_index->pathOf(fi))) m_rows.append(fi);
        }
        sortRows();   // re-apply the user's chosen sort so filtering doesn't scramble it
    }
    endResetModel();
}

void AssetListModel::sort(int column, Qt::SortOrder order)
{
    if (column < 0 || column >= ColCount) return;
    m_sortColumn = column; m_sortOrder = order;
    beginResetModel();
    sortRows();
    endResetModel();
}

void AssetListModel::sortRows()
{
    if (!m_index || m_sortColumn < 0 || m_rows.isEmpty()) return;
    const bool asc = (m_sortOrder == Qt::AscendingOrder);
    if (m_sortColumn == ColSize) {
        std::stable_sort(m_rows.begin(), m_rows.end(), [&](uint32_t a, uint32_t b) {
            const quint64 sa = m_index->files()[a].size, sb = m_index->files()[b].size;
            return asc ? sa < sb : sa > sb;
        });
        return;
    }
    // String columns: decorate with the sort key once (pathOf/dir walks are not free), then sort.
    QVector<QPair<QString, uint32_t>> keyed;
    keyed.reserve(m_rows.size());
    for (uint32_t fi : m_rows) {
        QString key;
        switch (m_sortColumn) {
            case ColName:     key = m_index->pathOf(fi); break;
            case ColTrueName: key = m_nameIndex ? m_nameIndex->nameFor(m_index->pathOf(fi)) : QString(); break;
            case ColExt:      { const auto& f = m_index->files()[fi]; key = f.extId < m_index->extensions().size() ? m_index->extensions()[f.extId] : QString(); break; }
            case ColBundle:   { const auto& f = m_index->files()[fi]; key = f.bundle < m_index->bundles().size() ? m_index->bundles()[f.bundle].name : QString(); break; }
            default: break;
        }
        keyed.append({key, fi});
    }
    std::stable_sort(keyed.begin(), keyed.end(), [&](const QPair<QString,uint32_t>& a, const QPair<QString,uint32_t>& b) {
        const int c = QString::compare(a.first, b.first, Qt::CaseInsensitive);
        return asc ? c < 0 : c > 0;
    });
    for (int i = 0; i < keyed.size(); ++i) m_rows[i] = keyed[i].second;
}

QHash<QString, int> AssetListModel::facetCounts() const
{
    QHash<QString, int> counts;
    if (!m_index) return counts;
    const QVector<Facets::Facet> facets = Facets::all();
    for (uint32_t fi : m_queryRows) {
        const QString path = m_index->pathOf(fi);
        const QStringList segs = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const Facets::Facet& f : facets)
            if (facetHit(f, path, segs)) ++counts[f.id];
    }
    return counts;
}

QString AssetListModel::pathAt(int row) const
{
    if (!m_index || row < 0 || row >= m_rows.size()) return QString();
    return m_index->pathOf(m_rows[row]);
}

QVariant AssetListModel::data(const QModelIndex& idx, int role) const
{
    if (!m_index || !idx.isValid() || idx.row() >= m_rows.size()) return {};
    const uint32_t fi = m_rows[idx.row()];
    const auto& f = m_index->files()[fi];
    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
            // In grid mode the name column becomes the tile CAPTION, so it shows the short file name
            // (the full path would be an unreadable wall under a thumbnail); the table shows the path.
            case ColName:     { const QString p = m_index->pathOf(fi);
                                return m_gridMode ? p.section(QLatin1Char('/'), -1) : p; }
            case ColTrueName: return m_nameIndex ? m_nameIndex->nameFor(m_index->pathOf(fi)) : QString();
            case ColExt:      return f.extId < m_index->extensions().size() ? m_index->extensions()[f.extId] : QString();
            case ColSize:     return QLocale().formattedDataSize(f.size);
            case ColBundle:   return f.bundle < m_index->bundles().size() ? m_index->bundles()[f.bundle].name : QString();
        }
    } else if (role == Qt::DecorationRole && idx.column() == ColName && m_iconProvider) {
        return iconData(fi);
    } else if (role == Qt::TextAlignmentRole && m_gridMode && idx.column() == ColName) {
        return int(Qt::AlignHCenter | Qt::AlignTop);   // centered caption under the grid icon
    } else if (role == Qt::ToolTipRole) {
        return m_index->pathOf(fi);
    } else if (role == Qt::UserRole) {
        return fi;
    }
    return {};
}

// Icon resolver (ported from D4's SnoListModel::iconData): a bounded, memoized QCache keyed by file
// index. Called by the view for EVERY visible cell on EVERY repaint (a mouse-over is a repaint), so
// returning a fresh QIcon per paint would re-scale and thrash the global QPixmapCache; memoizing
// stops that. A miss is cached as a NULL icon so a not-yet-rendered row doesn't re-run the provider
// each frame; refreshIconForFile drops that entry when the pixmap lands.
QVariant AssetListModel::iconData(uint32_t fileIndex) const
{
    if (const QIcon* hit = m_iconCache.object(fileIndex))
        return hit->isNull() ? QVariant() : QVariant(*hit);
    QPixmap pm = m_iconProvider ? m_iconProvider(fileIndex) : QPixmap();
    if (pm.isNull()) { m_iconCache.insert(fileIndex, new QIcon(), 1); return QVariant(); }
    // Scale to the requested icon size so the view can draw icons LARGER than the source (a plain
    // QIcon caps at the source size → Ctrl+scroll could only ever shrink). 0 = unscaled.
    if (m_iconPx > 0 && (pm.width() != m_iconPx || pm.height() != m_iconPx))
        pm = pm.scaled(m_iconPx, m_iconPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QIcon ic(pm);
    const int costKB = qMax(1, int((qint64(pm.width()) * pm.height() * 4) / 1024));
    m_iconCache.insert(fileIndex, new QIcon(ic), costKB);
    return QVariant(ic);
}

int AssetListModel::rowOfFile(uint32_t fileIndex) const
{
    for (int r = 0; r < m_rows.size(); ++r) if (m_rows[r] == fileIndex) return r;
    return -1;
}

void AssetListModel::setIconProvider(std::function<QPixmap(uint32_t)> fn)
{
    m_iconProvider = std::move(fn);
    m_iconCache.clear();
    refreshAllIcons();
}

void AssetListModel::setIconPx(int px)
{
    if (px == m_iconPx) return;
    m_iconPx = px;
    m_iconCache.clear();          // every cached icon is scaled to the OLD size
    refreshAllIcons();
}

void AssetListModel::setGridMode(bool on)
{
    if (on == m_gridMode) return;
    m_gridMode = on;
    if (!m_rows.isEmpty())        // caption text + decoration + alignment of the name column changed
        emit dataChanged(index(0, ColName), index(m_rows.size() - 1, ColName),
                         {Qt::DisplayRole, Qt::DecorationRole, Qt::TextAlignmentRole});
}

void AssetListModel::refreshIconForFile(uint32_t fileIndex)
{
    m_iconCache.remove(fileIndex);   // this file's pixmap just changed; everything else stays cached
    const int r = rowOfFile(fileIndex);
    if (r >= 0) emit dataChanged(index(r, ColName), index(r, ColName), {Qt::DecorationRole});
}

void AssetListModel::refreshAllIcons()
{
    m_iconCache.clear();
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, ColName), index(m_rows.size() - 1, ColName), {Qt::DecorationRole});
}

QVariant AssetListModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole || o != Qt::Horizontal) return {};
    switch (section) {
        case ColName:     return QStringLiteral("Path");
        case ColTrueName: return QStringLiteral("In-game name");
        case ColExt:      return QStringLiteral("Type");
        case ColSize:     return QStringLiteral("Size");
        case ColBundle:   return QStringLiteral("Bundle");
    }
    return {};
}
