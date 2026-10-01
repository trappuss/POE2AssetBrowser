#pragma once
#include "bundle/BundleIndex.h"
#include "bundle/Bundle.h"
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include "model/AssetText.h"
#include "model/GlbExporter.h"
#include "store/MaterialFamilyIndex.h"
#include "store/NameIndex.h"
#include "tex/DdsImage.h"

#include <QCache>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>
#include <memory>

// The engine-specific glue: one object that opens the game's Bundles2 in place, reads any file's
// bytes by path, and resolves a game asset into the generic structures the viewport and exporter
// consume. Everything above this line (viewport, list, export layout, search) is generic; this is
// where PoE2's storage and formats live (template "what is engine-specific").
//
// Bundle handles are cached (an armour's textures and meshes cluster in a few bundles), each with
// its own small block cache. Thread-safe: readFile is called from background load threads.
class AssetStore : public QObject {
    Q_OBJECT
public:
    explicit AssetStore(QObject* parent = nullptr);

    // Open the index at <bundlesDir>. Returns false with `error` set; `progress` gets status lines.
    bool open(const QString& bundlesDir, QString* error, const std::function<void(const QString&)>& progress = {});
    bool isOpen() const { return m_index.isLoaded(); }
    const BundleIndex& index() const { return m_index; }

    // The shader-family classification (docs §6). Built once per game build (load-cache-or-sweep) —
    // heavy the first time, so callers run buildMaterialIndex() on a background thread AFTER open().
    // Until then it is empty and the Models list falls back to path-only search.
    const MaterialFamilyIndex& materialIndex() const { return m_matIndex; }
    bool isMaterialIndexReady() const { return m_matIndex.isBuilt(); }
    bool buildMaterialIndex(const std::function<void(const QString&)>& progress = {});

    // The true in-game names, resolved from the game data tables (docs §8). Built once per game
    // build (load-cache-or-sweep), on a background thread after open(). Empty until then, and empty
    // for models that are not authored items (v1 covers weapons/armour/…).
    const NameIndex& nameIndex() const { return m_nameIndex; }
    bool isNameIndexReady() const { return m_nameIndex.isBuilt(); }
    bool buildNameIndex(const std::function<void(const QString&)>& progress = {});

    // Read a file's raw (decompressed) bytes by game path or by file-record index. Empty on failure.
    QByteArray readFile(const QString& path, QString* error = nullptr);
    QByteArray readFile(uint32_t fileIndex, QString* error = nullptr);

    // ── Resolving a model ─────────────────────────────────────────────────────────────────────
    // Load a .smd or .fmt by path into geometry. For a .smd, if `smDescriptorPath` (its .sm) is
    // supplied or discoverable, per-part materials are attached from it.
    bool loadModel(const QString& modelPath, ModelGeometry& geo, QString* error);

    // Resolve the export materials for a loaded geometry (decodes base-colour + normal textures).
    QVector<GlbExporter::ExportMaterial> resolveMaterials(const ModelGeometry& geo, bool decodeTextures);

    // Resolve the skeleton for a model. Uses the co-located .ast; for player armour whose folder has
    // no covering rig, falls back to the character base rig (onerig.ast) when `minBones` (the mesh's
    // joint-palette size, from geometry) exceeds the co-located rig. Pass 0 to disable the fallback.
    AstSkeleton::Skeleton loadSkeletonFor(const QString& modelPath, bool decodeClips, int minBones = 0);

    // ── Player animation library (docs/FORMATS.md §7) ────────────────────────────────────────────
    // The player's real motion is NOT in the base rig — art/models/charactersfour/onerig.ast carries
    // only a static idle_01 hold. It lives in ~112 per-move .ast files under
    // art/models/charactersfour/animations/<move>/onerig.ast, each a copy of the 87-bone base rig
    // carrying that move's clips (basesprint = 151 sprint variants, base2hsword = attack combos, …).
    // Every such file's rig shares the base rig's bone names, so its clips retarget onto any base-rig
    // character by name (AstSkeleton::retargetClip). Categories are cheap to list (index dir names); a
    // category's clip NAMES come from parsing its header (no key decode); one clip's keys are decoded
    // on demand for playback, so a 150-clip file never costs more than the one clip in use.
    struct AnimCategory { QString name; QString path; };   // display name · .ast game path
    const QVector<AnimCategory>& playerAnimCategories();
    // Header of a category .ast: bones + clip metadata (names, fps), no decoded keys. Invalid on failure.
    AstSkeleton::Skeleton playerAnimHeader(const QString& astPath);
    // A category .ast with ONLY clip `clipIndex`'s keys decoded (bones + that one clip). For playback.
    AstSkeleton::Skeleton playerAnimClip(const QString& astPath, int clipIndex);
    // Bone-name set of the player base rig (charactersfour/onerig.ast). A mesh whose resolved skeleton
    // covers most of these names is on the player rig, so the animation library applies to it; a
    // monster or prop rig does not. Empty if the base rig is absent from the install.
    const QSet<QString>& playerRigBoneNames();

