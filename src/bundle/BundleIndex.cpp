#include "bundle/BundleIndex.h"
#include "bundle/Bundle.h"

#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <algorithm>
#include <cstring>

namespace {
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
uint64_t rd64(const uint8_t* p) { uint64_t v; std::memcpy(&v, p, 8); return v; }

// Roots whose files are dropped from the listing (counted, never indexed). Matched on the first
// path component of the GENERATED path, which is authored data, not a guess from a file name.
bool isShaderCacheRoot(const std::string& firstComponent)
{
    return firstComponent.rfind("shadercache", 0) == 0;   // shadercached3d11/12/_xs, vulkan, agc
}
}

// ── Hashing ─────────────────────────────────────────────────────────────────────────────────────
uint64_t BundleIndex::hashUtf8Lower(const char* data, size_t len)
{
    // MurmurHash64A, seed 0x1337B33F. Input must already be lowercase; a trailing '/' is dropped.
    if (len && data[len - 1] == '/') --len;
    const uint64_t m = 0xC6A4A7935BD1E995ull;
    const int r = 47;
    uint64_t h = 0x1337B33Full ^ (uint64_t(len) * m);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
    const size_t nblocks = len / 8;
    for (size_t i = 0; i < nblocks; ++i) {
        uint64_t k = rd64(p + 8 * i);
        k *= m; k ^= k >> r; k *= m;
        h ^= k; h *= m;
    }
    const size_t rem = len & 7;
    if (rem) {
        uint64_t tail = 0;
        std::memcpy(&tail, p + 8 * nblocks, rem);   // little-endian: the same as the reference switch
        h ^= tail; h *= m;
    }
    h ^= h >> r; h *= m; h ^= h >> r;
    return h;
}

uint64_t BundleIndex::hashPath(const QString& path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QByteArray u = p.toLower().toUtf8();
    return hashUtf8Lower(u.constData(), size_t(u.size()));
}

// ── Directory / extension tables ────────────────────────────────────────────────────────────────
uint32_t BundleIndex::dirId(const std::string& dir)
{
    auto it = m_dirIds.find(dir);
    if (it != m_dirIds.end()) return it->second;
    const uint32_t id = uint32_t(m_dirs.size());
    m_dirs.push_back(dir);
    m_dirIds.emplace(dir, id);
    return id;
}

uint16_t BundleIndex::extId(const std::string& name)
{
    const size_t dot = name.rfind('.');
    std::string ext = (dot == std::string::npos) ? std::string() : name.substr(dot);
    if (ext.size() > 16) ext.clear();   // "foo.somethingverylong" is not an extension
    auto it = m_extIds.find(ext);
    if (it != m_extIds.end()) return it->second;
    const uint16_t id = uint16_t(m_exts.size());
    m_exts.append(QString::fromStdString(ext));
    m_extIds.emplace(ext, id);
    return id;
}

// ── Loading ─────────────────────────────────────────────────────────────────────────────────────
bool BundleIndex::load(const QString& bundlesDir, const QString& cacheDir, QString* error,
                       const std::function<void(const QString&)>& progress)
{
    auto say = [&](const QString& s) { if (progress) progress(s); };
    *this = BundleIndex();
    m_bundlesDir = bundlesDir;
    const QString indexPath = QDir(bundlesDir).filePath(QStringLiteral("_.index.bin"));
    const QFileInfo fi(indexPath);
    if (!fi.exists()) { if (error) *error = QStringLiteral("no _.index.bin in %1").arg(bundlesDir); return false; }
    m_fingerprint = QStringLiteral("%1:%2").arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());

    const QString cacheFile = cacheDir.isEmpty() ? QString()
        : QDir(cacheDir).filePath(QStringLiteral("bundle_index_v%1.bin").arg(kCacheVersion));
    if (!cacheFile.isEmpty()) {
        QString why;
        if (readCache(cacheFile, &why)) { m_fromCache = true; say(QStringLiteral("index: loaded from cache")); return true; }
        if (!why.isEmpty()) say(QStringLiteral("index cache not used: %1").arg(why));
    }

    QElapsedTimer t; t.start();
    say(QStringLiteral("index: decompressing _.index.bin"));
    QString err;
    auto b = Bundle::open(indexPath, &err);
    if (!b) { if (error) *error = err; return false; }
    const QByteArray raw = b->readAll(&err);
    if (raw.isEmpty()) { if (error) *error = err; return false; }
    if (!parseIndexPayload(raw, error, progress)) return false;
    say(QStringLiteral("index: %1 files in %2 bundles (%3 ms)").arg(m_files.size()).arg(m_bundles.size()).arg(t.elapsed()));

    if (!cacheFile.isEmpty()) {
        QString why;
        if (!writeCache(cacheFile, &why)) say(QStringLiteral("index cache not written: %1").arg(why));
    }
    return true;
}

