"""Where everything lives in the Development folder.

The scripts were written in a Linux sandbox and name their files by sandbox-style paths
(/home/devbox/work/..., /home/devbox/src/..., /home/devbox/nmplus/..., /tmp/devbox-0/...).
They keep those names; nmp() turns one into the real place here, so this table is the only
thing that knows the folder layout. (2026-11-24: the scripts' prefixes were renamed to /home/devbox
and /tmp/devbox-0, the same length. A name under the sandbox user's earlier name, as the sandbox's
own data files still hold, means the same place: /home/<any user>/ is /home/devbox/ and
/tmp/<any user>-0/ is /tmp/devbox-0/.)

    from nmpaths import nmp
    open(nmp("/home/devbox/work/gen_data/wm2k_ram.bin"), "rb")

A path no entry covers is returned as it is (an ordinary Windows path passes straight through).
Paths come back with forward slashes (Windows takes them, and the scripts split paths on "/").
A base kept in a variable still works when added to: nmp("/home/devbox/work/") + "gen_data/x" is
nmp("/home/devbox/work/gen_data/x") (the result remembers its sandbox name for that).

Importing this also puts toolchain\\ first on PATH, so the tools the scripts run (llvm-nm, ffmpeg,
...) are this folder's own.
"""
import os, re

ROOT = os.path.dirname(os.path.abspath(__file__)).replace("\\", "/")

def _r(*p): return "/".join((ROOT,) + p)

# (NM_MOD_DIR: another copy of the mod folder -- build_mod.ps1 -ModDir sets it, so the generators read and write that copy,
#  never the real mod\. Without it, the real mod\ as always. 2026-11-08)
_MOD = os.environ.get("NM_MOD_DIR", "").replace("\\", "/").rstrip("/") or None
def _mod(*p): return "/".join((_MOD,) + p) if _MOD else _r("mod", *p)

# Longest prefix first (checked in this order).
MAP = [
    # the sandbox's work folder
    ("/home/devbox/work/arcade/build",      _mod("build")),
    ("/home/devbox/work/arcade",            _mod()),
    ("/home/devbox/work/harness",           _r("rig", "harness")),
    ("/home/devbox/work/gen_data/music",    _r("mod", "music")),
    ("/home/devbox/work/gen_data",          _r("data", "gen_data")),
    ("/home/devbox/work/six",               _r("six-engine", "work")),
    ("/home/devbox/work/nmport",            _r("data", "nmport")),
    ("/home/devbox/work/announce",          _r("data", "announce")),
    ("/home/devbox/work/commentary",        _r("exe", "commentary-sim")),
    ("/home/devbox/work/port-patches",      _r("exe", "patches")),
    ("/home/devbox/work/revport",           _r("data", "revport")),
    ("/home/devbox/work/wmport",            _r("data", "wmport")),
    ("/home/devbox/work/sys",               _r("rig", "sys")),
    ("/home/devbox/work/states",            _r("rig", "states")),
    ("/home/devbox/work/nomercy.z64",       _r("roms", "WWF No Mercy (USA) (Rev 1).z64")),
    ("/home/devbox/work",                   _r("scratch")),       # anything else the sandbox kept there
    # the sandbox's source checkouts
    ("/home/devbox/src/parallel-n64",       _r("rig", "core")),
    ("/home/devbox/src/WWFNoMercyRecomp",   _r("exe", "WWFNoMercyRecomp")),
    ("/home/devbox/src/NoMercySix",         _r("six-engine", "NoMercySix")),
    ("/home/devbox/src/N64RecompSource",    _r("libs", "N64Recomp")),
    ("/home/devbox/src",                    _r("libs")),
    # No Mercy Plus V3, unpacked
    ("/home/devbox/nmplus",                 _r("data", "nmplus")),
    # the sandbox's scratch folder: the extracted game files, other games' ROMs, commentary audio
    ("/tmp/devbox-0/nmfiles",               _r("data", "extracted", "nmfiles")),
    ("/tmp/devbox-0/rev_files",             _r("data", "extracted", "rev_files")),
    ("/tmp/devbox-0/wm2k_files",            _r("data", "extracted", "wm2k_files")),
    ("/tmp/devbox-0/wm2k_work",             _r("data", "extracted", "wm2k_work")),
    ("/tmp/devbox-0/keep",                  _r("data", "extracted", "keep")),
    ("/tmp/devbox-0/sdk",                   _r("data", "extracted", "sdk")),
    ("/tmp/devbox-0/spk",                   _r("data", "extracted", "spk")),
    ("/tmp/devbox-0/rev_bm_ram.bin",        _r("data", "extracted", "rev_bm_ram.bin")),
    ("/tmp/devbox-0/wm2k",                  _r("roms", "wm2k")),
    ("/tmp/devbox-0/rev",                   _r("roms", "revenge")),
    ("/tmp/devbox-0/wwe13",                 _r("data", "commentary_audio", "wwe13")),
    ("/tmp/devbox-0/lowm",                  _r("data", "commentary_audio", "lowm")),
    ("/tmp/devbox-0",                       _r("scratch", "tmp")),
]

_tc = os.path.join(ROOT, "toolchain")
os.environ["PATH"] = os.pathsep.join([os.path.join(_tc, d) for d in
    ("python", "llvm-mingw/bin", "zig", "cmake/bin", "ninja", "ffmpeg", "bin")] + [os.environ.get("PATH", "")])

class SPath(str):
    """The real path (the str itself), remembering the sandbox path it came from."""
    def __new__(cls, real, sandbox):
        s = str.__new__(cls, real); s.sandbox = sandbox; return s
    def __add__(self, other):
        if isinstance(other, str): return nmp(self.sandbox + other)
        return NotImplemented

# The sandbox's own files (keep.json, the candidate lists, ...) still hold its paths under the sandbox user's earlier
# name: any user name in /home/<name>/... or /tmp/<name>-0/... means the same folders as devbox's.
_HOME = re.compile(r"^/home/[^/]+(?=/|$)")
_TMP = re.compile(r"^/tmp/[^/]+-0(?=/|$)")

def _resolve(p):
    q = _TMP.sub("/tmp/devbox-0", _HOME.sub("/home/devbox", p, 1), 1)
    for old, new in MAP:
        if q == old or q.startswith(old + "/"):
            rest = q[len(old):].lstrip("/")
            out = new + "/" + rest if rest else new
            if q.endswith("/") and not out.endswith("/"): out += "/"
            return out
    return p

def nmp(path):
    """A sandbox path -> the real path here (with the same trailing slash)."""
    p = getattr(path, "sandbox", None) or str(path)
    return SPath(_resolve(p), p)

if __name__ == "__main__":
    import sys
    for a in sys.argv[1:]: print(nmp(a))
