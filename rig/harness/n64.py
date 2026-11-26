"""Headless N64 test rig: drives the parallel-n64 libretro core (angrylion software RDP)
through ctypes. Gives scripted controller input, RDRAM read/write by N64 virtual
address, save states and PNG frame grabs.

RDRAM is stored by mupen64plus as host-endian 32-bit words, so the byte at N64
address A lives at offset (A & 0x7FFFFF) ^ 3.
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import ctypes as C, os, struct, zlib

# Windows: libretro's own build (buildbot nightly); it hasn't the sandbox build's retro_trace_* functions
CORE = (nmp("/home/devbox/src/parallel-n64/win/parallel_n64_libretro.dll") if os.name == "nt"
        else nmp("/home/devbox/src/parallel-n64/parallel_n64_libretro.so"))

# libretro joypad ids, named by the N64 button the core maps them to
BTN = {"A": 0, "B": 1, "L": 2, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7,
       "CD": 8, "CU": 9, "CL": 10, "CR": 11, "Z": 12, "R": 13}

ENV_GET_VARIABLE = 15
ENV_SET_PIXEL_FORMAT = 10
ENV_GET_SYSTEM_DIRECTORY = 9
ENV_GET_SAVE_DIRECTORY = 31
ENV_GET_CAN_DUPE = 3
ENV_GET_AUDIO_VIDEO_ENABLE = 47 | 0x10000
ENV_GET_INPUT_BITMASKS = 51 | 0x10000

class retro_variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]

class retro_game_info(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]

ENV_CB = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_CB = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_CB = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIOB_CB = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL_CB = C.CFUNCTYPE(None)
LOG_CB = C.CFUNCTYPE(None, C.c_int, C.c_char_p)
ENV_GET_LOG_INTERFACE = 27
STATE_CB = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

OPTIONS = {
    b"parallel-n64-gfxplugin": b"angrylion",
    b"parallel-n64-rspplugin": b"cxd4",
    b"parallel-n64-cpucore": b"dynamic_recompiler",
    b"parallel-n64-angrylion-multithread": b"off",
    b"parallel-n64-angrylion-sync": b"High",
    b"parallel-n64-angrylion-vioverlay": b"Filtered",
    b"parallel-n64-pak1": b"none", b"parallel-n64-pak2": b"none",
    b"parallel-n64-pak3": b"none", b"parallel-n64-pak4": b"none",
}

def png_bytes(w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class N64:
    def __init__(self, rom, workdir):
        os.makedirs(workdir, exist_ok=True)
        self.dir = workdir.encode()
        self.lib = C.CDLL(CORE)
        self.pixfmt = 0
        self.render = False
        self.frame = None          # (w, h, pitch, bytes)
        self.pads = [set(), set(), set(), set()]
        self.stick = [(0, 0)] * 4
        self.frames = 0
        self._keep = []
        self._strs = {k: C.c_char_p(v) for k, v in OPTIONS.items()}
        self._dir = C.c_char_p(self.dir)
        L = self.lib
        self._env = ENV_CB(self._env_cb); self._vid = VIDEO_CB(self._video_cb)
        self._aud = AUDIO_CB(lambda l, r: None); self.audio = []
        self.audio_rec = False
        self._audb = AUDIOB_CB(self._audio_cb)
        self.log = []
        self.nlog = 0
        def _lg(lvl, msg):
            self.nlog += 1
            if len(self.log) < 400: self.log.append(msg)
        self._log = LOG_CB(_lg)        # (the first 400 messages kept, all counted)
        self._poll = POLL_CB(lambda: None); self._state = STATE_CB(self._input_cb)
        L.retro_set_environment(self._env)
        L.retro_set_video_refresh(self._vid)
        L.retro_set_audio_sample(self._aud)
        L.retro_set_audio_sample_batch(self._audb)
        L.retro_set_input_poll(self._poll)
        L.retro_set_input_state(self._state)
        L.retro_init()
        self.romdata = open(rom, "rb").read()
        self._rombuf = C.create_string_buffer(self.romdata, len(self.romdata))
        gi = retro_game_info(rom.encode(), C.cast(self._rombuf, C.c_void_p), len(self.romdata), None)
        assert L.retro_load_game(C.byref(gi)), "load_game failed"
        if os.environ.get("N64_DENOM"):     # (test rig: the CPU 2^N times faster against the VI -- no frame drops)
            C.c_uint32.in_dll(L, "CountPerOpDenomPot").value = int(os.environ["N64_DENOM"])
        L.retro_get_memory_data.restype = C.c_void_p
        L.retro_get_memory_size.restype = C.c_size_t
        L.retro_serialize_size.restype = C.c_size_t
        L.retro_serialize.restype = C.c_bool
        L.retro_unserialize.restype = C.c_bool
        self.ram_size = 0x800000
        self._ram = None

    @property
    def ram(self):
        if self._ram is None:
            p = self.lib.retro_get_memory_data(2)
            assert p, "RDRAM not ready (run a frame first)"
            self._ram = (C.c_uint8 * self.ram_size).from_address(p)
        return self._ram

    # --- callbacks ---
    def _env_cb(self, cmd, data):
        if cmd == ENV_GET_VARIABLE:
            v = C.cast(data, C.POINTER(retro_variable)).contents
            s = self._strs.get(v.key)
            if s is None:
                return False
            C.cast(data, C.POINTER(C.c_void_p))[1] = C.cast(s, C.c_void_p).value
            return True
        if cmd == ENV_GET_LOG_INTERFACE:
            C.cast(data, C.POINTER(C.c_void_p))[0] = C.cast(self._log, C.c_void_p).value
            return True
        if cmd == ENV_SET_PIXEL_FORMAT:
            self.pixfmt = C.cast(data, C.POINTER(C.c_int)).contents.value
            return True
        if cmd in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY):
            C.cast(data, C.POINTER(C.c_void_p))[0] = C.cast(self._dir, C.c_void_p).value
            return True
        if cmd == ENV_GET_CAN_DUPE:
            C.cast(data, C.POINTER(C.c_bool))[0] = True
            return True
        if cmd == ENV_GET_AUDIO_VIDEO_ENABLE:
            C.cast(data, C.POINTER(C.c_int))[0] = 1 if self.render else 0
            return True
        return False

    def _audio_cb(self, data, frames):
        if self.audio_rec and data:
            self.audio.append(C.string_at(data, frames * 4))
        return frames

    def _video_cb(self, data, w, h, pitch):
        if data and self.render:
            self.frame = (w, h, pitch, C.string_at(data, pitch * h))

    def _input_cb(self, port, device, index, id_):
        if port > 3:
            return 0
        if device == 1:  # joypad
            return 1 if id_ in self.pads[port] else 0
        if device == 5 and index == 0:  # left analog
            return self.stick[port][id_]
        if device == 5 and index == 2:  # analog buttons (L2 / R2 as triggers)
            return 0x7FFF if (100 + id_) in self.pads[port] else 0
        if device == 5 and index == 1:  # right analog (C buttons)
            return getattr(self, 'rstick', [(0, 0)] * 4)[port][id_]
        return 0

    # --- control ---
    def run(self, n=1, port=0, buttons=(), stick=None, render_last=False):
        self.pads[port] = {BTN[b] for b in buttons}
        if stick is not None:
            self.stick[port] = stick
        for i in range(n):
            self.render = render_last and i == n - 1
            self.lib.retro_run()
            self.frames += 1
        self.pads[port] = set()
        if stick is not None:
            self.stick[port] = (0, 0)

    def press(self, *buttons, port=0, hold=3, after=20):
        self.run(hold, port, buttons)
        self.run(after)

    def shot(self, path, warm=3):
        # frames run with video off aren't drawn by the RDP: draw a few before grabbing one
        self.render = True
        for _ in range(warm):
            self.lib.retro_run(); self.frames += 1
        self.render = False
        self.run(1, render_last=True)
        w, h, pitch, d = self.frame
        out = bytearray()
        for y in range(h):
            row = d[y * pitch:y * pitch + w * 4]
            if self.pixfmt == 1:  # XRGB8888 little endian: B G R X
                for x in range(w):
                    out += bytes((row[4 * x + 2], row[4 * x + 1], row[4 * x]))
            else:
                row = d[y * pitch:y * pitch + w * 2]
                for x in range(w):
                    p = row[2 * x] | (row[2 * x + 1] << 8)
                    out += bytes((((p >> 11) & 31) << 3, ((p >> 5) & 63) << 2, (p & 31) << 3))
        open(path, "wb").write(png_bytes(w, h, bytes(out)))
        return path

    # --- memory (N64 virtual addresses, KSEG0) ---
    def _o(self, a):
        return (a & 0x7FFFFF) ^ 3
    def r8(self, a): return self.ram[self._o(a)]
    def w8(self, a, v): self.ram[self._o(a)] = v & 0xFF
    def r16(self, a): return (self.r8(a) << 8) | self.r8(a + 1)
    def w16(self, a, v): self.w8(a, v >> 8); self.w8(a + 1, v)
    def r32(self, a): return (self.r16(a) << 16) | self.r16(a + 2)
    def w32(self, a, v): self.w16(a, v >> 16); self.w16(a + 2, v)
    def s32(self, a):
        v = self.r32(a); return v - (1 << 32) if v & 0x80000000 else v
    def s16(self, a):
        v = self.r16(a); return v - (1 << 16) if v & 0x8000 else v
    def s8(self, a):
        v = self.r8(a); return v - 256 if v & 0x80 else v
    def dump(self):
        """Whole RDRAM as big-endian bytes (index = address & 0x7FFFFF)."""
        b = bytes(self.ram)
        a = memoryview(b).cast("I")
        return struct.pack(">%dI" % len(a), *a)

    def save(self):
        n = self.lib.retro_serialize_size()
        buf = C.create_string_buffer(n)
        assert self.lib.retro_serialize(buf, n)
        return (buf.raw, self.frames)
    def load(self, st):
        raw, fr = st
        buf = C.create_string_buffer(raw, len(raw))
        assert self.lib.retro_unserialize(buf, len(raw))
        self.frames = fr


def fbshot(m, path, w=480, h=240):
    """Screenshot from RDRAM: the framebuffer the game is displaying (osViGetCurrentFramebuffer)."""
    import numpy as np
    from PIL import Image
    fb = m.r32(m.r32(0x80054BF0) + 4) & 0x7FFFFF
    d = m.dump()[fb:fb + w * h * 2]
    px = np.frombuffer(d, '>u2').reshape(h, w).astype(np.uint32)
    r = ((px >> 11) & 31) << 3; g = ((px >> 6) & 31) << 3; b = ((px >> 1) & 31) << 3
    img = np.stack([r, g, b], axis=-1).astype(np.uint8)
    Image.fromarray(img).resize((640, 480), Image.NEAREST).save(path)
    return path
