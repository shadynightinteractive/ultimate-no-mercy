# handlers (2026-11-12): a copy of data/revport/attires/rv.py writing only into this folder (cap/, sys_rev/).
# (modcheck, 2026-11-13: copied from revbody's rig; "p:<state>" states are read from revbody's cap -- read only -- when not
#  here; "pokef:addr:value" writes a float, and "hide" / "show" make every man's model scale 0 / back: a plate)
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# rv.py from_state steps...: a Revenge session (as revport/logcalls.py: its file-read (1) and mesh-load (2)
# hooks) driven by steps:
#   A / START / DOWN ... press;  w60 wait;  P2:A press on pad 2;  H:A:30 hold 30 frames
#   shot:name  -> ports/cap/name.png ; save:name -> ports/cap/name.st (+ _calls.pkl, _ram.bin)
#   mark:label -> prints the files read (hook 1, a1) since the last mark
# from_state: "boot", a revport/cap state name, or "p:name" (a ports/cap state)
import sys, os, pickle
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from handlers import RIG     # (where its states and captures go: handlers/__init__.py -- 2026-11-13, final082)
os.makedirs(os.path.join(RIG, "cap"), exist_ok=True)
sys.path.insert(0, nmp("/home/devbox/work/harness"))
import n64
n64.OPTIONS[b"parallel-n64-cpucore"] = b"cached_interpreter"
M = n64.N64(os.environ.get("REVROM") or nmp("/tmp/devbox-0/rev/WCW-nWo Revenge (USA).z64"), os.path.join(RIG, "sys_rev"))   # (the rig folder's system folder)   # (REVROM: another ROM -- Revenge Redux, attires/)
RC = nmp("/home/devbox/work/revport/cap")
PC = os.path.join(RIG, "cap")   # (the rig folder: its states and captures)
HOOKS = {1: 0x80003684, 2: 0x8000498C}
if os.environ.get("HOOK3"): HOOKS[3] = 0x8000CFD4      # (the model loader: tag = a3 << 8 | 3)
M.run(1)
src = sys.argv[1]
if src != "boot":
    p = PC + "/" + src[2:] + ".st" if src.startswith("p:") else RC + "/" + src + ".st"
    if src.startswith("p:") and not os.path.exists(p):
        p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "revbody", "rig", "cap", src[2:] + ".st")
    M.load(pickle.load(open(p, "rb"))); M.run(1)
S = 0x80700000
def j(a): return 0x08000000 | ((a >> 2) & 0x3FFFFFF)
def hook():
    if M.r32(HOOKS[1]) >> 26 == 2: return       # (already in: a state saved by this script)
    for h, (tag, addr) in enumerate(sorted(HOOKS.items())):
        st = S + 0x100 * h
        o1, o2 = M.r32(addr), M.r32(addr + 4)
        tg = [0x00077A00, 0x35EF0003] if tag == 3 else [0x340F0000 | tag]
        pre = []
        if addr == 0x80003684 and os.environ.get("REMAP"):     # REMAP="x:y,...": file x read as file y (as logcalls.py)
            for xy in os.environ["REMAP"].split(","):
                x, y = (int(v, 16) for v in xy.split(":"))
                pre += [0x30B9FFFF, 0x34180000 | x, 0x17380002, 0, 0x34050000 | y]
        code = pre + [0x3C188070, 0x8F190FFC, 0x332E0FFF, 0x000E7100, 0x01D87021] + tg + [0xADCF1000,
                0xADC41004, 0xADC51008, 0xADC6100C, 0x27390001, 0xAF190FFC, o1, o2, j(addr + 8), 0]
        for i, w in enumerate(code): M.w32(st + 4 * i, w)
        M.w32(addr, j(st)); M.w32(addr + 4, 0)
    M.w32(S + 0xFFC, 0)
OVR = os.environ.get("OVR")
if not os.environ.get("NOHOOK"): hook()      # (NOHOOK=1: no hooks: a clean state to save)
if OVR:
    st = S + 0x400; addr = 0x8000CFD4
    if M.r32(addr) >> 26 != 2:
        o1, o2 = M.r32(addr), M.r32(addr + 4)
        code = [0x3C188070, 0x30990007, 0x0019C8C0, 0x0319C021, 0x8F190F00, 0x13200002, 0, 0x2726FFFF,
                0x8F190F04, 0x13200002, 0, 0x2727FFFF, o1, o2, j(addr + 8), 0]
        for i, w in enumerate(code): M.w32(st + 4 * i, w)
        M.w32(addr, j(st)); M.w32(addr + 4, 0)
    for x in OVR.split(","):
        sl, v, c = (int(y) for y in x.split(":"))
        M.w32(S + 0xF00 + 8 * sl, v + 1 if v >= 0 else 0); M.w32(S + 0xF04 + 8 * sl, c + 1 if c >= 0 else 0)