bool BundleIndex::parseIndexPayload(const QByteArray& raw, QString* error, const std::function<void(const QString&)>& progress)
{
    const uint8_t* d = reinterpret_cast<const uint8_t*>(raw.constData());
    const size_t n = size_t(raw.size());
    size_t p = 0;
    auto need = [&](size_t k) { return p + k <= n; };
    auto fail = [&](const QString& what) { if (error) *error = QStringLiteral("index payload: %1 at offset %2").arg(what).arg(p); return false; };

    if (!need(4)) return fail(QStringLiteral("truncated before bundle count"));
    const uint32_t bundleCount = rd32(d + p); p += 4;
    if (bundleCount > 10'000'000) return fail(QStringLiteral("absurd bundle count"));
    m_bundles.resize(bundleCount);
    for (uint32_t i = 0; i < bundleCount; ++i) {
        if (!need(4)) return fail(QStringLiteral("truncated in bundle table"));
        const uint32_t len = rd32(d + p); p += 4;
        if (len > 4096 || !need(len + 4)) return fail(QStringLiteral("bad bundle name length"));
        m_bundles[i].name = QString::fromUtf8(reinterpret_cast<const char*>(d + p), int(len)); p += len;
        m_bundles[i].uncompressedSize = rd32(d + p); p += 4;
    }

    if (!need(4)) return fail(QStringLiteral("truncated before file count"));
    const uint32_t fileCount = rd32(d + p); p += 4;
    if (!need(size_t(fileCount) * 20)) return fail(QStringLiteral("truncated in file table"));
    m_totalRecords = fileCount;
    struct Tmp { uint64_t hash; uint32_t bundle, offset, size; uint32_t out; };   // out: final index or UINT32_MAX
    std::vector<Tmp> tmp(fileCount);
    for (uint32_t i = 0; i < fileCount; ++i) {
        tmp[i].hash = rd64(d + p); tmp[i].bundle = rd32(d + p + 8); tmp[i].offset = rd32(d + p + 12); tmp[i].size = rd32(d + p + 16);
        tmp[i].out = UINT32_MAX;
        if (tmp[i].bundle >= bundleCount) return fail(QStringLiteral("file %1 points at bundle %2 of %3").arg(i).arg(tmp[i].bundle).arg(bundleCount));
        p += 20;
    }
    // Shared payloads: records that alias the same (bundle, offset, size).
    {
        std::vector<uint32_t> order(fileCount);
        for (uint32_t i = 0; i < fileCount; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            const Tmp &x = tmp[a], &y = tmp[b];
            return std::tie(x.bundle, x.offset, x.size) < std::tie(y.bundle, y.offset, y.size);
        });
        for (uint32_t i = 1; i < fileCount; ++i) {
            const Tmp &x = tmp[order[i - 1]], &y = tmp[order[i]];
            if (x.bundle == y.bundle && x.offset == y.offset && x.size == y.size) ++m_sharedPayloads;
        }
    }

    if (!need(4)) return fail(QStringLiteral("truncated before directory count"));
    const uint32_t dirCount = rd32(d + p); p += 4;
    if (!need(size_t(dirCount) * 20)) return fail(QStringLiteral("truncated in directory table"));
    std::vector<std::pair<uint32_t, uint32_t>> reps(dirCount);
    for (uint32_t i = 0; i < dirCount; ++i) {
        reps[i] = { rd32(d + p + 8), rd32(d + p + 12) };   // spec offset, spec size (recursive size unused)
        p += 20;
    }
    if (progress) progress(QStringLiteral("index: decompressing the path table"));
    QString err;
    auto nested = Bundle::fromMemory(raw.mid(int(p)), &err);
    if (!nested) { if (error) *error = QStringLiteral("index path bundle: %1").arg(err); return false; }
    const QByteArray spec = nested->readAll(&err);
    if (spec.isEmpty()) { if (error) *error = QStringLiteral("index path bundle: %1").arg(err); return false; }

    if (progress) progress(QStringLiteral("index: generating %1 paths").arg(fileCount));
    // Sort the temp records by hash for binary search during generation.
    std::vector<uint32_t> byHash(fileCount);
    for (uint32_t i = 0; i < fileCount; ++i) byHash[i] = i;
    std::sort(byHash.begin(), byHash.end(), [&](uint32_t a, uint32_t b) { return tmp[a].hash < tmp[b].hash; });
    auto lookup = [&](uint64_t h) -> int64_t {
        auto it = std::lower_bound(byHash.begin(), byHash.end(), h, [&](uint32_t idx, uint64_t v) { return tmp[idx].hash < v; });
        if (it == byHash.end() || tmp[*it].hash != h) return -1;
        return int64_t(*it);
    };

    m_dirs.clear(); m_dirIds.clear();
    dirId(std::string("(unnamed)"));                 // id 0
    m_arena.reserve(size_t(fileCount) * 8);
    m_files.reserve(size_t(fileCount) / 3 + 1024);   // ~1.4 M survive the shader-cache drop
    std::unordered_map<std::string, uint32_t> skippedByRoot;

    const uint8_t* s = reinterpret_cast<const uint8_t*>(spec.constData());
    const size_t sn = size_t(spec.size());
    std::vector<std::string> base;
    std::string path;
    uint32_t unmatched = 0;
    for (uint32_t di = 0; di < dirCount; ++di) {
        const size_t off = reps[di].first, sz = reps[di].second;
        if (off > sn || sz > sn - off) return fail(QStringLiteral("directory %1 spec out of range").arg(di));
        size_t q = off; const size_t end = off + sz;
        base.clear();
        bool basePhase = false;
        while (q + 4 <= end) {
            const uint32_t idx = rd32(s + q); q += 4;
            if (idx == 0) { basePhase = !basePhase; if (basePhase) base.clear(); continue; }
            // null-terminated string, bounded by the spec end
            const uint8_t* z = static_cast<const uint8_t*>(std::memchr(s + q, 0, end - q));
            if (!z) return fail(QStringLiteral("unterminated string in directory %1").arg(di));
            const size_t len = size_t(z - (s + q));
            if (idx - 1 < base.size()) { path = base[idx - 1]; path.append(reinterpret_cast<const char*>(s + q), len); }
            else                        path.assign(reinterpret_cast<const char*>(s + q), len);
            q += len + 1;
            if (basePhase) { base.push_back(path); continue; }
            // A generated file path. Everything is lowercase already (3.21.2+ scheme), but hash the
            // lowercased bytes anyway so a future mixed-case index still matches.
            std::string lower = path;
            for (char& c : lower) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            const int64_t ti = lookup(hashUtf8Lower(lower.data(), lower.size()));
            if (ti < 0) { ++unmatched; continue; }
            Tmp& rec = tmp[size_t(ti)];
            if (rec.out != UINT32_MAX) continue;   // duplicate generation of the same record
            const size_t slash = lower.rfind('/');
            const std::string dir = (slash == std::string::npos) ? std::string() : lower.substr(0, slash);
            const std::string name = (slash == std::string::npos) ? lower : lower.substr(slash + 1);
            const size_t firstSlash = lower.find('/');
            const std::string root = lower.substr(0, firstSlash == std::string::npos ? lower.size() : firstSlash);
            if (isShaderCacheRoot(root)) { ++skippedByRoot[root]; rec.out = UINT32_MAX - 1; continue; }
            FileRecord fr;
            fr.hash = rec.hash; fr.bundle = rec.bundle; fr.offset = rec.offset; fr.size = rec.size;
            fr.dir = dirId(dir);
            fr.nameOff = uint32_t(m_arena.size());
            fr.nameLen = uint16_t(std::min<size_t>(name.size(), 65535));
            fr.extId = extId(name);
            m_arena.append(name);
            rec.out = uint32_t(m_files.size());
            m_files.push_back(fr);
        }
    }
    // Records that never received a path: keep them, unnamed, so the count of what the game ships
    // is honest ("present · unnamed · absent" are three different answers).
    for (const Tmp& rec : tmp) {
        if (rec.out != UINT32_MAX) continue;
        FileRecord fr;
        fr.hash = rec.hash; fr.bundle = rec.bundle; fr.offset = rec.offset; fr.size = rec.size;
        fr.dir = 0; fr.nameOff = uint32_t(m_arena.size()); fr.nameLen = 0; fr.extId = extId(std::string());
        m_files.push_back(fr);
        ++m_unnamed;
    }
    Q_UNUSED(unmatched);
    m_byHash.reserve(m_files.size() * 2);
    for (uint32_t i = 0; i < m_files.size(); ++i) m_byHash.emplace(m_files[i].hash, i);
    for (const auto& kv : skippedByRoot) m_skipped.append(SkippedRoot{ QString::fromStdString(kv.first), kv.second });
    std::sort(m_skipped.begin(), m_skipped.end(), [](const SkippedRoot& a, const SkippedRoot& b) { return a.root < b.root; });
    m_arena.shrink_to_fit();
    return true;
}

