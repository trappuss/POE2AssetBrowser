// Container-only research: for each version of a model format, dump real bytes — file size, the
// offset of the "DOLm" geometry tag (if any), the header bytes before it, and the DOLm block head —
// so a parser for the older versions rests on measured layout, not a guess.
//
// usage: mesh_probe <bundlesDir> <.ext> [samplesPerVersion=2]

#include "store/AssetStore.h"
#include "bundle/BundleIndex.h"
#include <QCoreApplication>
#include <QMap>
#include <cstdio>
#include <cstring>

static void hexdump(const QByteArray& d, int from, int len)
{
    len = qMin(len, d.size() - from);
    for (int i = 0; i < len; i += 16) {
        printf("    %04x: ", from + i);
        for (int j = 0; j < 16; ++j) {
            if (i + j < len) printf("%02x ", uint8_t(d[from + i + j])); else printf("   ");
        }
        printf(" |");
        for (int j = 0; j < 16 && i + j < len; ++j) { char c = d[from+i+j]; printf("%c", (c>=32&&c<127)?c:'.'); }
        printf("|\n");
    }
}

static int findTag(const QByteArray& d, const char* tag, int from = 0)
{
    const int n = int(std::strlen(tag));
    for (int i = from; i + n <= d.size(); ++i) if (std::memcmp(d.constData() + i, tag, n) == 0) return i;
    return -1;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "usage: mesh_probe <bundlesDir> <.ext> [samples=2]\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }
    const BundleIndex& idx = store.index();
    const QString extName = QString::fromLocal8Bit(argv[2]);
    const int samples = argc > 3 ? QString::fromLocal8Bit(argv[3]).toInt() : 2;
    const int ext = idx.extensions().indexOf(extName);
    if (ext < 0) { fprintf(stderr, "ext not present\n"); return 1; }

    QMap<int,int> shown;
    for (uint32_t i = 0; i < idx.files().size(); ++i) {
        if (idx.files()[i].extId != uint16_t(ext)) continue;
        const QByteArray d = store.readFile(i, nullptr);
        if (d.isEmpty()) continue;
        const int ver = uint8_t(d[0]);
        if (shown.value(ver, 0) >= samples) continue;
        shown[ver]++;
        const int dolm = findTag(d, "DOLm");
        printf("\n=== %s  v%d  size=%d  DOLm@%d ===\n", qPrintable(idx.pathOf(i)), ver, d.size(), dolm);
        printf("  head:\n"); hexdump(d, 0, 64);
        if (dolm >= 0) { printf("  around DOLm:\n"); hexdump(d, dolm, 48); }
    }
    return 0;
}
