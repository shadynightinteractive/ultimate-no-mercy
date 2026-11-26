"""[commentary2 2026-11-18] THE DEVELOPER'S REFERENCE. Players never run this: the game makes the pack itself, natively, the
first time it finds the WWE '13 image in roms\ (exe src/main/commentary/arcade_commentary_build.cpp + packbuild/,
byte for byte this script's chain: see README.md). Kept to check the native builder against FFmpeg/vgmstream and to finish
the recipe with trace_disc.py once the disc is here.

Make commentary.pak from the player's own WWE '13 (Wii) disc (2026-11-14, commentary agent; PROTOTYPE, not yet run
on a disc). The pack doesn't ship: it is Jim Ross and Jerry Lawler from another game. What ships is recipe/ (the
director's index and, per clip, where it is on the disc -- no audio); this finds each clip on the player's disc,
decodes it, trims and encodes it the way mkcommentary.py did, and writes the pack.

  python rebuild.py check                                   the tools it needs, found or not
  python rebuild.py scan  <disc>                            what's on the disc: packages, media, recipe clips found
  python rebuild.py build <disc> <out commentary.pak> [--work <dir>] [--keep-work]

<disc>      a folder of the disc's files (Dolphin: right-click the game, Properties, Filesystem, Extract Entire Disc),
            or a disc image (.rvz .iso .wbfs .gcz .wia .ciso): its files are pulled out with DolphinTool
            (Dolphin's command-line tool; on PATH or in the DOLPHIN_TOOL environment variable)
(WWE '13 only: the user, 2026-11-14. The Legends of WrestleMania analysis is kept in legends/ as notes, unused.)
--work      where the decoded files go (default: a folder beside the output); deleted afterwards unless --keep-work

Tools (found on PATH, or the environment variable named): ffmpeg (FFMPEG), vgmstream-cli (VGMSTREAM_CLI: it decodes
Wwise's Vorbis), DolphinTool (DOLPHIN_TOOL, only for an image).
  vgmstream-cli: our own build from the local source, build_vgmstream.ps1 -> vgmstream_build\cli\vgmstream-cli.exe
  (found there without VGMSTREAM_CLI too; it needs the libvorbis.dll etc. from vgmstream_src\ext_libs\dll-x64 beside
  it or on PATH: build_vgmstream.ps1 -Finish copies them).

Each clip comes out one of:
  identical    the same bytes as the pack the recipe was made from (sha1)
  equivalent   the same line, encoded by another ffmpeg/libvorbis build: its length within 2 Vorbis blocks and its
               loudness within 1 dB of the recipe's (two encoders' results differ like that; see FINDINGS)
  wrong        found but not that line (left out, reported)
  missing      not on this disc (left out, reported)
The pack is written with every clip that's identical or equivalent, the index renumbered to match.
"""
import argparse, hashlib, json, os, shutil, struct, subprocess, sys, tempfile, wave
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import akpk, oggpk, cutfit
from compak_build import build, write_parts

# (commentary2 2026-11-18: the recipe is the game's own data now -- the game's builder reads the same files)
RECIPE = os.path.join(HERE, "..", "..", "exe", "WWFNoMercyRecomp", "assets", "commentary", "wwe13")
# (mkcommentary.py's: silence off both ends only, mono, the clip's own 20 kHz, Vorbis q0)
TRIM = ("silenceremove=start_periods=1:start_threshold=-50dB,areverse,"
        "silenceremove=start_periods=1:start_threshold=-50dB,areverse")
RATE = 20000
# (equivalent: loudness within this many dB of the recipe's. Two encodes of the same PCM by different builds differ by
#  well under 0.1 dB; the self-test's double encode (an encode of an encode) comes out ~1.1 dB louder: it sets 2.0)
RMS_TOL = 1.0
IMAGE_EXT = (".rvz", ".iso", ".wbfs", ".gcz", ".wia", ".ciso", ".gcm")

def tool(env, *names):
    p = os.environ.get(env)
    if p and os.path.exists(p): return p
    for n in names:
        q = shutil.which(n)
        if q: return q
    return None

def tools():
    vgm = tool("VGMSTREAM_CLI", "vgmstream-cli", "vgmstream_cli")
    return {"ffmpeg": tool("FFMPEG", "ffmpeg"), "vgmstream-cli": vgm,
            "DolphinTool": tool("DOLPHIN_TOOL", "DolphinTool", "dolphin-tool")}

def low_priority():
    # (the rebuild runs once, on the player's first start: it shouldn't make the PC sluggish meanwhile)
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)  # below normal
    else:
        try: os.nice(10)
        except OSError: pass

