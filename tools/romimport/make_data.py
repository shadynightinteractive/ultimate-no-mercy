"""make_data.py (romimport, 2026-11-18): the generators' intermediate files, made from the ROMs alone, into a ROM-only data
folder at the paths the generators read (nmpaths.py). Each replaces a capture or a pickle a lost producer made:

  nmport\\names.pkl              (vnames, nnames): move number -> move name, No Mercy's / No Mercy Plus V3's -- the keys and
                                their order frozen (names_index.json), every text read from the game's own files
  extracted\\wm2k_work\\hashes.pkl (nm_h, wm_h): file -> (md5, length, first 4 bytes), every No Mercy / WM2000 file
  gen_data\\<capture>.bin        RAM images from the ROMs by a load map (images.py; loadmaps\\<name>.json)
  gen_data\\wmport_videos.pkl    the four ported Superstars' Titantron video numbers -- our numbers (gen/wmport.py gives
                                them in this order: frozen\\wmport_videos.json), until wmport.py runs ROM-only itself

    python make_data.py <data folder> [--check <the project's data folder>]"""
import hashlib, json, os, pickle, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from roms import Roms
import images

# (name in loadmaps\, where it goes in the data folder)
IMAGES = [("van_sel", "gen_data/van_sel_ram.bin"), ("wm2k_ram", "gen_data/wm2k_ram.bin"), ("nm_pick", "gen_data/nm_pick_ram.bin"),
          ("rev_bm", "extracted/rev_bm_ram.bin"), ("wm_match", "wmport/wm/match_ram.bin"), ("wm2k_entr", "gen_data/wm2k_entr_ram.bin"),
          ("wm_sel1", "handlers_rig/wm2000/cap/sel1_ram.bin"), ("bb_vsel", "handlers_rig/imports2/cap/vsel_ram.bin"),
          ("rev_boot", "revport/attires/cap/v_boot_ram.bin")]
# rev_bm: Revenge in a match -- revanims / revlib read its code-to-list table 0x80030570 (main segment).
# rev_arena: one Revenge match map for all six arena captures -- gen/arenas*.py read only main-segment tables there, the same in
# all six (checked against each capture).
# rev_ent0: Revenge at an entrance close-up -- datarenaport\stage3d\extract.py --ram reads its scene table (scenes.json: equal)
IMAGES += [("rev_ent0", "arenaport/rev/cap/ent0_ram.bin")]
# wm_close1 / wm_fly1: WrestleMania 2000 at an entrance close-up / fly-by -- data\wmarena\wm3d.py --ram (its scene table:
# wm3d_scenes.json) and allenues_json.py (its venue lists: wm_venues.json) read them
IMAGES += [("wm_close1", "wmarena/rig/cap/close1_ram.bin"), ("wm_fly1", "wmarena/rig/cap/fly1_ram.bin")]
IMAGES += [("rev_arena", "arenaport/rev/cap/m%d_ram.bin" % v) for v in range(6)]

def text_at(F, f, o):
    d = F(f); return d[o:d.index(b"\0", o)].decode("latin1")

def names(R):
    ix = json.load(open(os.path.join(HERE, "names_index.json")))
    out = []
    for game in ("nomercy", "nmplus_v3"):
        F = R.files(game)
        out.append({k: (x if f is None else text_at(F, f, x)) for k, f, x in ix[game]})
    return tuple(out)

def hashes(R):
    out = []
    for game in ("nomercy", "wm2000"):
        F = R.files(game); h = {}
        for f in range(1, F.count() + 1):
            d = F(f)
            if d is not None: h[f] = (hashlib.md5(d).hexdigest(), len(d), d[:4])
        out.append(h)
    return tuple(out)

