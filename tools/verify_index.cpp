// Container-only verification harness: runs the real C++ Bundle + BundleIndex against the staged
// game index and prints numbers to diff against the Python oracle (tools/formats/parse_index.py).
// Not part of the app; built by tools/verify_container.sh.
#include "bundle/BundleIndex.h"
#include "bundle/Bundle.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTextStream>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString bundlesDir = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                        : QStringLiteral("/mnt/user-data/uploads/Path of Exile 2/Bundles2");
    BundleIndex idx;
    QString err;
    if (!idx.load(bundlesDir, QString(), &err, [](const QString& s){ fprintf(stderr, "  %s\n", qPrintable(s)); })) {
        fprintf(stderr, "load failed: %s\n", qPrintable(err));
        return 1;
    }
    printf("bundles=%zu files=%zu directories=%zu extensions=%d\n",
           idx.bundles().size(), idx.files().size(), idx.directories().size(), idx.extensions().size());
    printf("totalRecordsInIndex=%llu unnamed=%u sharedPayloads=%u\n",
           (unsigned long long)idx.totalRecordsInIndex(), idx.unnamedFiles(), idx.sharedPayloadFiles());
    for (const auto& s : idx.skippedRoots())
        printf("skipped root %-24s %u\n", qPrintable(s.root), s.files);

    // Hash-round-trip: every listed file's path must hash back to itself.
    quint64 mismatch = 0;
    for (uint32_t i = 0; i < idx.files().size(); ++i) {
        if (idx.files()[i].nameLen == 0) continue;
        if (BundleIndex::hashPath(idx.pathOf(i)) != idx.files()[i].hash) ++mismatch;
    }
    printf("path->hash round-trip mismatches: %llu\n", (unsigned long long)mismatch);

    // Known hashes from the wiki (post-3.21.2 murmur).
    printf("hash(audio/haptics)=0x%016llX (expect A6728264DDB4B5B9)\n",
           (unsigned long long)BundleIndex::hashPath(QStringLiteral("audio/haptics")));

    // Write the sorted (hash,size,path) list so it can be diffed against files.tsv.
    if (argc > 2) {
        QFile out(QString::fromLocal8Bit(argv[2]));
        if (out.open(QIODevice::WriteOnly)) {
            QTextStream ts(&out);
            for (uint32_t i = 0; i < idx.files().size(); ++i)
                ts << QString::asprintf("%016llX\t%u\t", (unsigned long long)idx.files()[i].hash, idx.files()[i].size)
                   << idx.pathOf(i) << '\n';
        }
    }

    // Extract one known file whose bytes the Python side also produced and md5 it.
    if (argc > 3) {
        uint32_t fi = 0;
        const BundleIndex::FileRecord* r = idx.find(QString::fromLocal8Bit(argv[3]), &fi);
        if (!r) { printf("extract: %s not found\n", argv[3]); return 0; }
        auto b = Bundle::open(idx.bundlesDir() + "/" + idx.bundles()[r->bundle].name + ".bundle.bin", &err);
        if (!b) { printf("extract: bundle open failed: %s\n", qPrintable(err)); return 0; }
        const QByteArray data = b->readRange(r->offset, r->size, &err);
        if (data.isEmpty()) { printf("extract: read failed: %s\n", qPrintable(err)); return 0; }
        printf("extract %s: %d bytes md5=%s\n", argv[3], data.size(),
               QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex().constData());
    }
    return 0;
}
