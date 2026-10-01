# POE2AssetBrowser — Session Rules

Native C++17 / Qt6 / OpenGL asset browser for **Path of Exile 2**. MSVC 2022, CMake+Ninja preset
`release`, vcpkg. Built on the AssetBrowser family design — the authority is
`docs/ASSETBROWSER_TEMPLATE.md` in the sibling `Diablo4AssetBrowser Native` repo, summarized by the
`assetbrowser-design` skill. Follow that template's tiers and conventions.

The user builds on Windows with `rebuild.bat` (→ `build_log.txt`, errors distilled to
`build_errors.txt`) and runs via `run.bat`. **You never build on their machine; they rebuild and
report.** In a cloud/Linux session you CAN compile-check the whole tree with
`tools/verify_container.sh` (distro Qt6) and run it headless under `xvfb-run` + software GL.

## The measured ground truth is `docs/FORMATS.md`

Every format fact was measured off the real install, with the method stated, and it distinguishes
**present · unnamed · absent**. Do not guess a layout — read that file, and if it's unsure, say so
rather than filling the gap. The Python reference parsers in `tools/formats/` are the oracle the C++
parsers were validated against; when you change a parser, re-check it against them.

## Non-negotiable working rules (from the family)

1. **Root cause before fix.** State the cause and the evidence before editing. Low confidence →
   add a measurement (a dump, a self-test case), don't guess.
2. **Classify by authored data, never a name substring** (template §3.8). Texture role = the `.mat`
   parameter name; material = the `.sm`/`.fmt` string table; never the file name. `verify-src.py`
   counts name-substring matches — keep it at what it is unless a genuine text test is added.
3. **Fail closed and report.** A file the tool can't read is *unnamed*/*unsupported version N*, said
   out loud — never drawn as garbage. One bad asset reports; it never takes the app down (the
   parsers throw a bounded Underrun that becomes a clean error).
4. **One matcher, one setting, one key.** All filtering goes through `util/QueryTerm.h` (with its
   startup self-test). One QSettings key per state; persist stable ids/strings, restore with
   findData.
5. **Verify before replying.** `verify-src.py` clean; `tools/verify_container.sh` links; the startup
   self-tests pass; and for a format change, the C++ still matches `tools/formats/`.

## Layout

```
src/bundle/   Bundle + BundleIndex + Oodle  — opens Bundles2, decompresses, rebuilds paths
src/tex/      DdsImage (+ bcdec)             — DDS/BC decode
src/model/    MeshParser (smd/fmt), AssetText (sm/mat/ao), AstSkeleton, GlbExporter, RigMath
src/store/    AssetStore + IndexLoader       — the engine-specific glue; the background index build
src/index/    AssetListModel                 — the shared list, filtered through QueryTerm
src/gl/       GLModelWidget + GLTextureWidget — the ONE shared viewport, and the texture viewport
src/tabs/     ModelsTab + TexturesTab
src/app/      MainWindow, Config, AppPaths, SehGuard, LogConsole
third_party/  ooz (Oodle, unmodified upstream + our stdafx shim), bcdec (MIT)
```

## Next work (tiers, roughly in order)

DONE: PBR material into the viewport shader + export — metallic-roughness BRDF with tangent-space
normal mapping, channel packing per docs/FORMATS.md §6.1 (normal RGB + alpha-gloss → roughness,
Metal_TEX → metalness, glow → emissive). The viewport and the .glb share `AssetStore::resolveMaterials`
so the packing has one source of truth. Verified: shaders compile/link/render (textured + untextured)
under llvmpipe; store yields full-res base/normal/MR; valid PBR .glb round-trips.

DONE: animation playback in the viewport — CPU skinning driven by a pose evaluator
(`AstSkeleton::skinMatrices`/`clipDuration`, math helpers in `RigMath.h`: quatToMat / composeTRS /
decomposeTRS / quatNlerp). A clip bar (clip selector · play/pause · scrub timeline) appears under the
viewport for skinned meshes. IMPORTANT fail-closed rule: playback is offered ONLY when the loaded
skeleton actually covers the mesh's joint palette (`skeletonMatchesMesh()`), because body-armour
pieces are skinned to a character rig they do not carry (docs/FORMATS.md §7) — those show at bind pose
with a status note, never animated against a mismatched skeleton. `loadSkeletonFor` resolves the
skeleton as: co-located .ast first, then (when that does not cover the palette AND the model is under
/items/armours/) the shared player base rig art/models/charactersfour/onerig.ast — VERIFIED as the
single universal player body rig for all attribute types (str/dex/int share one 47-joint palette,
onerig covers each 0-oob at correct-rig skinning quality; no per-type rig exists — tools/rig_cover.cpp). Verified: TRS roundtrip + bind-pose-identity + single-key-clip self-tests; bind pose
reproduces the static mesh exactly (0.0 dev) on 5 real meshes; a synthetic 0→90° bone rotation traces
the exact arc (2.707,0.707 at 45°); live GLModelWidget renders bind vs animated frames that differ;
mismatched rigs (the tattered robe) correctly report no clips.

