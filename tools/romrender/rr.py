"""rr.py (romrender, 2026-11-18): the deterministic software renderer's core -- the reference implementation.

Everything here is whole-number arithmetic with the rounding written out, so the same scene gives the same bytes on every
run, machine and language: no floating point, no library resampling or quantising, no dict / set order. tools\\romrender\\
cpp\\rr_core.cpp is its C++ twin (rr_check.py proves the two byte-identical); keep the two in step, line for line.

Rules both follow:
  - fdiv(a, b): division rounded toward minus infinity (Python's //; C++ needs the helper), b > 0.
  - products stay inside signed 64 bits (the bounds are noted where they matter).
  - ties break by index order (the first wins), never by a container's order.

The pieces:
  isin / icos            sine and cosine of an angle in 1/65536 turns, Q16 (65536 = 1.0), by an integer Taylor series
  isqrt                  floor square root
  Camera                 a look-at camera (Q16 basis), perspective with a focal length in sixteenths of a pixel
  Scene / render         textured, Gouraud-shaded triangles, back faces culled, near-plane clipped, Z-buffered,
                         perspective-correct texels, 1-bit alpha cut-out; rendered at S x S samples a pixel
  downsample             S x S samples -> one pixel (the covered samples' mean; transparent when under half covered)
  quantise               median cut in RGB555 -> a palette of at most n colours, each pixel its nearest
  resample               a picture scaled by exact box (area) weights
"""

# ---- integer helpers ------------------------------------------------------------------------------------------------------
def fdiv(a, b):
    """a / b rounded toward minus infinity (b > 0)"""
    return a // b

def rdiv(a, b):
    """a / b rounded to the nearest, halves up (b > 0)"""
    return (2 * a + b) // (2 * b)