# ---- the recipe --------------------------------------------------------------------------------------------------
def recipe_moves(d=None):
    """the pack's moves table: a developer's moves.tsv beside the recipe if there is one, else made from the player's No
    Mercy (USA) (Rev 1) ROM by the recipe's references (moves_refs.tsv) -- as the game does (packbuild/comm_moves.cpp).
    The names are the game's text: never in the repository (commentary2, 2026-11-18)."""
    d = d or RECIPE
    p = os.path.join(d, "moves.tsv")
    if os.path.exists(p): return open(p, encoding="utf-8", newline="").read()
    sys.path.insert(0, os.path.join(HERE, "..", "romimport"))
    from roms import Roms
    F = Roms().files("nomercy")
    if F is None: raise SystemExit("the moves table is made from the No Mercy (USA) (Rev 1) ROM: put it in roms")
    out = []
    for l in open(os.path.join(d, "moves_refs.tsv"), encoding="utf-8"):
        if not l.strip() or l.startswith("#"): continue
        n, f, o = (int(x) for x in l.split(TAB))
        b = F(f); out.append(str(n) + TAB + b[o:b.index(bytes(1), o)].decode("latin1").lower() + NL)
    return "".join(out)

TAB, NL = chr(9), chr(10)

def load_recipe(d=None):
    d = d or RECIPE
    lines = open(os.path.join(d, "lines.tsv"), encoding="utf-8", newline="").read()
    moves = recipe_moves(d)
    t = open(os.path.join(d, "clips.tsv"), encoding="utf-8").read().splitlines()
    cols = t[0].split("\t")
    clips = [dict(zip(cols, l.split("\t"))) for l in t[1:]]
    return lines, moves, clips

# ---- the disc ----------------------------------------------------------------------------------------------------
def disc_folder(disc, work, T):
    """the disc's files as a folder: the folder given, or the image's files pulled out into <work>/disc"""
    if os.path.isdir(disc): return disc
    if not disc.lower().endswith(IMAGE_EXT): raise SystemExit("%s: not a folder or a disc image" % disc)
    if not T["DolphinTool"]:
        raise SystemExit("a disc image needs DolphinTool (Dolphin's command-line tool) -- or extract the disc in Dolphin "
                         "(Properties, Filesystem, Extract Entire Disc) and give that folder")
    out = os.path.join(work, "disc")
    if not os.path.isdir(out) or not os.listdir(out):
        os.makedirs(out, exist_ok=True)
        # (DolphinTool extract, its arguments checked against Dolphin's source 2026-11-14 (DolphinTool/ExtractCommand.cpp):
        #  -i image, -o folder, -g only the DATA partition (the game's files), -q errors only. The decryption is
        #  Dolphin's own: nothing of Nintendo's is in our code.)
        r = subprocess.run([T["DolphinTool"], "extract", "-i", disc, "-o", out, "-g", "-q"], capture_output=True, text=True)
        if r.returncode != 0: raise SystemExit("DolphinTool extract failed:\n" + r.stdout + r.stderr)
    return out

def bank_media(b):
    """a sound bank's own media (DIDX / DATA chunks) -> {id: bytes}"""
    out, i, didx, data = {}, 0, None, None
    e = None
    while i + 8 <= len(b):
        tag = b[i:i + 4]
        if e is None:   # (the first chunk is BKHD; its size tells the byte order)
            e = "<" if struct.unpack_from("<I", b, i + 4)[0] < len(b) else ">"
        n = struct.unpack_from(e + "I", b, i + 4)[0]
        if tag == b"DIDX": didx = b[i + 8:i + 8 + n]
        elif tag == b"DATA": data = b[i + 8:i + 8 + n]
        i += 8 + n
    if didx and data is not None:
        for k in range(0, len(didx) - 11, 12):
            mid, off, size = struct.unpack_from(e + "3I", didx, k)
            out[mid] = data[off:off + size]
    return out

class Disc:
    """every Wwise media file on the disc, by id: in packages (.pck: streamed .wem and banks' own media) or loose"""
    def __init__(self, folder):
        self.where = {}      # id -> (how, path, entry)
        self.packages = []
        for root, _, files in os.walk(folder):
            for f in files:
                p = os.path.join(root, f)
                with open(p, "rb") as h: magic = h.read(4)
                if magic == b"AKPK":
                    pk = akpk.Package(p); self.packages.append((p, pk))
                    for e in pk.files:
                        if e[0] == "wem": self.where.setdefault(e[1], ("pck", pk, e))
                        else:
                            for mid in bank_media(pk.read(e)): self.where.setdefault(mid, ("bnk-in-pck", pk, e))
                elif magic == b"BKHD":
                    for mid in bank_media(open(p, "rb").read()): self.where.setdefault(mid, ("bnk", p, None))
                elif f.lower().endswith(".wem") and os.path.splitext(f)[0].split("_")[-1].isdigit():
                    self.where.setdefault(int(os.path.splitext(f)[0].split("_")[-1]), ("loose", p, None))

    def read(self, mid):
        how, a, e = self.where[mid]
        if how == "pck": return a.read(e)
        if how == "bnk-in-pck": return bank_media(a.read(e))[mid]
        if how == "bnk": return bank_media(open(a, "rb").read())[mid]
        return open(a, "rb").read()

