#!/usr/bin/env python3
"""mkplan.py: the release's mod without game data, and the plan the game's first start rebuilds it by (firstrun, 2026-11-17).

  python tools/firstrun/mkplan.py [--build mod/build/mod_six] [--content mod/build] [--out scratch/firstrun_plan]

The mod as built (build_mod.ps1) has tables made from games compiled into it: the mod's game-data headers (mod/src, made by
mod/gen from the games' files: tools/layout/private_links.txt lists them) and gen/asmpatch.py's patched copies of the six-man
ROM's functions (patches.o). A release may hold no game data (the user, 2026-11-14), so:

  firstrun/mod_binary.holes   the mod's image (mod_binary.bin) with every byte of those tables zero -- the HOLES
  firstrun/mod.json, mod_syms.bin   the mod's other two members, as built (the manifest and the symbol table: ours)
  firstrun/plan.bin           how the first start fills the holes again from the player's ROM: for each BASE hole, copies
                              of the player's No Mercy ROM (as it is, as the six-man engine makes it, or one of its files
                              unpacked) and the bytes that are ours (the link's addresses, the six-man patches' own words,
                              lists of file numbers); the SHA-256 of the image each way; the content packs' SHA-256
  modextra.bin                (never shipped: it is game data, like content.bin) the EXTRAS holes' bytes -- what only the
                              other games' ROMs, the emulator rig's captures and the theme recordings give: the ported
                              Superstars, No Mercy Plus's themes and videos, the arcade's arenas -- and the
                              flag that says they are in (mod/src/extras.c). The first start fills them only when this and
                              the content packs are all there and as made (their SHA-256).
  plan_report.txt             every hole: its symbol, class, bytes, how many the ROM gives, how many are ours and why
  firstrun/hdrfill.bin        (hdrlayout.py, called at the end) how each EXTRAS hole's bytes follow from its game-data
                              header's text: the layouts the game fills them by once the importer has made the headers
                              from the player's ROMs (only our numbers: offsets, sizes, leaf indices, the link's words);
                              hdrfill_report.txt says each hole's check (the text gives the build's bytes: SAME)

The holes are found from the build itself (mod.elf's symbols and relocations, mod.map's input sections), not guessed: a table
whose name a game-data header defines, every input section of patches.o, the extras flag. BASE (what the No Mercy ROM alone
gives) is the list below; everything else from a game-data header is EXTRAS. A base hole's bytes that no copy of the ROM
gives and that are not the link's must be one of the reviewed kinds (OURS_OK) -- otherwise this stops and says which: game
data the ROM doesn't give as it is has to be made by code (a port in the exe's arcade_firstrun.cpp), never shipped.

Exit 0 when the plan, applied here to a pool made from the ROM alone, gives the build's image back byte for byte.
"""
import argparse, hashlib, os, re, struct, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import modelf   # noqa: E402

REV1_SHA1 = '91cee3d096f4a76644d8b35b9aead6448909abd1'
AKI_TABLE = (0x16C31D8, 19459, 0x1BD150)    # No Mercy Rev 1's file table: offset, entries, data base (handlers/aki/romtable.py)
EXTRAS_FLAG = 'arcade_extras_in'
# Tables of game data written into the mod's own source (not a generated header), for the extras only: holes too.
EXTRAS_IN_SOURCE = {}   # (handsrom 2026-11-25: hands.c's wm_pose -- WrestleMania 2000's hand poses -- is wmport_files.h's wmh_pose now)
MIN_COPY = 24          # (a copy found anywhere in the ROM: this long or more)
MIN_FILL = 4           # (a run of one byte value this long: a FILL, not a copy and not data)
CONTENT_PACKS = ('content.bin', 'wmport.bin')   # (exe arcade_packs.cpp PACK_FILES: the extras' packs; no2k15: hd.bin, hd_rock.bin gone)