def isqrt(n):
    """floor(sqrt(n)), n >= 0 (Newton's method on integers)"""
    if n <= 0: return 0
    x = n; y = (x + 1) // 2
    while y < x:
        x = y; y = (x + n // x) // 2
    return x

# sine / cosine: angle a in 1/65536 of a turn; result Q16. A quarter-wave Taylor series in Q30 (x <= pi/2: the terms to
# x^13 / 13! leave under 1e-9), rounded once to Q16.
PI_Q30 = 3373259426           # round(pi * 2^30)
def _sin_q30(x):
    """sin(x) for 0 <= x <= pi/2, x and the result in Q30"""
    x2 = (x * x) >> 30
    term = x; total = x; k = 1
    while k < 14:
        term = (term * x2) >> 30
        term = term // ((k + 1) * (k + 2))
        total = total - term if (k // 2) % 2 == 0 else total + term
        k += 2
    return total

def isin(a):
    a &= 0xFFFF
    quarter, r = a >> 14, a & 0x3FFF                  # (a quarter turn = 16384)
    if quarter & 1: r = 0x4000 - r
    x = (r * PI_Q30) >> 15                            # r / 16384 x pi / 2, Q30
    s = (_sin_q30(x) + (1 << 13)) >> 14               # Q30 -> Q16, rounded
    return -s if quarter >= 2 else s

def icos(a): return isin(a + 0x4000)

def deg(d):
    """d degrees (an integer) in 1/65536 turns, rounded"""
    return rdiv(d * 65536, 360)

# ---- RGBA5551 ---------------------------------------------------------------------------------------------------------------
def c8(c5): return (c5 << 3) | (c5 >> 2)              # a 5-bit channel as 8 bits (the RDP's expansion)
def rgb_of(c):
    return (c8((c >> 11) & 31), c8((c >> 6) & 31), c8((c >> 1) & 31))
def to5551(r, g, b, a=1): return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | a

# ---- a picture: an AKI TEX file and its palette ---------------------------------------------------------------------------
WRAP, MIRROR, CLAMP = 0, 1, 2
class Texture:
    """w x h colours as RGBA5551 (the palette applied); wrap modes along s and t"""
    def __init__(self, w, h, texels, ws=WRAP, wt=WRAP):
        self.w, self.h, self.texels, self.ws, self.wt = w, h, texels, ws, wt

def wrap_mode(flag):
    """an AKI TEX header's wrap byte (+4 s, +5 t): bit 1 clamp, bit 0 mirror, else repeat"""
    return CLAMP if flag & 2 else (MIRROR if flag & 1 else WRAP)

def texture_from_tex(tex, pal, group=0):
    """an AKI TEX file (w - 1, h - 1, kind (1: 8 bits a pixel), _, wrap s, wrap t, ...; pixels at +8) with its palette
    (RGBA5551, big-endian); group > 0: that 16-colour row copied over row 0 first, as the game does for a model's
    palette group (skel.c: func_80036710(pal + 0x20 g, pal, 0x20))"""
    w, h = tex[0] + 1, tex[1] + 1
    pal = bytearray(pal)
    if group > 0 and len(pal) >= 0x20 * (group + 1): pal[0:0x20] = pal[0x20 * group:0x20 * group + 0x20]
    n = len(pal) // 2
    cols = [(pal[2 * i] << 8) | pal[2 * i + 1] for i in range(n)]
    out = []
    for i in range(w * h):
        if tex[2] == 1: k = tex[8 + i]
        else:
            b = tex[8 + (i >> 1)]
            k = (b >> 4) if (i & 1) == 0 else (b & 15)
        out.append(cols[k] if k < n else 0)
    return Texture(w, h, out, wrap_mode(tex[4]), wrap_mode(tex[5]))

def solid_texture(c5551):
    return Texture(1, 1, [c5551], WRAP, WRAP)

def _wrap(i, n, mode):
    if mode == CLAMP: return 0 if i < 0 else (n - 1 if i >= n else i)
    q = fdiv(i, n); r = i - q * n
    if mode == MIRROR and (q & 1): r = n - 1 - r
    return r

def texel(t, s, u):
    """the colour at texture coordinates s, u (10.5 fixed point, the RSP's: 32 a texel), point-sampled"""
    x = _wrap(fdiv(s, 32), t.w, t.ws); y = _wrap(fdiv(u, 32), t.h, t.wt)
    return t.texels[y * t.w + x]

# ---- the camera ---------------------------------------------------------------------------------------------------------
class Camera:
    """eye (world units), a Q16 basis (right, up, forward), focal length in 1/16 pixels, the picture's centre in 1/16
    pixels, the near plane (world units, Q8 below)"""
    def __init__(self, eye, right, up, fwd, focal16, cx16, cy16, near=8):
        self.eye, self.right, self.up, self.fwd = eye, right, up, fwd
        self.focal16, self.cx16, self.cy16, self.near8 = focal16, cx16, cy16, near * 256

def _norm16(v):
    """v (ints) as a Q16 unit vector"""
    n = isqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
    return [fdiv(c * 65536, n) if c >= 0 else -fdiv(-c * 65536, n) for c in v]

def _cross(a, b): return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]

def look_at(eye, at, focal16, cx16, cy16, near=8):
    """a camera at eye (world units, ints) looking at at, the world's y up"""
    f = _norm16([at[k] - eye[k] for k in range(3)])
    r = _norm16(_cross(f, [0, 1, 0]))
    u = _cross(r, f); u = [c >> 16 if c >= 0 else -((-c) >> 16) for c in u]
    return Camera(list(eye), r, u, f, focal16, cx16, cy16, near)

def focal_for(height_px, fov_deg):
    """the focal length (1/16 pixels) whose vertical field of view is fov_deg (an integer) for a picture height_px high:
    (h / 2) / tan(fov / 2), tan by isin / icos"""
    a = deg(fov_deg) // 2
    return fdiv(height_px * 8 * icos(a), isin(a))

# ---- the scene ----------------------------------------------------------------------------------------------------------
class Scene:
    """triangles: (texture index, flags, three vertices); a vertex (x, y, z world units, s, t 10.5, shade r, g, b Q8 --
    256 = the texel as it is). flags: CULL_BACK. background: RGBA (alpha 0: transparent)."""
    def __init__(self):
        self.textures = []; self.tris = []; self.background = (0, 0, 0, 0)
    def texture(self, t):
        self.textures.append(t); return len(self.textures) - 1
    def tri(self, tex, flags, a, b, c):
        self.tris.append((tex, flags, a, b, c))
CULL_BACK = 1

def _to_cam(cam, v):
    """a vertex in camera space: x right, y up, z forward, Q8 world units"""
    d = [v[0] - cam.eye[0], v[1] - cam.eye[1], v[2] - cam.eye[2]]
    x = d[0] * cam.right[0] + d[1] * cam.right[1] + d[2] * cam.right[2]
    y = d[0] * cam.up[0] + d[1] * cam.up[1] + d[2] * cam.up[2]
    z = d[0] * cam.fwd[0] + d[1] * cam.fwd[1] + d[2] * cam.fwd[2]
    return (fdiv(x, 256), fdiv(y, 256), fdiv(z, 256)) + tuple(v[3:])

def _clip_near(poly, near8):
    """Sutherland-Hodgman against z >= near (camera space Q8): new points interpolated with floor division"""
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        ina, inb = a[2] >= near8, b[2] >= near8
        if ina: out.append(a)
        if ina != inb:
            num, den = near8 - a[2], b[2] - a[2]
            if den < 0: num, den = -num, -den
            out.append(tuple(a[k] + fdiv((b[k] - a[k]) * num, den) for k in range(len(a))))
    return out

def render(scene, cam, W, H, S):
    """-> (colour [(r, g, b)] W S x H S, covered [bool]): the scene drawn at S x S samples a pixel"""
    WS, HS = W * S, H * S
    bg = scene.background
    col = [(bg[0], bg[1], bg[2])] * (WS * HS)
    cov = [bg[3] > 0] * (WS * HS)
    depth = [0] * (WS * HS)                                  # (1 / z, bigger is nearer; 0 = nothing yet)
    f16 = cam.focal16 * S
    cx, cy = cam.cx16 * S, cam.cy16 * S
    for tex_i, flags, a, b, c in scene.tris:
        t = scene.textures[tex_i]
        poly = _clip_near([_to_cam(cam, a), _to_cam(cam, b), _to_cam(cam, c)], cam.near8)
        if len(poly) < 3: continue
        # projected: (sx, sy) in 1/16 sample, iz = 2^28 / z (z in Q8 >= near: iz < 2^20), s, t, shade
        P = []
        for q in poly:
            z = q[2]
            sx = cx + fdiv(f16 * q[0], z); sy = cy - fdiv(f16 * q[1], z)
            P.append((sx, sy, fdiv(1 << 28, z)) + tuple(q[3:]))
        for k in range(1, len(P) - 1):
            _raster(col, cov, depth, WS, HS, t, flags, P[0], P[k], P[k + 1])
    return col, cov

def _edge(a, b, px, py):
    return (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0])

