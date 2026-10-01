# After a game patch

Path of Exile 2 patches rewrite `Bundles2`. The tool is built to notice and re-read on its own, but
here is what actually happens and what to do if something looks off.

## What updates automatically

- **The game index.** The cache is keyed on the index file's size and timestamp, so a patch
  invalidates it and the next launch rebuilds it (~10–15 s). You never have to clear it by hand.
- **In-game names.** Item, monster and NPC names are re-derived from the patched data tables during
  that rebuild, so renamed or newly-added assets get their current names.
- **The shader-family sweep** used by the Shader filter rebuilds when its own inputs change.

If you want to force any of this, **File ▸ Reload index**, or clear the relevant cache in
[Settings](Settings) ▸ Maintenance.

## What to check after a big patch

- **New assets show up** — the file counts in **File ▸ Health check…** should rise. If they didn't,
  you may still be pointed at an old install; confirm the folder in [Settings](Settings).
- **A model that used to load now won't**, reporting a format version — a patch can introduce a mesh
  format variant the current build doesn't parse yet. That is a real gap, not a corrupt file; see
  [Troubleshooting](Troubleshooting). The list, textures and names still work.
- **Names look wrong or missing** — the data-table layout occasionally shifts between major patches.
  The tool fails closed (it shows *unnamed*, never a guessed name). If many names vanish after a
  patch, that is the signal a column moved and the build needs updating.

## What a patch never breaks

Your exports, saved images and settings live outside the game folder, so a patch leaves them alone.
The tool only ever *reads* `Bundles2`; it never writes to the game install.
