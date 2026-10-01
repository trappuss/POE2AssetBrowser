#include "bulk/BulkExtractor.h"
#include "store/AssetStore.h"
#include "util/NameTemplate.h"

#include "bundle/BundleIndex.h"
#include "app/SehGuard.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSaveFile>
#include <QSet>
#include <QThread>

#include <atomic>
#include <thread>
#include <vector>

namespace {
// The manifest is a flat JSON object { "<game path>": "<relative output path>" } in the run root.
// Only-new consults it AND checks the file still exists, so deleting an output re-exports it.
QString manifestPath(const QString& root) { return QDir(root).filePath(QStringLiteral("_bulk_manifest.json")); }
QString failedPath(const QString& root)   { return QDir(root).filePath(QStringLiteral("_bulk_failed.txt")); }

QJsonObject loadManifest(const QString& root)
{
    QFile f(manifestPath(root));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
void saveManifest(const QString& root, const QJsonObject& m)
{
    QFile f(manifestPath(root));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(m).toJson(QJsonDocument::Indented));
}
}  // namespace

BulkExtractor::BulkExtractor(AssetStore* store, QVector<ExportLayout::Item> items, Options opt, QObject* parent)
    : QObject(parent), m_store(store), m_items(std::move(items)), m_opt(std::move(opt)) {}

bool BulkExtractor::exportModel(const ExportLayout::Item& it, const QString& outPath, QString* error)
{
    ModelGeometry geo;
    if (!m_store->loadModel(it.gamePath, geo, error)) return false;
    AstSkeleton::Skeleton skel = m_opt.glb.includeSkeleton ? m_store->loadSkeletonFor(it.gamePath, m_opt.glb.includeAnimations, geo.jointPaletteSize())
                                                           : AstSkeleton::Skeleton{};
    QVector<GlbExporter::ExportMaterial> mats = m_store->resolveMaterials(geo, m_opt.glb.embedTextures);
    return GlbExporter::write(geo, skel, mats, m_opt.glb, outPath, error);
}

bool BulkExtractor::exportTexture(const ExportLayout::Item& it, const QString& outPath, QString* error)
{
    const QImage img = m_store->loadTexture(it.gamePath, error, nullptr);
    if (img.isNull()) { if (error && error->isEmpty()) *error = QStringLiteral("decode returned no image"); return false; }
    if (!img.save(outPath, "PNG")) { if (error) *error = QStringLiteral("PNG write failed: %1").arg(outPath); return false; }
    return true;
}

// Raw mode: write this item's EXACT original bytes (a model pulls its whole dependency set) under
// outDir, mirroring the game path so .mat→.dds and .sm→.smd references still resolve. Models share
// materials and textures heavily, so the same original is a dependency of many items; each unique
// output path is written exactly ONCE across the whole run, and atomically, so two workers can never
// truncate + interleave the same file into a corrupt original. Fails only if nothing could be written.
bool BulkExtractor::exportRaw(const ExportLayout::Item& it, QString* error)
{
    const QString lower = it.gamePath.toLower();
    const bool isModel = lower.endsWith(QStringLiteral(".smd")) || lower.endsWith(QStringLiteral(".fmt"));
    QStringList files = isModel ? m_store->collectAssetFiles(it.gamePath, /*textures*/true, /*skeleton*/true)
                                : QStringList{ lower };
    if (!isModel) { const QString hdr = lower + QStringLiteral(".header"); if (m_store->index().find(hdr)) files << hdr; }
    int wrote = 0;
    for (const QString& gp : files) {
        const QString outPath = QDir(m_opt.outDir).filePath(gp);
        const QString key = QDir::cleanPath(outPath).toLower();
        // Claim the target path before doing anything with it: whoever claims it first is the sole
        // writer this run; everyone else treats it as done (the bytes are identical either way).
        {
            QMutexLocker cl(&m_rawMutex);
            if (m_rawClaimed.contains(key)) { ++wrote; continue; }
            m_rawClaimed.insert(key);
        }
        if (QFileInfo::exists(outPath)) { ++wrote; continue; }        // already on disk from a prior run
        const QByteArray bytes = m_store->readFile(gp, nullptr);
        if (bytes.isEmpty()) continue;
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        // QSaveFile commits atomically (writes a temp, then renames), so a reader/another worker never
        // observes a half-written original.
        QSaveFile f(outPath);
        if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit()) ++wrote;
    }
    if (wrote == 0) { if (error) *error = QStringLiteral("no original files could be read"); return false; }
    return true;
}

