# FAQ

## Does this need the game installed?

Yes. The tool reads the installed game's `Bundles2` directly — it ships no game assets or keys. Point
it at your Path of Exile 2 install and go.

## Does it modify my game files?

No. It only ever *reads* `Bundles2`. Exports, images and settings are written elsewhere (your chosen
export folder, and `data\` beside the executable).

## Where do my settings and caches live?

In a `data\` folder beside the executable — an INI file, not the registry — so the whole tool is
portable. You can move the folder to another PC and your setup comes with it.

## Why is an asset missing / why can't I find X by name?

Three different answers, and the tool distinguishes them (**File ▸ Health check…**):
*present* (it's there), *unnamed* (it's there but its path is encrypted, so it can't be listed by
name), and *absent*. Names come from the game's own data tables; if a record is unnamed the tool shows
it as *unnamed* rather than inventing a label.

## Search — how do the operators work?

Space is AND, a leading `-` excludes, `a|b` is OR within one term, and a bare number is a file-id
lookup. Full table in [Keyboard & mouse](Keyboard-and-mouse).

## What can it export?

Models to `.glb` or `.gltf + .bin` (geometry, skeleton, animation clips, full metallic-roughness
materials, optionally the assembled character with attachments), textures to PNG, and the current
viewport to a PNG image. Many assets at once from the Bulk tab. See [Exporting](Exporting).

## The exported image is blurry / how do I get a bigger one?

Use **Save preview image** and raise the **Scale**. It re-renders at the larger size (true
supersampling), so 200% is genuinely sharper — it is not an upscale of the on-screen pixels.

## Is the 3D view required?

No. The viewport needs OpenGL 3.3, but if it can't start (very old GPU, or a remote session with no
GPU) the lists, texture decoding, names, search and export-of-listed-data still work. See
[Troubleshooting](Troubleshooting).

## Why is the first launch slow?

It builds the game index once (~10–15 s) and caches it. Later launches load the cache. A game patch
invalidates the cache and it rebuilds automatically — see [After a game patch](After-a-game-patch).

## Can I animate any model?

Only where the model carries the rig it is skinned to. A body-armour piece is skinned to the character
skeleton it doesn't itself contain, so it's shown at bind pose with a note rather than animated against
the wrong rig. This is deliberate — see [The tabs](The-tabs).