# ---- what the No Mercy ROM alone gives, and the game needs without the extras (symbol name -> the part of it that is base:
#      (start, end) in bytes, None = all of it). The arenas' tables run over the game's ten arenas (0..9), then the arcade's 13.
BASE = [
    (r'xw\d', None), (r'xw\d_(rec|span|list|fz)', None), (r'xwalls', None),          # (the game's ten arenas' walls; xwalls: pointers)
    (r'xrs\d_cells', None), (r'xrs_spots', None), (r'xrs_arenas', (0, 10 * 28)),       # (their ring-and-stage maps)
    (r'xtable_block', (0, 10 * 0x100)),                                                  # (their per-venue entries)
    (r'xstage\d?_(mesh|pal)', None), (r'xstage_n', None), (r'xstage\d_close_(mesh|pal)', None),   # (their stage sets: file numbers)
    (r'xraw_(set_mesh|set_pal|fist_tex|fist_pal)', None),                                # (RAW IS WAR's)
    (r'label_custom_story', None), (r'label_versus', None), (r'label_lumberjack', None), (r'label_lj_count', None), (r'label_lj_pick', None), (r'hdr_exhibition', None), (r'hdr_versus', None),   # (menu lettering)
    (r'media_songs', None),                                                              # (the song table: the game's, longer)
    (r'wmp_files', (0x1075 * 8, 0x1198 * 8)),   # (the pack's file table's rows of the stage sets' files 0x6075-0x6197: where they are in wmport.bin / wmport_base.bin;
                                                # stagescale 2026-11-20: 5 rows on, the WM2000 close scenes' pieces before them now 5 more -- was 0x1070-0x1193)
]
# (romimport's finding, 2026-11-17) The 15 part tables of parts_ext.h are the game's own tables made longer: the patched
# func_80018A3C / func_8001858C (always on) read them for EVERY Superstar's parts, so their entries before the new ones are
# base: element size, first new entry (gen/mkparts.py: the game's entries, then the new from 0x80 / 0x20).
PART_TABLES = {'parts_lower_kind': (1, 0x80), 'parts_lower_fem': (1, 0x80), 'parts_lower_desc': (4, 0x80),
               'parts_upper_kind': (1, 0x80), 'parts_upper_desc': (4, 0x80), 'parts_face_tex': (2, 0x80),
               'parts_face_b1': (1, 0x80), 'parts_face_b2': (1, 0x80), 'parts_face_tv': (2, 0x80), 'parts_fh_tex': (2, 0x20),
               'parts_fh_b': (1, 0x20), 'parts_mask_desc': (4, 0x20), 'parts_mask_b': (1, 0x20), 'parts_param_kind': (1, 0x80),
               'parts_param_files': (4, 0x80)}
BASE += [(name, (0, size * ext0)) for name, (size, ext0) in PART_TABLES.items()]
BASE_RE = [(re.compile('^(%s)$' % p), part) for p, part in BASE]

# ---- reviewed kinds of base bytes that are ours (no copy of the ROM gives them, and they are not the link's): symbol -> why.
#      Anything else left over stops the plan.
OURS_OK = [
    (r'func_[0-9A-Fa-f]+|\.recomp_patch|\.rodata\.jt_.*', "the six-man engine's and the arcade's changes to the copied functions (gen/asmpatch.py): our code"),
    (r'xstage\d?_(mesh|pal)|xstage_n|xstage\d_close_(mesh|pal)|xraw_set_(mesh|pal)', 'lists of file numbers (which of the game\'s files make a stage set): references, not the files'),
    (r'xwalls|xw\d_(span|list|fz)|xw\d', "the walls' spans, lists and zones: the link's addresses and the game's own addresses (0x8014....)"),
    (r'media_songs', "the songs the arcade adds after the game's 80: sound numbers 200 on (ours)"),
    (r'xtable_block', "the per-venue entries' pointers (the link's and the game's addresses)"),
    (r'wmp_files', "the mod's pack's file table: each file's offset and size in the pack (its layout)"),
    (r'parts_\w+', "the part tables' game entries: the game's addresses (0x8004....), file numbers and kinds, as its own tables have them"),
]
OURS_RE = [(re.compile('^(%s)$' % p), why) for p, why in OURS_OK]

# ---- base tables our generators COMPUTED from the game's data (not copies of it): made by code in the exe from the ROM
#      (src/main/arcade_firstrun_ports.cpp), never shipped. symbol -> the port that makes it.
PORTS = [
    (r'xw\d_rec', 'walls'),               # (gen/arenas_walls.py: the game's ten arenas' collision records)
    (r'xrs\d_cells', 'ringstage'),        # (gen/arenas_collide.py + arenas_walls.py write_ringstage: their ring-and-stage maps)
    (r'xrs_arenas', 'ringstage'),
    (r'label_custom_story|label_versus|label_lumberjack|label_lj_count|label_lj_pick', 'labels'),     # (gen/labeltex.py: menu labels in the game's lettering)
    (r'hdr_versus', 'labels'),
]
PORT_RE = [(re.compile('^(%s)$' % p), name) for p, name in PORTS]


def sha256(b): return hashlib.sha256(b).digest()


def generated_headers(headers=None):
    """the mod's game-data headers: tools/layout/private_links.txt's mod/src entries (headers: the folder they are in, if
    not linked into mod/src -- e.g. the private generated folder; hdrlayout.py --headers)"""
    out = []
    for line in open(os.path.join(SRC, 'tools', 'layout', 'private_links.txt'), encoding='utf-8'):
        f = [x.strip() for x in line.split('|')]
        if len(f) == 3 and f[0] == 'file' and f[1].startswith('mod/src/') and f[1].endswith('.h'):
            out.append(os.path.join(headers, os.path.basename(f[1])) if headers else os.path.join(SRC, f[1]))
    return out


