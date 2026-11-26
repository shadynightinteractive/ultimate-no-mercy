"""Select-screen photos (2026-11-12, handlers): 64 x 64 CI4 with a 16-colour RGBA5551 palette (colour 0 clear), as the
game's photo files hold them and a folder's photo.bin has them (palette, 32 bytes, then 2048 bytes of pixels).

photo_from_shot is data/revport/mkphoto.py's way (the same code, kept the same so the photos come out the same): a
crop of a screenshot of the source game's own model of him (Revenge's Costume Change screen), the dark behind him cut out
by a flood fill from the edges, 15 colours. It works for any AKI game whose screen shows the man on a dark ground."""
import struct
from collections import deque

def to5551(r, g, b): return ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1

def photo_from_shot(path, box):
    from PIL import Image
    import numpy as np
    im = Image.open(path).convert("RGB").resize((320, 240), Image.BOX).crop(tuple(box)).resize((64, 64), Image.LANCZOS)
    f = np.array(im).astype(int)
    lum = f.mean(2)
    dark = lum < 4
    bg = np.zeros(dark.shape, bool); q = deque()
    for y in range(64):
        for x in range(64):
            if (y in (0, 63) or x in (0, 63)) and dark[y, x] and not bg[y, x]: bg[y, x] = True; q.append((y, x))
    while q:
        cy, cx = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            yy, xx = cy + dy, cx + dx
            if 0 <= yy < 64 and 0 <= xx < 64 and dark[yy, xx] and not bg[yy, xx]: bg[yy, xx] = True; q.append((yy, xx))
    m = ~bg
    qi = Image.fromarray(np.where(m[..., None], f, 0).astype(np.uint8)).quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    pal = qi.getpalette()[:45]; idx = np.array(qi)
    ci = np.where(m, idx + 1, 0).astype(np.uint8)
    tlut = [0] + [to5551(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2]) for i in range(15)]
    ci4 = bytes(((int(ci[y, x]) << 4) | int(ci[y, x + 1])) for y in range(64) for x in range(0, 64, 2))
    return struct.pack(">16H", *tlut) + ci4

def photo_from_picture(tex, pal):
    """(2026-11-12) A stand-in photo from one of his model's pictures (an AKI TEX picture: w - 1, h - 1, a byte 1 for 8 bits a
    pixel, ...; RGBA5551 palette): the picture scaled to 64 x 64, 15 colours. For a man with no screenshot of him yet."""
    from PIL import Image
    w, h = tex[0] + 1, tex[1] + 1
    if tex[2] == 1: px = list(tex[8:8 + w * h])
    else:
        px = []
        for b in tex[8:8 + w * h // 2]: px += [b >> 4, b & 15]
    cs = []
    for i in range(len(pal) // 2):
        v = struct.unpack_from(">H", pal, 2 * i)[0]
        cs.append(((v >> 11) << 3, ((v >> 6) & 31) << 3, ((v >> 1) & 31) << 3))
    im = Image.new("RGB", (w, h)); im.putdata([cs[i] if i < len(cs) else (0, 0, 0) for i in px])
    q = im.resize((64, 64), Image.LANCZOS).quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    p = q.getpalette()[:45]; p += [0] * (45 - len(p))
    idx = list(q.getdata())
    tlut = [0] + [to5551(p[3 * i], p[3 * i + 1], p[3 * i + 2]) for i in range(15)]
    ci4 = bytes(((idx[i] + 1) << 4) | (idx[i + 1] + 1) for i in range(0, 4096, 2))
    return struct.pack(">16H", *tlut) + ci4

# ---- (romrender, 2026-11-18) the photo RENDERED by our own renderer (tools\romrender: the user's decision A) ---------------
# His model as the folder has it (costume 1), drawn by rr_model.photo_from_model: head and shoulders, one framing and light for
# every import -- no screenshot, the same bytes everywhere. The importers write the folder's model first, then this.
_NM = {}
def _nm_files():
    if "f" not in _NM:
        from handlers.nomercy import game as NMG
        _NM["f"] = NMG.NoMercy().files
    return _NM["f"]

def _rr():
    import os, sys
    here = os.path.dirname(os.path.abspath(__file__))
    rr_dir = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(here))), "romrender")   # (tools\romrender)
    if rr_dir not in sys.path: sys.path.insert(0, rr_dir)
    import rr_model
    return rr_model

def photo_rendered(folder, costume=0):
    """(photo.bin bytes, how) rendered from the folder's model\ (None, why: no model)"""
    import os
    if not os.path.exists(os.path.join(folder, "model", "model.json")): return None, "no model in the folder"
    RM = _rr()
    desc, get = RM.folder_getter(folder, costume, _nm_files())
    return RM.photo_from_model(desc, get), RM.PHOTO_HOW

def write_rendered(folder, costume=0):
    """photo.bin in the folder from its model (a new import's own folder: written there); -> how (None: not written)"""
    import os
    ph, how = photo_rendered(folder, costume)
    if ph is None: return None
    open(os.path.join(folder, "photo.bin"), "wb").write(ph)
    return how

def photo_from_pool(desc, pool):
    """(photo.bin bytes, how) rendered from a model an importer has built (its descriptor numbered by its Pool, the
    cartridge's own files by No Mercy's numbers) -- the model its folder gets (aki\model.py write keeps every file)"""
    RM = _rr()
    return RM.photo_from_model(desc, RM.pool_getter(desc, pool, _nm_files())), RM.PHOTO_HOW

def photo_from_picture_rr(tex, pal):
    """photo_from_picture made by our renderer's integer steps (rr.resample's box weights, rr.quantise): one of his model's
    pictures scaled to 64 x 64, 15 colours -- a stand-in, the same bytes everywhere"""
    _rr()
    import rr
    t = rr.texture_from_tex(tex, pal)
    px = [rr.rgb_of(c) + (255,) for c in t.texels]
    sm = rr.resample_rec(px, t.w, t.h, 64, 64)
    pal16, idx = rr.quantise_rec([rr.c555(*p[:3]) for p in sm], 15)
    tl = [0] + [rr.p5551(p) for p in pal16] + [0] * (15 - len(pal16))
    ci = [k + 1 for k in idx]
    return struct.pack(">16H", *tl) + bytes((ci[i] << 4) | ci[i + 1] for i in range(0, 4096, 2))
