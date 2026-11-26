"""The parts probe (2026-11-13, modcheck): what each part number of each looks field BUILDS, in the stock game and in a mod.

A No Mercy mod can make its part tables longer and put other items at numbers the stock game already uses (No Mercy Plus V3:
top 125 is "none" in the game and Mideon's T-shirt in No Mercy Plus). Where a mod keeps those tables is its own business
(No Mercy Plus moved them to 0x8040xxxx), so nothing here reads them: each game's own model builder (No Mercy's func_80018A3C,
found in the mod's RAM by its code -- No Mercy Plus has it 0x90 on, at 0x80018ACC) is run on its RAM image (the project's
mipsemu) with one looks field changed at a time, and what that value puts into the model -- the files at each place of the
descriptor, BY CONTENT, and the other bytes it sets -- is that value's meaning. Two numbers mean the same when they build the
same. Run for every field and every value 0-255 in both games (about 20,000 builds, a couple of minutes).
"""
import hashlib, os, pickle, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
from .. import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
STOCK_BUILDER = 0x80018A3C
# the appearance entry (0x26 bytes: file 1's format, and the CAW lists' numbers) -> the looks struct the builder reads
# (mod gen/mkparts.py SRC2UN), and the fields' names (imports2 handlers\customs\nmplus.py APP_FIELDS)
SRC2UN = {0: 0, 1: 1, 2: 3, 3: 4, 4: 5, 5: 6, 6: 7, 7: 8, 8: 9, 9: 0xA, 0xA: 0xB, 0x1C: 0xC, 0x1D: 0xF, 0x1E: 0x12,
          0x1F: 0x13, 0x20: 0x15, 0x21: 0x18, 0x22: 0x1B, 0xC: 0x1E, 0xD: 0x1F, 0x11: 0x21, 0x12: 0x22, 0x13: 0x24,
          0x14: 0x25, 0xF: 0x27, 0x10: 0x28, 0x15: 0x2A, 0x16: 0x2B, 0x17: 0x2D, 0x18: 0x2E, 0x19: 0x30, 0x1A: 0x31,
          0x1B: 0x32, 0xE: 0x33, 0x23: 0x36, 0x24: 0x37, 0xB: 0x39, 0x25: 0x3C}
APP_FIELDS = ["body", "skin", "ring", "ring_c1", "ring_c2", "upper", "upper_c1", "upper_c2", "entr", "entr_c1",
              "entr_c2", "weapon", "gloves", "gloves_c", "tattoo", "wrist", "wrist_c", "lelbow", "lelbow_c",
              "relbow", "relbow_c", "lknee", "lknee_c", "rknee", "rknee_c", "boots", "boots_c1", "boots_c2",
              "head", "face", "hair", "hair_c", "front_hair", "facial_hair", "mask", "hat", "hat_c", "photo"]
# a field's Create-a-Wrestler list (the stock game's menu files, so the same for every No Mercy mod -- imports2's
# scripts\mk_caw_tables.py worked them out, nmplus_parts.json "field_list"): the names, for the report and for the check of
# names against builds below
FIELD_LIST = {"body": 0x4A56, "ring": 0x4A63, "upper": 0x4A65, "entr": 0x4A58, "gloves": 0x4A5C, "tattoo": 0x4A64,
              "wrist": 0x4A66, "lelbow": 0x4A57, "relbow": 0x4A57, "lknee": 0x4A61, "rknee": 0x4A61, "boots": 0x4A5A,
              "head": 0x4A60, "face": 0x4A59, "hair": 0x4A5D, "front_hair": 0x4A5E, "facial_hair": 0x4A5B, "mask": 0x4A62,
              "hat": 0x4A5F}
