"""The WWF No Mercy handler (2026-11-13, nmhandler): No Mercy is one more AKI source game. This reads the user's own No Mercy
ROM and says, for every stock Superstar, what the game builds for him -- so each can be written as a standard wrestler folder
(aki/folder.py, rosterids' format) and the game can load him from it, the built-in tables only the fallback.

Everything here comes from the ROM alone (no RAM image, no capture): the game's per-ordinal roster table in its main segment,
the appearance file (1), the names file (2), each man's moves and abilities files, the select photos' files. Where the game
turns those into a costume record (func_8000B834 -> func_8000B60C, func_8000B780) and a moveset (func_8000B914), this
follows the same steps, so the folder holds the same values the game would put in its records:

  record (0x4E bytes a costume)   name 0x00 (0x15), short name 0x15 (0x0B), nickname 0x20 (0x11) from the names file;
                                  the packed looks 0x31 (0x17) from the costume's appearance entry; weight 0x48 (u16),
                                  height 0x4A, music slot 0x4B, video 0x4C from his costume struct
  moveset (0xD8 bytes)            his moves file (0xB8 bytes, packed) then his abilities file (0x20)
  photo                           the photo number is in the looks (field 0x3C); its palette file 0x4AAD + 2p and picture
                                  file 0x4AAE + 2p (the TEX file 0x45B3 + p holds the same two)

His model is not a file of its own: the game builds it from the looks (func_80018A3C) out of its built-in parts. So a stock
folder's model reference IS his looks, and No Mercy's parts are the built-in library they name. The same goes for music
(a slot of the game's entrance table: song and entrance lights) and moves (No Mercy's own move numbers: "nomercy:<n>").
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
from .. import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
sys.path.insert(0, DEV)
from nmpaths import nmp
from ..aki import romtable as RT

GAME = "WWF No Mercy"

def rom_path(given=None):
    """the user's own No Mercy ROM, found the way the project's scripts find it (nmpaths: Development\\roms); never copied"""
    p = given or nmp("/home/devbox/work/nomercy.z64")
    if not os.path.exists(p): sys.exit("No Mercy ROM not found: %s (give --rom)" % p)
    return p

# ---- where things are (USA Rev 1) --------------------------------------------------------------------------------------
MAIN_VRAM, MAIN_ROM = 0x80000400, 0x1000      # the main segment: RAM 0x80000400.. is ROM 0x1000.. (checked: ROSTER_TABLE)
ROSTER_TABLE = 0x80047124                      # [ordinal] -> 4 costume structs of 0x14 bytes (mod src/roster.c)
OVL_A_VRAM, OVL_A_ROM = 0x800D9960, 0x00057310 # ovl_a (syms/dump.toml): its entrance table follows its code
ENTRANCE = 0x800F50D0                          # 12 bytes a music slot: s16 song, s16, u32 lights/effects, u32
FILE_APPEARANCE, FILE_NAMES = 1, 2
PHOTO_TEX, PHOTO_PAL, PHOTO_PIC = 0x45B3, 0x4AAD, 0x4AAE   # + p, + 2p, + 2p
STOCK_ORDINALS = range(1, 65)                  # 1-64: the Superstars, managers and officials (65-82 Create-a-Wrestler, 83 a spare)
REC_SIZE, MOVES_PACKED, ABILITIES = 0x4E, 0xB8, 0x20

# The appearance entry (0x26 bytes) -> the looks struct (0x3F bytes): which entry byte goes to which looks byte, as
# func_8000B60C copies them before it packs (exe src/game/lifted.cpp pack_looks_into_record). In the order of the looks
# struct's offsets, this is the folder's "looks" order (handlers1: LOOK_OFFSETS, mod parts.c fields[]).
LOOK_OFFSETS = [0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x12, 0x13, 0x15, 0x18, 0x1B, 0x1E,
                0x1F, 0x21, 0x22, 0x24, 0x25, 0x27, 0x28, 0x2A, 0x2B, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33, 0x36, 0x37, 0x39, 0x3C]
APP_TO_LOOKS = {0x00: 0x00, 0x01: 0x01, 0x02: 0x03, 0x03: 0x04, 0x04: 0x05, 0x05: 0x06, 0x06: 0x07, 0x07: 0x08, 0x08: 0x09,
                0x09: 0x0A, 0x0A: 0x0B, 0x1C: 0x0C, 0x1D: 0x0F, 0x1E: 0x12, 0x1F: 0x13, 0x20: 0x15, 0x21: 0x18, 0x22: 0x1B,
                0x0C: 0x1E, 0x0D: 0x1F, 0x11: 0x21, 0x12: 0x22, 0x13: 0x24, 0x14: 0x25, 0x0F: 0x27, 0x10: 0x28, 0x15: 0x2A,
                0x16: 0x2B, 0x17: 0x2D, 0x18: 0x2E, 0x19: 0x30, 0x1A: 0x31, 0x1B: 0x32, 0x0E: 0x33, 0x23: 0x36, 0x24: 0x37,
                0x0B: 0x39, 0x25: 0x3C}
LOOKS_TO_APP = {v: k for k, v in APP_TO_LOOKS.items()}
assert sorted(APP_TO_LOOKS) == list(range(0x26)) and sorted(APP_TO_LOOKS.values()) == LOOK_OFFSETS
PHOTO_FIELD = LOOK_OFFSETS.index(0x3C)         # (the last of the 38: the photo number)

class NoMercy:
    """The user's No Mercy ROM: its files (aki romtable) and the roster tables of its main segment."""
    def __init__(self, path=None):
        self.path = rom_path(path)
        self.files = RT.RomFiles(self.path, "nomercy")
        self.rom = self.files.rom
    def main(self, vaddr, n):
        o = vaddr - MAIN_VRAM + MAIN_ROM
        return self.rom[o:o + n]
    def ovl_a(self, vaddr, n):
        o = vaddr - OVL_A_VRAM + OVL_A_ROM
        return self.rom[o:o + n]
    def costume_struct(self, ordinal, c):
        """the 0x14-byte costume struct: id, ordinal, music slot, video, height, (spare), weight, moves file, abilities
        file, appearance entry, names entry, (spare)"""
        ptr = struct.unpack(">I", self.main(ROSTER_TABLE + 16 * ordinal + 4 * c, 4))[0]
        raw = self.main(ptr, 0x14)
        k = ("id", "ordinal", "music", "video", "height", "spare6", "weight", "moves_file", "abilities_file", "appearance", "names", "spare12")
        return dict(zip(k, struct.unpack(">HHBBBBHHHHHH", raw)), raw=raw)
    def names(self, entry):
        """names file entry -> (name, short name, nickname): three strings, offsets in a u16 list (func_8000B780)"""
        f = self.files(FILE_NAMES)
        out = []
        for k in range(3):
            o = struct.unpack_from(">H", f, 2 * (entry + k))[0]
            out.append(f[o:f.index(b"\0", o)])
        return out
    def appearance(self, entry):
        return self.files(FILE_APPEARANCE)[0x26 * entry:0x26 * entry + 0x26]
    def entrance(self, slot):
        song, x, fx, y = struct.unpack(">hhII", self.ovl_a(ENTRANCE + 12 * slot, 12))
        return dict(song=song, effects=fx)
    def photo(self, p):
        """photo p as a folder's photo.bin: its 16-colour palette (32 bytes), then 64 x 64 at 4 bits (2048), from the palette
        and picture files (the picture file has an 8-byte header first). Also the TEX file, for the check."""
        pal, pic, tex = self.files(PHOTO_PAL + 2 * p), self.files(PHOTO_PIC + 2 * p), self.files(PHOTO_TEX + p)
        return pal + pic[8:], dict(pal=pal, pic=pic, tex=tex)

