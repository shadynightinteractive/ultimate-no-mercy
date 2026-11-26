"""An import's skin: his source game's own, all over him; only his hands are No Mercy's, drawn in that skin (2026-11-16,
skintone2).

THE RULE (the user, 2026-11-16): an imported wrestler keeps the skin tone his source game (WCW/nWo Revenge, WrestleMania
2000, Badd Blood) draws him in -- face, body, arms, legs and hands alike. It reverses the call of 0.35.12, which moved every
import's skin to the nearest of No Mercy's eight tones: imports are faithful to their source game, and only universal
gameplay mechanics come from No Mercy. (What made the user decide: Revenge's Elizabeth came out pinker and with less
contrast than her tan in Revenge, and Kimberly had lilac specks on her forehead -- the recolour's, see below.)

WHAT THE STEP DID UNTIL NOW (wmport.nm_skin, then skinqa's copy of it here, 2026-11-15) and what of it stays:
  1. Every skin picture and palette recoloured to No Mercy's tone: each part 30% skin or more had its skin colours
     multiplied by No Mercy's tone / his own (the "gain"), and skinqa's whole_tone did the same to his head and to the
     parts under 30% skin. That was there only to give him No Mercy's tone. GONE: every part keeps the source's own
     picture and palette, untouched. (It is also what made Kimberly's specks: her forehead's pale highlights, Revenge's
     (240, 184, 152), times a gain that raised blue and lowered red -- No Mercy's tones are pinker than Revenge's tan --
     became (208, 160, 184), lilac: 129 pixels of her head picture, none in Revenge's own.)
  2. No Mercy's bare arm / leg / chest pictures (wmport.NM_SKIN_PIC) put on a man's parts that were all skin, in No Mercy's
     tone. Only for No Mercy's look: those pictures are drawn for No Mercy's own meshes (biceps and veins where its arm
     meshes put them; womenfix found them landing anywhere on a source's meshes). GONE: his own pictures.
  3. No Mercy's hand meshes and hand picture (parts 9, 10, 17, 18), flagged SKEL_WM_NMHANDS. NEEDED, and kept: with the flag
     mod/src/hands.c draws the hands No Mercy's way -- every hand pose an animation names (a grab, a fist, an open hand) is
     one of No Mercy's pose meshes, drawn with the model's own hand picture, laid out as No Mercy's 32 x 16 hand pictures
     are (WrestleMania 2000's and Revenge's are 64 x 32, for their own pose meshes). The meshes are as before: a man's
     wmport.NM_HAND_MESH, a woman's WOMAN_KIT (her palms No Mercy's women's, her fingers the straight pose meshes 0x145D /
     0x145E, womenfix2). What changes is the hand picture's COLOURS: No Mercy's hand picture in the palette row nearest his
     skin at the wrist, then each colour times that skin / that row's (per channel), so the hands' mean is the skin of his
     forearms (where they are bare; else of his bare parts) and there is no seam at the wrist. (The source's own hand
     pictures are no guide: WrestleMania 2000 draws most men's palms in one shared colour, (221, 148, 116), whatever
     their skin -- 4 lighter than his body on the median, up to 21.)
His own skin (the colour the hands are brought to) is measured as before: his bare parts' skin colours (2026-11-15,
skinqa: first in the SKIN_HUE window so tan, khaki or gold attire doesn't set it, then the colours near it); a body "skin"
far darker than his face is dark attire (his face's); a costume with no bare skin takes his other costumes'.
"""
import colorsys, math, struct

SKIN_HUE = (5.0, 25.0)   # (first look at his tone: nm_skin's test and an HSV hue in this window -- leaves out tan and gold
                         #  above, red trunks and dark red leather below (355-360: Gangrel's trousers); skin is 8-23)
SKIN_DH = 12.0   # (near his skin: CIE Lab hue angle within this of his tone's: tan trousers are 18 off, gold 39, red trunks
                 #  24, dark red / maroon leather 17-22, blond hair 42; a highlight or a shadow on skin 0-8)
