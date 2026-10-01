// Container-only research: probe a .datc64 table's structure from real bytes — row count, the
// 0xBB… boundary that separates the fixed rows from the variable (string/array) heap, the derived
// fixed-row width, a hexdump of the first rows, and the first UTF-16 strings in the heap. Establishes
// the on-disk layout before any schema is applied, so name extraction rests on measured structure.
//
// usage: dat_probe <bundlesDir> <data/balance/foo.datc64> [rowsToDump=3]

#include "store/AssetStore.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: dat_probe <bundlesDir> <path.datc64> [rows]\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const int rowsToDump = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 3;

    const QByteArray d = store.readFile(QString::fromLocal8Bit(argv[2]).toLower(), &err);
    if (d.isEmpty()) { fprintf(stderr, "read: %s\n", qPrintable(err)); return 1; }
    const uint8_t* p = reinterpret_cast<const uint8_t*>(d.constData());
    const int n = d.size();

    uint32_t rowCount = 0; std::memcpy(&rowCount, p, 4);
    // Find the 8-byte 0xBB… boundary that starts the variable heap.
    long boundary = -1;
    for (long i = 4; i + 8 <= n; ++i) {
        bool all = true;
        for (int k = 0; k < 8; ++k) if (p[i+k] != 0xBB) { all = false; break; }
        if (all) { boundary = i; break; }
    }
    printf("file size=%d  rowCount=%u  boundary@%ld  heapBytes=%ld\n", n, rowCount, boundary,
           boundary >= 0 ? long(n) - (boundary + 8) : -1L);
    if (boundary < 0) { printf("no 0xBB boundary found — not the expected .datc64 layout\n"); return 1; }
    if (rowCount == 0) { printf("(zero rows)\n"); return 0; }
    const long fixedBytes = boundary - 4;
    if (fixedBytes % rowCount != 0) printf("WARNING: fixed region %ld not divisible by rowCount %u\n", fixedBytes, rowCount);
    const long rowWidth = fixedBytes / rowCount;
    printf("fixed row width = %ld bytes\n", rowWidth);

    for (int r = 0; r < rowsToDump && r < int(rowCount); ++r) {
        const long off = 4 + long(r) * rowWidth;
        printf("row %d:\n    ", r);
        for (long b = 0; b < rowWidth && b < 96; ++b) { printf("%02x ", p[off + b]); if ((b % 16) == 15) printf("\n    "); }
        printf("\n");
    }

    // First UTF-16 strings in the heap (double-null terminated). Heap starts after the boundary.
    printf("\nfirst heap strings (UTF-16LE):\n");
    long h = boundary + 8; int shown = 0;
    while (h + 2 <= n && shown < 40) {
        // A string starts where we are; read until 0x0000.
        long s = h; QString str;
        while (s + 2 <= n) {
            char16_t c; std::memcpy(&c, p + s, 2); s += 2;
            if (c == 0) break;
            str.append(QChar(c));
        }
        if (str.size() >= 2 && str.size() < 120) { printf("  @%ld (+%ld): %s\n", h, h - (boundary + 8), qPrintable(str)); ++shown; }
        h = s + 2;   // skip the extra terminator word PoE uses
        if (str.isEmpty()) h = s;   // avoid stalling on runs of zeros
        if (h <= boundary + 8) break;
    }
    return 0;
}
