#include "store/IndexLoader.h"
#include "store/AssetStore.h"

IndexLoader::IndexLoader(AssetStore* store, QObject* parent) : QObject(parent), m_store(store) {}

IndexLoader::~IndexLoader()
{
    if (m_thread) { m_thread->quit(); m_thread->wait(); }
}

void IndexLoader::start(const QString& bundlesDir)
{
    if (m_thread) { m_thread->quit(); m_thread->wait(); delete m_thread; m_thread = nullptr; }
    const quint64 gen = ++m_generation;
    m_thread = QThread::create([this, bundlesDir, gen] {
        QString err;
        const bool ok = m_store->open(bundlesDir, &err, [this, gen](const QString& s) {
            if (gen == m_generation) emit progress(s);
        });
        // Only the newest generation reports; an older build discards itself (template §2).
        if (gen != m_generation) return;
        emit finished(ok, err);
        // The index is ready and the tabs are populating; now build the (heavier) shader-family
        // classification and the true-name index on this same worker so the UI thread never blocks.
        // Both are cached after the first game build, so this is instant on later runs.
        if (!ok || gen != m_generation) return;
        const auto rep = [this, gen](const QString& s) { if (gen == m_generation) emit progress(s); };
        m_store->buildMaterialIndex(rep);
        m_store->buildNameIndex(rep);           // no-op / disabled if the data tables are absent
        if (gen == m_generation) emit materialsReady();
    });
    m_thread->start();
}