def defined_names(path):
    """the names a C header defines at file level (arrays and variables with an initializer): on a line that starts a
    definition, the last name before its first '[' or '='"""
    t = open(path, encoding='latin-1').read()
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    t = re.sub(r'//[^\n]*', '', t)
    names = set()
    for line in t.split('\n'):
        if not line or line[0] in ' \t#}' or line.startswith(('typedef', '_Static_assert', 'extern')): continue
        head = re.split(r'[\[=]', line, 1)
        if len(head) < 2 or '(' in head[0]: continue
        m = re.search(r'(\w+)\s*$', head[0])
        if m and m.group(1) not in ('const', 'static'): names.add(m.group(1))
    return names


# ---- the pool: what the player's ROM gives -----------------------------------------------------------------------------
def rom_order(b):
    h = b[:4]
    if h == b'\x80\x37\x12\x40': return bytes(b)
    a = np.frombuffer(b, np.uint8)
    if h == b'\x37\x80\x40\x12': return a.reshape(-1, 2)[:, ::-1].tobytes()
    if h == b'\x40\x12\x37\x80': return a.reshape(-1, 4)[:, ::-1].tobytes()
    raise SystemExit('mkplan: not an N64 ROM')


def six_patch_table():
    t = open(os.path.join(SRC, 'exe', 'WWFNoMercyRecomp', 'src', 'main', 'six_rompatch.h')).read()
    return [(int(a, 16), int(b, 16)) for a, b in re.findall(r'\{ 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+) \}', t)]


def six_rom(rom):
    """the stock ROM as the exe makes it the six-man game's (main.cpp six_engine_init: every word of six_rompatch.h)"""
    s = bytearray(rom)
    for off, w in six_patch_table(): s[off:off + 4] = w.to_bytes(4, 'big')
    return bytes(s)


def lzss(src, n):
    """AKI's LZSS (romtable.py; the exe's arcade_firstrun.cpp has the same): a 4 KB ring from 0xFEE, flags low bit first"""
    ring = bytearray(0x1000); r = 0xFEE; i = 4; out = bytearray(n); o = 0; flags = 0
    while o < n and i < len(src):
        flags >>= 1
        if not flags & 0x100: flags = src[i] | 0xFF00; i += 1
        if flags & 1:
            if i >= len(src): break
            c = src[i]; i += 1; out[o] = c; o += 1; ring[r] = c; r = (r + 1) & 0xFFF
        else:
            if i + 1 >= len(src): break
            lo, hi = src[i], src[i + 1]; i += 2
            pos = lo | ((hi & 0xF0) << 4); ln = (hi & 0xF) + 3
            for k in range(ln):
                c = ring[(pos + k) & 0xFFF]
                if o < n: out[o] = c; o += 1
                ring[r] = c; r = (r + 1) & 0xFFF
    return bytes(out)


def aki_file(rom, i):
    """file i (1 .. entries - 1) of No Mercy's file table, unpacked as the game's file code does"""
    tab, entries, base = AKI_TABLE
    t0, t1 = struct.unpack_from('>II', rom, tab + 4 * (i - 1))
    a, b = min((t0 & ~1) + base, len(rom)), min((t1 & ~1) + base, len(rom))
    raw = rom[a:b] if b > a else b''
    if t0 & 1 and len(raw) >= 4:
        n = struct.unpack_from('>I', raw, 0)[0]
        if n <= 0x400000: return lzss(raw, n)
    return raw


