"""revmusic_newref.py (revmusic, 2026-11-18): the NEW reference for the user's decision B (Revenge's song 0x214 played by us
from its sequence, mod\\gen\\revplay.py, instead of the emulator recording 0214.wav).

Checks that a ROM-only chain run (chain.py --only legacy_in,mkcontent,imports in a worktree with this change) differs from
the OLD reference -- as the earlier new references (EARLIER: romrender's, decision A) left it -- ONLY where the song's
theme is:
  content_index.h   the "Revenge song 0214" instrument row (its length, wave record, note), the offsets after it and the
                    three sizes, all moved by the same amount; every other line the same
  content.bin       the same bytes before that instrument's data and after it (moved by that amount); its data decodes
                    (VADPCM) to the theme revplay.py plays now
  the wrestler folders  only theme.bin / theme.meta, and only in folders that had the one old 0x214 theme, all now the
                    one new theme -- the same data as content.bin's
then (--write) copies those files into the overlay private\\reference\\revmusic (at their places relative to private\\),
which chain.py / imports.py compare with first (NEW_REFS).

    python revmusic_newref.py [--write] [--run <worktree>]     (default: this worktree)"""
import argparse, filecmp, json, os, re, shutil, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PRIV = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
OVERLAY = os.path.join(PRIV, "reference", "revmusic")
# the new references made before this one (chain.py NEW_REFS): a file this one changes too (a Revenge folder's wrestler.json:
# romrender changed its photo note) is written as theirs with only our change made (chain.py takes a later decision's copy)
EARLIER = [os.path.join(PRIV, "reference", "romrender")]
def earlier_ref(p):
    for r in EARLIER:
        q = os.path.join(r, os.path.relpath(p, PRIV))
        if os.path.exists(q): return q
    return p
OLD_BIN = os.path.join(PRIV, "build", "mod", "build", "content.bin")
OLD_IDX = os.path.join(PRIV, "generated", "mod", "src", "content_index.h")
OLD_WRESTLERS = os.path.join(PRIV, "reference", "wrestlers_tested")
ROW = re.compile(r"^(\s*\{ )0x([0-9A-F]+), 0x([0-9A-F]+), (\{[^}]*\}, \d+, 0x[0-9A-F]+, \d \},  // )(.*)$")
SIZES = ("CONTENT_SIZE", "CONTENT_THEMES_SIZE", "CONTENT_FRAMES")
def pad16(n): return n + (-n % 16)

def json_diff(p, q):
    """the paths where two JSON files differ"""
    a, b, out = json.load(open(p, encoding="utf-8")), json.load(open(q, encoding="utf-8")), set()
    def walk(x, y, path):
        if isinstance(x, dict) and isinstance(y, dict):
            for k in set(x) | set(y): walk(x.get(k), y.get(k), path + "/" + k)
        elif x != y: out.add(path)
    walk(a, b, "")
    return out

def note_fixed(new, old):
    """the old reference's wrestler.json with only its source.theme note made the run's (the text replaced in place, so
    every other byte -- the known source.abilities note too -- stays the old reference's)"""
    t = open(old, encoding="utf-8").read()
    o_, n_ = json.load(open(old, encoding="utf-8"))["source"]["theme"], json.load(open(new, encoding="utf-8"))["source"]["theme"]
    eo, en = json.dumps(o_)[1:-1], json.dumps(n_)[1:-1]
    assert t.count(eo) == 1
    t = t.replace(eo, en)
    x = json.loads(t); y = json.load(open(old, encoding="utf-8")); y["source"]["theme"] = n_
    assert x == y
    return t

