#!/usr/bin/env python3
"""Reference parser for the decompressed PoE2 _.index.bin payload (index.raw).
Writes: bundles.tsv, files.tsv (hash, bundle, offset, size, path), and prints measured facts.
The path-spec bundle at the tail is itself a bundle; it is decompressed via the bundle_unpack tool."""
import struct, sys, subprocess, os, collections

RAW = sys.argv[1] if len(sys.argv) > 1 else '/root/work/poe2/index.raw'
OUT = os.path.dirname(RAW) or '.'
UNPACK = '/root/work/ooz/bundle_unpack'

data = open(RAW, 'rb').read()
pos = 0
def u32():
    global pos
    v = struct.unpack_from('<I', data, pos)[0]; pos += 4; return v
def u64():
    global pos
    v = struct.unpack_from('<Q', data, pos)[0]; pos += 8; return v

bundle_count = u32()
bundles = []
for i in range(bundle_count):
    n = u32(); name = data[pos:pos+n].decode('utf-8'); pos += n
    usz = u32()
    bundles.append((name, usz))
file_count = u32()
files = []
for i in range(file_count):
    h = u64(); bi = u32(); off = u32(); sz = u32()
    files.append([h, bi, off, sz, None])
rep_count = u32()
reps = []
for i in range(rep_count):
    h = u64(); po = u32(); ps = u32(); prs = u32()
    reps.append((h, po, ps, prs))
tail = data[pos:]
print(f'bundle_count={bundle_count} file_count={file_count} path_rep_count={rep_count} tail(path bundle)={len(tail)} bytes at offset {pos}')
print('first path_rep hash = 0x%016X' % reps[0][0])
open(os.path.join(OUT, 'pathspec.bundle.bin'), 'wb').write(tail)
subprocess.check_call([UNPACK, os.path.join(OUT, 'pathspec.bundle.bin'), os.path.join(OUT, 'pathspec.raw'), 'q'])
spec = open(os.path.join(OUT, 'pathspec.raw'), 'rb').read()
print(f'pathspec decompressed = {len(spec)} bytes')

# --- hashing candidates ---
def fnv1a64_pp(path):
    h = 0xCBF29CE484222325
    for b in (path.lower() + '++').encode('utf-8'):
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h
def murmur64a(path, seed=0x1337B33F):
    b = path.lower().encode('utf-8')
    if b.endswith(b'/'): b = b[:-1]
    m = 0xC6A4A7935BD1E995; r = 47; M = 0xFFFFFFFFFFFFFFFF
    h = (seed ^ (len(b) * m)) & M
    n8 = len(b) // 8
    for i in range(n8):
        k = struct.unpack_from('<Q', b, i*8)[0]
        k = (k * m) & M; k ^= k >> r; k = (k * m) & M
        h ^= k; h = (h * m) & M
    rem = b[n8*8:]
    if rem:
        h ^= int.from_bytes(rem.ljust(8, b'\0'), 'little')
        h = (h * m) & M
    h ^= h >> r; h = (h * m) & M; h ^= h >> r
    return h

byhash = {f[0]: f for f in files}
# --- generate paths from path reps ---
def gen(off, size):
    out = []
    p = off; end = off + size
    base = []; phase_base = False
    while p <= end - 4:
        idx = struct.unpack_from('<I', spec, p)[0]; p += 4
        if idx == 0:
            phase_base = not phase_base
            if phase_base: base = []
            continue
        z = spec.index(b'\0', p); s = spec[p:z].decode('utf-8'); p = z + 1
        idx -= 1
        if idx < len(base): s = base[idx] + s
        if phase_base: base.append(s)
        else: out.append(s)
    return out

total_paths = 0; matched_m = 0; matched_f = 0; unmatched = 0
dir_names = {}
for (h, po, ps, prs) in reps:
    paths = gen(po, ps)
    total_paths += len(paths)
    for s in paths:
        hm = murmur64a(s)
        if hm in byhash:
            byhash[hm][4] = s; matched_m += 1
        elif fnv1a64_pp(s) in byhash:
            byhash[fnv1a64_pp(s)][4] = s; matched_f += 1
        else:
            unmatched += 1
print(f'paths generated={total_paths} matched(murmur)={matched_m} matched(fnv++)={matched_f} unmatched={unmatched}')
# directory hash check: hash of directory path (no trailing slash)
sample_dir = None
for (h, po, ps, prs) in reps[1:50]:
    paths = gen(po, ps)
    if paths:
        d = paths[0].rsplit('/', 1)[0] if '/' in paths[0] else ''
        print(f'dir rep hash 0x{h:016X}  dir="{d}"  murmur(dir)=0x{murmur64a(d):016X}  murmur(dir/)=0x{murmur64a(d+"/"):016X}')
        break
nopath = sum(1 for f in files if f[4] is None)
print(f'files without a path: {nopath}')
with open(os.path.join(OUT, 'bundles.tsv'), 'w') as o:
    for i, (n, s) in enumerate(bundles): o.write(f'{i}\t{n}\t{s}\n')
with open(os.path.join(OUT, 'files.tsv'), 'w') as o:
    for f in files: o.write(f'{f[0]:016X}\t{f[1]}\t{f[2]}\t{f[3]}\t{f[4] or ""}\n')
ext = collections.Counter(os.path.splitext(f[4])[1].lower() for f in files if f[4])
print('top extensions:', ext.most_common(60))
dup = collections.Counter((f[1], f[2], f[3]) for f in files)
print('distinct payloads:', len(dup), 'files sharing a payload:', sum(c for c in dup.values() if c > 1))
