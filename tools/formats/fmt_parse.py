#!/usr/bin/env python3
"""Reference parser for PoE2 .fmt (fixed mesh) v9 (DOLm at 0x1f) and the legacy v4-v8 layout."""
import struct, glob, sys, collections
from smd_parse import vertex_stride, u8, u16, u32

def parse_v9(b):
    r = {'version': 9}
    r['meshCountHead'] = u16(b, 1); r['u3'] = u32(b, 3)
    r['bbox'] = struct.unpack_from('<6f', b, 7)
    p = 0x1f
    assert b[p:p+4] == b'DOLm', 'DOLm'
    r['bt24'] = u8(b, p+4); r['bt25'] = u8(b, p+5); lodCount = u8(b, p+6); meshCount = u8(b, p+7); r['bt28'] = u8(b, p+8)
    vf = u32(b, p+9); p += 13
    r.update(lodCount=lodCount, meshCount=meshCount, vf=vf)
    lods = [struct.unpack_from('<II', b, p+8*i) for i in range(lodCount)]; p += 8*lodCount
    st = vertex_stride(vf); r['lods'] = []
    for (tri, nv) in lods:
        meshes = [struct.unpack_from('<II', b, p+8*i) for i in range(meshCount)]; p += 8*meshCount
        nidx = sum(m[1] for m in meshes); assert nidx == tri*3
        isz = 4 if nv > 65535 else 2
        idx_off = p; p += nidx*isz; vtx_off = p; p += nv*st
        unk40 = None
        if vf & 0x40:   # measured (0x278/0x279/0x27a/0x78): 9 floats follow the vertex block
            unk40 = struct.unpack_from('<9f', b, p); p += 36
        r['lods'].append(dict(tri=tri, nv=nv, meshes=meshes, isz=isz, idx_off=idx_off, vtx_off=vtx_off, unk40=unk40))
    if r['bt24'] >= 4 and lodCount > 0:
        r['extra_model'] = u32(b, p); p += 4
    r['after_geom'] = p
    nG = u8(b, 3); nP = u16(b, 4); flag = u8(b, 6)   # head: locator group count, total point count, extra-block flag
    r['nGroups'] = nG; r['nPoints'] = nP; r['flag'] = flag
    if flag:
        raise ValueError('unparsed extra block (head flag byte %d)' % flag)
    groups = []; points = []
    def read_locators(q):
        gs = [(u8(b, q+6*i), u8(b, q+6*i+1), u32(b, q+6*i+2)) for i in range(nG)]; q += 6*nG
        assert all(g[0] == 1 for g in gs), gs
        assert sum(g[1] for g in gs) == nP, (gs, nP)
        pts = struct.unpack_from('<%df' % (3*nP), b, q); q += 12*nP
        return gs, pts, q
    if lodCount > 0:
        r['unk_after'] = u32(b, p); p += 4
        ents = []
        for m in range(meshCount):
            a = u32(b, p); p += 4
            if m == meshCount-1 and nG:
                groups, points, p = read_locators(p)
            e = u32(b, p); p += 4
            ents.append((a, e))
        r['ents'] = ents
        strings_off = p
        names = []; mats = []
        prev = 0
        for (a, e) in ents:
            region = b[strings_off + 2*prev: strings_off + 2*e].decode('utf-16-le')
            # a = char offset of the material string inside the region; name is [0,a) minus its terminator
            name = region[:min(a, len(region))].split('\0')[0]
            mat = region[a:].split('\0')[0] if a < len(region) else ''
            names.append(name); mats.append(mat)
            prev = e
        total_chars = prev
        strings = b[strings_off: strings_off + 2*total_chars].decode('utf-16-le')
        p = strings_off + 2*prev
        r['names'] = names; r['mats'] = mats
    else:
        if nG:
            groups, points, p = read_locators(p)
        total_chars = u32(b, p); p += 4
        strings = b[p:p+2*total_chars].decode('utf-16-le'); p += 2*total_chars
        r['names'] = []; r['mats'] = []
    r['locators'] = [(strings[off:].split('\0')[0], points[3*s:3*(s+n)]) for (k, n, off), s in zip(groups, [sum(g[1] for g in groups[:i]) for i in range(len(groups))])]
    r['tail_off'] = p; r['tail'] = b[p:]
    return r

def parse_legacy(b):
    """v4..v8: ver u8 | tri u32 | nv u32 | meshCount u8 | u32 0 | bbox 6f | u32 0 | (nameLen u32?, ...)"""
    r = {'version': b[0]}
    tri = u32(b, 1); nv = u32(b, 5); meshCount = u8(b, 9); r['u10'] = u32(b, 10)
    r['bbox'] = struct.unpack_from('<6f', b, 14)
    p = 38
    r['tri'] = tri; r['nv'] = nv; r['meshCount'] = meshCount
    r['raw_after_bbox'] = b[p:p+16]
    return r

def parse(b):
    if b[0] == 9: return parse_v9(b)
    return parse_legacy(b)

if __name__ == '__main__':
    files = sorted(glob.glob('/root/work/poe2/corpus/**/*.fmt', recursive=True))
    ok = 0; errs = collections.Counter(); tails = collections.Counter(); ex = collections.Counter(); mats = collections.Counter(); vfs = collections.Counter()
    alen = collections.Counter(); heads = collections.Counter()
    for f in files:
        b = open(f, 'rb').read()
        if b[0] != 9: continue
        try: r = parse(b)
        except Exception as e:
            errs['%s: %s' % (type(e).__name__, str(e)[:50])] += 1; continue
        ok += 1
        tails[(r['tail'][:4].hex(), len(r['tail']))] += 1
        if 'extra_model' in r: ex[r['extra_model']] += 1
        if 'unk_after' in r: ex['after=%d' % r['unk_after']] += 1
        vfs[hex(r['vf'])] += 1
        heads[(r['meshCountHead'] == r['meshCount'], r['u3'], r['bt24'], r['bt25'], r['bt28'])] += 1
        if r['lodCount']:
            for (a, e), n, m in zip(r['ents'], r['names'], r['mats']):
                alen['a-len(name)=%d' % (a - len(n))] += 1
                mats['mat' if m else 'nomat'] += 1
    print('v9 parsed', ok, 'errors', errs)
    print('tails', tails.most_common(8))
    print('extra', ex)
    print('vfs', vfs.most_common())
    print('heads (meshCount match, u3, bt24, bt25, bt28)', heads.most_common(8))
    print('a - len(name):', alen, 'materials:', mats)
