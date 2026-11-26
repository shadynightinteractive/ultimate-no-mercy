"""rr_model.py (romrender, 2026-11-18): a wrestler's model as the game has it -> rr scenes: his select photo (photo.bin:
64 x 64 CI4, 16 colours, colour 0 clear), by rr.py's integer renderer. (Titantron frames, a turntable render, were tried
and retired 2026-11-18: the user removed Bret's / Neidhart's Titantron instead.)

A model is a descriptor (0x270 bytes, No Mercy's: per part the files of its mesh, picture and palette -- handlers\\aki\\
model.py) and a way to get each field's file. Its parts' meshes are already in the model's rest pose (the whole man put
together, standing, arms down: the game's bones move them from there), so the portrait needs no skeleton: every part the
game draws, as its mesh has it. The mesh format is the game's own reader's (convert_mesh_points, func_8000C1A8: lifted.cpp).

    photo_from_model(desc, get)              -> photo.bin bytes (2080)
    folder_getter(folder, costume, nm_file)  -> (desc, get) for a wrestler folder's model (model\\model.json)
    pool_getter(desc, pool, nm_file)         -> get for a descriptor numbered by an importer's Pool (handlers\\aki\\model.py)
    pack_getter(desc, data, file0, nm_file)  -> get for a descriptor numbered in a pack (gen/wmport.py Files)
"""
import json, os, struct
try:
    from . import rr
except ImportError:
    import rr

DESC = 0x270
MAGIC_AT, MAGIC_WM = 0x268, 0x574D324B          # (mod/src/skel.h SKEL_MAGIC_AT / SKEL_MAGIC_WM: joined WM2000's way)

def part_offset(k):
    """a part's 8 bytes in the descriptor: 1-20 at 8k (20 = the head, 0xA0), 21 the second head piece 0xA8, 22-24 the
    split chest and arms 0xB0.., 25-27 the attachments 0xC8.."""
    return 8 * k if k <= 20 else 0xA8 + 8 * (k - 21)

def drawn_parts(desc):
    """the parts the game draws of this model, in order (mod/src/skel.c: setup_wm / setup_nm, skel_show): a WrestleMania 2000
    / Revenge model all its body parts (its part 4, the neck, too), the head 20, its attachments 25-27; one of No Mercy's
    its body but part 4 (never drawn), the split chest and arms 22-24, the head pieces 20-21, the attachments"""
    wm = struct.unpack_from(">I", desc, MAGIC_AT)[0] == MAGIC_WM
    if wm: return [k for k in range(1, 21)] + [25, 26, 27], True
    return [k for k in range(1, 20) if k != 4] + [20, 21, 22, 23, 24, 25, 26, 27], False

def group_rows(desc, wm):
    """which parts get the palette group copy (skel.c skel_textures: desc +0x255 > 0 -> its 16-colour row over row 0):
    WrestleMania 2000's body but the head pieces 4, 5 (and 20); No Mercy's 1-24 (the head 20 not when +0x254 bit 2)"""
    g = desc[0x255]
    if g >= 128 or g == 0: return set(), 0
    if wm: return {k for k in range(1, 20) if k not in (4, 5)}, g
    s = set(range(1, 25))
    if desc[0x254] & 2: s.discard(20)
    return s, g

# ---- the mesh -------------------------------------------------------------------------------------------------------------
def s8(b): return b - 256 if b > 127 else b