def looks_of(app):
    """an appearance entry -> his 38 looks fields in the folder's order (the values func_8000B60C hands the packer)"""
    return [app[LOOKS_TO_APP[o]] for o in LOOK_OFFSETS]

def app_of(looks):
    """the 38 looks fields -> an appearance entry (the inverse: what the mod hands func_8000B60C)"""
    a = bytearray(0x26)
    for v, o in zip(looks, LOOK_OFFSETS): a[LOOKS_TO_APP[o]] = v
    return bytes(a)

def text(b):
    """a names-file string as the folder writes it (the game's own text is ASCII; latin-1 keeps any byte as it is)"""
    return b.decode("latin1")

def stock(nm):
    """every stock Superstar: per ordinal 1-64, his four costumes' structs, names, looks, the moveset, the photo"""
    out = []
    for o in STOCK_ORDINALS:
        cs = [nm.costume_struct(o, c) for c in range(4)]
        c0 = cs[0]
        moves = nm.files(c0["moves_file"])[:MOVES_PACKED]
        abil = nm.files(c0["abilities_file"])[:ABILITIES]
        cos = []
        for c, s in enumerate(cs):
            n, sh, ni = nm.names(s["names"])
            app = nm.appearance(s["appearance"])
            cos.append(dict(struct=s, name=n, short=sh, nick=ni, app=app, looks=looks_of(app)))
        out.append(dict(ordinal=o, id=c0["id"], costumes=cos, moves=moves, abilities=abil,
                        sizes=(len(nm.files(c0["moves_file"])), len(nm.files(c0["abilities_file"])))))
    return out

