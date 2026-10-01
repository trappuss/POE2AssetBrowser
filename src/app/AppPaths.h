#pragma once
// Portable, self-contained storage (template §1). Everything the tool writes — settings INI,
// index cache, thumbnails, logs — lives in a "data" folder beside the executable: no Windows
// registry, no %AppData%. Move the folder and it keeps working; delete it and nothing is left.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QString>

namespace AppPaths {

inline QString dataDir()
{
    static const QString d = [] {
        QString base = QCoreApplication::applicationDirPath();
        if (base.isEmpty()) base = QDir::currentPath();
        const QString dir = QDir(base).filePath(QStringLiteral("data"));
        QDir().mkpath(dir);
        return dir;
    }();
    return d;
}

inline QString file(const QString& name) { return QDir(dataDir()).filePath(name); }

inline QString subDir(const QString& name)
{
    const QString d = QDir(dataDir()).filePath(name);
    QDir().mkpath(d);
    return d;
}

// Delete superseded versions of a versioned cache (template §1: version-stamped caches pruned at
// startup). Only files matching <stem><digits><ext> with a LOWER number are removed.
inline void pruneOldCaches(const QString& stem, int currentVersion, const QString& ext)
{
    const QDir d(dataDir());
    const QRegularExpression re(QStringLiteral("^%1(\\d+)%2$")
                                    .arg(QRegularExpression::escape(stem), QRegularExpression::escape(ext)));
    for (const QString& fn : d.entryList(QDir::Files)) {
        const QRegularExpressionMatch m = re.match(fn);
        if (!m.hasMatch()) continue;
        const int v = m.captured(1).toInt();
        if (v > 0 && v < currentVersion) QFile::remove(d.filePath(fn));
    }
}

}  // namespace AppPaths
