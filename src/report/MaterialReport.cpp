#include "report/MaterialReport.h"
#include "store/AssetStore.h"
#include "model/ModelGeometry.h"
#include "model/AssetText.h"

#include <QFileInfo>

namespace {
// Present in the index (resolvable) vs absent — stated in words, never just a tick.
QString resolveMark(AssetStore& store, const QString& path)
{
    if (path.isEmpty()) return QStringLiteral("(none declared)");
    return store.index().find(path) ? QStringLiteral("resolves") : QStringLiteral("ABSENT — not in this build's index");
}
}  // namespace

QString MaterialReport::explainModel(AssetStore& store, const QString& modelPath)
{
    QStringList out;
    out << QStringLiteral("Explain material — %1").arg(modelPath);
    out << QString(64, QLatin1Char('='));

    if (!store.isOpen()) { out << QStringLiteral("The index is not loaded."); return out.join(QLatin1Char('\n')); }

    ModelGeometry geo; QString err;
    if (!store.loadModel(modelPath, geo, &err)) {
        out << QStringLiteral("Could not load the model: %1").arg(err);
        return out.join(QLatin1Char('\n'));
    }
    const bool isFmt = modelPath.toLower().endsWith(QStringLiteral(".fmt"));
    out << QStringLiteral("Format: .%1 v%2   %3   %4 parts, %5 materials")
               .arg(isFmt ? QStringLiteral("fmt") : QStringLiteral("smd"))
               .arg(geo.formatVersion)
               .arg(geo.skinned ? QStringLiteral("skinned") : QStringLiteral("static"))
               .arg(geo.parts.size()).arg(geo.materialPaths.size());
    // The asset id — the MurmurHash64 of the path (PoE2 has no SNO). Shown in hex and decimal so it
    // can be cross-referenced with GGPK/bundle tooling and pasted back into the search box: find by
    // id accepts a decimal token or a "0x…" hex token.
    if (const BundleIndex::FileRecord* rec = store.index().find(modelPath))
        out << QStringLiteral("Asset id (path hash): 0x%1  (%2)")
                   .arg(rec->hash, 16, 16, QLatin1Char('0')).arg(rec->hash);

    // Where the roster came from — authored linkage, no name-substring guessing (template §18).
    out << QString();
    if (isFmt)
        out << QStringLiteral("Material roster source: the .fmt's own embedded material references.");
    else
        out << QStringLiteral("Material roster source: the .sm skinned-mesh descriptor beside the .smd "
                              "(each row names a .mat and how many consecutive parts use it).");

    if (geo.materialPaths.isEmpty()) {
        out << QString() << QStringLiteral("No materials are declared for this model.");
        return out.join(QLatin1Char('\n'));
    }

    for (int mi = 0; mi < geo.materialPaths.size(); ++mi) {
        const QString matPath = geo.materialPaths[mi];
        // How many parts of THIS model use this material (a measured, honest count — not a global
        // cross-reference, which the report deliberately does not fabricate).
        int usedBy = 0;
        for (const MeshPart& p : geo.parts) if (p.materialIndex == mi) ++usedBy;

        out << QString();
        out << QStringLiteral("[%1] %2").arg(mi + 1).arg(matPath);
        out << QStringLiteral("     status: %1   ·   used by %2 of %3 parts in this model")
                   .arg(resolveMark(store, matPath)).arg(usedBy).arg(geo.parts.size());

        const QByteArray matData = store.readFile(matPath, nullptr);
        if (matData.isEmpty()) {
            out << QStringLiteral("     (the .mat could not be read — nothing further to report for it)");
            continue;
        }
        const AssetText::Material mat = AssetText::parseMat(matData);
        if (!mat.graphs.isEmpty())
            out << QStringLiteral("     fxgraph parents: %1").arg(mat.graphs.join(QStringLiteral(" · ")));

        // Authored texture roles — the role NAME is authored data (a custom-parameter name), the
        // path is authored; the tool never infers a role from a file name.
        out << QStringLiteral("     Authored texture roles (from the .mat):");
        if (mat.textures.isEmpty())
            out << QStringLiteral("       (none — this material declares no textures)");
        for (const AssetText::MaterialTexture& t : mat.textures)
            out << QStringLiteral("       %1 → %2   [%3%4]")
                       .arg(t.role.isEmpty() ? QStringLiteral("(unnamed role)") : t.role, t.path,
                            resolveMark(store, t.path), t.srgb ? QStringLiteral(", sRGB") : QString());

        // Shader family / workflow — the authored fxgraph that decides how the channels are read.
        auto wfName = [](AssetText::Workflow w) {
            switch (w) {
                case AssetText::Workflow::MetalRough:          return QStringLiteral("metal-rough");
                case AssetText::Workflow::DielectricSpecGloss: return QStringLiteral("dielectric spec-gloss (non-metal)");
                case AssetText::Workflow::SpecGlossSpecMask:   return QStringLiteral("spec-gloss with spec mask (non-metal)");
                default:                                       return QStringLiteral("unknown");
            }
        };
        out << QStringLiteral("     Shader family: %1   →   workflow: %2")
                   .arg(mat.family.isEmpty() ? QStringLiteral("(no Materials/*.fxgraph found)") : mat.family, wfName(mat.workflow));
        auto alphaName = [](AssetText::AlphaMode a) {
            switch (a) {
                case AssetText::AlphaMode::Mask:     return QStringLiteral("MASK — hard alpha-test cutout (albedo A = opacity)");
                case AssetText::AlphaMode::Blend:    return QStringLiteral("BLEND — alpha-over transparency, no depth write");
                case AssetText::AlphaMode::Additive: return QStringLiteral("ADDITIVE — energy/glow added to the frame (glTF export approximates as BLEND)");
                default:                             return QStringLiteral("OPAQUE — no transparency (albedo A never discarded)");
            }
        };
        out << QStringLiteral("     Composite / alpha: %1").arg(alphaName(mat.alphaMode));
        QStringList fx;
        if (mat.hasSSS)          fx << QStringLiteral("subsurface (SSS)");
        if (mat.hasTranslucency) fx << QStringLiteral("translucency");
        if (mat.hasFur)          fx << QStringLiteral("fur");
        if (!fx.isEmpty()) out << QStringLiteral("     Effects: %1").arg(fx.join(QStringLiteral(", ")));

        // Authored vs substituted/interpreted — the whole point of the report (§18, §6).
        const QString base = mat.albedo, nga = mat.normalGlossAO, met = mat.metalMask, glow = mat.emissiveTex;
        out << QStringLiteral("     How the glTF PBR material is built (authored vs substituted/interpreted):");
        out << QStringLiteral("       base colour : %1")
                   .arg(base.isEmpty() ? QStringLiteral("SUBSTITUTED — flat mid-grey (no albedo role authored)")
                                       : QStringLiteral("AUTHORED — %1%2").arg(base, mat.specMaskInAlbedoAlpha ? QStringLiteral(" (alpha = specular mask)") : QStringLiteral(" (alpha = opacity)")));
        out << QStringLiteral("       normal      : %1")
                   .arg(nga.isEmpty() ? QStringLiteral("SUBSTITUTED — geometric normals only (no NormalGlossAO role)")
                                      : QStringLiteral("AUTHORED — %1 (packing auto-detected at decode: RG-normal[+B gloss,A AO] or full-RGB-normal[+A gloss])").arg(nga));
        out << QStringLiteral("       roughness   : %1")
                   .arg(nga.isEmpty() ? QStringLiteral("SUBSTITUTED — constant 0.8")
                                      : mat.usesRoughness ? QStringLiteral("INTERPRETED — the gloss channel taken as roughness directly (Use(s)Roughness flag set)")
                                                          : QStringLiteral("INTERPRETED — 1 − gloss (gloss = NormalGlossAO B or A per packing)"));
        out << QStringLiteral("       occlusion   : %1")
                   .arg(nga.isEmpty() ? QStringLiteral("SUBSTITUTED — 1.0 (no map)")
                                      : QStringLiteral("AUTHORED when RG-normal (NormalGlossAO A); 1.0 for full-RGB-normal (no baked AO)"));
        out << QStringLiteral("       metalness   : %1")
                   .arg(mat.workflow != AssetText::Workflow::MetalRough
                            ? QStringLiteral("SUBSTITUTED — 0; non-metal family → KHR_materials_specular%1")
                                  .arg(!mat.specColorTex.isEmpty() ? QStringLiteral(" (specular colour from SpecularColour_TEX)")
                                       : mat.specMaskInAlbedoAlpha ? QStringLiteral(" (spec mask from albedo alpha)") : QString())
                            : met.isEmpty() ? QStringLiteral("SUBSTITUTED — 0 (metal-rough but no SpecularMask/Metal authored)")
                                            : QStringLiteral("AUTHORED — %1 (R = metalness)").arg(met));
        if (!mat.specColorTex.isEmpty())
            out << QStringLiteral("       spec colour : AUTHORED — %1 (RGB → KHR_materials_specular specularColorTexture)").arg(mat.specColorTex);
        out << QStringLiteral("       emissive    : %1")
                   .arg(glow.isEmpty() ? QStringLiteral("SUBSTITUTED — none (no glow role authored)")
                                       : QStringLiteral("AUTHORED — %1").arg(glow));
        if (mat.hasTranslucency)
            out << QStringLiteral("       translucency: EXPORTED — KHR_materials_transmission%1").arg(mat.translucencyTex.isEmpty() ? QString() : QStringLiteral(" (map: %1)").arg(mat.translucencyTex));
        if (mat.hasSSS)
            out << QStringLiteral("       subsurface  : viewport-approximated (depth tints shade the terminator); no core-glTF SSS, so NOT baked into the export");
    }

    out << QString();
    out << QStringLiteral("Note: 'used by' counts parts within THIS model only. A global cross-reference "
                          "(every asset that shares a material) is not computed — the report does not imply "
                          "a number it did not measure.");
    return out.join(QLatin1Char('\n'));
}
