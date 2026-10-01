# Path of Exile 2 asset formats — as measured

Everything in this file was measured on the Steam install of Path of Exile 2 on 2026-09-13
(`_.index.bin` 114,989,715 bytes, 62,275 bundles, 4,261,026 files), against the community
documentation named in *Sources* at the end. Where the two disagreed the install won. Where this
file says *unverified*, it means exactly that: nothing in the tool relies on it, and the tool says
so in its Explain report rather than filling the gap.

The reference parsers that produced these numbers live in `tools/formats/` (Python; they are the
oracle the C++ parsers are checked against, not something the tool runs).

---

## 1. Storage: `Bundles2/`

The Steam client ships no `Content.ggpk`. Everything is under `Bundles2/`:

| Path | What |
|---|---|
| `_.index.bin` | The index bundle. One compressed bundle whose payload lists every bundle, every file and the directory table (§2). 115 MB compressed → 149 MB. |
| `_.index.low.bin`, `_.index.high.bin` | 74 bytes each. Not read. |
| `.index.dbg` | 88 MB, not read (debug symbols for the index; not needed to open anything). |
| `Shared.bundle.bin`, `Tiny.V0..VF[.1|.2].bundle.bin` | Root bundles. **Every small file lives in a `Tiny.*` bundle**: all `.mat`, `.sm`, `.amd`, `.ao`, every `*.header`, most `.ast`, and 6,832 small `.dds`. Each Tiny bundle is ~16 MB compressed / 48 MB (`.1` ones 220 MB). |
| `Content/…/<hash>.<asset>.bundle.bin`, `Streaming/Content/…`, `Streaming/Folders/…`, `Folders/…` | Per-asset bundles, named `<64-bit hex>[.<64-bit hex>].<stem>.<ext>` plus optional `.1`, `.2` parts. The full `.smd`/`.fmt`/`.dds` payloads live here. |

A bundle's on-disk path is `Bundles2/<bundle name from the index>.bundle.bin`.

### 1.1 Bundle file layout (measured, matches the ggpk.discussion template)

```
u32 uncompressed_size
u32 total_payload_size            // = sum(block_sizes)
u32 head_payload_size             // = 48 + 4*block_count
u32 first_file_encode             // 12 on every bundle measured (the "Hydra" selector; blocks decide)
u32 unk10                         // 1
u64 uncompressed_size2            // == uncompressed_size
u64 total_payload_size2           // == total_payload_size
u32 block_count
u32 uncompressed_block_granularity// 262144 on every bundle measured
u32 unk28[4]                      // 0,0,0,0
u32 block_sizes[block_count]
u8  blocks[]                      // Oodle-compressed; each decodes to 262144 bytes, the last to the remainder
```

Blocks begin `8C 0C` (Kraken), `8C 06` (Leviathan) or `8C 0A` (Mermaid). **The open-source
`ooz` decoder (powzix/ooz, `Kraken_Decompress`) decodes every block of every bundle read** —
the index, all 34 root bundles and the per-asset bundles used for the corpus — with the exact
byte counts expected. The decoder may write up to 64 bytes past the end of the output; the tool
over-allocates by that.

### 1.2 Path hashing

The first directory record's hash is `0xF42A94E69CFF42FE`, which selects the post-3.21.2 scheme:
**MurmurHash64A, seed `0x1337B33F`, over the lowercase UTF-8 path** (no `++` suffix). Verified:
4,261,021 of 4,261,026 generated paths hash to a file record. The remaining 5 files have no
resolvable path (their generated string hashes to nothing) and are listed as *unnamed*.
Directory hashes are the same hash of the directory path **without** a trailing slash
(`audio/haptics` → `0xA6728264DDB4B5B9`, measured).

The FNV-1a `++` scheme (pre-3.21.2) matched zero files and is not used by this build.

## 2. The index payload

```
u32 bundle_count
{ u32 name_len; char name[name_len]; u32 uncompressed_size }[bundle_count]
u32 file_count
{ u64 path_hash; u32 bundle_index; u32 offset; u32 size }[file_count]
u32 dir_count
{ u64 dir_hash; u32 spec_offset; u32 spec_size; u32 spec_recursive_size }[dir_count]
<a nested bundle (§1.1) holding the path specification, 56.8 MB → 126 MB>
```

Path generation follows the ggpk.discussion algorithm exactly (a zero word toggles between a
*base* phase that builds prefixes and a *generate* phase that emits paths; a non-zero word is a
1-based index into the base list). All 95,807 directory specs together generate exactly
`file_count` paths.

**All generated paths are lowercase.** Text files inside the game reference assets in mixed case
(`"Art/Models/…/TatteredRobe_Mc.mat"`); every lookup lowercases first.

227,592 file records share a payload with another record (same bundle/offset/size) — distinct
paths, one copy of the bytes.