# a plain costume (mod gen/mkparts.py JJ_BASE, as imports2.py uses it): the probe's starting looks
JJ_BASE = bytes.fromhex("02 03 0e 00 00 00 00 00 00 00 00 00 00 00 00 01 02 00 00 00 00 07 00 08 00 1c 00 00 01 09 09 02 09 00 00 00 00 09")
NOT_PROBED = {"photo"}            # (the select screen's photo: not the model's)

def un_of(app):
    un = bytearray(0x3F)
    for s, d in SRC2UN.items(): un[d] = app[s]
    return bytes(un)

def file_fields():
    return set(pickle.load(open(os.path.join(DEV, "data", "gen_data", "model_file_fields.pkl"), "rb")))

def find_builder(ram, stock_ram, n=64):
    """the mod's model builder: the stock one's first 64 instructions found in the mod's code by their shape -- opcodes and
    registers; the immediates and jump targets are left out, since a mod that moved its code and tables changes those
    (No Mercy Plus has it 0x90 on, its part tables elsewhere). The best place matching 90% or more of them, else None."""
    import numpy as np
    def masks(words):
        op = words >> 26
        return np.where(op == 0, 0xFFFFFFFF, np.where((op == 2) | (op == 3), 0xFC000000, 0xFFFF0000)).astype(np.uint32)
    a = STOCK_BUILDER & 0x7FFFFF
    want = np.frombuffer(stock_ram[a:a + 4 * n], dtype=">u4").astype(np.uint32)
    mk = masks(want); want = want & mk
    code = np.frombuffer(ram[:0x200000], dtype=">u4").astype(np.uint32)
    best, at = 0, None
    cand = np.nonzero((code & mk[0]) == want[0])[0]
    for i in cand:
        if i + n > len(code): continue
        sc = int(np.sum((code[i:i + n] & mk) == want))
        if sc > best: best, at = sc, i
    return (0x80000000 | (4 * int(at))) if at is not None and best >= 0.9 * n else None

class Builder:
    def __init__(self, ram, addr, files):
        sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
        from mipsemu import Emu
        self.E = Emu(ram); self.base = bytes(self.E.m); self.addr = addr; self.files = files
    def build(self, app):
        E = self.E
        E.m[:] = self.base                       # (each build from the same RAM: nothing left from the one before)
        E.wbytes(0x80781000, un_of(app) + b"\0"); E.wbytes(0x80782000, bytes(0x270))
        try: E.call(self.addr, [0x80781000, 0x80782000], max_steps=3_000_000)
        except Exception as e: return None
        return E.rbytes(0x80782000, 0x270)

def meaning(desc, offs, files, FF, shas):
    """the meaning of a build at the places `offs`: a file place -> its file's content (sha), or 0 / out of range; else the word"""
    if desc is None: return ("ERR",)
    out = []
    for k in offs:
        v = struct.unpack_from(">H", desc, k)[0]
        if k in FF:
            if v == 0: out.append((k, 0))
            elif v > files.count(): out.append((k, "beyond"))
            else:
                if v not in shas: shas[v] = files.sha(v)
                out.append((k, shas[v]))
        else: out.append((k, v))
    return tuple(out)