    // Decode a texture by path (a .dds). Reads the sibling .dds.header for dimensions if present.
    QImage loadTexture(const QString& ddsPath, QString* error, DdsImage::Info* infoOut = nullptr);

    // Every file under a directory prefix (recursive), optionally filtered to one extension (e.g.
    // ".smd"). Scans the directory table via the dir buckets, not all ~1.4M file records. Lowercase
    // game paths. Used to gather an item's /fx/ meshes and similar folder-scoped sets.
    QStringList filesUnderPrefix(const QString& dirPrefix, const QString& ext = QString());

    // ── Non-destructive raw extraction ──────────────────────────────────────────────────────────
    // Every ORIGINAL game file a model depends on: the mesh (.smd/.fmt), its .sm descriptor, the .mat
    // materials, and — with includeTextures — every .dds those materials reference; plus each file's
    // sibling .header. Optionally the .ao/.ast skeleton chain. All lowercase game paths that exist in
    // the index, de-duplicated. Feed these to readFile() to write the EXACT authored bytes (no decode,
    // no conversion) so a user can extract a model in its original condition.
    QStringList collectAssetFiles(const QString& modelPath, bool includeTextures = true, bool includeSkeleton = true);

    // ── Attachment assembly (docs/FORMATS.md §7) ────────────────────────────────────────────────
    // A character body's `.ao` declares `AttachedAnimatedObject { attached_object = "<bone> <ao>" }`
    // — a coat pinned to hip_jntBnd, feathers to the head, weapons in sockets — and each attachment
    // carries its own mesh. attachmentsForModel() reverse-maps a loaded body mesh to that `.ao` and
    // resolves every attachment to a drawable mesh + the parent bone it hangs from, so the viewport
    // can assemble the whole character. Empty when the mesh is not a body / declares no attachments.
    struct AttachmentPiece {
        QString bone;         // parent bone name in the BODY skeleton the piece attaches to
        QString aoPath;       // the attachment's own .ao
        QString smdPath;      // resolved attachment mesh (.smd); empty if it could not be resolved
        QString label;        // short display label (in-game name if known, else mesh stem)
    };
    struct Assembly {
        QString bodyAo;                     // the body .ao this was resolved from (empty = none found)
        QString skeletonAst;                // the body's declared ClientAnimationController skeleton (.ast)
        QVector<AttachmentPiece> pieces;
        bool isEmpty() const { return pieces.isEmpty(); }
    };
    Assembly attachmentsForModel(const QString& bodySmdPath);

    // Merge a body mesh with its attachments into ONE geometry for export: each attachment is baked at
    // its parent bone's bind transform and weighted 100% to that bone, so the shared skinning path (and
    // the .glb exporter) animate it rigidly with the body — no new coordinate math, it reuses the
    // verified skin. Materials/parts are concatenated; resolveMaterials(result) resolves them all.
    // Returns just the body geometry when there are no resolvable attachments.
    ModelGeometry assembleForExport(const QString& bodySmdPath, const AstSkeleton::Skeleton& skel);

    void clearCaches();

private:
    std::shared_ptr<Bundle> bundleFor(uint32_t fileIndex, QString* error);
    QString findSmForSmd(const QString& smdPath);   // guess the .sm beside a .smd via the .sm's own smdPath link
    // The body `.ao` for a mesh, found by a scoped `.ao` search CONFIRMED by SkinMesh equality (used by
    // both attachment assembly and skeleton resolution). Returns an invalid AnimatedObject when none.
    AssetText::AnimatedObject findBodyAo(const QString& bodySmdPath);

    // Build (once) the directory→files and extension→files buckets so the co-located .sm/.ast/.ao
    // lookups iterate only the files in the relevant directory/extension instead of scanning all
    // ~1.4M records every call (the old cost dominated bulk runs and per-selection load time).
    void ensureLookups();

    BundleIndex m_index;
    MaterialFamilyIndex m_matIndex;
    NameIndex m_nameIndex;
    QMutex m_bundleMutex;
    QHash<uint32_t, std::shared_ptr<Bundle>> m_bundleCache;   // by bundle index
    QList<uint32_t> m_bundleLru;                              // access order, most-recent last

    QMutex m_lookupMutex;
    bool m_lookupsBuilt = false;
    QHash<uint32_t, QVector<uint32_t>> m_dirFiles;   // directory id → file-record indices
    QHash<uint16_t, QVector<uint32_t>> m_extFiles;   // extension id → file-record indices

    // Player animation library: catalogue (built once) + a one-entry cache of the last category's
    // decompressed .ast bytes (so browsing several clips of one move re-reads the bundle only once).
    QMutex m_animMutex;
    bool m_animCatBuilt = false;
    QVector<AnimCategory> m_animCats;
    QString m_animBytesPath;
    QByteArray m_animBytes;
    QByteArray animBytes(const QString& astPath);    // cached read of a .ast file's bytes
    bool m_playerRigBonesBuilt = false;
    QSet<QString> m_playerRigBones;
};