def all_files(rom, work):
    """every file unpacked, one after another (the project's aki_unpack.c, built here for speed): (bytes, [(start, len)])"""
    exe = os.path.join(work, 'aki_unpack.exe')
    if not os.path.exists(exe):
        cc = os.path.join(SRC, 'toolchain', 'llvm-mingw', 'bin', 'clang.exe')
        subprocess.check_call([cc, '-O2', os.path.join(HERE, 'aki_unpack.c'), '-o', exe])
    rp = os.path.join(work, 'rom.z64'); open(rp, 'wb').write(rom)
    tab, entries, base = AKI_TABLE
    subprocess.check_call([exe, rp, hex(tab), str(entries), hex(base), os.path.join(work, 'files.bin'), os.path.join(work, 'files.idx')])
    os.remove(rp)
    data = open(os.path.join(work, 'files.bin'), 'rb').read()
    idx = open(os.path.join(work, 'files.idx'), 'rb').read()
    spans = [struct.unpack_from('<QII', idx, 16 * k)[:2] for k in range(len(idx) // 16)]
    return data, spans


class Pool:
    """the bytes the plan may copy: 0 the ROM (z64 order), 1 the six-man ROM, 2.. file i - 1 + 2 (unpacked)"""
    K = 8

    def __init__(self, rom, files, spans):
        self.rom, self.six = rom, six_rom(rom)
        self.files, self.spans = files, spans
        self.parts = [self.rom, self.six, files]
        self.starts = [0, len(rom), 2 * len(rom)]
        whole = np.concatenate([np.frombuffer(p, np.uint8) for p in self.parts])
        self.whole = whole.tobytes()
        # (every 4th byte of each ROM, and of each file from its own start: the files lie one after another, unaligned)
        n_rom = len(rom) - self.K + 1
        per_file = [self.starts[2] + s + np.arange(0, max(0, n - self.K + 1), 4, dtype=np.int64) for s, n in spans]
        idx = np.concatenate([np.arange(0, n_rom, 4, dtype=np.int64), len(rom) + np.arange(0, n_rom, 4, dtype=np.int64)] + per_file)
        h = self.hash(whole, idx)
        o = np.argsort(h, kind='stable'); self.h = h[o]; self.at = idx[o]

    def hash(self, a, idx):
        h = np.zeros(len(idx), np.uint64)
        for k in range(self.K): h = h * np.uint64(1099511628211) + a[idx + k].astype(np.uint64)
        return h

    def where(self, g):
        """global offset -> (source, offset): 0 rom, 1 six, ('file', i, offset in it)"""
        if g < self.starts[1]: return (0, g)
        if g < self.starts[2]: return (1, g - self.starts[1])
        f = g - self.starts[2]
        k = int(np.searchsorted(self.file_starts, f, side='right')) - 1
        return (2 + k, f - self.spans[k][0])

    def best(self, data, p, hint):
        """the longest copy of data[p:]: (global offset, length), or None. Found anywhere, a copy is MIN_COPY bytes or more
        and not a run of a few byte values (a run of zeros or of 0xFF is everywhere and says nothing of where data came from:
        that is a FILL); resumed where the last copy left off (hint: a function copied with a word of ours in it), 4 or more."""
        W = self.whole
        best = None
        if hint is not None and hint + 4 <= len(W) and W[hint:hint + 4] == data[p:p + 4]:
            n = 0
            while p + n < len(data) and hint + n < len(W) and W[hint + n] == data[p + n]: n += 1
            if n >= 4: best = (hint, n)     # (the same source read on: a function copied with a word of ours in it)
        if len(data) - p < 16: return best
        a = np.frombuffer(data[p:p + self.K], np.uint8)
        hv = 0
        for k in range(self.K): hv = (hv * 1099511628211 + int(a[k])) & 0xFFFFFFFFFFFFFFFF
        hv = np.uint64(hv)
        j0 = int(np.searchsorted(self.h, hv)); j1 = int(np.searchsorted(self.h, hv, side='right'))
        for j in range(j0, min(j1, j0 + 16384)):   # (a common header has thousands of places: the longest of them)
            g = int(self.at[j])
            if W[g:g + self.K] != data[p:p + self.K]: continue
            n = self.K
            while p + n < len(data) and g + n < len(W) and W[g + n] == data[p + n]: n += 1
            src, off = self.where(g)
            lim = (len(self.rom) - off) if src < 2 else (self.spans[src - 2][1] - off)
            n = min(n, lim)
            whole_file = src >= 2 and off == 0 and n >= 16      # (a file from its start: where a table copied from a file begins)
            if (n < MIN_COPY and not whole_file) or len(set(data[p:p + n])) < 6: continue
            if best is None or n > best[1]: best = (g, n)
        return best


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', default=os.path.join(SRC, 'mod', 'build', 'mod_six'))
    ap.add_argument('--content', default=os.path.join(SRC, 'mod', 'build'), help='the content packs as built (their SHA-256 go in the plan)')
    ap.add_argument('--rom', default=os.path.join(SRC, 'roms', 'WWF No Mercy (USA) (Rev 1).z64'))
    ap.add_argument('--out', default=os.path.join(SRC, 'scratch', 'firstrun_plan'))
    ap.add_argument('--headers', help='the generated headers the build used, if not linked into mod/src')
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, 'firstrun'), exist_ok=True)
    work = os.path.join(a.out, 'work'); os.makedirs(work, exist_ok=True)

    image, holes, relocs = find_holes(a.build, a.headers)
    holed = bytearray(image)
    for s, e, cls, label in holes: holed[s:e] = bytes(e - s)
    base_img = bytearray(image)
    for s, e, cls, label in holes:
        if cls == 'extras': base_img[s:e] = bytes(e - s)
    for w in extras_link_words(holes, relocs): base_img[w:w + 4] = image[w:w + 4]
    main2(a, work, image, holes, relocs, holed, base_img)


