#pragma once
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// The parsed `_.index.bin`: every bundle, every file record and every file's path, rebuilt from
// the index's own path-specification bundle (docs/FORMATS.md §2). Built once per game build and
// cached in data/cache/bundle_index_v<N>.bin, keyed on the index file's size + mtime.
//
// Memory: paths are held as a directory table + a name arena, not 4.26 million QStrings. Records
// under the five shader-cache roots (2.86 M files of hash-named blobs) are counted and dropped —
// a browser lists assets, and those are not assets. The count is reported so "present but not
// listed" stays a different answer from "absent".
class BundleIndex {
public:
    struct BundleRecord {
        QString  name;               // e.g. "Tiny.V4" or "Streaming/Content/art/models/.../<hash>.foo.fmt"
        uint32_t uncompressedSize = 0;
    };
    struct FileRecord {
        uint64_t hash = 0;           // MurmurHash64A of the lowercase path
        uint32_t bundle = 0;         // index into bundles()
        uint32_t offset = 0;         // into the bundle's uncompressed payload
        uint32_t size = 0;
        uint32_t dir = 0;            // index into the directory table (0 = "(unnamed)")
        uint32_t nameOff = 0;        // into the name arena
        uint16_t nameLen = 0;
        uint16_t extId = 0;          // index into extensions() ("" for none)
    };
    struct SkippedRoot { QString root; uint32_t files = 0; };

    static constexpr uint32_t kCacheVersion = 1;

    // Hash exactly as the game does: MurmurHash64A(seed 0x1337B33F) of the UTF-8, lowercased path
    // with any trailing '/' removed. Works for files and directories.
    static uint64_t hashPath(const QString& path);
    static uint64_t hashUtf8Lower(const char* data, size_t len);

    // Parse (or load from cache) the index at <bundlesDir>/_.index.bin. `progress` receives short
    // status strings; `cacheDir` may be empty to disable caching. Errors are returned, never thrown.
    bool load(const QString& bundlesDir, const QString& cacheDir, QString* error,
              const std::function<void(const QString&)>& progress = {});

    bool isLoaded() const { return !m_files.empty(); }
    const QString& bundlesDir() const { return m_bundlesDir; }
    // "size:mtime" of _.index.bin — the data fingerprint every dependent cache keys on.
    const QString& fingerprint() const { return m_fingerprint; }

    const std::vector<BundleRecord>& bundles() const { return m_bundles; }
    const std::vector<FileRecord>& files() const { return m_files; }
    const std::vector<std::string>& directories() const { return m_dirs; }   // lowercase, no trailing slash
    const QStringList& extensions() const { return m_exts; }                // lowercase, with the dot

    // Lookup by game path (any case, '/' separators). nullptr when absent. `index` receives the
    // position in files() when non-null.
    const FileRecord* find(const QString& path, uint32_t* index = nullptr) const;
    QString pathOf(uint32_t fileIndex) const;
    QString nameOf(uint32_t fileIndex) const;
    QString dirOf(uint32_t fileIndex) const;
    std::string_view nameView(uint32_t fileIndex) const;

    // Statistics the Health check reports.
    uint64_t totalRecordsInIndex() const { return m_totalRecords; }
    const QVector<SkippedRoot>& skippedRoots() const { return m_skipped; }
    uint32_t unnamedFiles() const { return m_unnamed; }
    uint32_t sharedPayloadFiles() const { return m_sharedPayloads; }
    bool loadedFromCache() const { return m_fromCache; }

private:
    bool parseIndexPayload(const QByteArray& raw, QString* error, const std::function<void(const QString&)>& progress);
    bool generatePaths(const QByteArray& spec, const std::vector<std::pair<uint32_t,uint32_t>>& reps, QString* error);
    bool readCache(const QString& file, QString* why);
    bool writeCache(const QString& file, QString* why) const;
    uint32_t dirId(const std::string& dir);
    uint16_t extId(const std::string& name);

    QString m_bundlesDir, m_fingerprint;
    std::vector<BundleRecord> m_bundles;
    std::vector<FileRecord> m_files;
    std::vector<std::string> m_dirs;
    std::unordered_map<std::string, uint32_t> m_dirIds;
    QStringList m_exts;
    std::unordered_map<std::string, uint16_t> m_extIds;
    std::string m_arena;
    std::unordered_map<uint64_t, uint32_t> m_byHash;
    QVector<SkippedRoot> m_skipped;
    uint64_t m_totalRecords = 0;
    uint32_t m_unnamed = 0, m_sharedPayloads = 0;
    bool m_fromCache = false;
};
