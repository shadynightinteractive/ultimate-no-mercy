import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Oracle 3: the stock game and the six-man ROM side by side in one process (two copies of the core), from the
# same state (the stock game's own: a Royal Rumble's versus screen, rr_vs_stock.st), with the same input.
# Every frame from ovl_e's load: each moved array (at its place in each game) and all the rest of the game's
# RAM that the engine keeps (main BSS, ovl_e data+BSS, the heap) are compared; a pointer into a moved array,
# in the six-man game, counts as the same pointer moved back.
#   oracle3.py SIX_ROM [frames] [state]       (env SIX_ONLY as for build_rom; O2_CORE)
import sys, os, struct, re
sys.path.insert(0, nmp("/home/devbox/work/harness")); sys.path.insert(0, nmp("/home/devbox/work/six"))
args = [a for a in sys.argv[1:]]
FULL = os.environ.get("O3_FULL") == "1"     # the six-man ROM as it ships (its own layout: ovl_e's BSS grown, the heap
                                            # after it, main's arrays and the framebuffers in expansion RAM)
if not FULL: sys.argv.append("--oracle")
import n64, numpy as np, pickle
import build_rom, spec
from rompatch import secs, fn2sec as _fn2sec
import pickle as _pk
_db = _pk.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
def secs_fn(addr):
    for s_, f_, a_, asm_ in _db[2]:
        if a_ == addr and f_.startswith('func_%08X' % addr): return s_
    raise KeyError(hex(addr))
six_rom = args[0]
THREADS_ALL = [0x80056670, 0x800571E0, 0x80057238, 0x8005D560, 0x8005D670, 0x8005D710, 0x8005E8F0, 0x8008D330, 0x8008D510, 0x8008E748]
frames = int(args[1]) if len(args) > 1 else 6000
ST = args[2] if len(args) > 2 else nmp("/home/devbox/work/states/rr_vs_stock.st")
n64.OPTIONS[b"parallel-n64-cpucore"] = os.environ.get("O2_CORE", "dynamic_recompiler").encode()
PAIR = ST.startswith("pair:")  # both games' own states, saved by a boot run (O3_SAVEAT): carry on from there
BOOT = ST == "boot" or PAIR
F0 = 0   # from power-on, both: the menus driven by the same inputs (BOOT_INPUTS)
st = None if BOOT else pickle.load(open(ST, "rb"))
A = n64.N64(nmp("/home/devbox/work/nomercy.z64"), nmp("/home/devbox/work/sys_o3A"))
if not BOOT: A.run(1); A.load(st)
n64.CORE = nmp("/home/devbox/work/harness/core_b.so")
B = n64.N64(six_rom, nmp("/home/devbox/work/sys_o3B"))
if not BOOT: B.run(1); B.load(st)
elif PAIR:
    _pa, _pb, F0 = pickle.load(open(ST[5:], "rb"))
    A.run(1); A.load(_pa); B.run(1); B.load(_pb)
    print("pair state at frame", F0)
elif FULL:
    # each from its own power-on (its heap is set up in the first frame, at its own place); their clocks are made
    # the same right after (fix_time)
    A.run(1); B.run(1)
    print("full build: both from their own power-on")
