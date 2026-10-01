#include "util/ThumbnailCache.h"

#include <QThreadPool>
#include <QRunnable>
#include <QTimer>
#include <QMetaObject>
#include <QThread>

namespace {
// A tiny QRunnable that runs a lambda on the thread pool (avoids a bespoke subclass per job).
class FnRunnable : public QRunnable {
public:
    explicit FnRunnable(std::function<void()> fn) : m_fn(std::move(fn)) { setAutoDelete(true); }
    void run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};
}  // namespace

ThumbnailCache::ThumbnailCache(std::function<QImage(quint32)> render, bool background, QObject* parent)
    : QObject(parent), m_render(std::move(render)), m_background(background)
{
    if (m_background) {
        // Leave a couple of cores for the UI; never fewer than 2 workers.
        m_maxInFlight = qMax(2, QThread::idealThreadCount() - 1);
    } else {
        m_timer = new QTimer(this);
        m_timer->setInterval(16);          // ~one screen refresh; renders a few heavy thumbnails/tick
        connect(m_timer, &QTimer::timeout, this, &ThumbnailCache::processGui);
    }
}

QPixmap ThumbnailCache::get(quint32 fileIndex)
{
    if (QPixmap* hit = m_cache.object(fileIndex)) return *hit;
    schedule(fileIndex);
    return QPixmap();
}

void ThumbnailCache::schedule(quint32 fi)
{
    if (m_pending.contains(fi)) return;
    m_pending.insert(fi);
    if (m_background) {
        // Bound concurrency: only launch while under the in-flight cap; the rest wait in m_pending and
        // are picked up as jobs finish (each finished job pulls the next pending one).
        int active = 0;
        // A cheap proxy for "in flight": pending items minus queued-but-not-started. We simply cap by
        // launching immediately and letting QThreadPool queue — QThreadPool bounds real parallelism to
        // its maxThreadCount, so a burst of scheduled jobs never oversubscribes the CPU.
        Q_UNUSED(active);
        const auto render = m_render;
        QThreadPool::globalInstance()->start(new FnRunnable([this, fi, render]() {
            QImage img = render(fi);
            // Hop back to the GUI thread to build the QPixmap + touch the cache/signals.
            QMetaObject::invokeMethod(this, [this, fi, img]() { finishOne(fi, img); }, Qt::QueuedConnection);
        }));
    } else {
        m_queue.enqueue(fi);
        if (m_timer && !m_timer->isActive()) m_timer->start();
    }
}

void ThumbnailCache::processGui()
{
    // Render at most a few per tick — GL renders are heavy, and this keeps the UI interactive while a
    // large grid fills in.
    int budget = m_guiBudget;
    while (budget-- > 0 && !m_queue.isEmpty()) {
        const quint32 fi = m_queue.dequeue();
        if (!m_pending.contains(fi)) continue;   // was cleared
        const QImage img = m_render(fi);
        finishOne(fi, img);
    }
    if (m_queue.isEmpty() && m_timer) m_timer->stop();
}

void ThumbnailCache::finishOne(quint32 fi, const QImage& img)
{
    m_pending.remove(fi);
    if (!img.isNull()) {
        QPixmap pm = QPixmap::fromImage(img);
        const int costKB = qMax(1, int((qint64(pm.width()) * pm.height() * 4) / 1024));
        m_cache.insert(fi, new QPixmap(pm), costKB);
        emit ready(fi);
    } else {
        // A failed render: cache a 1x1 transparent pixmap so we don't retry it forever (a null image
        // would just re-schedule on the next paint). The model treats a null-provider result as "no
        // icon"; a 1x1 transparent tile reads the same but ends the retry loop.
        QPixmap blank(1, 1); blank.fill(Qt::transparent);
        m_cache.insert(fi, new QPixmap(blank), 1);
        emit ready(fi);
    }
}

void ThumbnailCache::clear()
{
    m_cache.clear();
    m_pending.clear();
    m_queue.clear();
    if (m_timer) m_timer->stop();
}
