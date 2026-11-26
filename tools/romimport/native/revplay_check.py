"""revplay_check.py (revmusic, 2026-11-18): proves the C++ player (revplay.cpp) and the Python reference (mod\\gen\\revplay.py)
give the same theme sample, byte for byte, from the player's Revenge ROM (roms\\, by its fingerprint).

    python revplay_check.py [<song file, hex> ...]        (default: 214)
    python revplay_check.py --redux                       (Revenge Redux's themes, decision C: the clip of every song
                                                           gen/reduxmusic.py gives a Superstar, from the Redux ROM)

Builds revplay.cpp with REVPLAY_MAIN (clang++ / g++ on the PATH, or the toolchain's llvm-mingw) into a temporary folder,
runs both, compares the PCM, the loop start and the rate. Prints SAME / DIFFERENT per song; exit 1 when any differs."""
import hashlib, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
sys.path.insert(0, os.path.join(SRC, "mod", "gen")); sys.path.insert(0, os.path.dirname(HERE))
import revplay
from roms import Roms

def compiler():
    for c in ("clang++", "g++"):
        p = shutil.which(c)
        if p: return p
    p = os.path.join(SRC, "toolchain", "llvm-mingw", "bin", "clang++.exe")
    if os.path.exists(p): return p
    raise SystemExit("no C++ compiler (clang++ / g++)")

def main():
    if sys.argv[1:] == ["--redux"]:
        import reduxmusic
        rom = reduxmusic.redux_rom()
        if rom is None: raise SystemExit("no WCW/nWo Revenge Redux in roms\\ (the patched ROM, or Revenge + the Redux xdelta)")
        songs = sorted({s for c in reduxmusic.theme_list(rom) for s in [reduxmusic.song_for(rom, c)] if s})
        clip = True
    else:
        songs = [int(a, 16) for a in sys.argv[1:]] or [0x214]
        clip = False
        rom = Roms().rom("revenge")
        if rom is None: raise SystemExit("WCW/nWo Revenge's ROM is not in roms\\")
    tmp = tempfile.mkdtemp(prefix="revplay_")
    bad = 0
    try:
        exe = os.path.join(tmp, "revplay.exe")
        subprocess.check_call([compiler(), "-O2", "-std=c++17", "-DREVPLAY_MAIN", "-o", exe, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "exe", "WWFNoMercyRecomp", "src", "romimport", "revplay.cpp")])
        rp = os.path.join(tmp, "rom.z64")
        open(rp, "wb").write(rom)
        for f in songs:
            out = os.path.join(tmp, "%04x.pcm" % f)
            line = subprocess.check_output([exe, rp, ("clip:%x" if clip else "%x") % f, out], text=True).strip()
            c = open(out, "rb").read()
            facts = dict(zip(line.split()[0::2], line.split()[1::2]))
            y, ls, rate, info = reduxmusic.theme(rom, f) if clip else revplay.theme(rom, f)
            p = y.astype("<i2").tobytes()
            same = c == p and int(facts["loop"]) == ls and float(facts["rate"]) == rate
            bad += not same
            print("%s  song 0x%04X: %d samples, loop from %d, rate %r, md5 %s (C++ %s)" % (
                "SAME     " if same else "DIFFERENT", f, len(y), ls, rate, hashlib.md5(p).hexdigest(), hashlib.md5(c).hexdigest()))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
