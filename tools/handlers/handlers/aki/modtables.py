"""The mod table diff and its remap (2026-11-13, modcheck): what a mod changed in every table an importer reads, and the guard
that makes an importer fail loudly when it reads a value through the STOCK table of one the mod changed.

Why (the user, 2026-11-13): Goldust, imported from No Mercy Plus V3, came out in Mideon's shirt. The handler set his top to
125, the stock game's "none" -- but No Mercy Plus had made the top list longer and put Mideon's T-shirt at 125. Nothing in
the transfer was wrong; a stock number meant another item in the mod. "I ask not just for Goldust but for all texture mods."

So, for a mod given with its clean base (a patched ROM, or an xdelta plus the base ROM), this diffs:
  files        the AKI file table itself (found by its shape, not assumed: moved, made longer) and every file in it: same,
               changed, added, removed, or moved to another number (the same bytes under a new number)
  family "nomercy" (No Mercy and its mods)
    appearance   file 1, the 0x26-byte looks entries;  names: file 2;  the Create-a-Wrestler part lists (files 0x4A56-0x4A66)
                 and the Mall (0x4AA9) by index and name
    parts.<kind> what each part number BUILDS, per part kind: the mod's own model builder run on its RAM image, one field
                 changed at a time, against the game's builder on the game's -- the files each value puts in the model, by
                 content (aki/nmprobe.py). This is the table Goldust's top went wrong in.
    roster       the per-costume records (ID, ordinal, music, video, size, moves / abilities files, appearance, names entry),
                 found in each RAM image by their shape; with each record's moves / abilities / appearance by CONTENT
  family "wm2000" (WrestleMania 2000 and its mods: Badd Blood)
    roster       the records (name, ID, size, moves / abilities files, music, video, four costumes' looks) in the main segment
    moves        each record's moves and abilities files by content; move names (0x94-0x97); the animation files
                 (0x1609-0x2847) and their index tables 0x3AE / 0x3AF
The result is the mod's remap (remap.json): per table, per key, "same" or what changed, and for a changed key the stock key
with the same meaning (if there is one). Handlers load it with Remap.load() and read every stock-table value through it.
"""
import hashlib, json, os, re, struct, zipfile

from . import romtable as RT, vcdiff

class ModTableChanged(Exception):
    """an importer used a stock meaning the mod changed -- the message says which table, key, and both meanings"""

# ---- loading a ROM, a mod ROM, or a patch ------------------------------------------------------------------------------
def read_rom(path):
    """a ROM's bytes in big-endian (.z64) order, read in place; a .zip's first ROM read from the archive in memory"""
    if path.lower().endswith(".zip"):
        z = zipfile.ZipFile(path)
        name = next(n for n in z.namelist() if n.lower().endswith((".z64", ".n64", ".v64")))
        b = z.read(name)
    else:
        b = open(path, "rb").read()
    head = b[:4]
    if head == b"\x37\x80\x40\x12":                       # (.v64: bytes swapped in pairs)
        a = bytearray(b); a[0::2], a[1::2] = b[1::2], b[0::2]; b = bytes(a)
    elif head == b"\x40\x12\x37\x80":                     # (.n64: little-endian words)
        a = bytearray(len(b))
        for i in range(4): a[i::4] = b[3 - i::4]
        b = bytes(a)
    return b

def load_mod(base_path, mod_path=None, patch_path=None):
    """-> (base bytes, mod bytes, how the mod was got). Nothing is written: a patch is applied in memory."""
    base = read_rom(base_path)
    if patch_path:
        if "::" in patch_path:                 # (<archive>.zip::<member>: a patch read from its archive in memory)
            zp, member = patch_path.split("::", 1)
            pb = zipfile.ZipFile(zp).read(member)
        else: pb = open(patch_path, "rb").read()
        mod = vcdiff.apply(base, pb)
        how = "the patch %s applied to the base in memory" % os.path.basename(patch_path)
        if mod_path:
            given = read_rom(mod_path)
            if given != mod: raise ModTableChanged("the patched ROM given isn't the patch applied to the base")
            how += " (== the patched ROM given)"
    else:
        mod = read_rom(mod_path); how = "the patched ROM %s" % os.path.basename(mod_path)
    return base, mod, how