# ---- making one clip ----------------------------------------------------------------------------------------------
def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, **kw)
    if r.returncode != 0: raise RuntimeError("%s failed: %s" % (os.path.basename(cmd[0]), r.stderr[-400:]))
    return r

def wem_to_wav(T, wem, work, mid):
    """a .wem -> its PCM as the game plays it (vgmstream), 16-bit wav; cached in <work>/wav"""
    out = os.path.join(work, "wav", "%d.wav" % mid)
    if not os.path.exists(out):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        src = os.path.join(work, "wem", "%d.wem" % mid); os.makedirs(os.path.dirname(src), exist_ok=True)
        open(src, "wb").write(wem)
        if wem[:4] == b"OggS":
            # (a plain Ogg Vorbis file under a media id: only the self-test's made-up disc has these -- selftest.py)
            run([T["ffmpeg"], "-nostdin", "-loglevel", "error", "-y", "-i", src, "-ac", "1", out])
        else:
            if not T["vgmstream-cli"]: raise RuntimeError("vgmstream-cli not found (PATH, or set VGMSTREAM_CLI)")
            run([T["vgmstream-cli"], "-o", out, src])
        os.remove(src)
    return out

def encode(T, src, af, rate_args, extra_in=()):
    fd, tmp = tempfile.mkstemp(suffix=".ogg"); os.close(fd)
    try:
        run([T["ffmpeg"], "-nostdin", "-loglevel", "error", "-y", *extra_in, "-i", src, "-ac", "1", "-af", af, *rate_args,
             "-c:a", "libvorbis", "-q:a", "0", tmp])
        return open(tmp, "rb").read()
    finally:
        os.remove(tmp)

def pcm(T, src, af="anull"):
    raw = run([T["ffmpeg"], "-nostdin", "-loglevel", "error", "-i", src, "-ac", "1", "-af", af, "-ar", str(RATE),
               "-f", "s16le", "-"]).stdout
    return np.frombuffer(raw, dtype=np.int16).astype(np.float64)

def write_wav(path, x):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
        w.writeframes(np.clip(np.round(x), -32768, 32767).astype("<i2").tobytes())

def recipe_info():
    return json.load(open(os.path.join(RECIPE, "recipe.json"), encoding="utf-8"))

def finish(ogg, c):
    """our encode with the recipe's comment header and stream serial: identical bytes whenever the audio is
    (commentary2: the comment packet, the same for every clip, is recipe.json's comment_packet)"""
    return oggpk.replace_comment(ogg, bytes.fromhex(c.get("comment") or recipe_info()["comment_packet"]), int(c["serial"]))

def judge(T, ogg, c):
    # (commdisc 2026-11-18: the canonical clip is the native builder's from the disc, disc_sha1; the old developer clip, sha1, too)
    if hashlib.sha1(ogg).hexdigest() in (c.get("disc_sha1"), c["sha1"]): return "identical"
    samples = oggpk.packets(ogg)[-1][1]
    fd, tmp = tempfile.mkstemp(suffix=".ogg"); os.close(fd)
    try:
        open(tmp, "wb").write(ogg); x = pcm(T, tmp) / 32768
    finally:
        os.remove(tmp)
    rms = 10 * np.log10(np.mean(x ** 2) + 1e-12)
    # (a Vorbis block at q0 / 20 kHz is up to 2048 samples; the trim's edge can move by about that much)
    if abs(samples - int(c["samples"])) <= 4096 and abs(rms - float(c["rms"])) <= RMS_TOL: return "equivalent"
    return "wrong"

