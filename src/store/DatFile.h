#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>

// A reader for PoE's ".datc64" data tables (data/balance/*.datc64) — the columnar tables that hold
// the game's authored strings, including item display names. Layout measured from the real files
// (tools/dat_probe.cpp) and identical to the community-documented format:
//   [u32 rowCount][fixed rows, rowCount × rowWidth][8-byte 0xBB… boundary][variable heap]
// Fields inside a row pack tightly (bool=1, i32=4, string ref=8, foreign key=16…), so column offsets
// are BYTE offsets, not 8-aligned. String/data references are u64 offsets measured from the boundary
// position; the heap bytes begin at boundary+8, so a ref of 8 points at the first heap byte. A
// foreign key / null reference is the sentinel 0xFEFEFEFEFEFEFEFE.
//
// The table SCHEMA (which column is which) is not in the file; this reader exposes raw typed access
// and the caller supplies measured/validated offsets (see store/NameIndex, where the BaseItemTypes →
// ItemVisualIdentity link column was found by self-validation, not a guessed schema).
class DatFile {
public:
    static constexpr uint64_t kNull = 0xFEFEFEFEFEFEFEFEULL;

    // Parse decompressed table bytes. False (and !isValid) if the 0xBB boundary is missing or the
    // fixed region does not divide evenly into rows.
    bool load(const QByteArray& bytes);
    bool isValid() const { return m_boundary >= 0 && m_rowCount > 0 && m_rowWidth > 0; }

    uint32_t rowCount() const { return m_rowCount; }
    long rowWidth() const { return m_rowWidth; }

    // Raw u64 at (row, byte-column). 0 if out of range.
    uint64_t u64(uint32_t row, long col) const;
    // The UTF-16 heap string a string-ref column points to. Empty if the ref is null/out of range.
    QString str(uint32_t row, long col) const;

    // An array column is (u64 count @col, u64 heapOffset @col+8); the heap holds `count` consecutive
    // u64 entries at heapOffset. arrayCount reads the count; arrayU64 reads element `k`; strFromArray
    // decodes element `k` as a string ref (arrays of strings). All heap offsets are boundary-relative.
    uint64_t arrayCount(uint32_t row, long col) const { return u64(row, col); }
    uint64_t arrayU64(uint64_t heapOffset, uint64_t k) const;
    QString  strFromArray(uint64_t heapOffset, uint64_t k) const { return strAtRef(arrayU64(heapOffset, k)); }

private:
    QString strAtRef(uint64_t ref) const;

    QByteArray m_bytes;
    const uint8_t* m_p = nullptr;
    long m_size = 0;
    long m_boundary = -1;
    long m_rowWidth = 0;
    uint32_t m_rowCount = 0;
};
