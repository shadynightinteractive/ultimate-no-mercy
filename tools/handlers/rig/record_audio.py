# record_audio.py (imports2, 2026-11-13): <state> <out.raw> steps...: the rig's sound (stereo s16, the core's rate) while the
# steps run -- to hear at what rate Badd Blood plays a theme's samples (its bank's "tune" is WrestleMania 2000's, -72).
import os, sys, pickle, ctypes
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG     # (the Development folder; where the captures go: handlers/__init__.py -- 2026-11-13,
                                  #  final082: in imports2's folder these wrote beside themselves)
os.makedirs(os.path.join(RIG, "cap"), exist_ok=True)
sys.path.insert(0, DEV); sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
if os.name == "nt": ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
M = n64.N64(os.path.join(DEV, "roms", "WWF WarZone - Badd Blood (USA) (v1.0).z64"), os.path.join(RIG, "sys_bb"))
M.run(1); M.load(pickle.load(open(os.path.join(RIG, "cap", sys.argv[1] + ".st"), "rb"))); M.run(1)
M.audio_rec = True
for st in sys.argv[3:]:
    if st.startswith("w"): M.run(int(st[1:])); continue
    M.press(st, hold=5, after=45)
open(sys.argv[2], "wb").write(b"".join(M.audio)); print("frames", M.frames, "bytes", sum(len(a) for a in M.audio))
