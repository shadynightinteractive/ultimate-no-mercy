"""newref.py (romrender, 2026-11-18): checks that a ROM-only chain run with the renderer differs from the old reference ONLY in
what the renderer makes, then writes those files as the NEW reference (private\\reference\\romrender, at the same places
relative to private\\ as the old reference's files: chain.py / imports.py compare with them first).

    python newref.py <made wmport.bin> <wmport_files.h> <arena_files.h> <made wrestlers> [--port-photos <folder>]
                     [--write]

Allowed to differ:
  wmport.bin       only the files of the arenas' ARENA page photos (6 tiles each) and VS backdrops (40 each), the same size,
                   and Bret's / Neidhart's Revenge Titantron frames (TEX 64 x 64 CI8, 4624 bytes): all 144 of them now EMPTY
                   (the user removed their Titantron, 2026-11-18: gen/wmport.py REV_TRON_RETIRED) -- every other file byte
                   for byte; the pack's size the old one's less the frames'
  wmport_files.h   only its files' table (the offsets), WMP_PACK_SIZE, and its videos without Bret's / Neidhart's rows
  wmport_videos.pkl  the reference's without brethart / neidhart     (both: written to the new reference too)
  --port-photos    mkportphotos.py's three headers (rev_photos.h, jarrett_ / meanie_photo.h): written as the new reference
                   of private\generated\mod\src's (they change the mod build)
  wrestler folders only photo.bin, and wrestler.json in source.photo (plus the 45 known source.abilities notes) -- the
                   Revenge, WrestleMania 2000 and Imported collections; No Mercy's untouched
Exit 1 when anything else differs (nothing is written then)."""
import filecmp, json, os, re, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
PRIV = os.path.dirname(os.path.dirname(SRC)) if os.path.basename(os.path.dirname(SRC)) == "wt" else os.path.join(os.path.dirname(SRC), "private")
OLD_PACK = os.path.join(PRIV, "build", "mod", "build", "wmport.bin")
OLD_IDX = os.path.join(PRIV, "generated", "mod", "src", "wmport_files.h")
OLD_VIDEOS = os.path.join(PRIV, "data", "gen_data", "wmport_videos.pkl")
NEW_VIDEOS = os.path.join(SRC, "data", "gen_data", "wmport_videos.pkl")
GONE_VIDEOS = ("brethart", "neidhart")       # (the user removed their Titantron, 2026-11-18)
OLD_W = os.path.join(PRIV, "reference", "wrestlers_tested")
NEW_REF = os.path.join(PRIV, "reference", "romrender")

def files_of(pack, idx):
    offs = [(int(a, 16), int(n)) for a, n in re.findall(r"\{ (0x[0-9A-F]+), (\d+) \}", open(idx).read())]
    d = open(pack, "rb").read()
    return [d[o:o + n] for o, n in offs], len(d)

def header_check(new_h, old_h, nlen):
    """wmport_files.h: the same as the reference's but for the files' table (its offsets / sizes: checked against each other
    here, the files themselves above), the pack's size and the videos' section without Bret's / Neidhart's rows (the rest of
    it, its lengths, as the rows left need) -> [problems]"""
    def parts(L):
        t0 = next(i for i, l in enumerate(L) if l.startswith("static const u32 wmp_files["))
        t1 = next(i for i in range(t0, len(L)) if L[i].startswith("};"))
        v0 = next(i for i, l in enumerate(L) if l.startswith("// Their Titantron videos"))
        return L[:t0 + 1], L[t0 + 1:t1], L[t1:v0], L[v0:]
    nh, oh = parts(open(new_h).read().splitlines()), parts(open(old_h).read().splitlines())
    bad = []
    size = lambda L: [l for l in L if l.startswith("#define WMP_PACK_SIZE")]
    if [l for l in nh[0] if l not in size(nh[0])] != [l for l in oh[0] if l not in size(oh[0])]: bad.append("wmport_files.h's head")
    if size(nh[0]) != ["#define WMP_PACK_SIZE 0x%Xu" % nlen]: bad.append("wmport_files.h's WMP_PACK_SIZE")
    offs = [(int(a, 16), int(n)) for l in nh[1] for a, n in re.findall(r"\{ (0x[0-9A-F]+), (\d+) \}", l)]
    if len(nh[1]) != len(oh[1]) or any(offs[i + 1][0] != offs[i][0] + offs[i][1] + (-offs[i][1] % 8) for i in range(len(offs) - 1)):
        bad.append("wmport_files.h's table")
    if nh[2] != oh[2]: bad.append("wmport_files.h's hands' tables")
    keep = [l for l in oh[3] if not any(l.endswith("// %s" % g) or ("// %s (" % g) in l for g in GONE_VIDEOS)]
    vrows = [l for l in keep if l.startswith("    { {")]
    frows = [l for l in keep if l.startswith("    { 0x")]
    n_v = len(vrows)
    length = max(len(re.findall(r"\{ \d+, 0x[0-9A-F]+ \}", l)) for l in vrows)
    frames = max(len(re.findall(r"0x[0-9A-F]+", l.split("//")[0])) for l in frows)
    want = []
    for l in keep:
        if l.startswith("#define WMP_VIDEOS_N"): l = "#define WMP_VIDEOS_N %d" % n_v
        elif l.startswith("#define WMP_VIDEO_LEN"): l = "#define WMP_VIDEO_LEN %d" % length
        elif l.startswith("#define WMP_VIDEO_FRAMES"): l = "#define WMP_VIDEO_FRAMES %d" % frames
        want.append(l)
    if nh[3] != want: bad.append("wmport_files.h's videos: not the reference's without %s" % " / ".join(GONE_VIDEOS))
    return bad