def van_sel_extra(img, R):
    """No Mercy at its Superstar Select keeps every ordinal's moveset in RAM -- its moves file (0xB8 bytes) and abilities
    file (0x20) one after another, 0xD8 an ordinal, from the heap address the word at 0x80047E94 holds (0x803208D0 in the
    rig's session) -- which gen/mkroster.py reads (van_sets). Put there from the ROM's files, as the game's loader does:
    the ordinal's first costume struct names the two files (nomercy.game.costume_struct)."""
    import struct
    rom, F = R.rom("nomercy"), R.files("nomercy")
    HEAP = 0x803208D0
    struct.pack_into(">I", img, 0x47E94, HEAP)
    for o in range(84):
        p = struct.unpack_from(">I", rom, 0x47124 - 0x400 + 0x1000 + 16 * o)[0]
        cs = rom[p - 0x80000400 + 0x1000:p - 0x80000400 + 0x1000 + 0x14]
        mf, af = struct.unpack_from(">HH", cs, 10)
        blk = (F(mf)[:0xB8] + F(af)[:0x20]) if mf else bytes(0xD8)
        a = (HEAP + o * 0xD8) & 0x7FFFFF; img[a:a + 0xD8] = blk
    van_sel_records(img, R)

def van_sel_records(img, R):
    """...and every Superstar's four costume records (0x4E bytes each, ordinal-major, from the heap address the word at
    0x80047E90 holds: 0x8031A260 in the rig's session), which data\\revport\\nmsets.py reads the names from: built by the
    game's own func_8000B834(id, costume, record) run on the ROM (as tools\\handlers\\p0_nomercy.py does, which checks them
    against the capture: bytes 0x00-0x4C; 0x4D the builder never writes, left 0). Ordinals 1-64; the rest stay zero (the
    Create-a-Wrestler slots are empty at the Select)."""
    import struct
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "handlers"))
    from handlers.nomercy import game as G
    class NM: pass
    nm = NM(); nm.rom = R.rom("nomercy"); F = R.files("nomercy")
    E = G.Code(nm).E
    FILES_AT = 0x80600000
    E.wbytes(FILES_AT, F(1)); E.wbytes(FILES_AT + 0x10000, F(2))
    E.w32(0x80047EA0, FILES_AT); E.w32(0x80047EA4, FILES_AT + 0x10000)
    def read_file(e):                                  # func_80003C94(info, file, dest)
        e.wbytes(e.r[6], F(e.r[5] & 0xFFFF)); return True
    E.hooks[0x80003C94] = read_file
    RECS, REC = 0x8031A260, 0x80790000
    struct.pack_into(">I", img, 0x47E90, RECS)
    for o in G.STOCK_ORDINALS:
        p = struct.unpack_from(">I", nm.rom, G.ROSTER_TABLE - G.MAIN_VRAM + G.MAIN_ROM + 16 * o)[0]
        wid = struct.unpack_from(">H", nm.rom, p - G.MAIN_VRAM + G.MAIN_ROM)[0]
        for c in range(4):
            E.wbytes(REC, bytes(G.REC_SIZE)); E.call(0x8000B834, [wid, c, REC])
            a = (RECS + (o * 4 + c) * G.REC_SIZE) & 0x7FFFFF
            img[a:a + 0x4D] = E.rbytes(REC, 0x4D)

def wm2k_ram_extra(img, R):
    """WrestleMania 2000's music bank, as its sound start-up leaves it (banks.py), and the word that points at it"""
    import banks, struct
    B, C = banks.place(img, R.rom("wm2000"), "wm2000")
    struct.pack_into(">I", img, 0x571EC, B)

def bank_extra(game):
    """a select screen's music bank, as its sound start-up leaves it (banks.py), and the word that points at it (0x800571EC,
    the same in WrestleMania 2000 and Badd Blood) -- the importers' themes (handlers baddblood theme / theme_folder)"""
    def f(img, R):
        import banks
        B, C = banks.place(img, R.rom(game), game)
        struct.pack_into(">I", img, 0x571EC, B)
    return f

