"""rebuild.py end to end without a disc (2026-11-14, commentary agent): a made-up "disc" folder whose Wwise package
(big-endian, like the Wii's) holds, under their real media ids, the local cache's encodes of some WWE '13 clips (plain
Ogg Vorbis, which rebuild.py decodes with ffmpeg; the real disc's Wwise Vorbis needs vgmstream-cli). A recipe cut down
to those clips (and one spliced piece cut from one of them) is built into a pack, which is read back.

What it proves: the package reader, finding media by id, decode -> trim -> encode -> the recipe's Ogg header -> the
judge, cutting a piece, the renumbering and the pack writer. What it can't: decoding real Wwise Vorbis, or the
identical/equivalent split on real sources (these sources are already encoded once, so a clip comes out "equivalent"
at best; a cut whose source is lossy is judged by length and loudness only).
"""
import os, sys, shutil, json, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lowprio  # noqa: F401
import rebuild as RB, test_akpk, pakread
DEV = os.path.abspath(os.path.join(HERE, "..", ".."))   # (the source tree; data is linked in from the private folder)
W13 = os.path.join(DEV, "data", "commentary_audio", "wwe13", "ogg")

def main():
    t = os.path.join(HERE, "tmp", "selftest")
    if os.path.realpath(t).startswith(os.path.realpath(HERE)) and os.path.isdir(t): shutil.rmtree(t)
    os.makedirs(os.path.join(t, "disc", "files", "Audio")); os.makedirs(os.path.join(t, "recipe"))
    lines, moves, clips = RB.load_recipe()
    w13 = [c for c in clips if c["kind"] == "w13"][:30]
    cut = next(c for c in clips if c["kind"] == "cut" and c["source"].startswith("w13:") and "?" not in (c["start"], c["gain"]))
    want_ids = sorted({int(c["source"]) for c in w13} | {int(cut["source"][4:])})
    files = [(i, open(os.path.join(W13, "%d.ogg" % i), "rb").read()) for i in want_ids]
    open(os.path.join(t, "disc", "files", "Audio", "test.pck"), "wb").write(test_akpk.make(">", 0, [], files))
    # the recipe, cut down: those clips renumbered 0.., their rows
    sub = w13 + [cut]
    new = {c["clip"]: str(k) for k, c in enumerate(sub)}
    cols = open(os.path.join(RB.RECIPE, "clips.tsv"), encoding="utf-8").readline().rstrip("\n").split("\t")
    with open(os.path.join(t, "recipe", "clips.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(cols) + "\n")
        for c in sub: f.write("\t".join(new[c["clip"]] if k == "clip" else c.get(k, "-") for k in cols) + "\n")
    rows = [l.split("\t") for l in lines.splitlines()]
    sub_rows = [[new[r[0]]] + r[1:] for r in rows if r[0] in new]
    open(os.path.join(t, "recipe", "lines.tsv"), "w", encoding="utf-8", newline="").write("".join("\t".join(r) + "\n" for r in sub_rows))
    open(os.path.join(t, "recipe", "moves.tsv"), "w", encoding="utf-8", newline="").write(moves)
    shutil.copy(os.path.join(RB.RECIPE, "recipe.json"), os.path.join(t, "recipe", "recipe.json"))   # (commentary2: comment + target)
    RB.RECIPE = os.path.join(t, "recipe")
    RB.RMS_TOL = 2.0      # (these sources are encodes already: encoding one again comes out ~1.1 dB louder)
    class A: pass
    a = A(); a.disc = os.path.join(t, "disc"); a.out = os.path.join(t, "out", "commentary.pak"); a.legends = None  # (unused: WWE '13 only)
    a.work = os.path.join(t, "work"); a.keep_work = True
    os.makedirs(os.path.dirname(a.out))
    RB.cmd_build(a)
    st = json.load(open(a.out + ".rebuild.json"))
    r, m, c, sha = pakread.read_pack(a.out)
    print("statuses:", collections.Counter(st.values()))
    ok = len(c) == sum(1 for v in st.values() if v in ("identical", "equivalent")) and r.count("\n") == len(
        [x for x in sub_rows if st[x[0]] in ("identical", "equivalent")])
    print("pack read back: %d clips, %d rows -> %s" % (len(c), r.count("\n"), "ok" if ok else "MISMATCH"))
    print("cut piece (clip %s, %r): %s" % (new[cut["clip"]], cut["source"], st[new[cut["clip"]]]))

if __name__ == "__main__":
    main()
