#include "util/HoverPreview.h"

#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

HoverPreview::HoverPreview(QWidget* parent)
    : QFrame(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet(QStringLiteral(
        "HoverPreview{background:#17171a;border:1px solid #3a3a40;border-radius:10px;}"
        "QLabel{color:#e6e6e6;background:transparent;}"
        "QLabel#cap{color:#cfcfcf;}"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    m_img = new QLabel(this);
    m_img->setAlignment(Qt::AlignCenter);
    m_text = new QLabel(this);
    m_text->setObjectName(QStringLiteral("cap"));
    m_text->setTextFormat(Qt::RichText);
    m_text->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_text->setWordWrap(true);
    lay->addWidget(m_img, 0, Qt::AlignHCenter);
    lay->addWidget(m_text, 0);
    hide();
}

void HoverPreview::showFor(const QPixmap& source, const QString& html, const QPoint& globalPos)
{
    m_source = source;
    m_html = html;
    m_at = globalPos;
    relayout();
    show(); raise();
}

void HoverPreview::stepSize(int deltaPx)
{
    if (!isVisible()) return;
    m_previewPx = qBound(96, m_previewPx + deltaPx, 720);
    relayout();
}

void HoverPreview::relayout()
{
    if (m_source.isNull()) { m_img->hide(); m_img->clear(); }
    else {
        m_img->show();
        m_img->setPixmap(m_source.scaled(m_previewPx, m_previewPx, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_text->setVisible(!m_html.isEmpty());
    m_text->setText(m_html);
    m_text->setMaximumWidth(qMax(160, m_previewPx));
    adjustSize();

    // Anchor near the cursor, nudged fully on-screen.
    QPoint p = m_at + QPoint(18, 18);
    if (QScreen* scr = QGuiApplication::screenAt(m_at)) {
        const QRect g = scr->availableGeometry();
        if (p.x() + width()  > g.right())  p.setX(m_at.x() - width()  - 18);
        if (p.y() + height() > g.bottom()) p.setY(g.bottom() - height() - 4);
        if (p.x() < g.left()) p.setX(g.left() + 4);
        if (p.y() < g.top())  p.setY(g.top() + 4);
    }
    move(p);
}

void HoverPreview::hidePreview() { hide(); }
