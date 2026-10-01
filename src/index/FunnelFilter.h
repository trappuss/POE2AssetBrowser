#pragma once
#include <QHash>
#include <QStringList>
#include <QToolButton>
#include <QVector>

class QWidget;
class QCheckBox;

// The funnel filter control (template §9): a funnel-icon button whose popup STAYS OPEN while you tick
// grouped facet checkboxes (index/Facets.h), plus a Match-any (OR) toggle. Active facets show as
// removable chips in a separate bar the host tab places in its header, and the funnel icon is tinted
// while any filter is active. One shared control both the Models and Bulk tabs use, so they filter
// the same way; each persists its own selection under its own settings key.
//
// It reports selection changes via changed(); the host applies them to its AssetListModel and feeds
// live per-facet counts back with setCounts() (the count you see is the set you get).
class FunnelFilter : public QToolButton {
    Q_OBJECT
public:
    // `groups` restricts which facet groups appear (empty = all of Facets::groups()). The Textures
    // and Bulk tabs drop the "Shader" group, which only classifies models.
    explicit FunnelFilter(QWidget* parent = nullptr, const QStringList& groups = {});

    QStringList activeIds() const;
    bool matchAny() const;
    void setActive(const QStringList& ids, bool matchAny);   // restore persisted state (no signal)

    // Update the "(n)" count beside each facet, and the chip bar. Keyed by facet id.
    void setCounts(const QHash<QString, int>& counts);

    // The chip bar widget (owned here) — add it to the host's header layout. It shows one removable
    // chip per active facet and is hidden when nothing is active.
    QWidget* chipBar() const { return m_chipBar; }

signals:
    void changed();

private:
    void rebuildChips();
    void refreshTint();

    QWidget* m_panel = nullptr;
    QWidget* m_chipBar = nullptr;
    QCheckBox* m_matchAny = nullptr;
    QHash<QString, QCheckBox*> m_boxes;   // facet id → its checkbox
    QHash<QString, int> m_counts;
};
