"""vcdiff_check.py (reduxthemes, 2026-11-18): proves the C++ VCDIFF decoder (vcdiff.cpp) and the Python one
(tools\\handlers\\handlers\\aki\\vcdiff.py) give the same bytes, on the player's own patches in roms\\ (found by fingerprint,
tools\\romimport\\roms.py): No Mercy Plus V3 (v1.0 + its xdelta) and WCW/nWo Revenge Redux (Revenge + its xdelta, read in
memory from the release's .zip). Each result is also checked against the patched ROM's fingerprint.

    python vcdiff_check.py

Builds exe\WWFNoMercyRecomp\src\romimport\vcdiff.cpp (phase3 moved it there: the game builds it in) with VCDIFF_MAIN (clang++ / g++ on the PATH, or the toolchain's llvm-mingw) into a temporary folder.
Prints SAME / DIFFERENT per patch; exit 1 when any differs."""
import hashlib, os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import roms
from roms import Roms, SHA1, PATCHES, NMPLUS_PATCH, vcdiff
from revplay_check import compiler

def main():
    R = Roms()
    cases = []
    base, patch = R.rom("nomercy10"), R._find(NMPLUS_PATCH)
    if base is not None and patch: cases.append(("No Mercy Plus V3", base, open(patch, "rb").read(), SHA1["nmplus_v3"]))
    b_game, p_sha = PATCHES["revenge_redux"]
    base, patch = R.rom(b_game), R.patch(p_sha)
    if base is not None and patch is not None: cases.append(("WCW/nWo Revenge Redux", base, patch, SHA1["revenge_redux"]))
    if not cases: raise SystemExit("no patch and its base ROM in roms\\")
    tmp = tempfile.mkdtemp(prefix="vcdiff_")
    bad = 0
    try:
        exe = os.path.join(tmp, "vcdiff.exe")
        subprocess.check_call([compiler(), "-O2", "-std=c++17", "-DVCDIFF_MAIN", "-o", exe, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(HERE))), "exe", "WWFNoMercyRecomp", "src", "romimport", "vcdiff.cpp")])
        for name, base, patch, want in cases:
            s, p, o = (os.path.join(tmp, n) for n in ("src.bin", "patch.xdelta", "out.bin"))
            open(s, "wb").write(base); open(p, "wb").write(patch)
            subprocess.check_call([exe, s, p, o], stdout=subprocess.DEVNULL)
            c = open(o, "rb").read()
            py = vcdiff.apply(base, patch)
            same = c == py and hashlib.sha1(py).hexdigest() == want
            bad += not same
            print("%s  %s: %d bytes, SHA-1 %s (C++ %s)" % ("SAME     " if same else "DIFFERENT", name, len(py),
                  hashlib.sha1(py).hexdigest(), hashlib.sha1(c).hexdigest()))
            for f in (s, p, o): os.remove(f)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