M.load(M.save())
def calls_since(n0):
    n = M.r32(S + 0xFFC)
    return [tuple(M.r32(S + 0x1000 + 16 * (i & 0xFFF) + 4 * k) for k in range(4)) for i in range(max(n0, n - 4096), n)], n
last = M.r32(S + 0xFFC)
for st in sys.argv[2:]:
    if st.startswith("w") and st[1:].isdigit(): M.run(int(st[1:])); continue
    if st.startswith("shot:"): M.shot("%s/%s.png" % (PC, st[5:])); continue
    if st.startswith("mark:"):
        c, last = calls_since(last)
        print(st[5:], [hex(a1 & 0xFFFF) for t, a0, a1, a2 in c if t == 1], flush=True)
        for t, a0, a1, a2 in c:
            if t & 0xFF == 3: print("  load slot %d code %#x variant %d costume %d" % (a0, a1, a2, t >> 8), flush=True)
        continue
    if st.startswith("save:"):
        t = st[5:]; n = M.r32(S + 0xFFC)
        calls = [tuple(M.r32(S + 0x1000 + 16 * (i & 0xFFF) + 4 * k) for k in range(4)) for i in range(max(0, n - 4096), n)]
        pickle.dump(calls, open("%s/%s_calls.pkl" % (PC, t), "wb"))
        if not os.environ.get("NOSTATE"): pickle.dump(M.save(), open("%s/%s.st" % (PC, t), "wb"))   # (handlers: NOSTATE=1, no state kept)
        open("%s/%s_ram.bin" % (PC, t), "wb").write(bytes(M.dump()[:0x800000]))
        print("saved", t, n, "calls", flush=True); continue
    if st == "keep": KEPT = M.save(); continue            # (modcheck: the machine as it is now, kept in memory ...)
    if st == "back": M.load(KEPT); continue               # (... and back to it: the same frame again, deterministic)
    if st in ("hide", "hide0", "hide1"):                  # (modcheck: a man's model scale ~0 -- his slot's matrices and their
        import struct as _st                              #  copies, found holding his scale in a match -- so the next frame is
        HIDE = {0: (0x800651BC, 0x800651D0, 0x800651E4, 0x80066580, 0x80066594, 0x800665A8, 0x800706C4, 0x800706D8, 0x800706EC),
                1: (0x800651FC, 0x80065210, 0x80065224, 0x800665C0, 0x800665D4, 0x800665E8, 0x800707C0, 0x800707D4, 0x800707E8)}
        for sl in ((0, 1) if st == "hide" else (int(st[4]),)):     # the same picture without him: his plate; "hide": both)
            for a in HIDE[sl]: M.w32(a, _st.unpack(">I", _st.pack(">f", 0.0001))[0])
        continue
    if st.startswith("pokef:"):         # pokef:addr:value -> a float written (modcheck)
        import struct as _st
        _, a, v = st.split(":"); M.w32(int(a, 16), _st.unpack(">I", _st.pack(">f", float(v)))[0]); continue
    if st.startswith("poke:"):          # poke:addr:value -> a u32 written (ports2: Hennig, a locked man, put into a slot)
        _, a, v = st.split(":"); M.w32(int(a, 16), int(v, 16)); continue
    if st.startswith("poke8:"):         # poke8:addr:value -> a byte written (attires: a slot's costume, 0x8007EF80 + 0x10 slot)
        _, a, v = st.split(":"); a, v = int(a, 16), int(v, 16)
        w = M.r32(a & ~3); sh = 8 * (3 - (a & 3))
        M.w32(a & ~3, (w & ~(0xFF << sh)) | (v << sh)); continue
    if st.startswith("poke16:"):        # poke16:addr:value -> a u16 written (addr even)
        _, a, v = st.split(":"); a, v = int(a, 16), int(v, 16)
        w = M.r32(a & ~3); sh = 16 if (a & 2) == 0 else 0
        M.w32(a & ~3, (w & ~(0xFFFF << sh)) | (v << sh)); continue
    if st.startswith("RS:"):           # RS:x:y -> the right stick (the core's C buttons) pushed 5 frames, then 45 (attires:
        _, x, y = st.split(":")          #  C-right = RS:32767:0, C-up = RS:0:-32767 -- the CR / CU names above are the RetroPad's)
        M.rstick = [(int(x), int(y))] + [(0, 0)] * 3; M.run(5); M.rstick = [(0, 0)] * 4; M.run(45); continue
    if st.startswith("H:"):
        _, b, n = st.split(":"); M.run(int(n), 0, (b,)); continue
    if st.startswith("P2:"):
        M.press(st[3:], port=1, hold=5, after=45); continue
    M.press(st, hold=5, after=45)