# ---- the file table, found by its shape ----------------------------------------------------------------------------------
def find_file_tables(rom, min_files=1000):
    """every AKI file table in the ROM: (table offset, entries, data base). The shape: u32 offsets, bit 0 a packed flag, the
    first 0 or 1, never going down; and (all four games measured) the table sits right after the last file: base + the last
    entry == the table's own offset -- so the base comes out of it, nothing assumed."""
    import numpy as np
    out = []
    for al in range(0, 4, 2):
        n = (len(rom) - al) // 4
        w = np.frombuffer(rom, dtype=">u4", count=n, offset=al).astype(np.int64)
        v = w & ~1
        up = np.concatenate([v[1:] >= v[:-1], [False]])
        starts = np.nonzero((w <= 1) & up)[0]
        # (run lengths of "never going down", read once)
        brk = np.nonzero(~up)[0]
        for s in starts:
            if s > 0 and up[s - 1] and w[s - 1] <= 1: continue     # (inside a run of zeros: only its last zero can start a table)
            e = brk[np.searchsorted(brk, s)]        # (the last entry of the run)
            # (a table's only entry of 0 or 1 is its first: a run that has data going up before it starts at its LAST one --
            #  No Mercy Plus's table has 20 such words in front of it)
            low = np.nonzero(w[s:e + 1] <= 1)[0]
            s = s + int(low[-1])
            cnt = int(e - s + 1)
            if cnt < min_files: continue
            steps = np.diff(v[s:e + 1])
            if np.median(steps) < 16: continue      # (a run of counting data, not files: files are bigger than that)
            tab = al + 4 * int(s)
            for m in range(cnt, max(cnt - 4, 1), -1):     # (the run may take in a word or two after the table)
                base = tab - int(v[s + m - 1])
                if 0 < base < tab and base + int(v[s + 1]) <= tab:
                    out.append((tab, m, base)); break
    return sorted(set(out))

def lzss_cut(src, n):
    """aki LZSS for a packed file whose bytes end early (some mods' last files): as far as they go"""
    out = bytearray(); ring = bytearray(0x1000); r = 0xFEE; i = 4; flags = 0
    while len(out) < n and i < len(src):
        flags >>= 1
        if not (flags & 0x100): flags = src[i] | 0xFF00; i += 1
        if flags & 1:
            if i >= len(src): break
            c = src[i]; i += 1; out.append(c); ring[r] = c; r = (r + 1) & 0xFFF
        else:
            if i + 1 >= len(src): break
            lo = src[i]; hi = src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4)
            for k in range((hi & 0xF) + 3):
                c = ring[(pos + k) & 0xFFF]
                if len(out) < n: out.append(c)
                ring[r] = c; r = (r + 1) & 0xFFF
    return bytes(out)

class Files:
    """a ROM's files by number (the same reading as aki/romtable.py), its table found or given"""
    def __init__(self, rom, table=None, label=""):
        self.rom = rom; self.label = label
        if table is None:
            found = find_file_tables(rom)
            if not found: raise ModTableChanged("%s: no AKI file table found" % label)
            table = max(found, key=lambda t: t[1])
        self.tab, self.n, self.base = table
        self.t = struct.unpack(">%dI" % self.n, rom[self.tab:self.tab + 4 * self.n])
        self.cache = {}
    def count(self): return self.n - 1
    def raw(self, i):
        a = (self.t[i - 1] & ~1) + self.base; b = (self.t[i] & ~1) + self.base
        return self.rom[a:b]
    def __call__(self, i):
        if i < 1 or i > self.count(): return None
        if i not in self.cache:
            raw = self.raw(i)
            if self.t[i - 1] & 1 and len(raw) >= 4:
                dn = struct.unpack(">I", raw[:4])[0]
                if dn <= 0x400000:
                    try: raw = RT.lzss(raw, dn)
                    except IndexError: raw = lzss_cut(raw, dn)     # (a packed file cut short: what's there, as data/nmplus/xnm.py)
            self.cache[i] = raw
        return self.cache[i]
    def sha(self, i):
        b = self(i); return None if b is None else hashlib.sha1(b).hexdigest()[:16]

