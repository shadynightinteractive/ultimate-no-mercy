#!/usr/bin/env python3
"""texcheck (modcheck, 2026-11-13): the texture-pack checker -- does every picture of a pack replace something the game draws?

The exe's texture packs (exe src\\main\\arcade_texpacks.cpp) name each replacement PNG by a number made from the picture it
replaces: XXH3-64 of the game's picture as RGBA, seeded with its size (arcade_view.cpp). A PNG whose name matches nothing the
game draws is never used, silently -- a pack made from another version, another palette of the same picture, or a typo.
So this takes what the game ACTUALLY drew (a QA run with "_dump" in WCW_TEXTURE_PACKS_DIR: the game saves every picture it
draws, named that way -- the capture of the hashes seen) and checks each pack picture against it:

  python tool\\texcheck.py check --packs <texturepacks folder> --seen <dump folder> [--seen <another>] --out <report.txt>
  python tool\\texcheck.py renders --before <qa run> --after <qa run> --out <dir>     the before/after render check

Per picture: MATCHED (the game drew it), UNMATCHED (it didn't, in the runs given -- with the drawn pictures it looks most
like, and AMBIGUOUS when two or more look about as alike: often one picture in several palettes, so the pack only replaces
one of them), OVERRIDDEN (a later pack has the same name: it wins, as the exe loads them), IGNORED (the exe skips it: not 16
hex digits, or in a folder starting with "_"). A matched picture that looks nothing like the one it replaces is called out
too (LOOKS UNLIKE: maybe meant for another). Also: a drawn picture that has others alike in the dump that the pack doesn't
cover (PARTIAL: the same picture in other palettes or sizes).
"""
import argparse, glob, json, os, re, sys
import numpy as np
from PIL import Image, ImageFilter

if os.name == "nt":                # (the machine is shared: below normal priority, RULES.md)
    import ctypes; ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)

NAME = re.compile(r"^[0-9A-Fa-f]{16}$")

def packs_of(root):
    """the packs as the exe reads them: each folder of root not starting with "_", in name order; each PNG anywhere under
    it whose name is 16 hex digits -> [(pack, name, path)], [(path, why ignored)]"""
    use, skip = [], []
    for pk in sorted(os.listdir(root)):
        d = os.path.join(root, pk)
        if not os.path.isdir(d): continue
        for p in sorted(glob.glob(os.path.join(d, "**", "*"), recursive=True)):
            if os.path.isdir(p): continue
            stem, ext = os.path.splitext(os.path.basename(p))
            if pk.startswith("_"):
                if pk != "_dump": skip.append((p, "its pack's folder starts with _: switched off"))
                continue
            if ext != ".png": skip.append((p, "not .png (the exe reads only .png -- the extension's case counts)")); continue
            if not NAME.match(stem): skip.append((p, "not named by 16 hex digits: never matched")); continue
            use.append((pk, int(stem, 16), p))
    return use, skip

def feat(path, n=16):
    """a picture's look, small: n x n RGB and alpha, and the same in grey normalised (shape, whatever the colours)"""
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    a = np.asarray(im.resize((n, n), Image.BILINEAR), dtype=np.float32) / 255.0
    g = a[..., :3].mean(axis=2)
    g = (g - g.mean()) / (g.std() + 1e-3)
    return dict(w=w, h=h, rgba=a, grey=g)

def alike(f1, f2):
    """0..1: how alike two pictures look -- the grey pattern's correlation (an edit that recolours keeps it), the colours,
    and whether their shapes (aspect) agree"""
    corr = float((f1["grey"] * f2["grey"]).mean())
    col = 1.0 - float(np.abs(f1["rgba"] - f2["rgba"]).mean()) * 2
    asp = 1.0 if abs(np.log((f1["w"] / f1["h"]) / (f2["w"] / f2["h"]))) < 0.05 else 0.5
    return max(0.0, (0.6 * max(corr, 0) + 0.4 * max(col, 0)) * asp)

