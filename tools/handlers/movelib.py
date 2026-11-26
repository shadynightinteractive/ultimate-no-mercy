#!/usr/bin/env python3
"""movelib.py (2026-11-13, movelib): the Revenge handler's built-in move library, written from what gen/revanims.py made of
WCW/nWo Revenge's ROM (data\\gen_data\\rev_anims.json: the ported Superstars' moves played with Revenge's own animations) and
the pack those animations are in (mod build\\wmport.bin, rev_move_anims.h's numbers).

Why: movex.c used to find a port's moves by his place in ext_late (MoveX.late), so only the 37 built-in ports could have a
move of Revenge's, and no folder wrestler could name one. Now a move of another game is a library move with an ID
("revenge:126c_1270"), the same for whoever has it: a wrestler's moveset names it in a slot (his moves.json, or for a
built-in port the list below), and movex.c plays it from the ID.

A Revenge library move: Revenge's animation pair for it (the attacker's and the defender's files, by Revenge's numbers: the ID
is "revenge:<attacker>_<defender>" in hex) as the pack has them, and -- for a move that goes on (a hold's grab -> the hold) --
the block of the hold it goes on to (the loop's pair and the three endings', 8 files in a row). The No Mercy move it is played
on (its record: damage, timing, where it's done) is the slot's: the ID says it after an "@" ("revenge:126c_1270@940"), else
the library's usual one. The same animation brought over for several ports is one file pair in the pack many times over (the
pack keeps a copy per port: gen/wmport.py); the library takes the first copy, and this script checks every copy is the same
bytes, header and record (so a port plays the same animation whichever copy it is).

    python movelib.py <mod folder> <exe src\\main folder>
writes <mod>\\src\\move_lib.h (the library and the built-in ports' lists, for movex.c; it replaces movex_data.h) and
<exe>\\move_lib_ids.h (the IDs, for arcade_wrestlers.cpp's move_library), and prints a check of every row against
movex_data.h's (each port: entry, animation bytes, continuation bytes, the move in its place)."""
import collections, json, os, re, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from handlers import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
REV_ANIMS = os.path.join(DEV, "data", "gen_data", "rev_anims.json")
NMF = os.path.join(DEV, "data", "extracted", "nmfiles", "f", "%04x.bin")
GOES_ON = 0x1E00

def nm_flags(m):
    """No Mercy's move m: its record's flags (0x1E00: it goes on to a continuation)"""
    rix = open(NMF % 0x1F1, "rb").read(); recs = open(NMF % 0x1EF, "rb").read()
    ri = struct.unpack_from(">h", rix, 2 * m)[0]
    return struct.unpack_from(">I", recs, 36 * ri + 0x20)[0]

# Moves made by hand (movex.c's movex[] rows until today), as library moves: Bret Hart's and Sting's front special, Revenge's
# slam (code 58: No Mercy's 1880 byte for byte) going on into the Sharpshooter on the mat (code 200: their entry 157), by the
# game's own ground grapple (movex.c movex_chain_step). No animation of its own: `ground` is the entry the ground grapple at the
# legs picks.
HAND = [dict(id="revenge:slam_sharpshooter", file=0, then=0, ground=157, base=1880,
             about="Revenge's code 58 -> 200: the slam (No Mercy's 1880 byte for byte), then the ground grapple at his legs picks 157",
             ports={"brethart": [70], "sting": [70]})]