def make_clip(T, c, disc, work):
    kind = c["kind"]
    if kind == "w13":
        mid = int(c["source"])
        if mid not in disc.where: return None, "missing"
        ogg = encode(T, wem_to_wav(T, disc.read(mid), work, mid), TRIM, [])
    elif kind == "cut":
        if "?" in (c["source"], c["start"], c["gain"]): return None, "missing"
        g, n = c["source"].split(":")
        if g != "w13": return None, "missing"       # (a piece of a Legends line: Legends is left out for now)
        mid = int(n)
        if mid not in disc.where: return None, "missing"
        wav = wem_to_wav(T, disc.read(mid), work, mid)
        # (commdisc 2026-11-18: the pieces were cut from the whole line, start counted from the trimmed start -- cutfit.apply_exact)
        full, x = pcm(T, wav), pcm(T, wav, TRIM)
        first = next((i for i in range(len(full) - len(x) + 1) if np.array_equal(full[i:i + 64], x[:64])), 0)
        piece = cutfit.apply_exact(full, first, int(c["start"]), int(c["length"]), float(c["gain"]),
                                   int(c.get("fade_in", 0) or 0), int(c.get("fade_out", 0) or 0))
        p = os.path.join(work, "cut.wav"); write_wav(p, piece)
        ogg = encode(T, p, "anull", ["-ar", str(RATE)]); os.remove(p)
    else:
        return None, "missing"
    ogg = finish(ogg, c)
    return ogg, judge(T, ogg, c)

# ---- commands -----------------------------------------------------------------------------------------------------
def cmd_check():
    for k, v in tools().items(): print("%-14s %s" % (k, "found" if v else "NOT FOUND"))

def cmd_scan(a):
    T = tools()
    work = a.work or tempfile.mkdtemp(prefix="commentary_scan_")
    disc = Disc(disc_folder(a.disc, work, T))
    _, _, clips = load_recipe()
    need = [int(c["source"]) for c in clips if c["kind"] == "w13"]
    need += [int(c["source"].split(":")[1]) for c in clips if c["kind"] == "cut" and c["source"].startswith("w13:")]
    have = sum(1 for m in set(need) if m in disc.where)
    print("packages:", len(disc.packages))
    for p, pk in disc.packages:
        print("  %s  %s-endian  %d files  languages %s" % (os.path.basename(p), "little" if pk.e == "<" else "big",
                                                          len(pk.files), pk.languages))
    print("media on the disc:", len(disc.where))
    print("WWE '13 media the recipe needs: %d, found %d" % (len(set(need)), have))

def cmd_build(a):
    low_priority()
    T = tools()
    if not T["ffmpeg"]: raise SystemExit("ffmpeg not found (PATH, or set FFMPEG)")
    if not T["vgmstream-cli"]: print("note: vgmstream-cli not found (PATH, or set VGMSTREAM_CLI): Wwise clips will fail")
    work = a.work or os.path.join(os.path.dirname(os.path.abspath(a.out)), "commentary_work")
    os.makedirs(work, exist_ok=True)
    disc = Disc(disc_folder(a.disc, work, T))
    lines, moves, clips = load_recipe()
    got, status = {}, {}
    for k, c in enumerate(clips):
        try:
            ogg, st = make_clip(T, c, disc, work)
        except RuntimeError as e:
            ogg, st = None, "wrong"; print("clip %s: %s" % (c["clip"], e))
        status[int(c["clip"])] = st
        if st in ("identical", "equivalent"): got[int(c["clip"])] = ogg
        if k % 250 == 0: print("%d / %d clips" % (k, len(clips)), flush=True)
    # the pack: the clips we have, renumbered; their rows only
    order = sorted(got); new = {o: i for i, o in enumerate(order)}
    rows = []
    for l in lines.splitlines():
        f = l.split("\t")
        if int(f[0]) in new: rows.append("\t".join([str(new[int(f[0])])] + f[1:]))
    blob = build("".join(r + "\n" for r in rows), moves, [got[o] for o in order])
    nparts, sha = write_parts(blob, a.out)
    cnt = {}
    for st in status.values(): cnt[st] = cnt.get(st, 0) + 1
    exp = recipe_info()["target_sha1"]
    print("clips:", cnt)
    print("pack: %d clips, %d lines, %d parts, sha1 %s (%s the recipe's)" % (len(order), len(rows), nparts, sha,
                                                                         "the same as" if sha == exp else "not"))
    json.dump(status, open(a.out + ".rebuild.json", "w"))
    if not a.keep_work and not a.work: shutil.rmtree(work, ignore_errors=True)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("check")
    s = sub.add_parser("scan"); s.add_argument("disc"); s.add_argument("--work")
    b = sub.add_parser("build"); b.add_argument("disc"); b.add_argument("out")
    b.add_argument("--work"); b.add_argument("--keep-work", action="store_true")
    a = ap.parse_args()
    if a.cmd == "check": cmd_check()
    elif a.cmd == "scan": cmd_scan(a)
    elif a.cmd == "build": cmd_build(a)
    else: ap.print_help()

if __name__ == "__main__":
    main()
