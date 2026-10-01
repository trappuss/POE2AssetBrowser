# Settings

Open with **File ▸ Settings…**. The dialog is organised into tabs; every option is one setting stored
in `data\POE2AssetBrowser.ini` beside the executable (never the registry), so the tool stays portable.

## General

- **Path of Exile 2 folder** — the install folder that *contains* `Bundles2`. Changing it re-reads the
  game index when you close the dialog.
- **Bundles2 override** — point at a `Bundles2` directory directly (for an unpacked copy, or a
  non-standard layout). Leave empty to derive it from the game folder.

## Export ▸ Models

The defaults for a single-model export. The per-export dialog (shown on **Ctrl+E**) starts from these
and can override them for one run.

- **Unit scale** — metres per native unit. PoE2 art is ~0.5 cm/unit, so **0.005** gives a ~1.8 m
  character in Blender; **1.0** keeps raw units.
- **Format** — `.glb` (one self-contained binary) or `.gltf + .bin` (text glTF beside an external
  buffer).
- **Animations** — export all clips, or none by default (the per-export dialog also offers *current
  clip only*).
- **Include skeleton**, **Include attachments** (bake the assembled character), **Reconstruct
  normal-map Z**, **Rotate 180°**, **Embed textures** — see the in-dialog **Information** tab for what
  each one is for.

## Export ▸ Bulk

- **Worker threads** — how many assets decode in parallel. **0 = auto** (one per core). Bulk work is
  CPU-bound, so this scales with cores.
- **Folder layout** — how a multi-asset run arranges files: *Flat*, *By type*, *Mirror game folders*,
  or *Folder per model*. See [Exporting](Exporting).

## Hotkeys

Rebind the export and save-image shortcuts. Each row is a key-capture field — click it and press the
combination you want; clear it to unbind. The menus and the F1 sheet update to match.

## Maintenance

Lists the on-disk caches (the game index, the shader-family sweep, the name index) with their sizes,
and a **Clear** button per cache plus **Clear all caches**. Clearing a cache just makes the next launch
rebuild it — nothing game-related is lost.

## Information

Plain-language explanations of the confusable export options (what *Reconstruct normal-map Z* does,
`.glb` vs `.gltf + .bin`, image **Scale** re-rendering rather than upscaling, and so on). The same
explanations back the tooltips throughout the dialog.

## Restore export defaults

The button at the bottom clears the **export** options back to their defaults. It does this by
*removing* the saved keys (so a future default change is picked up), and it deliberately leaves your
folders and hotkeys alone.
