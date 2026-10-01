#pragma once
#include <QString>

// Thin wrapper over QSettings for the paths the app needs. Persists to data\POE2AssetBrowser.ini
// (main() points QSettings there). One key per state (template §3.1).
class Config {
public:
    static QString gameDir();               // the "Path of Exile 2" install folder
    static void    setGameDir(const QString& dir);
    static QString bundlesDir();            // <gameDir>/Bundles2 (or an explicit override)
    static QString bundlesDirOverride();    // the explicit override only (empty = derive from gameDir)
    static void    setBundlesDirOverride(const QString& dir);

    static QString lastExportDir();
    static void    setLastExportDir(const QString& dir);

    static QString bulkOutDir();            // last output folder for a bulk extraction run
    static void    setBulkOutDir(const QString& dir);

    // Bulk extraction worker threads. 0 = auto (QThread::idealThreadCount()). The work is
    // CPU-bound (Oodle decode + DDS decode + glb/png assembly), so it scales with cores; reads
    // are thread-safe (per-bundle mutex, reentrant decoder). Clamped to [1,32] at run time.
    static int     bulkWorkers();
    static void    setBulkWorkers(int n);

    // Export orientation: rotate models 180° about up on .glb export (for thumbnail pipelines).
    // Default false — the original model orientation is preferred on export.
    static bool    exportYaw180();
    static void    setExportYaw180(bool on);

    // .glb export options (Settings dialog, §16). Defaults mirror GlbExporter::Options so an
    // un-configured install exports exactly as before.
    static double  exportUnitScale();            // metres per native unit (default 0.005)
    static void    setExportUnitScale(double s);
    static bool    exportSkeleton();             // default true
    static void    setExportSkeleton(bool on);
    static bool    exportAnimations();           // default true
    static void    setExportAnimations(bool on);
    static bool    exportEmbedTextures();        // default true
    static void    setExportEmbedTextures(bool on);
    static bool    exportReconstructNormalZ();   // default true
    static void    setExportReconstructNormalZ(bool on);
    static bool    exportIncludeAttachments();   // default false — bake the body's attachments into the export
    static void    setExportIncludeAttachments(bool on);
    static bool    exportGltf();                 // default false — write .gltf + .bin instead of .glb
    static void    setExportGltf(bool on);

    // Image/icon export (§15). Defaults chosen for a "product shot": 100 % (on-screen resolution),
    // transparent background on, crop to the model on. scalePercent RE-RENDERS larger (supersample).
    static int     imageScalePercent();          // default 100 (clamped 25–400 at render time)
    static void    setImageScalePercent(int p);
    static bool    imageTransparentBg();          // default true
    static void    setImageTransparentBg(bool on);
    static bool    imageCropToModel();            // default true
    static void    setImageCropToModel(bool on);

    // Loose textures on .gltf export: write images as sibling .png files instead of embedding them.
    static bool    exportLooseTextures();         // default false
    static void    setExportLooseTextures(bool on);
    // Skip the per-export options dialog and use the saved Settings defaults.
    static bool    exportShowPrompt();            // default true
    static void    setExportShowPrompt(bool on);
    // Export straight into the last-used folder (skip the file/folder picker), auto-naming the file.
    static bool    exportReuseLastDir();          // default false
    static void    setExportReuseLastDir(bool on);
    // Skip the per-save image options dialog and use the saved Settings defaults.
    static bool    imageShowPrompt();             // default true
    static void    setImageShowPrompt(bool on);
};