void BulkExtractor::run()
{
    const int total = m_items.size();
    QDir().mkpath(m_opt.outDir);

    // Shared ledger, all touched only under `agg`.
    QMutex agg;
    QJsonObject manifest = loadManifest(m_opt.outDir);
    QFile failLog(failedPath(m_opt.outDir));
    bool failLogOpen = false;
    QSet<QString> claimedOut;                // converted-mode output paths already reserved this run
    int exported = 0, skipped = 0, failed = 0, doneCount = 0;
    QStringList pendingMsgs;                 // FAIL lines queued for the coordinator to emit
    // The item most recently picked up, for the progress label. An atomic int (not a shared QString)
    // so the coordinator never touches a copy-on-write buffer a worker also holds: it resolves the
    // name from m_items.at(idx), a detach-free concurrent read.
    std::atomic<int> lastIdx{-1};

    // Worker count: Options::workers, else auto. CPU-bound, so scale with cores; never exceed the
    // work available.
    int nWorkers = m_opt.workers > 0 ? m_opt.workers : QThread::idealThreadCount();
    nWorkers = qBound(1, nWorkers, 32);
    if (total > 0) nWorkers = qMin(nWorkers, total);

    std::atomic<int> nextIdx{0};             // the shared work cursor
    std::atomic<int> finishedWorkers{0};

    QElapsedTimer clock; clock.start();

    // One worker: pull the next item, export it, record the result. Pause is checked between items
    // (matching the serial behaviour — a file in flight finishes). Cancel stops the loop.
    auto worker = [&]() {
        for (;;) {
            if (m_cancel.loadRelaxed()) break;
            while (m_paused.loadRelaxed() && !m_cancel.loadRelaxed()) QThread::msleep(60);
            if (m_cancel.loadRelaxed()) break;

            const int i = nextIdx.fetch_add(1);
            if (i >= total) break;
            lastIdx.store(i, std::memory_order_relaxed);

            // .at() — NOT operator[]: QVector is copy-on-write and the non-const operator[] detaches
            // (mutates the shared control block), which races across workers. .at() is const, never
            // detaches, so concurrent reads of the shared item list are safe.
            const ExportLayout::Item& it = m_items.at(i);
            const QString lower = it.gamePath.toLower();
            const bool isModel = lower.endsWith(QStringLiteral(".smd")) || lower.endsWith(QStringLiteral(".fmt"));
            const bool isTex   = lower.endsWith(QStringLiteral(".dds"));

            if ((isModel && !m_opt.models) || (isTex && !m_opt.textures) || (!isModel && !isTex)) {
                // Not something this run writes — count as skipped so totals reconcile.
                QMutexLocker l(&agg); ++skipped; ++doneCount;
                continue;
            }

            const bool raw = m_opt.rawOriginals;
            // Raw mode mirrors the game path (originals keep their structure); converted mode uses the
            // chosen layout + NameTemplate and a .glb/.png extension.
            QString destDir, rel, absOut;
            bool doSkip = false;
            if (raw) {
                rel = it.gamePath;
                absOut = QDir(m_opt.outDir).filePath(it.gamePath);
                if (m_opt.onlyNew) {
                    QMutexLocker l(&agg);
                    if (manifest.contains(it.gamePath) && QFileInfo::exists(absOut)) doSkip = true;
                }
            } else {
                destDir = ExportLayout::dirFor(m_opt.outDir, m_opt.layout, it);
                const QString stem = NameTemplate::apply(QStringLiteral("{{FileName}}"), QFileInfo(it.gamePath).completeBaseName(), 0);
                const QString ext = isModel ? QStringLiteral(".glb") : QStringLiteral(".png");
                QString cand = QDir(destDir).filePath(stem + ext);
                auto norm = [](const QString& p){ return QDir::cleanPath(p).toLower(); };
                QMutexLocker l(&agg);
                const QString baseRel = QDir(m_opt.outDir).relativeFilePath(cand);
                if (m_opt.onlyNew && manifest.contains(it.gamePath) && QFileInfo::exists(cand)
                    && manifest.value(it.gamePath).toString() == baseRel) {
                    doSkip = true;   // this exact asset already exported here
                } else if (claimedOut.contains(norm(cand))) {
                    // A DIFFERENT asset already claimed this name (two paths share a base name in one
                    // destination folder — routine in Flat/Type layout). Writing both to one path would
                    // silently drop the first, or corrupt the file if two workers open it at once.
                    // Disambiguate with the asset's stable path hash, then a counter if even that clashes.
                    const QString h = QString::number(BundleIndex::hashPath(it.gamePath), 16);
                    cand = QDir(destDir).filePath(stem + QStringLiteral("_") + h + ext);
                    for (int n = 1; claimedOut.contains(norm(cand)); ++n)
                        cand = QDir(destDir).filePath(QStringLiteral("%1_%2_%3%4").arg(stem, h).arg(n).arg(ext));
                }
                if (!doSkip) claimedOut.insert(norm(cand));
                rel = QDir(m_opt.outDir).relativeFilePath(cand);
                absOut = cand;
            }
            if (doSkip) { QMutexLocker l(&agg); ++skipped; ++doneCount; continue; }

            if (!raw && !QDir().mkpath(destDir)) {
                QMutexLocker l(&agg);
                ++failed; ++doneCount;
                if (!failLogOpen) failLogOpen = failLog.open(QIODevice::WriteOnly | QIODevice::Append);
                if (failLogOpen) failLog.write(QStringLiteral("%1\tcould not create %2\n").arg(it.gamePath, destDir).toUtf8());
                pendingMsgs << QStringLiteral("FAIL  %1  (mkpath)").arg(it.gamePath);
                continue;
            }

            // Heavy work — done WITHOUT the lock so workers run in parallel.
            // One malformed asset reports to _bulk_failed.txt and the run continues; without the
            // guard an access violation inside a parser took the whole run (and the app) down.
            QString err;
            bool ok = false;
            seh::HardwareFault fault;
            const bool survived = seh::runGuarded("bulk-export", [&]() {
                ok = raw ? exportRaw(it, &err)
                         : (isModel ? exportModel(it, absOut, &err) : exportTexture(it, absOut, &err));
            }, &fault);
            if (!survived) { ok = false; err = QStringLiteral("hardware fault: %1").arg(fault.what); }

            QMutexLocker l(&agg);
            if (ok) { ++exported; manifest.insert(it.gamePath, rel); }
            else {
                ++failed;
                if (!failLogOpen) failLogOpen = failLog.open(QIODevice::WriteOnly | QIODevice::Append);
                if (failLogOpen) failLog.write(QStringLiteral("%1\t%2\n").arg(it.gamePath, err.isEmpty() ? QStringLiteral("unknown error") : err).toUtf8());
                pendingMsgs << QStringLiteral("FAIL  %1  (%2)").arg(it.gamePath, err);
            }
            ++doneCount;
        }
        finishedWorkers.fetch_add(1);
    };

    std::vector<std::thread> pool;
    pool.reserve(nWorkers);
    for (int w = 0; w < nWorkers; ++w) pool.emplace_back(worker);

    // Coordinator (this thread): owns pause-time accounting, throttled progress, periodic manifest
    // saves, and all signal emission — so signals always originate from the worker QThread.
    qint64 pausedAccum = 0, pauseStart = 0, lastSave = 0;
    bool wasPaused = false;
    for (;;) {
        QThread::msleep(80);
        const qint64 now = clock.elapsed();

        const bool p = m_paused.loadRelaxed();
        if (p && !wasPaused) { pauseStart = now; wasPaused = true; }
        else if (!p && wasPaused) { pausedAccum += now - pauseStart; wasPaused = false; }

        const bool doSave = (now - lastSave > 1000);
        int d, e, s, f; QStringList msgs; QByteArray mfBytes;
        {
            QMutexLocker l(&agg);
            d = doneCount; e = exported; s = skipped; f = failed;
            msgs.swap(pendingMsgs);
            // Serialise the manifest to an independent byte buffer UNDER the lock, then write it
            // unlocked — so no copy-on-write QJsonObject buffer is shared across the lock boundary.
            if (doSave) mfBytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
        }
        for (const QString& m : msgs) emit message(m);

        // The progress label: resolve the most-recent item's name from the shared list (a
        // detach-free .at() read), producing a coordinator-owned QString that shares nothing.
        const int li = lastIdx.load(std::memory_order_relaxed);
        const QString cur = (li >= 0 && li < total) ? QFileInfo(m_items.at(li).gamePath).fileName() : QString();

        const qint64 activeMs = now - pausedAccum - (wasPaused ? now - pauseStart : 0);
        int eta = -1;
        if (d > 0 && d < total) eta = int((double(activeMs) / d) * (total - d) / 1000.0);
        emit progress(d, total, e, s, f, cur, eta);

        // Persist the manifest periodically so a crash mid-run doesn't lose the ledger.
        if (doSave) {
            QFile f(manifestPath(m_opt.outDir));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(mfBytes);
            lastSave = now;
        }

        if (finishedWorkers.load() >= nWorkers) break;
    }
    for (std::thread& t : pool) t.join();

    // Drain any final messages and settle the ledger.
    int e, s, f; QStringList msgs;
    {
        QMutexLocker l(&agg);
        e = exported; s = skipped; f = failed; msgs.swap(pendingMsgs);
        if (failLogOpen) failLog.close();
    }
    for (const QString& m : msgs) emit message(m);
    saveManifest(m_opt.outDir, manifest);

    if (m_cancel.loadRelaxed()) emit message(QStringLiteral("Cancelled."));
    const QString summary = QStringLiteral("Done — %1 exported, %2 skipped, %3 failed → %4")
        .arg(e).arg(s).arg(f).arg(m_opt.outDir);
    emit message(summary);
    emit progress(e + s + f, total, e, s, f, QString(), 0);   // actual completed (accurate on cancel)
    emit finished(e, s, f, summary);
}