def probe(stock_files, mod_files, stock_ram, mod_ram, log=print, base_app=JJ_BASE):
    """-> {field: (stock meanings [256], mod meanings [256], files named by each value in each game)}"""
    FF = file_fields()
    mb = find_builder(mod_ram, stock_ram)
    if mb is None: raise SystemExit("the parts probe: the mod's model builder wasn't found (its code isn't the stock one moved)")
    log("parts probe: stock builder 0x%08X, the mod's 0x%08X" % (STOCK_BUILDER, mb))
    S, M = Builder(stock_ram, STOCK_BUILDER, stock_files), Builder(mod_ram, mb, mod_files)
    ss, ms = {}, {}
    out = {}
    for fi, name in enumerate(APP_FIELDS):
        if name in NOT_PROBED: continue
        builds = []
        for B in (S, M):
            row = []
            for v in range(256):
                a = bytearray(base_app); a[fi] = v; row.append(B.build(bytes(a)))
            builds.append(row)
        # (the places this field touches, in either game: where some value's build differs from value 0's)
        offs = set()
        for row in builds:
            d0 = row[0] or bytes(0x270)
            for d in row:
                if d is None: continue
                for k in range(0, 0x270, 2):
                    if d[k:k + 2] != d0[k:k + 2]: offs.add(k)
        offs = sorted(offs - {0x254, 0x256})
        sm = [meaning(d, offs, stock_files, FF, ss) for d in builds[0]]
        mm = [meaning(d, offs, mod_files, FF, ms) for d in builds[1]]
        named = [[sorted({struct.unpack_from(">H", d, k)[0] for k in offs if k in FF} - {0}) if d else [] for d in row]
                 for row in builds]
        out[name] = (sm, mm, named, offs)
        log("   %-12s %3d places; %3d values differ" % (name, len(offs), sum(1 for v in range(256) if sm[v] != mm[v])))
    return out

def stock_valid(sig):
    return sig != ("ERR",) and not any(x[1] == "beyond" for x in sig)

def diff_parts(stock_files, mod_files, stock_ram, mod_ram, log=print):
    """the "parts.<field>" tables of the remap: per value, the same build or not, and each side's equivalent"""
    from .modtables import caw_items
    P = probe(stock_files, mod_files, stock_ram, mod_ram, log=log)
    T = {}
    for name, (sm, mm, named, offs) in P.items():
        changed = [v for v in range(256) if sm[v] != mm[v]]
        first_s, first_m = {}, {}
        for v in range(256):
            if stock_valid(sm[v]): first_s.setdefault(sm[v], v)
            if stock_valid(mm[v]): first_m.setdefault(mm[v], v)
        lst = FIELD_LIST.get(name)
        ls = caw_items(stock_files(lst)) if lst else {}; lm = caw_items(mod_files(lst)) if lst else {}
        def label(v, side):
            sig = (sm if side == 0 else mm)[v]
            if sig == ("ERR",): return "the builder failed"
            if not stock_valid(sig): return "past the table (a file number beyond the game's)"
            nm = (ls if side == 0 else lm).get(v)
            files = named[side][v]
            same0 = sig == (sm if side == 0 else mm)[0]
            return "%s%s" % (("%r " % nm) if nm else "", "files " + " ".join("%04X" % f for f in files) if files else
                             ("builds as value 0" if same0 else "no file"))
        T["parts." + name] = {
            "status": "same" if not changed else "changed",
            "about": "what looks field %r builds, per value: the files it puts in the model by content (aki/nmprobe.py)" % name,
            "places": offs, "changed": changed, "added": [], "removed": [],
            "stock": {str(v): label(v, 0) for v in changed}, "mod": {str(v): label(v, 1) for v in changed},
            "map_mod_to_stock": {str(v): first_s[mm[v]] for v in changed if mm[v] in first_s},
            "map_stock_to_mod": {str(v): first_m[sm[v]] for v in changed if sm[v] in first_m},
            "mod_only": [v for v in changed if stock_valid(mm[v]) and mm[v] not in first_s],
            # (names against builds: a part the mod drew again under its old name -- a texture mod -- changes what the number
            #  builds with no name changed; only the builds can tell. "renamed_same_build": the name changed, the build not)
            "redrawn_same_name": [v for v in changed if ls.get(v) and ls.get(v) == lm.get(v) and stock_valid(sm[v])],
            "renamed_same_build": [v for v in range(256) if v not in changed and ls.get(v) and lm.get(v) and ls[v] != lm[v]]}
    T["parts"] = {"status": "same" if all(T[k]["status"] == "same" for k in T) else "changed",
                  "about": "the parts probe: %d looks fields x 256 values in each game's own builder" % len(P),
                  "fields_changed": sorted(k[6:] for k in T if T[k]["status"] != "same")}
    return T