def mesh(m):
    """the game's reader: -> (points [(x, y, z, s, t, nx, ny, nz)], triangles [(a, b, c)], has_normals). Points: (byte +
    offset x 16) x scale; texture coordinates (byte + the high nibble of byte 7) x 32 + 8 (10.5 fixed point), or with
    byte 0 bit 7 two 16-bit ones (and no normals: the colour 0xC8 grey)"""
    alt = m[0] & 0x80; sc = (m[0] & 0x7F) + 1; nv = m[1] & 0x7F; nt = m[2]
    off = [s8(m[4]) * 16, s8(m[5]) * 16, s8(m[6]) * 16]
    hu, hv = m[7] & 0xF0, (m[7] << 4) & 0xF0
    pts = []
    for i in range(nv):
        o = 8 + 8 * i
        x, y, z = [(s8(m[o + k]) + off[k]) * sc for k in range(3)]
        if alt:
            s = (((m[o + 4] << 8) | m[o + 5]) << 5) + 8; t = (((m[o + 6] << 8) | m[o + 7]) << 5) + 8
            pts.append((x, y, z, s, t, 0, 0, 0))
        else:
            s = ((m[o + 3] + hu) << 5) + 8; t = ((m[o + 4] + hv) << 5) + 8
            pts.append((x, y, z, s, t, s8(m[o + 5]), s8(m[o + 6]), s8(m[o + 7])))
    o = 8 + 8 * nv
    tris = [(m[o + 3 * i] & 0x7F, m[o + 3 * i + 1] & 0x7F, m[o + 3 * i + 2] & 0x7F) for i in range(nt)]
    return pts, tris, not alt

def model_parts(desc, get):
    """[(k, points, triangles, normals?, rr.Texture)] for every part drawn that has a mesh and a picture"""
    ks, wm = drawn_parts(desc)
    grp, g = group_rows(desc, wm)
    out = []
    for k in ks:
        o = part_offset(k)
        m = get(o)
        if not m or len(m) < 8: continue
        tex, pal = get(o + 2), get(o + 4)
        if not tex or not pal: continue
        pts, tris, nrm = mesh(m)
        out.append((k, pts, tris, nrm, rr.texture_from_tex(tex, pal, g if k in grp else 0)))
    return out

# ---- getters --------------------------------------------------------------------------------------------------------------
def nm_file_reader(folder):
    """No Mercy's files as extract.py writes them (<folder>\\%04x.bin)"""
    cache = {}
    def f(i):
        if i not in cache:
            p = os.path.join(folder, "%04x.bin" % i)
            cache[i] = open(p, "rb").read() if os.path.exists(p) else None
        return cache[i]
    return f

def folder_getter(folder, costume, nm_file):
    mj = json.load(open(os.path.join(folder, "model", "model.json")))
    co = mj["costumes"][costume]
    desc = open(os.path.join(folder, "model", co["descriptor"]), "rb").read()
    own = {o: os.path.join(folder, "model", mj["files"][i]) for o, i in co["fields"]}
    def get(o):
        if o in own: return open(own[o], "rb").read()
        v = struct.unpack_from(">H", desc, o)[0]
        return nm_file(v) if v else None
    return desc, get

def pool_getter(desc, pool, nm_file):
    def get(o):
        v = struct.unpack_from(">H", desc, o)[0]
        if not v: return None
        return pool.get(v) if pool.ours(v) else nm_file(v)
    return get

def pack_getter(desc, data, file0, nm_file):
    def get(o):
        v = struct.unpack_from(">H", desc, o)[0]
        if not v: return None
        return data[v - file0] if file0 <= v < file0 + len(data) else nm_file(v)
    return get

# ---- lighting and framing -------------------------------------------------------------------------------------------------
AMBIENT, DIFFUSE = 150, 150          # (Q8: the shade = ambient + diffuse x the light's cosine; 256 = the picture as it is)

def light_dir(cam):
    """the light: from over the camera's left shoulder (camera space (-1, 1.2, -1.3)), as a world Q16 unit vector"""
    return rr._norm16([-cam.right[k] * 10 + cam.up[k] * 12 - cam.fwd[k] * 13 for k in range(3)])

def shade_of(n, L):
    """a point's shade (Q8) from its normal n (s8 x 3) and the light L (Q16)"""
    nn = rr.isqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2])
    if nn == 0: return AMBIENT + DIFFUSE // 2
    d = n[0] * L[0] + n[1] * L[1] + n[2] * L[2]          # (|n| x cos x 2^16)
    if d <= 0: return AMBIENT
    return AMBIENT + rr.fdiv(DIFFUSE * d, nn * 65536)

