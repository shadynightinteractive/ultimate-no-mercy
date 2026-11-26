"""The WWF WarZone - Badd Blood handler (2026-11-13, imports2): a WrestleMania 2000 mod, read as one more AKI game.

Badd Blood is WrestleMania 2000 with its data changed: the same game code NWXE, the same boot code, the same main-segment
code (checked: the roster records, the moveset unpack 0x80004D0C and the model loader's costume site 0x800146CC are where
WrestleMania 2000 has them), its AKI file table moved -- found at ROM 0x1137FAA (WrestleMania 2000's is at 0x11778BE), the
same 10,312 entries and data base 0x144AA0 (aki/romtable.py GAMES["baddblood"]) -- and 1,772 of its files changed
(scratch\\badd_blood\\diff_files.py). So everything the WM2000 handler knows holds, read from THIS ROM instead:

  roster    its record table (RAM 0x800411B0, 0x4C records) is static data of the main segment: read straight from the ROM
            (RAM 0x80000400 = ROM 0x1000, as No Mercy). Checked on WrestleMania 2000 itself: the same reading of its ROM
            gives gen/wm2k.py roster() of its RAM image, record for record (scripts\\bb_roster.py).
  model     his costumes as Badd Blood builds them in a match: a capture in the project's libretro rig (rig\\bb.py,
            rig\\capture.py: data/wmport/wmcap.py's way -- the Superstar Select, the loader's costume forced, into a match),
            the descriptor and each part's picture as the match drew it, meshes from this ROM's files; then the WM2000
            handler's steps (gen/wmport.py nm_descriptor; aki/skintone.py nm_hands: No Mercy's hands in his own skin).
  moveset   gen/mkroster.py port_wm_moveset (WM2000's moves -> No Mercy's by animation), given THIS ROM's moves,
            abilities, move names and animation files (53 of its animations differ from WrestleMania 2000's).
  theme     its music bank (the select screen's RAM: the bank's records) and the samples in this ROM, as gen/mkcontent.py
            wm_theme does for WrestleMania 2000 -- written as the folder's theme.bin / theme.meta. (2026-11-13: the sound its
            entrance table names for his music slot, at the rate the game plays it -- theme_folder's notes.)
  video     its entrance table's frame list (the select screen's RAM, 0x80102070 + 12 v) and its frame files (64 x 64
            CI8 TEX: No Mercy's frame format) -- the folder's video.bin, a frame per 1/7.5 s as the list's times say.
  photo     a crop of the rig's screenshot of its own Superstar Select with him on it (aki/photo.py).
"""
import hashlib, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
from .. import DEV, RIG     # (the Development folder, the rig's captures: handlers/__init__.py, 2026-11-13)
sys.path.insert(0, DEV)
from nmpaths import nmp
from ..aki import romtable as RT
from ..aki import model as M
from ..aki import skintone as ST      # (2026-11-15, skinqa)
from ..aki import handfit as HF      # (2026-11-16, womenfix2 round 2)

GAME = "WWF WarZone - Badd Blood (a WrestleMania 2000 mod)"
ROM = os.path.join(DEV, "roms", "WWF WarZone - Badd Blood (USA) (v1.0).z64")
MAIN_VRAM, MAIN_ROM, MAIN_END =0x80000400, 0x1000, 0x144AA0
ROSTER, ROSTER_N = 0x800411B0, 0x4C

class BaddBlood:
    def __init__(self, path=None):
        self.path = path or ROM
        self.files = RT.RomFiles(self.path, "baddblood")
        rom = self.files.rom
        img = bytearray(0x800000)
        img[MAIN_VRAM & 0x7FFFFF:(MAIN_VRAM & 0x7FFFFF) + MAIN_END - MAIN_ROM] = rom[MAIN_ROM:MAIN_END]
        self.ram = bytes(img)          # the main segment where the game loads it (no capture: the ROM alone)
    def r32(self, a): return struct.unpack_from(">I", self.ram, a & 0x7FFFFF)[0]
    def rb(self, a, n): a &= 0x7FFFFF; return self.ram[a:a + n]
    def cstr(self, a): a &= 0x7FFFFF; return self.ram[a:self.ram.index(b"\0", a)].decode("latin1")
    def roster(self):
        """its records: name (the select screen's markup: <short> part in angle brackets, {nick}), ID, ordinal, height
        (inches over 5'0"), weight (less 100), moves file, abilities file, music (a sound of its bank), video, four
        costumes' looks (0x24 bytes each)"""
        out = []
        for k in range(ROSTER_N):
            rec = self.r32(ROSTER + 4 * k)
            f = struct.unpack(">HHHHHHBB", self.rb(rec + 4, 14))
            out.append(dict(k=k, raw_name=self.cstr(self.r32(rec)), id=f[0], ord=f[1], height=f[2], weight=f[3],
                            moves=f[4], abilities=f[5], music=f[6], video=f[7],
                            looks=[self.rb(self.r32(rec + 0x14 + 4 * i), 0x24) for i in range(4)]))
        return out