### 2.1 What is in the index (this build)

| Extension | Count | Where |
|---|---|---|
| *(none)* | 2,856,820 | `shadercache*/xx/<md5>` — shader caches, five backends. Not indexed by the tool beyond a count. |
| `.header` | 426,944 | Split headers: 220,767 `.tgm.header`, 147,622 `.dds.header`, 31,585 `.smd.header`, 26,970 `.fmt.header`. Every one lives in a Tiny bundle. |
| `.tgm` | 220,767 | Terrain tile geometry (not parsed yet). |
| `.mat` | 202,606 | Materials — JSON (§6). |
| `.dds` | 150,553 | Textures (§5). 3,295 have no `.header`. |
| `.ao` | 73,461 | Animated-object metadata (§7). |
| `.sm` | 31,762 | Skinned-mesh descriptor (§7). All say `SkinnedMeshData`. |
| `.smd` | 31,585 | Skinned mesh (§3). |
| `.fmt` | 26,970 | Fixed mesh (§4). |
| `.ast` | 17,914 | Skeleton + animation clips (§8). |
| `.amd` | 17,593 | Animation metadata (text, §7). |
| `.tmd` | 9,706 | **Not meshes**: UTF-16 tile-metadata text (`version 8`). |

## 3. `.smd` — skinned mesh

Three on-disk versions, told apart by the first byte. Measured on 1,777 files; all parse with
zero bytes unaccounted for (the tail after the name table is a fixed-shape trailer, §3.4).

### 3.1 Version 3 (912 of 1,777 in the corpus; every character/armour/monster mesh seen)

```
u8   version            = 3
u8   b1                 = 4 (every file)
u16  meshCount          (== the DOLm meshCount)
u32  nameBytesTotal     = sum of the UTF-16 name byte lengths (§3.3)
f32  bbox[6]            minX maxX minY maxY minZ maxZ   ← interleaved, NOT min-xyz/max-xyz
0x20 'DOLm'
u8   layoutVersion      1..4 (bt24 in Aqua's reader) — see §3.3
u8   zero
u8   lodCount           0, 1 or 2 seen
u8   meshCount
u8   zero
u32  vertexFormat       bit flags, §3.2
{ u32 triangleCount; u32 vertexCount }[lodCount]
per LOD:
  { u32 indexStart; u32 indexCount }[meshCount]      // indices into this LOD's index buffer
  index buffer: u16[triangleCount*3], or u32 when vertexCount > 65535
  vertex buffer: vertexCount × stride(vertexFormat)
  [u32 extra] only when layoutVersion >= 4 — always 0 in the corpus (§3.3)
name table (§3.3)
trailer (§3.4)
```

Sanity: `sum(indexCount) == triangleCount*3` and `max(index) < vertexCount` on every file.

### 3.2 Vertex format flags and layout

Field order is bit order descending for the first four; the three low bits are appended after
the weights (measured on 0x23d, 0x3e, 0x27a).

| Flag | Bytes | Content | Measured how |
|---|---|---|---|
| 0x20 | 12 | position `f32 x y z` | matches the bbox |
| 0x10 | 8 | normal `snorm8 x y z w` + tangent `snorm8 x y z w` | mean |n| = 1.000 over 619 files (biased `(b-128)/127` gives 1.21); tangent·normal ≈ 0; tangent w = ±1 (bytes 127/129) |
| 0x08 | 4 | uv `f16 u v` | |
| 0x04 | 8 | bone index `u8×4` + weight `u8×4` | weights sum to 255 (254–256 with rounding) |
| 0x02 | 4 | colour `rgba8` | 0x3e files: constant `1f 1f 1f ff` water tint |
| 0x40 | 4 | unknown `u32` (value 0 or 1) | 0x278/0x27a fmt files only; **the block after the vertices in these files is not fully understood** (§4.3) |
| 0x01 | 4 | second uv `f16 u v` | 0x23d weapon files |
| 0x200 | 0 | no per-vertex data | present on 0x23c/0x238/0x200; meaning unknown |

Shadowth117's reader has 0x01 and 0x02 swapped; the bytes say colour is 0x02.

**Normal/tangent encoding differs by version.** v3 (and every v9 `.fmt`) is signed `int8/127`.
v1/v2 are biased `(byte-128)/127` (normals come out unit-length that way and not the other; the
tangent w byte is 1 or 255 → −1/+1).

**UVs are used as stored, wrapping**: sampling the tattered-robe colour atlas at the mesh's own
UVs lands on opaque texels 96–98% of the time with v as-is, 83% with v negated. Values run
−1..1 (and beyond, for tiling), so `fract()` them. glTF's texture origin is the same corner as
D3D's, so export writes them unchanged.

**Winding**: `cross(v1−v0, v2−v0)` agrees with the stored vertex normals on 99.5% of triangles
over 1,174 files — front faces are counter-clockwise in the native frame.