SKIN_C = 0.4     # (... and its chroma at least this share of his tone's: not a grey)
SKIN_DL = (45.0, 35.0)   # (... and its lightness at most this much below / above his tone's: shadows deep, highlights bright)

# (2026-11-15, womenfix; 2026-11-16, womenfix2) WHICH No Mercy hands the step puts on. wmport's NM_HAND_MESH / NM_HAND_PIC
# are the hands No Mercy's own builder gives a MAN of body type 3 (Medium 2: The Rock's build). A woman gets No Mercy's
# women's (body type 15, Stephanie McMahon: its builder func_80018A3C run on its select RAM, womenfix scripts\nmdesc2.py):
# palms 0x118A / 0x1191 and picture 0x2A96 (palette 0x2A95), the same layout as the men's set. Her FINGERS are No Mercy's
# straight, closed-up pose meshes 0x145D (side 0, part 10) / 0x145E (side 1, part 18) -- 0x80048418's lists, loaded by
# func_8001692C: every stance's hand event is hand pose 0, drawn with the descriptor's own hand meshes, and No Mercy's
# women's own fingers 0x118B / 0x1192 are curled like the men's (womenfix2 work\evidence\hand_options.png). They use the
# same hand picture (u 0..8, v 0..15, as 0x118B), so the other hand poses are as before.
class Kit:
    def __init__(self, name, hand_mesh, hand_pic):
        self.name, self.hand_mesh, self.hand_pic = name, hand_mesh, hand_pic

WOMAN_KIT = Kit("a woman's", {9: 0x118A, 10: 0x145D, 17: 0x1191, 18: 0x145E}, (0x2A96, 0x2A95))

def _lab(c):
    def lin(u):
        u /= 255.0
        return u / 12.92 if u <= 0.04045 else ((u + 0.055) / 1.055) ** 2.4
    r, g, b = (lin(x) for x in c)
    x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    return (116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z)))

def _lch(c):
    L, a, b = _lab(c)
    return L, math.hypot(a, b), math.degrees(math.atan2(b, a))

def _hsv_hue(c):
    h = colorsys.rgb_to_hsv(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0)[0] * 360.0
    return h - 360.0 if h > 345.0 else h

DEEP_DH = 6.0    # (a colour more saturated than wmport._skin allows is still his skin when its HSV hue is this close to his own
                 #  skin's -- and inside SKIN_HUE: Jacqueline's deep red-brown is 11-15 degrees, her skin 13)

def deep_skin(c, own):
    """his skin, however saturated (2026-11-16, skintone2 round 2): wmport._skin, or a colour past its saturation cap (0.78)
    in his own skin's hue. WrestleMania 2000 draws Jacqueline's skin a deep red-brown, (105, 34, 14) on her forearms --
    saturation 0.83 to 1.0 -- so only her forearms' highlights (21% of the picture, (162, 79, 53)) counted as skin, her hands
    were brought to them, and in the game they stood out lighter and less red than her arms (QA, 2026-11-16). Only in his own
    hue: red attire is as saturated (Shawn Michaels' pads (208, 8, 24), Mideon's and Viscera's red at 355-0 degrees) and must
    stay out; a first try without the hue test let it in."""
    if W_skin(c): return True
    r, g, b = c
    hh, ss, vv = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    h, h0 = _hsv_hue(c), _hsv_hue(own)
    return ss > 0.78 and vv >= 0.15 and r > b + 25 and SKIN_HUE[0] <= h < SKIN_HUE[1] and abs(h - h0) <= DEEP_DH

def W_skin(c):
    """wmport._skin (the same test, here so deep_skin can call it without the module)"""
    r, g, b = c
    hh, ss, vv = colorsys.rgb_to_hsv(r / 255.0, g / 255.0, b / 255.0)
    return (hh < 50 / 360 or hh > 345 / 360) and 0.15 <= ss <= 0.78 and vv >= 0.22 and r > b + 25

