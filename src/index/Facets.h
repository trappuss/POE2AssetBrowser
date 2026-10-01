#pragma once
#include "store/MaterialFamilyIndex.h"   // WorkflowBit / EffectBit for the Shader facet group
#include <QString>
#include <QStringList>
#include <QVector>

// The funnel's facet taxonomy (template §9) — grouped, tickable tags that narrow the asset list.
//
// PoE2 ships no appearance-metadata tag system (unlike D4's AppearanceMeta), so every facet here is
// derived from AUTHORED data already in the index: the asset's own folder path under Bundles2. The
// path IS the taxonomy the artists authored — art/models/items/armours/... — so classifying by it is
// evidence, not a name-substring guess (template §3.8: classify by authored data, never a name
// substring; a folder-segment test is a path-structure test, not a filename match).
//
// The classes below were derived by enumerating the real index (58k models + 150k textures). Two
// findings shaped the matcher, both worked out from the data rather than assumed:
//   • CATEGORY is anchored to the PRIMARY class — the segment right after "models"/"textures" — so a
//     monster-themed shield at art/textures/items/armour/shields/monsters/... counts as Items (its
//     real class), not Monsters. Matching "monsters" anywhere in the path produced 251 such false
//     positives; anchoring removes them.
//   • ITEM TYPE spelling differs by side: models use items/armours, items/weapons (plural); textures
//     use items/armour, items/weapon (singular). So each item-type facet carries both spellings.
// The shader-family / workflow classification is authored too, but lives INSIDE each of the 202k
// .mat files, so it can't be a path test. It is swept once into a cached MaterialFamilyIndex (built
// on a background thread after the index) and exposed here as the "Shader" facet group (workflow +
// effect chips) plus the #family:/#workflow:/#effect: search tokens. Those facets carry shader=true
// and are matched by the list model against that index, never by matches() below.
namespace Facets {

struct Facet {
    QString id;                       // stable id persisted in settings (never a display index)
    QString label;                    // shown in the funnel and as a chip
    QString group;                    // "Category", "Item type", or "Shader"
    bool    afterRoot = false;        // true: a pattern must start right after a models/textures segment
    QVector<QStringList> patterns;    // match if ANY pattern appears as consecutive segments
    // Shader facets (workflow/effect) are NOT path-based — they classify by the material data inside
    // the asset, resolved through MaterialFamilyIndex, so matches() below never matches them; the
    // list model evaluates them against that index instead. `bit` selects which mask bit to test.
    bool    shader = false;
    bool    effect = false;           // shader facet kind: workflow (false) vs effect-layer (true)
    quint8  bit = 0;                  // MaterialFamilyIndex::WorkflowBit / EffectBit
};

inline QString shaderGroup() { return QStringLiteral("Shader"); }

inline QVector<Facet> all()
{
    auto cat = [](const char* id, const char* label, const char* cls) {
        return Facet{QString::fromLatin1(id), QString::fromLatin1(label), QStringLiteral("Category"),
                     true, {{QString::fromLatin1(cls)}}};
    };
    auto item = [](const char* id, const char* label, std::initializer_list<const char*> subs) {
        Facet f{QString::fromLatin1(id), QString::fromLatin1(label), QStringLiteral("Item type"), false, {}};
        for (const char* s : subs) f.patterns.push_back({QStringLiteral("items"), QString::fromLatin1(s)});
        return f;
    };
    auto shader = [](const char* id, const char* label, bool effect, quint8 bit) {
        Facet f{QString::fromLatin1(id), QString::fromLatin1(label), QStringLiteral("Shader"), false, {}};
        f.shader = true; f.effect = effect; f.bit = bit;
        return f;
    };
    return {
        // ── Category: the PRIMARY class folder (segment after art/models or art/textures) ──
        cat("cat.items",       "Items",        "items"),
        cat("cat.monsters",    "Monsters",     "monsters"),
        cat("cat.characters",  "Characters",   "charactersfour"),
        cat("cat.npc",         "NPCs",         "npc"),
        cat("cat.pet",         "Pets",         "pet"),
        cat("cat.effects",     "Effects",      "effects"),
        cat("cat.terrain",     "Terrain",      "terrain"),
        cat("cat.environment", "Environment",  "environment"),
        cat("cat.doodads",     "Doodads",      "doodads"),
        cat("cat.chests",      "Chests",       "chests"),
        cat("cat.portals",     "Portals",      "portals"),
        cat("cat.mapdevices",  "Map devices",  "mapdevices"),
        cat("cat.interface",   "Interface",    "interface"),
        // ── Item type: subfolder under items (both models 'armours' and textures 'armour' spellings) ──
        item("item.armours",         "Armours",          {"armours", "armour"}),
        item("item.weapons",         "Weapons",          {"weapons", "weapon"}),
        item("item.offhand",         "Off-hand",         {"offhand"}),
        item("item.quivers",         "Quivers",          {"quivers", "quiver"}),
        item("item.backattachments", "Back attachments", {"backattachments"}),
        item("item.quests",          "Quest items",      {"quests"}),
        // ── Shader: the material workflow / effect layers, from MaterialFamilyIndex (docs §6). Not a
        //    path test — classifies a model by the .mat data its .sm references. Only models carry
        //    these; textures never match. ──
        shader("wf.metalrough",   "Metal-rough",           false, MaterialFamilyIndex::WfMetalRough),
        shader("wf.dielectric",   "Dielectric spec-gloss", false, MaterialFamilyIndex::WfDielectric),
        shader("wf.specgloss",    "Spec-gloss",            false, MaterialFamilyIndex::WfSpecGloss),
        shader("wf.other",        "Other / effect",        false, MaterialFamilyIndex::WfOther),
        shader("fx.sss",          "Subsurface (SSS)",      true,  MaterialFamilyIndex::FxSSS),
        shader("fx.translucency", "Translucency",          true,  MaterialFamilyIndex::FxTranslucency),
        shader("fx.fur",          "Fur",                   true,  MaterialFamilyIndex::FxFur),
        shader("fx.alphatest",    "Alpha-tested",          true,  MaterialFamilyIndex::FxAlphaTest),
    };
}

inline QStringList groups() { return {QStringLiteral("Category"), QStringLiteral("Item type"), shaderGroup()}; }

// Do `pathSegs` contain `pat` as consecutive segments starting at index `start`?
inline bool patternAt(const QStringList& pathSegs, const QStringList& pat, int start)
{
    if (start + pat.size() > pathSegs.size()) return false;
    for (int k = 0; k < pat.size(); ++k) if (pathSegs[start + k] != pat[k]) return false;
    return true;
}

// Does `path` match this facet? A folder-structure test on authored data, never a filename substring.
// afterRoot facets (Category) require the pattern to begin right after a "models"/"textures" segment
// (the asset's primary class); others match the pattern anywhere as consecutive segments.
inline bool matches(const Facet& f, const QStringList& pathSegs)
{
    if (f.shader) return false;   // shader facets classify by material data, not the path (see model)
    for (const QStringList& pat : f.patterns) {
        if (pat.isEmpty()) return true;
        if (f.afterRoot) {
            for (int i = 0; i + 1 < pathSegs.size(); ++i)
                if ((pathSegs[i] == QStringLiteral("models") || pathSegs[i] == QStringLiteral("textures"))
                    && patternAt(pathSegs, pat, i + 1)) return true;
        } else {
            for (int i = 0; i + pat.size() <= pathSegs.size(); ++i)
                if (patternAt(pathSegs, pat, i)) return true;
        }
    }
    return false;
}
inline bool matches(const Facet& f, const QString& path)
{
    return matches(f, path.split(QLatin1Char('/'), Qt::SkipEmptyParts));
}

inline Facet byId(const QString& id)
{
    for (const Facet& f : all()) if (f.id == id) return f;
    return Facet{};
}

// Self-test (template §4 discipline). Empty string on success.
inline QString selfTest()
{
    const QString robe  = QStringLiteral("art/models/items/armours/bodyarmours/foo/bar_ab12.smd");
    const QString sword = QStringLiteral("art/models/items/weapons/onehand/sword_cd34.fmt");
    const QString swordTex = QStringLiteral("art/textures/items/weapon/onehand/sword__colour.dds");
    const QString mon   = QStringLiteral("art/models/monsters/genericbiped/skel_ef56.smd");
    const QString monShield = QStringLiteral("art/textures/items/armour/shields/monsters/blackguardshield/x_colour.dds");
    const QString envTex = QStringLiteral("art/textures/environment/act1/rock_12.dds");
    const Facet items = byId(QStringLiteral("cat.items")), monsters = byId(QStringLiteral("cat.monsters"));
    // Items category matches gear on both model and texture sides.
    if (!matches(items, robe) || !matches(items, sword)) return QStringLiteral("Facets: items should match gear");
    if (matches(items, mon)) return QStringLiteral("Facets: items must not match a monster model");
    // Anchoring: a monster-themed shield is Items (primary class), NOT Monsters.
    if (!matches(items, monShield)) return QStringLiteral("Facets: shield should be Items");
    if (matches(monsters, monShield)) return QStringLiteral("Facets: anchored monsters must NOT match a shield under items/.../monsters");
    if (!matches(monsters, mon)) return QStringLiteral("Facets: monsters should match a real monster model");
    // Item type, both spellings.
    if (!matches(byId(QStringLiteral("item.armours")), robe)) return QStringLiteral("Facets: armours should match robe (plural)");
    if (!matches(byId(QStringLiteral("item.weapons")), sword)) return QStringLiteral("Facets: weapons should match sword model (plural)");
    if (!matches(byId(QStringLiteral("item.weapons")), swordTex)) return QStringLiteral("Facets: weapons should match sword texture (singular)");
    if (matches(byId(QStringLiteral("item.armours")), sword)) return QStringLiteral("Facets: armours must not match a weapon");
    // Category on a texture path; whole-segment (not substring) matching.
    if (!matches(byId(QStringLiteral("cat.environment")), envTex)) return QStringLiteral("Facets: environment should match texture");
    // "item" (singular, no s) must not match the "items" segment.
    { Facet bad{QStringLiteral("x"),QStringLiteral("x"),QStringLiteral("g"),false,{{QStringLiteral("item")}}};
      if (matches(bad, robe)) return QStringLiteral("Facets: must match whole segments, not substrings"); }
    if (!byId(QStringLiteral("nope")).id.isEmpty()) return QStringLiteral("Facets: unknown id should be empty");
    // Shader facets exist, live in the Shader group, and NEVER match by path (they classify by
    // material data through MaterialFamilyIndex — see AssetListModel::facetHit).
    const Facet wf = byId(QStringLiteral("wf.metalrough"));
    if (wf.id.isEmpty() || !wf.shader || wf.group != shaderGroup()) return QStringLiteral("Facets: wf.metalrough should be a Shader facet");
    if (matches(wf, robe)) return QStringLiteral("Facets: a shader facet must never match by path");
    if (!groups().contains(shaderGroup())) return QStringLiteral("Facets: Shader group must be listed");
    return QString();
}

}  // namespace Facets
