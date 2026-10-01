# Diagnostics

The tool is built to explain itself rather than make you guess. When something looks wrong, these
built-in reports answer *why* — and they always separate what the game actually authored from what the
tool substituted or interpreted.

## Health check

**File ▸ Health check…** reports what the loaded index really holds:

- the Bundles directory and the index fingerprint it is keyed on;
- total records vs **listed (named)** files;
- **unnamed** files — present in the game, but with an encrypted/unresolved path, so they can't be
  listed by name (shown, not hidden);
- files that share a payload;
- the shader-cache roots that are counted but not listed.

This is the honest answer to "why is X missing": *present*, *unnamed* and *absent* are three different
things, and the report tells you which one you're looking at — it never fabricates a name to fill a
gap.

## Explain material

Right-click a model in the list ▸ **Explain material…** for a plain-text report of how that model's
materials were resolved:

- where the material roster came from;
- every authored texture role and whether it resolved to a real texture;
- **which PBR values are authored versus stand-ins** the tool substituted or interpreted (for
  example, a roughness pulled from a gloss channel, or a specular colour derived when none was
  authored).

If a model looks wrong in the viewport, this is the first place to look — it shows whether the input
data is missing or the tool made an assumption.

## Find by id

Assets are keyed by a hash of their path. Type a number in any search box — decimal, or `0x…` for the
hash directly — to jump straight to that file id. Useful when a crash log, another tool, or a data
table refers to an asset by id rather than name.

## When to reach for which

| Question | Report |
|---|---|
| "Is this asset even in my install?" | Health check (present / unnamed / absent) |
| "Why does this model look untextured / wrong?" | Explain material |
| "Something referenced file 12345 — what is it?" | Find by id (type the number) |
| "Names disappeared after a patch" | [After a game patch](After-a-game-patch) |
| "The 3D view is black" | [Troubleshooting](Troubleshooting) |
