# Exporting

Everything the tool can export is in one place: the **Export** menu. It covers **model(s)**,
**selected parts**, **preview image**, **turntable GIF** and **animation-loop GIF**. Each opens its
own options dialog, and each honours a multi-selection in the Models list — select several rows and
you get one file per model into a folder you choose. The same actions are on a Models-list row's
right-click menu.

## Non-destructive — and two ways to export

The tool is **read-only**: it opens the game's `Bundles2` in place and **never writes to the game
install or its files**. The only things it writes are its own caches (in `data\` beside the
executable) and the files you explicitly export to a folder you pick.

There are two distinct export paths, and it's worth knowing which you want:

- **Converted** — `.glb` / `.gltf` for models, `.png` for textures. Ready to drop straight into
  Blender or a DCC app. The geometry is faithful, but textures are re-packed into the shapes glTF
  expects (a combined occlusion-roughness-metal map, a reconstructed normal map, PNG instead of DDS).
  Convenient, but *derived* — not the original files.
- **Raw originals (exact, unmodified)** — the pristine authored game files, byte-for-byte: the mesh
  (`.smd`/`.fmt`), its `.sm` descriptor, every `.mat` material and every `.dds` texture it references,
  plus the `.ao`/`.ast` and `.header` sidecars. Nothing is decoded or converted; the files keep their
  original game folder structure so the `.mat`→`.dds` and `.sm`→`.smd` references still resolve. This
  is how you get a model in its **original condition**.

Both are always available, so a preview or a converted `.glb` never stops you recovering the untouched
source. Raw extraction lives in three places: **Export ▸ Extract original files…** (Models tab),
**Customize ▸ Export outfit ▸ Original files (raw, every piece)…**, and the **Raw originals** checkbox
in the [Bulk](#bulk-extraction) tab. Individual textures also save losslessly from the Textures tab.

## Models → `.glb`

Load a model in the Models tab and choose **Export ▸ Export model(s)…** (or **Ctrl+E**, or right-click
a list row). The tool writes a binary glTF (`.glb`) containing:

- geometry — positions, normals, tangents and UVs;
- for a skinned model — the skeleton, inverse-bind matrices, and every animation clip;
- a full metallic-roughness material — base colour, tangent-space normal map, a packed
  metallic-roughness texture (roughness from the normal map's gloss alpha, metalness from the
  Metal_TEX) and any emissive/glow map, all embedded as PNG.

If you have selected specific parts in the viewport, only those parts are exported (verbatim);
otherwise the whole model is written.

**Getting the original files instead:** **Export ▸ Extract original files (raw, unmodified)…** writes
the model's exact authored files — the mesh, its `.sm`, every `.mat`, and every `.dds` — byte-for-byte
into a folder, mirroring their game paths (so the material→texture references resolve). Use this when
you want the source assets untouched rather than a converted `.glb`. It honours a multi-selection.

**Exporting many at once:** select several rows in the Models list (Ctrl/Shift-click) and export —
you pick one output folder and each model is written as its own file. (The viewport part-subset only
applies to a single-model export.) For hundreds of assets at a time, use the [Bulk](#bulk-extraction)
tab instead.

### Export options

Each export shows an options dialog (its choices are remembered for next time):

- **Format** — `.glb` (one self-contained binary) or `.gltf + .bin` (a text glTF beside an external
  binary buffer, for pipelines that want to edit the JSON).
- **Animations** — *all clips*, *current clip only* (the one selected in the viewport), or *none*.
- **Include skeleton** and, for a model with them, **Include attachments** — bakes the assembled
  character (body + coat/hat/weapons on their bones, weighted so they follow the animation) into one
  file, matching the assembled view.
- **Unit scale**, **Reconstruct normal-map Z**, **Rotate 180°**, **Embed textures** — described in
  [Settings](Settings) and the in-dialog Information tab.

The same options have defaults in **File ▸ Settings ▸ Export**; the per-export dialog just lets you
override them for one run.

### Axis convention

PoE2 stores meshes Z-down. The exporter converts to the glTF **Y-up** convention with a proper
rotation (so triangle winding and normals stay correct), which is exactly what Blender expects. Just
**File ▸ Import ▸ glTF 2.0** in Blender and the model comes in upright and correctly wound.

### What is and isn't in the export yet

- **In:** geometry, skinning, animation clips, and a metallic-roughness material (base colour,
  normal, packed metal-rough, emissive).
- **Notes:** PoE2 normals often store only X and Y; **Reconstruct normal-map Z** (on by default)
  rebuilds Z = √(1−x²−y²) so DCC apps light the relief correctly. Subsurface/SSS is not a standard
  glTF channel and is not written.

## Textures → PNG

In the Textures tab, **Save PNG…** writes the decoded top mip as a PNG. Channel isolation in the
viewer is for inspection only — the saved PNG is the full RGBA image.

## Images → PNG

**Save preview image** (right-click a model ▸ *Save preview image…*, or **Ctrl+Shift+I**) saves the
current viewport view — the model as you have it framed and shaded — as a PNG. It renders the asset
alone (no grid or skeleton overlay), with options remembered between saves:

- **Scale** (25–400%) — the image is *re-rendered* at that size, so 200% is a genuinely sharper
  picture, **not** an enlargement of the on-screen pixels.
- **Transparent background** — drops the model onto a transparent (alpha) PNG for icons and
  compositing, instead of the viewport backdrop.
- **Crop to model** — trims the empty margin so the image is tight around the model (transparent
  background only).

## Animated GIFs

Two entries in the **Export** menu make looping GIFs of the model as it sits in the viewport:

- **Turntable GIF** — one full 360° spin of the camera. If a clip is *playing*, the pose plays through
  the spin and the frame count is snapped so a whole number of clip cycles lands in one revolution, so
  the loop is seamless; paused or stopped, it orbits the static pose.
- **Animation-loop GIF** — the current clip sampled at **its own authored frame rate**, one GIF frame
  per clip frame, so the loop is exact and plays at real speed. Needs a clip loaded.

The options dialog offers **scale** (100% and below — GIF is not a detail medium), **max colours**,
**dither**, **transparent background**, **crop to model**, and an **optimise to target size** toggle.
The turntable additionally has a **frame rate** and **frames per turn**; the animation-loop has neither
— it takes both from the clip. Optimising re-encodes toward a size cap in a fixed
order — reduce palette, then turn dither off, then downscale — and ships the smallest attempt; frames
are captured once, so only the encoding repeats. If the target can't be reached it says so and ships
the smallest result rather than silently missing it.

Export dialogs default to your last export folder (and Documents on first use), never inside the
tool's own folder.

## Bulk extraction

The **Bulk** tab exports many assets in one run. Filter the whole index with the same search box and
`.smd`/`.fmt`/`.dds` facet as the other tabs — the match count you see is exactly the set that will
be written — choose an output folder, then press **Extract matches**. Models are written as `.glb`
and textures as `.png`.

- **Raw originals (exact files, unmodified)** — tick this to extract the pristine authored files
  instead of converting. Each matched model pulls in its `.sm`/`.mat`/`.dds`/`.ao`/`.ast` dependencies,
  and everything is written byte-for-byte in its original game folder structure (shared textures are
  written once). The `.glb`/`.png` format options are ignored in this mode. This is the fastest way to
  pull a whole filtered set of assets in their original condition.

- **Layout** — how files are arranged under the output folder:
  - *Flat* — everything in one folder;
  - *By type* — `models/` and `textures/`;
  - *Mirror game folders* — the asset's own path under Bundles2 is recreated as subfolders;
  - *Folder per model* — each asset in a folder named after it.
- **Only new** skips anything already exported (tracked in `_bulk_manifest.json` in the output
  folder); untick it to overwrite. Delete an output file and it is re-exported next run.
- A run happens on a background thread with a live progress bar and console, a working **Pause**
  (paused time is excluded from the estimate) and **Cancel** (or press **Esc**). One bad asset never
  stops the run — failures are written to `_bulk_failed.txt` with a reason each.

- **Rotate 180° (face camera / thumbnails)** turns models to face +Z on export, for thumbnail
  generators that expect characters facing the camera. It is **off by default** — the original
  model orientation is preferred on export — and the choice is remembered. The flip is applied as a
  single wrapper node, so the mesh geometry, skeleton and skin weights are the untouched original;
  only the display orientation changes. The single-model export honours the same setting.

Bulk runs decode assets across several worker threads (one per core by default). Set the count in
**Settings ▸ Export ▸ Bulk**.
