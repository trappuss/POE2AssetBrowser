# Changelog

## v0.9.0 — first public preview (extremely early / alpha)

> **This is an extremely early version.** It is a preview, not a finished tool: broad in scope but
> rough in places, with gaps it names honestly (see *Known limitations*). The version number is
> historical — read it as "early preview", not "almost 1.0". Formats, settings and caches may change
> between versions.

The first drop that is a browser rather than a foundation: it opens the game in place, names what it
finds, draws it with real materials, animates it, assembles characters, and exports it in the formats
people actually use.

### Reading the game
- Opens `Bundles2` directly — Oodle-decompresses bundles (vendored open-source `ooz`), reads
  `_.index.bin`, and rebuilds all ~1.4 million asset paths (MurmurHash64A), cross-checked file-for-file
  against a reference parser.
- **Real in-game names** for items, icons, monsters and NPCs, read from the game's `.datc64` data
  tables — search by the name you know. Encrypted paths are shown as *unnamed*, never guessed.
- **Health check** distinguishes present / unnamed / absent, so "why is X missing" has an honest answer.

### Viewport
- Shared 3D viewport with flat / shaded / **full metallic-roughness PBR** / wireframe, a per-channel
  viewer (normal, roughness, metallic, AO, emissive), and orbit-pan-zoom.
- Materials resolved from the game's own graphs — MetalRough, Dielectric-spec-gloss and spec-gloss-mask
  families, the two normal-map packings, alpha modes (opaque / mask / blend / additive), and subsurface
  tint. **Explain material** reports which values are authored versus substituted.
- **Part selection as a set** with a viewport right-click menu: frame, isolate (hide the rest), export
  selected parts, copy name. Four side panels — Parts, Animations, Attachments, Info.
- **Grid / thumbnail view** on the Models and Textures tabs: rendered model icons (flat, or an optional
  base-colour-textured mode), resizable tiles, and a card-style hover preview you can scroll to resize.
- **Animation playback** (scrub, play/pause), failing closed on a body piece skinned to a rig it does
  not carry rather than animating against the wrong skeleton.
- **Attachment assembly** — a body with its coat / hat / weapons on the right bones, following the
  animation, toggleable per piece.

### Customize tab (character dressing room)
- Pick a class, assemble an outfit from real armour sets and per-slot pieces, toggle hair / head /
  equipped-item FX, randomise, and save / load outfit presets.
- **Player animation library.** The base rig carries only a static idle; the real player motion lives
  in ~112 per-move animation files (sprint, weapon attacks, skills, dodge, death, revive…). A "Move"
  selector lists them, and the chosen clip is retargeted onto the assembled character by bone name and
  played on the fully-skinned figure. Clips are decoded one at a time, so a 150-clip move set never
  costs more than the clip in play. The same move library is available on the **Models** tab for any
  mesh that loads on the player base rig (armour pieces), gated by bone-name match so monsters and
  props never get player moves.
- Export the whole figure as one file, without weapons, or each equipped item separately.

### Textures
- Decodes every `.dds` (BC1/BC7/…) in-tool, with channel isolation, an alpha-over-checker view, a pixel
  inspector, a hover preview, and PNG / raw-DDS export.

### Export — one Export menu, each with multi-select
- **Models** → `.glb` or `.gltf + .bin`: geometry, skeleton, inverse-bind matrices, animation clips and
  a full metallic-roughness material set; optionally the assembled character.
- **Selected parts** → just the parts picked in the viewport.
- **Preview image** → PNG at 25–400 % (re-rendered larger, not upscaled), transparent background,
  crop-to-model.
- **Turntable GIF** (orbits the model centre) and **Animation-loop GIF**, with a size-budget optimiser.
- **Original-format / raw** extraction of the authored bytes, non-destructively.
- **Bulk** extraction of thousands of assets in one parallel run — folder-layout choices, an only-new
  manifest, live progress, a working pause / cancel, and a raw-originals mode.

### Fit and finish
- A funnel of tags drawn from the game's own folder tree, a shader-family facet, and a search language
  (`space` = AND, `-` exclude, `a|b` OR, a number = find by id).
- A tabbed Settings dialog with rebindable hotkeys, an in-app **F1** cheat-sheet, and a twelve-page
  wiki manual.
- Portable: everything it writes lives in a `data\` folder beside the exe (INI settings, version-stamped
  caches). No game assets or keys ship in this repo.

### Known limitations
- **Alpha quality** — rough edges and bugs are expected; settings/cache formats may change.
- Legacy `.fmt` versions 4–8 and `.tmd` terrain geometry are not decoded yet (the tool names the version
  it could not read).
- Some full-character NPCs load in a head-down bind pose — that is how their rig is authored; orbit to
  view them. Items and gear are unaffected.
- Animated worn pieces are merged and weighted rigidly to their parent bone — they move with the body,
  but cloth / secondary motion is not simulated.
