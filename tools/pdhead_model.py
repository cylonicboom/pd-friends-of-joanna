#!/usr/bin/env python3
"""Build a Perfect Dark Chead*Z model from an FBX (or OBJ) mesh.

Emits the N64 big-endian modeldef the port's LOADTYPE_MODEL path expects,
then wraps it in Rare's 0x1173 container.
"""
import struct, zlib, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pdhead_fbx as fbxlib

SEG_SELF, SEG_MTX, SEG_VTX, SEG_COL = 5, 3, 4, 6
# defaults taken from a shipped head; --from-template overrides them
DEFAULT_BOX = (-81.0, 81.0, 14.0, 281.0, -81.0, 125.0)
DEFAULT_SCALE = 285.0212708
SKEL_HEAD = 0x0d
MAX_VTX_PER_BATCH = 15          # F3DEX vertex cache, matching vanilla heads
NUM_COLOURS = 64                # one gSPColor block, the idiom CheadCatherineZ uses

def be32(v): return struct.pack('>I', v & 0xffffffff)
def seg(s, off): return (s << 24) | (off & 0xffffff)

# ── input ────────────────────────────────────────────────────────────

def read_fbx(path):
    ver, root = fbxlib.load(path)
    objs = root.find('Objects')
    geo = [c for c in objs.children if c.name == 'Geometry'][0]
    V = geo.find('Vertices').props[0]
    PI = geo.find('PolygonVertexIndex').props[0]
    uvl = geo.find('LayerElementUV')
    UV = uvl.find('UV').props[0]
    UVI = uvl.find('UVIndex').props[0]
    MAT = geo.find('LayerElementMaterial').find('Materials').props[0]

    pos = [(V[i*3], V[i*3+1], V[i*3+2]) for i in range(len(V)//3)]
    tris, cur = [], []
    for pvi, idx in enumerate(PI):
        cur.append((~idx if idx < 0 else idx, pvi))
        if idx < 0:
            assert len(cur) == 3, "non-triangle polygon"
            tris.append(cur); cur = []
    uv = [(UV[i*2], UV[i*2+1]) for i in range(len(UV)//2)]
    return pos, tris, uv, UVI, MAT

# ── geometry fit ─────────────────────────────────────────────────────

def fit(pos, box):
    """Uniform scale to the target head height, then translate y onto it."""
    if not pos:
        raise ValueError("the mesh has no vertices")
    ys = [p[1] for p in pos]
    span = max(ys) - min(ys)
    if span <= 0:
        raise ValueError("every vertex sits at y=%g, so the mesh has no height to "
                         "scale onto the hit box. The exporter probably used a "
                         "different up axis." % ys[0])
    k = (box[3] - box[2]) / span
    return k, box[2] - min(ys) * k

# ── display list ─────────────────────────────────────────────────────

def gvtx(n, v0, off):    return be32(0x04000000 | ((n-1) << 20) | (v0 << 16) | (n*12)) + be32(seg(SEG_VTX, off))
def gcol(n, off):        return be32(0x07000000 | (((n-1) << 2) << 16) | (n*4)) + be32(seg(SEG_COL, off))
def gmtx(off):           return be32(0x01020040) + be32(seg(SEG_MTX, off))
def gtexid(texid):
    # our 15-bit bind: w1[0..11] | w0[6..8] << 12 (pd-fojo gbiex.h G_NOOP_TEXSLOT)
    if not (0 <= texid <= 0x7fff): raise ValueError("texture id 0x%x does not fit a bind" % texid)
    return be32(0xc0080003 | (((texid >> 12) & 0x7) << 6)) + be32(texid & 0xfff)
def genddl():            return be32(0xb8000000) + be32(0)

def gtri4(t):
    """t: up to 4 triangles, each a 3-tuple of cache indices. All-zero tris are skipped by the RSP."""
    t = list(t) + [(0,0,0)] * (4 - len(t))
    w0 = 0xb1000000 | (t[3][2] << 12) | (t[2][2] << 8) | (t[1][2] << 4) | t[0][2]
    w1 = ((t[3][1] << 28) | (t[3][0] << 24) | (t[2][1] << 20) | (t[2][0] << 16)
        | (t[1][1] << 12) | (t[1][0] << 8)  | (t[0][1] << 4)  | t[0][0])
    return be32(w0) + be32(w1)

PREAMBLE = (be32(0xe7000000) + be32(0)          # pipesync
          + be32(0xb7000000) + be32(0x00002000)  # setgeometrymode
          + be32(0xb6000000) + be32(0x000f3205)  # cleargeometrymode
          + be32(0xb7000000) + be32(0x00002205)  # setgeometrymode
          + be32(0xe7000000) + be32(0)
          + be32(0xba000c02) + be32(0x00002000)  # setothermode_h
          + be32(0xba001001) + be32(0)
          + be32(0xba001102) + be32(0)
          + be32(0xbb000001) + be32(0xffffffff)) # gsSPTexture
BATCH_SEP = be32(0xb6000000) + be32(0x000f3205) + be32(0xb7000000) + be32(0x00002205)

def build_geometry(pos, tris, uv, uvi, mat, matmap, texsize, k, yoff, flip_v=False):
    """Unindexed: three vertices per triangle, grouped by texture id.
    Returns (vtx_bytes, dl_bytes, texids_in_use_order)."""
    groups = {}
    for ti, tri in enumerate(tris):
        groups.setdefault(matmap[mat[ti]], []).append(tri)
    order = list(groups.keys())

    verts, dl = bytearray(), bytearray(PREAMBLE)
    dl += gcol(NUM_COLOURS, 0)
    dl += gmtx(0)

    for texid in order:
        w, h = texsize[texid]
        dl += gtexid(texid)
        tl = groups[texid]
        per = MAX_VTX_PER_BATCH // 3          # 5 triangles per vertex load
        for b in range(0, len(tl), per):
            chunk = tl[b:b+per]
            base = len(verts)
            for tri in chunk:
                for (vi, pvi) in tri:
                    x, y, z = pos[vi]
                    u, v = uv[uvi[pvi]]
                    if flip_v: v = 1.0 - v
                    verts += struct.pack('>hhhBBhh',
                        int(round(x*k)), int(round(y*k + yoff)), int(round(z*k)),
                        0, 0,                                   # flags, colour index
                        int(round(u*w*32)), int(round(v*h*32)))
            n = len(chunk) * 3
            dl += gvtx(n, 0, base)
            cache = [(i*3, i*3+1, i*3+2) for i in range(len(chunk))]
            for c in range(0, len(cache), 4):
                dl += gtri4(cache[c:c+4])
            dl += BATCH_SEP
    dl += genddl()
    return bytes(verts), bytes(dl), order

def build(pos, tris, uv, uvi, mat, matmap, texsize, box, scale, hitpart=8,
          with_toggle=True):
    k, yoff = fit(pos, box)
    vtx, opadl, order = build_geometry(pos, tris, uv, uvi, mat, matmap, texsize, k, yoff)
    numvertices = len(vtx) // 12
    if numvertices > 0x7fff:
        raise ValueError("%d vertices, but the model header stores the count in a "
                         "signed 16-bit field. This mesh has to be decimated or "
                         "split; shipped heads are under 600." % numvertices)
    colours = struct.pack('>4B', 255, 255, 255, 255) * NUM_COLOURS
    stub = genddl()

    ntex = len(order)
    nnodes = 4 if with_toggle else 2
    off_texcfg = 0x1c
    off_nodes  = off_texcfg + ntex * 12
    N_BBOX, N_DL, N_TOGGLE, N_DLSTUB = (off_nodes + i*0x18 for i in range(4))
    off_bbox   = off_nodes + nnodes*0x18
    off_vtx    = off_bbox + 0x1c
    off_col    = off_vtx + len(vtx)
    off_dlro   = (off_col + len(colours) + 7) & ~7
    off_dlro2  = off_dlro + 0x18
    off_togro  = off_dlro2 + 0x18
    off_opa    = ((off_togro + 8 if with_toggle else off_dlro + 0x18) + 7) & ~7
    off_stub   = off_opa + len(opadl)
    total      = off_stub + len(stub) if with_toggle else off_opa + len(opadl)

    out = bytearray(total)
    def put(off, data): out[off:off+len(data)] = data

    # modeldef
    put(0, be32(seg(SEG_SELF, N_BBOX)) + be32(SKEL_HEAD) + be32(0)
         + struct.pack('>hhfhh', 0, 1, scale, 0, ntex) + be32(seg(SEG_SELF, off_texcfg)))
    # texconfigs
    for i, texid in enumerate(order):
        w, h = texsize[texid]
        put(off_texcfg + i*12, be32(texid) + struct.pack('>8B', w, h, 6, 0, 2, 0, 0, 0)[:8])
    # nodes: type, pad, rodata, parent, next, prev, child
    def node(off, typ, rodata, parent, nxt, prv, child):
        put(off, struct.pack('>HH', typ, 0) + b''.join(be32(seg(SEG_SELF, x)) if x else be32(0)
            for x in (rodata, parent, nxt, prv, child)))
    node(N_BBOX, 0x0a, off_bbox, 0, 0, 0, N_DL)
    if with_toggle:
        node(N_DL,     0x18, off_dlro,  N_BBOX, N_TOGGLE, 0, 0)
        node(N_TOGGLE, 0x12, off_togro, N_BBOX, 0,        N_DL, N_DLSTUB)
        node(N_DLSTUB, 0x18, off_dlro2, N_TOGGLE, 0,      0, 0)
    else:
        node(N_DL, 0x18, off_dlro, N_BBOX, 0, 0, 0)
    # bbox rodata
    put(off_bbox, struct.pack('>i6f', hitpart, *box))
    put(off_vtx, vtx)
    put(off_col, colours)
    # dl rodata: opagdl, xlugdl, colours, vertices, numvertices, mcount, rwdataindex, numcolours
    put(off_dlro, be32(seg(SEG_SELF, off_opa)) + be32(0) + be32(0) + be32(seg(SEG_SELF, off_vtx))
        + struct.pack('>hhHH', numvertices, 3, 0, NUM_COLOURS))
    if with_toggle:
        put(off_dlro2, be32(seg(SEG_SELF, off_stub)) + be32(0) + be32(0) + be32(0)
            + struct.pack('>hhHH', 0, 3, 0, 0))
        put(off_togro, be32(seg(SEG_SELF, N_DLSTUB)) + struct.pack('>HH', 0, 0))
    put(off_opa, opadl)
    if with_toggle:
        put(off_stub, stub)
    return bytes(out), dict(k=k, yoff=yoff, numvertices=numvertices, nodes=nnodes,
                            opagdl_len=len(opadl), texids=order)

def rarezip(raw):
    """0x1173 + 3-byte big-endian raw length + raw deflate."""
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return b'\x11\x73' + struct.pack('>I', len(raw))[1:] + c.compress(raw) + c.flush()


def write_out(path, data):
    """Write via a temp file in the same directory, then rename over."""
    import os, tempfile
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(dir=d, prefix=".pdhead.", suffix=".tmp")
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.replace(tmp, path)
    except OSError:
        with open(path, "wb") as f:
            f.write(data)
