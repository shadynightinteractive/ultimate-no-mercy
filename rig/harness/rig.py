"""Arcade test rig: boots No Mercy in the emulator with the arcade blob injected.

The blob (arcade/build/test/arcade_test.bin) is linked at 0x80700000. Two jumps patch the game:
  0x800012B0 (frame task submit)          -> j 0x80700000 (frame_tramp)
  0x800346AC (osContGetReadData's jr $ra) -> j 0x80700008 (pad_tramp)
Keyboard state for the mod lives at 0x807F8000 (u8 per SDL scancode).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, sys, struct
sys.path.insert(0, os.path.dirname(__file__))
import n64

SIX = bool(os.environ.get("RIG_SIX"))
BLOB = nmp("/home/devbox/work/arcade/build/%s/arcade_test.bin") % ("test6" if SIX else "test")
ELF = BLOB[:-3] + "elf"
KEYS = 0x807F8000
CFG = 0x807F8100
SC = {"1": 30, "2": 31, "5": 34, "6": 35, "F2": 59, "UP": 82, "DOWN": 81, "LEFT": 80, "RIGHT": 79, "RETURN": 40, "SPACE": 44, "ESC": 41}


class Rig:
    def __init__(self, cpu=b"cached_interpreter", workdir=nmp("/home/devbox/work/sys_rig")):
        n64.OPTIONS[b"parallel-n64-cpucore"] = cpu
        # (the six-man rig needs the six ROM: install() copies the engine's code from it)
        rom = nmp("/home/devbox/src/NoMercySix/nomercy.z64") if SIX else nmp("/home/devbox/work/nomercy.z64")
        self.m = n64.N64(os.environ.get("NM_ROM", rom), workdir)
        self.m.run(1)
        self.install()

    def install(self):
        m = self.m
        if SIX and m.r32(0x80780000) == 0:   # the six-man engine's rewrites (the PC port has them recompiled; here: its ROM's copy, put in RAM)
            rb = m.romdata[0x1FF0000:0x2000000]
            for i in range(0, len(rb), 4):
                m.w32(0x80780000 + i, struct.unpack(">I", rb[i:i + 4])[0])
        blob = open(BLOB, "rb").read()
        blob += b"\0" * (-len(blob) % 4)
        for i in range(0, len(blob), 4):
            if SIX and 0x80000 <= i < 0x90000: continue     # (the six-man engine's code is there: the blob's gap)
            m.w32(0x80700000 + i, struct.unpack(">I", blob[i:i + 4])[0])
        # bss up to the DL buffers: zero
        for a in (range(0x807A0000, 0x807C0000, 4) if SIX else range(0x80700000 + len(blob), 0x807C0000, 4)):
            m.w32(a, 0)
        for i in range(256):
            m.w8(KEYS + i, 0)
        for i in range(16):
            m.w32(CFG + 4 * i, 0xFFFFFFFF)
        m.w32(0x800012B0, 0x081C0000)  # j 0x80700000
        m.w32(0x800012B4, 0x00000000)
        m.w32(0x800346AC, 0x081C0002)  # j 0x80700008
        cam = self.sym("test_camera_mul")
        m.w32(0x80001590, 0x0C000000 | ((cam >> 2) & 0x3FFFFFF))  # jal test_camera_mul (was the 4x4 multiply)

    def config(self, idx, value):
        self.m.w32(CFG + 4 * idx, value)

    def key(self, name, frames=8, after=10):
        self.m.w8(KEYS + SC[name], 1)
        self.m.run(frames)
        self.m.w8(KEYS + SC[name], 0)
        self.m.run(after)

    def pad(self, btn, port=0, hold=3, after=15):
        self.m.press(btn, port=port, hold=hold, after=after)

    # mod state (read from the blob's statics through the ELF symbol table)
    _syms = None
    def sym(self, name):
        if Rig._syms is None:
            import subprocess
            out = subprocess.run(["llvm-nm", ELF],
                                 capture_output=True, text=True).stdout
            Rig._syms = {l.split()[2]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) == 3}
        return Rig._syms[name]

    def phase(self):
        names = ["ATTRACT", "PLAY", "GAMEOVER"]
        p = self.m.s32(self.sym("phase"))
        return names[p] if 0 <= p < len(names) else p

    def state(self):
        m = self.m
        return dict(phase=self.phase(), credits=m.s32(self.sym("credits")), mode=m.s32(0x800457A0),
                    screen=m.s16(0x800457A4), pf=m.s32(self.sym("phase_frames")), goal=m.s32(self.sym("goal")),
                    act=(m.s32(self.sym("active")), m.s32(self.sym("active") + 4)), cont=m.s32(self.sym("cont_player")))

def elf_syms(elf):
    import subprocess
    out = subprocess.run(["llvm-nm", elf], capture_output=True, text=True).stdout
    return {l.split()[2]: int(l.split()[0], 16) for l in out.splitlines() if len(l.split()) == 3}

def save_state(R, path):
    """a state, with the blob's ELF beside it (resume remaps the game's jumps into an older blob)"""
    import pickle, shutil
    pickle.dump(R.m.save(), open(path, "wb")); shutil.copy(ELF, path + ".elf")

def remap(R, old_elf):
    """the game's j/jal into the blob (the rig's patches, made by the blob the state was saved with) -> the
    same functions in the current blob"""
    m = R.m
    old = {v: k for k, v in elf_syms(old_elf).items()}
    new = elf_syms(ELF)
    n = 0
    for a in range(0x80000400, 0x80200000, 4):
        w = m.r32(a)
        op = w >> 26
        if op not in (2, 3): continue
        t = 0x80000000 | ((w & 0x3FFFFFF) << 2)
        if not (0x80700000 <= t < 0x807A0000) or 0x80780000 <= t < 0x80790000 or t < 0x80700100: continue   # (the trampolines and the six-man engine stay)
        name = old.get(t)
        if name is None or name not in new: continue
        m.w32(a, (op << 26) | ((new[name] >> 2) & 0x3FFFFFF)); n += 1
    return n

def resume(R, path, phase=1, active=(1, 0), game_mode=1):
    """Load a saved state, put the current blob in, and set the arcade's state (statics are reset)."""
    import pickle
    R.m.load(pickle.load(open(path, "rb")))
    R.install()
    if os.path.exists(path + ".elf"): print("remapped", remap(R, path + ".elf"), "jumps")
    m = R.m
    m.w32(R.sym("phase"), phase)
    m.w32(R.sym("active"), active[0]); m.w32(R.sym("active") + 4, active[1])
    m.w32(R.sym("game_mode"), game_mode)
    m.w32(R.sym("booted"), 1)
    m.w32(R.sym("cont_player"), 0xFFFFFFFF)
    m.w32(R.sym("sfx_pending"), 0xFFFFFFFF)
    m.w32(R.sym("dbg_sfx"), 0xFFFFFFFF)
