#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

// Parsers for PoE2's small text/JSON descriptors: the `.sm` skinned-mesh descriptor, the `.mat`
// material, and the `.ao` animated-object metadata. All are UTF-16LE (with or without a BOM);
// `.mat` is JSON, the others are line-based. Layout in docs/FORMATS.md §6–7.
//
// These wire a mesh to its materials, skeleton and animations without any name-substring guessing:
// every link is an authored path in the file.
namespace AssetText {

// Decode a `.sm`/`.ao`/`.amd`/`.mat` blob to a QString, handling the BOM-optional UTF-16LE the
// game uses (and UTF-8 as a fallback). Empty on an unreadable blob.
QString decodeText(const QByteArray& data);

// ── .sm : SkinnedMeshData ─────────────────────────────────────────────────────────────────────
struct SkinnedMeshDesc {
    int     version = 0;
    QString smdPath;                 // "art/…/foo.smd" (lowercase)
    struct MatRun { QString matPath; int count; };  // material + how many consecutive meshes use it
    QVector<MatRun> materials;
    float   bbox[6] = {0,0,0,0,0,0};
    bool    valid = false;
};
SkinnedMeshDesc parseSm(const QByteArray& data);

// ── .mat : material ───────────────────────────────────────────────────────────────────────────
// PoE2 materials (docs/FORMATS.md §6, all measured from real .mat files): a JSON object whose
// `graphinstances` layer one or more fxgraphs. The FIRST Metadata/Materials/*.fxgraph is the shader
// family (the PBR workflow); further graphs add effects (SSS, translucency, fur, alpha-test). Roles
// are authored custom-parameter names with fixed channel packing:
//   AlbedoTransparency_TEX : RGB = base colour (sRGB), A = opacity
//   AlbedoSpecMask_TEX     : RGB = base colour (sRGB), A = specular mask (spec-gloss families)
//   NormalGlossAO_TEX      : R,G = tangent normal x,y (Z reconstructed), B = gloss, A = AO
//   SpecularMask_TEX/Metal_TEX : R = metalness (metal-rough families)
enum class Workflow { Unknown, MetalRough, DielectricSpecGloss, SpecGlossSpecMask };

// How the material composites against the frame — an AUTHORED signal read from the Force* effect
// graph (measured on real materials): ForceAlphaTest* → Mask (hard cutout), ForceAlphaBlend /
// ForceNoZWriteAlphaBlend → Blend (alpha-over, no depth write), ForceAdditive → Additive (energy /
// glow), otherwise Opaque. Drives both the viewport (blend state + whether to discard on albedo A)
// and the glTF export (alphaMode). Note: albedo A is only opacity for Opaque/Mask/Blend — for the
// AlbedoSpecMask family it is a spec mask, so the viewport must never discard/blend on it.
enum class AlphaMode { Opaque, Mask, Blend, Additive };

struct MaterialTexture {
    QString role;                    // the authored custom-parameter name (AlbedoTransparency_TEX, …)
    QString path;                    // "art/…/foo.dds" (lowercase)
    bool    srgb = false;
};
struct Material {
    int     version = 0;
    QStringList graphs;              // fxgraph parents, in order
    QVector<MaterialTexture> textures;
    bool    valid = false;

    // ── Derived, evidence-based semantics ──
    QString  family;                 // primary Materials/*.fxgraph stem, e.g. "MetalRough"
    Workflow workflow = Workflow::Unknown;
    bool     alphaTest = false;      // a ForceAlphaTest* graph is present → glTF alphaMode MASK
    AlphaMode alphaMode = AlphaMode::Opaque;  // authored composite mode (Force* effect graph)
    bool     usesRoughness = false;  // Use(s)Roughness param → the gloss channel already IS roughness
    float    occlusionPower = 1.0f;  // AO strength (OcclusionPower param)
    // BasicColourNormalSpec family (Blinn-Phong; ~⅓ of item armour). Its "base_normalspec_texture"
    // packs the tangent normal in the A,G channels and a specular mask in R (measured from the .mat
    // texture src/dest masks), driven by a scalar Blinn specular_exponent — NOT the NormalGlossAO
    // packing. Flagged so the material pipeline unpacks the right channels instead of mis-reading it.
    bool     normalSpecPacked = false;   // base_normalspec_texture: normal in A,G, spec mask in R
    float    specularExponent = 0.0f;    // Blinn-Phong specular_exponent (→ GGX roughness)

    // Resolved role paths (empty when absent):
    QString  albedo;                 // AlbedoTransparency_TEX / AlbedoSpecMask_TEX
    QString  normalGlossAO;          // NormalGlossAO_TEX (RG normal, B gloss, A AO)
    QString  metalMask;              // SpecularMask_TEX / Metal_TEX (R metalness)
    QString  specColorTex;           // SpecularColour_TEX — RGB specular colour (spec-gloss workflow)
    bool     specMaskInAlbedoAlpha = false;  // AlbedoSpecMask family: A is a spec mask, not opacity

    // Effect layers:
    bool     hasSSS = false;         // SSS3Add — subsurface scattering
    QString  sssTex;                 // 01_SSS_TEX (R = SSS mask)
    float    sssTint[3][3] = {};     // [depth 0..2][r,g,b] shallow→deep subsurface tint
    bool     hasTranslucency = false;// TranslucencyTex
    QString  translucencyTex;        // "Translucency map"
    bool     hasFur = false;         // FurV2 (shell fur — not representable in glTF)
    QString  furNoiseTex;            // FurV2 Noise_TEX — the fur-strand noise (breaks the surface into strands)
    QString  furMaskTex;             // FurV2 DepthMap_TEX — fur length / density mask (single-channel, BC4)
    float    furDepth = 0.0f;        // FurV2 "Depth" param — fur length scale (0 when absent)
    QString  emissiveTex;            // glow/emissive role if present

    // Resolve the first texture whose role name contains any case-insensitive key; empty when none.
    QString findTexture(const QStringList& roleKeys) const;
    QString baseColor() const { return albedo; }
    QString normal() const    { return normalGlossAO; }
    QString metal() const     { return metalMask; }
    QString emissive() const  { return emissiveTex; }
};
Material parseMat(const QByteArray& data);

// ── .ao : animated object ─────────────────────────────────────────────────────────────────────
struct AnimatedObject {
    int     version = 0;
    QString extends;
    QString smPath;                  // client/SkinMesh { skin = "…sm" }
    QString skeletonAst;             // client/ClientAnimationController { skeleton = "…ast" }
    QString animationAmd;            // AnimationController { metadata = "…amd" }
    struct Attach { QString bone; QString aoPath; };
    QVector<Attach> attachments;     // AttachedAnimatedObject
    QString selfPath;                // the .ao's own path, set by the caller (not parsed) when useful
    bool    valid = false;
};
AnimatedObject parseAo(const QByteArray& data);

QString selfTest();

}  // namespace AssetText
