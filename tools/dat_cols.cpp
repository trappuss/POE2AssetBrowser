// Container-only research: classify each byte-offset column of a .datc64 table by decoding it as a
// string reference across a sample of rows — so the Name column and the model(.ao/.aoc) column can
// be identified from evidence, not a guessed schema. Reports, per offset that decodes to plausible
// strings: how many are .ao/.aoc paths, how many are metadata paths, how many are "plain" display
// names (no slash), with a few examples.
//
// usage: dat_cols <bundlesDir> <path.datc64> [sampleRows=200]

#include "store/AssetStore.h"
#include "store/DatFile.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: dat_cols <bundlesDir> <path.datc64> [sampleRows]\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    DatFile t;
    if (!t.load(store.readFile(QString::fromLocal8Bit(argv[2]).toLower(), &err)) || !t.isValid()) { fprintf(stderr, "load: %s\n", qPrintable(err)); return 1; }
    const uint32_t rows = t.rowCount();
    const QString arg3 = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();

    // row:N mode — dump every byte offset of one row that decodes to a clean full path or plain name,
    // so the true column layout (Name, .ao model) is legible for that known monster.
    if (arg3.startsWith(QStringLiteral("row:"))) {
        const uint32_t r = arg3.mid(4).toUInt();
        printf("row %u of %s:\n", r, argv[2]);
        for (long col = 0; col + 8 <= t.rowWidth(); ++col) {
            const QString s = t.str(r, col);
            if (s.size() < 3 || s.size() > 120) continue;
            const bool full = s.startsWith(QStringLiteral("Metadata/"), Qt::CaseInsensitive)
                           || s.startsWith(QStringLiteral("Art/"), Qt::CaseInsensitive);
            const QString sl = s.toLower();
            const bool model = sl.endsWith(QStringLiteral(".ao")) || sl.endsWith(QStringLiteral(".aoc"))
                            || sl.endsWith(QStringLiteral(".act")) || sl.endsWith(QStringLiteral(".fmt"))
                            || sl.endsWith(QStringLiteral(".smd"));
            bool plain = !s.contains(QLatin1Char('/')) && !s.contains(QLatin1Char('\\'));
            for (const QChar c : s) if (!(c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('\'') || c == QLatin1Char('[') || c == QLatin1Char(']') || c == QLatin1Char('-') || c == QLatin1Char(','))) { plain = false; break; }
            if (full || model || (plain && s.contains(QLatin1Char(' ')))) printf("  col@%-4ld = \"%s\"\n", col, qPrintable(s));
        }
        return 0;
    }

    // arr:N mode — treat each 8-aligned offset of row N as an array (u64 count @col, u64 heapOffset
    // @col+8), read the `count` u64 string-refs at that heap offset, and print any array whose refs
    // decode to clean full paths. This is how a monster's list of model .ao is stored.
    if (arg3.startsWith(QStringLiteral("arr:"))) {
        const uint32_t r = arg3.mid(4).toUInt();
        printf("row %u arrays of %s:\n", r, argv[2]);
        for (long col = 0; col + 16 <= t.rowWidth(); col += 8) {
            const uint64_t count = t.u64(r, col);
            const uint64_t off   = t.u64(r, col + 8);
            if (count == 0 || count > 32 || off == DatFile::kNull) continue;
            // Read the array's ref list from the heap via a synthetic accessor: reuse str() by
            // pointing at absolute heap positions. The array's element refs sit at heap[off]+k*8,
            // and each is itself a boundary-relative string ref.
            QStringList decoded;
            bool clean = true;
            for (uint64_t k = 0; k < count && k < 8; ++k) {
                const QString s = t.strFromArray(off, k);
                if (s.startsWith(QStringLiteral("Metadata/"), Qt::CaseInsensitive) || s.startsWith(QStringLiteral("Art/"), Qt::CaseInsensitive))
                    decoded << s;
                else { clean = false; break; }
            }
            if (clean && !decoded.isEmpty())
                printf("  col@%-4ld [%llu]: %s\n", col, (unsigned long long)count, qPrintable(decoded.join(QStringLiteral(" | ")).left(160)));
        }
        return 0;
    }

    // fkto:N mode — find byte offsets whose u64 is, for EVERY row, either < N (a valid index into a
    // table of N rows) or the 0xFE null. Identifies a foreign-key column into that table.
    if (arg3.startsWith(QStringLiteral("fkto:"))) {
        const uint64_t N = arg3.mid(5).toULongLong();
        printf("FK columns into a %llu-row table:\n", (unsigned long long)N);
        for (long col = 0; col + 8 <= t.rowWidth(); ++col) {
            bool all = true; int nonNull = 0; uint64_t maxv = 0;
            for (uint32_t r = 0; r < rows; ++r) {
                const uint64_t v = t.u64(r, col);
                if (v == DatFile::kNull) continue;
                if (v >= N) { all = false; break; }
                ++nonNull; if (v > maxv) maxv = v;
            }
            if (all && nonNull > int(rows) / 4) printf("  col@%-4ld  nonNull=%-4d maxIdx=%llu\n", col, nonNull, (unsigned long long)maxv);
        }
        return 0;
    }

    const int sample = arg3.isEmpty() ? 200 : arg3.toInt();
    const uint32_t step = rows > uint32_t(sample) ? rows / sample : 1;
    printf("%s: rows=%u width=%ld  (sampling every %u)\n", argv[2], rows, t.rowWidth(), step);

    for (long col = 0; col + 8 <= t.rowWidth(); col += 1) {
        int nonEmpty = 0, ao = 0, meta = 0, plain = 0, tested = 0;
        QString ex1, ex2;
        for (uint32_t r = 0; r < rows; r += step) {
            ++tested;
            const QString s = t.str(r, col);
            if (s.isEmpty() || s.size() > 200) continue;
            // A string-ref column decodes to readable text for MOST rows; junk offsets rarely do.
            ++nonEmpty;
            const QString sl = s.toLower();
            if (sl.endsWith(QStringLiteral(".ao")) || sl.endsWith(QStringLiteral(".aoc"))) ++ao;
            else if (sl.contains(QLatin1Char('/'))) ++meta;
            else ++plain;
            if (ex1.isEmpty()) ex1 = s; else if (ex2.isEmpty() && s != ex1) ex2 = s;
        }
        // Only report columns that look like a string column (decodes for >60% of sampled rows).
        if (tested == 0 || double(nonEmpty) / tested < 0.6) continue;
        printf("  col@%-4ld  nonEmpty=%-4d  ao=%-4d meta=%-4d plain=%-4d   e.g. \"%s\" / \"%s\"\n",
               col, nonEmpty, ao, meta, plain, qPrintable(ex1.left(46)), qPrintable(ex2.left(46)));
    }
    return 0;
}
