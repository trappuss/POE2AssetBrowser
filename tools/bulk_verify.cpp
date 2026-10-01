// Container-only: prove the parallel BulkExtractor produces byte-identical output to a single
// worker, and measure the speed-up. Runs the SAME matched set through BulkExtractor twice — once
// with workers=1, once with workers=N — into two output trees, then diffs the trees file-by-file.
//
// usage: bulk_verify <bundlesDir> <N-workers> [count] [pathSubstr]
//   count      how many models + how many textures to gather (default 40 each)
//   pathSubstr only gather assets whose path contains this (default "art/models/")

#include "store/AssetStore.h"
#include "bulk/BulkExtractor.h"
#include "util/ExportLayout.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <cstdio>

static QByteArray sha1(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

// Every regular file under `root`, as a map of {relativePath -> sha1}.
static QHash<QString, QByteArray> hashTree(const QString& root)
{
    QHash<QString, QByteArray> out;
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = QDir(root).relativeFilePath(abs);
        if (rel.startsWith(QStringLiteral("_bulk_"))) continue;   // manifest/faillog are run metadata
        out.insert(rel, sha1(abs));
    }
    return out;
}

static int runOnce(AssetStore& store, const QVector<ExportLayout::Item>& items,
                   const QString& outDir, int workers, qint64* ms)
{
    QDir(outDir).removeRecursively();
    QDir().mkpath(outDir);
    BulkExtractor::Options opt;
    opt.outDir = outDir;
    opt.layout = ExportLayout::kFolder();     // exercise dirFor() nesting
    opt.onlyNew = false;                      // force a full export both times
    opt.models = true; opt.textures = true;
    opt.workers = workers;
    // Defaults for opt.glb (skeleton on, anims on, textures embedded, scale 0.005).

    BulkExtractor ex(&store, items, opt);
    QElapsedTimer t; t.start();
    ex.run();                                 // synchronous; blocks until finished
    *ms = t.elapsed();
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: bulk_verify <bundlesDir> <N-workers> [count] [pathSubstr]\n"); return 2; }
    const QString bundlesDir = QString::fromLocal8Bit(argv[1]);
    const int nWorkers = QString::fromLocal8Bit(argv[2]).toInt();
    const int count = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 40;
    const QString sub = argc > 4 ? QString::fromLocal8Bit(argv[4]).toLower() : QStringLiteral("art/models/");

    AssetStore store; QString err;
    fprintf(stderr, "opening index...\n");
    if (!store.open(bundlesDir, &err)) { fprintf(stderr, "open failed: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();

    // Gather `count` models (.smd/.fmt) and `count` textures (.dds) whose path contains `sub`.
    QVector<ExportLayout::Item> items;
    int nModels = 0, nTex = 0;
    for (uint32_t i = 0; i < idx.files().size() && (nModels < count || nTex < count); ++i) {
        const QString p = idx.pathOf(i);
        if (!p.contains(sub)) continue;
        const bool m = p.endsWith(QStringLiteral(".smd")) || p.endsWith(QStringLiteral(".fmt"));
        const bool t = p.endsWith(QStringLiteral(".dds"));
        if (m && nModels < count) { items.push_back({i, p}); ++nModels; }
        else if (t && nTex < count) { items.push_back({i, p}); ++nTex; }
    }
    fprintf(stderr, "gathered %d items (%d models, %d textures)\n", items.size(), nModels, nTex);
    if (items.isEmpty()) { fprintf(stderr, "nothing matched\n"); return 1; }

    const QString base = QStringLiteral("/root/work/bulk_verify");
    const QString dirA = base + QStringLiteral("/serial");
    const QString dirB = base + QStringLiteral("/parallel");

    // Warm-up pass: touch every bundle/decode once so the OS page cache and any lazy work is hot
    // for BOTH timed runs — otherwise whichever runs first eats the cold-read cost and the
    // comparison is meaningless.
    qint64 msWarm = 0;
    fprintf(stderr, "== warm-up (workers=%d, discarded) ==\n", nWorkers);
    runOnce(store, items, base + QStringLiteral("/warm"), nWorkers, &msWarm);
    QDir(base + QStringLiteral("/warm")).removeRecursively();

    qint64 msA = 0, msB = 0;
    fprintf(stderr, "== run A: workers=1 (warm) ==\n");                 runOnce(store, items, dirA, 1, &msA);
    fprintf(stderr, "== run B: workers=%d (warm) ==\n", nWorkers);      runOnce(store, items, dirB, nWorkers, &msB);

    // Diff the two trees.
    const auto A = hashTree(dirA);
    const auto B = hashTree(dirB);
    int same = 0, diff = 0, onlyA = 0, onlyB = 0;
    for (auto it = A.begin(); it != A.end(); ++it) {
        auto j = B.find(it.key());
        if (j == B.end()) { ++onlyA; printf("ONLY-IN-SERIAL   %s\n", qPrintable(it.key())); }
        else if (j.value() != it.value() || it.value().isEmpty()) { ++diff; printf("DIFFER           %s\n", qPrintable(it.key())); }
        else ++same;
    }
    for (auto it = B.begin(); it != B.end(); ++it)
        if (!A.contains(it.key())) { ++onlyB; printf("ONLY-IN-PARALLEL %s\n", qPrintable(it.key())); }

    printf("\n=== bulk_verify ===\n");
    printf("files: serial=%d parallel=%d  identical=%d differ=%d onlyA=%d onlyB=%d\n",
           A.size(), B.size(), same, diff, onlyA, onlyB);
    printf("time:  serial=%lldms  parallel(%d)=%lldms  speedup=%.2fx\n",
           (long long)msA, nWorkers, (long long)msB, msB > 0 ? double(msA) / double(msB) : 0.0);
    const bool ok = (diff == 0 && onlyA == 0 && onlyB == 0 && same > 0);
    printf("RESULT: %s\n", ok ? "PASS — parallel output is byte-identical to serial" : "FAIL — outputs diverge");
    return ok ? 0 : 1;
}