def plain_name(raw):
    """the select screen's markup -> (full name, short name, nickname): "<VADER>" -> VADER / VADER; "{THE HITMAN}BRET HART"
    -> BRET HART, nick THE HITMAN; "BLACKJACK <WINDHAM>" -> short WINDHAM"""
    import re
    nick = re.findall(r"\{([^}]*)\}", raw)
    body = re.sub(r"\{[^}]*\}", "", raw)
    short = re.findall(r"<([^>]*)>", body)
    full = body.replace("<", "").replace(">", "").strip()
    return full, (short[0].strip() if short else full), (nick[0] if nick else None)

# ---- the model: a capture's descriptor and pictures, his meshes from this ROM ----------------------------------------
def build_model(bb, k, caps, report, key):
    """his four costumes (caps: each costume's match RAM, slot 0 him) -> (descriptors, pool), the WM2000 handler's steps
    (handlers1 wm2000/game.py build_model) with the meshes read from Badd Blood's files"""
    sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
    import wmport as W
    from ..revenge.game import _PoolAsFiles
    pool = M.Pool()
    descs = []
    for c, ram in enumerate(caps):
        wd, parts = W.capture(ram, 0)
        kinds = ram[0x41CC4:0x41CC4 + 0x40]              # (its hands' kind, by the looks' first byte: this match's table)
        nm = bytearray(W.nm_descriptor(wd))
        for o in list(range(8, 0xB0, 8)) + list(range(0xC8, 0xE0, 8)): nm[o:o + 6] = bytes(6)
        for p in parts:
            o = p["at"] if p["k"] <= 20 else 0xC8 + 8 * (p["k"] - 22)
            m = pool.one(bb.files(p["mesh"]))
            t, q = pool.pair(p["tex"], p["pal"]) if p["tex"] else (0, 0)
            struct.pack_into(">HHH", nm, o, m, t, q)
        nm[W.SKEL_WM_HANDS] = kinds[bb.roster()[k]["looks"][c][0]]
        descs.append(bytes(nm))
    F = _PoolAsFiles(pool)
    skins = {}
    # (2026-11-16, skintone2: aki/skintone.py nm_hands -- No Mercy's hands in his own skin; his skin, every part of it,
    #  as Badd Blood draws it: the user's rule, imports keep their source game's skin tone)
    descs = [ST.nm_hands(W, d, F, "%s %d" % (key, c), report, skins, key) for c, d in enumerate(descs)]
    descs = HF.fit_wrists(descs, pool, report, key, W.nmf)    # (2026-11-16, womenfix2 round 2: aki/handfit.py -- the wrists onto the hands)
    pals = set()
    for d in descs:                                       # (No Mercy's palettes are 0x200 bytes: gen/wmport.py NM_PAL_SIZE)
        for kk in range(1, 28):
            o = 8 * kk if kk <= 20 else (0xA8 + 8 * (kk - 21))
            q = struct.unpack_from(">H", d, o + 4)[0]
            if pool.ours(q): pals.add(q)
    for q in pals:
        b = pool.data[q - M.POOL0]
        if len(b) < W.NM_PAL_SIZE: pool.data[q - M.POOL0] = bytes(b) + bytes(W.NM_PAL_SIZE - len(b))
    return descs, pool