def bounds(parts, ks):
    """(x0, x1, y0, y1, z0, z1) of the parts ks' points (None: none)"""
    b = None
    for k, pts, tris, nrm, t in parts:
        if k not in ks: continue
        for p in pts:
            if b is None: b = [p[0], p[0], p[1], p[1], p[2], p[2]]
            else:
                b = [min(b[0], p[0]), max(b[1], p[0]), min(b[2], p[1]), max(b[3], p[1]), min(b[4], p[2]), max(b[5], p[2])]
    return b

def scene_of(parts, cam):
    S = rr.Scene()
    L = light_dir(cam)
    for k, pts, tris, nrm, t in parts:
        ti = S.texture(t)
        vs = []
        for p in pts:
            sh = shade_of(p[5:8], L) if nrm else 200
            vs.append((p[0], p[1], p[2], p[3], p[4], sh, sh, sh))
        for a, b, c in tris:
            if a < len(vs) and b < len(vs) and c < len(vs): S.tri(ti, rr.CULL_BACK, vs[a], vs[b], vs[c])
    return S

HEAD = (5, 20, 21)            # (the face, the head, the second head piece: the photo frames the head by them)
YAW = 15                      # (degrees: turned a little to his left, as No Mercy's own photos often are)
DIST = 600                    # (world units from the head: a long lens, little perspective)

def portrait_camera(parts, W, H, turn, window_h_num, window_h_den, top_margin_den, centre_y_of=None):
    """a camera DIST from his head, turned `turn` (1/65536 of a turn) round him, whose picture is window_h (world units) high: the head's height
    x num / den, the head's top 1 / top_margin_den of the picture under its top edge"""
    face = bounds(parts, (5,)) or bounds(parts, HEAD)
    head = bounds(parts, HEAD) or face
    top, chin = head[3], face[2]
    hh = max(1, top - chin)
    win = rr.fdiv(hh * window_h_num, window_h_den)
    cx, cz = rr.fdiv(face[0] + face[1], 2), rr.fdiv(face[4] + face[5], 2)
    cy = top + rr.fdiv(win, top_margin_den) - rr.fdiv(win, 2)
    sn, cs = rr.isin(turn), rr.icos(turn)
    eye = [cx + rr.fdiv(DIST * sn, 65536), cy, cz + rr.fdiv(DIST * cs, 65536)]
    focal16 = rr.fdiv(H * 16 * DIST, win)
    return rr.look_at(eye, [cx, cy, cz], focal16, W * 8, H * 8, near=8)

# ---- the photo ------------------------------------------------------------------------------------------------------------
PHOTO_W = PHOTO_H = 64
SS = 4                        # (samples a pixel each way)

def photo_pixels(parts):
    cam = portrait_camera(parts, PHOTO_W, PHOTO_H, rr.deg(YAW), 7, 4, 14)
    S = scene_of(parts, cam)
    return rr.render_px(S, cam, PHOTO_W, PHOTO_H, SS)

def photo_bin(px):
    """64 x 64 RGBA -> photo.bin: 16 RGBA5551 colours (0 clear) then 2048 bytes of CI4, the high nibble first"""
    opaque = [i for i, p in enumerate(px) if p[3]]
    pal, idx = rr.quantise_rec([rr.c555(*px[i][:3]) for i in opaque], 15)
    ci = [0] * len(px)
    for i, k in zip(opaque, idx): ci[i] = k + 1
    tl = [0] + [rr.p5551(p) for p in pal] + [0] * (15 - len(pal))
    return struct.pack(">16H", *tl) + bytes((ci[i] << 4) | ci[i + 1] for i in range(0, len(ci), 2))

def photo_from_model(desc, get):
    return photo_bin(photo_pixels(model_parts(desc, get)))

PHOTO_HOW = ("rendered by tools\\romrender (our renderer) from his model as this folder has it (costume 1): head and "
             "shoulders, turned %d degrees, lit from the left" % YAW)
