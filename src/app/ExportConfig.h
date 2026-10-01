#pragma once
#include "app/Config.h"
#include "model/GlbExporter.h"

// Single source of truth: build GlbExporter::Options from the persisted Settings (Config). Both the
// Models-tab single export and the Bulk export read through this, so the Settings dialog governs
// every .glb the app writes. Defaults (in Config) mirror GlbExporter::Options, so an un-configured
// install exports exactly as the code defaults would.
inline GlbExporter::Options exportOptionsFromConfig()
{
    GlbExporter::Options o;
    o.unitScale         = float(Config::exportUnitScale());
    o.yaw180            = Config::exportYaw180();
    o.includeSkeleton   = Config::exportSkeleton();
    o.includeAnimations = Config::exportAnimations();
    o.embedTextures     = Config::exportEmbedTextures();
    o.reconstructNormalZ = Config::exportReconstructNormalZ();
    o.includeAttachments = Config::exportIncludeAttachments();
    return o;
}