DONE: bulk extraction (template §14 + §15 + §25). A **Bulk** tab filters the whole index through the
shared QueryTerm matcher (`.smd`/`.fmt`/`.dds`), shows a live match count (the count you see is the
set you export), and runs a background `BulkExtractor` worker: models → `.glb`, textures → `.png`,
into the layout chosen in `util/ExportLayout.h` (Flat · By type · Mirror game folders · Folder per
model — the ported D4 mechanism, PoE2 taxonomy = the game's own path tree). Fail-closed per §25:
only-new via `_bulk_manifest.json`, failures to `_bulk_failed.txt` with reasons, working Pause
(paused time excluded from ETA) and Cancel/Esc. `export/folderLayout` is a stable string id
(findData, fail-to-Flat). ExportLayout has a startup self-test. Verified: self-test; headless run
(export + manifest + only-new skip + Type-layout routing + failures file); live BulkTab widget ran
74 textures end-to-end on a background thread; Cancel stops a run in ~100ms and restores the UI.
DONE (§25 parallel workers): the run now walks the matched set across N worker threads (Bulk tab
"Threads" spinbox, 0 = auto = idealThreadCount, `export/bulkWorkers`; clamped [1,32]). Safe because
AssetStore reads are thread-safe — the bundle-handle cache mutex is released before the read,
`Bundle::readRange` serialises per bundle on its own mutex, and the Oodle decoder is reentrant —
while the heavy per-item work (decode + assemble + write) runs in worker-local memory. A coordinator
loop on the run thread owns pause-time accounting, throttled progress, periodic manifest saves and
all signal emission. Ledger (manifest/counters/fail-log) is mutex-guarded. Two subtle COW hazards
found and fixed during verification (an earlier note claimed the store "serialised reads behind one
mutex" — it never did; that mutex only guards the handle cache): `m_items[i]` (non-const QVector
operator[] detaches → use `.at()`), and a shared QString progress label + QJsonObject manifest copy
crossing the lock boundary (→ atomic last-index + serialise-under-lock/write-unlocked). Verified in
container: TSan clean (0 races), byte-identical output vs a single worker over a real 80-item set,
0 crashes across 12+ stress runs, real 1.5–1.7× on a 2-core box (tools/bulk_verify.cpp).

DONE: "Explain material" report (template §18) — right-click a Models-tab row → a plain-text report
(src/report/MaterialReport). States where the material roster came from (.sm / .fmt), each material's
present/ABSENT status, every AUTHORED texture role from the .mat and whether it resolves in the index,
and — the point — which glTF PBR values are AUTHORED vs SUBSTITUTED vs INTERPRETED (gloss→roughness).
Deliberately does NOT fabricate a global cross-reference count. Verified on the robe (MetalRoughBN +
SSS, 4 roles, metalness authored) and the spikerant effect (DielectricSpecGloss → metalness correctly
reported as substituted 0, matching the fxgraph name).

NUANCE surfaced by the report (now HANDLED): PoE2 has at least two material workflows —
MetalRoughBN (metal-rough, what §6.1 packing was measured on) and DielectricSpecGloss (spec-gloss).
For dielectric spec-gloss the metalness-0 + gloss→rough handling was already correct, and a true
spec-gloss that authors a specular-colour map is now unpacked properly: AssetText parses
SpecularColour_TEX (roles SpecularColour/SpecularColor/SpecColour/SpecColor, never confused with
SpecularMask or albedo's "colour" key), AssetStore loads its RGB into em.specularColor (preferred over
the albedo-alpha-derived spec colour), the viewport shader reads it as F0 (uSpecColor sampler, sRGB,
f0 = uHasSpecColor==1 ? texture(uSpecColor,uv).rgb : mix(vec3(0.04),albedo,metal)), and GlbExporter
emits it as KHR_materials_specular specularColorTexture. MaterialReport calls it out as an AUTHORED
spec-colour line. Verified on metadata/pet/sandworm (family SpecGloss, specColorTex resolved) and the
shader compiles+links under llvmpipe. The fxgraph parents (shown by the report) remain the authored
signal for which workflow a material uses.

DONE: funnel / facets / chips (template §9) — a funnel-button popup that stays open while you tick
grouped tags, on both the Models and Bulk tabs. Tags are derived from AUTHORED data (the Bundles2
path tree), never file-name substrings: index/Facets.h defines a Category group (anchored to the
PRIMARY class — the segment after models/textures, so a monster-themed shield is Items not Monsters)
and an Item-type group (matching both the models 'armours'/'weapons' and textures 'armour'/'weapon'
spellings). index/FunnelFilter is the shared control (grouped checkboxes with live counts, Match-any
OR toggle, removable chips, funnel tint); AssetListModel gained setFacets/facetCounts and a combined
applyFilters() that rebuilds once per keystroke. Selections persist per tab (facets/models,
facets/bulk). Facets has a startup self-test (now 8 in main.cpp). Verified: self-test (incl. the
anchoring and both spellings); real-index counts (anchoring corrected doodads 22440→687, effects
10646→4400, and armours picked up the textures side 23727→28477); live BulkTab — ticking Items+Armours
filtered to exactly 28477 (Items∩Armours), two chips shown, selection persisted.
Taxonomy evidence (from the real index): art/models/* = terrain, items, monsters, effects, pet,
chests, npc, portals, mapdevices, charactersfour, misc; art/textures/* = interface, environment,
items, monsters, misc, pet, doodads, npc, general; items/* = armours, weapons, offhand, quivers,
backattachments, quests.

DONE: shader-family facet (the "shader" half of §9). store/MaterialFamilyIndex sweeps every .mat →
{family stem, workflow, effect flags} and every .sm → the model it skins inherits the UNION of its
materials' classification (multiple .sm can skin one .smd — merge, never overwrite, or families are
lost). Cached to material_index_v1.bin keyed on the index fingerprint (BundleIndex pattern), built on
the same background thread right after the index (IndexLoader::materialsReady). AssetListModel takes
the index as its `#`-token metadata source and backs a new "Shader" funnel group: chips Metal-rough /
Dielectric spec-gloss / Spec-gloss / Other-effect + SSS / Translucency / Fur / Alpha-tested, and
search tokens #family:<stem>, #workflow:<name>, #effect:<name>. Shader facets carry shader=true and
are matched against the index, never by path (Facets::matches short-circuits them). The Shader group
shows only on the Models tab; the Bulk/Textures funnels drop it (a workflow chip would silently hide
every .dds), though Bulk still honours the search tokens. Data validated against all 202,606 real
.mat (tools/mat_survey.cpp): 50,088 carry a Materials/* family, the other 152,518 are genuine
effect/VFX materials (no surface family) — an empty family is correct, not a gap. Coverage: all
31,585 .smd classified (100%); the 26,970 .fmt (static meshes, no .sm) are NOT yet classified — a
clean follow-up (their materials are embedded in the .fmt, not a .sm). Verified in container:
MaterialFamilyIndex self-test; full build (202,606 mats, 31,585 models, 214 families) with cache
round-trip byte-identical; spot-check of 4,000 .sm vs independent .sm→.mat resolution (0 mismatch);
end-to-end through the real AssetListModel (tools/matfacet_verify.cpp) — #workflow:metalrough=9341,
dielectricspecgloss=4956, specgloss=8938, #effect:sss=3009, alphatest=15741, #family:hair=86, and the
facet chips + facetCounts all agree with independent counts (PASS). Also classifies static .fmt v9
meshes (materials in their own string table, via MeshParser::parseFmt), independently spot-checked
(0 mismatch). The .fmt v4–8 legacy meshes stay unclassified — a clean gap, never a wrong guess.

FORMAT SURVEY (tools/version_survey.cpp, tools/mesh_probe.cpp — first-byte version histogram + DOLm
probe, over the real index): .smd is only v1/v2/v3 (all parsed). .fmt v9 uses the DOLm block the
parser reads; .fmt v4/5/6/7/8 (≈1,600 readable here) are an OLDER pre-DOLm layout with NO DOLm tag —
a different header per version, mostly effect/billboard/terrain-test geometry (art/particles/*/mb/,
light_test, *_splash). .tmd: v5/v6 are binary terrain meshes with a header bbox; v255 (8,901 files)
is NOT binary — it is a UTF-16LE TEXT descriptor ("version 8\n11 11 0 -333.333 …"), terrain-gen
params, not a renderable mesh. Reverse-engineering the .fmt v4–8 / .tmd binary layouts has only the
header bbox as a validation oracle (Blender can't open them), and the content is low-value vs the
characters/armour/monsters already covered — so per "no guessing, mark hunches and ask" this was NOT
built speculatively; it awaits a decision on whether the effort/risk is worth it.

DONE: find-by-id. PoE2 has no SNO — the asset id IS the MurmurHash64 of the path. The shared
QueryTerm matcher now takes an id token in decimal OR "0x…" hex (0x prefix required so hex-looking
words like face/dead stay name searches); AssetListModel's idText carries both the decimal and the
zero-padded 16-hex form, so either resolves the asset. The "Explain material" report prints the id
(hex + decimal) for cross-referencing with GGPK/bundle tooling and pasting back. Verified: QueryTerm
self-test (hex/decimal/negative/word-regression cases) + end-to-end through AssetListModel over the
real index (tools/matid_verify.cpp — 8 models across the tree each resolve by decimal and 0x-hex, PASS).

DONE: true in-game names (docs/FORMATS.md §9). The game's names live in the .datc64 data tables under
data/balance/, NOT in filenames. store/DatFile reads the columnar format (measured: u32 rowCount,
fixed rows, 0xBB boundary, UTF-16 heap; refs are u64 offsets from the boundary; fields pack tightly so
column offsets are BYTE offsets). store/NameIndex resolves the all-authored chain BaseItemTypes.Name
(col@32) ─FK col@124→ ItemVisualIdentity.AOFile (col@16) → .ao → .sm → .smd (the browsed mesh). The FK
column was found by SELF-VALIDATION (the one column that, across all 5,496 items, always links to a
same-category .ao), not a trusted schema — verified exact (DullHatchet→"Dull Hatchet",
RustedCuirass→"Rusted Cuirass", CrudeBow→"Crude Bow"). Shared meshes (base + rune variants + a generic
base) pick the canonical name the model filename carries (rustedcuirass_drop→"Rusted Cuirass", not the
shorter "Garment"); all names stay searchable. Cached (name_index_v1.bin, fingerprint-keyed), built on
the background thread after materials (IndexLoader). AssetListModel gained an "In-game name" column
(hidden on Textures) and folds the names into the search haystack; the id search also gained 0x-hex.
Verified in container (I staged the 3 data-table bundles from the user's install): NameIndex self-test;
build (708 visuals → 667 named models) with cache round-trip; end-to-end through AssetListModel
(tools/name_e2e.cpp — the column shows "Dull Hatchet", search 'dull hatchet' finds the hash-named
mesh, PASS). MONSTERS now covered too (same chain): MonsterVarieties.Name (col@272) + AO ARRAY
(col@56 = u64 count, col@64 = heap offset → list of .ao refs) → .ao → .sm → .smd. DatFile gained
array reads (arrayCount/strFromArray). Column offsets validated in tools/dat_cols.cpp;
FishParasite→"Chyme Skitterer", BloodFever2H→"Blood-fevered Warrior". Placeholder/controller varieties
(Name = Daemon/Invisible/Clone/[ANY MONSTER]) reuse real bodies so they're SKIPPED (else a swampvine
body gets stamped "Daemon"); leading dev tags [DNT-UNUSED]/(DNT) stripped. Monster bodies are shared
more than item meshes (genericbiped), so a shared mesh keeps one representative name + all searchable.
STALE-CACHE BUG (found when user "saw no monster names"): adding monsters didn't bump
NameIndex::kCacheVersion, so the app loaded the old items-only name_index_v1.bin (matching
version+fingerprint) and never re-swept → monsters invisible. FIX: kCacheVersion is now bumped on any
build() content change (v1 items → v2 +monsters+icons → v3 +NPCs); main.cpp prunes old versions.
ITEM ICONS now named (Textures tab): ItemVisualIdentity.DDSFile (col@8) IS the icon .dds → maps
straight to the item name, no chain (currencyweaponquality.dds→"Blacksmith's Whetstone"); TexturesTab
un-hides the In-game name column + wires setNameIndex. NPCs now DONE (not the col@67 dead-end): an
NPC's Id (NPCs col@0) is a MonsterVariety Id → match by STRING, that variety's AO array is the NPC's
model, NPC display Name = NPCs col@8. Verified end-to-end: Una→art/models/npc/una/rig_befeec1b.smd,
Dannig→dannig_armour. So names cover ITEMS + ITEM ICONS + MONSTERS + NPCs (~4239 named assets:
2028 models + 2750 icons in the staged subset; more on a full install). Research tools:
tools/dat_probe, dat_link, dat_cols (row:/arr:/fkto: modes), ao_chain, npc_link, name_verify,
name_e2e, pathgrep. On a real install the data bundles are always present, so names just work; if
absent, names are disabled, never fabricated.

"VERTEX EXPLOSION" now FIXED (user screenshot of treasurehunter coat): a v1/v2 .smd stores each
mesh's part boundary as a TRIANGLE offset, but parseSmd split parts treating it as a raw INDEX offset,
so every part after the first re-grouped its indices into the wrong triples → shattered triangles
(silhouette + bbox stay correct, which is why earlier bbox/skeleton checks all passed — the tell is
that the part index counts aren't multiples of 3). Fix: `bounds[m] = triStart*3` (clamped monotonic/
in-range). Proven on coat + sin (max triangle edge 74%→13% / 355→49 of the model diagonal) and by a
headless render (tools/render_offscreen — renders the REAL viewport shader offscreen to a PNG; a new
permanent visual-verification tool alongside skin_diag/rig_cover). Affects multi-part v1 meshes (many
NPC/attachment models), viewport AND export. NOTE the diagnosis lesson: my bbox/extent/skeleton
diagnostics were blind to triangle CONNECTIVITY — always test per-triangle edge compactness for
"explosion" reports, and render_offscreen is now the fastest way to actually SEE a mesh headless.

ALPHA TRANSPARENCY now DONE: the viewport only did a hard alpha-test cutout (discard<0.35) with NO
blending, and it discarded on albedo alpha UNCONDITIONALLY — wrong for the AlbedoSpecMask family where
that alpha is a spec mask (punched holes in solid surfaces). Added AssetText::AlphaMode {Opaque, Mask,
Blend, Additive} read from the authored Force* graph (measured on sin: ForceAdditive→Additive,
ForceNoZWriteAlphaBlend→Blend, ForceAlphaTest*→Mask). Viewport: uAlphaMode uniform, discard ONLY for
Mask, two-pass draw (opaque/mask first with depth write, then Blend/Additive with depth test on +
depth write off; additive = SRC_ALPHA,ONE, blend = alpha-over). Export: alphaMode carried on
ExportMaterial → glTF MASK/BLEND (glTF has no additive → BLEND). Verified: mat_dump modes correct,
shader compiles under llvmpipe, sin glb materials MASK/BLEND/BLEND, and render_offscreen shows sin's
cyan additive eye-glow + blended soul compositing. Files: AssetText.h/.cpp, AssetStore.cpp,
GlbExporter.h, GLModelWidget.h/.cpp, ModelsTab.cpp, MaterialReport.cpp. User must rebuild.bat.

FOUR SEPARATE PANELS (tabbed) now DONE (user chose tabbed strip): ModelsTab's old Parts+Info splitter
is now a QTabWidget — Parts (table) · Animations (QListWidget of clips, row 0 = bind pose, click plays)
· Attachments (assembly, below) · Info (scrollable). AttachmentsForModel + the anim list mirror the
clip bar.

ATTACHMENT ASSEMBLY now DONE (user chose "assemble in viewport"): a body's .ao has
`AttachedAnimatedObject { attached_object = "<bone> <child.ao>" }` (coat→hip_jntBnd,
feathers→aux_Head_attachment). AssetText::parseAo ALREADY parses `attachments` (bone + child .ao) —
reused. AssetStore::attachmentsForModel(bodySmd) reverse-maps mesh→.ao: findSmForSmd(body) gives the
body .sm, then a SCOPED search (.ao paths containing the model's last two dir segments) CONFIRMED by
ao.smPath==body.sm (search-then-validate, not a name guess) finds the body .ao; each attachment child
.ao → SkinMesh .sm → .smd is resolved to a drawable mesh + label (folder name e.g. "coat"). Viewport:
GLModelWidget gained setAttachments/setAttachmentVisible/clearAttachments — each attachment's verts are
BAKED into the body's Y-up frame at its parent bone's bind transform (bone matched by NAME in m_skel)
and drawn as extra groups through the SAME two-pass alpha path (drawPart/passOf now take a materials
vector; groups = body + visible attachments). Attachments tab lists each piece with a show/hide check;
unresolved meshes are listed but greyed. Tools: tools/attach_test (prints a body's assembly). Files: AssetStore.h/.cpp,
GLModelWidget.h/.cpp, ModelsTab.h/.cpp.

ATTACHMENT ANIMATION-FOLLOW + NPC SKELETON RESOLUTION now DONE (2026-09-14, follow-up): (1) attachments
now RIGIDLY FOLLOW the body animation — GLModelWidget::reskinAttachments re-bakes each piece per frame
at its bone's ANIMATED world matrix = RigMath::mul(bind[b], skin[b]) (row-vector; = bind[b] at rest, so
bind pose is unchanged); called from uploadSkinnedFrame. Rigid follow only (no cloth sim). (2) NPC
SKELETON RESOLUTION: many NPC bodies keep their rig in an animations/ SUBDIR that loadSkeletonFor's
dir-scan missed → they showed bind-pose only. Now when the dir-scan is empty, loadSkeletonFor consults
the body .ao's declared ClientAnimationController.skeleton (via findBodyAo — the scoped .ao search,
factored out and shared with attachmentsForModel; AnimatedObject gained selfPath, Assembly gained
skeletonAst). Evidence-based (the .ao names the rig, not a guess). VERIFIED: treasurehunter_03 body now
loads 85 bones + 8 clips (was 0), and render_offscreen at portal_opening_01 t=4s shows the coat
following the hips through the animated pose. Files: AssetStore.h/.cpp, AssetText.h (selfPath),
GLModelWidget.h/.cpp, tools/render_offscreen (gained clipIndex+time args). User must rebuild.bat.

EXPORT OPTIONS + MULTI-SELECT now DONE (2026-09-14, user: "flesh out export options and multi-select"):
(1) At-export options dialog (src/app/ExportOptionsDialog.h/.cpp, in CMakeLists + verify_container.sh)
shown on Ctrl+E / context-menu export — scale, format (.glb / .gltf+.bin), animations (all / current
clip only / none), skeleton, include-attachments, yaw180, embed-textures, reconstruct-normal-Z; prefilled
from Config and saved back (new Config keys exportIncludeAttachments, exportGltf). (2) MULTI-SELECT
export: the Models list is ExtendedSelection; ModelsTab::selectedModelPaths() + exportOne() export each
selected .smd/.fmt as its own file into a chosen FOLDER (collision-suffixed), one options dialog for the
batch; single-select still uses a save-file dialog + viewport part-subset. (3) ANIM SELECTION:
GlbExporter::Options.onlyClip (-1 all, >=0 that clip); exporter filters clips. (4) .gltf+.bin: write()
splits the built .glb into text .gltf + external .bin (buffer uri) when the path ends .gltf — images stay
bufferView-refs, resolve through the .bin. (5) INCLUDE ATTACHMENTS: AssetStore::assembleForExport bakes
each attachment at its bone's bind and weights it 100% to that bone, MERGED into the body geometry, so the
PROVEN skinning path exports an animated assembled character (no new coordinate math). Verified headless:
anim 26→1 clip; .gltf+.bin + real-Blender import; assembled export (body+coat+feathers, skin-check
0.000000, Blender 8 actions); all four compose (assembled+.gltf+single-clip imports clean). Files:
GlbExporter.h/.cpp, AssetStore.h/.cpp, Config.h/.cpp, ExportConfig.h, ExportOptionsDialog.h/.cpp,
ModelsTab.h/.cpp, CMakeLists.txt, tools/export_model (--clip/--attach). NOTE new source added → if
rebuild.bat doesn't pick up ExportOptionsDialog, run build.bat (CMake reconfigure). All 14 files md5-
verified on disk after the earlier silent-write scare.

Remaining: (pending user decision, my rec = skip) .fmt v4–8 + .tmd binary parsing; the showpiece tab
(§33). Everything else in the user's requests is DONE. Possible polish (not requested): attachments
following body animation; body bind-pose orientation for some NPCs is head-down (orbit handles it).