**Axes**: −Z is up (bboxes of standing characters run z ≈ −170..−110; the hip bone sits at
z = −102). The tool maps native `(x, y, z)` → glTF `(x, −z, y)`, a proper rotation (det +1), so
winding is preserved. Which way a character faces has not been measured.

### 3.3 Name table and layout versions

After the last LOD's vertex buffer (and the `extra` u32 when layoutVersion ≥ 4):

```
u32 nameBytes[meshCount]      // UTF-16LE byte lengths
utf16 names (no terminators)  // e.g. "NecklaceShape", "R_Chest_BonesShape"
```

`nameBytesTotal` in the head equals the sum on every file. Layout versions seen: 1 (16 files),
2 (546), 3 (52), 4 (298). The `extra` u32 exists iff layoutVersion ≥ 4 (all 1,777 files account
to the byte with that rule and not without it). No corpus file has layoutVersion ≥ 4 *and* two
LODs, so whether `extra` is per model or per LOD is **unverified**; the tool reads it once per
model and validates the name table afterwards.

### 3.4 Trailer

`u8 4|3, u8 shapeCount, u16 0`, then a shape section, then zero padding. Files with
`shapeCount = 0` end in exactly 32 bytes (`04 00 00 00` + 28 zeros). Files with shapes carry
sphere-like records (`u32 1; f32×3; u32 1; u32 nameLen; u32 0; ascii name` — e.g.
`collision_sphere_hipShape`). **Not parsed**; the tool stops after the name table.

### 3.5 Versions 1 and 2 (legacy; 865 files, mostly `art/models/effects/*` and MTX)

```
u8  version (1|2)
u32 triangleCount
u32 vertexCount
u8  b9 (4)
u8  meshCount
u8  b11 (0)
u32 nameBytesTotal
f32 bbox[6]              interleaved as above
[v2 only: u32 = 4]
{ u32 nameBytes; u32 triStart }[meshCount]
utf16 names
u16 indices[triangleCount*3]
vertex[vertexCount] — fixed 32-byte layout = flags 0x3c with the biased normal encoding
trailer `04 00 00 00` + 28 zeros
```

**Per-mesh `triStart` is a TRIANGLE offset, not an index offset** (unlike the v3/DOLm `starts[]`,
which are index-unit). Mesh *m*'s indices are `indices[triStart[m]*3 .. triStart[m+1]*3)`. Reading it
as a raw index offset splits parts mid-triangle and re-groups every following index into the wrong
triple — the "vertex explosion" (measured on `treasurehunter_03/.../coat/rig_b14ad7ab` and
`npc/sin/rig_0799337d`: max triangle edge 80 in a 90-unit coat; ×3 → 14.6, all part index runs become
multiples of 3). Single-part meshes are unaffected. Verified with `tools/render_offscreen` (a headless
render of the real viewport shader) and per-part triangle-edge stats.

### 3.6 `.smd.header`

A derived summary that lives in a Tiny bundle (cheap to read without touching the Streaming
bundle): `u32 vertexCount; u32 indexCount; u32 0; f32 bbox[6] (min xyz, max xyz — NOT
interleaved here); u8 1; u32 vertexFormat; u32 0; u32 0; u8 0; u32 lodCount; u32 meshCount; u32 0;
u32 0; u8 0; { u32 indexStart; u32 indexCount; u32 nameLen; u32 0; ascii name }[meshCount]` then
the same trailer as §3.4. The tool uses it for the list (counts, format) and reads the `.smd`
only when the model is opened.

## 4. `.fmt` — fixed mesh

Versions 4–9 by first byte. **Version 9 (2,916 of 4,543) is the only one parsed**; 2,893 of
those account to the byte, 21 carry a flagged extra block the tool does not read (§4.3), 2 have
name tables that fail UTF-16 decoding.

### 4.1 Version 9

```
u8   version = 9
u16  meshCount
u8   locatorGroupCount
u16  locatorPointCount
u8   extraBlockFlag           1 on 21 files (area-transition doodads); §4.3
f32  bbox[6]                  interleaved
0x1f 'DOLm' … identical to §3.1 from layoutVersion onward (LODs, mesh table, indices, vertices,
     extra u32 when layoutVersion ≥ 4)
[if vertexFormat & 0x40: 9 × f32 after the vertex buffer — see §3.2]
u32  zero
per mesh: u32 matOffset; [locators, only on the LAST mesh, §4.2]; u32 regionEnd
utf16 string block, regionEnd chars in total
```

