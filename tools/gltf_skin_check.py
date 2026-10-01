#!/usr/bin/env python3
"""Strict glTF-convention skinning validator for exported .glb files.

This exists because the app's own math is row-major / row-vector (v·M) while glTF is column-major /
column-vector (M·v). A validator written in the app's convention is CIRCULAR — it shares any
transpose/convention bug with the exporter and reports "correct" while a real glTF consumer (Blender)
skins the mesh wrong. This checker deliberately uses ONLY the glTF spec convention, independent of the
app, and it agrees with Blender (it caught the inverse-bind-matrix transpose bug that every row-vector
check had passed).

Checks, per glTF spec:
  * jointMatrix[j] = globalTransform(joint_j) · inverseBindMatrix[j] must be identity at rest
    (globalTransform composed parent-outer from node TRS/matrix; matrices column-major).
  * skinned rest position = Σ wᵢ · (jointMatrix[jᵢ] · vertex) must equal POSITION (mesh must not
    deform at rest).
Exit code = number of files that fail. Usage: gltf_skin_check.py a.glb [b.glb ...]
"""
import json, struct, sys, numpy as np

def load(path):
    d = open(path, 'rb').read()
    _, _, ln = struct.unpack('<III', d[:12]); off = 12; ch = {}
    while off < ln:
        cl, ct = struct.unpack('<II', d[off:off+8]); ch[ct.to_bytes(4,'little')] = d[off+8:off+8+cl]; off += 8+cl
    return json.loads(ch[b'JSON'].decode()), ch[b'BIN\x00']

def accessor(g, bn, ai):
    a = g['accessors'][ai]; bv = g['bufferViews'][a['bufferView']]
    bo = bv.get('byteOffset',0) + a.get('byteOffset',0); n = a['count']; t = a['type']
    nc = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[t]
    dt = {5120:'i1',5121:'u1',5122:'i2',5123:'u2',5125:'u4',5126:'f4'}[a['componentType']]
    x = np.frombuffer(bn, dtype=np.dtype(dt), count=n*nc, offset=bo)
    return x.reshape(n, nc) if nc > 1 else x

def quat_col(x, y, z, w):
    # column-vector rotation matrix (v' = R·v), glTF convention
    return np.array([[1-2*(y*y+z*z), 2*(x*y-w*z),   2*(x*z+w*y)],
                     [2*(x*y+w*z),   1-2*(x*x+z*z), 2*(y*z-w*x)],
                     [2*(x*z-w*y),   2*(y*z+w*x),   1-2*(x*x+y*y)]])

def check(path):
    g, bn = load(path)
    if 'skins' not in g or not g['skins']:
        print(f"{path}: no skin (static) — skin check n/a"); return True
    nodes = g['nodes']; skin = g['skins'][0]; joints = skin['joints']
    ibmraw = accessor(g, bn, skin['inverseBindMatrices'])
    IBM = [ibmraw[i].reshape(4,4).T for i in range(len(joints))]   # column-major stored -> math matrix
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get('children', []): parent[c] = i
    def local(ni):
        n = nodes[ni]
        if 'matrix' in n: return np.array(n['matrix'], float).reshape(4,4).T
        T = np.eye(4); T[:3,3] = n.get('translation',[0,0,0])
        q = n.get('rotation',[0,0,0,1]); R = np.eye(4); R[:3,:3] = quat_col(*q)
        S = np.eye(4); s = n.get('scale',[1,1,1]); S[0,0],S[1,1],S[2,2] = s
        return T @ R @ S
    def glob(ni):
        m = local(ni); p = parent.get(ni)
        while p is not None: m = local(p) @ m; p = parent.get(p)   # global = parent_global · local
        return m
    GJ = [glob(j) for j in joints]
    # jointMatrix at bind = global·IBM. For a correct rig this is a SINGLE rigid transform shared by
    # every joint: identity for a normal export, or a constant rotation when an export option (e.g.
    # the 180° yaw wrapper node) rotates the whole model. So the real invariant is not "== I" but
    # "all joints share one transform M0, and the skinned rest mesh == M0·POSITION" (rigid, no
    # per-vertex deformation). Checking == I would false-positive on the intentional yaw.
    JM = [GJ[k] @ IBM[k] for k in range(len(joints))]
    M0 = JM[0]
    consistency = max(float(np.abs(JM[k] - M0).max()) for k in range(len(joints)))
    worst = 0.0; nn = 0
    for m in g['meshes']:
        for p in m['primitives']:
            at = p['attributes']
            if 'JOINTS_0' not in at: continue
            P = accessor(g, bn, at['POSITION']).astype(float); J = accessor(g, bn, at['JOINTS_0']); W = accessor(g, bn, at['WEIGHTS_0']).astype(float)
            step = max(1, len(P)//5000)
            for vi in range(0, len(P), step):
                v = np.array([P[vi,0], P[vi,1], P[vi,2], 1.0]); a = np.zeros(4); ws = 0.0
                for k in range(4):
                    w = W[vi,k]
                    if w <= 0: continue
                    jj = int(J[vi,k])
                    if jj >= len(GJ): continue
                    a += w * (JM[jj] @ v); ws += w
                if ws > 0:
                    exp = (M0 @ v)[:3]
                    worst = max(worst, float(np.abs(a[:3]-exp).max())); nn += 1
    net = "identity" if float(np.abs(M0 - np.eye(4)).max()) < 1e-3 else "rigid transform (rotated/scaled export)"
    ok = consistency < 1e-3 and worst < 1e-3
    print(f"{path}: joint-transform consistency={consistency:.6f}  skinned-rest vs (M0·POSITION) max={worst:.6f} over {nn} verts  net={net}  {'OK' if ok else 'BROKEN'}")
    return ok

if __name__ == "__main__":
    if len(sys.argv) < 2: print("usage: gltf_skin_check.py a.glb [b.glb ...]"); sys.exit(2)
    fails = sum(0 if check(p) else 1 for p in sys.argv[1:])
    print(f"gltf_skin_check: {fails} failure(s)")
    sys.exit(fails)