def check(packs_root, seen_dirs, out):
    use, skip = packs_of(packs_root)
    seen = {}
    for d in seen_dirs:
        for p in glob.glob(os.path.join(d, "*.png")):
            stem = os.path.splitext(os.path.basename(p))[0]
            if NAME.match(stem): seen.setdefault(int(stem, 16), p)
    L = ["TEXTURE PACK CHECK (modcheck, 2026-11-13)", "packs: %s" % packs_root,
         "what the game drew: %d pictures (%s)" % (len(seen), "; ".join(seen_dirs)), ""]
    last = {}
    for pk, nm, p in use: last[nm] = (pk, p)
    sf = {}
    def sfeat(nm):
        if nm not in sf: sf[nm] = feat(seen[nm])
        return sf[nm]
    allseen = None
    counts = {"MATCHED": 0, "UNMATCHED": 0, "AMBIGUOUS": 0, "OVERRIDDEN": 0, "LOOKS UNLIKE": 0, "PARTIAL": 0, "IGNORED": len(skip)}
    rows = []
    for pk, nm, p in use:
        key = "%016X" % nm
        if last[nm][1] != p:
            counts["OVERRIDDEN"] += 1
            rows.append(("OVERRIDDEN", pk, key, "pack %s has the same name and loads later: it wins" % last[nm][0])); continue
        fp = feat(p)
        if nm in seen:
            s = alike(fp, sfeat(nm))
            if s < 0.35:
                counts["LOOKS UNLIKE"] += 1
                rows.append(("LOOKS UNLIKE", pk, key, "drawn, but it looks little like the picture it replaces (%.2f): meant "
                             "for another?" % s))
            else:
                counts["MATCHED"] += 1
                # (the same picture drawn under other names: another palette or size -- this pack covers only this one)
                if allseen is None: allseen = {k: sfeat(k) for k in seen}
                sib = sorted(((alike(sfeat(nm), f), k) for k, f in allseen.items() if k != nm and k not in last), reverse=True)
                sib = [(v, k) for v, k in sib if v > 0.9 and abs(np.corrcoef(sfeat(nm)["grey"].ravel(), allseen[k]["grey"].ravel())[0, 1]) > 0.95]
                if sib:
                    counts["PARTIAL"] += 1
                    rows.append(("MATCHED+PARTIAL", pk, key, "drawn (%.2f alike); %d more drawn picture(s) look the same in other "
                                 "colours and the pack has none for them: %s" % (s, len(sib), " ".join("%016X" % k for _, k in sib[:6]))))
                else: rows.append(("MATCHED", pk, key, "drawn (%.2f alike)" % s))
            continue
        if allseen is None: allseen = {k: sfeat(k) for k in seen}
        best = sorted(((alike(fp, f), k) for k, f in allseen.items()), reverse=True)[:4]
        amb = len(best) > 1 and best[0][0] > 0.5 and best[1][0] > best[0][0] - 0.05
        tag = "AMBIGUOUS" if amb else "UNMATCHED"
        counts[tag] += 1
        rows.append((tag, pk, key, "never drawn in these runs; looks most like %s" %
                     ", ".join("%016X (%.2f%s)" % (k, v, ", the same size" if (allseen[k]["w"], allseen[k]["h"]) == (fp["w"], fp["h"]) else "") for v, k in best[:3])
                     + ("  -- two or more about as alike: which one was meant?" if amb else "")))
    for p, why in skip: rows.append(("IGNORED", os.path.basename(os.path.dirname(p)), os.path.basename(p), why))
    L.append("  ".join("%s %d" % kv for kv in counts.items()))
    L.append("")
    for r in rows: L.append("%-15s %-14s %-20s %s" % r)
    ok = counts["UNMATCHED"] == 0 and counts["AMBIGUOUS"] == 0 and counts["LOOKS UNLIKE"] == 0
    L += ["", "RESULT: %s" % ("PASS (every picture replaces one the game drew)" if ok else
          "FAIL (%d pictures replace nothing the game drew in these runs)" % (counts["UNMATCHED"] + counts["AMBIGUOUS"] + counts["LOOKS UNLIKE"]))]
    open(out, "w", encoding="utf-8").write("\n".join(L) + "\n")
    print("\n".join(L))
    return ok

