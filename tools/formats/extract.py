#!/usr/bin/env python3
"""Extract files from the staged PoE2 bundles using files.tsv/bundles.tsv (from parse_index.py).
usage: extract.py <outdir> <path-substring-or-regex> [max]
Only bundles that are staged under UPLOADS are readable; others are reported as 'bundle not staged'."""
import os, re, sys, subprocess, struct
UPLOADS = '/mnt/user-data/uploads/Path of Exile 2/Bundles2'
UNPACK = '/root/work/ooz/bundle_unpack'
CACHE = '/root/work/poe2/raw'
os.makedirs(CACHE, exist_ok=True)
bundles = [l.rstrip('\n').split('\t') for l in open('/root/work/poe2/bundles.tsv')]
rows = [l.rstrip('\n').split('\t') for l in open('/root/work/poe2/files.tsv')]

def bundle_raw(bi):
    name = bundles[bi][1]
    src = os.path.join(UPLOADS, name + '.bundle.bin')
    if not os.path.exists(src):
        return None
    raw = os.path.join(CACHE, name.replace('/', '__') + '.raw')
    if not os.path.exists(raw):
        subprocess.check_call([UNPACK, src, raw, 'q'])
    return raw

def read_file(row):
    raw = bundle_raw(int(row[1]))
    if raw is None: return None
    with open(raw, 'rb') as f:
        f.seek(int(row[2])); return f.read(int(row[3]))

if __name__ == '__main__':
    outdir, pat = sys.argv[1], sys.argv[2]
    mx = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
    rx = re.compile(pat)
    n = missing = 0
    for r in rows:
        if not r[4] or not rx.search(r[4]): continue
        data = read_file(r)
        if data is None:
            missing += 1; continue
        dst = os.path.join(outdir, r[4])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        open(dst, 'wb').write(data)
        n += 1
        if n >= mx: break
    print(f'extracted {n}, not staged {missing}')
