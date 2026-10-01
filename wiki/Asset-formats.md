# Asset formats

This is the short version. The **authoritative, measured reference is `docs/FORMATS.md`** in the
repository — every field there was read off the real install, with the method stated, and it says
where it is still unsure.

## Storage

The game ships everything under `Bundles2\`. There is no `Content.ggpk` on Steam. Bundles are
Oodle-compressed (Kraken / Mermaid / Leviathan) in 256 KiB blocks. `_.index.bin` is itself a bundle;
its payload lists every bundle and every file, and a nested bundle at its tail encodes all the file
paths. Paths are hashed with **MurmurHash64A** (seed `0x1337B33F`) over the lowercased path — the
post-3.21.2 scheme. The tool verifies this by round-tripping: every path it lists hashes back to its
own record.

## Models

- **`.smd`** — skinned mesh (characters, armour, monsters). A `DOLm` block at offset 0x20, LODs, a
  per-mesh index table, an interleaved vertex buffer whose layout is a flag word, and a UTF-16 name
  table. Three on-disk versions (1, 2, 3).
- **`.fmt`** — fixed mesh (props, terrain doodads). Same `DOLm` block at 0x1f, plus a string table
  carrying each mesh's Maya name and its `.mat` path, and optional named locator points. Version 9
  is decoded; 4–8 are not yet.
- **`.tmd`** — terrain tile metadata (text), not a mesh.

Vertices are Z-down; the viewer and exporter rotate to Y-up. Front faces are counter-clockwise.

## Skeleton and animation

- **`.ast`** — the skeleton (bone hierarchy + model-space bind matrices) and animation clips. The
  clip key data is a nested compressed bundle at the file's tail. Versions 11 and 12 are decoded.

## Materials and textures

- **`.mat`** — UTF-16 JSON. The texture *role* is the authored parameter name (`base_color_texture`,
  `NormalGlossAO_TEX`, `Metal_TEX`, `Glow_TEX`, …), which is what the tool classifies on — never the
  file name.
- **`.dds`** — a standard DDS with a DX10 header; BC1 and BC7 dominate. A sibling **`.dds.header`**
  (in a small "Tiny" bundle) carries the full dimensions and format so the list is fast.
- **`.sm`** / **`.ao`** / **`.amd`** — small UTF-16 text files that wire a mesh to its materials,
  skeleton and animations.