def renders(before, after, out, screen="select"):
    """the before/after render check: the same scenario without the packs and with them. The two runs' pictures aren't
    taken at the same moments, so each run's pictures of one screen (No Mercy's Superstar Select: visual.is_select) are
    put together -- their per-pixel median: the screen as it stands, without what moves -- and the two medians compared:
    where they differ is what the packs changed. With the game's own log lines about the packs."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import visual as V
    os.makedirs(out, exist_ok=True)
    def median_of(run):
        ims = []
        for p in sorted(glob.glob(os.path.join(run, "shots", "*.png"))):
            im = Image.open(p).convert("RGB")
            if V.is_select(im): ims.append(np.asarray(im))
        return (np.median(np.stack(ims[::max(1, len(ims) // 30)]), axis=0).astype(np.uint8), len(ims)) if ims else (None, 0)
    L = ["BEFORE / AFTER (modcheck, 2026-11-13)", "before: %s" % before, "after:  %s" % after, ""]
    for run, tag in ((before, "before"), (after, "after")):
        try:
            for l in open(os.path.join(run, "game_stderr.txt"), encoding="utf-8", errors="replace"):
                if l.startswith("[textures]"): L.append("%s log: %s" % (tag, l.rstrip()))
        except OSError: pass
    A, na = median_of(before); B, nb = median_of(after)
    L.append("pictures of the %s screen: before %d, after %d" % (screen, na, nb))
    if A is None or B is None or A.shape != B.shape:
        L.append("RESULT: NO PICTURES TO COMPARE"); open(os.path.join(out, "renders.txt"), "w").write("\n".join(L) + "\n"); print("\n".join(L)); return False
    d = np.abs(A.astype(np.int16) - B.astype(np.int16)).max(axis=2) > 40
    dm = np.asarray(Image.fromarray((d * 255).astype(np.uint8)).filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(9))) > 127
    frac = float(dm.mean())
    # (the changed areas, as boxes: rows and columns of 1/16 of the screen with change in them)
    h, w = dm.shape; cells = []
    for gy in range(16):
        for gx in range(16):
            c = dm[gy * h // 16:(gy + 1) * h // 16, gx * w // 16:(gx + 1) * w // 16]
            if c.mean() > 0.05: cells.append((gx, gy))
    L.append("changed: %.1f%% of the screen, in %d of its 256 cells" % (100 * frac, len(cells)))
    C = B.copy(); C[dm] = (C[dm] * 0.3 + np.array([255, 0, 255]) * 0.7).astype(np.uint8)
    S = Image.new("RGB", (w * 3, h)); S.paste(Image.fromarray(A), (0, 0)); S.paste(Image.fromarray(B), (w, 0)); S.paste(Image.fromarray(C), (2 * w, 0))
    S.resize((w * 3 // 2, h // 2)).save(os.path.join(out, "before_after.png"))
    L.append("side by side (before | after | what changed, magenta): %s" % os.path.join(out, "before_after.png"))
    ok = frac > 0.001
    L.append("RESULT: %s" % ("the packs changed what the game drew" if ok else "NOTHING CHANGED: the packs replaced nothing drawn"))
    open(os.path.join(out, "renders.txt"), "w", encoding="utf-8").write("\n".join(L) + "\n")
    print("\n".join(L))
    return ok

def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    c = sp.add_parser("check"); c.add_argument("--packs", required=True); c.add_argument("--seen", action="append", required=True)
    c.add_argument("--out", required=True)
    r = sp.add_parser("renders"); r.add_argument("--before", required=True); r.add_argument("--after", required=True)
    r.add_argument("--out", required=True)
    a = ap.parse_args()
    if a.cmd == "check": sys.exit(0 if check(a.packs, a.seen, a.out) else 1)
    else: sys.exit(0 if renders(a.before, a.after, a.out) else 1)

if __name__ == "__main__":
    main()
