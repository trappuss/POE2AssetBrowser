#!/usr/bin/env python3
"""Reference parser for PoE2 .ast (skeleton + animation clips). Layout measured against the corpus, seeded by Shadowth117's POE2Action.cs."""
import struct, glob, sys, collections, subprocess, os
from smd_parse import u8, u16, u32

UNPACK = '/root/work/ooz/bundle_unpack'

def parse(b, want_anim=False):
    r = {}
    r['version'] = u8(b, 0); boneCount = u8(b, 1); r['b2'] = u8(b, 2); animCount = u8(b, 3)
    r['b4'] = u8(b, 4); r['b5'] = u8(b, 5); r['b6'] = u8(b, 6); lightCount = u8(b, 7)
    p = 8
    bones = []
    for i in range(boneCount):
        sib = u8(b, p); child = u8(b, p+1); mat = struct.unpack_from('<16f', b, p+2); p += 66
        nl = u8(b, p); unk = u8(b, p+1); p += 2
        name = b[p:p+nl].decode('utf-8'); p += nl
        bones.append(dict(sibling=sib, child=child, mat=mat, unk=unk, name=name))
    lights = []
    for i in range(lightCount):
        nl = u8(b, p); unk = u8(b, p+1); p += 2
        v = struct.unpack_from('<12f', b, p); p += 48
        i0 = u32(b, p); i1 = u32(b, p+4); i2 = u16(b, p+8); p += 10
        name = b[p:p+nl].decode('utf-8'); p += nl
        lights.append(dict(name=name, unk=unk, v=v, i=(i0, i1, i2)))
    anims = []
    for i in range(animCount):
        bc = u16(b, p); u2 = u16(b, p+2); b4 = u8(b, p+4); nl = u8(b, p+5); pl = u8(b, p+6); p += 7
        off = u32(b, p); size = u32(b, p+4); p += 8
        name = b[p:p+nl].decode('utf-8'); p += nl
        parent = b[p:p+pl].decode('utf-8'); p += pl
        anims.append(dict(boneCount=bc, u2=u2, b4=b4, offset=off, size=size, name=name, parent=parent))
    r.update(bones=bones, lights=lights, anims=anims, blob_off=p)
    blob = b[p:]
    r['blob_len'] = len(blob)
    if len(blob) >= 12:
        r['blob_head'] = struct.unpack_from('<III', blob, 0)
        r['blob_encode'] = u32(blob, 12) if len(blob) >= 16 else None
    if want_anim and len(blob) >= 60:
        tmp = '/tmp/_ast_blob.bin'; out = '/tmp/_ast_blob.raw'
        open(tmp, 'wb').write(blob)
        subprocess.check_call([UNPACK, tmp, out, 'q'])
        raw = open(out, 'rb').read()
        r['raw_len'] = len(raw)
        r['keys'] = []
        for a in anims:
            q = a['offset']
            ks = []
            for bi in range(a['boneCount']):
                kb = u8(raw, q); node = u32(raw, q+1); ns = u32(raw, q+5); nr = u32(raw, q+9); npos = u32(raw, q+13)
                u = struct.unpack_from('<4I', raw, q+17); q += 33
                sk = struct.unpack_from('<%df' % (4*ns), raw, q); q += 16*ns
                rk = struct.unpack_from('<%df' % (5*nr), raw, q); q += 20*nr
                pk = struct.unpack_from('<%df' % (4*npos), raw, q); q += 16*npos
                ks.append(dict(kb=kb, node=node, ns=ns, nr=nr, np=npos, u=u, sk=sk, rk=rk, pk=pk))
            ks_end = q
            r['keys'].append((ks, ks_end - a['offset'], a['size']))
    return r

if __name__ == '__main__':
    files = sorted(glob.glob('/root/work/poe2/corpus/**/*.ast', recursive=True))
    ver = collections.Counter(); errs = collections.Counter(); enc = collections.Counter(); misc = collections.Counter()
    bl = collections.Counter(); ok = 0
    for f in files:
        b = open(f, 'rb').read()
        try:
            r = parse(b)
        except Exception as e:
            errs[type(e).__name__ + ' ' + str(e)[:40]] += 1; continue
        ok += 1
        ver[(r['version'], r['b2'], r['b4'], r['b5'], r['b6'])] += 1
        enc[(r.get('blob_encode'), r['blob_len'] > 0)] += 1
        if r['anims']:
            a = r['anims'][0]
            misc['u2=%d b4=%d' % (a['u2'], a['b4'])] += 1
            misc['boneCount==len(bones)' if a['boneCount'] == len(r['bones']) else 'boneCount!=len(bones)'] += 1
        for bn in r['bones']: bl['bone.unk=%d' % bn['unk']] += 1
        if r['lights']: misc['has lights'] += 1
        if r['blob_len'] and 'blob_head' in r:
            u, tp, hp = r['blob_head']
            misc['blob sizes consistent' if 12 + hp + tp == r['blob_len'] else 'blob sizes INCONSISTENT'] += 1
    print('parsed', ok, 'errors', errs)
    print('version/b2/b4/b5/b6:', ver.most_common(8))
    print('blob encode:', enc.most_common(6))
    print('misc:', misc.most_common(10))
    print('bone unk:', bl.most_common(5))
