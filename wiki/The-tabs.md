# The tabs

The tab order is **Textures · Models · Customize · Bulk**. Below they are grouped by what each is for.

## Models

Lists every `.smd` (skinned mesh — characters, armour, monsters) and `.fmt` (fixed mesh — props,
terrain doodads) in the game. Double-click a row to load it.

- **Search** — space between terms is AND, a leading `-` excludes, and `a|b` inside a term is OR.
  So `robe -drop` finds robes but not their drop versions; `helmet|gloves str` finds strength
  helmets or gloves.
- **Type facet** — the dropdown beside the search box narrows to `.smd` or `.fmt`.
- **Filters (funnel)** — the funnel button opens a popup that stays open while you tick tags. The
  tags come from the game's own folder layout (not from file names): a **Category** group — Items,
  Monsters, Characters, NPCs, Pets, Effects, Terrain, Environment, Doodads, Chests, Portals, Map
  devices, Interface — and an **Item type** group — Armours, Weapons, Off-hand, Quivers, Back
  attachments, Quest items. Each tag shows how many of the current results fall in it. Ticks in
  different groups AND together (Items **and** Armours); ticks in the same group OR (Armours **or**
  Weapons); a **Match any** toggle ORs everything instead. Every active filter appears as a
  removable chip, and the funnel is highlighted while any filter is on. Category is anchored to an
  asset's primary class, so a monster-themed shield counts as an Item, not a Monster.
- **List or grid** — a toggle switches between the detail list and a **grid of rendered thumbnails**.
  The grid button's **▾ arrow** opens an option, **"Textured (base-colour) icons"**, to render the
  thumbnails with their base-colour texture instead of flat grey; tiles resize, and hovering a tile
  (or a list row) shows a card preview you can **scroll to grow or shrink**.
- **Shading** — Flat, Shaded (full metallic-roughness PBR with normal mapping), or Wireframe.
- **Channel** — the lit result by default; switch to view a single input raw — Normal, Roughness,
  Metallic, AO, Emissive, or the flat base colour.
- **Overlays** — Grid and Skeleton toggles, behind one master "Overlays" switch.
- **Animation** — for a skinned mesh whose rig it carries, a clip bar appears under the viewport:
  pick a clip, play/pause, or scrub the timeline. A body-armour piece is skinned to the character
  skeleton it does not itself carry, so it shows at bind pose with a note rather than animating
  against the wrong rig. For a mesh that loads on the player base rig (armour pieces), an Animations
  **Move** selector appears so you can play the real player moves (sprint, attacks, skills…) on it —
  the same library as the **Customize** tab below.
- **Selecting parts** — click a part in the viewport to select it; Ctrl/Shift-click adds or removes.
  The PARTS panel below mirrors the selection both ways. `Ctrl+E` exports the whole model, or just
  the selected parts if any are selected.
- **Explain material…** — right-click a row for a plain-text report of what the model's materials
  are: where the roster came from, every authored texture role and whether it resolves, and — the
  point — which PBR values are authored versus stand-ins the tool substituted or interpreted.
- **Camera** — left-drag orbits, wheel zooms, middle-drag (or Alt+right-drag) pans, double-click a
  part frames it.

## Textures

Every `.dds` in the game, decoded in-tool. Double-click to preview.

- **Channel** — RGB, or isolate R / G / B / A.
- **Alpha checker** — composite the alpha over a checkerboard to see transparency.
- **Zoom / pan** — wheel zooms, drag pans, double-click resets. The status bar shows the pixel value
  under the cursor.
- **List or grid** — like the Models tab, a grid of thumbnails with a scroll-to-resize hover preview.
- **Save** — writes the decoded image as PNG, or right-click to export the original `.dds` bytes.

## Customize

A character dressing room. Build a figure and pose it, then export it.

- **Class** — pick a class; its body mesh and default pieces load onto the shared character rig.
- **Outfit** — fill the figure from a real armour set, or choose each slot's piece individually
  (slot dropdowns are searchable). **Randomise** rolls a set; **presets** save and reload a look.
- **Toggles** — show/hide hair, the head mesh, and equipped-item FX meshes.
- **Panels** — Parts, Animations, Attachments and Info, the same as the Models tab, adapted to the
  assembled figure.
- **Animations — the player move library.** The base character rig carries only a static idle, so
  the real motion is read from the game's ~112 per-move animation files. The **Move** selector lists
  them — "(this class rig)" for the idle, or a move such as `basesprint`, `base2hsword`, `bladedance`,
  `groundstomp`, `warcry`, `death`… — and the clips of the chosen move appear below. Pick a clip and
  it is retargeted onto the assembled character by bone name and played on the fully-skinned figure
  (so worn armour deforms with the body). If a move doesn't match the current rig, the tool says so
  rather than mangling the pose.
- **Export** — the whole figure as one file, the figure without weapons, or each equipped item as its
  own file, using the same options as the Models tab.

## Bulk

Extract many assets in one run. Filter the whole index (models and textures) with the same search,
type facet and funnel filters as the other tabs — the match count you see is exactly the set that
will be written — pick an output folder and a layout, then **Extract matches**. Models are written as
`.glb`, textures as `.png`, or choose **raw originals** to write the authored bytes untouched. See
[Exporting](Exporting) for the layout choices and the only-new / pause / cancel behaviour.

## Health check

**File ▸ Health check…** reports what the index holds: the total records, how many are listed, how
many have no resolvable path (shown as *unnamed* rather than hidden), how many share a payload, and
the shader-cache roots that are counted but not listed. This is the honest answer to "why is X
missing" — *present*, *unnamed*, and *absent* are three different answers.