def _top_left(a, b):
    """an edge a -> b of a triangle wound so its area is positive (y down): top (horizontal, going left... ) or left"""
    dy, dx = b[1] - a[1], b[0] - a[0]
    return (dy == 0 and dx < 0) or dy > 0

def _raster(col, cov, depth, WS, HS, t, flags, p0, p1, p2):
    area = _edge(p0, p1, p2[0], p2[1])
    if area == 0: return
    if area > 0:                       # (wound the other way on screen: seen from the back -- the game's meshes face < 0)
        if flags & CULL_BACK: return
    else:
        p1, p2 = p2, p1; area = -area
    x0 = max(0, fdiv(min(p0[0], p1[0], p2[0]), 16)); x1 = min(WS - 1, fdiv(max(p0[0], p1[0], p2[0]), 16))
    y0 = max(0, fdiv(min(p0[1], p1[1], p2[1]), 16)); y1 = min(HS - 1, fdiv(max(p0[1], p1[1], p2[1]), 16))
    if x0 > x1 or y0 > y1: return
    b0 = 0 if _top_left(p1, p2) else 1          # (a sample exactly on an edge: drawn only for a top or left edge)
    b1 = 0 if _top_left(p2, p0) else 1
    b2 = 0 if _top_left(p0, p1) else 1
    for y in range(y0, y1 + 1):
        py = 16 * y + 8
        for x in range(x0, x1 + 1):
            px = 16 * x + 8
            w0 = _edge(p1, p2, px, py); w1 = _edge(p2, p0, px, py); w2 = _edge(p0, p1, px, py)
            if w0 < b0 or w1 < b1 or w2 < b2: continue
            n0 = fdiv(w0 * 65536, area); n1 = fdiv(w1 * 65536, area); n2 = fdiv(w2 * 65536, area)
            den = n0 * p0[2] + n1 * p1[2] + n2 * p2[2]           # (Q16 x 2^20: under 2^37)
            if den <= 0: continue
            i = y * WS + x
            if depth[i] >= den: continue
            s = fdiv(n0 * p0[2] * p0[3] + n1 * p1[2] * p1[3] + n2 * p2[2] * p2[3], den)
            u = fdiv(n0 * p0[2] * p0[4] + n1 * p1[2] * p1[4] + n2 * p2[2] * p2[4], den)
            c = texel(t, s, u)
            if not (c & 1): continue                             # (the colour's alpha bit clear: not drawn)
            r, g, b = rgb_of(c)
            sr = fdiv(n0 * p0[5] + n1 * p1[5] + n2 * p2[5], 65536)
            sg = fdiv(n0 * p0[6] + n1 * p1[6] + n2 * p2[6], 65536)
            sb = fdiv(n0 * p0[7] + n1 * p1[7] + n2 * p2[7], 65536)
            col[i] = (min(255, (r * sr + 128) >> 8), min(255, (g * sg + 128) >> 8), min(255, (b * sb + 128) >> 8))
            cov[i] = True
            depth[i] = den