Per mesh, the region `[previousEnd, regionEnd)` of the string block holds `name \0 [material \0]`.
`matOffset` is the character offset of the material string inside the region — equal to
`len(name)+1` when a material follows, `len(name)` when none does (then the region is just the
name and a terminator). Names are Maya DAG paths joined with `|`
(`fixed_Seals_thickwall_CvMed_01|CaveSeal_067|CaveSeal_0Shape67`). Only the first mesh of a group
carries the `.mat` path; later meshes with an empty material *appear* to share it, but that is a
hunch — the tool shows them as *no material declared* until measured.

### 4.2 Locators

When `locatorGroupCount > 0`: `{ u8 1; u8 pointCount; u32 nameOffsetChars }[groupCount]` followed
by `f32 xyz[locatorPointCount]`, placed after the last mesh's `matOffset`. Named locator points
(`orbFX`, `Max1`…). Files with `lodCount = 0` (944) are locator-only: `[groups][points] u32
totalChars, strings`.

### 4.3 Not understood

* `extraBlockFlag = 1` (21 files): 84–88 bytes of floats between `matOffset` and `regionEnd`
  (looks like a portal/camera frame). Names are recovered by scanning the UTF-16 tail.
* Flag 0x40 files: the 9-float block after the vertices fits 15 of 24 files; the rest leave
  4–8 bytes unexplained. Geometry is fine (stride verified); only the string table may fail.
* Versions 4–8 (1,627 files): `u8 ver; u32 tri; u32 nv; u8 meshCount; u32 0; f32 bbox[6];
  u32 0; …` then a mesh table, biased-normal 32-byte vertices and UTF-16 names + `.mat` path.
  Left for a later pass.

### 4.4 `.fmt.header`

Same idea as §3.6 (counts + bbox + names). Not read yet.

## 5. Textures

### 5.1 `.dds`

Standard DDS with the `DX10` extension header on every file measured (6,839 pairs). DXGI formats
seen: 71 BC1_UNORM (3,704), 98 BC7_UNORM (3,123), 99 BC7_UNORM_SRGB, 74 BC2_UNORM, 28/29
R8G8B8A8. All 2D, no arrays or cubemaps in the sample. Mip chains are complete and standard.
File names lie about the codec (`*_dxt5.dds` files are BC7): **the DXGI field decides**.

Decoded with `bcdec` (MIT). BC7 verified visually on the tattered-robe atlas.

### 5.2 `.dds.header`

```
u32 version = 3
u32 width, height, mipCount      // of the FULL texture
u32 gggFormat                    // 205 ↔ BC1, 213 ↔ BC7, 207 ↔ BC2, 21 ↔ RGBA8 (measured pairs)
u32 thisDdsSize                  // bytes that follow
u32 fullDdsSize                  // == size of the sibling .dds
DDS…                             // a 16×16 (or smaller) R8G8B8A8 placeholder with its own mips
```

Every one of the 3,000 sampled headers is version 3 and starts its DDS immediately at byte 28;
none is a `*path` redirect (VisualGGPK3 handles that case for PoE 1; the tool keeps the branch).
The header is how the Textures list knows size/format/mips without opening a Streaming bundle.

## 6. `.mat` — materials

**UTF-16LE JSON, with a BOM on 61% of files and without one on the rest** (a `{` followed by a
null byte). `"version": 4`. Structure:

```json
{"version":4,
 "textures":[{"filename":"Art/…/X_Colour_DXT3.dds","format":"BC7","sources":[…],"count":1}],
 "defaultgraph":{"version":3,"shader_group":["Material"]},
 "graphinstances":[{"parent":"Metadata/Materials/MetalRoughBN.fxgraph",
                    "custom_parameters":[{"name":"AlbedoTransparency_TEX","parameters":[{"path":"…dds","srgb":true}]}, …]}]}
```

The texture ROLE is the `custom_parameters[].name` — authored data, which is what the tool
classifies on (never the file name). Over the 20,252 materials referenced by meshes in the corpus:

| Role name(s) | Count | Meaning |
|---|---|---|
| `base_color_texture`, `AlbedoTransparency_TEX`, `AlbedoSpecMask_TEX`, `AlbedoTransparency`, `AlbedoMetallic`, `Masked_AT_Color` | 10,617 / 6,894 / 1,905 / 843 / 376 / 242 | base colour (+ alpha, spec mask or metal in A) |
| `NormalGlossAO_TEX`, `NormalOcclusionGloss`, `NormalAOGloss_TEX`, `base_normalspec_texture`, `Normal_TEX` | 8,908 / 1,218 / 261 / 1,988 / — | normal map + one packed scalar (§6.1) |
| `Metal_TEX`, `Metallic` | 1,469 / 382 | metalness |
| `SpecularMask_TEX`, `SpecularColour_TEX` | 881 / 659 | spec-gloss family |
| `Glow_TEX`, `Glow_MASK` | 1,365 / 503 | emissive |
| `SSS_TEX`, `01_SSS_TEX`, `SubsurfaceGlow_TEX`, `Translucency map` | 674 / 115 / 611 / 662 | subsurface |

