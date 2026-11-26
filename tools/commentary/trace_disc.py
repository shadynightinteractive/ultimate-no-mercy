"""Developer side, once a WWE '13 disc is here (2026-11-14, commentary agent): finish the recipe from it.

  python trace_disc.py <disc folder or image> [--work <dir>]

1. Every WWE '13 clip of the recipe: its .wem found on the disc (by id) -> src_sha1 filled in (so a player's rebuild
   can tell its disc has the very same file), and the clip remade (rebuild.py's way) and judged against today's pack.
2. The cut pieces whose source isn't known (names / lead-ins cut from lines the local caches don't have): each piece
   (data/extracted/keep/Keep/*.wav) is looked for in every clip on the disc -- a coarse pass at 4 kHz picks the 5 best
   clips, a fine pass at 20 kHz on the silence-trimmed clip gives the place, the gain and the score.
Writes recipe/clips.tsv back (a copy of the old one kept as clips.before_disc.tsv) and trace_disc.txt (every result).
Low priority throughout.

DONE AND SUPERSEDED (commdisc 2026-11-18, with the real disc): the 151 pieces are placed in the recipe. This script as it is
finds none of them: its coarse pass takes x[::5] with no anti-aliasing filter (a one-sample shift scrambles aliased speech),
and it searches the TRIMMED line with the cutfit model, while the pieces were cut from the whole line by
cutfit.apply_exact (README.md, "With the real disc"). Placing them again: resample to 2 kHz with a filter
(scipy.signal.resample_poly), search the untrimmed lines, then the fine pass and the exact model at 20 kHz.
"""
import argparse, hashlib, json, os, shutil, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lowprio  # noqa: F401
import rebuild as RB
import splice_trace as S
import cutfit

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("disc"); ap.add_argument("--work")
    a = ap.parse_args()
    T = RB.tools()
    work = a.work or os.path.join(HERE, "work_disc"); os.makedirs(work, exist_ok=True)
    disc = RB.Disc(RB.disc_folder(a.disc, work, T))
    lines, moves, clips = RB.load_recipe()
    log = []
    print("media on the disc:", len(disc.where))
    # 1. sources' sha1, and each WWE '13 clip remade and judged
    stat = {}
    for c in clips:
        if c["kind"] == "w13" and int(c["source"]) in disc.where:
            c["src_sha1"] = hashlib.sha1(disc.read(int(c["source"]))).hexdigest()
            ogg, st = RB.make_clip(T, c, disc, work)
            stat[st] = stat.get(st, 0) + 1
            log.append("clip %s w13 %s: %s" % (c["clip"], c["source"], st))
        elif c["kind"] == "w13":
            stat["missing"] = stat.get("missing", 0) + 1; log.append("clip %s w13 %s: missing" % (c["clip"], c["source"]))
    print("WWE '13 clips remade:", stat)
    # 2. the cut pieces with no known place
    todo = [c for c in clips if c["kind"] == "cut" and "?" in (c["source"], c["start"], c["gain"])]
    if todo:
        keep = json.load(open(os.path.join(S.KEEP, "keep.json"), encoding="utf-8"))
        coarse = {}
        for mid in disc.where:
            x = RB.pcm(T, RB.wem_to_wav(T, disc.read(mid), work, mid), RB.TRIM) / 32768
            coarse[mid] = x[::5].astype(np.float32)          # (4 kHz: 20 kHz taken one in five; a coarse pass only)
        for c in todo:
            piece = S.dec(os.path.join(S.KEEP, S.keep_name(keep[int(c["keep"])]["file"])))
            p4 = piece[::5]
            cand = []
            ids = [int(c["source"].split(":")[1])] if c["source"].startswith("w13:") else list(coarse)
            for mid in ids:
                if mid in coarse: cand.append((S.best_match(p4, coarse[mid].astype(np.float64))[0], mid))
            cand.sort(reverse=True)
            best = (0, None, 0, 0)
            for _, mid in cand[:5]:
                x = RB.pcm(T, RB.wem_to_wav(T, disc.read(mid), work, mid), RB.TRIM) / 32768
                s, k, g = S.best_match(piece, x)
                if s > best[0]: best = (s, mid, k, g)
            log.append("cut %s (keep %s %r): best %.4f in %s at %d gain %.4f" % (
                c["clip"], c["keep"], keep[int(c["keep"])]["text"], best[0], best[1], best[2], best[3]))
            print(log[-1], flush=True)
            if best[0] >= 0.9:
                c.update(source="w13:%d" % best[1], start=str(best[2]), length=str(len(piece)), gain="%.4f" % best[3])
    # 3. every cut piece from a WWE '13 line, against the disc's exact clip: gain and end fades (cutfit.py's model);
    #    a good fit is tens of dB, against the caches' lossy copies it couldn't be told from codec noise
    keep = json.load(open(os.path.join(S.KEEP, "keep.json"), encoding="utf-8"))
    for c in clips:
        if c["kind"] != "cut" or not c["source"].startswith("w13:") or "?" in (c["start"], c["gain"]): continue
        mid = int(c["source"][4:])
        if mid not in disc.where: continue
        x = RB.pcm(T, RB.wem_to_wav(T, disc.read(mid), work, mid), RB.TRIM)
        piece = S.dec(os.path.join(S.KEEP, S.keep_name(keep[int(c["keep"])]["file"]))) * 32768
        # (the place again, to the sample, on the exact clip)
        _, k, _ = S.best_match(piece, x)
        g, fin, fout, snr = cutfit.fit(piece, x, k)
        c.update(start=str(k), gain="%.5f" % g, fade_in=str(fin), fade_out=str(fout))
        log.append("cut %s %r: start %d gain %.4f fades %d / %d, fit %.1f dB" % (
            c["clip"], keep[int(c["keep"])]["text"], k, g, fin, fout, snr))
        print(log[-1], flush=True)
    shutil.copy(os.path.join(RB.RECIPE, "clips.tsv"), os.path.join(RB.RECIPE, "clips.before_disc.tsv"))
    cols = open(os.path.join(RB.RECIPE, "clips.tsv"), encoding="utf-8").readline().rstrip("\n").split("\t")
    with open(os.path.join(RB.RECIPE, "clips.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(cols) + "\n")
        for c in clips: f.write("\t".join(c.get(k, "-") for k in cols) + "\n")
    open(os.path.join(HERE, "trace_disc.txt"), "w", encoding="utf-8").write("\n".join(log) + "\n")

if __name__ == "__main__":
    main()