def diff_files(base, mod):
    """-> the "files" table: per number same / changed / added / removed, and a changed or added one's bytes found under
    another stock number ("moved_from")"""
    changed, added, removed = [], [], []
    for i in range(1, max(base.count(), mod.count()) + 1):
        if i > mod.count(): removed.append(i); continue
        if i > base.count(): added.append(i); continue
        if base.raw(i) == mod.raw(i): continue
        if base(i) != mod(i): changed.append(i)
    by = {}
    for i in changed + added:                    # (only these: the stock file of the same bytes, if any)
        by.setdefault(mod(i), None)
    if by:
        for j in range(1, base.count() + 1):
            b = base(j) if len(base.raw(j)) < 0x200000 else None
            if b in by and by[b] is None: by[b] = j
    moved = {i: by[mod(i)] for i in changed + added if by.get(mod(i)) not in (None, i)}
    return {"status": "same" if not (changed or added or removed) and (base.tab, base.n, base.base) == (mod.tab, mod.n, mod.base) else "changed",
            "stock_table": [base.tab, base.n, base.base], "mod_table": [mod.tab, mod.n, mod.base],
            "moved_table": base.tab != mod.tab, "longer_by": mod.count() - base.count(),
            "changed": changed, "added_count": len(added), "added_first_last": [added[0], added[-1]] if added else [],
            "removed": removed, "moved_from": {str(k): v for k, v in moved.items()}}

# ---- No Mercy's lists in files ----------------------------------------------------------------------------------------
CAW_LISTS = range(0x4A56, 0x4A67)        # (the Create-a-Wrestler part lists; imports2 scripts\caw_lists.py)
MALL = 0x4AA9
def caw_items(d):
    """a part list -> {index: name}: each item is 01|03, a kind byte, its index, its name, 0 (imports2 caw_lists.py's
    reading). Used for labels only: what an index MEANS is the parts probe's (aki/nmprobe.py), by content."""
    out = {}
    if d is None: return out
    for m in re.finditer(rb"[\x01\x03][\x00-\xff]([\x00-\xff])([\x20-\x7e]{2,})\x00", d):
        out.setdefault(m.group(1)[0], m.group(2).decode("latin1"))
    return out

