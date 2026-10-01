# Keyboard & mouse

Everything here is also in the app under **Help ▸ Controls & shortcuts** (or press **F1**), where the
shortcuts show your current bindings.

## Viewport (Models tab)

| Input | Action |
|---|---|
| Left-drag | Orbit the camera |
| Mouse wheel | Zoom in / out |
| Middle-drag, or Alt+right-drag | Pan |
| Click a part | Select it — it highlights and mirrors into the Parts panel |
| Ctrl / Shift + click | Add or remove a part from the selection |
| Double-click a part | Frame it (no change to the selection) |
| Right-click a part | Open a menu: frame · isolate (hide the rest) · export selected parts · copy part name(s). Right-clicking a part outside the current selection selects it first. |

Selection is a set: a plain click replaces it, Ctrl/Shift-click extends it. What is selected is what
`Export` writes when any parts are selected (see [Exporting](Exporting)).

## Textures tab

| Input | Action |
|---|---|
| Mouse wheel | Zoom |
| Drag | Pan |
| Double-click | Reset the view |

The status bar shows the pixel value under the cursor.

## Keyboard

| Shortcut | Action |
|---|---|
| Ctrl+E | Export the selected model(s) |
| Ctrl+Shift+I | Save the current view as a PNG image |
| Esc | Cancel a running bulk extraction |
| F1 | Controls & shortcuts |

The turntable and animation-loop GIF exports have **no default key** — bind them in
[Settings](Settings) ▸ Hotkeys if you want one. All export shortcuts are rebindable there; the menu
labels and the F1 sheet always show the keys you have set, so they never drift from the real bindings.

Every export — model(s), selected parts, preview image, and the two GIFs — lives in the **Export**
menu, and each honours a multi-selection in the list (one file per model).

## Search box (all tabs)

| Type | Meaning |
|---|---|
| `robe gloves` | AND — every term must match |
| `robe -drop` | a leading `-` excludes |
| `helmet\|gloves` | `\|` is OR within one term |
| a number | find by file id (decimal, or `0x…` hash) |

The [funnel filter](The-tabs) (tag tick-boxes) combines with the search box; the result count you see
is exactly the set an export or bulk run will write.
