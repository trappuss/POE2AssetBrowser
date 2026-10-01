#pragma once
// Lazily produces per-file thumbnails for the grid view, with a bounded cache so scrolling a large
// grid never re-renders what is already done (the icon-cache discipline is ported from D4's
// SnoListModel — see AssetListModel::iconData). Two modes, chosen by whether the render function is
// thread-safe:
//   • background = true  — the render fn is pure/thread-safe (e.g. a DDS decode); run on a thread pool.
//   • background = false — the render fn must run on the GUI thread (e.g. an OpenGL render); run a few
//                          per QTimer tick so the UI stays responsive.
// The render fn returns a QImage — a QPixmap can only be built on the GUI thread, which this class does
// when the image lands. get() returns the finished pixmap, or a null pixmap while it schedules the
// render; ready(fileIndex) fires when a thumbnail becomes available so the model can repaint that row.
#include <QCache>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QQueue>
#include <QSet>
#include <functional>

class QTimer;

class ThumbnailCache : public QObject {
    Q_OBJECT
public:
    ThumbnailCache(std::function<QImage(quint32)> render, bool background, QObject* parent = nullptr);

    QPixmap get(quint32 fileIndex);        // cached pixmap, or a null pixmap + schedule a render
    void clear();                          // drop everything (the data source changed)
    void setBudgetKB(int kb) { m_cache.setMaxCost(qMax(1024, kb)); }
    void setGuiBudget(int n) { m_guiBudget = qMax(1, n); }   // GUI mode: how many to render per tick

signals:
    void ready(quint32 fileIndex);

private:
    void schedule(quint32 fi);
    void processGui();                     // GUI mode: render a bounded number of pending per tick
    void finishOne(quint32 fi, const QImage& img);   // GUI thread: cache + emit ready

    std::function<QImage(quint32)> m_render;
    bool m_background;
    QCache<quint32, QPixmap> m_cache{ 128 * 1024 };   // cost = KB of pixmap
    QSet<quint32> m_pending;
    QQueue<quint32> m_queue;                // GUI mode render order
    QTimer* m_timer = nullptr;
    int m_maxInFlight = 0;                  // background: cap concurrent jobs
    int m_guiBudget = 3;                    // GUI mode: renders per tick
};