def names_list(d):
    """file 2 -> [name]: u16 offsets, then the strings"""
    out = []
    if not d or len(d) < 2: return out
    first = struct.unpack_from(">H", d, 0)[0]
    for k in range(first // 2):
        o = struct.unpack_from(">H", d, 2 * k)[0]
        if o >= len(d): break
        e = d.find(b"\0", o); out.append(d[o:e if e >= 0 else len(d)].decode("latin1", "replace"))
    return out

def keyed_diff(a, b, same=lambda x, y: x == y):
    """two {key: value} -> (status, changed keys, added keys, removed keys)"""
    ch = sorted(k for k in a if k in b and not same(a[k], b[k]))
    ad = sorted(k for k in b if k not in a); rm = sorted(k for k in a if k not in b)
    return ("same" if not (ch or ad or rm) else "changed"), ch, ad, rm

def table_entry(status, changed, added, removed, stock, mod, about, **more):
    d = {"status": status, "about": about, "changed": changed, "added": added, "removed": removed,
         "stock": {str(k): stock[k] for k in changed + removed if k in stock},
         "mod": {str(k): mod[k] for k in changed + added if k in mod}}
    d.update(more)
    return d

# ---- No Mercy's roster records, found in a RAM image -----------------------------------------------------------------
def find_nm_roster(ram):
    """the per-costume records (0x14 bytes: ID, ordinal, music, video, height, -, weight, moves, abilities, appearance,
    names, -), 4 per ordinal, one after another from ordinal 1: found by their shape -- ordinal 1's four records each
    ID 1, ordinal 1, appearances 0-3, an empty record before them (stock No Mercy: 0x800459B8, No Mercy Plus: 0x80401060).
    -> the address of ordinal 1's first record"""
    KEY = bytes([0, 1, 0, 1])                 # (ID 1, ordinal 1)
    p = 0
    while True:
        p = ram.find(KEY, p + 1)
        if p < 0: return None
        if p % 2 or p < 0x14: continue
        if all(ram[p + 0x14 * c:p + 0x14 * c + 4] == KEY and
               struct.unpack_from(">H", ram, p + 0x14 * c + 0xE)[0] == c for c in range(4)) and ram[p - 0x14:p] == bytes(0x14):
            return 0x80000000 | p

def nm_roster(ram, at, files, names):
    k = ("id", "ordinal", "music", "video", "height", "spare6", "weight", "moves", "abilities", "appearance", "names", "spare12")
    out = {}
    a = at & 0x7FFFFF
    for o in range(1, 160):
        ok = False
        for c in range(4):
            r = dict(zip(k, struct.unpack_from(">HHBBBBHHHHHH", ram, a + (o - 1) * 0x50 + c * 0x14)))
            if r["ordinal"] != o: break
            ok = True
            nm = names[r["names"]] if r["names"] < len(names) else None
            ap = files(1)
            app = ap[0x26 * r["appearance"]:0x26 * r["appearance"] + 0x26].hex() if ap else None
            out["%d/%d" % (o, c)] = dict(r, name=nm, moves_sha=files.sha(r["moves"]), abilities_sha=files.sha(r["abilities"]),
                                         appearance_bytes=app)
        if not ok: break
    return out

def diff_nm(base, mod, base_ram=None, mod_ram=None, probe=True, log=print):
    """the No Mercy family's tables, stock -> mod"""
    T = {}
    ap_b, ap_m = base(1) or b"", mod(1) or b""
    ea = {i: ap_b[0x26 * i:0x26 * i + 0x26].hex() for i in range(len(ap_b) // 0x26)}
    eb = {i: ap_m[0x26 * i:0x26 * i + 0x26].hex() for i in range(len(ap_m) // 0x26)}
    st, ch, ad, rm = keyed_diff(ea, eb)
    T["appearance"] = table_entry(st, ch, ad, rm, ea, eb, "file 1: the 0x26-byte looks entries (in that game's part numbers)")
    na, nb = names_list(base(2)), names_list(mod(2))
    da, db = dict(enumerate(na)), dict(enumerate(nb))
    st, ch, ad, rm = keyed_diff(da, db)
    T["names"] = table_entry(st, ch, ad, rm, da, db, "file 2: the names, by entry")
    for f in list(CAW_LISTS) + [MALL]:
        la, lb = caw_items(base(f)), caw_items(mod(f))
        st, ch, ad, rm = keyed_diff(la, lb)
        T["caw_%04X" % f] = table_entry(st, ch, ad, rm, la, lb, ("the Mall's packs" if f == MALL else
                                        "a Create-a-Wrestler part list") + " (file 0x%04X): index -> name, labels only" % f)
    if base_ram is not None and mod_ram is not None:
        ra, rb = find_nm_roster(base_ram), find_nm_roster(mod_ram)
        if ra is None or rb is None:
            T["roster"] = {"status": "unknown", "about": "the roster records weren't found in a RAM image (%s / %s)" % (ra, rb)}
        else:
            xa, xb = nm_roster(base_ram, ra, base, na), nm_roster(mod_ram, rb, mod, nb)
            # (the same MEANING: the same name, ID, music, video, size, the moves / abilities / looks bytes -- not their numbers)
            def same(r1, r2):
                return all(r1[k] == r2[k] for k in ("id", "music", "video", "height", "weight", "name", "moves_sha",
                                                     "abilities_sha", "appearance_bytes"))
            st, ch, ad, rm = keyed_diff(xa, xb, same)
            fields = {k: [f for f in ("id", "name", "music", "video", "height", "weight", "moves_sha", "abilities_sha",
                                      "appearance_bytes") if xa[k][f] != xb[k][f]] for k in ch}
            T["roster"] = table_entry(st, ch, ad, rm, xa, xb, "the per-costume records (ordinal/costume), found in each RAM "
                                      "image at 0x%08X / 0x%08X; compared by meaning (name, ID, music, video, size, the "
                                      "moves / abilities / looks bytes)" % (ra, rb), fields_changed=fields)
        if probe:
            from . import nmprobe
            T.update(nmprobe.diff_parts(base, mod, base_ram, mod_ram, log=log))
    else:
        T["roster"] = {"status": "unknown", "about": "needs RAM images (--base-ram / --mod-ram): the records live in RAM"}
        T["parts"] = {"status": "unknown", "about": "needs RAM images: the parts probe runs each game's own model builder"}
    return T

# ---- WrestleMania 2000's roster, in the main segment ----------------------------------------------------------------
WM_MAIN = (0x80000400, 0x1000, 0x144AA0)
WM_ROSTER, WM_ROSTER_N = 0x800411B0, 0x4C
def wm_roster(rom, files):
    vram, r0, r1 = WM_MAIN
    img = bytearray(0x800000); img[vram & 0x7FFFFF:(vram & 0x7FFFFF) + r1 - r0] = rom[r0:r1]
    r32 = lambda a: struct.unpack_from(">I", img, a & 0x7FFFFF)[0]
    def cstr(a):
        a &= 0x7FFFFF; e = img.index(b"\0", a); return img[a:e].decode("latin1", "replace")
    out = {}
    for k in range(WM_ROSTER_N):
        rec = r32(WM_ROSTER + 4 * k)
        if not 0x80000400 <= rec < 0x80144000:
            raise ModTableChanged("record %d of the roster isn't where WrestleMania 2000 keeps it: this mod moved the table" % k)
        f = struct.unpack(">HHHHHHBB", bytes(img[(rec + 4) & 0x7FFFFF:(rec + 4 & 0x7FFFFF) + 14]))
        looks = [bytes(img[r32(rec + 0x14 + 4 * i) & 0x7FFFFF:(r32(rec + 0x14 + 4 * i) & 0x7FFFFF) + 0x24]).hex() for i in range(4)]
        out[k] = dict(name=cstr(r32(rec)), id=f[0], ordinal=f[1], height=f[2], weight=f[3], moves=f[4], abilities=f[5],
                      music=f[6], video=f[7], moves_sha=files.sha(f[4]), abilities_sha=files.sha(f[5]), looks=looks)
    return out

def diff_wm(base, mod, log=print):
    T = {}
    ra, rb = wm_roster(base.rom, base), wm_roster(mod.rom, mod)
    keys = ("name", "id", "height", "weight", "music", "video", "moves_sha", "abilities_sha", "looks")
    st, ch, ad, rm = keyed_diff(ra, rb, lambda a, b: all(a[k] == b[k] for k in keys))
    T["roster"] = table_entry(st, ch, ad, rm, ra, rb, "the %d records (main segment 0x%08X): name, ID, size, moves / "
                              "abilities files by content, music, video, four costumes' looks" % (WM_ROSTER_N, WM_ROSTER),
                              fields_changed={k: [f for f in keys if ra[k][f] != rb[k][f]] for k in ch})
    for name, rng, about in (("move_names", range(0x94, 0x98), "the move names (files 0x94-0x97)"),
                             ("anim_index", (0x3AE, 0x3AF), "the animation index tables (files 0x3AE / 0x3AF)"),
                             ("anims", range(0x1609, 0x2848), "the animation files (0x1609-0x2847)")):
        a = {i: base.sha(i) for i in rng}; b = {i: mod.sha(i) for i in rng}
        st, ch, ad, rm = keyed_diff(a, b)
        T[name] = {"status": st, "about": about, "changed": ch}
    return T

# ---- the whole diff --------------------------------------------------------------------------------------------------
FAMILIES = ("nomercy", "wm2000", "revenge")
def diff(family, base_rom, mod_rom, base_ram=None, mod_ram=None, how="", probe=True, log=print):
    base = Files(base_rom, label="the base"); mod = Files(mod_rom, label="the mod")
    log("files: base table 0x%X (%d files, data 0x%X), mod table 0x%X (%d files, data 0x%X)"
        % (base.tab, base.count(), base.base, mod.tab, mod.count(), mod.base))
    T = {"files": diff_files(base, mod)}
    T["files"]["about"] = "the AKI file table and every file: by number, by content"
    if family == "nomercy": T.update(diff_nm(base, mod, base_ram, mod_ram, probe=probe, log=log))
    elif family == "wm2000": T.update(diff_wm(base, mod, log=log))
    out = {"format": 1, "family": family, "how": how,
           "base_sha1": hashlib.sha1(base_rom).hexdigest(), "mod_sha1": hashlib.sha1(mod_rom).hexdigest(),
           "identical": base_rom == mod_rom, "tables": T}
    return out

# ---- the remap, as handlers use it -----------------------------------------------------------------------------------
class Remap:
    """A mod's remap (remap.json). Every value a handler takes from STOCK knowledge -- a part number that means "none" in the
    game, a file number the stock game has some file at, a roster index -- goes through here, and if the mod changed that
    table at that key, it stops with both meanings rather than carrying on with the wrong one.

        R = Remap.load("work/nmplus/remap.json")
        R.stock("parts.upper", 0x7D)        # raises: the mod's 125 builds Mideon's shirt, the stock 125 builds no top
        R.to_stock("parts.lower", 139)      # the stock number that builds the same, or raises (none has it: a model of his own)
        R.same("files", 0x95)               # -> False for a file the mod changed
    A table the remap has no word on ("unknown": no RAM image, say) also stops: unchecked isn't the same as unchanged."""
    def __init__(self, d, path=""):
        self.d = d; self.path = path; self.T = d["tables"]
        self.used = []                        # (every check made: the import report lists them)
    @classmethod
    def load(cls, path): return cls(json.load(open(path)), path)
    @classmethod
    def none(cls, about="no mod: the source game itself"):
        """for a handler reading a clean, unmodded ROM: every table is the stock one"""
        return cls({"format": 1, "family": None, "identical": True, "how": about, "tables": {}}, "")
    def _t(self, table):
        if self.d.get("identical"): return None
        t = self.T.get(table)
        if t is None:
            top = table.split(".")[0]
            t = self.T.get(top)
            if t is None or t.get("status") == "unknown":
                raise ModTableChanged("%s: the remap (%s) hasn't checked table %r -- run the table diff with what it needs"
                                      % (self.d.get("how"), self.path, table))
        if t.get("status") == "unknown":
            raise ModTableChanged("%s: table %r unchecked: %s" % (self.path, table, t.get("about")))
        return t
    def same(self, table, key):
        t = self._t(table)
        if t is None: return True
        if t.get("status") == "same": return True
        k = str(key)
        if table == "files":
            i = int(key)
            return i not in t["changed"] and i not in t["removed"] and not (t["added_first_last"] and
                   t["added_first_last"][0] <= i <= t["added_first_last"][1])
        return k not in t.get("stock", {}) and k not in t.get("mod", {}) and key not in t.get("changed", []) \
            and key not in t.get("added", []) and key not in t.get("removed", [])
    def stock(self, table, key, why=""):
        """the handler is about to use `key` with its STOCK meaning: allowed only if the mod left it so"""
        ok = self.same(table, key)
        self.used.append((table, key, "stock meaning", ok, why))
        if not ok and os.environ.get("MODCHECK_NEGATIVE_TEST") == "1":
            # (modcheck's negative test only: make the WRONG folder on purpose, to show the visual check flags it too)
            print("NEGATIVE TEST: %s %r used with its stock meaning although the mod changed it" % (table, key))
            return key
        if not ok:
            t = self._t(table); k = str(key)
            raise ModTableChanged("%s %r: the handler uses the stock meaning (%s), but this mod changed it -- stock: %s; mod: %s%s"
                                  % (table, key, why or "no reason given", t.get("stock", {}).get(k, "(none)"),
                                     t.get("mod", {}).get(k, "(none)"),
                                     "; the stock meaning is the mod's %s" % t["map_stock_to_mod"][k]
                                     if k in t.get("map_stock_to_mod", {}) else ""))
        return key
    def mod_for_stock(self, table, key, why=""):
        """the mod's key that means what the stock `key` means (e.g. the mod's "no top" for the stock 0x7D)"""
        if self.same(table, key): self.used.append((table, key, "same", True, why)); return key
        t = self._t(table); m = t.get("map_stock_to_mod", {}).get(str(key))
        self.used.append((table, key, "mod's equivalent %s" % m, m is not None, why))
        if m is None:
            raise ModTableChanged("%s %r (%s): this mod has nothing that means what the stock one does" % (table, key, why))
        return m
    def to_stock(self, table, key, why=""):
        """the stock key that means what the mod's `key` does (a mod value kept in our game's numbering)"""
        if self.same(table, key): self.used.append((table, key, "same", True, why)); return key
        t = self._t(table); s = t.get("map_mod_to_stock", {}).get(str(key))
        self.used.append((table, key, "stock equivalent %s" % s, s is not None, why))
        if s is None:
            raise ModTableChanged("%s %r (%s): this mod's value has no stock equivalent (it's the mod's own: stock %s, mod %s)"
                                  % (table, key, why, t.get("stock", {}).get(str(key), "(none)"), t.get("mod", {}).get(str(key), "(none)")))
        return s
    def report(self):
        return ["remap %s: %s %r %s%s" % ("OK " if ok else "BAD", t, k, how, (" -- " + why) if why else "") for t, k, how, ok, why in self.used]

class ModFiles:
    """a mod's files, with the stock game's beside them: files(i) is the mod's; stock_file(i) is the base's and stops if
    the mod changed file i (a handler reading a stock file the mod replaced is reading the wrong thing)"""
    def __init__(self, mod_files, base_files, remap):
        self.m, self.b, self.R = mod_files, base_files, remap
    def __call__(self, i): return self.m(i)
    def count(self): return self.m.count()
    def stock_file(self, i, why=""):
        self.R.stock("files", i, why); return self.b(i)
