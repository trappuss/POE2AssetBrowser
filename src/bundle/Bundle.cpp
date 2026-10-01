#include "bundle/Bundle.h"
#include "bundle/Oodle.h"

#include <QFile>
#include <QMutexLocker>
#include <cstring>

namespace {
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }
constexpr size_t kFixedHead = 12 + 48;   // three u32 + the 48-byte head payload before block sizes
}

bool parseBundleHeader(const uint8_t* d, size_t len, Bundle::Header& h, QString* error)
{
    if (len < kFixedHead) { if (error) *error = QStringLiteral("bundle too small for a header (%1 bytes)").arg(len); return false; }
    h.uncompressedSize = rd32(d + 0);
    h.totalPayloadSize = rd32(d + 4);
    h.headPayloadSize  = rd32(d + 8);
    h.firstFileEncode  = rd32(d + 12);
    const uint64_t uncompressed2 = rd64(d + 20);
    const uint64_t payload2      = rd64(d + 28);
    h.blockCount  = rd32(d + 36);
    h.granularity = rd32(d + 40);
    if (uncompressed2 != h.uncompressedSize || payload2 != h.totalPayloadSize) {
        if (error) *error = QStringLiteral("bundle header disagrees with itself (u32/u64 sizes differ)");
        return false;
    }
    if (h.granularity == 0 || h.granularity > (64u << 20)) {
        if (error) *error = QStringLiteral("bundle block granularity %1 is not plausible").arg(h.granularity);
        return false;
    }
    if (h.headPayloadSize != 48 + 4ull * h.blockCount) {
        if (error) *error = QStringLiteral("head_payload_size %1 != 48 + 4*block_count (%2)").arg(h.headPayloadSize).arg(h.blockCount);
        return false;
    }
    if (len < 12 + (size_t)h.headPayloadSize) {
        if (error) *error = QStringLiteral("bundle truncated inside the block-size table");
        return false;
    }
    // uncompressed size must fit the block count: every block but the last is exactly one granule
    const uint64_t expectBlocks = h.uncompressedSize == 0 ? 0 : (h.uncompressedSize + h.granularity - 1) / h.granularity;
    if (expectBlocks != h.blockCount) {
        if (error) *error = QStringLiteral("block_count %1 does not match uncompressed_size %2 / granularity %3")
                                .arg(h.blockCount).arg(h.uncompressedSize).arg(h.granularity);
        return false;
    }
    h.blockSizes.resize(int(h.blockCount));
    h.blockOffsets.resize(int(h.blockCount));
    uint64_t off = 12 + h.headPayloadSize, sum = 0;
    for (uint32_t i = 0; i < h.blockCount; ++i) {
        const uint32_t bs = rd32(d + 60 + 4 * i);
        h.blockSizes[int(i)] = bs;
        h.blockOffsets[int(i)] = off;
        off += bs; sum += bs;
    }
    if (sum != h.totalPayloadSize) {
        if (error) *error = QStringLiteral("sum of block sizes %1 != total_payload_size %2").arg(sum).arg(h.totalPayloadSize);
        return false;
    }
    return true;
}

std::shared_ptr<Bundle> Bundle::open(const QString& filePath, QString* error)
{
    std::shared_ptr<Bundle> b(new Bundle);
    b->m_path = filePath;
    b->m_file.reset(new QFile(filePath));
    if (!b->m_file->open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot open %1: %2").arg(filePath, b->m_file->errorString());
        return nullptr;
    }
    if (!b->parseHeader(error)) return nullptr;
    return b;
}

std::shared_ptr<Bundle> Bundle::fromMemory(const QByteArray& bytes, QString* error)
{
    std::shared_ptr<Bundle> b(new Bundle);
    b->m_path = QStringLiteral("<memory>");
    b->m_memory = bytes;
    if (!b->parseHeader(error)) return nullptr;
    return b;
}

bool Bundle::parseHeader(QString* error)
{
    QByteArray head;
    if (m_file) {
        head = m_file->read(qint64(kFixedHead));
        if (head.size() < int(kFixedHead)) { if (error) *error = QStringLiteral("%1: too small to be a bundle").arg(m_path); return false; }
        const uint32_t hps = rd32(reinterpret_cast<const uint8_t*>(head.constData()) + 8);
        if (hps > (256u << 20)) { if (error) *error = QStringLiteral("%1: absurd head_payload_size").arg(m_path); return false; }
        m_file->seek(0);
        head = m_file->read(qint64(12 + hps));
        if (head.size() < int(12 + hps)) { if (error) *error = QStringLiteral("%1: truncated header").arg(m_path); return false; }
        if (!parseBundleHeader(reinterpret_cast<const uint8_t*>(head.constData()), size_t(head.size()), m_hdr, error)) {
            if (error) error->prepend(m_path + QStringLiteral(": "));
            return false;
        }
        const uint64_t need = 12 + m_hdr.headPayloadSize + m_hdr.totalPayloadSize;
        if (uint64_t(m_file->size()) < need) {
            if (error) *error = QStringLiteral("%1: file is %2 bytes, header promises %3").arg(m_path).arg(m_file->size()).arg(need);
            return false;
        }
    } else {
        if (!parseBundleHeader(reinterpret_cast<const uint8_t*>(m_memory.constData()), size_t(m_memory.size()), m_hdr, error)) return false;
        const uint64_t need = 12 + m_hdr.headPayloadSize + m_hdr.totalPayloadSize;
        if (uint64_t(m_memory.size()) < need) { if (error) *error = QStringLiteral("in-memory bundle truncated"); return false; }
    }
    return true;
}