# ---- the game's own code, run from the ROM (rig\harness\mipsemu.py) ----------------------------------------------------
# Where the folder holds a value the game derives with code (a moveset's 292 slots; a costume's packed looks; his model),
# the handler and its proof run the game's own function on a RAM image made from the ROM alone: the main segment's code and
# data where the game loads them (RAM 0x80000400 = ROM 0x1000, up to ovl_a's place). Nothing captured from a running game.
# (Checked 2026-11-13: the moveset unpack gives the same 292 slots and abilities for all 64 as on gen_data\van_sel_ram.bin,
#  the Superstar Select's RAM that gen/mkroster.py runs it on.)
BUF_M, BUF_P, BUF_S, BUF_A = 0x80780000, 0x80780100, 0x80781000, 0x80782000
BUF_REC, BUF_APP, BUF_UN, BUF_MODEL = 0x80784000, 0x80785000, 0x80786000, 0x80787000
UNPACK_MOVES = 0x80005EFC        # func_80005EFC(packed moves, slots out, packed abilities, abilities out): the menus' unpack
PACK_LOOKS = 0x8000B60C          # func_8000B60C(record, appearance entry): the looks packed into record + 0x31
UNPACK_LOOKS = 0x8001AC44        # func_8001AC44(packed looks, looks struct out)
BUILD_MODEL = 0x80018A3C         # func_80018A3C(looks struct, model descriptor out, 0x270 bytes)

class Code:
    def __init__(self, nm):
        sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
        from mipsemu import Emu
        img = bytearray(0x800000)
        n = OVL_A_VRAM - MAIN_VRAM
        img[MAIN_VRAM & 0x7FFFFF:(MAIN_VRAM & 0x7FFFFF) + n] = nm.rom[MAIN_ROM:MAIN_ROM + n]
        self.E = Emu(bytes(img))
    def unpack_moves(self, moves, abilities):
        """-> (292 slots as [move, flag, flag, flag], 55 abilities): the game's unpack of a packed moveset"""
        E = self.E
        E.wbytes(BUF_M, moves[:MOVES_PACKED]); E.wbytes(BUF_P, abilities[:ABILITIES])
        E.wbytes(BUF_S, bytes(0x800)); E.wbytes(BUF_A, bytes(0x80))
        E.call(UNPACK_MOVES, [BUF_M, BUF_S, BUF_P, BUF_A])
        slots = [[E.r16(BUF_S + 6 * i), E.r8(BUF_S + 6 * i + 2), E.r8(BUF_S + 6 * i + 3), E.r8(BUF_S + 6 * i + 4)] for i in range(292)]
        abil = [struct.unpack(">h", E.rbytes(BUF_A + 2 * i, 2))[0] for i in range(55)]
        return slots, abil
    def pack_looks(self, app):
        """an appearance entry -> the 0x17 packed bytes func_8000B60C puts at record + 0x31"""
        E = self.E
        E.wbytes(BUF_REC, bytes(0x80)); E.wbytes(BUF_APP, bytes(app) + bytes(0x40 - len(app)))
        E.call(PACK_LOOKS, [BUF_REC, BUF_APP])
        return E.rbytes(BUF_REC + 0x31, 0x17)
    def unpack_looks(self, packed):
        """0x17 packed bytes -> the 0x3F-byte looks struct (func_8001AC44)"""
        E = self.E
        E.wbytes(BUF_APP, bytes(packed) + b"\0"); E.wbytes(BUF_UN, bytes(0x40))
        E.call(UNPACK_LOOKS, [BUF_APP, BUF_UN])
        return E.rbytes(BUF_UN, 0x3F)
    def model(self, un):
        """a looks struct -> the 0x270-byte model descriptor func_80018A3C builds (his parts named by file number)"""
        E = self.E
        E.wbytes(BUF_UN, bytes(un) + b"\0"); E.wbytes(BUF_MODEL, bytes(0x270))
        E.call(BUILD_MODEL, [BUF_UN, BUF_MODEL], max_steps=5_000_000)
        return E.rbytes(BUF_MODEL, 0x270)