// ── Lookup ──────────────────────────────────────────────────────────────────────────────────────
const BundleIndex::FileRecord* BundleIndex::find(const QString& path, uint32_t* index) const
{
    auto it = m_byHash.find(hashPath(path));
    if (it == m_byHash.end()) return nullptr;
    if (index) *index = it->second;
    return &m_files[it->second];
}

std::string_view BundleIndex::nameView(uint32_t i) const
{
    const FileRecord& f = m_files[i];
    return std::string_view(m_arena.data() + f.nameOff, f.nameLen);
}

QString BundleIndex::nameOf(uint32_t i) const
{
    const auto v = nameView(i);
    return QString::fromUtf8(v.data(), int(v.size()));
}

QString BundleIndex::dirOf(uint32_t i) const
{
    return QString::fromStdString(m_dirs[m_files[i].dir]);
}

QString BundleIndex::pathOf(uint32_t i) const
{
    const FileRecord& f = m_files[i];
    if (f.nameLen == 0) return QStringLiteral("(unnamed %1)").arg(f.hash, 16, 16, QLatin1Char('0'));
    const std::string& dir = m_dirs[f.dir];
    if (dir.empty() || f.dir == 0) return nameOf(i);
    return QString::fromStdString(dir) + QLatin1Char('/') + nameOf(i);
}

