#pragma once
#include "model/GlbExporter.h"
#include "util/ExportLayout.h"
#include <QAtomicInt>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class AssetStore;

// The bulk-extraction run (template §14 + §25). A worker QObject that lives on its own QThread: it
// walks a fixed set of matched assets, writes each (models → .glb, textures → .png) into the layout
// Settings ▸ Export chose (util/ExportLayout.h), and reports progress. Fail-closed, template §25:
//   • Only-new skips anything already written, tracked in _bulk_manifest.json in the output folder.
//   • Failures go to _bulk_failed.txt with a reason each; one bad asset never takes the run down.
//   • Cancel and Pause are thread-safe flags; paused time is excluded from the ETA.
// Parallel pool (§25): the run walks the matched set across N worker threads. This is safe because
// AssetStore reads are thread-safe — bundle-handle caching is behind a fast mutex that is released
// before the read, Bundle::readRange serialises per bundle on its own mutex, and the Oodle decoder
// is reentrant — while the heavy per-item work (Oodle + DDS decode, ORM build, glb/png assembly,
// disk write) runs in worker-local memory. The shared ledger (manifest, counters, fail log) is
// mutex-guarded; a coordinator loop on the run thread owns pause-time accounting, throttled
// progress, and periodic manifest saves. Worker count comes from Options::workers (0 = auto =
// QThread::idealThreadCount()), clamped to [1,32].
class BulkExtractor : public QObject {
    Q_OBJECT
public:
    struct Options {
        QString outDir;
        QString layout;                 // ExportLayout id (Flat/Type/Folder/_model)
        bool    onlyNew = true;         // false = overwrite
        bool    models = true;          // export .smd/.fmt as .glb
        bool    textures = true;        // export .dds as .png
        // Non-destructive raw mode: instead of converting to .glb/.png, write the EXACT original game
        // files (a model pulls its .sm/.mat/.dds/.ao/.ast dependencies), byte-for-byte, mirroring their
        // game paths under outDir. Layout/NameTemplate are ignored (originals keep their own structure).
        bool    rawOriginals = false;
        int     workers = 0;            // parallel worker threads (0 = auto = idealThreadCount)
        GlbExporter::Options glb;       // model export options (skeleton/anims/textures)
    };

    BulkExtractor(AssetStore* store, QVector<ExportLayout::Item> items, Options opt, QObject* parent = nullptr);

    void requestCancel() { m_cancel.storeRelaxed(1); }
    void setPaused(bool p) { m_paused.storeRelaxed(p ? 1 : 0); }

public slots:
    void run();                         // invoke after moving to the worker thread

signals:
    void progress(int done, int total, int exported, int skipped, int failed,
                  const QString& current, int etaSeconds);
    void message(const QString& line);
    void finished(int exported, int skipped, int failed, const QString& summary);

private:
    // The full output path is resolved (and made collision-free) by run() and passed in, so two
    // distinct assets that share a base name never write the same file.
    bool exportModel(const ExportLayout::Item& it, const QString& outPath, QString* error);
    bool exportTexture(const ExportLayout::Item& it, const QString& outPath, QString* error);
    bool exportRaw(const ExportLayout::Item& it, QString* error);   // write exact originals mirroring game paths

    AssetStore* m_store;
    QVector<ExportLayout::Item> m_items;
    Options m_opt;
    QAtomicInt m_cancel{0};
    QAtomicInt m_paused{0};
    // Raw mode: the union of a run's dependency sets is written once. This guards the shared set of
    // output paths already claimed by some worker, so two workers never write the same original file
    // concurrently (which would interleave into a corrupt file).
    QMutex m_rawMutex;
    QSet<QString> m_rawClaimed;
};
