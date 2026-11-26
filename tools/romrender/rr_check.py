"""rr_check.py (romrender, 2026-11-18): proves the C++ twin (cpp/rr_core.cpp) byte-identical to the Python reference (rr.py).

    python rr_check.py build <out.exe> [<clang++>] [-O0|-O2]   compile the twin (the toolchain's clang, no floating point used)
    python rr_check.py run <rr_core.exe> <dump folder>          replay every job a run left there (RR_DUMP_DIR=<folder> while
                                                                any producer runs: the chain's wmport / imports steps) and
                                                                compare each result with the Python one, byte for byte;
                                                                plus the integer sine table and square roots
Exit 1 on any difference."""
import glob, os, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rr

def build(out, cxx=None, opt="-O2"):
    if cxx is None:
        cxx = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE)))), "toolchain", "llvm-mingw", "bin",
                           "x86_64-w64-mingw32-clang++.exe")
    cmd = [cxx, "-std=c++17", opt, "-ffp-contract=off", "-Wall", "-Wextra", "-static", "-o", out, "-DRR_MAIN", os.path.join(os.path.dirname(os.path.dirname(HERE)), "exe", "WWFNoMercyRecomp", "src", "romimport", "rr_core.cpp")]
    r = subprocess.run(cmd); return r.returncode

def trig_bytes():
    import struct
    a = b"".join(struct.pack("<i", rr.isin(x)) for x in range(65536))
    b = b"".join(struct.pack("<q", rr.isqrt(k * k * 977 + k * 31 + 7)) for k in range(65536))
    return a + b

def run(exe, folder):
    bad = n = 0
    with tempfile.TemporaryDirectory() as td:
        j = os.path.join(td, "trig.job"); o = os.path.join(td, "trig.out")
        open(j, "wb").write(b"RRJ1" + rr._words([3]))
        subprocess.run([exe, j, o], check=True)
        same = open(o, "rb").read() == trig_bytes(); n += 1; bad += not same
        print("trig (isin 65536, isqrt 65536): %s" % ("SAME" if same else "DIFFERENT"))
        kinds = {}
        for job in sorted(glob.glob(os.path.join(folder, "*.job"))):
            out = os.path.join(td, "x.out")
            r = subprocess.run([exe, job, out])
            ok = r.returncode == 0 and open(out, "rb").read() == open(job[:-4] + ".out", "rb").read()
            k = os.path.basename(job).split("_")[0]
            kinds.setdefault(k, [0, 0]); kinds[k][0] += 1; kinds[k][1] += not ok
            n += 1; bad += not ok
            if not ok: print("DIFFERENT", os.path.basename(job))
        for k in sorted(kinds): print("%-9s %5d jobs, %d different" % (k, kinds[k][0], kinds[k][1]))
    print("ALL SAME (%d)" % n if not bad else "%d of %d DIFFERENT" % (bad, n))
    return 1 if bad else 0

if __name__ == "__main__":
    if sys.argv[1] == "build": sys.exit(build(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 and not sys.argv[3].startswith("-") else None,
                                              sys.argv[-1] if sys.argv[-1].startswith("-O") else "-O2"))
    sys.exit(run(sys.argv[2], sys.argv[3]))