bool Bundle::readCompressedBlock(uint32_t idx, QByteArray& out, QString* error)
{
    const uint64_t off = m_hdr.blockOffsets[int(idx)];
    const uint32_t sz = m_hdr.blockSizes[int(idx)];
    if (m_file) {
        if (!m_file->seek(qint64(off))) { if (error) *error = QStringLiteral("%1: seek failed").arg(m_path); return false; }
        out = m_file->read(sz);
        if (out.size() != int(sz)) { if (error) *error = QStringLiteral("%1: short read of block %2").arg(m_path).arg(idx); return false; }
    } else {
        out = m_memory.mid(int(off), int(sz));
    }
    return true;
}

// Caller holds m_mutex.
QByteArray Bundle::decodeBlock(uint32_t idx, QString* error)
{
    for (CachedBlock& c : m_cache)
        if (c.idx == idx) { c.stamp = ++m_stamp; return c.data; }

    QByteArray comp;
    if (!readCompressedBlock(idx, comp, error)) return QByteArray();
    const uint64_t want = (idx + 1 == m_hdr.blockCount)
        ? m_hdr.uncompressedSize - uint64_t(m_hdr.granularity) * (m_hdr.blockCount - 1)
        : m_hdr.granularity;
    QByteArray out(int(want + oodle::kOverrun), Qt::Uninitialized);
    const int n = oodle::decompress(reinterpret_cast<const uint8_t*>(comp.constData()), size_t(comp.size()),
                                    reinterpret_cast<uint8_t*>(out.data()), size_t(want));
    if (n != int(want)) {
        if (error) *error = QStringLiteral("%1: Oodle decode of block %2 returned %3, expected %4 (block starts %5 %6)")
            .arg(m_path).arg(idx).arg(n).arg(want)
            .arg(comp.size() > 0 ? uint8_t(comp[0]) : 0, 2, 16, QLatin1Char('0'))
            .arg(comp.size() > 1 ? uint8_t(comp[1]) : 0, 2, 16, QLatin1Char('0'));
        return QByteArray();
    }
    out.resize(int(want));
    if (m_cacheBlocks > 0) {
        if (m_cache.size() < m_cacheBlocks) m_cache.append(CachedBlock{idx, out, ++m_stamp});
        else {
            int victim = 0;
            for (int i = 1; i < m_cache.size(); ++i) if (m_cache[i].stamp < m_cache[victim].stamp) victim = i;
            m_cache[victim] = CachedBlock{idx, out, ++m_stamp};
        }
    }
    return out;
}

QByteArray Bundle::readRange(uint64_t offset, uint64_t size, QString* error)
{
    if (size == 0) return QByteArray();
    if (offset + size > m_hdr.uncompressedSize) {
        if (error) *error = QStringLiteral("%1: range %2+%3 exceeds payload %4").arg(m_path).arg(offset).arg(size).arg(m_hdr.uncompressedSize);
        return QByteArray();
    }
    if (size > (1ull << 31)) { if (error) *error = QStringLiteral("%1: range too large").arg(m_path); return QByteArray(); }
    QMutexLocker lock(&m_mutex);
    QByteArray out;
    out.reserve(int(size));
    const uint32_t g = m_hdr.granularity;
    uint32_t first = uint32_t(offset / g), last = uint32_t((offset + size - 1) / g);
    for (uint32_t i = first; i <= last; ++i) {
        const QByteArray blk = decodeBlock(i, error);
        if (blk.isEmpty()) return QByteArray();
        const uint64_t blkStart = uint64_t(i) * g;
        const uint64_t from = (i == first) ? offset - blkStart : 0;
        const uint64_t to   = (i == last)  ? offset + size - blkStart : uint64_t(blk.size());
        if (to > uint64_t(blk.size()) || from > to) { if (error) *error = QStringLiteral("%1: block %2 shorter than expected").arg(m_path).arg(i); return QByteArray(); }
        out.append(blk.constData() + from, int(to - from));
    }
    return out;
}

QByteArray Bundle::readAll(QString* error)
{
    if (m_hdr.uncompressedSize == 0) return QByteArray();
    if (m_hdr.uncompressedSize > (1ull << 31)) { if (error) *error = QStringLiteral("%1: too large to read whole").arg(m_path); return QByteArray(); }
    QMutexLocker lock(&m_mutex);
    QByteArray out(int(m_hdr.uncompressedSize + oodle::kOverrun), Qt::Uninitialized);
    uint64_t pos = 0;
    QByteArray comp;
    for (uint32_t i = 0; i < m_hdr.blockCount; ++i) {
        if (!readCompressedBlock(i, comp, error)) return QByteArray();
        const uint64_t want = (i + 1 == m_hdr.blockCount) ? m_hdr.uncompressedSize - pos : m_hdr.granularity;
        const int n = oodle::decompress(reinterpret_cast<const uint8_t*>(comp.constData()), size_t(comp.size()),
                                        reinterpret_cast<uint8_t*>(out.data()) + pos, size_t(want));
        if (n != int(want)) {
            if (error) *error = QStringLiteral("%1: Oodle decode of block %2 returned %3, expected %4").arg(m_path).arg(i).arg(n).arg(want);
            return QByteArray();
        }
        pos += want;
    }
    out.resize(int(m_hdr.uncompressedSize));
    return out;
}

void Bundle::setCacheBlocks(int n)
{
    QMutexLocker lock(&m_mutex);
    m_cacheBlocks = qMax(0, n);
    if (m_cache.size() > m_cacheBlocks) m_cache.resize(m_cacheBlocks);
}

void Bundle::clearCache()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
}