# ---- the moveset ------------------------------------------------------------------------------------------------------
def moveset(bb, e, ref_ord, report, key):
    """his moves and abilities files -> No Mercy's 292 slots and 55 abilities (gen/mkroster.py port_wm_moveset), with
    gen/wm2k.py's and gen/wmanim.py's WrestleMania 2000 files swapped for this ROM's: the moves, the move names (files
    0x95-0x97, two of which the mod changed) and the animations (wmanim's file hashes, md5, as gen made them)"""
    sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
    import mkroster as K, wm2k, wmanim
    wm2k.wfile = bb.files
    wm2k.WM_NAMES = wm2k._by_move(wm2k._names((0x95, 0x96, 0x97), bb.files), bb.files(0x94))
    for f in range(0x1609, 0x2848):
        if f in wmanim.wm_h:
            b = bb.files(f); h = wmanim.wm_h[f]
            wmanim.wm_h[f] = (hashlib.md5(b).hexdigest(), len(b), b[:4]) if len(h) == 3 else h
    # (2026-11-13, modcheck) the remap's word on it: every animation Badd Blood changed must now be read from Badd Blood
    # -- one left with WrestleMania 2000's hash (an entry of another shape, kept above) would map his moves by the stock file
    from ..aki import modtables as MTB
    rp = os.environ.get("BADDBLOOD_REMAP") or os.path.join(HERE, "..", "..", "work", "baddblood", "remap.json")   # (tools\handlers\work)
    if not os.path.exists(rp):
        raise MTB.ModTableChanged("Badd Blood: no remap at %s -- run tool\\modcheck.py tables for it first" % rp)
    R = MTB.Remap.load(rp)
    if R.d["mod_sha1"] != bb.files.sha1: raise MTB.ModTableChanged("the remap %s is for another ROM" % rp)
    if os.environ.get("MODCHECK_NEGATIVE_ANIM") == "1":      # (modcheck's negative test: one changed animation left stock)
        f0 = next(f for f in R.T["anims"]["changed"] if f in wmanim.wm_h and len(wmanim.wm_h[f]) == 3)
        wmanim.wm_h[f0] = ("0" * 32, 0, b"")              # (a hash that isn't Badd Blood's file: as if left at the stock one)
    for f in R.T["anims"]["changed"]:
        if f in wmanim.wm_h and len(wmanim.wm_h[f]) == 3 and wmanim.wm_h[f][0] != hashlib.md5(bb.files(f)).hexdigest():
            raise MTB.ModTableChanged("animation file 0x%X: Badd Blood changed it, but its stock hash is still used" % f)
    for f in R.T["move_names"]["changed"]:
        if f not in (0x94, 0x95, 0x96, 0x97): raise MTB.ModTableChanged("move names file 0x%X changed and isn't read" % f)
    report.append("%s: remap checked -- %d changed animations and move-name files %s read from Badd Blood itself"
                  % (key, len(R.T["anims"]["changed"]), ", ".join("0x%X" % f for f in R.T["move_names"]["changed"])))
    wmanim.WM1 = [struct.unpack_from(">h", bb.files(0x3AE), o)[0] for o in range(0, len(bb.files(0x3AE)), 2)]
    wmanim.WM2 = [struct.unpack_from(">h", bb.files(0x3AF), o)[0] for o in range(0, len(bb.files(0x3AF)), 2)]
    return K.port_wm_moveset(key, e["moves"], e["abilities"], ref_ord, {}, report)

def pack_moveset(slots, ab, ref_ord):
    """-> the packed 216 bytes the game reads for his abilities (and the editor copies), slots it can't say the
    reference's (the Revenge handler's pack_plain); moves.json keeps every slot as mapped"""
    from ..revenge.game import pack_plain
    return pack_plain(slots, ab, ref_ord)

# ---- theme and video --------------------------------------------------------------------------------------------------
def _ram32(ram, a): return struct.unpack_from(">I", ram, a & 0x7FFFFF)[0]
def _ram16(ram, a): return struct.unpack_from(">H", ram, a & 0x7FFFFF)[0]

