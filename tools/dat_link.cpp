// Container-only research: discover the BaseItemTypes → ItemVisualIdentity foreign-key column by
// SELF-VALIDATION (no external schema trusted blindly): for each u64-aligned column in BaseItemTypes,
// treat it as an IVI row index and measure how often the referenced IVI row's Id equals the last
// path segment of the BaseItemType's own Id. The column that validates broadly IS the FK. Then print
// the resulting model(.ao) → in-game-name mapping for a sample, proving name resolution end-to-end.
//
// usage: dat_link <bundlesDir>

#include "store/AssetStore.h"
#include <QCoreApplication>
#include <QHash>
#include <cstdio>
#include <cstring>

// Minimal .datc64 reader — layout proven in tools/dat_probe.cpp: u32 rowCount, fixed rows, an
// 8-byte 0xBB boundary, then a heap; string/data refs are u64 offsets from the boundary position,
// heap bytes begin at boundary+8. FK/refs of 0xFE… are null.
struct Dat {
    QByteArray bytes;
    const uint8_t* p = nullptr;
    long size = 0, boundary = -1, rowWidth = 0;
    uint32_t rowCount = 0;

    bool load(AssetStore& store, const QString& path) {
        bytes = store.readFile(path, nullptr);
        if (bytes.isEmpty()) return false;
        p = reinterpret_cast<const uint8_t*>(bytes.constData()); size = bytes.size();
        std::memcpy(&rowCount, p, 4);
        for (long i = 4; i + 8 <= size; ++i) { bool a=true; for(int k=0;k<8;++k) if(p[i+k]!=0xBB){a=false;break;} if(a){boundary=i;break;} }
        if (boundary < 0 || rowCount == 0) return false;
        rowWidth = (boundary - 4) / rowCount;
        return (boundary - 4) % rowCount == 0;
    }
    const uint8_t* row(uint32_t r) const { return p + 4 + long(r) * rowWidth; }
    uint64_t u64at(uint32_t r, long col) const { uint64_t v; std::memcpy(&v, row(r) + col, 8); return v; }
    // A string at heap offset `ref` (ref measured from the boundary position).
    QString str(uint64_t ref) const {
        long s = boundary + long(ref);
        if (s < boundary + 8 || s + 2 > size) return QString();
        QString out;
        while (s + 2 <= size) { char16_t c; std::memcpy(&c, p + s, 2); s += 2; if (c == 0) break; out.append(QChar(c)); if (out.size() > 400) break; }
        return out;
    }
    QString strCol(uint32_t r, long col) const { return str(u64at(r, col)); }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { fprintf(stderr, "usage: dat_link <bundlesDir>\n"); return 2; }
    AssetStore store; QString err;
    if (!store.open(QString::fromLocal8Bit(argv[1]), &err)) { fprintf(stderr, "open: %s\n", qPrintable(err)); return 1; }

    Dat bit, ivi;
    if (!bit.load(store, QStringLiteral("data/balance/baseitemtypes.datc64"))) { fprintf(stderr, "BIT load failed\n"); return 1; }
    if (!ivi.load(store, QStringLiteral("data/balance/itemvisualidentity.datc64"))) { fprintf(stderr, "IVI load failed\n"); return 1; }
    printf("BaseItemTypes: rows=%u width=%ld    ItemVisualIdentity: rows=%u width=%ld\n",
           bit.rowCount, bit.rowWidth, ivi.rowCount, ivi.rowWidth);

    // IVI: col@0 = Id, col@16 = AOFile (proven in dat_probe). Precompute IVI Ids.
    QVector<QString> iviId(ivi.rowCount), iviAO(ivi.rowCount);
    for (uint32_t r = 0; r < ivi.rowCount; ++r) { iviId[r] = ivi.strCol(r, 0); iviAO[r] = ivi.strCol(r, 16); }

