# Troubleshooting

## Triage

| Symptom | Likely cause | Go to |
|---|---|---|
| "Couldn't find Bundles2/_.index.bin" | Wrong folder selected | *Wrong folder* below |
| The 3D view is black / nothing draws | GPU/driver too old for OpenGL 3.3 | *OpenGL* below |
| A model loads but a part is untextured | That material declares no texture for that channel — **Explain material** (right-click the row) shows which values are authored vs. substituted | Right-click ▸ Explain material |
| A `.fmt` won't load, says "version N" | It's a `.fmt` v4–8 (older static meshes, not decoded yet) | Expected — see the note below |
| A full character loads upside-down | Its rig is authored head-down; orbit the camera to view it | Expected — items/gear are unaffected |
| It's slow to first list | First run rebuilds the index (~10–15 s) | Expected, cached after |

## Wrong folder

Point **File ▸ Set Path of Exile 2 folder…** at the folder that *contains* `Bundles2` — not
`Bundles2` itself (though the tool tries to recover if you pick that). On Steam it's
`…\steamapps\common\Path of Exile 2`.

## OpenGL

The viewport needs OpenGL 3.3. If the 3D view stays black on a very old GPU or in a remote session
without GPU acceleration, the rest of the tool (lists, textures, the Health check) still works. On
Windows, updating the graphics driver is the usual fix.

## The index seems stale after a patch

The cache is keyed on the index file's size and timestamp, so it rebuilds automatically. If you ever
want to force it, delete `data\cache\bundle_index_v1.bin` and relaunch (**File ▸ Reload index** also
rebuilds).
