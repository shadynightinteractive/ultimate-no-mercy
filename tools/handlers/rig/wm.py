# wm.py (wm2kall, 2026-11-13): a WWF WrestleMania 2000 session in the project's libretro rig (rig\harness\n64.py), as bb.py
# drives Badd Blood -- the same steps -- with two differences: the ROM is read straight out of the user's archive
# (roms\WWF_WrestleMania_2000.zip, in memory: nothing is unpacked to disk), and the session writes only into the rig folder
# (handlers RIG: cap\, sys_wm\).
#   python wm.py <from> steps...      from: "boot" or a state name in cap\
#   A / START / DOWN ...  press (5 frames held, 45 after);  w60 wait 60 frames;  H:A:30 hold A 30 frames
#   shot:name  -> cap\name.png;  save:name -> cap\name.st + cap\name_ram.bin;  ram:name -> cap\name_ram.bin only
#   poke:addr:value  a u32 written;  pokeb:addr:hex  bytes written;  P2:A  pad 2;  P3:A, P4:A  pads 3 and 4
import io, os, sys, pickle, ctypes, time, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from handlers import DEV, RIG
os.makedirs(os.path.join(RIG, "cap"), exist_ok=True)
sys.path.insert(0, DEV); sys.path.insert(0, os.path.join(DEV, "rig", "harness"))
if os.name == "nt":      # (the machine is shared: this session takes spare CPU only -- BELOW_NORMAL_PRIORITY_CLASS)
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = os.environ.get("WMCORE", "cached_interpreter").encode()
ZIP = os.environ.get("WMROM") or os.path.join(DEV, "roms", "WWF_WrestleMania_2000.zip")

def rom_bytes(path=ZIP):
    """the ROM image: a .z64 read as it is, or the one .z64 inside a .zip, read in memory"""
    if path.lower().endswith(".zip"):
        z = zipfile.ZipFile(path)
        name = [n for n in z.namelist() if n.lower().endswith((".z64", ".n64", ".v64"))][0]
        return z.read(name)
    return open(path, "rb").read()

# (n64.N64 reads its ROM with open(path).read(): handed a file-like of the bytes instead, the archive is never unpacked)
_real_open = open
def _open(p, *a, **k):
    return io.BytesIO(rom_bytes(p)) if p == ZIP else _real_open(p, *a, **k)
n64.open = _open

_M = None
def session(src="boot"):
    """the machine, at `src` -- one machine a process (the core loads one game once: a second N64() fails), so a later call
    puts the same machine back to the state asked for"""
    global _M
    if _M is None:
        _M = n64.N64(ZIP, os.path.join(RIG, os.environ.get("WMSYS", "sys_wm")))
        _M.run(1)
        _M.boot = pickle.dumps(_M.save())
    M = _M
    if src == "boot":
        M.load(pickle.loads(M.boot)); M.run(1)
    if src != "boot":
        st_path = src if src.endswith(".st") else os.path.join(RIG, "cap", src + ".st")
        M.load(pickle.load(_real_open(st_path, "rb"))); M.run(1)
    return M

def step(M, st):
    CAP = os.path.join(RIG, "cap")
    if st.startswith("w") and st[1:].isdigit(): M.run(int(st[1:])); return
    if st.startswith("shot:"): M.shot(os.path.join(CAP, st[5:] + ".png")); print("shot", st[5:], M.frames, flush=True); return
    if st.startswith("save:"):
        t = st[5:]
        pickle.dump(M.save(), _real_open(os.path.join(CAP, t + ".st"), "wb"))
        _real_open(os.path.join(CAP, t + "_ram.bin"), "wb").write(bytes(M.dump()[:0x800000]))
        print("saved", t, M.frames, flush=True); return
    if st.startswith("ram:"):
        _real_open(os.path.join(CAP, st[4:] + "_ram.bin"), "wb").write(bytes(M.dump()[:0x800000])); print("ram", st[4:], flush=True); return
    if st.startswith("pokeb:"):
        _, a, hx = st.split(":"); a = int(a, 16)
        for i, b in enumerate(bytes.fromhex(hx)): M.w8(a + i, b)
        return
    if st.startswith("poke:"):
        _, a, v = st.split(":"); M.w32(int(a, 16), int(v, 16)); return
    if st.startswith("reload"):          # (after pokes into code: the core's cached code thrown away, as wmcap.py does)
        M.load(M.save()); return
    if st.startswith("H:"):
        _, b, n = st.split(":"); M.run(int(n), 0, (b,)); return
    if st[:3] in ("P2:", "P3:", "P4:"):
        M.press(st[3:], port=int(st[1]) - 1, hold=5, after=45); return
    M.press(st, hold=5, after=45)

if __name__ == "__main__":
    t0 = time.time()
    M = session(sys.argv[1])
    for st in sys.argv[2:]: step(M, st)
    print("frames", M.frames, "%.1f s" % (time.time() - t0))