    // A foreign-key column is, for EVERY row, either a valid IVI index or the 16-byte 0xFE null —
    // string-ref columns hold heap byte-offsets (up to ~%heap, mostly out of IVI's index range) and
    // int columns vary, so both fail this test. Among the always-valid-or-null columns, the IVI FK
    // is the one whose resolved .ao paths are item art (metadata/items/…).
    // Category oracle: the true FK maps a BaseItemType to an ItemVisualIdentity whose .ao lives in
    // the SAME item category — segment 2 of "Metadata/Items/<Category>/…" must match between the
    // item's own Id and its linked .ao. A column that indexes a valid-but-wrong IVI row (e.g. a
    // small-range enum landing in the currency block) fails this across the 5,496 rows.
    auto seg2 = [](const QString& s){ const QStringList p = s.split(QLatin1Char('/'), Qt::SkipEmptyParts); return p.size() > 2 ? p[2].toLower() : QString(); };
    const uint64_t kNull = 0xFEFEFEFEFEFEFEFEULL;
    // Scan EVERY byte offset, not just 8-aligned ones — .datc64 packs fields tightly (bool=1, i32=4),
    // so a 16-byte foreign key can start at any offset.
    long bestCol = -1; double bestScore = -1; int bestDistinct = 0;
    for (long col = 0; col + 8 <= bit.rowWidth; col += 1) {
        bool allValid = true; int nonNull = 0, catMatch = 0; QSet<uint32_t> distinct;
        for (uint32_t r = 0; r < bit.rowCount; ++r) {
            const uint64_t v = bit.u64at(r, col);
            if (v == kNull) continue;
            if (v >= ivi.rowCount) { allValid = false; break; }
            ++nonNull; distinct.insert(uint32_t(v));
            if (!seg2(iviAO[v]).isEmpty() && seg2(iviAO[v]) == seg2(bit.strCol(r, 0))) ++catMatch;
        }
        if (!allValid || nonNull < 100) continue;
        const double score = double(catMatch) / nonNull;
        if (score > bestScore) { bestScore = score; bestCol = col; bestDistinct = distinct.size(); }
    }
    printf("discovered IVI-FK column offset = %ld  (category match %.1f%%, %d distinct IVI rows)\n",
           bestCol, bestScore * 100.0, bestDistinct);
    if (bestCol < 0 || bestScore < 0.6) { printf("FK column not confidently found\n"); return 1; }

    // Spot-check KNOWN equipment base types (schema-free confirmation that col@bestCol is the IVI FK).
    printf("\n=== spot-check: known base items → name → .ao ===\n");
    const char* probes[] = {"onehandaxe", "onehandsword", "twohandaxe", "bodyarmour", "helmet", "boots", "bow", "quarterstaff"};
    for (const char* pr : probes) {
        int shown = 0;
        for (uint32_t r = 0; r < bit.rowCount && shown < 2; ++r) {
            const QString id = bit.strCol(r, 0);
            if (!id.toLower().contains(QString::fromLatin1(pr))) continue;
            const uint64_t v = bit.u64at(r, bestCol);
            const QString ao = (v < ivi.rowCount) ? iviAO[v] : QStringLiteral("(none)");
            printf("  %-58s  \"%s\"\n      .ao=%s\n", qPrintable(id), qPrintable(bit.strCol(r, 32)), qPrintable(ao));
            ++shown;
        }
    }

    // Shared-.ao structure: how many base types map to each .ao, and does a clean canonical name exist?
    QHash<QString, QVector<QString>> aoToNames;
    int linked = 0;
    for (uint32_t r = 0; r < bit.rowCount; ++r) {
        const uint64_t v = bit.u64at(r, bestCol);
        if (v >= ivi.rowCount) continue;
        const QString ao = iviAO[v].toLower(); const QString name = bit.strCol(r, 32);
        if (ao.isEmpty() || name.isEmpty()) continue;
        aoToNames[ao].push_back(name); ++linked;
    }
    int shared = 0; for (auto it = aoToNames.begin(); it != aoToNames.end(); ++it) if (it.value().size() > 1) ++shared;
    printf("\nAOFile→Name: %d links, %d distinct .ao, %d of them used by >1 base type\n", linked, aoToNames.size(), shared);
    printf("examples of shared .ao (all base-type names that use it):\n");
    int shown = 0;
    for (auto it = aoToNames.begin(); it != aoToNames.end() && shown < 8; ++it) {
        if (it.value().size() < 2) continue;
        printf("  %s :\n", qPrintable(it.key()));
        for (const QString& n : it.value()) printf("      \"%s\"\n", qPrintable(n));
        ++shown;
    }
    return 0;
}
