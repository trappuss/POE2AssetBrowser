#include "store/NameIndex.h"
#include "store/AssetStore.h"
#include "store/DatFile.h"
#include "bundle/BundleIndex.h"
#include "model/AssetText.h"

#include <QDataStream>
#include <QDir>
#include <QFile>

namespace {
constexpr quint32 kMagic = 0x4e414d45;   // 'NAME'
// Measured/validated BaseItemTypes + ItemVisualIdentity column byte-offsets (see NameIndex.h).
constexpr long kBIT_Name = 32;
constexpr long kBIT_IviFk = 124;
constexpr long kIVI_DDSFile = 8;    // ItemVisualIdentity 2D icon (.dds) — proven in tools/dat_probe.cpp
constexpr long kIVI_AOFile = 16;
// MonsterVarieties (validated in tools/dat_cols.cpp): Name at byte 272; the model .ao is an ARRAY
// column at byte 56 (u64 count @56, u64 heap offset @64).
constexpr long kMV_Name = 272;
constexpr long kMV_AoArr = 56;
constexpr long kMV_Id = 0;
// NPCs (validated in tools/npc_link.cpp): Id at byte 0, display Name at byte 8. An NPC's Id is a
// MonsterVariety Id (string match), so the NPC's model is that variety's AO — no FK guessing.
constexpr long kNPC_Id = 0;
constexpr long kNPC_Name = 8;
// Rune/placeholder prefixes that mark a VARIANT of a base item sharing its mesh — stripped to get
// the canonical base name. Kept in the search set so a variant name still finds the model.
const char* const kVariantPrefixes[] = { "Runeforged ", "Runemastered ", "Runesmithed ", "[DNT] ", "(DNT) " };
// Pure placeholder / controller monster names that reuse real bodies — skipped so they don't stamp a
// real mesh (e.g. a swamp-vine body) with "Daemon"/"Invisible".
bool isPlaceholderName(const QString& n)
{
    static const char* const kSkip[] = { "Daemon", "Invisible", "Clone", "[ANY MONSTER]", "Dummy", "Basic Monster" };
    for (const char* s : kSkip) if (n.compare(QString::fromLatin1(s), Qt::CaseInsensitive) == 0) return true;
    return false;
}
}

namespace {
// Reduce to lowercase alphanumerics for stem matching ("Rusted Cuirass" → "rustedcuirass").
QString alnumLower(const QString& s)
{
    QString o; o.reserve(s.size());
    for (const QChar c : s) if (c.isLetterOrNumber()) o.append(c.toLower());
    return o;
}
}

// Choose the canonical base name for a mesh that several base types (plus rune variants) can share.
// The authored model FILENAME is the tie-breaker: the base whose de-spaced name is contained in the
// model's own path is the item that mesh actually belongs to (rustedcuirass_drop → "Rusted Cuirass",
// not the generic "Garment" that merely reuses the mesh). Falls back to the shortest plain base.
QString NameIndex::canonicalOf(const QStringList& names, const QString& modelPath)
{
    const QString stem = alnumLower(modelPath.section(QLatin1Char('/'), -1));
    QString byStem, byShort;
    for (const QString& raw : names) {
        QString s = raw.trimmed();
        bool changed = true;
        while (changed) {                       // strip any stacked variant / dev-tag prefixes
            changed = false;
            for (const char* pfx : kVariantPrefixes) {
                const QString p = QString::fromLatin1(pfx);
                if (s.startsWith(p, Qt::CaseInsensitive)) { s = s.mid(p.size()).trimmed(); changed = true; }
            }
            // A leading bracketed dev tag, e.g. "[DNT-UNUSED]Water Ball" or "(DNT) X".
            if (s.startsWith(QLatin1Char('[')) || s.startsWith(QLatin1Char('('))) {
                const QChar close = s.startsWith(QLatin1Char('[')) ? QLatin1Char(']') : QLatin1Char(')');
                const int e = s.indexOf(close);
                if (e > 0 && s.left(e).contains(QStringLiteral("DNT"), Qt::CaseInsensitive)) { s = s.mid(e + 1).trimmed(); changed = true; }
            }
        }
        if (s.isEmpty()) continue;
        if (byShort.isEmpty() || s.size() < byShort.size()) byShort = s;
        const QString key = alnumLower(s);
        // Prefer a name the model filename actually carries; among those, the longest (most specific).
        if (!key.isEmpty() && stem.contains(key) && key.size() > alnumLower(byStem).size()) byStem = s;
    }
    return !byStem.isEmpty() ? byStem : byShort;
}

