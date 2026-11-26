"""pages_dump.py <run> [--json out.json]: the Superstar Select's pages as the game had them at the end of a QA run
(roster.c roster_pages_tab / roster_pages_n / roster_pages_flags, from state.rdram.bin, where logic.c says in QA_STATE + 0x3E0):
printed, or written as a select_pages.json (names from the story roster where a name is known, else "0x...")."""
import json, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.dirname(HERE)

def names():
    """ID -> the story roster's name for him (the longest that's plain text)"""
    r = json.load(open(os.path.join(DEV, "mod", "build", "stories", "_roster.json"), encoding="utf8"))
    out = {}
    for k, v in r.items():
        if not re.fullmatch(r"[A-Z0-9 .'&!/-]+", k): continue
        if v not in out or len(k) > len(out[v]): out[v] = k
    return out

def read(run):
    b = open(os.path.join(run, "state.rdram.bin"), "rb").read()
    # (the dump: the game's 8 MB, then the mods' memory from 0x81000000)
    off = lambda a: a - 0x81000000 + 0x800000 if a >= 0x81000000 else a - 0x80000000
    u8 = lambda a: b[off(a) ^ 3]
    u16 = lambda a: struct.unpack_from("<H", b, off(a) ^ 2)[0]
    u32 = lambda a: struct.unpack_from("<I", b, off(a))[0]
    # (the mod's memory is placed when it's loaded: logic.c puts the count and the tables' places in QA_STATE)
    s = {"roster_pages_tab": u32(0x807F33E4), "roster_pages_flags": u32(0x807F33E8)}
    n = u32(0x807F33E0)
    pages = []
    for p in range(min(n, 32)):
        pg = s["roster_pages_tab"] + 0x38 * p
        c = u16(pg + 0x24)
        nm = bytes(u8(pg + 0x26 + i) for i in range(0x12)).split(b"\0")[0].decode("latin1")
        pages.append(dict(name=nm, flags=u8(s["roster_pages_flags"] + p),
                          ids=[u16(pg + 4 * i) for i in range(min(c, 9))], ords=[u16(pg + 4 * i + 2) for i in range(min(c, 9))], count=c))
    return n, pages

if __name__ == "__main__":
    run = sys.argv[1]
    n, pages = read(run)
    if "--json" in sys.argv:
        N = names()
        out = {"about": "", "unlisted": "append", "pages": []}
        for p in pages:
            e = {"name": p["name"]}
            if p["flags"] & 1: e["rumble"] = False
            e["wrestlers"] = [N.get(i, "0x%04X" % i) for i in p["ids"]]
            out["pages"].append(e)
        json.dump(out, open(sys.argv[sys.argv.index("--json") + 1], "w"), indent=2)
    print("pages:", n)
    for i, p in enumerate(pages):
        print("%2d %-16s %s count %d: %s" % (i, p["name"], "no-random" if p["flags"] & 1 else "         ", p["count"],
                                              " ".join("%03X" % x for x in p["ids"])))
