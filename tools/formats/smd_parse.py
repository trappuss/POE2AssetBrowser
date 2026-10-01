#!/usr/bin/env python3
"""Reference parser for PoE2 .smd (skinned mesh data), versions 1 and 3. Measured, not copied.
Prints byte accounting so every region of the file is explained."""
import struct, sys, glob, collections

def u8(b, p): return b[p]
def u16(b, p): return struct.unpack_from('<H', b, p)[0]
def u32(b, p): return struct.unpack_from('<I', b, p)[0]

def vertex_stride(vf):
    # measured: 0x20 position float3; 0x10 normal snorm8x4 + tangent snorm8x4; 0x8 uv half2; 0x4 bone idx u8x4 + weight u8x4; 0x2 colour rgba8; 0x40 u32 (unknown, =1); 0x1 uv2 half2. Field order = bit order descending except 0x2/0x40/0x1 are after weights (measured 0x23d, 0x3e, 0x27a).
    s = 0
    if vf & 0x20: s += 12
    if vf & 0x10: s += 8
    if vf & 0x08: s += 4
    if vf & 0x02: s += 4
    if vf & 0x04: s += 8
    if vf & 0x40: s += 4   # measured on 0x278/0x27a: a u32 (value 1) after the colour
    if vf & 0x01: s += 4
    return s

PER_LOD = False
def parse_v3(b):
    r = {'version': 3}
    r['b1'] = u8(b, 1); r['meshCountHead'] = u16(b, 2); r['nameBytesTotal'] = u32(b, 4)
    r['bbox'] = struct.unpack_from('<6f', b, 8)  # minx maxx miny maxy minz maxz
    p = 0x20
    assert b[p:p+4] == b'DOLm', 'DOLm'
    r['bt24'] = u8(b, p+4); r['bt25'] = u8(b, p+5); lodCount = u8(b, p+6); meshCount = u8(b, p+7); r['bt28'] = u8(b, p+8)
    vf = u32(b, p+9); p += 13
    r['lodCount'] = lodCount; r['meshCount'] = meshCount; r['vf'] = vf
    lods = [struct.unpack_from('<II', b, p+8*i) for i in range(lodCount)]; p += 8*lodCount
    r['lods'] = []
    stride = vertex_stride(vf)
    for (tri, nv) in lods:
        meshes = [struct.unpack_from('<II', b, p+8*i) for i in range(meshCount)]; p += 8*meshCount
        nidx = sum(m[1] for m in meshes)
        assert nidx == tri*3, (nidx, tri)
        isz = 4 if nv > 65535 else 2
        idx_off = p; p += nidx*isz
        vtx_off = p; p += nv*stride
        extra = None
        if r['bt24'] >= 4 and PER_LOD:
            extra = u32(b, p); p += 4
        r['lods'].append(dict(tri=tri, nv=nv, meshes=meshes, isz=isz, idx_off=idx_off, vtx_off=vtx_off, extra200=extra))
    r['after_geom'] = p
    if r['bt24'] >= 4 and not PER_LOD and lodCount > 0:
        r['extra_model'] = u32(b, p); p += 4
    if meshCount > 0:
        lens = struct.unpack_from('<%dI' % meshCount, b, p); p += 4*meshCount
        names = []
        for l in lens:
            names.append(b[p:p+l].decode('utf-16-le')); p += l
        r['names'] = names
        assert sum(lens) == r['nameBytesTotal'], (sum(lens), r['nameBytesTotal'])
    else:
        r['names'] = []
    r['tail_off'] = p
    r['tail'] = b[p:]
    return r

def parse_v1(b):
    r = {'version': b[0]}
    tri = u32(b, 1); nv = u32(b, 5)
    r['b9'] = u8(b, 9); meshCount = u8(b, 10); r['b11'] = u8(b, 11)
    r['nameBytesTotal'] = u32(b, 12)
    r['bbox'] = struct.unpack_from('<6f', b, 16)
    p = 40
    if b[0] == 2:
        r['v2_extra'] = u32(b, p); p += 4
    meshes = [struct.unpack_from('<II', b, p+8*i) for i in range(meshCount)]; p += 8*meshCount  # (nameLen, idxStart)
    names = []
    for (l, s) in meshes:
        names.append(b[p:p+l].decode('utf-16-le')); p += l
    r['names'] = names; r['meshCount'] = meshCount
    idx_off = p; p += tri*3*2
    vtx_off = p; p += nv*32
    r['lods'] = [dict(tri=tri, nv=nv, meshes=[(s, 0) for (l, s) in meshes], isz=2, idx_off=idx_off, vtx_off=vtx_off)]
    r['tail_off'] = p; r['tail'] = b[p:]
    return r

def parse(b):
    v = b[0]
    if v == 3: return parse_v3(b)
    if v in (1, 2): return parse_v1(b)
    raise ValueError('unknown smd version %d' % v)

if __name__ == '__main__':
    files = sorted(glob.glob(sys.argv[1] if len(sys.argv) > 1 else '/root/work/poe2/corpus/**/*.smd', recursive=True))
    ok = 0; errs = collections.Counter(); tails = collections.Counter(); unkz = collections.Counter(); b1 = collections.Counter()
    heads = collections.Counter(); stride_err = 0
    for f in files:
        b = open(f, 'rb').read()
        try:
            r = parse(b)
        except Exception as e:
            errs['%s: %s' % (type(e).__name__, str(e)[:50])] += 1; continue
        ok += 1
        t = r['tail']
        tails[(t[:4].hex(), len(t))] += 1
        for l in r['lods']:
            if l.get('extra200') is not None: unkz[l['extra200']] += 1
        if 'extra_model' in r: unkz['model=%d' % r['extra_model']] += 1
        if r['version'] == 2: unkz['v2extra=%d' % r['v2_extra']] += 1
        if r['version'] == 3:
            heads[(r['b1'], r['bt24'], r['bt25'], r['bt28'], r['meshCountHead'] == r['meshCount'])] += 1
    print('parsed', ok, 'errors', errs)
    print('tail (first4, len):', tails.most_common(12))
    print('unk_zero:', unkz)
    print('v3 heads (b1,bt24,bt25,bt28,meshCountHead==meshCount):', heads.most_common(10))