// ── Cache ───────────────────────────────────────────────────────────────────────────────────────
namespace {
constexpr quint32 kMagic = 0x58493250;   // "P2IX"
}

bool BundleIndex::writeCache(const QString& file, QString* why) const
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) { if (why) *why = f.errorString(); return false; }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << kMagic << kCacheVersion << m_fingerprint;
    ds << quint32(m_bundles.size());
    for (const BundleRecord& b : m_bundles) ds << b.name << quint32(b.uncompressedSize);
    ds << quint32(m_dirs.size());
    for (const std::string& d : m_dirs) ds << QByteArray::fromRawData(d.data(), int(d.size()));
    ds << m_exts;
    ds << quint64(m_arena.size());
    ds.writeRawData(m_arena.data(), int(m_arena.size()));
    ds << quint32(m_files.size());
    ds.writeRawData(reinterpret_cast<const char*>(m_files.data()), int(m_files.size() * sizeof(FileRecord)));
    ds << quint64(m_totalRecords) << quint32(m_unnamed) << quint32(m_sharedPayloads);
    ds << quint32(m_skipped.size());
    for (const SkippedRoot& s : m_skipped) ds << s.root << quint32(s.files);
    ds << kMagic;   // trailer: a truncated file cannot pass
    if (ds.status() != QDataStream::Ok) { if (why) *why = QStringLiteral("stream error"); return false; }
    if (!f.commit()) { if (why) *why = f.errorString(); return false; }
    return true;
}

