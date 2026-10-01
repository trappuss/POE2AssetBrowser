#pragma once
#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QVector>
#include <cstdint>
#include <memory>

// One *.bundle.bin (or _.index.bin): the fixed header, the block-size table and on-demand block
// decompression. Layout measured in docs/FORMATS.md §1.1.
//
// Files inside a bundle are addressed by (offset, size) into the UNCOMPRESSED payload, and never
// align to block boundaries, so readRange() decodes only the 256 KiB blocks it needs. A small
// per-bundle block cache keeps the blocks of the most recent reads: the Textures list opens every
// `.dds.header` in a Tiny bundle, thousands of small files that share a few hundred blocks.
class Bundle {
public:
    struct Header {
        uint32_t uncompressedSize = 0;
        uint32_t totalPayloadSize = 0;
        uint32_t headPayloadSize  = 0;
        uint32_t firstFileEncode  = 0;
        uint32_t blockCount       = 0;
        uint32_t granularity      = 0;
        QVector<uint32_t> blockSizes;
        QVector<uint64_t> blockOffsets;   // absolute file offset of each block's first byte
    };

    // Open from disk. `error` receives a reason on failure. Keeps the file open (QFile) for
    // random access; safe to share between threads (every read takes the mutex).
    static std::shared_ptr<Bundle> open(const QString& filePath, QString* error = nullptr);
    // Parse a bundle held in memory (the nested path-spec bundle inside the index, the animation
    // blob at the tail of an .ast).
    static std::shared_ptr<Bundle> fromMemory(const QByteArray& bytes, QString* error = nullptr);

    const Header& header() const { return m_hdr; }
    const QString& path() const { return m_path; }
    uint64_t uncompressedSize() const { return m_hdr.uncompressedSize; }

    // Decompress [offset, offset+size) of the payload. Empty on failure (with `error` set).
    QByteArray readRange(uint64_t offset, uint64_t size, QString* error = nullptr);
    // Decompress everything (the index).
    QByteArray readAll(QString* error = nullptr);

    // Cache control: number of decoded blocks kept per bundle (default 16 = 4 MiB).
    void setCacheBlocks(int n);
    void clearCache();

private:
    Bundle() = default;
    bool parseHeader(QString* error);
    bool readCompressedBlock(uint32_t idx, QByteArray& out, QString* error);
    QByteArray decodeBlock(uint32_t idx, QString* error);   // cached

    QString    m_path;
    QByteArray m_memory;        // when fromMemory()
    std::unique_ptr<class QFile> m_file;
    Header     m_hdr;
    QMutex     m_mutex;
    struct CachedBlock { uint32_t idx = UINT32_MAX; QByteArray data; uint64_t stamp = 0; };
    QVector<CachedBlock> m_cache;
    uint64_t   m_stamp = 0;
    int        m_cacheBlocks = 16;
};

// Parse just the header fields of a bundle byte range (no decoding). Used by the health check.
bool parseBundleHeader(const uint8_t* data, size_t len, Bundle::Header& out, QString* error);