bool NameIndex::build(AssetStore& store, const std::function<void(const QString&)>& progress)
{
    if (!store.isOpen()) return false;
    const auto say = [&](const QString& s) { if (progress) progress(s); };

    // Collect every (model .ao → display name) pair from the data tables. Each browsable mesh is
    // reached from an .ao (item visual or monster AO), so keying by .ao unifies items and monsters.
    QHash<QString, QStringList> aoToNames;
    // Item 2D icons (.dds) are a browsable asset in their own right (the Textures tab), named the
    // same way but with no chain — ItemVisualIdentity.DDSFile IS the icon file.
    QHash<QString, QStringList> ddsToNames;

    // ── Items: BaseItemTypes.Name ─FK→ ItemVisualIdentity (AOFile model + DDSFile icon). ──
    DatFile bit, ivi;
    if (bit.load(store.readFile(QStringLiteral("data/balance/baseitemtypes.datc64"), nullptr)) && bit.isValid()
     && ivi.load(store.readFile(QStringLiteral("data/balance/itemvisualidentity.datc64"), nullptr)) && ivi.isValid()) {
        int n = 0, ni = 0;
        for (uint32_t r = 0; r < bit.rowCount(); ++r) {
            const uint64_t fk = bit.u64(r, kBIT_IviFk);
            if (fk >= ivi.rowCount()) continue;
            const QString name = bit.str(r, kBIT_Name);
            if (name.isEmpty()) continue;
            const QString ao = ivi.str(uint32_t(fk), kIVI_AOFile);
            if (!ao.isEmpty()) { aoToNames[ao.toLower()].append(name); ++n; }
            const QString dds = ivi.str(uint32_t(fk), kIVI_DDSFile);
            if (!dds.isEmpty()) { ddsToNames[dds.toLower()].append(name); ++ni; }
        }
        say(QStringLiteral("names: BaseItemTypes=%1 rows → %2 model + %3 icon links").arg(bit.rowCount()).arg(n).arg(ni));
    } else {
        say(QStringLiteral("names: item tables not available"));
    }

    // ── Monsters: MonsterVarieties.Name + its AO array (col@56 = count, col@64 = heap offset →
    //    the monster's model .ao list). Column offsets measured/validated (tools/dat_cols.cpp). ──
    DatFile mv;
    if (mv.load(store.readFile(QStringLiteral("data/balance/monstervarieties.datc64"), nullptr)) && mv.isValid()) {
        int n = 0;
        for (uint32_t r = 0; r < mv.rowCount(); ++r) {
            const QString name = mv.str(r, kMV_Name);
            if (name.isEmpty() || isPlaceholderName(name)) continue;
            const uint64_t cnt = mv.arrayCount(r, kMV_AoArr);
            const uint64_t off = mv.u64(r, kMV_AoArr + 8);
            for (uint64_t k = 0; k < cnt && k < 4; ++k) {      // primary + a few LOD/variant .ao
                const QString ao = mv.strFromArray(off, k);
                if (ao.toLower().endsWith(QStringLiteral(".ao"))) { aoToNames[ao.toLower()].append(name); ++n; }
            }
        }
        say(QStringLiteral("names: MonsterVarieties=%1 rows → %2 monster links").arg(mv.rowCount()).arg(n));
    } else {
        say(QStringLiteral("names: monstervarieties.datc64 not available"));
    }

    // ── NPCs: an NPC's Id (col@0) is a MonsterVariety Id, so its model is that variety's AO (matched
    //    by Id string, no FK). The NPC's col@8 is the proper character name (e.g. "Einhar,
    //    Beastmaster"). Validated in tools/npc_link.cpp. ──
    if (mv.isValid()) {
        DatFile npc;
        if (npc.load(store.readFile(QStringLiteral("data/balance/npcs.datc64"), nullptr)) && npc.isValid()) {
            QHash<QString, uint32_t> mvById;
            for (uint32_t r = 0; r < mv.rowCount(); ++r) { const QString id = mv.str(r, kMV_Id); if (!id.isEmpty()) mvById.insert(id.toLower(), r); }
            int n = 0;
            for (uint32_t r = 0; r < npc.rowCount(); ++r) {
                const QString name = npc.str(r, kNPC_Name);
                if (name.isEmpty() || isPlaceholderName(name)) continue;
                auto it = mvById.find(npc.str(r, kNPC_Id).toLower());
                if (it == mvById.end()) continue;
                const uint32_t m = it.value();
                const uint64_t cnt = mv.arrayCount(m, kMV_AoArr);
                const uint64_t off = mv.u64(m, kMV_AoArr + 8);
                for (uint64_t k = 0; k < cnt && k < 4; ++k) {
                    const QString ao = mv.strFromArray(off, k);
                    if (ao.toLower().endsWith(QStringLiteral(".ao"))) { aoToNames[ao.toLower()].append(name); ++n; }
                }
            }
            say(QStringLiteral("names: NPCs=%1 rows → %2 npc links").arg(npc.rowCount()).arg(n));
        }
    }

    if (aoToNames.isEmpty() && ddsToNames.isEmpty()) { say(QStringLiteral("names: no data tables available — names disabled")); return false; }
    say(QStringLiteral("names: %1 model visuals + %2 icons to resolve").arg(aoToNames.size()).arg(ddsToNames.size()));

    // Record a (browsable asset path → names) mapping: canonical primary + a searchable set.
    const auto record = [&](const QString& path, const QStringList& names) {
        const QString canonical = canonicalOf(names, path);
        if (canonical.isEmpty()) return false;
        const quint64 h = BundleIndex::hashPath(path);
        m_primary.insert(h, canonical);
        QStringList uniq; for (const QString& n : names) if (!uniq.contains(n, Qt::CaseInsensitive)) uniq << n;
        m_search.insert(h, uniq.join(QLatin1Char(' ')).toLower());
        return true;
    };

    // ── Resolve each .ao → .sm → .smd (the browsed mesh) and record the model→name mapping. ──
    // A per-.sm cache avoids re-parsing a descriptor shared by several .ao.
    int resolved = 0, unresolved = 0, nAo = 0;
    for (auto it = aoToNames.begin(); it != aoToNames.end(); ++it) {
        if ((++nAo % 2000) == 0) say(QStringLiteral("names: resolved %1/%2 visuals…").arg(nAo).arg(aoToNames.size()));
        const QByteArray ad = store.readFile(it.key(), nullptr);
        if (ad.isEmpty()) { ++unresolved; continue; }          // .ao in an absent bundle
        const AssetText::AnimatedObject ao = AssetText::parseAo(ad);
        if (ao.smPath.isEmpty()) { ++unresolved; continue; }   // static / extends-only .ao (v1 skips)
        const QByteArray sd = store.readFile(ao.smPath.toLower(), nullptr);
        if (sd.isEmpty()) { ++unresolved; continue; }
        const AssetText::SkinnedMeshDesc sm = AssetText::parseSm(sd);
        if (sm.smdPath.isEmpty() || !store.index().find(sm.smdPath)) { ++unresolved; continue; }
        if (record(sm.smdPath, it.value())) ++resolved;
    }
    say(QStringLiteral("names: %1 models named (%2 visuals unresolved — absent bundle / no .sm)").arg(resolved).arg(unresolved));

    // ── Icons: no chain — the .dds is the asset. Record it if it is in the index. ──
    int icons = 0;
    for (auto it = ddsToNames.begin(); it != ddsToNames.end(); ++it)
        if (store.index().find(it.key()) && record(it.key(), it.value())) ++icons;
    say(QStringLiteral("names: %1 item icons named").arg(icons));

    m_built = true;
    return true;
}