def find_holes(build, headers=None):
    """(the mod's image, its holes [(start, end, 'base' | 'extras', label)] in image offsets, its relocated words) -- from the
    build folder's mod.elf, mod.map and mod_binary.bin (tools/aot/aot.py uses it too: the release's base image's hash)"""
    elf, secs, syms = modelf.read_elf(os.path.join(build, 'mod.elf'))
    relocs = modelf.reloc_words(elf, secs)
    image = open(os.path.join(build, 'mod_binary.bin'), 'rb').read()
    for s in secs:     # (the image is the ELF's sections from the link address: checked)
        if s['flags'] & 2 and s['type'] != 8 and s['size']:
            o = s['addr'] - modelf.BASE
            assert image[o:o + s['size']] == elf[s['off']:s['off'] + s['size']], 'mod_binary.bin is not this mod.elf: ' + s['name']
    names = {}
    for h in generated_headers(headers):
        for n in defined_names(h): names.setdefault(n, os.path.basename(h))
    holes = []      # (start, end, class 'base' | 'extras', label)
    def add(start, end, cls, label):
        if end > start: holes.append((start - modelf.BASE, end - modelf.BASE, cls, label))
    for s in syms:
        if not s['alloc'] or s['type'] != 1 or s['size'] == 0: continue
        if s['name'] == EXTRAS_FLAG: add(s['addr'], s['addr'] + s['size'], 'extras', EXTRAS_FLAG); continue
        if s['name'] in EXTRAS_IN_SOURCE: add(s['addr'], s['addr'] + s['size'], 'extras', '%s (%s)' % (s['name'], EXTRAS_IN_SOURCE[s['name']])); continue
        hdr = names.get(s['name'])
        if not hdr: continue
        part = next((pt for r, pt in BASE_RE if r.match(s['name'])), 'none')
        if part == 'none':
            add(s['addr'], s['addr'] + s['size'], 'extras', '%s (%s)' % (s['name'], hdr))
        else:
            lo, hi = part if part else (0, s['size'])
            add(s['addr'], s['addr'] + lo, 'extras', '%s [..%d] (%s)' % (s['name'], lo, hdr))
            add(s['addr'] + lo, s['addr'] + min(hi, s['size']), 'base', '%s (%s)' % (s['name'], hdr) if part is None else '%s [%d..%d] (%s)' % (s['name'], lo, hi, hdr))
            add(s['addr'] + min(hi, s['size']), s['addr'] + s['size'], 'extras', '%s [%d..] (%s)' % (s['name'], hi, hdr))
    for addr, size, obj, sec in modelf.map_inputs(os.path.join(build, 'mod.map')):
        if obj == 'patches.o' and size: add(addr, addr + size, 'base', 'patches.o %s' % sec)
    holes.sort()
    for s, e, label in extras_strings(elf, secs, image, holes, relocs)[0]: holes.append((s, e, 'extras', label))
    holes.sort()
    for x, y in zip(holes, holes[1:]):
        assert x[1] <= y[0], 'holes overlap: %s / %s' % (x, y)
    assert any(h[3] == EXTRAS_FLAG for h in holes), 'no extras flag (mod/src/extras.c) in this build'
    return image, holes, relocs


STRING_TAG = '@str'