Most common graphs on mesh materials: `BasicColour` (8,640), `DarkenTex`, `ForceAdditive`,
`ForceAlphaTestWithShadow` (4,282 — alpha-tested), `MetalRoughBN` (1,528), `DielectricSpecGloss[BN]`,
`SpecGlossSpecMaskOpaque[BN]`, `MetalRough`, `SSSAdd`, `GlowAdd`.

### 6.1 Shader families and PBR channel packing (measured)

A `.mat` layers one or more `graphinstances`. The **first `Metadata/Materials/*.fxgraph` is the
shader family** (the workflow); later graphs add effects. Measured across character/monster/effect
materials (conquista NPC set decoded channel-by-channel; cross-checked on eyes, weapon, atziri):

**Shader families (workflow):**

| Family fxgraph | Workflow | Metalness | Roughness source |
|---|---|---|---|
| `MetalRough`, `MetalRoughBN` | metal-rough | `SpecularMask_TEX`/`Metal_TEX` (R) | gloss → `1 − gloss` |
| `DielectricSpecGloss[BN]` | dielectric | 0 (KHR_materials_specular) | `UseRoughness` flag → channel is roughness |
| `SpecGlossSpecMaskOpaque[BN]` | spec-gloss | 0 (KHR_materials_specular) | `UsesRoughness` flag; spec mask in albedo **A** |

**Texture roles and packing:**

| Role | Packing (measured) |
|---|---|
| `AlbedoTransparency_TEX` | RGB = albedo (sRGB); A = opacity (alpha-test keys on it) |
| `AlbedoSpecMask_TEX` | RGB = albedo (sRGB); **A = specular mask** (spec-gloss families) |
| `NormalGlossAO_TEX` | **two packings, detected per-texture** (see below) |
| `SpecularMask_TEX` / `Metal_TEX` | grayscale metalness in R; 0 = dielectric |
| `SpecularColour_TEX` | RGB = authored specular colour (F0) for the spec-gloss workflow — an explicit map, not derived from albedo alpha |
| `01_SSS_TEX` (+ `03/04/05_SSS_{R,G,B}_Tint`) | subsurface mask (R) + three shallow→deep depth tints |
| `Translucency map` | translucency mask |
| `Glow_TEX` / emissive | emissive colour |

**`NormalGlossAO_TEX` has TWO packings under the same role name** — decided per texture by whether
`2·RGB−1` is unit length on average (`mean |‖2·RGB−1‖−1|`, threshold ~0.12):

* **RG-normal** (e.g. conquista body): R,G = tangent normal x,y (Z reconstructed `= √(1−x²−y²)`),
  **B = gloss**, **A = ambient occlusion**. Proof: B dips below 0.5 (impossible for a real z ≥ 0),
  RG reconstructs a valid z for 99.4 % of texels, B renders as per-material gloss zones and A as soft
  geometric occlusion.
* **full-normal** (e.g. conquista eyes, weapon): R,G,B = full tangent normal (`z = 2B−1`), **A =
  gloss**, no baked AO. Proof: `‖2·RGB−1‖ ≈ 1.00` (0.04–0.05 error), B ≈ 0.9+ with low variance.

**Mapping to glTF.** The tool builds one ORM texture per material — **R = occlusion, G = roughness
(`1 − gloss`, or gloss directly when `Use(s)Roughness` is set), B = metalness** — and references its
R channel as `occlusionTexture`. It builds a clean normal (full RGB when B is z, else RG +
reconstructed z). Dielectric/spec-gloss families emit `KHR_materials_specular` (metalness forced 0).
When the `.mat` authors an explicit `SpecularColour_TEX`, its RGB is exported directly as the
`specularColorTexture` (and the viewport reads it as F0); otherwise, when the spec mask is packed in
albedo alpha, that alpha is used to build the specular-colour texture instead. `Translucency map` materials emit
`KHR_materials_transmission`. SSS is approximated in the
viewport (its authored depth tints shade the terminator) but has no core-glTF representation, so it
is not baked into the export — the base metal-rough result is exported instead.

**Composite / alpha mode** is read from the authored `Force*` effect graph, not guessed (measured on
real materials, e.g. Innocence/Sin's set): `ForceAlphaTest*` → **Mask** (hard cutout at 0.35, glTF
`alphaMode:MASK`); `ForceAlphaBlend` / `ForceNoZWriteAlphaBlend` → **Blend** (alpha-over, no depth
write, glTF `BLEND`); `ForceAdditive` → **Additive** (energy/glow; the viewport uses a `SRC_ALPHA,ONE`
add, the glTF export approximates it as `BLEND` since glTF has no additive); otherwise **Opaque**. The
viewport draws opaque/mask parts first, then transparent parts with the depth test on but depth writes
off. Crucially the cutout `discard` fires **only** for Mask — it must never key on the albedo alpha of
an Opaque material, because for the `AlbedoSpecMask` family that alpha is a *spec mask*, not opacity
(discarding on it punched holes across solid surfaces).