def theme(rom, ram, sound):
    """sound `sound` of its music bank (the bank's records in `ram`, the samples in `rom`) -> (theme.bin, theme.meta,
    about): as gen/mkcontent.py wm_theme takes WrestleMania 2000's, its instrument's VADPCM samples and wave record
    (0xD0: header, codebook at 0x18, loop at 0xA0), then the level, loop and tuning bytes tools/wrestler_theme.py writes"""
    WB = _ram32(ram, 0x800571EC); WC = _ram32(ram, WB + 0x10)
    wp = _ram32(ram, WB + 0x18 + 8 * sound); sc = ram[wp & 0x7FFFFF:(wp & 0x7FFFFF) + 24]
    ni = _ram16(ram, _ram32(ram, WB + 0x14) + 2 * sc[sc.index(0x81) + 1])
    e = _ram32(ram, _ram32(ram, WC + 0x2C) + 4 * ni); base, ln = _ram32(ram, e), _ram32(ram, e + 4)
    w = bytearray(0xD0); w[0:0x18] = ram[(e & 0x7FFFFF):(e & 0x7FFFFF) + 0x18]
    # (the record's ROM offset and its RAM pointers to the codebook and loop mean nothing in a folder: 0, as
    #  tools/wrestler_theme.py writes them -- the sound is theme.bin from its start, the codebook and loop are copied in)
    struct.pack_into(">I", w, 0, 0); w[0x0C:0x18] = bytes(0x0C)
    w[0x18:0x18 + 0x88] = ram[(_ram32(ram, e + 0x10) & 0x7FFFFF):(_ram32(ram, e + 0x10) & 0x7FFFFF) + 0x88]
    if _ram32(ram, e + 0x0C): w[0xA0:0xA0 + 0x2C] = ram[(_ram32(ram, e + 0x0C) & 0x7FFFFF):(_ram32(ram, e + 0x0C) & 0x7FFFFF) + 0x2C]
    level = ram[(_ram32(ram, WC + 0x24) & 0x7FFFFF) + ni]
    tune = _ram32(ram, _ram32(ram, WC + 0x28) + 4 * ni)
    loops = 1 if _ram32(ram, e + 0x0C) else 0
    data = rom[base:base + ln]
    meta = bytes(w) + struct.pack(">BBxxI", level, loops, tune)
    return data, meta, dict(sound=sound, instrument=ni, rom_offset="0x%X" % base, bytes=ln, loops=bool(loops))

# (2026-11-13, themefix2) Which sound, and at what rate -- both read from Badd Blood itself playing an entrance in the rig
# (scratch\agents\themefix2: rig_rec.py recordings, allvoices.py reading the sound voices out of RAM snapshots, snap_phat.py):
#  - the sound: a record's music byte is a SLOT of the entrance table (0x8010206C, 12 bytes a slot), and the slot's s16 at
#    +8 is the music bank's sound. The table shuffles them: Vader's slot 14 plays sound 25, Goldust's slot 38 sound 43 (in
#    the rig his entrance's two voices -- a pair, 138 samples apart, one left, one right -- read instrument 25's samples, and
#    loop at its loop points). The record's byte taken as the sound (as before) gave another man's theme.
#  - the rate: the voices' pitch (their resampler's float, 0.2498 for Vader's) times the sound's output rate (28,803 a
#    second, the rig's: audio frames over video frames) -- 7,196 a second for Vader's, 7,016 for Goldust's, the rig's sound
#    found again in both channels by GCC-PHAT at exactly those rates. That is 28819 x 2^((tuning + 48) / 12): the tuning
#    (-72.02, -72.46) in semitones from the output rate, with this game's theme script (note 0x30). The old reading,
#    10787 x 2^((tuning + 48) / 12) = 2,697, took No Mercy's 10787 as the base: 17 semitones (2.67 times) too slow, which
#    turned the music into a low roar. (The "2,700 a second" measurement it rested on was never found again: themefix.)
# The exe's player plays every theme.bin at 10787 (arcade_media_decode.cpp RATE) and doesn't read the tuning, so the
# folder's theme is the mod's sound made 10787 a second: decoded, resampled (polyphase, as mp3theme.py does), made as loud
# as the game's themes (mp3theme.py's level), encoded again with a codebook of its own -- the way tools/wrestler_theme.py
# makes every added theme, tuning -48 and all.
THEME_RATE, THEME_TUNE = 10787, 0xC2400000
# (the base: a tuning of -48 plays at this. The voices' own pitch, read from RAM, times the rig's sound out (28,803.1 a second:
#  480 audio frames a video frame) is 7,196.4 / 7,016.2 / 7,179.7 a second for tunings -72.02 / -72.46 / -72.06: each
#  28803.1 x 2^((tuning + 48) / 12) x 1.00055 -- the driver's pitch 0.055 % sharp of the plain formula, the same for all three)
OUT_RATE = 28818.9
ENTRANCES = 0x8010206C                 # the entrance table: slot k at + 12 k; its music bank sound at + 8 (s16)
def theme_sound(ram, slot):
    """the music bank sound the entrance of music slot `slot` plays (the entrance table, in the select screen's RAM)"""
    return struct.unpack_from(">h", ram, (ENTRANCES + 12 * slot + 8) & 0x7FFFFF)[0]