def extras_strings(elf, secs, image, holes, relocs):
    """(relclean 2026-11-19; the user's decision of 2026-11-19: the names the extras' tables point at go into the first
    start's holes too) The strings an extras table points at: a table in a game-data header with a string in it (ext_late's
    names, xarena_names' arena names) holds a POINTER, and the string itself is a literal the compiler put in .rodata.str*,
    outside every table -- so it shipped in mod_binary.holes. Each such string becomes an EXTRAS hole of its own, filled at
    the first start from the same header text as the table (hdrlayout.py: a STRING hole; exe ri_hdrfill.cpp).
    Only a string nothing else can read: the compiler's own named literal (an STT_OBJECT in a .rodata.str* section, exactly
    the string and its zero, no other symbol on any of its bytes, none of its bytes in another hole), and every relocation
    in the whole program that refers to it is a 32-bit data word (R_MIPS_32) holding its address, inside the extras holes of
    ONE table -- no code reaches it (no HI16 / LO16 / 26 field refers to it), no other table, and no relocation goes through
    its section's own symbol (an offset into the section could hide a reference). A string shared with anything else (the
    linker merges equal literals: "" is code's too) stays where it is.
    -> ([(start, end, label)], [(start, why) of the strings an extras word points at that stay]); label
    "<table>@str<k> (<header>)": the table's k-th such string by address."""
    import bisect
    B = modelf.BASE
    syms = modelf.symbols_by_index(elf, secs)
    ents = modelf.reloc_entries(elf, secs)
    extras = sorted((s, e, label) for s, e, cls, label in holes if cls == 'extras')
    x_starts = [h[0] for h in extras]
    def extras_hole(off):
        k = bisect.bisect_right(x_starts, off) - 1
        return extras[k] if k >= 0 and off < extras[k][1] else None
    all_holes = sorted((s, e) for s, e, cls, label in holes)
    a_starts = [h[0] for h in all_holes]
    def in_a_hole(s, e):
        k = bisect.bisect_right(a_starts, e - 1) - 1
        return k >= 0 and all_holes[k][1] > s
    refs = {}
    sec_sym_secs = set()
    for addr, typ, si in ents:
        if si == 0 or si >= len(syms): continue
        refs.setdefault(si, []).append((addr - B, typ))
        if syms[si]['type'] == 3: sec_sym_secs.add(syms[si]['sec'])     # (STT_SECTION: section + an offset in place)
    objs = sorted((y['addr'] - B, y['addr'] - B + y['size'], i) for i, y in enumerate(syms)
                  if y['sec'] is not None and y['size'] and y['type'] not in (3, 4) and secs[y['sec']]['flags'] & 2)
    o_starts = [o[0] for o in objs]
    def owners(s, e):
        k = bisect.bisect_left(o_starts, e)
        return [o for o in objs[:k] if o[1] > s]
    # the strings the extras' words point at (the ones that can move, and the ones that stay, with the reason)
    pointed = {}
    for s, e, label in extras:
        for a in range(s + (-s % 4), e - 3, 4):
            if B + a not in relocs: continue
            v = struct.unpack_from('>I', image, a)[0] - B
            if 0 <= v < len(image) and not in_a_hole(v, v + 1): pointed.setdefault(v, set())
    found, stay = [], []
    for t in sorted(pointed):
        own = [o for o in owners(t, t + 1)]
        if len(own) != 1 or own[0][0] != t:
            stay.append((t, 'not one named literal of its own (%d symbols on it)' % len(own))); continue
        s, e, si = own[0]
        y = syms[si]; sec = secs[y['sec']]
        if y['type'] != 1 or not sec['name'].startswith('.rodata.str'): stay.append((t, 'not a string literal')); continue
        if e - s < 2 or image[e - 1] != 0 or 0 in image[s:e - 1]: stay.append((t, 'not one string and its zero')); continue
        if len(owners(s, e)) != 1: stay.append((t, 'another symbol on its bytes')); continue
        if in_a_hole(s, e): stay.append((t, 'in another hole')); continue
        if y['sec'] in sec_sym_secs: stay.append((t, 'its section is referred to by offset')); continue
        r = refs.get(si, [])
        tables = {(extras_hole(a) or (None, None, None))[2] for a, typ in r}
        if not r or any(typ != 2 for a, typ in r): stay.append((t, 'code refers to it')); continue
        if None in tables or len(tables) != 1: stay.append((t, 'not one extras table\'s alone')); continue
        if any(struct.unpack_from('>I', image, a)[0] != B + s for a, typ in r): stay.append((t, 'a word into it, not at it')); continue
        label = tables.pop()
        m = re.search(r'\(([\w.]+\.[ch])\)$', label)
        if not m: stay.append((t, 'its table has no header')); continue
        found.append((s, e, label.split(' ')[0], m.group(1)))
    out, k_of = [], {}
    for s, e, table, header in found:
        k = k_of[table] = k_of.get(table, -1) + 1
        out.append((s, e, '%s%s%d (%s)' % (table, STRING_TAG, k, header)))
    return out, stay


def extras_link_words(holes, relocs):
    """(image offset) of every relocated word inside an extras hole: an address the link chose (ours), filled in the
    base too -- a pointer in an extras table must stay a pointer into the mod even with the table's data left out (the
    loader relocates it either way: left zero it would become the load offset, a pointer to nothing; proof 2 found
    commentary_frame reading such a name pointer at the first frame, 2026-11-18)"""
    out = []
    for s, e, cls, label in holes:
        if cls != 'extras': continue
        for a in range(s + (-s % 4), e - 3, 4):
            if modelf.BASE + a in relocs: out.append(a)
    return out


def base_image(build):
    """the mod's image as the first start makes it without the built-in extras: their holes zero (the base ones as built)"""
    image, holes, relocs = find_holes(build)
    b = bytearray(image)
    for s, e, cls, label in holes:
        if cls == 'extras': b[s:e] = bytes(e - s)
    for a in extras_link_words(holes, relocs): b[a:a + 4] = image[a:a + 4]
    return bytes(b)