bool BundleIndex::readCache(const QString& file, QString* why)
{
    QFile f(file);
    if (!f.exists()) { if (why) why->clear(); return false; }
    if (!f.open(QIODevice::ReadOnly)) { if (why) *why = f.errorString(); return false; }
    QDataStream ds(&f);
    ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, ver = 0; QString fp;
    ds >> magic >> ver >> fp;
    if (magic != kMagic || ver != kCacheVersion) { if (why) *why = QStringLiteral("different cache version"); return false; }
    if (fp != m_fingerprint) { if (why) *why = QStringLiteral("game index changed (%1 → %2)").arg(fp, m_fingerprint); return false; }
    quint32 nb = 0; ds >> nb;
    if (nb > 10'000'000) return false;
    m_bundles.resize(nb);
    for (quint32 i = 0; i < nb; ++i) { quint32 sz; ds >> m_bundles[i].name >> sz; m_bundles[i].uncompressedSize = sz; }
    quint32 nd = 0; ds >> nd;
    if (nd > 50'000'000) return false;
    m_dirs.resize(nd); m_dirIds.clear();
    for (quint32 i = 0; i < nd; ++i) { QByteArray b; ds >> b; m_dirs[i] = b.toStdString(); m_dirIds.emplace(m_dirs[i], i); }
    ds >> m_exts;
    m_extIds.clear();
    for (int i = 0; i < m_exts.size(); ++i) m_extIds.emplace(m_exts[i].toStdString(), uint16_t(i));
    quint64 arenaLen = 0; ds >> arenaLen;
    if (arenaLen > (2ull << 30)) return false;
    m_arena.resize(size_t(arenaLen));
    if (ds.readRawData(m_arena.data(), int(arenaLen)) != int(arenaLen)) { if (why) *why = QStringLiteral("truncated arena"); return false; }
    quint32 nf = 0; ds >> nf;
    if (nf > 100'000'000) return false;
    m_files.resize(nf);
    const int bytes = int(size_t(nf) * sizeof(FileRecord));
    if (ds.readRawData(reinterpret_cast<char*>(m_files.data()), bytes) != bytes) { if (why) *why = QStringLiteral("truncated file table"); return false; }
    quint64 total = 0; quint32 unnamed = 0, shared = 0; ds >> total >> unnamed >> shared;
    m_totalRecords = total; m_unnamed = unnamed; m_sharedPayloads = shared;
    quint32 ns = 0; ds >> ns;
    if (ns > 1000) return false;
    m_skipped.clear();
    for (quint32 i = 0; i < ns; ++i) { SkippedRoot s; quint32 c; ds >> s.root >> c; s.files = c; m_skipped.append(s); }
    quint32 trailer = 0; ds >> trailer;
    if (trailer != kMagic || ds.status() != QDataStream::Ok) { if (why) *why = QStringLiteral("cache file truncated"); m_files.clear(); return false; }
    // Validate what we will index by: every record's bundle/dir/name must be in range.
    for (const FileRecord& fr : m_files) {
        if (fr.bundle >= m_bundles.size() || fr.dir >= m_dirs.size() || size_t(fr.nameOff) + fr.nameLen > m_arena.size() || fr.extId >= m_exts.size()) {
            if (why) *why = QStringLiteral("cache record out of range"); m_files.clear(); return false;
        }
    }
    m_byHash.clear(); m_byHash.reserve(m_files.size() * 2);
    for (uint32_t i = 0; i < m_files.size(); ++i) m_byHash.emplace(m_files[i].hash, i);
    return true;
}