QString NameIndex::nameFor(const QString& modelPath) const
{
    auto it = m_primary.find(BundleIndex::hashPath(modelPath));
    return it == m_primary.end() ? QString() : it.value();
}

QString NameIndex::searchNamesFor(const QString& modelPath) const
{
    auto it = m_search.find(BundleIndex::hashPath(modelPath));
    return it == m_search.end() ? QString() : it.value();
}

bool NameIndex::save(const QString& cacheDir, const QString& fingerprint, QString* why) const
{
    if (cacheDir.isEmpty()) { if (why) *why = QStringLiteral("no cache dir"); return false; }
    QDir().mkpath(cacheDir);
    QFile f(QDir(cacheDir).filePath(QStringLiteral("name_index_v%1.bin").arg(kCacheVersion)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (why) *why = QStringLiteral("open for write failed"); return false; }
    QDataStream ds(&f); ds.setVersion(QDataStream::Qt_6_0);
    ds << kMagic << kCacheVersion << fingerprint;
    ds << quint32(m_primary.size());
    for (auto it = m_primary.begin(); it != m_primary.end(); ++it) ds << quint64(it.key()) << it.value();
    ds << quint32(m_search.size());
    for (auto it = m_search.begin(); it != m_search.end(); ++it) ds << quint64(it.key()) << it.value();
    ds << kMagic;
    return ds.status() == QDataStream::Ok;
}

bool NameIndex::load(const QString& cacheDir, const QString& fingerprint, QString* why)
{
    if (cacheDir.isEmpty()) { if (why) *why = QStringLiteral("no cache dir"); return false; }
    QFile f(QDir(cacheDir).filePath(QStringLiteral("name_index_v%1.bin").arg(kCacheVersion)));
    if (!f.open(QIODevice::ReadOnly)) { if (why) *why = QStringLiteral("no cache file"); return false; }
    QDataStream ds(&f); ds.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0, ver = 0; QString fp; ds >> magic >> ver >> fp;
    if (magic != kMagic || ver != kCacheVersion) { if (why) *why = QStringLiteral("different cache version"); return false; }
    if (fp != fingerprint) { if (why) *why = QStringLiteral("game index changed"); return false; }
    QHash<quint64, QString> primary, search;
    quint32 n = 0; ds >> n; for (quint32 k = 0; k < n; ++k) { quint64 h; QString s; ds >> h >> s; primary.insert(h, s); }
    ds >> n; for (quint32 k = 0; k < n; ++k) { quint64 h; QString s; ds >> h >> s; search.insert(h, s); }
    quint32 trailer = 0; ds >> trailer;
    if (trailer != kMagic || ds.status() != QDataStream::Ok) { if (why) *why = QStringLiteral("cache truncated"); return false; }
    m_primary = std::move(primary); m_search = std::move(search); m_built = true;
    return true;
}

QString NameIndex::selfTest()
{
    // Filename tie-breaker: the base the model path carries wins over a shorter generic base.
    if (canonicalOf({QStringLiteral("Garment"), QStringLiteral("Rusted Cuirass")},
                    QStringLiteral("art/.../rustedcuirass_drop_6e7f190b.smd")) != QStringLiteral("Rusted Cuirass"))
        return QStringLiteral("NameIndex: canonical should pick the base the filename carries");
    // Variant prefixes are stripped; the plain base the filename carries is chosen.
    if (canonicalOf({QStringLiteral("Runeforged Explorer Armour"), QStringLiteral("Trailblazer Armour"),
                     QStringLiteral("Explorer Armour")}, QStringLiteral("art/.../explorerarmourdrop_1.smd")) != QStringLiteral("Explorer Armour"))
        return QStringLiteral("NameIndex: canonical should strip variants and match the filename");
    if (canonicalOf({QStringLiteral("[DNT] Ritualistic Axe")}, QStringLiteral("x.smd")) != QStringLiteral("Ritualistic Axe"))
        return QStringLiteral("NameIndex: canonical should strip a [DNT] prefix");
    if (!canonicalOf({}, QStringLiteral("x.smd")).isEmpty()) return QStringLiteral("NameIndex: empty in → empty out");
    return QString();
}