else:
    # one frame after power-on, the stock game's state in both (the boot code's ROM checksum takes a time that
    # depends on the ROM's bytes, and the game keeps that time); the six-man ROM's main code + data patched in
    A.run(1); st = A.save(); B.run(1); B.load(st)
    _s = secs["main_1050"]; _n = 0
    for o in range(0, 0x56670 - 0x450, 4):
        a_, b_ = A.romdata[_s["rom"] + o:_s["rom"] + o + 4], B.romdata[_s["rom"] + o:_s["rom"] + o + 4]
        if a_ != b_: B.w32(_s["vram"] + o, struct.unpack(">I", b_)[0]); _n += 1
    # ovl_e's arrays at their new places are its BSS in the full build (zeroed when it loads): zero here
    for lo_, hi_, new_ in build_rom.RELOC:
        if lo_ not in set(b_ for b_, s_, n_, w_ in build_rom.MAIN_NEW):
            o_ = new_ & 0x7FFFFF; B.ram[o_:o_ + 6 * (hi_ - lo_) // 4] = bytes(6 * (hi_ - lo_) // 4)
    # the main segment's arrays: what the stock code put at the old places in that first frame (the pads' status
    # pointers, func_800056C0) is at the new places
    _nm = 0
    for b_, s_, n_, w_ in build_rom.MAIN_NEW:
        for k_ in range(0, 4 * s_, 4):
            if A.r32(b_ + k_) and not (b_ + k_) & 3 and not n_ & 3: B.w32(n_ + k_, A.r32(b_ + k_)); _nm += 1
    print("boot: main words", _n, "; expansion RAM at the new places was", hex(B.r32(0x80400000)), "; array words carried", _nm)
    # $gp (the wrestler count) is the main thread's (0x80089040): rw_run_overlay sets it when an overlay starts; one
    # frame in, that may have happened already (the stock code it ran then): its saved context's $gp as rw_run_overlay
    # would have left it
    print("boot: main thread's saved pc %08x, running thread %08x" % (B.r32(0x80089040 + 0x11C), B.r32(0x800537D0)))
    B.w32(0x80089040 + 0xE8, 0); B.w32(0x80089040 + 0xEC, 4)
    if os.environ.get("O3_POISON"):     # every other thread's $gp = 0x7F: a slot loop on one would show (or crash)
        for t_ in THREADS_ALL:
            if t_ != 0x80089040: B.w32(t_ + 0xE8, 0); B.w32(t_ + 0xEC, int(os.environ["O3_POISON"], 0))
        print("boot: $gp poisoned in the other threads")
    A.load(A.save()); B.load(B.save())
FULL_SETTLE = int(os.environ.get("O3_SETTLE", "1500")) if FULL and BOOT and not PAIR else 0   # (own power-ons: the boot's loading reaches each frame's end at a
                                         #  slightly different point until the title screen; compared from there)
RELOC = build_rom.RELOC
MAINSET = set(b for b, s_, n_, w_ in build_rom.MAIN_NEW)
adr = 0x80000000 + 4 * np.arange(0x200000, dtype=np.int64)
data = (adr >= 0x80040380) & ~((adr >= 0x800D9960) & (adr < 0x800F2940)) & ~((adr >= 0x80106760) & (adr < 0x80147EE0))
if not BOOT:
    for M in (A, B):
        hk = [a for a in range(0x80000450, 0x80040000, 4) if (M.r32(a) >> 26) in (2, 3) and 0x700000 <= ((M.r32(a) & 0x3FFFFFF) << 2) < 0x800000]
        assert not hk, "state has test-build hooks at %s" % [hex(a) for a in hk[:4]]
    stock = A.romdata; six = B.romdata
    s = secs["main_1050"]; n = 0
    for o in range(0, 0x56670 - 0x450, 4):
        a, b = stock[s["rom"] + o:s["rom"] + o + 4], six[s["rom"] + o:s["rom"] + o + 4]
        if a != b: B.w32(s["vram"] + o, struct.unpack(">I", b)[0]); n += 1
    print("main words", n)
    # the menu overlays loaded at the state (ovl_a, ovl_c at the versus screen) run a few frames before ovl_e loads:
    # their code as the six-man ROM has it
    for name in ("ovl_a", "ovl_b", "ovl_c", "ovl_d"):
        sc = secs[name]; o0 = sc["rom"]
        probe = range(0, min(sc["size"], 0x18000), 0x400)
        if not all(B.r32(sc["vram"] + o) == struct.unpack(">I", stock[o0 + o:o0 + o + 4])[0] for o in probe): continue
        k = 0
        for o in range(0, sc["size"], 4):
            a, b = stock[o0 + o:o0 + o + 4], six[o0 + o:o0 + o + 4]
            if a != b: B.w32(sc["vram"] + o, struct.unpack(">I", b)[0]); k += 1
        print(name, "loaded at the state:", k, "words")
    RELOC = build_rom.RELOC
    MAINSET = set(b for b, s_, n_, w_ in build_rom.MAIN_NEW)
    # the moved arrays: ovl_e's are BSS (zeroed when it loads); the main segment's hold the menus' choices already, so
    # the six-man game has those at their new places (slots 4, 5 zero), and every pointer to them is moved too
    ram = np.frombuffer(bytes(B.ram), dtype='<u4').copy(); newram = ram.copy(); nptr = 0
    adr = 0x80000000 + 4 * np.arange(len(ram), dtype=np.int64)
    data = (adr >= 0x80040380) & ~((adr >= 0x800D9960) & (adr < 0x800F2940)) & ~((adr >= 0x80106760) & (adr < 0x80147EE0))
    # (not code: main's text, ovl_a's and ovl_c's -- the overlays loaded at the versus screen; their data and BSS can
    #  hold pointers the menus made)
    for lo, hi, new in RELOC:
        if lo not in MAINSET: continue
        sel = data & (ram >= lo) & (ram < hi); nptr += int(sel.sum())
        newram[sel] = ram[sel] - lo + new
    be = bytearray(newram.astype('>u4').tobytes())          # RDRAM as the N64 sees it (byte = address & 0x7FFFFF)
    for lo, hi, new in RELOC:
        o, ol, nb = new & 0x7FFFFF, lo & 0x7FFFFF, hi - lo  # (4 slots' bytes)
        be[o:o + nb * 6 // 4] = (bytes(be[ol:ol + nb]) if lo in MAINSET else bytes(nb)) + bytes(nb // 2)
    B.ram[:] = np.frombuffer(bytes(be), dtype='>u4').astype('<u4').tobytes()
    print("main-array pointers moved:", nptr)
    if os.environ.get("O3_DBG"):
        for a_ in [int(x, 16) for x in os.environ["O3_DBG"].split(",")]:
            nw_ = next((a_ - lo + new for lo, hi, new in RELOC if lo <= a_ < hi), a_)
            print("dbg %08x (six %08x): stock %08x six %08x" % (a_, nw_, A.r32(a_), B.r32(nw_)))
    A.load(A.save()); B.load(B.save())

# The game reads the CPU's Count (osGetCount, 0x8003E270) for its frame time; the two emulators' Counts drift a
# few ticks apart (interrupt timing: the RSP/RDP work on different addresses), and the game would follow. So in both,
# the frame timer's osGetCount calls read a clock of the oracle's own: one frame's ticks a frame, plus 0x200 a call.
FIXTIME = os.environ.get("O3_FIXTIME", "1") == "1"
CLOCK = 0x807FFFF0
def fix_time(M):
    for k, w in enumerate((0x3C018080, 0x8C22FFF0, 0x24420200, 0x03E00008, 0xAC22FFF0)): M.w32(0x807FF000 + 4 * k, w)
    # (only the game's frame timer, func_80038050, reads it: libultra's timers keep the hardware's Count, which
    #  their compare interrupts run on)
    for site_ in (0x80038140, 0x80038168):
        assert M.r32(site_) in (0x0C00F89C, 0x0C1FFC00), hex(M.r32(site_))
        M.w32(site_, 0x0C1FFC00)                             # jal 0x807FF000
    if M.r32(0x8003E270) == 0x081FFC00: M.w32(0x8003E270, 0x40024800); M.w32(0x8003E274, 0x03E00008)   # (an older state's)
    M.w32(CLOCK, M.r32(CLOCK) or 0x10000000)
    # and the idle thread (func_800004D0) stirs the random number while the CPU is idle: as many times as the idle
    # time allows, so with the emulators' timing apart, the two games' random numbers part too -- not stirred here
    assert M.r32(0x80000570) in (0x0C00D844, 0), hex(M.r32(0x80000570))      # (0: a state saved with it already done)
    M.w32(0x80000570, 0)
# The logical frames: the main thread's waits for its next frame, counted in both games by the same osRecvMesg
# entry detour (orc/tramp.S, at 0x807FF100; the counter at 0x807FFFE0). The games are compared only where both main
# threads wait for a frame and have waited as many times: while loading, a game can be a few stores (or a frame)
# further on than the other at the end of a retrace -- comparing there would show the loads' progress, not the game.
SYNC = os.environ.get("O3_SYNC", "1") == "1"
MAIN_T, FRAMEQ, FCOUNT = 0x80089040, (0x80060B20, 0x80063368), 0x807FFFE0
def install_sync(M):
    tb = open(nmp("/home/devbox/work/six/orc/tramp.bin"), "rb").read()
    for k_ in range(0, len(tb), 4): M.w32(0x807FF100 + k_, struct.unpack(">I", tb[k_:k_ + 4])[0])
    assert (M.r32(0x80036CD0), M.r32(0x80036CD4)) in ((0x27BDFFD8, 0xAFB00010), (0x081FFC40, 0)), hex(M.r32(0x80036CD0))
    M.w32(0x80036CD0, 0x081FFC40); M.w32(0x80036CD4, 0)
    M.w32(FCOUNT, 0)
def waiting(M): return M.r16(MAIN_T + 0x10) == 8 and M.r32(MAIN_T + 0x8) in FRAMEQ
if SYNC:
    for M in (A, B): install_sync(M)
    A.load(A.save()); B.load(B.save())
sync_off = None; skipped = 0; MAXSKIP = int(os.environ.get("O3_MAXSKIP", "600"))
def in_step():
    """both main threads wait for a frame, the same number of frames in"""
    global sync_off, skipped
    if not SYNC: return True
    if not (waiting(A) and waiting(B)): skipped += 1; return False
    ca, cb = A.r32(FCOUNT), B.r32(FCOUNT)
    if sync_off is None: sync_off = ca - cb          # (a pair state: the two may have been a frame apart there)
    if ca - cb != sync_off: skipped += 1; return False
    skipped = 0; return True
if FIXTIME:
    for M in (A, B): fix_time(M)
    if FULL and BOOT and not PAIR: B.w32(0x80053780, A.r32(0x80053780))   # (the random state the boot's timing left: the same)
    A.load(A.save()); B.load(B.save())
# The rewrites (rw/: C for functions an immediate can't fix): the blob at 0x80780000 in the six-man game, and each
# rewritten function's first two instructions `j rw_<addr>; nop` -- put back whenever its overlay loads again.
RW = os.environ.get("O3_RW")          # the blob's layout: oracle|full
RWHOOKS = []
if RW:
    BLOB = six_rom + ".blob" if os.path.exists(six_rom + ".blob.bin") else nmp("/home/devbox/work/six/rw/out/%s/blob") % RW   # (the ROM's own)
    _blob = open(BLOB + ".bin", "rb").read()
    for k_ in range(0, len(_blob), 4): B.w32(0x80780000 + k_, struct.unpack(">I", _blob[k_:k_ + 4].ljust(4, b"\0"))[0])
    for line_ in open(BLOB + ".syms"):
        a_, t_, nm_ = line_.split()
        if not re.fullmatch(r"rw_[0-9A-F]{8}", nm_): continue       # (the register stubs are called from their sites)
        fa_ = int(nm_[3:], 16); sec_ = secs_fn(fa_)
        o_ = secs[sec_]["rom"] + fa_ - secs[sec_]["vram"]
        RWHOOKS.append((fa_, int(a_, 16), struct.unpack(">2I", B.romdata[o_:o_ + 8])))
    print("rewrites:", [hex(h[0]) for h in RWHOOKS])
    # (the stock game's Rumble has 4 in the ring: so does the six-man game's here, unless O3_RING says otherwise)
    _rs = {l.split()[2]: int(l.split()[0], 16) for l in open(BLOB + ".syms")}
    if "rw_rumble_ring" in _rs:
        _a = _rs["rw_rumble_ring"]; _w = B.r32(_a & ~3); _sh = (3 - (_a & 3)) * 8
        B.w32(_a & ~3, (_w & ~(0xFF << _sh)) | (int(os.environ.get("O3_RING", "4")) << _sh))
    # (O3_TWIN: the stock game gets the six-man game's timing: the same rw_run_overlay detour at every overlay's entry (it sets
    # $gp, which stock code never reads), so the two run the same instructions there and their frames end at the
    # same points -- else the loading loops' state at a frame's end parts for a few frames (heap rover, textures);
    # not needed with the main-thread sync below)
    if os.environ.get("O3_TWIN"):
        for k_ in range(0, len(_blob), 4): A.w32(0x80780000 + k_, struct.unpack(">I", _blob[k_:k_ + 4].ljust(4, b"\0"))[0])
        _syms = {l.split()[2]: int(l.split()[0], 16) for l in open(BLOB + ".syms")}
        for sec_, site_, old_, stub_ in build_rom.CALLS:
            assert A.r32(site_) == 0x0C000000 | ((old_ >> 2) & 0x3FFFFFF), hex(A.r32(site_))
            A.w32(site_, 0x0C000000 | ((_syms[stub_] >> 2) & 0x3FFFFFF))
            assert B.r32(site_) == A.r32(site_), (hex(B.r32(site_)), hex(A.r32(site_)))
        A.load(A.save()); B.load(B.save())
def rw_apply(M):
    for fa_, ta_, orig_ in RWHOOKS:
        if (M.r32(fa_), M.r32(fa_ + 4)) == orig_:            # (its overlay is in, as the ROM has it)
            M.w32(fa_, 0x08000000 | ((ta_ >> 2) & 0x3FFFFFF)); M.w32(fa_ + 4, 0)
def ovle_in(M):
    r = M.romdata; o0 = secs["ovl_e"]["rom"]
    return all(M.r32(0x800D9960 + o) == struct.unpack(">I", r[o0 + o:o0 + o + 4])[0] for o in (0x100, 0x1000, 0x8000, 0x20000, 0x40000, 0x60000))
# what's compared: (name, stock addr, six addr, bytes) -- in the match (ovl_e loaded) and before it (the menus'
# overlays, ovl_a and ovl_c, loaded: their BSS has ovl_e's arrays' addresses, unmoved for them)
STACKS = [(0x80056820, 0x80057020), (0x80057410, 0x80059410), (0x80059410, 0x8005B410), (0x8005B410, 0x8005D410), (0x8005EAA0, 0x80060AA0),
          (0x800891F0, 0x8008D1F0), (0x8008D6C0, 0x8008E6C0), (0x8008E8F8, 0x8008F8F8)]
# (thread stacks, from the osCreateThread calls' stack tops and the thread structs below them: frames' saved
#  registers and locals, stale or live, not the game's state -- left out; and the threads' saved register contexts,
#  OSThread +0x20..+0x1B0: registers mid-computation -- a relocated lui's high half plus an index, say)
THREADS = [0x80097D08,     # __osThreadSave: __osException saves registers here first (0x8003C770)
           0x80056670,     # the boot thread (osCreateThread at 0x800004AC; its stack, 0x80056820..0x80057020)
           0x80057238, 0x8005D560, 0x8005D670, 0x8005D710, 0x8005E8F0, 0x80089040, 0x8008D330, 0x8008D510, 0x8008E748]
# message queues (the osCreateMesgQueue calls' fixed ones: header, messages): which message is first and how many
# wait follows the timing, not the game
QUEUES = [(0x8005D410, 0x8005D428), (0x8005D448, 0x8005D460), (0x8005D480, 0x8005D498), (0x8005D4B8, 0x8005D4D0), (0x8005D4F0, 0x8005D508), (0x8005D528, 0x8005D540), (0x80060AA8, 0x80060AC0), (0x80060B00, 0x80060B18), (0x80060B20, 0x80060B38), (0x80062FC8, 0x80062FE0), (0x80063368, 0x80063380), (0x8008E6C0, 0x8008E6D8), (0x8008F900, 0x8008F918), (0x800A4020, 0x800A4038), (0x800A9420, 0x800A9438), (0x800A98C8, 0x800A98E0), (0x800AA050, 0x800AA068), (0x800BD930, 0x800BD948),
          (0x8005D428, 0x8005D448), (0x8005D460, 0x8005D480), (0x8005D498, 0x8005D4B8), (0x8005D4D0, 0x8005D4F0), (0x8005D508, 0x8005D528), (0x8005D540, 0x8005D560), (0x80060AC0, 0x80060B00), (0x80060B18, 0x80060B1C), (0x80060B38, 0x80060B3C), (0x80062FC0, 0x80062FC4), (0x80063380, 0x80063384), (0x8008E6D8, 0x8008E6DC), (0x8008F918, 0x8008F92C), (0x8008E6E0, 0x8008E6E4), (0x800A111C, 0x800A1120), (0x8009FE70, 0x8009FE74), (0x800A875C, 0x800A8760), (0x8008E730, 0x8008E734)]
# the framebuffer being drawn (0x80054B94: one of the two): which one is a matter of how many VIs have gone by
# the audio library's state (its code is 0x80027xxx..0x8002Exxx): voices, sample counters, buffers -- they run on
# the audio interface's interrupts, so on the emulators' timing
AUDIO = [(0x80076280, 0x80076564),   # (sound handles by effect id, func_80017568: the audio library's voices)
         (0x8008D1F0, 0x8008D330), (0x800A93E0, 0x800A9420), (0x80053150, 0x80053168), (0x8008E6C0, 0x8008E748)]
# libultra's thread queues (0x800537C0..: the run queue, the active list, the running thread): which thread is
# where at a frame's end is the timing; and the thread structs whole (state, queue links, registers)
OSQ = [(0x800537C0, 0x800537E0)]
FBPTR = AUDIO + [(0x80052E2C, 0x80052E30),  # (ovl_e's BSS end in the overlay table: the full build's own value)
                 (0x80054B90, 0x80054BF8),   # (libultra's two VI contexts and its current/next pointers)
         (0x80057020, 0x80057238)]   # (and the I/O request blocks there: which wait to be done when, the timing)
_o = secs["ovl_e"]["rom"] + 0x800DE4B4 - secs["ovl_e"]["vram"]
SIX_STUBS = struct.unpack(">I", B.romdata[_o:_o + 4])[0] >> 26 == 3     # (this six-man ROM has the name-buffer stub)
# areas the six-man game keeps in a block of its own (build_rom.EXTRA) in any match: (name, stock lo, hi, six place)
XMOVES = [("HUD name textures", 0x80156880, 0x801578C0, build_rom.EXTRA["namebuf"])] if "namebuf" in build_rom.EXTRA and SIX_STUBS else []
def cmp_list(arrays, regs, xmoves=()):
    moved = {lo: (hi, new) for lo, hi, new in RELOC}
    out = []
    for b, sz, why in arrays:
        if b in moved: out.append((why, b, moved[b][1], moved[b][0] - b))
    for why, lo, hi, new in xmoves: out.append((why, lo, new, hi - lo))
    holes = sorted([(b, moved[b][0]) for b, sz, why in arrays if b in moved] + [(lo, hi) for why, lo, hi, new in xmoves] + STACKS + QUEUES + FBPTR + OSQ + [(t, t + 0x1B0) for t in THREADS])
    def piece(name, a, b):          # (words where aligned -- pointers in them are mapped -- bytes at the edges)
        a4, b4 = (a + 3) & ~3, b & ~3
        if a4 >= b4: out.append((name, a, a, b - a)); return
        if a < a4: out.append((name, a, a, a4 - a))
        out.append((name, a4, a4, b4 - a4))
        if b4 < b: out.append((name, b4, b4, b - b4))
    for name, lo, hi in regs:
        p = lo
        for hl, hh in holes + [(hi, hi)]:
            if hh <= p or hl >= hi: continue
            if hl > p: piece(name, p, hl)
            p = max(p, hh)
        if p < hi: piece(name, p, hi)
    return out
HEAPR = ("heap", 0x8016E000, 0x8038F800)
# (the full build's other moves: the heap, the two framebuffers)
#  The heap (func_800007D4) hands out blocks from its top down, so a block is as far below the heap's end in each:
#  its header (0x8016E000..+0x10) moves with the start, everything above with the end.
#  (Run with SIX_HEAPGROW=0 -- the same size, so both ends move alike; the heap takes blocks from both ends.)
HEAPSHIFT = spec.HEAP_END - 0x8038F800
assert HEAPSHIFT == spec.HEAP - 0x8016E000 or not FULL, "O3_FULL needs SIX_HEAPGROW=0 (a heap of the stock size)"
EXTRA = [(0x8016E000, 0x8016E010, spec.HEAP, spec.HEAP + 0x10), (0x8016E010, 0x8038F800, 0x8016E010 + HEAPSHIFT, spec.HEAP_END),
         (0x8038F800, 0x803C7C00, spec.FB0, spec.FB0 + 0x38400),
         (0x803C7C00, 0x80400000, spec.FB1, spec.FB1 + 0x38400)] if FULL else []
# (data in another shape -- compared element by element -- when this ROM has the rewrites that use it)
_o = secs["ovl_e"]["rom"] + 0x800EC940 - secs["ovl_e"]["vram"]
ELEMS = build_rom.oracle_elems() if struct.unpack(">I", B.romdata[_o:_o + 4])[0] >> 26 == 2 else []
CMP = cmp_list(spec.OVLE + spec.MAIN, [("main data", 0x80040380, 0x80056670), ("main BSS", 0x80056670, 0x800BD960), ("ovl_e data+BSS", 0x80144670, 0x8016B9F0), HEAPR], XMOVES + [(nm, sa, sa + nb, sb) for nm, sa, sb, nb in ELEMS])
CMP_PRE = cmp_list(spec.MAIN, [("main data", 0x80040380, 0x80056670), ("main BSS", 0x80056670, 0x800BD960), ("ovl_a data+BSS", 0x800F2940, 0x80106760),
                               ("ovl_c data+BSS", 0x80147EE0, 0x8015BA00), HEAPR])
if FULL:       # the heap: stock 0x8016E000.. against the six-man one at its new place
    for L_ in (CMP, CMP_PRE):
        L_[:] = [x for x in L_ if x[0] != "heap"] + [("heap", 0x8016E010, 0x8016E010 + HEAPSHIFT, 0x8038F800 - 0x8016E010)]
NEWR = np.array([(lo, hi, new, new + 6 * (hi - lo) // 4) for lo, hi, new in RELOC + [(lo, hi, new) for why, lo, hi, new in XMOVES]] + EXTRA, dtype=np.int64)
def unmove(w):
    """(and the physical form, 0x00xxxxxx, of each)"""
    w = w.astype(np.int64)
    for lo, hi, nw, ne in NEWR:
        sel = (w >= nw) & (w < ne); w[sel] += lo - nw
        sel = (w >= (nw & 0x1FFFFFFF)) & (w < (ne & 0x1FFFFFFF)); w[sel] += lo - nw
    return w
# differences explained (stock behaviour that copies stale stack bytes into the game's state: in the six-man
# game the same stale bytes are a moved pointer's): (lo, hi, mask of the bits ignored in each word there)
#  - func_800140B4's part record (auStack_68) is filled by func_8001AC44, which leaves byte 0x14 (and 0x13's
#    upper bits) unwritten; func_80018A3C -> func_80018808 copies record bytes 0x13/0x14 into bytes 2-3 of
#    each 4-byte entry at model descriptor +0x218 (the entries the 0x8004FDD8 list names).
#    The same holds for the other func_80018808 calls (descriptor +0xE0..+0x1E0: record bytes 0x20, 0x23, 0x26,
#    0x29, 0x2C, 0x2F are never unpacked either), so bytes 2-3 of each 4-byte entry in +0xE0..+0x254 are left out.
EXPLAINED = [(0x800A8980 + m * 0x270 + 0xE0, 0x800A8980 + m * 0x270 + 0x254, 0x0000FFFF) for m in range(4)]
# (timestamps of the hardware Count, which the six-man game's extra instructions -- rw_run_overlay, the full build's
#  bigger heap clear -- move on by a few ticks: libultra's timer list base, 0x800A8950 (osSetTimer, func_80037BE0;
#  only libultra's timer interrupts use it), and ovl_a's timers 0x800F2960.. when it loads)
EXPLAINED += [(0x800A8950, 0x800A8954, 0xFFFFFFFF), (0x800F2960, 0x800F2970, 0xFFFFFFFF)]
def explained(a, wa, wb):
    for lo, hi, mask in EXPLAINED:
        if lo <= a < hi and (int(wa) & ~mask) == (int(wb) & ~mask): return True
    return False
# A word that held a moved pointer (the two games' values different, the same after mapping) and then had part of
# it overwritten (a halfword store or DMA): the bytes not overwritten are the old pointer's, so they differ.
# (stock address -> the last (stock, six) pointer values seen there)
PTRW = {}; residue_seen = set()
def residue(a, wa, wb):
    if a not in PTRW: return False
    pa, pb = PTRW[a]; wa, wb = int(wa), int(wb)
    for sh in range(0, 32, 8):
        x, y = (wa >> sh) & 0xFF, (wb >> sh) & 0xFF
        if x != y and not (x == (pa >> sh) & 0xFF and y == (pb >> sh) & 0xFF): return False
    if a not in residue_seen and len(residue_seen) < 20:
        residue_seen.add(a); print("  (%08x: %08x/%08x keeps bytes of the moved pointer %08x/%08x stored there before)" % (a, wa, wb, pa, pb))
    return True
def bebytes(M, a, n):
    o = a & 0x7FFFFF; o0 = o & ~3; o1 = (o + n + 3) & ~3
    return np.frombuffer(bytes(M.ram[o0:o1]), dtype='<u4').astype('>u4').tobytes()[o - o0:o - o0 + n]
def where(a):
    for b, sz, why in spec.OVLE + spec.MAIN:
        if b <= a < b + 4 * sz: return "%s slot %d +%x" % (why, (a - b) // sz, (a - b) % sz)
    return "%08x" % a
OH = [int(x) for x in os.environ['O3_OLDHITS'].split(',')] if os.environ.get('O3_OLDHITS') else None   # from,to (match frames)
TR = [int(x, 16) for x in os.environ['O3_TRACE'].split(',')] if os.environ.get('O3_TRACE') else None   # lo,hi,from,to (match frames; hex)
def oh_start():
    import ctypes as C
    RR = [(lo, hi) for lo, hi, new in RELOC if not os.environ.get('O3_OHMAIN') or lo in MAINSET]
    if os.environ.get('O3_SLOT45'):    # instead: slots 4 and 5 of every moved array (a 4-man game never goes there)
        RR = [(new + (hi - lo), new + 6 * (hi - lo) // 4) for lo, hi, new in RELOC]
    if os.environ.get('O3_STRAY'):     # and the expansion RAM around the new places (an access there is a stray one)
        nr = sorted((new, new + 6 * (hi - lo) // 4) for lo, hi, new in RELOC); p_ = 0x80400000
        for a_, b_ in nr + [(0x80600000, 0x80600000)]:
            if a_ > p_: RR.append((p_, a_))
            p_ = max(p_, b_)
    rr = (C.c_uint32 * (2 * len(RR)))(*[x for lo, hi in RR for x in (lo, hi)])
    B.lib.retro_trace_ring_blob(1); B.lib.retro_trace_reset(); B.lib.retro_trace_ranges(rr, len(RR)); B.lib.retro_trace_ra(int(os.environ.get('O3_RA', '0'), 16)); B.lib.retro_trace_enable(1)
if OH and OH[0] < 0: oh_start()
_prep = {}
def prep(lst):
    """split a compare list: regions compared in place (one index array), moved arrays (aligned: word slices;
    unaligned: bytes)"""
    if id(lst) in _prep: return _prep[id(lst)]
    same = [(sa, nb) for name, sa, sb, nb in lst if sa == sb]
    idx = np.concatenate([np.arange((sa & 0x7FFFFF) // 4, ((sa & 0x7FFFFF) + nb) // 4) for sa, nb in same if sa % 4 == 0 and nb % 4 == 0])
    mv = [(name, sa, sb, nb) for name, sa, sb, nb in lst if sa != sb or sa % 4 or nb % 4]
    _prep[id(lst)] = (idx, mv); return _prep[id(lst)]
# A difference counts when it is there PERSIST (3) frames running: the emulators' interrupts land a few instructions
# apart (their timing follows the RSP/RDP's work on different addresses), and the six-man game runs a few more
# instructions (rw_run_overlay), so at a frame's end one game can be a store or two further on than the other; in a
# loading loop (the boot's alloc/free pairs, the heap rover 0x800435F0) that lasts two frames; then they agree again.
PERSIST = int(os.environ.get("O3_PERSIST", "3"))
_prev = {}; _hist = {}
def compare(lst, label):
    global shown
    cur = {}          # stock address -> description
    idx, mv = prep(lst)
    WA = np.frombuffer(A.ram, dtype='<u4'); WB = np.frombuffer(B.ram, dtype='<u4')
    d = idx[WA[idx] != WB[idx]]
    if len(d):
        wa = WA[d]; wb = WB[d]; ub = unmove(wb)
        for k in range(len(d)):
            a_ = 0x80000000 + 4 * int(d[k])
            if wa[k] == ub[k]: PTRW[a_] = (int(wa[k]), int(wb[k])); continue
            if not explained(a_, wa[k], wb[k]) and not residue(a_, wa[k], wb[k]): cur[a_] = ("in place", "%s %08x/%08x" % (where(a_), wa[k], wb[k]))
    for name, sa, sb, nb in mv:
        if sa % 4 or sb % 4 or nb % 4:      # a byte/short array: no pointers in it
            xa = bebytes(A, sa, nb); xb = bebytes(B, sb, nb)
            if xa == xb: continue
            for k in range(nb):
                if xa[k] != xb[k]: cur[sa + k] = (name, "%s %02x/%02x" % (where(sa + k), xa[k], xb[k]))
            continue
        oa, ob = (sa & 0x7FFFFF) // 4, (sb & 0x7FFFFF) // 4
        wa = WA[oa:oa + nb // 4]; wb = WB[ob:ob + nb // 4]
        if np.array_equal(wa, wb): continue
        ub = unmove(wb); dd = np.nonzero((wa != wb) & (wa.astype(np.int64) != ub))[0]
        for k in np.nonzero((wa != wb) & (wa.astype(np.int64) == ub))[0]: PTRW[sa + 4 * int(k)] = (int(wa[k]), int(wb[k]))
        for k in dd:
            if not explained(sa + 4 * k, wa[k], wb[k]) and not residue(sa + 4 * int(k), wa[k], wb[k]): cur[sa + 4 * int(k)] = (name, "%s %08x/%08x" % (where(sa + 4 * k), wa[k], wb[k]))
    h_ = _hist.setdefault(id(lst), {})
    for a_ in list(h_):
        if a_ not in cur: del h_[a_]
    for a_ in cur: h_[a_] = h_.get(a_, 0) + 1
    real = {a_: v for a_, v in cur.items() if h_[a_] >= PERSIST}
    if real and shown < (400 if KEEP else 40):
        groups = {}
        for a_, (g, txt) in sorted(real.items()): groups.setdefault(g, []).append(txt)
        for g, l in groups.items():
            print("%s: %s: %d differ (%d frames running):" % (label, g, len(l), PERSIST), ", ".join(l[:8])); shown += 1
    return 1 if real else 0
ABS = bool(os.environ.get('O3_ABS'))
KEEP = bool(os.environ.get('O3_KEEP'))     # (carry on after differences: do they go away?)
SL = int(os.environ['O3_SLOG']) if os.environ.get('O3_SLOG') else None   # (frame: the stores made in it, both games, compared in order)
STALE = os.environ.get('O3_STALE'); stale_seen = set()
# the menus, from power-on: Multi Play > Royal Rumble, random order, 8 superstars, Watch, Raw is War, the rules,
# Rock / HHH / Austin / Mankind; the versus screen; then A (the match)
BOOT_INPUTS = [((), 1501), (("START",), 4), ((), 420), (("START",), 4), ((), 200), (("A",), 4), ((), 400),
    (("DOWN",), 4), ((), 30), (("A",), 4), ((), 200), (("A",), 4), ((), 60), (("UP",), 4), ((), 15), (("UP",), 4), ((), 35),
    (("A",), 4), ((), 60)] + [(("DOWN",), 4), ((), 12)] * 7 + [((), 10), (("A",), 4), ((), 60), (("A",), 4), ((), 60),
    (("START",), 4), ((), 200), (("A",), 4), ((), 60), (("RIGHT",), 4), ((), 20), (("A",), 4), ((), 60), (("RIGHT",), 4), ((), 20),
    (("A",), 4), ((), 60), (("DOWN",), 4), ((), 20), (("A",), 4), ((), 660), (("A",), 6), ((), 30)]
INPUT = [b for b, n_ in BOOT_INPUTS for _ in range(n_)][F0:] if BOOT else [("A",)] * 6 + [()] * 30
if not BOOT: A.press("A", hold=6, after=30); B.press("A", hold=6, after=30); INPUT = []
f0 = None; i = 0; bad = 0; shown = 0
for f in range(60000):
    btn = INPUT[f] if f < len(INPUT) else ()
    if FIXTIME:
        for M in (A, B): M.w32(CLOCK, (M.r32(CLOCK) + 781250) & 0xFFFFFFFF)
    if RW: rw_apply(B)
    A.run(1, buttons=btn); B.run(1, buttons=btn)
    if STALE:
        rb_ = np.frombuffer(bytes(B.ram), dtype='<u4')
        st_ = np.zeros(len(rb_), dtype=bool)
        for lo, hi, new in RELOC:
            if lo in MAINSET: st_ |= (rb_ >= lo) & (rb_ < hi)
        st_ &= data
        for k in np.nonzero(st_)[0]:
            a_ = 0x80000000 + 4 * int(k)
            if int(rb_[k]) % 4 or any(l_ <= a_ < h_ for l_, h_ in STACKS) or any(t_ <= a_ < t_ + 0x1B0 for t_ in THREADS): continue
            if a_ not in stale_seen:
                stale_seen.add(a_)
                if len(stale_seen) < 60: print("frame %d: %08x holds %08x (old %s); stock %08x; before it %08x/%08x" % (f, a_, int(rb_[k]), where(int(rb_[k])), A.r32(a_), A.r32(a_ - 4), B.r32(a_ - 4)))
    if os.environ.get("O3_SAVEAT") and f + F0 == int(os.environ["O3_SAVEAT"]):
        pickle.dump((A.save(), B.save(), f + F0 + 1), open(os.environ.get("O3_SAVETO", nmp("/home/devbox/work/states/o3pair.st")), "wb"))
        print("saved the pair at frame", f + F0 + 1)
    K = f + F0 if ABS else (i if f0 is not None else -10**9)    # (O3_ABS: the hooks below count frames from power-on/the state)
    if OH and K == OH[0]: oh_start()
    if OH and K == OH[1]:
        B.lib.retro_trace_enable(0); B.lib.retro_trace_dump(nmp("/tmp/devbox-0/o3_oldhits.txt").encode())
        print("accesses to the moved arrays' old places in the six-man game (frames %d..%d):" % tuple(OH))
        import ctypes as C
        ra = (C.c_uint32 * 16)(); nra = B.lib.retro_trace_ra_get(ra); print(" ra at O3_RA:", [hex(ra[k]) for k in range(nra)])
        for line in open(nmp("/tmp/devbox-0/o3_oldhits.txt")):
            p_ = line.split(); print("  pc %s count %s writes %s  %s..%s  %s" % (p_[0], p_[1], p_[2], p_[3], p_[4], where(int(p_[3], 16))))
        break
    if SL is not None and K == SL - 1:
        for M in (A, B): M.lib.retro_slog(1, None, 0)
    if SL is not None and K == SL:
        import ctypes as C
        logs = []
        for M in (A, B):
            n_ = M.lib.retro_slog(0, None, 0); buf = (C.c_uint32 * (4 * n_))(); M.lib.retro_slog(2, buf, n_)
            logs.append(np.frombuffer(buf, dtype=np.uint32).reshape(-1, 4).copy())
        def keep_(lg):          # (not the stacks' and saved registers' stores)
            ad_ = lg[:, 1].astype(np.int64); k_ = np.ones(len(lg), dtype=bool)
            for l_, h_ in STACKS + [(t_ + 0x20, t_ + 0x1B0) for t_ in THREADS]: k_ &= ~((ad_ >= l_) & (ad_ < h_))
            return lg[k_]
        la, lb = keep_(logs[0]), keep_(logs[1]); print("store logs:", len(la), len(lb))
        lb2 = lb.copy(); lb2[:, 1] = unmove(lb[:, 1]); lb2[:, 2] = np.where(lb[:, 2] != la[:min(len(la), len(lb)), 2][:len(lb)] if len(la) >= len(lb) else True, unmove(lb[:, 2]), lb[:, 2]) if False else unmove(lb[:, 2])
        m_ = min(len(la), len(lb))
        eq = (la[:m_, 0] == lb2[:m_, 0]) & (la[:m_, 1] == lb2[:m_, 1]) & ((la[:m_, 2] == lb2[:m_, 2]) | (la[:m_, 2] == lb[:m_, 2]))
        bad_ = np.nonzero(~eq)[0]
        print("first differing store:", bad_[:1])
        if os.environ.get("O3_SLOG_ADDR"):      # (stock address: the stores there, both games, with their mask)
            q_ = int(os.environ["O3_SLOG_ADDR"], 16) & ~3
            for nm_, lg, lu in (("stock", la, la), ("six", lb, lb2)):
                for k in np.nonzero((lu[:, 1].astype(np.int64) & 0x1FFFFFFC) == (q_ & 0x1FFFFFFF))[0]: print(" ", nm_, k, "pc %08x [%08x]=%08x m%08x" % tuple(lg[k]))
        if not len(bad_) and len(la) != len(lb):
            for nm_, lg in (("stock", la), ("six", lb)):
                print(nm_, "last stores:", ["pc %08x [%08x]=%08x" % tuple(lg[k][:3]) for k in range(max(0, m_ - 6), len(lg))])
        if len(bad_):
            k0 = int(bad_[0])
            for k in range(max(0, k0 - int(os.environ.get("O3_SLOG_CTX", "12"))), min(m_, k0 + 8)):
                print("%s %7d  stock pc %08x [%08x]=%08x m%08x   six pc %08x [%08x]=%08x m%08x" % ("*" if k == k0 else " ", k, *la[k], *lb[k]))
        break
    if TR and K == TR[2]:
        for M in (A, B): M.lib.retro_trace_ring_blob(1); M.lib.retro_trace_reset(); M.lib.retro_trace_range(TR[0], TR[1]); M.lib.retro_trace_ra(TR[4] if len(TR) > 4 else 0); M.lib.retro_trace_regs_last(1 if os.environ.get("O3_REGS_LAST") else 0); M.lib.retro_trace_enable(1)
    if TR and K == TR[3]:
        import ctypes as C
        for nm, M in (("stock", A), ("six", B)):
            M.lib.retro_trace_enable(0); M.lib.retro_trace_dump((nmp("/tmp/devbox-0/o3tr_%s.txt") % nm).encode())
            print("trace", nm); print(open(nmp("/tmp/devbox-0/o3tr_%s.txt") % nm).read()[:3000])
            pcs = (C.c_uint32 * 256)(); ad = (C.c_uint32 * 256)(); M.lib.retro_trace_ring(pcs, ad)
            ra = (C.c_uint32 * 16)(); nra = M.lib.retro_trace_ra_get(ra); print(" ra", [hex(ra[k]) for k in range(nra)])
            rg = (C.c_uint32 * 32)()
            if M.lib.retro_trace_regs_get(rg):
                nm_ = "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()
                print(" regs at the first traced access at that pc:", " ".join("%s=%08x" % (nm_[q], rg[q]) for q in range(32)))
                print(" stack:", " ".join("%08x" % M.r32(rg[29] + 4 * q) for q in range(64)))
            print(" ring", sorted(set((hex(pcs[k]), hex(ad[k])) for k in range(256) if pcs[k]))[:40])
        break
    if os.environ.get("O3_WATCH"):
        w_ = [int(x, 16) for x in os.environ["O3_WATCH"].split(",")]; wa_, wb_ = w_[0], w_[-1]     # (stock address[,six address])
        print("watch", K, "%08x %08x" % (A.r32(wa_), B.r32(wb_)))
    if os.environ.get("O3_DUMP") and K == int(os.environ["O3_DUMP"]):
        open(nmp("/tmp/devbox-0/o3_ram_stock.bin"), "wb").write(A.dump()); open(nmp("/tmp/devbox-0/o3_ram_six.bin"), "wb").write(B.dump())
    ia, ib = ovle_in(A), ovle_in(B)
    if f0 is None:
        if ia != ib: print("ovl_e loads at different frames (%d): %s %s" % (f, ia, ib)); sys.exit(1)
        if not ia:
            pb = compare(CMP_PRE, 'before the match, frame %d' % f) if not (bad and (OH or TR or SL is not None)) and f >= FULL_SETTLE and in_step() else 0; bad += pb
            if skipped > MAXSKIP:
                print("out of step for %d frames (frame %d): main threads %04x/%04x queues %08x/%08x counts %d/%d" % (skipped, f, A.r16(MAIN_T + 0x10), B.r16(MAIN_T + 0x10), A.r32(MAIN_T + 8), B.r32(MAIN_T + 8), A.r32(FCOUNT), B.r32(FCOUNT))); bad += 1; skipped = 0
                for nm_, M_ in (("stock", A), ("six", B)):
                    print("  %s main: pc %08x ra %08x sp %08x cause %08x badvaddr %08x" % (nm_, M_.r32(MAIN_T + 0x11C), M_.r32(MAIN_T + 0x104), M_.r32(MAIN_T + 0xF4), M_.r32(MAIN_T + 0x120), M_.r32(MAIN_T + 0x124)))
            if pb and 'first_pre' not in dir(): first_pre = f
            if bad and 'first_pre' not in dir(): first_pre = f
            if bad and not OH and f > first_pre + 3 and not KEEP: break
            continue
        f0 = f; print("ovl_e loaded at frame", f)
    if not (bad and (OH or TR or SL is not None)) and in_step(): bad += compare(CMP, 'match frame %d' % i)   # (tracing: the first differences are enough)
    if skipped > MAXSKIP: print("out of step for %d frames (match frame %d): main threads %04x/%04x queues %08x/%08x counts %d/%d" % (skipped, i, A.r16(MAIN_T + 0x10), B.r16(MAIN_T + 0x10), A.r32(MAIN_T + 8), B.r32(MAIN_T + 8), A.r32(FCOUNT), B.r32(FCOUNT))); bad += 1; skipped = 0
    if bad and not TR and not OH and i > (first_bad if 'first_bad' in dir() else i) + 3 and not KEEP: break
    if bad and 'first_bad' not in dir(): first_bad = i
    i += 1
    if i >= frames: break
n64.fbshot(A, nmp("/tmp/devbox-0/o3_stock.png")); n64.fbshot(B, nmp("/tmp/devbox-0/o3_six.png"))
for nm_, M in (("stock", A), ("six", B)):
    if M.nlog: print("%s core log: %d messages, e.g." % (nm_, M.nlog), [str(x)[:120] for x in M.log[:6]])
print("compared %d frames: %s" % (i, "IDENTICAL" if not bad else "DIFFER"))