def near_skin(c, own):
    """is colour c his skin (lit or shaded), his own skin's mean colour being own? -- see SKIN_DH, SKIN_C, SKIN_DL"""
    L, C, h = _lch(c); L0, C0, h0 = _lch(own)
    return abs((h - h0 + 180.0) % 360.0 - 180.0) <= SKIN_DH and C >= SKIN_C * C0 and L0 - SKIN_DL[0] <= L <= L0 + SKIN_DL[1]

FACE_PIX = 150   # (his face's tone is taken when his head's pictures show at least this many pixels of skin)
FACE_DARKER = 25.0   # (a body "skin" this much darker (CIE L*) than his face's is dark attire -- leather, a brown suit: a
                     #  face and a body drawn alike are within 8 in the source; Dr. Frank's trousers 46 darker. Only darker:
                     #  a face darker than the body is a mask, paint or hair on the head's pictures -- La Parka, Shogun,
                     #  Ivory -- and says nothing of the body)

FACE_FAR = 15.0  # (a face this far from his other costumes' skin -- Lab, lightness at half weight -- is a variant drawn in
                 #  another colour: a face and a body drawn alike are 2-8 apart in the source; Dr. Frank's red variant 30)

WRIST_PIX = 200  # (a forearm's skin is his wrists' colour when it shows at least this many skin pixels -- a tenth of a
                 #  32 x 64 forearm picture: WrestleMania 2000 draws many forearms part sleeve or band, Edge's 42% skin)

def _far(a, b):
    la, lb = _lab(a), _lab(b)
    return math.sqrt((0.5 * (la[0] - lb[0])) ** 2 + (la[1] - lb[1]) ** 2 + (la[2] - lb[2]) ** 2) > FACE_FAR

def _darker(body, face):
    return _lab(face)[0] - _lab(body)[0] > FACE_DARKER

def _pack(r, g, b, alpha):
    """an RGBA5551 colour, each channel rounded to the nearest of its 32 steps"""
    q = lambda x: min(31, max(0, int(x / 8.0 + 0.5)))
    return (q(r) << 11) | (q(g) << 6) | (q(b) << 1) | alpha

def hand_mean(W, pal_file, row):
    """No Mercy's hand picture's mean colour in palette row `row` of its palette file (pixels weighted as drawn)"""
    pic_tex = W.nmf(pal_file[0]); pal = W._bake(W.nmf(pal_file[1]), row)
    cs = [W._rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(16)]
    px = W._pix(pic_tex)
    return tuple(sum(cs[i][j] for i in px) / len(px) for j in range(3))

def hand_in_skin(W, hand_pic, have):
    """No Mercy's hand picture (hand_pic: its picture and palette files) with its colours in his skin, have: the palette row
    nearest it, each colour times have / that row's mean, per channel -- its shading No Mercy's, its mean his.
    -> (picture, palette, row, gain)"""
    means = [hand_mean(W, hand_pic, r) for r in range(8)]
    row = min(range(8), key=lambda r: sum((a - b) ** 2 for a, b in zip(means[r], have)))
    gain = [have[j] / max(1.0, means[row][j]) for j in range(3)]
    pal = bytearray(W._bake(W.nmf(hand_pic[1]), row))
    for i in range(16):                    # (a 4-bit picture: the palette's first 16 colours; every one a skin shade)
        v = struct.unpack_from(">H", pal, 2 * i)[0]
        c = W._rgb(v)
        struct.pack_into(">H", pal, 2 * i, _pack(*[min(255.0, c[j] * gain[j]) for j in range(3)], v & 1))
    return W.nmf(hand_pic[0]), bytes(pal), row, gain

