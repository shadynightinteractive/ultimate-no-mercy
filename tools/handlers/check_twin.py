#!/usr/bin/env python3
"""check_twin.py (nmhandler, 2026-11-13): reads a QA run's log for the in-game twin of the stock folders (option
qa_stock_twin, mod src/roster.c) and checks that EVERY stock Superstar with a folder was compared -- each costume's record,
model and music, his moveset, each photo's three files -- and how each came out.

    python check_twin.py <qa run folder> <wrestlers folder> [--expect-different id:what,...]

Writes tools\\handlers\\work\\twin_<run name>.txt (and prints it). Exit 0 when every
expected comparison is there and IDENTICAL (or DIFFERENT exactly where --expect-different says).
"""
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(HERE, "work"), exist_ok=True)

def main():
    run, wdir = sys.argv[1], sys.argv[2]
    expect_diff = set()
    if "--expect-different" in sys.argv:
        expect_diff = set(sys.argv[sys.argv.index("--expect-different") + 1].split(","))
    log = open(os.path.join(run, "game_stderr.txt"), encoding="utf-8", errors="replace").read().splitlines()
    got = {}            # (sid, what, n) -> [verdicts]
    for l in log:
        m = re.match(r"\[wrestlers\] stock twin ([^\s:]+):? (?:costume (\d+): (record|model|music)|(moveset)|photo (\d+):) ?:?\s*(IDENTICAL|DIFFERENT)", l)
        if not m: continue
        sid = m.group(1)
        if m.group(3): key = (sid, m.group(3), int(m.group(2)))
        elif m.group(4): key = (sid, "moveset", 0)
        else: key = (sid, "photo", int(m.group(5)))
        got.setdefault(key, []).append(m.group(6))
    problems = [l for l in log if l.startswith("[wrestlers] stock ") and "stock twin" not in l]
    lines, bad = [], 0
    folders = []
    for root, dirs, files in os.walk(wdir):
        if "wrestler.json" in files:
            folders.append(root); dirs[:] = []
    for d in sorted(folders):
        j = json.load(open(os.path.join(d, "wrestler.json"), encoding="utf-8"))
        if "builtin" not in j: continue
        sid = j["id"]
        want = [(sid, w, c) for c in range(4) for w in ("record", "model", "music")] + [(sid, "moveset", 0)]
        photos = sorted({l[37] for l in j.get("looks", [])})
        want += [(sid, "photo", p) for p in photos]
        row, ok = [], True
        for k in want:
            v = got.get(k, [])
            n_need = 3 if k[1] == "photo" else 1          # (a photo: its TEX, palette and picture files)
            tag = "%s:%s" % (sid, k[1] if k[1] in ("moveset",) else "%s%d" % (k[1], k[2]))
            diff_expected = tag in expect_diff or ("%s:%s" % (sid, k[1])) in expect_diff
            if len(v) < n_need: row.append("%s MISSING" % tag); ok = False; continue
            if diff_expected:
                if "DIFFERENT" not in v: row.append("%s identical but a difference was expected" % tag); ok = False
                else: row.append("%s DIFFERENT (as expected)" % tag)
            elif any(x != "IDENTICAL" for x in v): row.append("%s DIFFERENT" % tag); ok = False
        bad += not ok
        n_ok = sum(1 for r in row if "MISSING" not in r and "DIFFERENT" not in r and "expected" not in r)
        lines.append("%-20s %s" % (sid, ("IDENTICAL: 4 records, 4 models, 4 music, moveset, %d photo%s x 3 files" % (len(photos), "" if len(photos) == 1 else "s"))
                                   if ok and not any("as expected" in r for r in row) else "; ".join(r for r in row if "IDENTICAL" not in r) or "ok"))
    lines.append("%d stock folders, %d with a problem; %d comparisons in the log; other stock lines: %d" % (len(lines), bad, sum(len(v) for v in got.values()), len(problems)))
    lines += ["  " + p for p in problems[:20]]
    out = "\n".join(lines)
    print(out)
    open(os.path.join(HERE, "work", "twin_%s.txt" % os.path.basename(os.path.normpath(run))), "w", newline="\n").write(
        "run %s\nfolders %s\n%s\n" % (os.path.basename(os.path.normpath(run)), os.path.basename(os.path.normpath(wdir)), out))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
