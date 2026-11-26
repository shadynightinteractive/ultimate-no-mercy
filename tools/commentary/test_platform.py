"""Same native code built for Windows (llvm-mingw, UCRT libm) and Linux (gcc, glibc libm): same bytes?
step 1: python -I cmp_platform.py make <clips> <count> <outdir>   (raw inputs + jobs.txt)
step 2: run both exes over jobs.txt (win here; linux in WSL: run_jobs.sh)
step 3: python -I cmp_platform.py cmp <outdir>"""
import os, sys, subprocess, struct, random

def make(d, count, out):
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(7)
    fs = sorted(f for f in os.listdir(d) if f.endswith(".ogg")); rnd.shuffle(fs)
    jobs = []
    for k, f in enumerate(fs[:count]):
        raw = subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-i", os.path.join(d, f), "-ac", "1", "-f", "s16le", "-"],
                             capture_output=True, check=True).stdout
        x = list(struct.unpack("<%dh" % (len(raw) // 2), raw))
        y = [0] * rnd.randint(0, 2000) + [rnd.randint(-80, 80) for _ in range(rnd.randint(0, 1500))] + x + \
            [rnd.randint(-120, 120) for _ in range(rnd.randint(0, 1500))]
        open(os.path.join(out, "%04d.raw" % k), "wb").write(struct.pack("<%dh" % len(y), *y))
        jobs.append("%04d %d %d" % (k, 20000 if k % 3 else 22050, rnd.randint(0, 2**32 - 1)))
    open(os.path.join(out, "jobs.txt"), "w", newline="\n").write("\n".join(jobs) + "\n")

def cmp(out):
    same = diff = 0
    for l in open(os.path.join(out, "jobs.txt")):
        k = l.split()[0]
        a = open(os.path.join(out, k + ".win.ogg"), "rb").read(); b = open(os.path.join(out, k + ".lin.ogg"), "rb").read()
        if a == b: same += 1
        else: diff += 1; print("differ", k, len(a), len(b))
    print("same %d, different %d" % (same, diff))

if __name__ == "__main__":
    if sys.argv[1] == "make": make(sys.argv[2], int(sys.argv[3]), sys.argv[4])
    else: cmp(sys.argv[2])
