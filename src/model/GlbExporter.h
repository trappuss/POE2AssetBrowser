#pragma once
#include "model/ModelGeometry.h"
#include "model/AstSkeleton.h"
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

// Self-contained binary-glTF (.glb) writer — no external glTF library, so the byte format is fully
// under our control and verifiable. Exports POSITION / NORMAL / TANGENT / TEXCOORD_0 / indices,
// plus JOINTS_0 / WEIGHTS_0 + skin + inverseBindMatrices when a skeleton is supplied, and each
// AstSkeleton clip as a glTF animation.
//
// Everything is emitted in the glTF Y-up frame: native (x,y,z) → (x, -z, y), a proper rotation, so
// winding is preserved (docs/FORMATS.md §3.2). This is the single-model path — it deliberately
// ignores the shared output layout (template §6).
namespace GlbExporter {

struct ExportMaterial {
    QString name;
    QImage  baseColor;         // → baseColorTexture (RGB albedo + A opacity)
    QImage  normal;            // → normalTexture (RGB tangent normal, Z reconstructed from PoE2 RG)
    QImage  metallicRoughness; // → metallicRoughnessTexture in glTF ORM packing: R=AO, G=roughness, B=metalness
    QImage  emissive;          // → emissiveTexture
    QImage  specularColor;     // → KHR_materials_specular.specularColorTexture (spec-gloss families)
    bool    doubleSided = true; // PoE cloth/armour is generally two-sided
    bool    alphaCutout = false;   // glTF alphaMode MASK (ForceAlphaTest materials)
    bool    alphaBlend = false;    // glTF alphaMode BLEND (transparent, non-tested)
    int     alphaMode = 0;         // viewport composite: 0 Opaque, 1 Mask, 2 Blend, 3 Additive (AssetText::AlphaMode)
    float   alphaCutoff = 0.5f;
    float   metalFactor = 1.0f;   // scales metallicRoughnessTexture.B; 0 for dielectric / spec-gloss
    float   roughFactor = 1.0f;
    float   emissiveStrength = 0.0f;   // 0 = no glow (KHR_materials_emissive_strength when >1)
    bool    hasOcclusion = false;      // metallicRoughnessTexture.R holds AO → also emit occlusionTexture
    float   occlusionStrength = 1.0f;
    bool    dielectricSpec = false;    // emit KHR_materials_specular (dielectric spec-gloss look)
    float   transmissionFactor = 0.0f; // KHR_materials_transmission (translucency); 0 = none
    float   subsurface[3] = {0,0,0};   // viewport-only: SSS mid-depth tint (0,0,0 = no subsurface)
    // FurV2 shell-fur (viewport preview only — glTF has no fur). furNoise = strand noise,
    // furMask = fur length/density (single channel). When isFur, the viewport shades the surface as
    // fur (strand detail + density AO + a soft fuzz rim) instead of the flat-grey no-albedo fallback.
    bool    isFur = false;
    QImage  furNoise;
    QImage  furMask;
    float   furDepth = 0.0f;
};

struct Options {
    bool  includeSkeleton = true;
    bool  includeAnimations = true;
    // Which clips to export when includeAnimations is on: -1 = all clips; >=0 = only that clip index
    // (the "current clip only" choice). Ignored when includeAnimations is false (= no animation).
    int   onlyClip = -1;
    bool  includeAttachments = false;   // bake the assembled attachments (parented to their bones) in
    bool  embedTextures = true;
    // Write textures as loose sibling .png files (referenced by URI) instead of embedding them. Only
    // used for .gltf export; ignored for a self-contained .glb. When on, embedTextures is treated as on
    // for the purpose of emitting the texture set — the images just land beside the file, not inside it.
    bool  looseTextures = false;
    bool  reconstructNormalZ = true;   // rebuild B = sqrt(1-x²-y²) so DCCs light BC5-style normals right
    // PoE2 art units are ~0.5 cm each, so metres = native × 0.005. Anchored on atziriphase2's
    // hip bone (210.6 native → 1.05 m, right for a tall boss) and body top (437 → 2.19 m); a
    // standard humanoid hip (~180 native) lands at ~0.9 m → a 1.8 m character. This matches the
    // value the user found empirically in Blender. Exposed as a Settings override (docs §16).
    float unitScale = 0.005f;
    // Rotate the whole model 180° about the up axis (via a wrapper root node, not by touching the
    // geometry) so characters face +Z for thumbnail generators. OFF by default: the most original
    // orientation is preferred on export; turn this on only when a downstream tool needs the flip.
    bool  yaw180 = false;
};

// Write `geo` to `path`. `skeleton` may be empty (static export). `materials` is indexed by
// ModelGeometry::materialPaths; entries may be default-constructed. Returns false with `error` set.
bool write(const ModelGeometry& geo, const AstSkeleton::Skeleton& skeleton,
           const QVector<ExportMaterial>& materials, const Options& opt,
           const QString& path, QString* error);

// One loose texture to write beside a .gltf: its file name (referenced by URI in the glTF) and PNG bytes.
struct LooseImage { QString name; QByteArray png; };

// Produce the .glb bytes without touching disk (used by the self-test and drag-out). When
// opt.looseTextures is set, the texture images are NOT embedded; instead each is appended to
// `looseOut` (if non-null) and referenced by URI, for the caller to write next to the .gltf.
QByteArray build(const ModelGeometry& geo, const AstSkeleton::Skeleton& skeleton,
                 const QVector<ExportMaterial>& materials, const Options& opt, QString* error,
                 QVector<LooseImage>* looseOut = nullptr);

// Self-test: build a one-triangle skinned glb in memory and re-parse its JSON + chunk lengths.
QString selfTest();

}  // namespace GlbExporter
