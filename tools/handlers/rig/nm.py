# nm.py (imports2, 2026-11-13): a No Mercy Plus V3 session in the project's libretro rig, as bb.py drives Badd Blood. The rig's
# core opens ROMs of 64 MB at most (its log: "Failed to load ROM" for No Mercy Plus's 68 MB); No Mercy Plus's file table and
# every file end at 45 MB (its table: the last file's end 0x2BBC4D0), so the core is handed the ROM's first 64 MB from memory
# -- nothing is written to disk, the ROM file is read in place as always. Steps: as bb.py (pokeb: bytes into RAM).
import os, sys, pickle, ctypes, io
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG     # (the Development folder; where the captures go: handlers/__init__.py -- 2026-11-13,
                                  #  final082: in imports2's folder these wrote beside themselves)
os.makedirs(os.path.join(RIG, "cap"), exist_ok=True)
sys.path.insert(0, DEV); sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
if os.name == "nt": ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
ROM = os.path.join(DEV, "data", "nmplus", "nmplus.z64")
_real_open = open
def _open(p, mode="r", *a, **k):          # (only the ROM, only for the core: its first 64 MB)
    if p == ROM and "b" in mode: return io.BytesIO(_real_open(p, "rb").read()[:0x4000000])
    return _real_open(p, mode, *a, **k)
n64.open = _open
M = n64.N64(ROM, os.path.join(RIG, "sys_nm"))
CAP = os.path.join(RIG, "cap")
M.run(1)
src = sys.argv[1]
if src != "boot":
    M.load(pickle.load(_real_open(os.path.join(CAP, src + ".st"), "rb"))); M.run(1)
for st in sys.argv[2:]:
    if st.startswith("w") and st[1:].isdigit(): M.run(int(st[1:])); continue
    if st.startswith("shot:"): M.shot(os.path.join(CAP, st[5:] + ".png")); print("shot", st[5:], M.frames, flush=True); continue
    if st.startswith("save:"):
        t = st[5:]; pickle.dump(M.save(), _real_open(os.path.join(CAP, t + ".st"), "wb"))
        _real_open(os.path.join(CAP, t + "_ram.bin"), "wb").write(bytes(M.dump()[:0x800000])); print("saved", t, M.frames, flush=True); continue
    if st.startswith("pokeb:"):
        _, a, hx = st.split(":"); a = int(a, 16)
        for i, b in enumerate(bytes.fromhex(hx)): M.w8(a + i, b)
        continue
    if st.startswith("P2:"):
        M.press(st[3:], port=1, hold=5, after=45); continue
    if st.startswith("H:"):
        _, b, n = st.split(":"); M.run(int(n), 0, (b,)); continue
    if st.startswith("RS:"):
        _, x, y = st.split(":"); M.rstick = [(int(x), int(y))] + [(0, 0)] * 3; M.run(5); M.rstick = [(0, 0)] * 4; M.run(45); continue
    M.press(st, hold=5, after=45)
print("frames", M.frames)