def json_diff(a, b, path=""):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)): out += json_diff(a.get(k), b.get(k), path + "/" + k)
    elif a != b: out.append(path)
    return out

def main():
    pack, idx, af, wr = sys.argv[1:5]; write = "--write" in sys.argv
    bad = []
    # the pack
    new, nlen = files_of(pack, idx); old, olen = files_of(OLD_PACK, OLD_IDX)
    s = open(af).read()
    first = lambda name: [int(x, 16) - 0x5000 for x in re.search(name + r"\[XARENA_N\] = \{([^}]*)\}", s).group(1).split(",")]
    allowed = set()
    for p in first("xarena_preview_file"): allowed |= set(range(p, p + 6))
    for b in first("xarena_backdrop_file"): allowed |= set(range(b, b + 40))
    changed = [i for i in range(len(new)) if new[i] != old[i]]
    trons = [i for i in changed if i not in allowed and len(new[i]) == 0 and len(old[i]) == 4624 and old[i][:3] == b"TEX"]
    if len(new) != len(old): bad.append("wmport.bin %d files, the reference's %d" % (len(new), len(old)))
    if nlen != olen - 4624 * len(trons): bad.append("wmport.bin size %d, the reference's %d less the frames'" % (nlen, olen))
    if [i for i in allowed if len(new[i]) != len(old[i])]: bad.append("an arena file's size differs")
    other = [i for i in changed if i not in allowed and i not in trons]
    print("wmport.bin: %d files differ: %d arena page / backdrop files (of %d), %d Titantron frames removed (empty), %d other"
          % (len(changed), len(set(changed) & allowed), len(allowed), len(trons), len(other)))
    if other: bad.append("wmport.bin files 0x%s differ" % ", 0x".join("%X" % (0x5000 + i) for i in other[:10]))
    if len(trons) != 144: bad.append("%d Titantron frames removed (144 expected)" % len(trons))
    bad += header_check(idx, OLD_IDX, nlen)
    import pickle
    nv, ov = pickle.load(open(NEW_VIDEOS, "rb")), pickle.load(open(OLD_VIDEOS, "rb"))
    if nv != {k: v for k, v in ov.items() if k not in GONE_VIDEOS}: bad.append("wmport_videos.pkl %s, the reference's %s" % (nv, ov))
    print("wmport_files.h / wmport_videos.pkl: %s" % ("only the table, the size and the two videos gone" if not bad else "see below"))
    # the wrestler folders
    to_copy = []
    for col in ("No Mercy", "Revenge", "WrestleMania 2000", "Imported"):
        pa, pb = os.path.join(wr, col), os.path.join(OLD_W, col)
        fa = {os.path.relpath(os.path.join(r, f), pa) for r, _, fs in os.walk(pa) for f in fs}
        fb = {os.path.relpath(os.path.join(r, f), pb) for r, _, fs in os.walk(pb) for f in fs}
        if fa != fb: bad.append("%s: files %d only made, %d only in the reference" % (col, len(fa - fb), len(fb - fa)))
        n_ph = n_js = 0
        for f in sorted(fa & fb):
            a, b = os.path.join(pa, f), os.path.join(pb, f)
            if filecmp.cmp(a, b, shallow=False): continue
            name = os.path.basename(f)
            if col != "No Mercy" and name == "photo.bin": to_copy.append((a, b)); n_ph += 1; continue
            if col != "No Mercy" and name == "wrestler.json":
                d = json_diff(json.load(open(a, encoding="utf-8")), json.load(open(b, encoding="utf-8")))
                if set(d) <= {"/source/photo", "/source/abilities"} and "/source/photo" in d: to_copy.append((a, b)); n_js += 1; continue
                bad.append("%s/%s: %s" % (col, f, d)); continue
            bad.append("%s/%s differs" % (col, f))
        print("%-18s %4d files: %d photos and %d wrestler.json (their source.photo) rendered, nothing else differs" % (col, len(fa), n_ph, n_js)
              if not [x for x in bad if x.startswith(col)] else "%-18s differs: see below" % col)
    for x in bad: print("NOT EXPECTED:", x)
    if bad: return 1
    if write:
        to_copy += [(pack, OLD_PACK), (idx, OLD_IDX), (NEW_VIDEOS, OLD_VIDEOS)]
        if "--port-photos" in sys.argv:
            pf = sys.argv[sys.argv.index("--port-photos") + 1]
            for h in ("rev_photos.h", "jarrett_photo.h", "meanie_photo.h"):
                to_copy.append((os.path.join(pf, h), os.path.join(PRIV, "generated", "mod", "src", h)))
        for a, b in to_copy:
            q = os.path.join(NEW_REF, os.path.relpath(b, PRIV)); os.makedirs(os.path.dirname(q), exist_ok=True)
            shutil.copyfile(a, q)
        print("new reference written: %d files under %s" % (len(to_copy), NEW_REF))
    return 0

if __name__ == "__main__":
    sys.exit(main())