# ---- samples -> pixels --------------------------------------------------------------------------------------------------
def downsample(col, cov, W, H, S):
    """-> [(r, g, b, a)] W x H: each pixel the mean of its covered samples (rounded), a = 255 when at least half are
    covered, else (0, 0, 0, 0)"""
    WS = W * S; out = []
    for y in range(H):
        for x in range(W):
            n = 0; sr = sg = sb = 0
            for j in range(S):
                row = (y * S + j) * WS + x * S
                for i in range(S):
                    if cov[row + i]:
                        r, g, b = col[row + i]; n += 1; sr += r; sg += g; sb += b
            if 2 * n >= S * S and n: out.append((rdiv(sr, n), rdiv(sg, n), rdiv(sb, n), 255))
            else: out.append((0, 0, 0, 0))
    return out

# ---- palette: median cut in RGB555 ----------------------------------------------------------------------------------------
def c555(r, g, b):
    """an 8-bit colour as 5-bit channels, rounded to the nearest (the 5551 format's colours)"""
    return (rdiv(r * 31, 255), rdiv(g * 31, 255), rdiv(b * 31, 255))

def quantise(colours, n):
    """colours: [(r5, g5, b5)] (one per opaque pixel) -> (palette [(r5, g5, b5)] at most n, index per colour).
    Median cut: start with every distinct colour (counted) in one box; while there are fewer than n boxes, split the
    box with the most pixels among those holding two colours or more (ties: the earlier box) along its widest channel
    (ties: r, g, b) at the pixel median, the colours ordered by that channel then by the colour; a box's colour is its
    pixels' mean, rounded. Each colour then takes the palette's nearest (squared distance; ties: the lower index)."""
    hist = {}
    for c in colours: hist[c] = hist.get(c, 0) + 1
    keys = sorted(hist)                                    # (a set order never decides: sorted)
    boxes = [[(k, hist[k]) for k in keys]] if keys else []
    while len(boxes) < n:
        best = -1; best_n = 0
        for bi, bx in enumerate(boxes):
            if len(bx) < 2: continue
            cnt = sum(e[1] for e in bx)
            if cnt > best_n: best, best_n = bi, cnt
        if best < 0: break
        bx = boxes[best]
        rng = [max(e[0][ch] for e in bx) - min(e[0][ch] for e in bx) for ch in range(3)]
        ch = 0
        for k in (1, 2):
            if rng[k] > rng[ch]: ch = k
        bx = sorted(bx, key=lambda e: (e[0][ch], e[0]))
        half = (best_n + 1) // 2; acc = 0; cut = 1
        for i, e in enumerate(bx):
            acc += e[1]
            if acc >= half: cut = i + 1; break
        if cut >= len(bx): cut = len(bx) - 1
        boxes[best:best + 1] = [bx[:cut], bx[cut:]]
    pal = []
    for bx in boxes:
        cnt = sum(e[1] for e in bx)
        pal.append(tuple(rdiv(sum(e[0][ch] * e[1] for e in bx), cnt) for ch in range(3)))
    near = {}
    for k in keys:
        bi = 0; bd = -1
        for i, p in enumerate(pal):
            d = (k[0] - p[0]) ** 2 + (k[1] - p[1]) ** 2 + (k[2] - p[2]) ** 2
            if bd < 0 or d < bd: bi, bd = i, d
        near[k] = bi
    return pal, [near[c] for c in colours]