## 7. Text metadata: `.sm`, `.amd`, `.ao`

All three are **UTF-16LE without a BOM** (`.tmd` has one). CRLF line ends.

`.sm` (version 4/5/6):
```
version 6
SkinnedMeshData "Art/…/TatteredRobeStrInt_faeffb98.smd"
Materials 1
	"Art/…/TatteredRobe_Mc.mat" 7          ← the material and how many consecutive meshes use it
BoundingBox -48.2024 -19.5659 -167.493 34.3365 18.0339 -111.148
BoneGroups 0
```
Material rows map onto the `.smd` meshes in order (`7` = all seven meshes of the robe).

**Locale-variant naming (measured):** a localized mesh is named `<stem>.<locale>.smd` (e.g. Atziri's
Japanese variant `atziriphase2_armour.japan_0234604c.japan.smd`), but its `.<locale>.sm` descriptor
references the mesh WITHOUT the trailing `.<locale>` infix (`atziriphase2_armour.japan_0234604c.smd`).
So the `.sm`→`.smd` link is not always an exact string match: `AssetStore::findSmForSmd` first tries
the exact path, then falls back to the mesh path with its pre-`.smd` filename segment removed
(delocalized). Without this, a localized model exports with **no materials** (the `.sm` is never
matched). A non-localized name like `rig_3b9cf934.smd` has no such segment, so the fallback adds no
candidate and cannot mis-match.

`.amd` (version 4): a clip count then per clip `"name"`, loop mode (`loop`, `once`, `loopnoint`),
an integer (1000 / 1666 — duration? unverified), and event lines.

`.ao` (version 3): `extends "Metadata/…"` plus blocks. The ones the tool reads:
`client/SkinMesh { skin = "…sm" }`, `client/ClientAnimationController { skeleton = "…ast" }`,
`AnimationController { metadata = "…amd" }`, `AttachedAnimatedObject { attached_object = "bone …ao" }`.

**Attachment assembly.** A character body's `.ao` lists `AttachedAnimatedObject` entries, each pinning
a child `.ao` to a named bone of the body skeleton — a coat on `hip_jntBnd`, hat-feathers on
`aux_Head_attachment`, weapons in the `ClientAnimationController` sockets (`socket = Back / parent =
aux_Back_attachment`, …). Each attachment carries its own mesh (`SkinMesh` → `.sm` → `.smd`) and its
own cloth rig, and can itself declare further attachments. The tool assembles the full character in the
viewport: `AssetStore::attachmentsForModel(bodySmd)` reverse-maps the loaded mesh to its `.ao`
(`findSmForSmd` gives the body `.sm`; a scoped `.ao` search **confirmed** by `SkinMesh` equality, not a
name guess), resolves each attachment to a drawable mesh, and the viewport bakes it onto its parent
bone's bind transform. Placement is bind-pose (attachments don't yet follow body animation, which would
need per-frame re-baking + a cloth sim).

Body armour `.ao`s do NOT name a skeleton — they extend `Metadata/SkinBodyArmour` → `Metadata/Skin`
(→ `nothing`), and none of that chain declares a skeleton. So a body-armour mesh's bone indices
refer to the shared **character base rig** it does not carry. That rig is `art/models/charactersfour/onerig.ast`
(the player "one rig"). Evidence: it is the **only** player body rig in the tree — `charactersfour`
holds one `onerig.ast` (plus a per-animation-group copy of the same rig); every other `.ast` there is
a class attachment (hair/skirt/necklace) or a shapeshifter form, and **no per-attribute (str/dex/int)
body rig exists**. Measured (`tools/rig_cover.cpp`): the same body armour in all three pure types
(`abyssalcuirass` str/dex/int) shares one identical 47-joint palette, and `onerig` (87 bones) covers
each with 0 out-of-range joints and skins them at 12–14 % of the model diagonal (the correct-rig range
for loose drape; a *foreign* rig scatters vertices ~62 %). The tattered robe behaves the same (46-joint
palette, ~14 %, animates coherently). The tool therefore resolves the skeleton as: co-located `.ast` first, and when that does not
cover the mesh's joint palette **and** the model is under `/items/armours/`, it falls back to
`onerig.ast` (`AssetStore::loadSkeletonFor`, gated so monster meshes are never given a player rig).
Held items (shields, weapons) are `FixedMesh` (`.fmt`) — static, no skeleton. Self-rigged meshes
(monsters, effects, gloves/boots with a covering `.ast` beside them) keep their co-located rig.