def rev_boot_extra(img, R):
    """WCW/nWo Revenge's costume table as its boot leaves it (handlers revenge game.roster reads it: *0x8003A594, 42 bytes an
    entry, 4 a Superstar): the heap block (0x3480 bytes at 0x803ACFB0 in the rig's session) filled by the game's own
    0x8002BAB8 (run here on the ROM image), then what its save load (overlay code at ROM 0x66C24, ordinals 0-63 through
    0x8002B790: entry 0 and entries 4-255) writes back from the rig's save -- the game's own defaults, so every byte stays
    as built but the first colour row (+38), which the load sets to 0. Checked: the whole table = the capture's."""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "rig", "harness"))
    from mipsemu import Emu
    T, N = 0x803ACFB0, 0x3480
    E = Emu(bytes(img)); E.w32(0x8003A594, T); E.wbytes(T, bytes(N))
    E.call(0x8002BAB8, [], max_steps=50_000_000)
    tab = bytearray(E.rbytes(T, N))
    for i in [0] + list(range(4, 256)): tab[42 * i + 38] = 0
    a = T & 0x7FFFFF; img[a:a + N] = tab
    struct.pack_into(">I", img, 0x3A594, T)

EXTRA = {"rev_boot": rev_boot_extra, "van_sel": van_sel_extra, "wm2k_ram": wm2k_ram_extra, "wm_sel1": bank_extra("wm2000"), "bb_vsel": bank_extra("baddblood")}

# (2026-11-18) One value of No Mercy Plus V3's bank as the shipped content index has it, which no ROM has: instrument 43's
# sample length 0x3BDF9 (the ROM, and the rig's own NM+ capture, say 0x3BDFA). It came from the original nmbank.pkl, made in
# a session that is lost; data\revport\ports\nmbank.pkl was set to it by hand to rebuild the shipped index. Kept as OUR
# override so the ROM-only build plays exactly what the shipped game plays (its pack bytes are the same either way:
# padding). Words: [address, value].
NMBANK_OVERRIDE = [[0x807C254C, 0x0003BDF9]]   # (wave record +4: the length)

