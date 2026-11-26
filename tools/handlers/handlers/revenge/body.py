"""A Revenge Superstar's body, and the No Mercy body type it becomes (revbody, 2026-11-13).

What WCW/nWo Revenge keeps for each man's body: one 16-bit "body word" per model code (the table of pointers at
0x80031998, one list per group, code >> 8; entry (code & 0xFF) - 1). Its two parts, found in Revenge's code:
  - bits 0-6, the FRAME: which body his meshes are made for. Revenge's attires are grouped by it (the attire tables
    0x800360F0 / 0x8003673C take it as their index: 24, 63, 33, 20 and 7 attires for frames 0-4) and every attire's
    meshes fit only its frame. Seen on the roster: 0 the small men (Rey, Juventud, Kidman, Psychosis ...), 1 and 2 the
    two standard frames (Sting, Bret, Hogan ... / The Giant, Piper, Scott Steiner, Konnan ...), 3 the heavy men (Neidhart,
    Meng, Scott Norton, Kim Chee ...), 4 the people who don't wrestle (managers, valets).
  - bits 12-14, the SCALE: 0x8000B370 turns it into a float from the table at 0x800318C0 (1.00, 0.96, 1.02, 0.98, 1.04,
    1.06, 1.07, 1.08), and 0x80008C94 makes the man's whole model that size (func_8001BE70 with it as x, y and z: a uniform
    scale on his slot's model matrix, 0x800706C4 + 0xFC x slot -- seen in a match in the rig: The Giant's is 1.08).
    It is Revenge's only size knob: its heights and weights are text on the select screen.
The shape of his body -- broad, lean, big -- is in his meshes, which the handler already brings over whole (his model).

What No Mercy does with a costume's body type (the looks' first field) once the model is a folder's own: the meshes it
would pick are replaced by his, so what is left is (build_model_from_looks, 0x800148F0) the body type's entry in the table
0x80048A48, which picks the belt mesh he wears as a champion (0x80052D00, set_belt_model) and the hands' class (0x80047FE8:
the fingers' scale and place, 0x80012BE0 -- 0 the Skinny types' smaller fingers, 1 the Medium and Thick types' as they
are, 2 the Fat ones', 4 the women's). His size is not the body type's: No Mercy scales every man by his height
(0.97 + 0.00047 x height x 255 / 36: superstar_size, slot_model_scale), and the handler writes Revenge's height.

So the body type is chosen as No Mercy's own Create-a-Wrestler builds (0 Skinny 1 .. 7 Fat 2) would describe his frame:
the frame picks the family, his Revenge weight the step within it; Revenge's women get No Mercy's women's type (15).
A port whose type was chosen by hand keeps it: "body_type" in his wrestlers/<id>.json, with its reason.
"""
import struct

SCALE_TABLE = 0x800318C0      # Revenge: 8 floats, the scale classes
BODY_WORDS = 0x80031998       # Revenge: a pointer per group to its u16 body words
FRAME_ATTIRES = 0x8003673C    # Revenge: s16 per frame, how many attires it has

# No Mercy's (read from its ROM data, van_sel_ram.bin; the names are the Create-a-Wrestler lists')
NM_NAMES = {0: "Skinny 1", 1: "Skinny 2", 2: "Medium 1", 3: "Medium 2", 4: "Thick 1", 5: "Thick 2", 6: "Fat 1", 7: "Fat 2",
            15: "a woman's"}
NM_BELT = [0, 0, 1, 1, 2, 2, 3, 4, 1, 5, 1, 0, 2, 6, 6, 7, 7, 8, 9, 9, 9, 10, 1, 1]   # 0x80048A48[body type] (low byte)
NM_HAND_CLASS = [0, 1, 1, 2, 3, 2, 4, 4, 4, 4, 4]                                     # 0x80047FE8[that]
WOMEN_GROUP = 9               # Revenge's group 9: Elizabeth, Kimberly, Jackreen, Sister Sherri, Woman
NM_WOMAN = 15                 # the women's body type most of No Mercy's women have (its roster: 13-21 are women's)
# A woman's type needs a woman's parts in the looks: the game builds its own model of the looks (func_80018A3C) before the
# folder's model replaces it, and The Rock's costumes with type 15 made it read past its tables and crash as the match
# loaded (QA rb_body_13 / rb_body_14, 2026-11-13). So a valet's looks are a No Mercy woman's costumes -- ordinal 51
# (0x0702), type 15 in all four -- with only the parts; whom she's based on (moves, the rest) stays as it was.
NM_WOMAN_LOOKS = 51

class Tables:
    """Revenge's body tables, from the same boot RAM the roster is read from (they are the ROM's own data: the main code
    segment, ROM offset = address - 0x80000400 + 0x1000)"""
    def __init__(self, ram):
        self.r = ram
        self.scales = struct.unpack_from(">8f", ram, SCALE_TABLE & 0x7FFFFF)
        self.attires = struct.unpack_from(">5h", ram, FRAME_ATTIRES & 0x7FFFFF)
    def word(self, code):
        p = struct.unpack_from(">I", self.r, (BODY_WORDS & 0x7FFFFF) + 4 * (code >> 8))[0]
        return struct.unpack_from(">H", self.r, (p & 0x7FFFFF) + 2 * ((code & 0xFF) - 1))[0]
    def body(self, code):
        """his body as Revenge keeps it: {word, frame, frame's attire count, scale class, scale}"""
        w = self.word(code)
        frame, sc = w & 0x7F, (w & 0x7000) >> 12
        return dict(word="0x%04X" % w, frame=frame, frame_attires=self.attires[frame] if frame < 5 else None,
                    scale_class=sc, scale=round(self.scales[sc], 4))