**Player animation library.** `charactersfour/onerig.ast` (the base rig a body mesh skins to) carries
exactly **one clip, `idle_01`, and it is a static hold** (measured: 87 bones × 2 keys at frames 0/24
with identical values, 0 motion). The player's real motion is not there — it lives in **~112 separate
per-move `.ast` files** under `art/models/charactersfour/animations/<move>/onerig.ast`, one per move:
`basesprint`, `basedodgeroll`, `base2hsword`, `bladedance`, `flickerstrike`, `groundstomp`, `warcry`,
`maceleapslam`, `death`, `revive`, … Each is a **copy of the same 87-bone base rig** (verified
bone-for-bone identical name list and order to `onerig.ast`) carrying that move's clips: `basesprint`
holds 151 clips (`sprint_2hsword_01`, `sprint_bow_01`, `sprint_claw_claw_01`, … per weapon set, 60 fps,
~34 frames, 43/87 bones with time-varying rotation); `base2hsword` holds 79 (`attack_2hsword_01a` plus
additive `_step_L/_R` footwork variants named via the clip record's `parentName`); `bladedance` 14 at
91 frames. Because the rigs share bone names, any clip retargets onto a character's resolved rig by
**name** (`AstSkeleton::retargetClip`), no index assumption. The Customize tab's Animations panel lists
these as selectable moves; clips are decoded lazily one at a time (`AssetStore::playerAnimClip` →
`AstSkeleton::parseWithClip`) so a 151-clip file never costs more than the one clip in play. The base
rig's own `idle_01` is offered as the "(base) idle" source.

## 8. `.ast` — skeleton and clips

Versions 6–12 by first byte. **11 and 12 (the current writers, 7,500 of 10,572 corpus files)
are parsed fully; 6–10 differ in the bone and clip records and are left unread** (bones only,
best effort, marked as such).

```
u8  version (11|12)
u8  boneCount
u8  b2            0/1/2/7/11 — unknown, does not change the layout
u8  animCount
u8  0, 0, 0
u8  lightCount
bone[boneCount]:  u8 nextSibling (255 = none); u8 firstChild (255 = none); f32 m[16]; u8 nameLen; u8 unk (0, rarely 1/64); ascii name
light[lightCount]: u8 nameLen; u8 unk; f32[12]; u32; u32; u16; ascii name   (320 files have lights)
clip[animCount]:  u16 boneCount; u8 fps (24|30|60); u8 flags (0x6c|0x6f); u8 0; u8 nameLen; u8 parentLen; u32 offset; u32 size; ascii name; ascii parentName
<a nested bundle (§1.1): the key data>
```

Hierarchy comes from the sibling/child links (walk child chains; parents precede children in file
order on every file measured). **The 4×4 is row-major with the translation in row 3, and it is the
bone's PARENT-LOCAL bind matrix** (v·M convention: `world = local × parentWorld`) — NOT model-space.
Measured proof: a `phys_beam` chain has every child bone at local `(320,0,0)` relative to the
previous bone; read as model-space they pile onto one point, but composed as local they extend into
a proper beam (0 → −1600 in world Y). A humanoid rig read as model-space collapses every bone to a
knot at the origin (the "broken skeleton" bug); composed as local it forms the correct spine +
clavicles + skirt. **The parser (AstSkeleton) therefore ACCUMULATES local → model-space
(`modelBind[b] = localBind[b] × modelBind[parent]`) and stores the model-space result in `Bone.bind`,
with `inverseBind = inverse(modelBind)`** — so the overlay, CPU skinning and the .glb exporter, all of
which expect a model-space bind, are correct. The clip position/rotation/scale keys are likewise
PARENT-LOCAL (they drive the node's local transform, glTF-style). Earlier notes here called the raw
matrix "model-space"; that was wrong — the bind-pose-identity check that seemed to confirm it was
vacuous (`inverse(bind) × bind = I` holds for any bind), and the real discriminator is the bone world
positions, which only make sense under the local reading.

Key data, per clip at `offset` inside the decompressed nested bundle, `boneCount` records:

```
u8  0
u32 boneIndex
u32 nScale, nRot, nPos
u32 unk[4]                 (0,0,0,0) or (0,0,0,1)
{ f32 frame; f32 x y z }[nScale]
{ f32 frame; f32 x y z w }[nRot]     unit quaternions, w last
{ f32 frame; f32 x y z }[nPos]
```

Frame times are integers stored as floats; divide by `fps`. The sum of record sizes equals the
clip's `size` on 187/187 clips checked; 16 clips in the sample overran their buffer and are
reported as unreadable rather than guessed at.

## 9. `.datc64` — data tables and true item names

The game's authored strings live in columnar data tables under `data/balance/*.datc64` (English at
the root; localized copies under `data/balance/<language>/`). Layout, all measured from the real
files (`tools/dat_probe.cpp`):

```
[u32 rowCount] [fixed rows: rowCount × rowWidth] [8-byte 0xBB… boundary] [variable heap]
```

`rowWidth = (boundaryOffset − 4) / rowCount` — derivable from the file itself. Fields inside a row
pack tightly (bool=1, i32=4, string ref=8, foreign key/array=16), so column offsets are **byte**
offsets, not 8-aligned. A string/data reference is a `u64` offset measured **from the boundary
position**; the heap bytes start at `boundary + 8`, so a ref of 8 points at the first heap byte.
Strings are UTF-16LE, null-terminated. A null reference / foreign key is the sentinel
`0xFEFEFEFEFEFEFEFE`. The table SCHEMA (which column is which) is not in the file. `store/DatFile`
reads the format; the caller supplies measured, validated offsets.

**True item names** (`store/NameIndex`) are resolved through an all-authored chain, every link
measured and validated against real bytes (`tools/dat_link.cpp`, `tools/ao_chain.cpp`), never from a
filename:

```
BaseItemTypes.Name (col@32)  ──(foreign key col@124)──▶  ItemVisualIdentity
ItemVisualIdentity.AOFile (col@16)  ──▶  .ao  ──(skin)──▶  .sm  ──▶  .smd   (the browsed mesh)
```

The `ItemVisualIdentity` foreign-key column (byte 124) was found by **self-validation**, not a
trusted external schema: it is the one column whose value, across all 5,496 base items, is always a
valid IVI index or null AND links to an `ItemVisualIdentity` whose `.ao` is in the *same item
category* as the item. Spot-checks are exact — `FourOneHandAxe1` → "Dull Hatchet" → `DullHatchetDrop`
model; `FourBodyStr1` → "Rusted Cuirass"; `FourBow1` → "Crude Bow". One mesh can be shared by several
base types plus rune variants (`Runeforged`/`Runemastered`) and a generic base; the canonical name is
the base whose de-spaced form the model's own filename carries (`rustedcuirass_drop` → "Rusted
Cuirass", not the generic "Garment" that merely reuses the mesh), and every name stays searchable.
**Monster names** are resolved the same way from `MonsterVarieties` (measured/validated in
`tools/dat_cols.cpp`): `Name` at byte 272, and the model `.ao` is an **array** column at byte 56
(`u64 count` @56, `u64 heap offset` @64 → a list of `count` boundary-relative string refs). The
first `.ao` chains `.ao → .sm → .smd` exactly like items — `FishParasite.ao → …fshparasite_armour…`
= "Chyme Skitterer", `BloodFever2H.ao → …fallenkaruioldermale… body` = "Blood-fevered Warrior".
Placeholder/controller varieties whose Name is "Daemon"/"Invisible"/"Clone"/"[ANY MONSTER]" reuse
real bodies, so they are skipped; leading dev tags (`[DNT-UNUSED]…`, `(DNT) …`) are stripped. Monster
bodies are shared more than item meshes (many varieties reuse one `genericbiped` body), so a shared
mesh keeps one representative monster name and every name stays searchable.

**NPCs** are named too: an NPC's Id (`NPCs` col@0) is a MonsterVariety Id, so its model is that
variety's AO — matched by **Id string**, not a foreign-key column (the obvious FK candidate, `NPCs`
col@67, resolves Einhar to "Flesh Larva", so it is wrong and was rejected). The NPC display Name is
`NPCs` col@8 ("Einhar, Beastmaster"); validated in `tools/npc_link.cpp` — `Una`→`…/una/rig_…`,
`Dannig`→`…/dannig/dannig_armour_…`.

