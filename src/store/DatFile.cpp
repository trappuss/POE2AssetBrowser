#include "store/DatFile.h"
#include <cstring>

bool DatFile::load(const QByteArray& bytes)
{
    m_bytes = bytes;
    m_p = reinterpret_cast<const uint8_t*>(m_bytes.constData());
    m_size = m_bytes.size();
    m_boundary = -1; m_rowWidth = 0; m_rowCount = 0;
    if (m_size < 12) return false;

    std::memcpy(&m_rowCount, m_p, 4);
    // Find the 8-byte 0xBB… boundary that separates the fixed rows from the heap.
    for (long i = 4; i + 8 <= m_size; ++i) {
        bool all = true;
        for (int k = 0; k < 8; ++k) if (m_p[i + k] != 0xBB) { all = false; break; }
        if (all) { m_boundary = i; break; }
    }
    if (m_boundary < 0 || m_rowCount == 0) return false;
    const long fixedBytes = m_boundary - 4;
    if (fixedBytes % m_rowCount != 0) { m_boundary = -1; return false; }
    m_rowWidth = fixedBytes / m_rowCount;
    return m_rowWidth > 0;
}

uint64_t DatFile::u64(uint32_t row, long col) const
{
    if (!isValid() || row >= m_rowCount || col < 0 || col + 8 > m_rowWidth) return 0;
    uint64_t v; std::memcpy(&v, m_p + 4 + long(row) * m_rowWidth + col, 8);
    return v;
}

uint64_t DatFile::arrayU64(uint64_t heapOffset, uint64_t k) const
{
    if (!isValid()) return 0;
    const long p = m_boundary + long(heapOffset) + long(k) * 8;
    if (p < m_boundary + 8 || p + 8 > m_size) return 0;
    uint64_t v; std::memcpy(&v, m_p + p, 8);
    return v;
}

QString DatFile::strAtRef(uint64_t ref) const
{
    if (ref == kNull) return QString();
    const long s0 = m_boundary + long(ref);
    if (s0 < m_boundary + 8 || s0 + 2 > m_size) return QString();
    QString out;
    long s = s0;
    while (s + 2 <= m_size) {
        char16_t c; std::memcpy(&c, m_p + s, 2); s += 2;
        if (c == 0) break;
        out.append(QChar(c));
        if (out.size() > 512) break;   // guard against a bad ref running off into the heap
    }
    return out;
}

QString DatFile::str(uint32_t row, long col) const
{
    return strAtRef(u64(row, col));
}
