# capture_arena.py <out.pkl> <state.rdram.bin> ...: the arena as the game draws it, out of QA runs' RAM: the scene's draw items (NM_ITEMS, tron.c), each item's points, its display list's triangles and pictures, baked to world
# coordinates. Wrestlers and the HUD left out. Several dumps (other camera angles) are merged: an item the camera had culled in one is in another. Temporary test program only; nothing here is read by the mod or the game.
import pickle, struct, sys, hashlib
import numpy as np

NM_ITEMS, NM_ITEM_N = 0x80060BA0, 0x800619B2
BONES0, BONE_SPAN = 0x8050B430, 6 * 0x1810
TRON_IMG = 0x806C0000

class Ram:
    def __init__(s, path): s.d = open(path, 'rb').read()
    def ok(s, a): return 0x80000000 <= a < 0x80000000 + len(s.d) - 4
    def u32(s, a): return struct.unpack_from('<I', s.d, a - 0x80000000)[0]
    def s16(s, a): return struct.unpack_from('<h', s.d, (a ^ 2) - 0x80000000)[0]
    def u16(s, a): return struct.unpack_from('<H', s.d, (a ^ 2) - 0x80000000)[0]
    def u8(s, a): return s.d[(a ^ 3) - 0x80000000]
    def f32(s, a): return struct.unpack_from('<f', s.d, a - 0x80000000)[0]
    def mat(s, a): return np.array(struct.unpack_from('<16f', s.d, a - 0x80000000), np.float64).reshape(4, 4)
    def bytes_(s, a, n): return bytes(s.u8(a + i) for i in range(n))

def phys(a): return (a & 0x1FFFFFFF) | 0x80000000