def main():
    out = sys.argv[1]
    ref = sys.argv[sys.argv.index("--check") + 1] if "--check" in sys.argv else None
    R = Roms()
    def put(rel, b, how):
        p = os.path.join(out, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(b)
        msg = ""
        if ref and how == "image": msg = "(made from the ROM; the capture also holds run-time state)"
        elif ref:
            q = os.path.join(ref, rel)
            if how == "bytes": msg = "SAME" if os.path.exists(q) and open(q, "rb").read() == b else "DIFFERENT"
            else:
                a, c = pickle.loads(b), pickle.load(open(q, "rb"))
                same = a == c and all(list(x) == list(y) for x, y in zip(a, c) if isinstance(x, dict))
                msg = "SAME (values and key order)" if same else ("SAME values, other order" if a == c else "DIFFERENT")
        print("%-34s %9d bytes  %s" % (rel, len(b), msg))
    put("nmport/names.pkl", pickle.dumps(names(R)), "pickle")
    put("extracted/wm2k_work/hashes.pkl", pickle.dumps(hashes(R)), "pickle")
    v = json.load(open(os.path.join(HERE, "frozen", "wmport_videos.json")))
    put("gen_data/wmport_videos.pkl", pickle.dumps(v), "pickle")
    v = json.load(open(os.path.join(HERE, "frozen", "model_file_fields.json")))["fields"]
    put("gen_data/model_file_fields.pkl", pickle.dumps(v), "bytes")
    nm_img = None
    for name, rel in IMAGES:
        mp = os.path.join(HERE, "loadmaps", name + ".json")
        if not os.path.exists(mp): print("%-34s no load map yet" % rel); continue
        img = bytearray(images.build(json.load(open(mp)), R))
        if name in EXTRA: EXTRA[name](img, R)
        put(rel, bytes(img), "image")
        if name == "nm_pick": nm_img = img
    # revport\nm_feats.pkl / rev_feats.pkl: every animation file's anim.feats() over a frozen range (frozen\anim_feats.json)
    import numpy as np
    sys.path.insert(0, os.path.join(out, "revport")); import anim
    fz = json.load(open(os.path.join(HERE, "frozen", "anim_feats.json")))
    for name in ("nm_feats", "rev_feats"):
        game, lo, hi = fz[name]; F = R.files(game)
        d = {f: anim.feats(F(f)) for f in range(lo, hi + 1)}
        rel = "revport/%s.pkl" % name; b = pickle.dumps(d)
        p = os.path.join(out, rel); open(p, "wb").write(b); msg = ""
        if ref:
            c = pickle.load(open(os.path.join(ref, rel), "rb"))
            same = list(c) == list(d) and all(c[k][1] == d[k][1] and c[k][0].dtype == d[k][0].dtype and np.array_equal(c[k][0], d[k][0]) for k in c)
            msg = "SAME (values and key order)" if same else "DIFFERENT"
        print("%-34s %9d bytes  %s" % (rel, len(b), msg))
    # revport\rev_movesets.pkl (tools\handlers revenge, revmoves): Revenge's moveset files by (group, row) -- the game's
    # table of group arrays (u16 file numbers, each array 4-byte aligned, its padding a 0) whose pointers are at ROM 0x39664
    # (0-terminated; RAM = ROM + 0x7FFFF400); a group runs to the next one's start (the last to the pointer table); then
    # each row's list (file f + 2: 763 u16)
    rv, RF = R.rom("revenge"), R.files("revenge")
    PT = 0x39664; ptrs = []
    while struct.unpack_from(">I", rv, PT + 4 * len(ptrs))[0]: ptrs.append(struct.unpack_from(">I", rv, PT + 4 * len(ptrs))[0] - 0x7FFFF400)
    rows = []
    for g, s in enumerate(ptrs):
        e = ptrs[g + 1] if g + 1 < len(ptrs) else PT
        fs = list(struct.unpack_from(">%dH" % ((e - s) // 2), rv, s))
        while fs and fs[-1] == 0: fs.pop()
        rows += [(g, r, f) for r, f in enumerate(fs)]
    mv = (rows, {(g, r): struct.unpack(">763H", RF(f + 2)) for g, r, f in rows})
    rel = "revport/rev_movesets.pkl"; b = pickle.dumps(mv); open(os.path.join(out, rel), "wb").write(b); msg = ""
    if ref:
        c = pickle.load(open(os.path.join(ref, rel), "rb"))
        msg = "SAME (values and key order)" if c[0] == mv[0] and c[1] == mv[1] and list(c[1]) == list(mv[1]) else "DIFFERENT"
    print("%-34s %9d bytes  %s" % (rel, len(b), msg))
    # revport\nm_sets.pkl: No Mercy's movesets per ordinal -- our script data\revport\nmsets.py, on the ROM-made van_sel image
    import subprocess
    subprocess.run([sys.executable, os.path.join(out, "revport", "nmsets.py")], check=True, stdout=subprocess.DEVNULL)
    rel = "revport/nm_sets.pkl"
    if ref:
        a, c = pickle.load(open(os.path.join(out, rel), "rb")), pickle.load(open(os.path.join(ref, rel), "rb"))
        print("%-34s %9d bytes  %s" % (rel, os.path.getsize(os.path.join(out, rel)), "SAME (values and key order)" if a == c and list(a) == list(c) else "DIFFERENT"))
    if nm_img is not None:
        # scratch\tmp\nmbank.pkl (gen/mkcontent.py): No Mercy Plus V3's RAM with its music bank (banks.py) -- B and C
        import banks
        img = bytearray(nm_img); B, C = banks.place(img, R.rom("nmplus_v3"), "nmplus_v3")
        for a, v in NMBANK_OVERRIDE: struct.pack_into(">I", img, a & 0x7FFFFF, v)
        b = pickle.dumps({"ram": bytes(img), "B": B, "C": C})
        p = os.path.join(out, "..", "scratch", "tmp", "nmbank.pkl"); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, "wb").write(b); print("%-34s %9d bytes  (No Mercy Plus V3's bank from its ROM)" % ("scratch/tmp/nmbank.pkl", len(b)))

if __name__ == "__main__":
    main()