def check_index(old, new):
    """-> (offset, old length, new length, the new row's wave bytes) after checking every line"""
    a, b = open(old).read().splitlines(), open(new).read().splitlines()
    assert len(a) == len(b), "the index's lines: %d / %d" % (len(a), len(b))
    k = next(i for i, l in enumerate(a) if ROW.match(l) and ROW.match(l).group(5).startswith("Revenge song 0214"))
    ma, mb = ROW.match(a[k]), ROW.match(b[k])
    off, lo, ln = int(ma.group(2), 16), int(ma.group(3), 16), int(mb.group(3), 16)
    assert int(mb.group(2), 16) == off and mb.group(5).startswith("Revenge song 0214")
    delta = pad16(ln) - pad16(lo)
    for i, (x, y) in enumerate(zip(a, b)):
        if i == k or x == y: continue
        m = re.match(r"^#define (\w+) 0x([0-9A-F]+)u?(.*)$", x)
        if m and m.group(1) in SIZES:
            assert y == "#define %s 0x%Xu%s" % (m.group(1), int(m.group(2), 16) + delta, m.group(3)) or \
                   y == "#define %s 0x%X%s" % (m.group(1), int(m.group(2), 16) + delta, m.group(3)), (x, y)
            continue
        mx, my = ROW.match(x), ROW.match(y)
        assert mx and my and i > k and int(my.group(2), 16) == int(mx.group(2), 16) + delta and \
            (mx.group(1), mx.group(3), mx.group(4), mx.group(5)) == (my.group(1), my.group(3), my.group(4), my.group(5)), \
            "an unexpected difference, line %d" % (i + 1)
    wave = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", mb.group(4).split("}")[0]))
    return off, lo, ln, wave, delta

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--run", default=os.path.dirname(os.path.dirname(HERE)))
    ap.add_argument("--content", help="a folder with the run's content.bin + content_index.h (when the shared mod\build "
                                      "has been rebuilt since)")
    ap.add_argument("--wrestlers", help="the run's imported folders (default <run>\scratch\chain\imports)")
    A = ap.parse_args()
    new_bin = os.path.join(A.content, "content.bin") if A.content else os.path.join(A.run, "mod", "build", "content.bin")
    new_idx = os.path.join(A.content, "content_index.h") if A.content else os.path.join(A.run, "mod", "src", "content_index.h")
    new_wrestlers = A.wrestlers or os.path.join(A.run, "scratch", "chain", "imports")
    # the index and content.bin
    off, lo, ln, wave, delta = check_index(OLD_IDX, new_idx)
    o, n = open(OLD_BIN, "rb").read(), open(new_bin, "rb").read()
    assert len(n) == len(o) + delta and n[:off] == o[:off] and n[off + pad16(ln):] == o[off + pad16(lo):], "content.bin"
    block = n[off:off + ln]
    print("content_index.h / content.bin: only Revenge song 0x214's instrument differs (%d -> %d bytes; the rest moved %+d)"
          % (lo, ln, delta))
    # its data is the theme played now
    sys.path.insert(0, os.path.join(A.run, "mod", "gen")); sys.path.insert(0, HERE)
    import revplay, numpy as np
    from roms import Roms
    y, ls, rate, info = revplay.theme(Roms().rom("revenge"), 0x214)
    order, npred = struct.unpack_from(">ii", wave, 0x18)
    book = struct.unpack_from(">%dh" % (order * npred * 8), wave, 0x20)
    ns = struct.unpack_from(">I", wave, 0xA4)[0]
    dec = np.array(revplay.vadpcm_decode(block, book, 0, len(block) // 9, 0, 0)[:ns], float)
    assert ns == len(y) and struct.unpack_from(">I", wave, 0xA0)[0] == ls, "the instrument's length / loop"
    snr = 10 * np.log10((y.astype(float) ** 2).mean() / ((dec - y) ** 2).mean())
    print("  its data decodes to the theme revplay.py plays: %d samples, loop from %d, VADPCM SNR %.1f dB" % (ns, ls, snr))
    assert snr > 15
    # the folders
    changed, olds, news, notes = [], set(), set(), []
    for col in sorted(os.listdir(OLD_WRESTLERS)):
        pa, pb = os.path.join(new_wrestlers, col), os.path.join(OLD_WRESTLERS, col)
        if not os.path.isdir(pb) or col.startswith("_"): continue          # (the collections; not _example)
        fa = {os.path.relpath(os.path.join(r, f), pa) for r, _, fs in os.walk(pa) for f in fs}
        fb = {os.path.relpath(os.path.join(r, f), pb) for r, _, fs in os.walk(pb) for f in fs}
        assert fa == fb, "%s: the files differ (%d only made, %d only in the reference)" % (col, len(fa - fb), len(fb - fa))
        for f in sorted(fa):
            if filecmp.cmp(os.path.join(pa, f), earlier_ref(os.path.join(pb, f)), shallow=False): continue
            if os.path.basename(f) == "wrestler.json":
                d = json_diff(os.path.join(pa, f), earlier_ref(os.path.join(pb, f)))
                if not d: continue
                assert d <= {"/source/abilities", "/source/theme"}, "%s: %s differs (%s)" % (col, f, sorted(d))
                if "/source/theme" in d: notes.append(os.path.join(col, f))  # its note says the theme's length / loop
                continue                                                  # (source.abilities: imports.py's known notes)
            assert os.path.basename(f) in ("theme.bin", "theme.meta"), "%s: %s differs" % (col, f)
            changed.append(os.path.join(col, f))
            (olds if f.endswith(".bin") else set()).add(open(earlier_ref(os.path.join(pb, f)), "rb").read())
            (news if f.endswith(".bin") else set()).add(open(os.path.join(pa, f), "rb").read())
    assert len(olds) == 1 and len(news) == 1, "the themes that differ are not all the one old 0x214 theme / the one new one"
    assert news == {block}, "the folders' new theme is not content.bin's"
    assert sorted(os.path.dirname(c) for c in notes) == sorted(os.path.dirname(c) for c in changed if c.endswith("theme.bin"))
    nf = len([c for c in changed if c.endswith("theme.bin")])
    print("wrestler folders: only theme.bin / theme.meta and wrestler.json's theme note differ, in %d folders that had the old 0x214 theme: now the new one "
          "(the same data as content.bin's)" % nf)
    if A.write:
        put = [(new_bin, OLD_BIN), (new_idx, OLD_IDX)] + [(os.path.join(new_wrestlers, c), os.path.join(OLD_WRESTLERS, c))
                                                          for c in changed]
        for src, ref in put:
            dst = os.path.join(OVERLAY, os.path.relpath(ref, PRIV))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)
        for c in notes:
            dst = os.path.join(OVERLAY, os.path.relpath(os.path.join(OLD_WRESTLERS, c), PRIV))
            open(dst, "w", encoding="utf-8", newline="").write(note_fixed(os.path.join(new_wrestlers, c),
                                                                          earlier_ref(os.path.join(OLD_WRESTLERS, c))))
        print("written: %d files under %s" % (len(put) + len(notes), os.path.relpath(OVERLAY, PRIV)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