def nm_body_type(e, b):
    """roster entry e (game.roster) and his Revenge body b -> (No Mercy body type, why)"""
    lbs = e["weight"] + 100 if e.get("weight") is not None else None
    if e["code"] >> 8 == WOMEN_GROUP:
        return NM_WOMAN, "Revenge's valets (group 9): No Mercy's women's type"
    f = b["frame"]
    if f == 0:
        if lbs is not None and lbs >= 180: return 1, "Revenge's small frame (0), %d lbs: Skinny 2" % lbs
        return 0, "Revenge's small frame (0)%s: Skinny 1" % ("" if lbs is None else ", %d lbs" % lbs)
    if f == 3:
        if lbs is not None and lbs >= 330: return 7, "Revenge's heavy frame (3), %d lbs: Fat 2" % lbs
        return 6, "Revenge's heavy frame (3)%s: Fat 1" % ("" if lbs is None else ", %d lbs" % lbs)
    # the standard frames (1, 2) and the people who don't wrestle (4): by weight, as No Mercy's builds step
    if lbs is None: return 3, "Revenge's frame %d, no weight on his record: Medium 2 (the standard build)" % f
    for top, t in ((230, 2), (265, 3), (275, 4)):
        if lbs < top: return t, "Revenge's frame %d, %d lbs: %s" % (f, lbs, NM_NAMES[t])
    return 5, "Revenge's frame %d, %d lbs: Thick 2" % (f, lbs)

def nm_scale(height):
    """No Mercy's whole-model scale for a height (inches over 5'0"): size_code_scale, then slot_model_scale (float maths
    as the game's: 0.97 + size x 0.00047)"""
    h = max(0, min(height, 35)) if height is not None else None
    if h is None: return None
    size = h * 255 // 36
    return round(0.97 + size * 0.00047, 4)

def nm_size_for_scale(scale):
    """(2026-11-13, revbody: the user's decision -- every Revenge man at Revenge's own scale) No Mercy's size code (0-255,
    a folder's "size") whose scale is nearest Revenge's: 0.97 + 0.00047 x size. Revenge's 0.96 is below No Mercy's least
    (0.97 at 0): it gets 0."""
    return max(0, min(255, int(round((scale - 0.97) / 0.00047))))

def scale_of_size(size):
    return round(0.97 + size * 0.00047, 4)

def height_for_scale(scale):
    """the height (inches over 5'0") whose No Mercy scale is nearest Revenge's scale -- for the men Revenge gives no height
    (its managers and valets: 0'0" on their records), so their size is Revenge's and not their base's"""
    return min(range(36), key=lambda h: (abs(nm_scale(h) - scale), h))

# ---- (2026-11-15, womenfix) No Mercy's own height and weight CODES for the women and the people who don't wrestle ----------
# No Mercy gives its women and its non-wrestlers no height or weight: their records hold codes the game reads specially.
#   height 36-39: the select screen writes one of four special texts (format_height_text, ovl 0x8011B38C: under 36 it is
#     feet and inches) and size_code_scale (0x8000A9A8) gives 36 the size 0 (37: 0x6A); a folder's "size" wins anyway.
#   weight 500-503: the special texts again (format_weight_text 0x8011B46C), and at a match's start the weight class takes
#     a fixed value for them (set_weight_class: 500 -> 120, 501 -> 140, 502 -> 300, 503 -> 120, i.e. 220 / 240 / 400 / 220
#     lbs): a woman a light weight, a non-wrestler a heavy one. Any other weight from 500 up is cut to 499 -- 599 lbs, a
#     SUPER HEAVYWEIGHT whom every slam is a struggle against (mod/NOTES.md 0.38.7, ovl_e 0x80128E78).
#   No Mercy's own: its women 36 / 500 (Stephanie, Lita, Trish, Terri, Ivory, The Kat, Linda, Mae Young, Ho; Chyna and Tori
#   37 / 501), its non-wrestlers 37 / 501 (Mr. McMahon, Shane McMahon; Jim Ross and Earl Hebner 36 / 501).
# Revenge has none for its valets and managers (0'0", 0 lbs on their records): the valets showed 5'0" (the height made for
# their scale) and their base's weight, The Rock's 275 lbs (the user, 2026-11-15). WrestleMania 2000's women and managers
# have 36 / 37 and its own placeholder weight 600 -- in No Mercy 599 lbs. So an import's woman gets No Mercy's women's
# codes, a non-wrestler with no weight No Mercy's non-wrestlers'.
NM_WOMAN_HEIGHT, NM_WOMAN_WEIGHT = 36, 500
NM_STAFF_HEIGHT, NM_STAFF_WEIGHT = 37, 501
NM_CODE_LBS = {500: 220, 501: 240, 502: 400, 503: 220}   # (what the engine weighs a code as: set_weight_class's value + 100)

def engine_lbs(weight):
    """a folder's "weight" (lbs - 100, or one of No Mercy's codes) -> the pounds No Mercy's engine weighs him at (None stays None)"""
    if weight is None: return None
    if weight in NM_CODE_LBS: return NM_CODE_LBS[weight]
    return min(weight, 499) + 100
