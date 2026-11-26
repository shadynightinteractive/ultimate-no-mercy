# export.py: everything the native test program needs, into native_test/data/game.pak: the two 2K15 men (data/austin2k15, data/rock2k15: read only), their pictures, the moves' animations (No Mercy's files, decoded), the arena (build/arena.pkl, capture_arena.py).
# Temporary test: nothing outside native_test is written.
import json, os, pickle, struct, sys
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(HERE + '/../..')
sys.path.insert(0, ROOT); sys.path.insert(0, ROOT + '/data/announce')
from nmpaths import nmp
import fk
F = nmp('/tmp/devbox-0/nmfiles/f/%04x.bin')
OUT = HERE + '/../data/game.pak'
os.makedirs(os.path.dirname(OUT), exist_ok=True)

MEN = (dict(name='STONE COLD', build=ROOT + '/data/austin2k15/build', tex=ROOT + '/data/austin2k15/src/Stone Cold Steve Austin/textures/%s.dds'),
       dict(name='THE ROCK', build=ROOT + '/data/rock2k15/build', tex=ROOT + '/data/rock2k15/src/TheRock1/%s.png'))
MATS = ('all_color', 'head', 'shoes', 'l_kneepad', 'r_kneepad', 'eye')
# The moves (names as main.cpp's CLIPS enum, in order). From the game's move records (file 0x69) and its table of animation files; the rest told by eye, tools/sheet.py
CLIPS = [('idle', 0x2DD9), ('walk', 0x2DE8), ('run', 0x4473), ('punch', 0x3AC4), ('hit', 0x3AB7), ('kick', 0x41E3), ('clothes', 0x3445), ('clothes_d', 0x3447),
         ('suplex', 0x33B3), ('suplex_d', 0x33B5), ('stunner', 0x37EE), ('stunner_d', 0x37EF), ('rockbottom', 0x37FA), ('rockbottom_d', 0x37FB),
         ('taunt_a', 0x43F8), ('taunt_r', 0x441D), ('getup', 0x2F79), ('getup2', 0x2F7E)]

out = bytearray()
def u32(v): out.extend(struct.pack('<I', v))
def fl(*v): out.extend(struct.pack('<%df' % len(v), *v))
def blob(b): out.extend(b)

textures = []          # (w, h, rgba)
def add_tex(w, h, rgba):
    textures.append((w, h, rgba)); return len(textures) - 1

men_bytes = []
for man in MEN:
    D = pickle.load(open(man['build'] + '/hd.pkl', 'rb'))
    pos, uv, nrm, bones, wts, finger, curled, straight = (D[k] for k in ('pos', 'uv', 'nrm', 'bones', 'weights', 'finger', 'curled', 'straight'))
    shade = np.clip(0.70 + 0.22 * nrm[:, 1] + 0.12 * np.abs(nrm[:, 2]) + 0.04 * np.abs(nrm[:, 0]), 0.45, 1.0)
    tex_of = {}
    for m in MATS:
        im = Image.open(man['tex'] % m).convert('RGB')
        rgba = im.convert('RGBA').tobytes()
        tex_of[m] = add_tex(im.size[0], im.size[1], rgba)
        print(' ', man['name'], m, im.size)
    # vertices: pos3 uv2 shade1 | closed3 straight3 | weights3 | bones4(u8: three bones, the finger + 1)
    n = len(pos)
    hand = np.where(np.isin(bones[:, 0], (17,)), 1, 0)    # (17: his left: which of the two hand shapes)
    v = np.zeros(n, dtype=[('p', '<3f'), ('uv', '<2f'), ('s', '<f'), ('c', '<3f'), ('o', '<3f'), ('w', '<3f'), ('b', 'u1', (3,)), ('f', 'u1')])
    v['p'] = pos; v['uv'] = np.stack([uv[:, 0], 1 - uv[:, 1]], 1); v['s'] = shade
    v['c'] = curled - pos; v['o'] = straight - pos; v['w'] = wts; v['b'] = bones[:, :3]
    v['f'] = np.where(finger >= 0, finger + 1 + hand * 8, 0)
    batches = []; idx = []
    for m in MATS:
        ts = [t for (name, mt), tl in D['tris'].items() if mt == m for t in tl]
        if not ts: continue
        batches.append((tex_of[m], len(idx), len(ts) * 3)); idx.extend(i for t in ts for i in t)
    sk = {int(k): v_ for k, v_ in json.load(open(man['build'] + '/joints.json')).items()}
    ORDER = (2, 3, 5, 12, 11, 9, 19, 16, 17, 7, 6, 8, 14, 13, 15)
    joints = [(k, sk[k][0], *sk[k][1]) for k in ORDER]
    men_bytes.append((man['name'], v.tobytes(), n, np.array(idx, np.uint32).tobytes(), len(idx), batches, joints))
    print(man['name'], 'points', n, 'triangles', len(idx) // 3)

# the arena: per picture and wrap, in the game's order
A = pickle.load(open(HERE + '/../build/arena.pkl', 'rb'))
atex = {}
def arena_tex(k):
    if k not in atex:
        w, h, rgba = A['tex'][k]
        atex[k] = add_tex(w, h, rgba)
    return atex[k]
av = bytearray(); ab = []; n_av = 0
items = sorted(A['items'].values(), key=lambda i: i['order'])
floor = 0.0
for it in items:
    cur = None
    for tk, cms, cmt, ws in it['tris']:
        if tk is None: continue
        key = (tk, cms, cmt)
        if key != cur:
            ab.append([-1 if tk == 'SCREEN' else arena_tex(tk), cms, cmt, n_av, 0]); cur = key
        for w in ws:
            av += struct.pack('<5f4B', *w[:5], *w[5:9]); n_av += 1
        ab[-1][4] += 3
print('arena: %d triangles, %d batches, %d pictures' % (n_av // 3, len(ab), len(atex)))

# ---- write
blob(b'NMT1')
u32(len(textures))
for w, h, r in textures:
    u32(w); u32(h); u32(len(r)); blob(r)
u32(len(men_bytes))
for name, vb, n, ib, ni, batches, joints in men_bytes:
    nb = name.encode().ljust(16, b'\0'); blob(nb)
    u32(n); u32(ni); blob(vb); blob(ib)
    u32(len(batches))
    for t, s, c in batches: u32(t); u32(s); u32(c)
    u32(len(joints))
    for b_, p_, x, y, z in joints: u32(b_); u32(p_); fl(x, y, z)
u32(len(CLIPS))
for name, f in CLIPS:
    kfs, n = fk.decode(open(F % f, 'rb').read())
    blob(name.encode().ljust(16, b'\0')); u32(len(kfs)); u32(n)
    for kf in kfs:
        for b in range(20):
            rot, p = kf[b]
            fl(*rot, *p)
    print(' clip %-14s file %x: %d frames, %d keys, root %s -> %s' % (name, f, n, len(kfs), np.round(kfs[0][1][1], 0), np.round(kfs[-1][1][1], 0)))
u32(n_av); blob(av)
u32(len(ab))
for t, cms, cmt, s, c in ab:
    out.extend(struct.pack('<iIIII', t, cms, cmt, s, c))
open(OUT, 'wb').write(out)
print('pak: %d MB, %d textures' % (len(out) // (1 << 20), len(textures)))
