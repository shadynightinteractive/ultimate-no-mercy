"""opens.py (romimport): run a Python tool and list every file it reads -- to see which captures a step still needs.

    python opens.py <log file> <script.py> [args...]
Writes <log file>: each distinct path opened for reading (and how often), in first-open order. The tool runs unchanged."""
import builtins, io, os, runpy, sys, collections

log, script = sys.argv[1], sys.argv[2]
seen = collections.OrderedDict()
_open, _ioopen = builtins.open, io.open
def _note(f, mode):
    if isinstance(f, (str, bytes, os.PathLike)) and "r" in (mode or "r") and "+" not in (mode or ""):
        p = os.path.normcase(os.path.abspath(os.fsdecode(f)))
        seen[p] = seen.get(p, 0) + 1
def op(f, mode="r", *a, **k):
    _note(f, mode); return _open(f, mode, *a, **k)
builtins.open = op
import numpy as _np                          # (np.load / np.fromfile go through their own opens)
_npload = _np.load
def npload(f, *a, **k): _note(f, "rb"); return _npload(f, *a, **k)
_np.load = npload
sys.argv = sys.argv[2:]
sys.path.insert(0, os.path.dirname(os.path.abspath(script)))     # (as python <script> would)
try:
    runpy.run_path(script, run_name="__main__")
finally:
    with _open(log, "w", encoding="utf-8") as w:
        for p, n in seen.items(): w.write("%5d  %s\n" % (n, p))
