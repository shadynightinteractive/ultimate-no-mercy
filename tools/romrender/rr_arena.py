"""rr_arena.py (romrender, 2026-11-18): an arena's ARENA-page photo and VS backdrop, rendered by rr.py from the arena's
own meshes and pictures as the pack has them (the user's decision A: no screenshots).

    pieces: [(mesh file bytes (No Mercy's format), TEX file, palette file, quarter turns about y)]
    photo(pieces, W, H)      -> [(r, g, b, 255)] W x H: the arena from behind the ring's near side, down toward the stage
    page_tiles(photo_px, logo, head16)   -> the ARENA page's six 64 x 32 CI8 tiles (gen/arenas.py's layout)
    backdrop_tiles(px)       -> the VS backdrop's forty 64 x 30 CI4 tiles (one palette at the head of the first)

The camera and light are one for every arena (one framing, the old pictures' composition): from above the ring's near
right corner, EYE, looking across the ring toward the stage, AT, FOV degrees high -- the ring at three quarters, its near
aprons (the arena's name) toward the camera, the stage at the back left, the crowd behind. Each mesh's colour: its points' colours x the picture (byte 1 bit 7: the picture as it is; the 16-bit form:
grey 0xC8), x BRIGHT (the arenas are dark; the page's photos are lit up)."""
import struct
try:
    from . import rr, rr_model
except ImportError:
    import rr, rr_model

EYE, AT, FOV = (680, 380, -820), (-200, 110, 420), 48
BRIGHT_NUM, BRIGHT_DEN = 17, 10
SS = 3

def scene(pieces):
    S = rr.Scene(); S.background = (0, 0, 0, 255)
    texs = {}
    for mb, tex, pal, turns in pieces:
        if not mb or len(mb) < 8 or not tex or not pal: continue
        key = (tex, pal)
        if key not in texs: texs[key] = S.texture(rr.texture_from_tex(tex, pal))
        ti = texs[key]
        whole = mb[1] & 0x80                       # (the list's combiner: the picture x 1)
        alt = mb[0] & 0x80
        pts, tris, _ = rr_model.mesh(mb)
        vs = []
        for i, p in enumerate(pts):
            x, z = p[0], p[2]
            for _ in range(turns & 3): x, z = z, -x
            if whole: c = (255, 255, 255)
            elif alt: c = (0xC8, 0xC8, 0xC8)
            else: c = (mb[8 + 8 * i + 5], mb[8 + 8 * i + 6], mb[8 + 8 * i + 7])
            sh = [rr.fdiv(c[k] * 256 * BRIGHT_NUM, 255 * BRIGHT_DEN) for k in range(3)]
            vs.append((x, p[1], z, p[3], p[4], sh[0], sh[1], sh[2]))
        for a, b, c in tris:
            if a < len(vs) and b < len(vs) and c < len(vs): S.tri(ti, rr.CULL_BACK, vs[a], vs[b], vs[c])
    return S

def camera(W, H):
    return rr.look_at(list(EYE), list(AT), rr.focal_for(H, FOV), W * 8, H * 8, near=8)

# An arena whose own data has a sky (Bash at the Beach: its stage's three sky walls) has it drawn behind the scene, where
# the background would be black: one upright wall, square to the camera's level direction, SKY_DIST ahead of the eye
# (behind every piece: the arenas reach 3443 at most), each picture once across SKY_W, the pictures left to right.
SKY_DIST, SKY_W, SKY_Y0, SKY_Y1 = 5000, 2400, -400, 2600

def add_sky(S, sky):
    """sky: [(TEX file, palette file)] left to right -> its quads in S (lit as they are, shade 256; no culling)"""
    hx, hz = AT[0] - EYE[0], AT[2] - EYE[2]
    L = rr.isqrt(hx * hx + hz * hz)
    cx, cz = EYE[0] + rr.fdiv(hx * SKY_DIST, L), EYE[2] + rr.fdiv(hz * SKY_DIST, L)
    rx, rz = -hz, hx                                   # (the camera's right, level: forward x up)
    n = len(sky)
    for k, (tex, pal) in enumerate(sky):
        t = rr.texture_from_tex(tex, pal)
        ti = S.texture(t)
        a, b = (2 * k - n) * SKY_W // 2, (2 * k + 2 - n) * SKY_W // 2   # (this picture's span, from the middle)
        xa, za = cx + rr.fdiv(rx * a, L), cz + rr.fdiv(rz * a, L)
        xb, zb = cx + rr.fdiv(rx * b, L), cz + rr.fdiv(rz * b, L)
        s1, t1 = t.w * 32 - 1, t.h * 32 - 1
        v = [(xa, SKY_Y1, za, 0, 0), (xb, SKY_Y1, zb, s1, 0), (xb, SKY_Y0, zb, s1, t1), (xa, SKY_Y0, za, 0, t1)]
        v = [p + (256, 256, 256) for p in v]
        S.tri(ti, 0, v[0], v[1], v[2]); S.tri(ti, 0, v[0], v[2], v[3])