def main():
    mod, exe = sys.argv[1], sys.argv[2]
    j = json.load(open(REV_ANIMS))
    rma_h = open(os.path.join(mod, "src", "rev_move_anims.h")).read()
    rma = {m.group(1): int(m.group(2), 16) for m in re.finditer(r"#define RMA_(\w+) 0x([0-9A-F]+)u", rma_h)}
    rma_first = int(re.search(r"#define RMA_FIRST 0x([0-9A-F]+)u", rma_h).group(1), 16)
    wmp_h = open(os.path.join(mod, "src", "wmport_files.h")).read()
    wmp0 = int(re.search(r"#define WMP_FILE0 0x([0-9A-F]+)u", wmp_h).group(1), 16)
    wmp = [(int(a, 16), int(b)) for a, b in re.findall(r"\{ 0x([0-9A-F]+), (\d+) \}", wmp_h.split("wmp_files[WMP_FILES_N][2] = {")[1].split("};")[0])]
    pack = open(os.path.join(mod, "build", "wmport.bin"), "rb").read()
    F = {f["name"]: f for f in j["files"]}
    order = [f["name"] for f in j["files"]]                 # (the pack's order: rma_hdr / rma_rec's too)
    def content(name):
        """a pair's attacker and defender files as the game gets them: the bytes, header and record"""
        out = []
        for k in (0, 1):
            n = rma[name] + k
            o, sz = wmp[n - wmp0]
            out.append((pack[o:o + sz], F[name]["hdr"][k], F[name]["rec"][k]))
        return tuple(out)
    canon = {}                                              # content -> its first pair's name (lowest number)
    for name in sorted(order, key=lambda n: rma[n]): canon.setdefault(content(name), name)
    first = lambda name: canon[content(name)]
    copies = collections.Counter(first(n) for n in order)

    # library moves: (the pair, the block it goes on to or none)
    rows = j["rows"]
    # (a row with no block whose move ends -- the slot's move there doesn't go on -- plays the same with the pair's block as
    #  without it: movex.c plays a library move's block only on a move that goes on. So such a row takes the pair's library
    #  move with its block, and a pair is one library move unless a port's move there goes on without one.)
    block_of = {}
    for r in rows:
        if r.get("cont"): block_of.setdefault(first(r["name"]), first(r["cont"]))
    def key_of(r):
        if r.get("cont"): return (first(r["name"]), first(r["cont"]))
        played = r.get("carrier") or r["move"]
        if first(r["name"]) in block_of and not (nm_flags(played) & GOES_ON): return (first(r["name"]), block_of[first(r["name"])])
        return (first(r["name"]), None)
    lib, lib_key = [], {}
    for r in rows:
        key = key_of(r)
        if key not in lib_key:
            lib_key[key] = len(lib)
            f = F[key[0]]
            lib.append(dict(pair=key[0], cont=key[1], fa=f["fa"], fd=f["fd"], bases=collections.Counter(), ports=set()))
        m = lib[lib_key[key]]
        m["bases"][r.get("carrier") or r["move"]] += 1
        m["ports"].add(r["key"])
    # names: "revenge:<fa>_<fd>"; a pair that has two library moves (with its hold's block, and without it: a port whose move
    # there ends, or a block left out) -- the one with the block keeps the plain name, the other gets "~plain"
    by_pair = collections.defaultdict(list)
    for m in lib: by_pair[(m["fa"], m["fd"])].append(m)
    for (fa, fd), ms in by_pair.items():
        if len({m["cont"] for m in ms}) != len(ms): raise SystemExit("two library moves with one pair and one block")
        conts = sorted(ms, key=lambda m: (m["cont"] is None, m["cont"] or ""))
        for k, m in enumerate(conts):
            m["id"] = "revenge:%04x_%04x" % (fa, fd) + ("" if k == 0 else ("~plain" if m["cont"] is None else "~%d" % (k + 1)))
    for m in lib: m["base"] = m["bases"].most_common(1)[0][0]
    hand = [dict(h) for h in HAND]
    # IDs: the hand-made moves first, then the pairs in the pack's order (stable from one run to the next)
    lib.sort(key=lambda m: (rma[m["pair"]], m["cont"] is not None, rma[m["cont"]] if m["cont"] else 0))
    allm = hand + lib
    for i, m in enumerate(allm): m["num"] = i + 1
    num_of = {m["id"]: m["num"] for m in allm}

    # the built-in ports' lists: each port's rows, in gen/revanims.py's order (the QA cycles them in that order)
    late_of = {}
    for r in rows: late_of[r["key"]] = r["late"]
    LATE_IDX = {"brethart": 11, "sting": 19}                  # (the hand rows' ports: movex.c LATE_BRET, LATE_STING)
    port = collections.defaultdict(list)
    for h in hand:
        for key, entries in h["ports"].items():
            for e in entries: port[LATE_IDX[key]].append((e, h["num"], 0, "%s %s" % (key, h["id"])))
    for r in rows:
        key = key_of(r)
        num = [x for x in lib if (x["pair"], x["cont"]) == key][0]["num"]
        port[r["late"]].append((r["slot"], num, r.get("carrier", 0), "%s slot %d: Revenge code %d, on %d%s" % (
            r["key"], r["slot"], r["code"], r["move"], " (in its place %d)" % r["carrier"] if r.get("carrier") else "")))

    # the check: every row's entry, animation (bytes, header, record), continuation and move in its place as movex_data.h's
    # (gen/revanims.py's rows by ext_late index: data\gen_data\movex_data.h since 2026-11-13, before that mod src\)
    old_at = [q for q in (os.environ.get("MOVELIB_OLD", ""), os.path.join(DEV, "data", "gen_data", "movex_data.h"),
                          os.path.join(mod, "src", "movex_data.h")) if q and os.path.exists(q)]
    if not old_at: raise SystemExit("no movex_data.h to check against (gen/revanims.py writes it: data\\gen_data)")
    old = open(old_at[0]).read()
    print("checked against", old_at[0])
    rx = re.compile(r"\{ (\d+), (\d+), RMA_(\w+), 0, \{ 0 \}, \{ 0 \}, \{ (RMA_\w+|0) \}, (\d+) \},")
    old_rows = [(int(a), int(b), c, d[4:] if d != "0" else None, int(e)) for a, b, c, d, e in rx.findall(old)]
    new_rows = []
    by_num = {m["num"]: m for m in allm}
    for late in sorted(port):
        for e, num, carrier, _ in port[late]:
            m = by_num[num]
            if m["id"].startswith("revenge:slam"): continue
            new_rows.append((late, e, m["pair"], m["cont"], carrier))
    bad = ended = 0
    if len(old_rows) != len(new_rows): print("rows: %d before, %d now" % (len(old_rows), len(new_rows))); bad += 1
    for o, n in zip(sorted(old_rows, key=lambda x: (x[0], x[1])), sorted(new_rows, key=lambda x: (x[0], x[1]))):
        same = o[:2] == n[:2] and o[4] == n[4] and content(o[2]) == content(n[2]) and \
               ((o[3] is None and n[3] is None) or (o[3] and n[3] and content(o[3]) == content(n[3])))
        if not same:
            # (a row whose move ends but the library move has a block: the block is only played on a move that goes on --
            #  movex.c, as before: then the same)
            if o[:2] == n[:2] and o[4] == n[4] and content(o[2]) == content(n[2]) and o[3] is None and n[3] is not None:
                played = o[4] or [r for r in rows if (r["late"], r["slot"]) == o[:2]][0]["move"]
                if not (nm_flags(played) & GOES_ON):
                    ended += 1; continue          # (its move ends: the block isn't played, as before)
            bad += 1; print("  DIFFERENT", o, n)
    print("check: %d rows, %d different (each port's entry, its animation's bytes / header / record, its block's, the move in its place);"
          " %d of them with a block now on a move that ends (not played, as before)" % (len(old_rows), bad, ended))
    # (the QA's cycle, movex_qa_entry: each port's moves below entry 140 with an animation, each animation once, in order --
    #  by the pack's name before, by the library move's file now)
    qa_bad = 0
    for late in sorted(set(r[0] for r in old_rows)):
        seen, was = set(), []
        for r in [r for r in old_rows if r[0] == late and r[1] < 140]:
            if r[2] not in seen: seen.add(r[2]); was.append(r[1])
        seen, now = set(), []
        for e, num, carrier, _ in port[late]:
            m = by_num[num]
            if "pair" not in m or e >= 140: continue
            if m["pair"] not in seen: seen.add(m["pair"]); now.append(e)
        if was != now: qa_bad += 1; print("  QA cycle of late %d: %s before, %s now" % (late, was, now))
    print("QA cycle (movex_qa_entry): %d ports different" % qa_bad)
    bad += qa_bad

    # mod: src/move_lib.h
    L = ["// generated by movelib.py (2026-11-13, movelib: tools\\handlers -- the Revenge handler's library, from",
         "// gen/revanims.py's rev_anims.json and the pack): the built-in move libraries and the built-in ports' lists. Replaces",
         "// movex_data.h (rows by ext_late index). movex.c plays a library move from its number (move ID - 1: the exe's",
         "// move_lib_ids.h has the same numbers by name); a folder wrestler names one in his moves.json.",
         '#include "rev_move_anims.h"',
         "// The library moves: { the attacker's animation file (the defender's the next; 0: the move's own), the hold it goes on",
         "// to: its loop's attacker file (the endings + 2 / + 4 / + 6; 0: none), the entry the game's ground grapple picks after",
         "// it (0: none) }",
         "#define MOVELIB_N %d" % len(allm),
         "static const LibMove move_lib[MOVELIB_N] = {"]
    for m in allm:
        if "pair" in m:
            L.append("    { RMA_%s, %s, 0 },   // %d %s (on %d; %d cop%s in the pack; ports: %s)" % (
                m["pair"], "RMA_" + m["cont"] if m["cont"] else "0", m["num"], m["id"], m["base"], copies[m["pair"]],
                "y" if copies[m["pair"]] == 1 else "ies", " ".join(sorted(m["ports"]))))
        else:
            L.append("    { %d, %d, %d },   // %d %s (on %d): %s" % (m["file"], m["then"], m["ground"], m["num"], m["id"], m["base"], m["about"]))
    L.append("};")
    n_late = max(port) + 1
    L += ["// The built-in ports' lists (2026-11-13: what were movex_data.h's rows): a port's moveset entries that play a library",
          "// move, { entry, library move number, the move in the entry's place (0: his moveset's) }, in gen/revanims.py's order.",
          "// port_moves_at[late] .. port_moves_at[late + 1]: the port at ext_late[late] (roster_ext.h).",
          "#define PORT_LATE_N %d" % n_late,
          "#define PORT_MOVES_N %d" % sum(len(v) for v in port.values()),
          "static const PortMove port_moves[PORT_MOVES_N] = {"]
    at, starts = 0, []
    for late in range(n_late):
        starts.append(at)
        for e, num, carrier, about in port.get(late, []):
            L.append("    { %d, %d, %d },   // %s" % (e, num, carrier, about)); at += 1
    starts.append(at)
    L.append("};")
    L.append("static const u16 port_moves_at[PORT_LATE_N + 1] = { %s };" % ", ".join(str(s) for s in starts))
    L += ["// the pack's animations' header (the file's first 4 bytes) and record (file 0x2BF's, Revenge's made No Mercy's), by",
          "// file - RMA_FIRST (as gen/revanims.py made them for movex_data.h)",
          "#define RMA_N %d" % (2 * len(order)),
          "static const u8 rma_hdr[RMA_N + 1][4] = {"]
    L += ["    { %s }," % ", ".join("0x%02X" % b for b in bytes.fromhex(h)) for n in order for h in F[n]["hdr"]]
    L += ["    { 0 } };", "static const u8 rma_rec[RMA_N + 1][36] = {"]
    L += ["    { %s }," % ", ".join("0x%02X" % b for b in bytes.fromhex(h)) for n in order for h in F[n]["rec"]]
    L += ["    { 0 } };"]
    open(os.path.join(mod, "src", "move_lib.h"), "w", newline="\n").write("\n".join(L) + "\n")
    # (the headers and records: the same lines movex_data.h has)
    tail_old = old[old.index("#define RMA_N"):]
    tail_new = "\n".join(L)[("\n".join(L)).index("#define RMA_N"):] + "\n"
    print("rma_hdr / rma_rec: %s" % ("the same as movex_data.h's" if tail_old == tail_new else "DIFFERENT"))

    # exe: move_lib_ids.h
    E = ["// generated by movelib.py (2026-11-13, movelib): the built-in move libraries' IDs, for arcade_wrestlers.cpp's",
         "// move_library. { ID, its number (the mod's move_lib.h: number - 1), the No Mercy move it is usually played on }.",
         "#define MOVELIB_N %d" % len(allm),
         "static const struct { const char* id; uint32_t number; uint16_t base; } move_lib_ids[MOVELIB_N] = {"]
    for m in allm: E.append('    { "%s", %d, %d },' % (m["id"], m["num"], m["base"]))
    E.append("};")
    open(os.path.join(exe, "move_lib_ids.h"), "w", newline="\n").write("\n".join(E) + "\n")
    print("%d library moves (%d made by hand), %d port rows over %d ports; %d animation pairs in the pack, %d different" % (
        len(allm), len(hand), at, len([p for p in port if port[p]]), len(order), len(canon)))
    json.dump({m["id"]: dict(num=m["num"], base=m["base"], pair=m.get("pair"), cont=m.get("cont"), fa=m.get("fa"), fd=m.get("fd"))
               for m in allm}, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "move_lib.json"), "w"), indent=1)
    return bad

if __name__ == "__main__":
    sys.exit(1 if main() else 0)