def p5551(p, a=1): return (p[0] << 11) | (p[1] << 6) | (p[2] << 1) | a

# ---- resampling (exact box weights) ---------------------------------------------------------------------------------------
def resample(px, sw, sh, dw, dh):
    """px: [(r, g, b, a)] sw x sh -> dw x dh by area weights: output pixel (x, y) covers source [x sw / dw, (x+1) sw / dw)
    x [y sh / dh, ...); colours weighted by coverage x alpha (premultiplied), rounded"""
    out = []
    for y in range(dh):
        ya, yb = y * sh, (y + 1) * sh                     # (in 1/dh source pixels)
        for x in range(dw):
            xa, xb = x * sw, (x + 1) * sw                 # (in 1/dw source pixels)
            tw = ta = tr = tg = tb = 0
            for j in range(ya // dh, min(sh, (yb + dh - 1) // dh)):
                oy = min(yb, (j + 1) * dh) - max(ya, j * dh)
                if oy <= 0: continue
                for i in range(xa // dw, min(sw, (xb + dw - 1) // dw)):
                    ox = min(xb, (i + 1) * dw) - max(xa, i * dw)
                    if ox <= 0: continue
                    w = ox * oy; r, g, b, a = px[j * sw + i]
                    tw += w; ta += w * a; tr += w * a * r; tg += w * a * g; tb += w * a * b
            if ta == 0: out.append((0, 0, 0, 0)); continue
            out.append((rdiv(tr, ta), rdiv(tg, ta), rdiv(tb, ta), rdiv(ta, tw)))
    return out

# ---- what the front ends call (rr_check.py records every call to replay it in the C++ twin) ---------------------------------
RECORD = None             # (a list: each call's (kind, inputs, output) appended)
import os as _os
DUMP = _os.environ.get("RR_DUMP_DIR")     # (set: every call written there as a job for the C++ twin + its result: rr_check.py)

def _words(ws):
    import struct
    return struct.pack("<%dq" % len(ws), *ws)

def job_bytes(kind, args):
    """the C++ harness's job file for a call (cpp/rr_core.cpp main)"""
    if kind == "render":
        scene, cam, W, H, S = args
        ws = [0, W, H, S] + list(cam.eye) + list(cam.right) + list(cam.up) + list(cam.fwd)
        ws += [cam.focal16, cam.cx16, cam.cy16, cam.near8] + list(scene.background) + [len(scene.textures)]
        for t in scene.textures: ws += [t.w, t.h, t.ws, t.wt] + list(t.texels)
        ws.append(len(scene.tris))
        for tex, flags, a, b, c in scene.tris: ws += [tex, flags] + list(a) + list(b) + list(c)
    elif kind == "quant":
        colours, n = args
        ws = [1, n, len(colours)] + [x for c in colours for x in c]
    elif kind == "resample":
        px, sw, sh, dw, dh = args
        ws = [2, sw, sh, dw, dh] + [x for p in px for x in p]
    return b"RRJ1" + _words(ws)

def result_bytes(kind, out):
    if kind in ("render", "resample"): return bytes(x for p in out for x in p)
    pal, idx = out
    return bytes([len(pal) & 255, len(pal) >> 8]) + bytes(x for p in pal for x in p) + b"".join(bytes([k & 255, k >> 8]) for k in idx)

def _note(kind, args, out):
    if RECORD is not None: RECORD.append((kind, args, out))
    if DUMP:
        import hashlib
        j = job_bytes(kind, args); h = hashlib.sha1(j).hexdigest()[:20]
        _os.makedirs(DUMP, exist_ok=True)
        p = _os.path.join(DUMP, "%s_%s" % (kind, h))
        if not _os.path.exists(p + ".job"):
            open(p + ".out", "wb").write(result_bytes(kind, out)); open(p + ".job", "wb").write(j)

def render_px(scene, cam, W, H, S):
    """the scene drawn and downsampled: [(r, g, b, a)] W x H"""
    col, cov = render(scene, cam, W, H, S)
    out = downsample(col, cov, W, H, S)
    _note("render", (scene, cam, W, H, S), out)
    return out

def quantise_rec(colours, n):
    out = quantise(colours, n)
    _note("quant", (colours, n), out)
    return out

def resample_rec(px, sw, sh, dw, dh):
    out = resample(px, sw, sh, dw, dh)
    _note("resample", (px, sw, sh, dw, dh), out)
    return out