def photo(pieces, W, H, sky=None):
    S = scene(pieces)
    if sky: add_sky(S, sky)
    return rr.render_px(S, camera(W, H), W, H, SS)

# ---- the ARENA page: 128 x 96 -> six 64 x 32 CI8 tiles -------------------------------------------------------------------
def _fill(img, W, x0, y0, x1, y1, c):
    for y in range(y0, y1):
        for x in range(x0, x1): img[y * W + x] = c

def _over(dst, src):
    """src over dst (8-bit alpha, rounded)"""
    a = src[3]
    if a == 0: return dst
    if a == 255: return src
    oa = a + rr.rdiv(dst[3] * (255 - a), 255)
    if oa == 0: return (0, 0, 0, 0)
    return tuple(rr.rdiv(src[k] * a * 255 + dst[k] * dst[3] * (255 - a), oa * 255) for k in range(3)) + (oa,)

def texture_rgba(tex, pal):
    t = rr.texture_from_tex(tex, pal)
    return [rr.rgb_of(c) + ((255 if c & 1 else 0),) for c in t.texels], t.w, t.h

def page(photo_px, logo):
    """photo_px: 100 x 74; logo: (rgba, w, h) -> 128 x 96 RGBA: the photo in a white frame, the logo in a framed box at the
    lower right (a square one filling it, a wide one fitted across it) -- gen/arenas.py's layout"""
    W = 128; img = [(0, 0, 0, 0)] * (128 * 96)
    _fill(img, W, 0, 0, 102, 76, (255, 255, 255, 255))
    for y in range(74):
        for x in range(100): img[(y + 1) * W + x + 1] = photo_px[y * 100 + x]
    _fill(img, W, 70, 44, 126, 96, (255, 255, 255, 255)); _fill(img, W, 71, 45, 125, 95, (0, 0, 0, 255))
    px, lw, lh = logo
    h = 48 if lw == lh else max(1, rr.rdiv(52 * lh, lw))
    y0 = 46 if lw == lh else 46 + (48 - h) // 2
    sm = rr.resample_rec(px, lw, lh, 52, h)
    for y in range(h):
        for x in range(52):
            i = (y0 + y) * W + 72 + x
            img[i] = _over(img[i], sm[y * 52 + x])
    return img

def page_tiles(img, head16):
    """128 x 96 RGBA -> six files (head16 + 256 colours, 0 clear, + 64 x 32 indices), k = column * 3 + row"""
    out = []
    for k in range(6):
        x0, y0 = 64 * (k // 3), 32 * (k % 3)
        tile = [img[(y0 + y) * 128 + x0 + x] for y in range(32) for x in range(64)]
        op = [i for i, p in enumerate(tile) if p[3] >= 128]
        pal, idx = rr.quantise_rec([rr.c555(*tile[i][:3]) for i in op], 255)
        ci = [0] * len(tile)
        for i, q in zip(op, idx): ci[i] = q + 1
        tl = [0] + [rr.p5551(p) for p in pal] + [0] * (255 - len(pal))
        out.append(bytes(head16) + struct.pack(">256H", *tl) + bytes(ci))
    return out

def backdrop_tiles(px):
    """320 x 240 RGBA -> forty files: 64 x 30 CI4 tiles row by row, the 16-colour palette at the head of the first"""
    pal, idx = rr.quantise_rec([rr.c555(*p[:3]) for p in px], 16)
    tl = struct.pack(">16H", *([rr.p5551(p) for p in pal] + [0] * (16 - len(pal))))
    out = []
    for t in range(40):
        x0, y0 = 64 * (t % 5), 30 * (t // 5)
        q = [idx[(y0 + y) * 320 + x0 + x] for y in range(30) for x in range(64)]
        out.append((tl if t == 0 else b"") + bytes((q[i] << 4) | q[i + 1] for i in range(0, len(q), 2)))
    return out
