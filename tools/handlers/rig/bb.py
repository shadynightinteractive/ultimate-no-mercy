# bb.py (modcheck, 2026-11-13: imports2's, its states read from imports2's cap, its pictures written here)
# bb.py (imports2, 2026-11-13): a WarZone Badd Blood session in the project's libretro rig (rig\harness\n64.py, as the
# handlers' rv.py drives Revenge), writing only into the rig folder (handlers RIG: cap\, sys_bb\). The ROM is read in place, never copied.
#   python bb.py <from> steps...      from: "boot" or a state name in cap\
#   A / START / DOWN ...  press (5 frames held, 45 after);  w60 wait 60 frames;  H:A:30 hold A 30 frames
#   shot:name  -> cap\name.png;  save:name -> cap\name.st + cap\name_ram.bin
#   poke:addr:value  a u32 written;  P2:A  pad 2
import os, sys, pickle, ctypes
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG     # (the Development folder; where the captures go: handlers/__init__.py -- 2026-11-13,
                                  #  final082: in imports2's folder these wrote beside themselves)
os.makedirs(os.path.join(RIG, "cap"), exist_ok=True)
sys.path.insert(0, DEV); sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
if os.name == "nt":      # (the machine is shared: this session takes spare CPU only -- BELOW_NORMAL_PRIORITY_CLASS)
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
ROM = os.environ.get("BBROM") or os.path.join(DEV, "roms", "WWF WarZone - Badd Blood (USA) (v1.0).z64")
M = n64.N64(ROM, os.path.join(RIG, os.environ.get("BBSYS", "sys_bb")))
CAP = os.path.join(RIG, "cap")
I2CAP = os.path.join(DEV, "scratch", "agents", "imports2", "rig", "cap")   # (modcheck: imports2's states, read only)
M.run(1)
src = sys.argv[1]
if src != "boot":
    st_path = src if src.endswith(".st") else os.path.join(CAP if os.path.exists(os.path.join(CAP, src + ".st")) else I2CAP, src + ".st")
    M.load(pickle.load(open(st_path, "rb"))); M.run(1)
for st in sys.argv[2:]:
    if st.startswith("w") and st[1:].isdigit(): M.run(int(st[1:])); continue
    if st.startswith("shot:"): M.shot(os.path.join(CAP, st[5:] + ".png")); print("shot", st[5:], M.frames, flush=True); continue
    if st.startswith("save:"):
        t = st[5:]
        pickle.dump(M.save(), open(os.path.join(CAP, t + ".st"), "wb"))
        open(os.path.join(CAP, t + "_ram.bin"), "wb").write(bytes(M.dump()[:0x800000]))
        print("saved", t, M.frames, flush=True); continue
    if st.startswith("ram:"):
        open(os.path.join(CAP, st[4:] + "_ram.bin"), "wb").write(bytes(M.dump()[:0x800000])); print("ram", st[4:], flush=True); continue
    if st.startswith("pokeb:"):          # pokeb:addr:hexbytes -> bytes written (a record's packed looks)
        _, a, hx = st.split(":"); a = int(a, 16)
        for i, b in enumerate(bytes.fromhex(hx)): M.w8(a + i, b)
        continue
    if st.startswith("poke:"):
        _, a, v = st.split(":"); M.w32(int(a, 16), int(v, 16)); continue
    if st.startswith("reload"):          # (after pokes into code: the core's cached code thrown away, as wmcap.py does)
        M.load(M.save()); continue
    if st.startswith("H:"):
        _, b, n = st.split(":"); M.run(int(n), 0, (b,)); continue
    if st.startswith("P2:"):
        M.press(st[3:], port=1, hold=5, after=45); continue
    if st.startswith("RS:"):
        _, x, y = st.split(":"); M.rstick = [(int(x), int(y))] + [(0, 0)] * 3; M.run(5); M.rstick = [(0, 0)] * 4; M.run(45); continue
    M.press(st, hold=5, after=45)
print("frames", M.frames)
