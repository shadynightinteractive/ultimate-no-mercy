#!/usr/bin/env python3
# storycmp.py: both story compilers -- gen/storyc.py and the exe's arcade_stories.cpp (built alone as
# build/storytest/story_test.exe, again when its source is newer) -- on every story: the same bytes? (or both
# refusing it). build_mod.ps1 runs it; a difference stops the build.
#   python mod/gen/storycmp.py [story.json ...]     (none: mod\stories, qa\stories_test, the game's stories folder)
import os, sys, glob, json, subprocess, shutil
HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import storyc

OUT = os.path.join(DEV, "mod", "build", "storytest")
CPP = os.path.join(DEV, "exe", "WWFNoMercyRecomp", "src", "main", "arcade_stories.cpp")
DRV = os.path.join(HERE, "story_test.cpp")
EXE = os.path.join(OUT, "story_test.exe")

def build_exe():
    if os.path.exists(EXE) and os.path.getmtime(EXE) > max(os.path.getmtime(CPP), os.path.getmtime(DRV)): return
    cxx = os.path.join(DEV, "toolchain", "llvm-mingw", "bin", "x86_64-w64-mingw32-clang++.exe")
    inc = os.path.join(DEV, "exe", "WWFNoMercyRecomp", "lib", "N64ModernRuntime", "thirdparty")   # (the json.hpp the exe builds with)
    print("storycmp: building story_test.exe (the exe's compiler alone)")
    r = subprocess.run([cxx, "-std=c++20", "-O1", "-Wno-deprecated-literal-operator", "-I", inc, DRV, "-o", EXE])
    if r.returncode: raise SystemExit("storycmp: story_test.exe didn't build")

def py_pack(path):
    try:
        st = json.load(open(path, encoding="utf-8"))
        storyc.check(st, os.path.basename(path))
        return storyc.Pack(st).build(), None
    except SystemExit as e: return None, str(e)
    except Exception as e: return None, "%s: %s" % (type(e).__name__, e)

def main(files):
    os.makedirs(OUT, exist_ok=True)
    build_exe()
    roster = os.path.join(OUT, "_roster.json")
    subprocess.run([sys.executable, os.path.join(HERE, "storyc.py"), "--roster", roster], check=True, stdout=subprocess.DEVNULL)
    shutil.copy(storyc.voices_file(), os.path.join(OUT, "_voices.json"))
    if not files:
        for d in (os.path.join(DEV, "mod", "stories"), os.path.join(DEV, "qa", "stories_test"), os.path.join(DEV, "..", "stories")):
            files += [f for f in sorted(glob.glob(os.path.join(d, "*.json"))) if not os.path.basename(f).startswith("_") and os.path.basename(f) != "story.schema.json"]
    bad = 0
    for f in files:
        name = os.path.basename(f)
        data, err = py_pack(f)
        out = os.path.join(OUT, "cpp.pack")
        if os.path.exists(out): os.remove(out)
        r = subprocess.run([EXE, roster, f, out], capture_output=True, text=True)
        cdata = open(out, "rb").read() if r.returncode == 0 and os.path.exists(out) else None
        if data is None and cdata is None: print("  %-36s both refuse it: %s" % (name, err)); continue
        if data is None or cdata is None or data != cdata:
            bad += 1
            print("  %-36s DIFFERENT: py %s / exe %s" % (name, "%d bytes" % len(data) if data else err,
                                                     "%d bytes" % len(cdata) if cdata else r.stderr.strip()))
        else: print("  %-36s same bytes (%d)" % (name, len(data)))
    print("storycmp: %d stories, %d different" % (len(files), bad))
    return bad

if __name__ == "__main__":
    sys.exit(1 if main(sys.argv[1:]) else 0)
