#pragma once
#include <QObject>
#include <QString>
#include <QThread>

class AssetStore;

// Background index build (template §2: detached thread → readyChanged signal the UI re-populates
// on → a generation counter so an in-flight build discards itself if the data dir switches). Owns
// nothing; the AssetStore lives in the main thread and is opened on the worker.
class IndexLoader : public QObject {
    Q_OBJECT
public:
    explicit IndexLoader(AssetStore* store, QObject* parent = nullptr);
    ~IndexLoader() override;

    // Start (or restart) a build for `bundlesDir`. A running build for a different dir is superseded.
    void start(const QString& bundlesDir);
    bool isRunning() const { return m_thread && m_thread->isRunning(); }

signals:
    void progress(const QString& line);
    void finished(bool ok, const QString& error);
    // Emitted after finished(true,…) once the shader-family classification is ready (loaded from
    // cache, or swept on the same background thread). The list tabs enable shader search/facets on
    // it. Not emitted when the index build failed.
    void materialsReady();

private:
    AssetStore* m_store;
    QThread* m_thread = nullptr;
    quint64 m_generation = 0;
};
