# Glossary

- **Bundle** — an Oodle-compressed container file (`*.bundle.bin`) holding one or many game files.
- **`_.index.bin`** — the master index: which bundle every file lives in, and all the file paths.
- **Oodle** — the compression the bundles use (Kraken/Mermaid/Leviathan). Decoded here by the
  open-source `ooz`.
- **`.smd` / `.fmt`** — skinned mesh / fixed mesh. Skinned meshes deform with a skeleton; fixed
  meshes don't.
- **`.ast`** — a skeleton plus its animation clips.
- **`.mat`** — a material: which textures a mesh uses and how, as UTF-16 JSON.
- **`.dds` / `.dds.header`** — a texture, and a small sibling that records its true size and format.
- **BC1 / BC7** — block-compression texture formats (DXT1 and the modern high-quality one).
- **DOLm** — the four-byte tag that begins the geometry block inside a `.smd`/`.fmt`.
- **glb** — binary glTF, the 3D format the tool exports (opens in Blender).
- **Unnamed** — a file whose path the index doesn't resolve; counted, not hidden, so "present but
  unnamed" stays distinct from "absent".
