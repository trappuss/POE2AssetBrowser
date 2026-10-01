#pragma once
// A compact card popup that shows a preview image with a couple of info lines when the pointer dwells
// over a grid/list row. One per tab; the tab arms it from a hover (QEvent::ToolTip) and can resize the
// preview live while it's shown (mouse-wheel over the row). Frameless tool window so it floats over the
// view; laid out as a card — image on top, caption beneath — rather than a bulky side-by-side panel.
#include <QFrame>
#include <QPixmap>
#include <QPoint>
#include <QString>

class QLabel;

class HoverPreview : public QFrame {
    Q_OBJECT
public:
    explicit HoverPreview(QWidget* parent = nullptr);

    // Show near globalPos with `source` (the best-resolution image available) scaled to the current
    // preview size, and `html` info beneath. A null source shows just the text.
    void showFor(const QPixmap& source, const QString& html, const QPoint& globalPos);
    void stepSize(int deltaPx);          // grow/shrink the preview image while it is shown
    bool isShowing() const { return isVisible(); }
    void hidePreview();

private:
    void relayout();

    QLabel* m_img = nullptr;
    QLabel* m_text = nullptr;
    QPixmap m_source;                    // unscaled best-res source; rescaled on resize
    QString m_html;
    QPoint  m_at;                        // anchor (global cursor position)
    int     m_previewPx = 240;           // current preview edge length
};
