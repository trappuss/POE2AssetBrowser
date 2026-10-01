#!/usr/bin/env python3
"""Container-only: import a .glb with the REAL Blender glTF importer and report whether it loads,
plus armature/animation/bone-length sanity. This is the check the C++/Python validators could NOT
do — it caught a corrupt-animation export (samplers referencing accessors that were never emitted)
that every matrix-identity check had passed, because Blender's importer is the only thing that
actually walks the animation samplers.

Setup (once):  pip install bpy --break-system-packages   (bpy 4.x/5.x, needs Python 3.11)
Usage:         python3 tools/blender_import_check.py model.glb [more.glb ...]

Exit code = number of files that failed to import. Prints one block per file.
"""
import sys

def check(path):
    import bpy, statistics as st
    bpy.ops.wm.read_factory_settings(use_empty=True)
    try:
        bpy.ops.import_scene.gltf(filepath=path)
    except Exception as e:
        print(f"{path}: IMPORT FAILED — {type(e).__name__}: {e}")
        return False
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    acts = list(bpy.data.actions)
    line = f"{path}: IMPORT OK  meshes={len(meshes)} armatures={len(arms)} actions={len(acts)}"
    if arms:
        b = arms[0].data.bones
        lens = [bn.length for bn in b] or [0]
        zero = sum(1 for l in lens if l < 1e-5)
        line += f"  bones={len(b)} bone_len[min={min(lens):.4f} max={max(lens):.4f} mean={st.mean(lens):.4f}] zero_len={zero}"
    print(line)
    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: blender_import_check.py model.glb [more.glb ...]"); sys.exit(2)
    try:
        import bpy  # noqa: F401
    except ImportError:
        print("bpy not installed — run: pip install bpy --break-system-packages"); sys.exit(2)
    fails = sum(0 if check(p) else 1 for p in sys.argv[1:])
    print(f"blender_import_check: {fails} failure(s)")
    sys.exit(fails)