**Item icons** (`.dds`, the Textures tab) are named with no chain at all: `ItemVisualIdentity.DDSFile`
(col@8) IS the icon file, so it maps straight to the item's `BaseItemTypes.Name`
(`currencyweaponquality.dds` → "Blacksmith's Whetstone").

`DatFile` reads scalar refs and arrays. The index is cached (`name_index_v<N>.bin`, keyed on the
bundle-index fingerprint; the version bumps whenever the sweep changes so a stale cache is discarded)
and built on the background thread after the shader index. Covered: **items, item icons, monsters,
NPCs**. If the `data/balance` bundles are absent, names are simply disabled, never fabricated.

## 10. Sources

* ggpk.discussion wiki, *Bundle scheme* — bundle and index layout, path generation, hashing.
* aianlinb/LibGGPK3 (LibBundle3) — the hash-variant sentinel, the `.dds.header` skip rule.
* Shadowth117/PSO2-Aqua-Library `AquaModelLibrary.Data/POE2` — the first SMD/AST readers for
  PoE2; the vertex-flag and key-set layouts above started from that code and were corrected
  where the bytes disagreed (0x01/0x02 swapped; bbox interleaving; the layoutVersion ≥ 4 u32).
* powzix/ooz — Oodle Kraken/Mermaid/Leviathan decoder. iOrange/bcdec — block decoder.