def theme_rate(tune):
    """the samples a second the game plays a theme of this tuning at (with its own theme script; measured, above)"""
    return OUT_RATE * 2 ** ((tune + 48.0) / 12.0)
def theme_folder(rom, ram, slot):
    """-> (theme.bin, theme.meta, about, (source pcm, its rate)): the theme of music slot `slot` (a record's music byte), as a
    folder theme the game plays at its own rate"""
    import numpy as np
    from fractions import Fraction
    from scipy import signal
    sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
    import vadpcm, mp3theme
    sound = theme_sound(ram, slot)
    data, meta, about = theme(rom, ram, sound)
    about = dict(about, slot=slot)
    tune = struct.unpack(">f", meta[0xD4:0xD8])[0]
    src_rate = theme_rate(tune)
    order, npred = struct.unpack_from(">ii", meta, 0x18)
    pcm = np.asarray(vadpcm.decode(data, meta[0x18:0x18 + 8 + order * npred * 16], len(data) * 16 // 9), dtype=np.float64)
    loop0, loop1 = struct.unpack_from(">II", meta, 0xA0)
    if about["loops"] and loop1: pcm = pcm[:loop1]
    fr = Fraction(THEME_RATE / src_rate).limit_denominator(4000)
    y = signal.resample_poly(pcm, fr.numerator, fr.denominator, window=("kaiser", 8.0))
    ls = int(round(loop0 * fr)) // 16 * 16 if about["loops"] else 0
    n = (len(y) - ls) // 16 * 16 + ls; y = y[:n]
    med, _ = mp3theme.loudness(y, THEME_RATE)
    y = mp3theme.limit(y * (mp3theme.TARGET_MED / med), R=THEME_RATE)
    out = np.clip(np.round(y), -32768, 32767).astype(np.int16)
    book, enc, samples = vadpcm.encode(out, THEME_RATE)
    dec = vadpcm.decode(enc, book, samples)
    state = dec[ls - 16:ls] if ls >= 16 else [0] * 16
    w = bytearray(0xD0)
    struct.pack_into(">IIBB", w, 0, 0, len(enc), 0, 1)
    w[0x18:0x18 + len(book)] = book
    struct.pack_into(">IIi16h", w, 0xA0, ls, samples, -1, *[int(v) for v in state])
    m = bytes(w) + struct.pack(">BBxxI", 0, 1, THEME_TUNE)
    about = dict(about, tuning=round(tune, 3), source_rate=round(src_rate, 1), rate=THEME_RATE, seconds=round(samples / THEME_RATE, 1),
                 loop_from_s=round(ls / THEME_RATE, 1), made="decoded, resampled %s, levelled, VADPCM again" % fr)
    return bytes(enc), m, about, (pcm, src_rate)

def video_list(ram, v):
    """its video v's frame list [(frame file, time)] (gen/wmport.py wm_video: the entrance table at 0x80102070)"""
    a = _ram32(ram, 0x80102070 + 12 * v); out = []
    while _ram16(ram, a) != 0xFFFF:
        out.append((_ram16(ram, a), _ram16(ram, a + 2))); a += 4
        assert len(out) < 512
    return out

FRAME_BYTES = 16 + 512 + 64 * 64
def video(files, ram, v):
    """-> (video.bin, about). Each list entry is a frame file and how long it stays up (its low 15 bits, in the game's
    frames, 1/30 s: most entries are 4, No Mercy's own Titantron rate of 7.5 a second; 0x8000 marks a picture frame, the
    short unmarked ones are the static flash between them, frame 0x284). The folder's video.bin is shown at 7.5 frames a
    second (tools/wrestler_video.py's rate), so the list is sampled every 4 ticks: the frame up at that moment. One pass
    of the list (the game plays it round)."""
    lst = video_list(ram, v)
    for f, _ in lst:
        b = files(f)
        assert len(b) == FRAME_BYTES and b[:3] == b"TEX", "frame file 0x%X isn't a 64 x 64 CI8 frame" % f
    ends, t = [], 0
    for f, d in lst: t += d & 0x7FFF; ends.append(t)
    out = bytearray(); shown = []
    for tick in range(0, t, 4):
        i = next(j for j, e in enumerate(ends) if tick < e)
        out += files(lst[i][0]); shown.append(lst[i][0])
    return bytes(out), dict(video=v, entries=len(lst), ticks=t, frame_files=["0x%X" % f for f in sorted({f for f, _ in lst})],
                            frames_written=len(shown))
