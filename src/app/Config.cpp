#include "app/Config.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {
constexpr auto kGameDir    = "paths/gameDir";
constexpr auto kBundlesDir = "paths/bundlesDir";
constexpr auto kExportDir  = "paths/lastExportDir";
constexpr auto kBulkOutDir = "paths/bulkOutDir";
constexpr auto kBulkWorkers = "export/bulkWorkers";
constexpr auto kExportYaw180 = "export/yaw180";
constexpr auto kExportUnitScale = "export/unitScale";
constexpr auto kExportSkeleton = "export/skeleton";
constexpr auto kExportAnimations = "export/animations";
constexpr auto kExportEmbedTex = "export/embedTextures";
constexpr auto kExportNormalZ = "export/reconstructNormalZ";
constexpr auto kExportAttachments = "export/includeAttachments";
constexpr auto kExportGltf = "export/gltf";
constexpr auto kImageScale = "image/scalePercent";
constexpr auto kImageTransparent = "image/transparentBg";
constexpr auto kImageCrop = "image/cropToModel";
constexpr auto kExportLooseTex = "export/looseTextures";
constexpr auto kExportShowPrompt = "export/showPrompt";
constexpr auto kExportReuseLastDir = "export/reuseLastDir";
constexpr auto kImageShowPrompt = "image/showPrompt";
}

QString Config::gameDir()                    { return QSettings().value(kGameDir).toString(); }
void    Config::setGameDir(const QString& d) { QSettings().setValue(kGameDir, d); }

QString Config::bundlesDir()
{
    const QString override_ = QSettings().value(kBundlesDir).toString();
    if (!override_.isEmpty() && QFileInfo::exists(override_)) return override_;
    const QString game = gameDir();
    if (game.isEmpty()) return QString();
    return QDir(game).filePath(QStringLiteral("Bundles2"));
}

QString Config::bundlesDirOverride()               { return QSettings().value(kBundlesDir).toString(); }
void    Config::setBundlesDirOverride(const QString& d) { if (d.isEmpty()) QSettings().remove(kBundlesDir); else QSettings().setValue(kBundlesDir, d); }

QString Config::lastExportDir()                    { return QSettings().value(kExportDir).toString(); }
void    Config::setLastExportDir(const QString& d) { QSettings().setValue(kExportDir, d); }

QString Config::bulkOutDir()                    { return QSettings().value(kBulkOutDir).toString(); }
void    Config::setBulkOutDir(const QString& d) { QSettings().setValue(kBulkOutDir, d); }

int  Config::bulkWorkers()          { return QSettings().value(kBulkWorkers, 0).toInt(); }   // 0 = auto
void Config::setBulkWorkers(int n)  { QSettings().setValue(kBulkWorkers, n); }

// Export orientation. Default false — the most original model orientation is preferred on export;
// enable only for thumbnail pipelines that need characters facing +Z (see GlbExporter::Options).
bool Config::exportYaw180()                 { return QSettings().value(kExportYaw180, false).toBool(); }
void Config::setExportYaw180(bool on)       { QSettings().setValue(kExportYaw180, on); }

double Config::exportUnitScale()               { return QSettings().value(kExportUnitScale, 0.005).toDouble(); }
void   Config::setExportUnitScale(double s)    { QSettings().setValue(kExportUnitScale, s); }
bool   Config::exportSkeleton()                { return QSettings().value(kExportSkeleton, true).toBool(); }
void   Config::setExportSkeleton(bool on)      { QSettings().setValue(kExportSkeleton, on); }
bool   Config::exportAnimations()              { return QSettings().value(kExportAnimations, true).toBool(); }
void   Config::setExportAnimations(bool on)    { QSettings().setValue(kExportAnimations, on); }
bool   Config::exportEmbedTextures()           { return QSettings().value(kExportEmbedTex, true).toBool(); }
void   Config::setExportEmbedTextures(bool on) { QSettings().setValue(kExportEmbedTex, on); }
bool   Config::exportReconstructNormalZ()      { return QSettings().value(kExportNormalZ, true).toBool(); }
void   Config::setExportReconstructNormalZ(bool on) { QSettings().setValue(kExportNormalZ, on); }
bool   Config::exportIncludeAttachments()      { return QSettings().value(kExportAttachments, false).toBool(); }
void   Config::setExportIncludeAttachments(bool on) { QSettings().setValue(kExportAttachments, on); }
bool   Config::exportGltf()                    { return QSettings().value(kExportGltf, false).toBool(); }
void   Config::setExportGltf(bool on)          { QSettings().setValue(kExportGltf, on); }

int    Config::imageScalePercent()             { return QSettings().value(kImageScale, 100).toInt(); }
void   Config::setImageScalePercent(int p)     { QSettings().setValue(kImageScale, p); }
bool   Config::imageTransparentBg()            { return QSettings().value(kImageTransparent, true).toBool(); }
void   Config::setImageTransparentBg(bool on)  { QSettings().setValue(kImageTransparent, on); }
bool   Config::imageCropToModel()              { return QSettings().value(kImageCrop, true).toBool(); }
void   Config::setImageCropToModel(bool on)    { QSettings().setValue(kImageCrop, on); }

// Write textures as loose files (a .gltf's images as sibling .png) instead of embedding them in the
// file. Only meaningful for .gltf export; .glb is always self-contained. Default off (embed).
bool   Config::exportLooseTextures()           { return QSettings().value(kExportLooseTex, false).toBool(); }
void   Config::setExportLooseTextures(bool on) { QSettings().setValue(kExportLooseTex, on); }
// Show the export options dialog before each export. When off, exports use the saved Settings defaults.
bool   Config::exportShowPrompt()              { return QSettings().value(kExportShowPrompt, true).toBool(); }
void   Config::setExportShowPrompt(bool on)    { QSettings().setValue(kExportShowPrompt, on); }
// Skip the file/folder picker and export straight into the last-used folder (auto-named).
bool   Config::exportReuseLastDir()            { return QSettings().value(kExportReuseLastDir, false).toBool(); }
void   Config::setExportReuseLastDir(bool on)  { QSettings().setValue(kExportReuseLastDir, on); }
// Show the save-image options dialog before each image save. When off, uses the saved Settings defaults.
bool   Config::imageShowPrompt()               { return QSettings().value(kImageShowPrompt, true).toBool(); }
void   Config::setImageShowPrompt(bool on)     { QSettings().setValue(kImageShowPrompt, on); }