def nm_hands(W, model, F, name, report, skins=None, who=None, kit=None):
    """a ported model (its descriptor, files in F; W: the wmport module) with No Mercy's hands in his own skin, every other
    part as the source drew it. skins: who -> the skin seen on his costumes so far (a costume with no bare skin takes his
    other's). kit: WOMAN_KIT for a woman (No Mercy's women's hands); None, a man: wmport's NM_HAND_MESH / NM_HAND_PIC.
    Raises SystemExit when there is no skin to go by (the caller keeps the source's own hands then)."""
    d = bytearray(model)
    hand_mesh = kit.hand_mesh if kit else W.NM_HAND_MESH
    hand_pic = kit.hand_pic if kit else W.NM_HAND_PIC
    def data(i): return F.data[i - W.WMP_FILE0] if i >= W.WMP_FILE0 else W.nmf(i)
    pics = {}
    for k in range(1, 20):
        m, t, q = struct.unpack_from(">HHH", d, 8 * k)
        if not t or k in W.SKIN_KEEP: continue
        tex, pal = data(t), data(q)
        cs = [W._rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(len(pal) // 2)]
        pics[k] = (tex, pal, cs, W._pix(tex))
    def measure(test):
        out = {}
        for k, (tex, pal, cs, px) in pics.items():
            sk = [i for i in px if i < len(cs) and test(cs[i])]
            out[k] = dict(frac=len(sk) / max(1, len(px)), n=len(sk),
                          avg=tuple(sum(cs[i][j] for i in sk) / max(1, len(sk)) for j in range(3)))
        return out
    def mean_of(parts):
        full = [k for k, p in parts.items() if p["frac"] >= 0.95 and k not in W.NM_HAND_MESH]
        src = full or [k for k, p in parts.items() if p["frac"] >= 0.5]
        tot = sum(parts[k]["n"] for k in src)
        return tuple(sum(parts[k]["avg"][j] * parts[k]["n"] for k in src) / tot for j in range(3)) if tot else None
    # (his own skin first from colours in the SKIN_HUE window; then skin is what is near it -- else his other costumes'
    #  tone. And a body "skin" far darker than his face's is dark attire -- Dr. Frank's brown leather trousers (74, 38, 29)
    #  on a pale man: his face's tone instead)
    first = lambda c: W._skin(c) and SKIN_HUE[0] <= _hsv_hue(c) < SKIN_HUE[1]
    own = mean_of(measure(first))
    face = None
    acc, n = [0.0, 0.0, 0.0], 0
    for k in W.SKIN_KEEP:
        m, t, q = struct.unpack_from(">HHH", d, 8 * k)
        if not t: continue
        pal = data(q); cs = [W._rgb(struct.unpack_from(">H", pal, 2 * i)[0]) for i in range(len(pal) // 2)]
        for i in W._pix(data(t)):
            if i < len(cs) and first(cs[i]): acc = [a + c for a, c in zip(acc, cs[i])]; n += 1
    if n >= FACE_PIX: face = tuple(a / n for a in acc)
    from_face = False
    ref = face if face is not None else skins.get(who) if skins is not None else None   # (no face to go by: his main tone)
    if ref is not None and own is not None and _darker(own, ref):
        report.append("%s: his body's skin colours (%s) are far darker than his %s (%s): attire -- that tone" % (
            name, ", ".join("%d" % x for x in own), "face's" if face is not None else "main variant's",
            ", ".join("%d" % x for x in ref)))
        own, from_face = ref, True
    if own is None and skins is not None and who in skins: own = skins[who]
    if own is None: own = mean_of(measure(W._skin))           # (nothing else to go by: the plain test's, as nm_skin)
    if own is None:
        raise SystemExit("%s: no bare skin in any costume so far (its skin tone unknown)" % name)
    is_skin = lambda c: W._skin(c) and near_skin(c, own)
    parts = measure(is_skin)
    full = [k for k, p in parts.items() if p["frac"] >= 0.95 and k not in W.NM_HAND_MESH]
    src = full or [k for k, p in parts.items() if p["frac"] >= 0.5]
    keep = None
    if skins is not None and who in W.REV_PORTED:
        if (who, "bare") not in skins: skins[(who, "bare")] = set(full)
        else:
            bare = [k for k in skins[(who, "bare")] if k in parts and parts[k]["frac"] >= 0.95]
            if bare: src = bare
            elif who in skins: keep = skins[who]
    tot = sum(parts[k]["n"] for k in src)
    if keep is not None:
        have = keep; report.append("%s: none of his costume 0's bare parts bare -- his costume 0's tone" % name)
    elif tot:
        have = tuple(sum(parts[k]["avg"][j] * parts[k]["n"] for k in src) / tot for j in range(3))
        if skins is not None and who not in skins: skins[who] = have
    elif skins is not None and who in skins and face is not None and _far(face, skins[who]):
        # (no bare skin, and his face isn't his other costumes' skin: the source draws this variant in another colour --
        #  Dr. Frank's red variant 1 -- so its own face's tone, not theirs)
        have = face; report.append("%s: no bare skin in this costume, his face (%s) unlike his other costumes' -- his face's tone" % (
            name, ", ".join("%d" % x for x in face)))
    elif skins is not None and who in skins:
        have = skins[who]; report.append("%s: no bare skin in this costume -- his other costume's tone" % name)
    elif from_face:
        have = own; report.append("%s: no bare skin -- the tone his face's or main variant's" % name)
        if skins is not None: skins[who] = have
    else:
        raise SystemExit("%s: no bare skin in any costume so far (its skin tone unknown)" % name)
    # ---- the hands: No Mercy's meshes, No Mercy's hand picture in his skin; nothing else changes ---------------------------
    # (the skin at his WRISTS: his forearms' (parts 11 and 16) where they show skin, so hand and forearm meet in one colour
    #  -- the source draws a forearm up to 7 (CIE Lab) lighter or darker than the mean of his bare parts: Edge, Ken
    #  Shamrock, Buff Bagwell. Not when a forearm's "skin" is far from his (FACE_FAR): a brown suit's sleeve -- Gerald
    #  Brisco's variant 1. Covered forearms: his skin's mean)
    #  (2026-11-16, skintone2 round 2: the forearms' skin taken with deep_skin -- however saturated, in his own hue: only
    #  here, at the wrist. Taken so for his whole body it let deep orange or red attire in -- Alex Wright's costume 3 orange
    #  trunks, (192, 72, 32), became "his skin")
    wparts = {k: v for k, v in measure(lambda c: deep_skin(c, own) and near_skin(c, own)).items() if k in (11, 16)}
    fore = [k for k in (11, 16) if k in wparts and wparts[k]["n"] >= WRIST_PIX]
    wrist = tuple(sum(wparts[k]["avg"][j] * wparts[k]["n"] for k in fore) / sum(wparts[k]["n"] for k in fore)
                  for j in range(3)) if fore else None
    target, at = (wrist, "his forearms'") if wrist is not None and not _far(wrist, have) else (have, "his skin's")
    tex, pal, row, gain = hand_in_skin(W, hand_pic, target)
    t, q = F.pair(tex, pal)
    for k, mesh in hand_mesh.items():
        struct.pack_into(">HHH", d, 8 * k, mesh, t, q)
    struct.pack_into(">I", d, W.SKEL_WM_FLAGS, struct.unpack_from(">I", d, W.SKEL_WM_FLAGS)[0] | W.SKEL_WM_NMHANDS)
    report.append("%s: skin %s kept (his source game's) -- No Mercy's %shands in %s %s: hand palette row %d x %s" % (
        name, tuple(int(x) for x in have), "women's " if kit else "", at, tuple(int(x) for x in target), row,
        tuple(round(x, 3) for x in gain)))
    return bytes(d)