def rgba5551(v):
    return ((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31, 255 if v & 1 else 0

def decode_tex(R, addr, fmt, siz, w, h, line_bytes, tlut):
    """the picture as RGBA bytes (N64: fmt 0 RGBA, 2 CI, 3 IA, 4 I; siz 0 4-bit, 1 8-bit, 2 16-bit, 3 32-bit)"""
    out = bytearray(w * h * 4)
    for y in range(h):
        row = addr + y * line_bytes
        for x in range(w):
            if siz == 0:
                b = R.u8(row + x // 2); v = (b >> 4) if x % 2 == 0 else (b & 15)
            elif siz == 1: v = R.u8(row + x)
            elif siz == 2: v = R.u16(row + x * 2)
            else: v = None
            if fmt == 2:
                c = rgba5551(R.u16(tlut + v * 2)) if tlut else (v, v, v, 255)
            elif fmt == 0 and siz == 2: c = rgba5551(v)
            elif fmt == 0 and siz == 3: c = (R.u8(row + x * 4), R.u8(row + x * 4 + 1), R.u8(row + x * 4 + 2), R.u8(row + x * 4 + 3))
            elif fmt == 3 and siz == 0: i = (v >> 1) * 255 // 7; c = (i, i, i, 255 if v & 1 else 0)
            elif fmt == 3 and siz == 1: i = (v >> 4) * 17; c = (i, i, i, (v & 15) * 17)
            elif fmt == 3 and siz == 2: c = (v >> 8, v >> 8, v >> 8, v & 255)
            elif fmt == 4 and siz == 0: c = (v * 17, v * 17, v * 17, 255)
            elif fmt == 4 and siz == 1: c = (v, v, v, 255)
            else: c = (255, 0, 255, 255)
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(c)
    return bytes(out)

def walk_dl(R, dl, st, emit, depth=0):
    n = 0
    a = phys(dl)
    while n < 4000:
        n += 1
        if not R.ok(a): return
        w0, w1 = R.u32(a), R.u32(a + 4); a += 8
        op = w0 >> 24
        if op == 0xDF: return
        if op == 0xDE:
            if w1 >> 24 == 0x01 or w1 < 0x1000: pass          # (a segment's: the game's own setup)
            elif depth < 8: walk_dl(R, w1, st, emit, depth + 1)
            if (w0 >> 16) & 0xFF: return                      # (a branch)
        elif op == 0x01:
            cnt = (w0 >> 12) & 0xFF; end = (w0 >> 1) & 0x7F; first = end - cnt
            load_vtx(R, phys(w1), first, cnt, st)
        elif op == 0x05: emit(st, (w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF)
        elif op == 0x06:
            emit(st, (w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF)
            emit(st, (w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF)
        elif op == 0x07:
            emit(st, (w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF)
            emit(st, (w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF)
        elif op == 0xFD:
            st['img'] = phys(w1); st['fmt'] = (w0 >> 21) & 7; st['siz'] = (w0 >> 19) & 3; st['imgw'] = (w0 & 0xFFF) + 1
        elif op == 0xF0:
            st['tlut'] = st['img']
        elif op in (0xF3, 0xF4):
            st['tex_img'] = st['img']; st['tex_fmt0'] = st['fmt']; st['tex_siz0'] = st['siz']; st['tex_imgw'] = st['imgw']
        elif op == 0xF5:
            tile = (w1 >> 24) & 7
            if tile == 0:
                st['tile'] = dict(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3, line=(w0 >> 9) & 0x1FF, cmt=(w1 >> 18) & 3, shiftt=(w1 >> 10) & 15, cms=(w1 >> 8) & 3, shifts=w1 & 15)
        elif op == 0xF2:
            if ((w1 >> 24) & 7) == 0:
                st['sl'] = ((w0 >> 12) & 0xFFF) / 4.0; st['tl'] = (w0 & 0xFFF) / 4.0
                st['w'] = int(round(((w1 >> 12) & 0xFFF) / 4.0 - st['sl'])) + 1; st['h'] = int(round((w1 & 0xFFF) / 4.0 - st['tl'])) + 1

def load_vtx(R, a, first, cnt, st):
    for i in range(cnt):
        p = a + i * 16
        if not R.ok(p): return
        st['v'][first + i] = (R.s16(p), R.s16(p + 2), R.s16(p + 4), R.s16(p + 8), R.s16(p + 10), R.u8(p + 12), R.u8(p + 13), R.u8(p + 14), R.u8(p + 15))

def capture(path, items_out, tex):
    R = Ram(path)
    n = R.s16(NM_ITEM_N)
    print(path[-60:], 'items', n)
    seen = set(); kept = 0
    for i in range(n):
        it = R.u32(NM_ITEMS + 4 * i)
        while R.ok(it) and it >= 0x80000400 and it not in seen:
            seen.add(it)
            kind, node = R.u32(it), R.u32(it + 4)
            nxt = R.u32(it + 8)
            if kind <= 1 and R.ok(node) and node >= 0x80000400:
                f = R.u8(node + 0x18)
                hud = R.u8(node + 0x19)
                wrestler = BONES0 <= node < BONES0 + BONE_SPAN
                dl = R.u32(it + 0x34)
                if kind == 0 and R.u32(node + 0x10): dl = R.u32(node + 0x10)
                if not (f & 0x80) and not hud and not wrestler and dl:
                    st = dict(v={}, img=0, fmt=0, siz=0, imgw=0, tlut=0, tile=dict(fmt=0, siz=0, line=0, cmt=0, shiftt=0, cms=0, shifts=0), sl=0.0, tl=0.0, w=32, h=32, tex_img=0, tex_imgw=0)
                    va = R.u32(it + 0x30); cnt = R.s16(it + 0x2C); first = R.s16(it + 0x2E)
                    if va and cnt > 0 and R.ok(phys(va)): load_vtx(R, phys(va), first, cnt, st)
                    M = R.mat(node + 0x5C)
                    tris = []
                    def emit(s, a, b, c):
                        a //= 2; b //= 2; c //= 2
                        if a not in s['v'] or b not in s['v'] or c not in s['v']: return
                        t = s['tile']
                        key = (s.get('tex_img'), s.get('tlut') if t['fmt'] == 2 else 0, t['fmt'], t['siz'], s['w'], s['h'], t['line'], t['cms'], t['cmt'], s.get('tex_imgw', 0))
                        tris.append((key, s['sl'], s['tl'], t['shifts'], t['shiftt'], s['v'][a], s['v'][b], s['v'][c]))
                    walk_dl(R, dl, st, emit)
                    if tris:
                        sig = (node, dl)                 # (the same piece in another run: the first's. One that's moved isn't the arena's: a shadow, a man)
                        if sig in items_out:
                            if np.abs(items_out[sig]['M'][3, :3] - M[3, :3]).max() > 2.0: items_out[sig]['moved'] = True
                        else:
                            tl = []
                            for key, sl, tl_, ss, st_, *vs in tris:
                                tk = None
                                img, pal, fmt, siz, w, h, line, cms, cmt, imgw = key
                                if img == TRON_IMG: tk = 'SCREEN'
                                elif img and R.ok(img) and 0 < w <= 1024 and 0 < h <= 1024:
                                    lb = (line * 8) if line else max(1, (imgw * (4 << siz)) // 8)
                                    if lb == 0: lb = (w * (4 << siz)) // 8
                                    # (the picture's own bytes make its name: the same picture in two runs is one)
                                    raw_key = (img, pal, fmt, siz, w, h, lb)
                                    cache = tex.setdefault('_c', {})
                                    ck = (path, raw_key)
                                    if ck not in cache:
                                        rgba = decode_tex(R, img, fmt, siz, w, h, lb, pal)
                                        h_ = hashlib.md5(rgba).hexdigest()
                                        tex[h_] = (w, h, rgba); cache[ck] = h_
                                    tk = cache[ck]
                                ws = []
                                for v in vs:
                                    p = np.array([v[0], v[1], v[2], 1.0]) @ M
                                    sc = 1.0 / (1 << ss) if ss < 11 else float(1 << (16 - ss))
                                    tc = 1.0 / (1 << st_) if st_ < 11 else float(1 << (16 - st_))
                                    ws.append((float(p[0]), float(p[1]), float(p[2]), (v[3] / 32.0 * sc - sl) / max(w, 1), (v[4] / 32.0 * tc - tl_) / max(h, 1), v[5], v[6], v[7], v[8]))
                                tl.append((tk, cms, cmt, ws))
                            items_out[sig] = dict(order=len(items_out), key=float(R.f32(it + 0x24)), tris=tl, node=node, kind=kind, M=M, moved=False)
                            kept += 1
            it = nxt
    print('  new items', kept, 'total', len(items_out), 'textures', len(tex) - 1)

if __name__ == '__main__':
    items = {}; tex = {}
    for p in sys.argv[2:]: capture(p, items, tex)
    tex.pop('_c', None)
    # (what moved: the men's own shadows and such, in the bones' memory, are out. The ropes also move, a little, when they're hit: they stay, as they were in the first run)
    for k, v in items.items():
        if v['moved']:
            p = np.array([w[:3] for t in v['tris'] for w in t[3]]); print('  moved: node %x, %d triangles, x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f' % (k[0], len(v['tris']), p[:, 0].min(), p[:, 0].max(), p[:, 1].min(), p[:, 1].max(), p[:, 2].min(), p[:, 2].max()))
    mv = [k for k, v in items.items() if v['moved'] and not (0x80390000 <= k[0] < 0x803C0000)]
    print('left out, they move:', len(mv), [hex(k[0]) for k in mv][:30])
    for k in mv: del items[k]
    tris = sum(len(v['tris']) for v in items.values())
    print('items', len(items), 'triangles', tris, 'textures', len(tex))
    pickle.dump(dict(items=items, tex=tex), open(sys.argv[1], 'wb'))
