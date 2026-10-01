#include "index/FunnelFilter.h"
#include "index/Facets.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

FunnelFilter::FunnelFilter(QWidget* parent, const QStringList& groups) : QToolButton(parent)
{
    const QStringList showGroups = groups.isEmpty() ? Facets::groups() : groups;
    setText(QStringLiteral("▽ Filters"));
    setToolTip(QStringLiteral("Filter by category and type — the popup stays open while you tick."));
    setPopupMode(QToolButton::InstantPopup);

    // The popup: one QWidgetAction holding a panel of grouped checkboxes. A QWidgetAction's widget
    // takes clicks WITHOUT closing the menu — the "menu that stays open" trick (template §9).
    auto* menu = new QMenu(this);
    m_panel = new QWidget(menu);
    auto* pv = new QVBoxLayout(m_panel);
    pv->setContentsMargins(10, 8, 10, 8);

    m_matchAny = new QCheckBox(QStringLiteral("Match any (OR) instead of all"), m_panel);
    pv->addWidget(m_matchAny);
    connect(m_matchAny, &QCheckBox::toggled, this, [this] { emit changed(); });

    const QVector<Facets::Facet> facets = Facets::all();
    for (const QString& group : showGroups) {
        auto* line = new QFrame(m_panel); line->setFrameShape(QFrame::HLine); line->setFrameShadow(QFrame::Sunken);
        pv->addWidget(line);
        auto* hdr = new QLabel(QStringLiteral("<b>%1</b>").arg(group), m_panel);
        pv->addWidget(hdr);
        for (const Facets::Facet& f : facets) {
            if (f.group != group) continue;
            auto* cb = new QCheckBox(f.label, m_panel);
            cb->setProperty("facetId", f.id);
            pv->addWidget(cb);
            m_boxes.insert(f.id, cb);
            connect(cb, &QCheckBox::toggled, this, [this] { rebuildChips(); refreshTint(); emit changed(); });
        }
    }
    pv->addStretch(1);

    // Scroll so a long taxonomy never clips (template §16 makeTab discipline).
    auto* scroll = new QScrollArea(menu);
    scroll->setWidget(m_panel); scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(240); scroll->setMinimumHeight(360);
    auto* wa = new QWidgetAction(menu); wa->setDefaultWidget(scroll);
    menu->addAction(wa);
    setMenu(menu);

    // The chip bar (host places it in the header). Hidden until something is active.
    m_chipBar = new QWidget(parent);
    auto* cbl = new QHBoxLayout(m_chipBar); cbl->setContentsMargins(0, 0, 0, 0); cbl->setSpacing(4);
    m_chipBar->setVisible(false);

    refreshTint();
}

QStringList FunnelFilter::activeIds() const
{
    QStringList ids;
    for (auto it = m_boxes.constBegin(); it != m_boxes.constEnd(); ++it)
        if (it.value()->isChecked()) ids << it.key();
    return ids;
}

bool FunnelFilter::matchAny() const { return m_matchAny->isChecked(); }

void FunnelFilter::setActive(const QStringList& ids, bool matchAny)
{
    for (auto it = m_boxes.constBegin(); it != m_boxes.constEnd(); ++it) {
        QSignalBlocker b(it.value());
        it.value()->setChecked(ids.contains(it.key()));
    }
    { QSignalBlocker b(m_matchAny); m_matchAny->setChecked(matchAny); }
    rebuildChips();
    refreshTint();
}

void FunnelFilter::setCounts(const QHash<QString, int>& counts)
{
    m_counts = counts;
    for (const Facets::Facet& f : Facets::all()) {
        auto* cb = m_boxes.value(f.id);
        if (cb) cb->setText(QStringLiteral("%1 (%2)").arg(f.label).arg(counts.value(f.id, 0)));
    }
    rebuildChips();
}

void FunnelFilter::rebuildChips()
{
    auto* lay = qobject_cast<QHBoxLayout*>(m_chipBar->layout());
    if (!lay) return;
    // Clear existing chips.
    QLayoutItem* it;
    while ((it = lay->takeAt(0))) { if (it->widget()) it->widget()->deleteLater(); delete it; }

    const QStringList ids = activeIds();
    if (ids.isEmpty()) { m_chipBar->setVisible(false); return; }
    m_chipBar->setVisible(true);
    lay->addWidget(new QLabel(QStringLiteral("Active:"), m_chipBar));
    for (const QString& id : ids) {
        const Facets::Facet f = Facets::byId(id);
        if (f.id.isEmpty()) continue;
        auto* chip = new QToolButton(m_chipBar);
        const int n = m_counts.value(id, -1);
        chip->setText(n >= 0 ? QStringLiteral("%1: %2 (%3)  ✕").arg(f.group, f.label).arg(n)
                             : QStringLiteral("%1: %2  ✕").arg(f.group, f.label));
        chip->setToolTip(QStringLiteral("Remove this filter"));
        chip->setAutoRaise(true);
        // Squared chips (3px radius) to match the tool's overall aesthetic, not pill-shaped tags.
        chip->setStyleSheet(QStringLiteral(
            "QToolButton{border:1px solid palette(mid);border-radius:3px;padding:2px 8px;background:palette(button);}"
            "QToolButton:hover{border-color:palette(highlight);}"));
        connect(chip, &QToolButton::clicked, this, [this, id] {
            if (auto* cb = m_boxes.value(id)) cb->setChecked(false);   // triggers changed() + rebuild
        });
        lay->addWidget(chip);
    }
    lay->addStretch(1);
}

void FunnelFilter::refreshTint()
{
    const bool active = !activeIds().isEmpty();
    setText(active ? QStringLiteral("▼ Filters •") : QStringLiteral("▽ Filters"));
    // A bordered, padded button so it reads like the other toolbar buttons (not a flat, squished
    // tool-button); the active state just adds weight + the highlight colour.
    setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid palette(mid);border-radius:3px;padding:4px 12px;%1}"
        "QToolButton:hover{border-color:palette(highlight);}"
        "QToolButton::menu-indicator{width:0px;}")
        .arg(active ? QStringLiteral("font-weight:bold;color:palette(highlight);") : QString()));
}
