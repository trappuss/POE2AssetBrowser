// POE2AssetBrowser — entry point.
//
// A native C++17 / Qt6 / OpenGL asset browser for Path of Exile 2, built on the AssetBrowser family
// design (docs/ASSETBROWSER_TEMPLATE.md — the D4AssetBrowser reference). It opens the game's
// Bundles2 storage in place (Oodle-decompressed via the bundled ooz), rebuilds the file paths from
// the index, decodes textures (bcdec) and models (.smd/.fmt) in-tool, and exports rigged, animated
// .glb. Everything it writes lives in data/ beside the exe.
#include <QApplication>
#include <QSettings>
#include <QSurfaceFormat>

#include "app/AppPaths.h"
#include "app/MainWindow.h"
#include "app/SehGuard.h"
#include "bundle/BundleIndex.h"
#include "model/MeshParser.h"
#include "model/AssetText.h"
#include "model/AstSkeleton.h"
#include "model/GlbExporter.h"
#include "tex/DdsImage.h"
#include "util/QueryTerm.h"
#include "util/ExportLayout.h"
#include "index/Facets.h"
#include "store/MaterialFamilyIndex.h"
#include "store/NameIndex.h"

#include <QtGlobal>

int main(int argc, char** argv)
{
    // Structured-exception translator on the GUI thread (template §2: crash-prone paths are
    // SEH-guarded). SehGuard was compiled into this app from day one but never installed and
    // never called, so a bad mesh or a GPU-driver fault killed the process outright. Background
    // threads (bulk workers) install it themselves through seh::runGuarded.
    seh::installSehTranslator();

    // OpenGL 3.3 core — the compressed-BC uploads and FBO picking the viewport uses are all core in
    // 3.3, which every driver from the last decade supports.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("POE2AssetBrowser"));
    QApplication::setApplicationName(QStringLiteral("POE2AssetBrowser"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // Portable settings: data\POE2AssetBrowser.ini beside the exe, never the registry.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, AppPaths::dataDir());

    // Version-stamped caches pruned at startup (template §1).
    AppPaths::pruneOldCaches(QStringLiteral("bundle_index_v"), int(BundleIndex::kCacheVersion), QStringLiteral(".bin"));
    AppPaths::pruneOldCaches(QStringLiteral("material_index_v"), int(MaterialFamilyIndex::kCacheVersion), QStringLiteral(".bin"));
    AppPaths::pruneOldCaches(QStringLiteral("name_index_v"), int(NameIndex::kCacheVersion), QStringLiteral(".bin"));

    // Startup self-tests for invariants that would otherwise fail silently (template §4, §7). A
    // failure is printed loudly but does not stop the app — the tool still runs, it just says so.
    const QString tests[] = { QueryTerm::selfTest(), MeshParser::selfTest(), DdsImage::selfTest(),
                              AssetText::selfTest(), AstSkeleton::selfTest(), GlbExporter::selfTest(),
                              ExportLayout::selfTest(), Facets::selfTest(), MaterialFamilyIndex::selfTest(),
                              NameIndex::selfTest() };
    // qWarning, not stderr: a Windows GUI build has no console, so stderr was discarded and a
    // failing self-test was silent. qWarning reaches a debugger and any installed message handler.
    for (const QString& t : tests)
        if (!t.isEmpty()) qWarning("SELF-TEST FAILED: %s", qUtf8Printable(t));

    MainWindow w;
    w.show();
    return app.exec();
}