def main2(a, work, image, holes, relocs, holed, base_img):

    # ---- the pool, from the ROM alone
    rom = rom_order(open(a.rom, 'rb').read())
    assert hashlib.sha1(rom).hexdigest() == REV1_SHA1, 'not WWF No Mercy (USA) (Rev 1)'
    print('pool: the ROM, the six-man ROM, the ROM\'s files unpacked ...', flush=True)
    files, spans = all_files(rom, work)
    pool = Pool(rom, files, spans)
    pool.file_starts = np.array([s for s, n in spans], np.int64)

    # ---- the recipes of the base holes
    ops = []        # ('copy', dst, src, off, len) | ('lit', dst, bytes)
    report, problems = [], []
    tot = {'copied': 0, 'link': 0, 'ours': 0}
    # the part tables' game entries: where the game has each table and how many (parts_ext.h: "the game's 0x... (n)")
    game_tables = {}
    hdr = open(os.path.join(a.headers or os.path.join(SRC, 'mod', 'src'), 'parts_ext.h'), encoding='latin-1').read()
    for m in re.finditer(r"// the game's 0x([0-9A-Fa-f]+) \((\d+)\), then the new\n[^\n]*?(\w+)\[", hdr):
        game_tables[m.group(3)] = (int(m.group(1), 16), int(m.group(2)))
    ports = []      # (start, len, port name)
    for s, e, cls, label in holes:
        if cls != 'base': continue
        sym = label.split(' ')[0]
        port = next((name for r, name in PORT_RE if r.match(sym)), None)
        if port:
            ports.append((s, e - s, port, sym))
            report.append('%-6s %-46s %7d bytes: made by code from the ROM (arcade_firstrun.cpp port %s)' % (cls, label, e - s, port))
            continue
        data = image[s:e]; p = 0; hint = None
        # (a table that is one piece of one file, as it is: one copy -- found whole, zeros and all)
        if len(data) >= 64 and not any(((modelf.BASE + s + q) & ~3) in relocs for q in range(0, len(data), 4)):
            at = pool.files.find(data)
            if at >= 0:
                k = int(np.searchsorted(pool.file_starts, at, side='right')) - 1
                if at + len(data) <= spans[k][0] + spans[k][1]:
                    ops.append(('copy', s, 2 + k, at - spans[k][0], len(data)))
                    report.append('%-6s %-46s %7d bytes: %7d from the ROM (file %d as it is)' % (cls, label, e - s, len(data), k + 1))
                    tot['copied'] += len(data)
                    continue
        copied = link = ours = 0; lit = bytearray(); lit_at = None; ours_bytes = 0
        # (a part table: the game's own entries copied from where the game has its table -- its main segment in the ROM,
        #  vram 0x80000400 at ROM 0x1000 -- whatever they look like (0 / 1 flags are no less the game's); checked equal)
        if sym in PART_TABLES and sym in game_tables:
            vram, count = game_tables[sym]
            n = min(count * PART_TABLES[sym][0], len(data)); ro = vram - 0x80000400 + 0x1000
            assert rom[ro:ro + n] == data[:n], '%s: the game\'s entries are not its table in the ROM' % sym
            ops.append(("copy", s, 0, ro, n)); copied += n; p = n; hint = ro + n      # (the ROM is the pool's first part)
        def flush():
            nonlocal lit, lit_at
            if lit: ops.append(('lit', lit_at, bytes(lit))); lit = bytearray(); lit_at = None
        while p < len(data):
            addr = modelf.BASE + s + p
            w0 = (addr - (addr & 3))
            in_reloc = w0 in relocs
            if not in_reloc and data[p] == 0:
                z = 0
                while p + z < len(data) and data[p + z] == 0 and not ((modelf.BASE + s + p + z) & ~3) in relocs: z += 1
                if z >= MIN_FILL:
                    flush(); ops.append(('zero', s + p, z)); p += z; hint = (hint + z) if hint is not None else None
                    continue
            m = None if in_reloc else pool.best(data, p, hint)
            if m and m[1] >= 4:
                g, n = m
                # (a copy stops short of the next relocated word: the link's bytes are written as they are)
                q = 0
                while q < n:
                    a2 = modelf.BASE + s + p + q
                    if (a2 & ~3) in relocs and q > 0: break
                    q += 1
                n = q
                flush()
                src, off = pool.where(g)
                ops.append(('copy', s + p, src, off, n))
                copied += n; p += n; hint = g + n
                continue
            if lit_at is None: lit_at = s + p
            lit.append(data[p])
            if in_reloc: link += 1
            elif data[p]: ours += 1
            else: link += 0
            if not in_reloc and data[p]: ours_bytes += 1
            p += 1; hint = (hint + 1) if hint is not None else None
        flush()
        sym = label.split(' ')[0] if not label.startswith('patches.o') else label.split(' ')[1]
        why = next((w for r, w in OURS_RE if r.match(sym)), None)
        report.append('%-6s %-46s %7d bytes: %7d from the ROM, %6d the link\'s, %6d ours%s' % (
            cls, label, e - s, copied, link, ours, ('  -- ' + why) if ours and why else ''))
        tot['copied'] += copied; tot['link'] += link; tot['ours'] += ours
        if ours and not why:
            problems.append('%s: %d bytes no copy of the ROM gives that are not the link\'s -- game data to make by code' % (label, ours))
    for s, e, cls, label in holes:
        if cls == 'extras': report.append('extras %-46s %7d bytes (content\\modextra.bin)' % (label, e - s))
    # the link's words inside the extras' tables, filled in the base too (extras_link_words)
    xl = extras_link_words(holes, relocs)
    for w in xl: ops.append(('lit', w, image[w:w + 4]))
    tot['link'] += 4 * len(xl)
    report.append('base   the link\'s words in the extras\' tables: %d words (pointers into the mod)' % len(xl))

    # ---- apply the plan to the pool as the first start will (files unpacked one by one, as it does) and check
    made = bytearray(holed)
    cache = {}
    six = six_rom(rom)
    for s, n, port, name in ports: made[s:s + n] = image[s:s + n]     # (here: the build's; the exe makes them -- its check is the SHA-256)
    for op in ops:
        if op[0] == 'lit': made[op[1]:op[1] + len(op[2])] = op[2]; continue
        if op[0] == 'zero': made[op[1]:op[1] + op[2]] = bytes(op[2]); continue
        _, dst, src, off, n = op
        if src == 0: b = rom
        elif src == 1: b = six
        else:
            if src not in cache: cache[src] = aki_file(rom, src - 2 + 1)
            b = cache[src]
        made[dst:dst + n] = b[off:off + n]
    ok_base = bytes(made) == bytes(base_img)
    print('plan applied to the ROM alone: %s (%d ops, %d files unpacked)' % ('the base image, byte for byte' if ok_base else 'DIFFERENT', len(ops), len(cache)))

    # ---- the extras
    extras = [(s, e) for s, e, cls, label in holes if cls == 'extras']
    packs = []
    for name in CONTENT_PACKS:
        p = os.path.join(a.content, name)
        packs.append((name, sha256(open(p, 'rb').read()) if os.path.exists(p) else bytes(32), os.path.getsize(p) if os.path.exists(p) else 0))
    full = bytearray(made)
    for s, e in extras: full[s:e] = image[s:e]
    ok_full = bytes(full) == image

    # ---- write
    fr = os.path.join(a.out, 'firstrun')
    open(os.path.join(fr, 'mod_binary.holes'), 'wb').write(holed)
    for m in ('mod.json', 'mod_syms.bin'):
        open(os.path.join(fr, m), 'wb').write(open(os.path.join(a.build, m), 'rb').read())
    P = bytearray(b'UNMPLAN1')
    P += struct.pack('<III', 1, len(image), len(rom))
    P += sha256(rom) + sha256(bytes(holed)) + sha256(bytes(base_img)) + sha256(image)
    P += struct.pack('<I', len(packs))
    for name, h, size in packs: P += name.encode().ljust(32, b'\0') + h + struct.pack('<Q', size)
    P += struct.pack('<I', len(holes))
    hole_name = lambda label: label.split(' ')[1] if label.startswith('patches.o') else label.split(' ')[0]
    for s, e, cls, label in holes: P += struct.pack('<IIB3x', s, e - s, 0 if cls == 'base' else 1) + hole_name(label).encode()[:31].ljust(32, bytes(1))
    P += struct.pack('<I', len(ports))
    for s, n, port, name in ports: P += struct.pack('<II', s, n) + port.encode().ljust(16, bytes(1)) + name.encode().ljust(32, bytes(1))
    P += struct.pack('<I', len(ops))
    for op in ops:
        if op[0] == 'copy': P += struct.pack('<BIIII', 1, op[1], op[2], op[3], op[4])
        elif op[0] == 'zero': P += struct.pack('<BII', 3, op[1], op[2])
        else: P += struct.pack('<BII', 2, op[1], len(op[2])) + op[2]
    open(os.path.join(fr, 'plan.bin'), 'wb').write(P)
    payload = b''.join(struct.pack('<II', s, e - s) + image[s:e] for s, e in extras)
    X = bytearray(b'UNMXTRA1') + sha256(payload) + struct.pack('<I', len(extras)) + payload   # (the payload's own SHA-256)
    open(os.path.join(a.out, 'modextra.bin'), 'wb').write(X)
    nb = sum(e - s for s, e, c, l in holes if c == 'base'); nx = sum(e - s for s, e in extras)
    head = ['mkplan: %s' % a.build,
            'holes: %d (%d bytes): base %d bytes -- from the ROM %d, the link\'s %d, ours %d; extras %d bytes' % (
                len(holes), nb + nx, nb, tot['copied'], tot['link'], tot['ours'], nx),
            'plan: %d ops, %d bytes; modextra.bin %d bytes' % (len(ops), len(P), len(X)),
            'image SHA-256: holed %s  base %s  full %s' % (sha256(bytes(holed)).hex()[:16], sha256(bytes(base_img)).hex()[:16], sha256(image).hex()[:16]),
            'check: base %s, full %s' % ('SAME' if ok_base else 'DIFFERENT', 'SAME' if ok_full else 'DIFFERENT'), '']
    open(os.path.join(a.out, 'plan_report.txt'), 'w', encoding='utf-8').write('\n'.join(head + problems + [''] + report) + '\n')
    print('\n'.join(head))
    for p in problems: print('NOT OURS:', p)
    # ---- the extras holes' layouts from the headers' text (hdrlayout.py): firstrun/hdrfill.bin, checked against the build
    import hdrlayout
    try:
        ok_text, lines = hdrlayout.make(a.build, a.headers, fr)
    except hdrlayout.Stop as x:
        ok_text, lines = False, ['hdrlayout: STOP -- %s' % x]
        print(lines[0])
    open(os.path.join(a.out, 'hdrfill_report.txt'), 'w', encoding='utf-8').write(''.join(x + chr(10) for x in lines))
    sys.exit(0 if ok_base and ok_full and not problems and ok_text else 1)


if __name__ == '__main__':
    main()
