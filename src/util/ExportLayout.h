#pragma once
// How a multi-asset (bulk) export lays its files out on disk — ONE implementation shared by every
// path that writes more than one asset (template §15). Ported from D4AssetBrowser's ExportLayout.h;
// the MECHANISM is copied verbatim (stable string ids, fail-to-Flat, sanitized folder names, one
// _misc bucket so a run never scatters loose files into the parent), the TAXONOMY is PoE2's.
//
// D4 grouped by AppearanceMeta tag groups (Class/Type) keyed by appearance SNO. PoE2 has no SNO and
// no appearance metadata; its authored grouping data is the game's own directory tree — the path
// under Bundles2 IS the taxonomy the artists authored. So the PoE2 modes are:
//   Flat    — everything at the root
//   Type    — "models" (.smd/.fmt) · "textures" (.dds) · "other"   (a coarse, always-available split)
//   Folder  — mirror the asset's own game directory (art/models/items/... → nested subfolders)
//   Model   — each asset in a folder named by its own stem
// The layout picks the GROUP FOLDER only; what is written inside a group is identical in every mode,
// which makes Flat "one group at the root" rather than a special case (template §15).
//
// The rule is the ENTRY POINT, not the item count: BATCH paths (the Bulk tab) obey the layout;
// SINGLE paths (Models-tab Ctrl+E, drag-out) deliberately pass it by (template §6).
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVector>

namespace ExportLayout {

// One item to place: its game path (the authored taxonomy) is all the layout needs.
struct Item {
    quint32 fileIndex = 0;
    QString gamePath;      // lowercase Bundles2 path, e.g. "art/models/.../foo_ab12.smd"
};

// Stored in export/folderLayout as a STABLE STRING, never the combo index (inserting a mode would
// reorder every saved value — the identity this codebase refuses to persist, template §3.1).
inline QString kFlat()   { return QString(); }
inline QString kType()   { return QStringLiteral("Type"); }
inline QString kFolder() { return QStringLiteral("Folder"); }
inline QString kModel()  { return QStringLiteral("_model"); }   // '_' so it can't collide with a taxonomy word

// Only ids this build understands. Anything else — a newer build's mode, a hand-edited INI — reads
// as Flat: an unknown value must fail to where the user pointed, never somewhere they did not ask.
inline bool isKnown(const QString& mode)
{
    return mode.isEmpty() || mode == kType() || mode == kFolder() || mode == kModel();
}

inline QString mode()
{
    const QString m = QSettings().value(QStringLiteral("export/folderLayout")).toString();
    return isKnown(m) ? m : kFlat();
}

// Persist the batch folder-layout choice (one key, stable string; unknown falls back to Flat on read).
inline void setMode(const QString& m)
{
    if (m.isEmpty()) QSettings().remove(QStringLiteral("export/folderLayout"));
    else             QSettings().setValue(QStringLiteral("export/folderLayout"), m);
}

// Sanitize one path segment for Windows: strip separators, trailing dots, and reserved device
// names, so mkpath never silently fails and leaves every write into that folder unexplained.
inline QString sanitizeSegment(QString s)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]"));
    s.replace(bad, QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith(QLatin1Char('.'))) s.chop(1);
    static const QStringList kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),  QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"),
        QStringLiteral("COM5"), QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"), QStringLiteral("LPT7"),
        QStringLiteral("LPT8"), QStringLiteral("LPT9") };
    if (kReserved.contains(s, Qt::CaseInsensitive)) s.prepend(QLatin1Char('_'));
    return s;
}

// The coarse Type bucket for a game path, by extension. Never empty (falls to "other").
inline QString typeBucket(const QString& gamePath)
{
    const QString lower = gamePath.toLower();
    if (lower.endsWith(QStringLiteral(".smd")) || lower.endsWith(QStringLiteral(".fmt"))) return QStringLiteral("models");
    if (lower.endsWith(QStringLiteral(".dds"))) return QStringLiteral("textures");
    return QStringLiteral("other");
}

// The subfolder (relative to the run root) for one item under `mode`. "" = the root itself.
inline QString subfolderFor(const QString& mode, const ExportLayout::Item& it)
{
    if (mode.isEmpty() || !isKnown(mode)) return QString();
    if (mode == kType()) return typeBucket(it.gamePath);
    if (mode == kModel()) {
        const QString stem = sanitizeSegment(QFileInfo(it.gamePath).completeBaseName());
        return stem.isEmpty() ? QStringLiteral("_misc") : stem;
    }
    // Folder: mirror the game directory, each segment sanitized. An asset with no directory (a bare
    // name) goes to _misc rather than loose at the root.
    const QString dir = QFileInfo(it.gamePath).path();   // directory portion of the game path
    if (dir.isEmpty() || dir == QStringLiteral(".")) return QStringLiteral("_misc");
    QStringList out;
    for (const QString& seg : dir.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        const QString s = sanitizeSegment(seg);
        if (!s.isEmpty()) out << s;
    }
    return out.isEmpty() ? QStringLiteral("_misc") : out.join(QLatin1Char('/'));
}

// Absolute destination directory for one item, resolved against the run root.
inline QString dirFor(const QString& root, const QString& mode, const ExportLayout::Item& it)
{
    const QString sub = subfolderFor(mode, it);
    return sub.isEmpty() ? root : QDir(root).filePath(sub);
}

// Self-test: the mechanism's invariants (template §4 discipline — a matcher/layout with a startup
// check). Returns an empty string on success, else the first failure.
inline QString selfTest()
{
    Item smd{0, QStringLiteral("art/models/items/armours/foo/bar_ab12.smd")};
    Item dds{1, QStringLiteral("art/textures/foo/baz_cd34.dds")};
    Item bare{2, QStringLiteral("loose.dds")};
    // Flat: everything at root.
    if (!subfolderFor(kFlat(), smd).isEmpty()) return QStringLiteral("ExportLayout: Flat not empty");
    // Unknown id fails to Flat.
    if (!subfolderFor(QStringLiteral("Nonsense"), smd).isEmpty()) return QStringLiteral("ExportLayout: unknown mode not Flat");
    // Type buckets.
    if (subfolderFor(kType(), smd) != QStringLiteral("models"))   return QStringLiteral("ExportLayout: smd type bucket wrong");
    if (subfolderFor(kType(), dds) != QStringLiteral("textures")) return QStringLiteral("ExportLayout: dds type bucket wrong");
    // Folder mirrors the directory.
    if (subfolderFor(kFolder(), smd) != QStringLiteral("art/models/items/armours/foo"))
        return QStringLiteral("ExportLayout: folder mirror wrong (%1)").arg(subfolderFor(kFolder(), smd));
    // A bare-name asset never lands loose in the parent.
    if (subfolderFor(kFolder(), bare) != QStringLiteral("_misc")) return QStringLiteral("ExportLayout: bare folder not _misc");
    // Model uses the stem.
    if (subfolderFor(kModel(), smd) != QStringLiteral("bar_ab12")) return QStringLiteral("ExportLayout: model stem wrong");
    // Reserved device names are escaped.
    if (sanitizeSegment(QStringLiteral("CON")) != QStringLiteral("_CON")) return QStringLiteral("ExportLayout: reserved name not escaped");
    if (sanitizeSegment(QStringLiteral("trailing.")) != QStringLiteral("trailing")) return QStringLiteral("ExportLayout: trailing dot kept");
    return QString();
}

}   // namespace ExportLayout
