"""revplay.py (revmusic, 2026-11-18): WCW/nWo Revenge's songs played by us from its own data, as Revenge plays them -- no
recording. The reference for the C++ version (tools\\romimport\\native\\revplay.cpp): both give the same bytes.

Revenge's music is its OWN sequencer (not libultra's sequence player) driving libultra's n_audio synthesizer:
  * a song (Revenge files 0x210-0x224) is up to 24 tracks; each track has one voice, an event stream (notes, rests and
    0x80-0xAA commands), a volume stream and a pitch-bend stream (both run-length coded), an envelope table and a drum table.
  * the sequencer runs every 16666 us (an ALPlayer: 480 output samples at the game's rate): every track's clock advances
    by its tempo (409/256 of a duration unit by default), due events are read, then the envelope / streams / pitch / volume
    / pan are updated and sent to the synthesizer (Revenge's code: song start 0x8001381C, tick 0x80014500, events 0x8001488C,
    the commands' jump table 0x80036E80, envelope 0x80015118, pitch 0x80014F5C with its own 2^x 0x80015858, volume and pan
    0x80014DF4). This file mirrors that code step for step, its float arithmetic in single precision as the N64 does it.
  * the instruments are the waves of the bank "proset.wbk" (an "N64 PtrTablesV2" bank in the ROM): VADPCM samples, a root
    key and a detune each (Revenge's loader 0x800159EC).
  * the synthesizer (n_audio, as the game's RSP microcode does it): each voice's wave decoded (VADPCM), resampled at its pitch
    (the 4-tap filter of the microcode's table, in the ROM), enveloped (its volume squared, the pan's equal-power table,
    linear ramps), mixed dry and into the effect bus; the effect is libultra's BIGROOM reverb; all in 184-sample chunks.

Everything is integer arithmetic except the sequencer's single-precision floats (mirrored exactly: f32() after every
operation) and a few double-precision steps of Revenge's own code (Python floats are IEEE doubles, as C++'s). No library
float function (sin, pow, exp) is used anywhere: the results are the same on every machine.

    python revplay.py [<song file, hex> [<out.wav>]]     (the song as the game plays it: 28804 Hz stereo)"""
import os, struct, sys

# ---------------------------------------------------------------------------------------------------- single precision
_F = struct.Struct(">f")
def f32(x):
    """x rounded to IEEE single precision (what the N64's FPU keeps after a .s operation)"""
    return _F.unpack(_F.pack(x))[0]
def s8(v): v &= 0xFF; return v - 256 if v & 0x80 else v
def s16(v): v &= 0xFFFF; return v - 65536 if v & 0x8000 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v & 0x80000000 else v
def trunc(x):
    """trunc.w: toward zero"""
    return int(x)
M64 = (1 << 64) - 1

# ---------------------------------------------------------------------------------------------------- the ROM's data
REV_CODE = 0x80000400 - 0x1000            # Revenge's main code: RAM address - this = ROM offset
BANK_MAGIC = b"N64 PtrTablesV2\x00"      # (the bank's header; vanilla Revenge names it proset.wbk, Revenge Redux leaves it blank)
# Where the game's own boot code says its music bank and its files are (each a lui / addiu pair: the ROM address it loads).
# Read from the code, never assumed, so a patched Revenge is played from its own data: WCW/nWo Revenge Redux (the user's
# decision C, 2026-11-18; gen/reduxmusic.py) moves the bank, its samples and the file table past vanilla's 16 MB.
BANK_AT = (0x80003134, 0x80003138, 0x3C11, 0x2631)        # the bank's header (lui / addiu s1)
SAMPLES_AT = (0x800031D4, 0x800031D8, 0x3C02, 0x2442)     # the bank's sample data (lui / addiu v0: the sequencer's set-up)
FILES_AT = (0x800034C8, 0x800034CC, 0x3C04, 0x2484)       # the file table (lui / addiu a0)
FILES_SIZE = 0x800034A4                                   # the table's size in bytes (li a0: an addiu or ori from zero)
FILES_BASE = (0x80003510, 0x80003514, 0x3C14, 0x2694)     # the files' offsets count from here (lui / addiu s4)
def code_word(rom, a): return struct.unpack_from(">I", rom, a - REV_CODE)[0]
def code_addr(rom, at):
    """the address a lui / addiu pair of the game's code loads (the addiu's immediate is signed)"""
    hi_a, lo_a, hi_op, lo_op = at
    hi, lo = code_word(rom, hi_a), code_word(rom, lo_a)
    assert hi >> 16 == hi_op and lo >> 16 == lo_op, "the game's code at 0x%08X is not the one this player knows" % hi_a
    return ((hi & 0xFFFF) << 16) + s16(lo)
def game_files(rom):
    """the game's files by number (tools\\romimport\\roms.py RomFiles: AKI's table and LZSS), from the table its boot code
    reads (for vanilla Revenge the same table, count and base as aki.romtable's GAMES row)"""
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
                                    "tools", "romimport"))
    from roms import RomFiles
    op = code_word(rom, FILES_SIZE)
    assert op >> 16 in (0x2404, 0x3404), "the file table's size in the game's code"
    size = s16(op) if op >> 16 == 0x2404 else op & 0xFFFF
    rf = RomFiles.__new__(RomFiles)
    rf.rom, rf.sha1, rf.n, rf.base, rf.cache = rom, None, size // 4, code_addr(rom, FILES_BASE), {}
    rf.t = struct.unpack_from(">%dI" % rf.n, rom, code_addr(rom, FILES_AT))
    return rf
RESAMPLE_LUT = 0x8003A670                 # the audio microcode's resampler table (64 phases x 4 taps), in Revenge's data
EQPOWER = 0x800370F0                      # libultra's equal-power table (128)
SONG_FIRST, SONG_LAST = 0x210, 0x224

class Bank:
    """the music bank (vanilla's proset.wbk): waves (VADPCM) and their tunings, as Revenge's loader makes them"""
    def __init__(self, rom):
        at = code_addr(rom, BANK_AT)
        assert rom[at:at + len(BANK_MAGIC)] == BANK_MAGIC, "the music bank where the game's code says"
        w = lambda o: struct.unpack_from(">I", rom, at + o)[0]
        base = code_addr(rom, SAMPLES_AT)
        n, keys, tunes, waves = w(0x20), w(0x24), w(0x28), w(0x2C)
        self.tuning, self.waves = [], []
        for i in range(n):
            # tuning = detune / 100 + (root byte - 48), in single precision (0x80015A60..)
            t = f32(f32(float(s8(rom[at + tunes + 4 * i]))) / 100.0)
            t = f32(t + float(s8(rom[at + keys + i] - 48)))
            self.tuning.append(t)
            o = at + w(waves + 4 * i)
            wb, ln, typ = struct.unpack_from(">IIB", rom, o)
            loop, book = struct.unpack_from(">II", rom, o + 12)
            assert typ == 0, "VADPCM waves only"
            start = wb if (wb & 0xFF000000) == 0xFF000000 else wb + base
            order, npred = struct.unpack_from(">ii", rom, at + book)
            assert order == 2
            bk = struct.unpack_from(">%dh" % (order * npred * 8), rom, at + book + 8)
            lp = None
            if loop:
                ls, le, lc = struct.unpack_from(">IIi", rom, at + loop)
                lp = (ls, le, lc, struct.unpack_from(">16h", rom, at + loop + 12))
            self.waves.append(Wave(rom[start:start + ln], bk, npred, lp))

class Wave:
    def __init__(self, data, book, npred, loop):
        self.data, self.book, self.npred, self.loop = data, book, npred, loop
        self._pcm = None
    def pcm(self):
        """(the decoded samples up to the loop's end -- or the whole wave --, the loop's samples or None)"""
        if self._pcm is None:
            nf = len(self.data) // 9
            if self.loop is None:
                self._pcm = (vadpcm_decode(self.data, self.book, 0, nf, 0, 0), None)
            else:
                ls, le, lc, st = self.loop
                assert lc == -1, "loops that end are not used by the songs"
                first = vadpcm_decode(self.data, self.book, 0, (le + 15) // 16, 0, 0)[:le]
                f0 = ls // 16
                seg = vadpcm_decode(self.data, self.book, f0, (le + 15) // 16, st[14], st[15])[ls - 16 * f0:le - 16 * f0]
                self._pcm = (first, seg)
            self._pcm = tuple(None if a is None else np.array(a, np.int64) for a in self._pcm)
        return self._pcm

def clamp16(v): return -32768 if v < -32768 else 32767 if v > 32767 else v

def vadpcm_decode(data, book, f0, f1, l1, l2):
    """VADPCM frames f0..f1-1 (9 bytes: a header -- scale, predictor -- and 16 4-bit residuals), order 2, from the history
    (l1, l2) = (second last, last) sample: the microcode's ADPCM decode"""
    out = []
    for f in range(f0, f1):
        h = data[9 * f]
        scale, pred = h >> 4, h & 15
        rs = 12 - scale if scale < 12 else 0
        cb1 = book[16 * pred: 16 * pred + 8]; cb2 = book[16 * pred + 8: 16 * pred + 16]
        res = []
        for b in data[9 * f + 1: 9 * f + 9]:
            res.append(s16((b & 0xF0) << 8) >> rs)
            res.append(s16((b & 0x0F) << 12) >> rs)
        for half in (0, 8):
            src = res[half:half + 8]
            for i in range(8):
                acc = (src[i] << 11) + cb1[i] * l1 + cb2[i] * l2
                for j in range(i): acc += cb2[j] * src[i - j - 1]
                out.append(clamp16(acc >> 11))
            l1, l2 = out[-2], out[-1]
    return out

class Song:
    """a song file: its tracks' three streams, its envelope and drum tables (offsets in the file)"""
    def __init__(self, data):
        self.d = data
        w = lambda o: struct.unpack_from(">I", data, o)[0]
        self.ntracks = w(0)
        A, B, C, self.env, self.drums = w(4), w(8), w(12), w(16), w(20)
        self.tracks = []
        for i in range(self.ntracks):
            a = w(A + 4 * i)
            if a: self.tracks.append((i, a, w(B + 4 * i), w(C + 4 * i)))

# ---------------------------------------------------------------------------------------------------- the sequencer
TEMPO0 = 24576 // 60          # the default tempo (0x80015C50: 24576 / the 60 Hz retrace)
RAMP_US = 16666               # every volume change is a ramp of one tick (0x800604E0 = 1000000 / 60)
MASTER = 32767                # the music's master volume (0x800604E6, set to 32767 at start)

class Track:
    """a track's state: Revenge's 344-byte track struct (its offsets in the comments)"""
    def __init__(self, song, bank, i, a, b, c):
        self.song, self.bank, self.num = song, bank, i
        self.pc = a                    # +4   event stream
        self.wave_due = None           # +8   a wave to start at the next tick
        self.now = 0                   # +16  the track's clock (64 bits; 256 = one duration unit)
        self.bt = 0; self.ct = 0       # +32 / +40  the volume / pitch-bend streams' clocks
        self.bend = f32(0.0)           # +56  pitch bend in semitones
        self.last_pitch = f32(99.9)    # +60
        self.pitch = f32(0.0)          # +64  the note's pitch (tuning + note + transpose)
        self.fine = f32(0.0)           # +68  fine tune
        self.cp = c; self.bp = b       # +72 / +76 the pitch-bend / volume streams
        self.next = 0                  # +80  the next event's time
        self.start = 0                 # +88  the note's start
        self.porta_from = f32(0.0)     # +104
        self.porta_at = f32(0.0)       # +108
        self.release = 0               # +112 the note's release time
        self.rate1 = f32(1.0); self.rate2 = f32(1.0 / 255.0); self.rate3 = f32(1.0 / 15.0)   # +120/+124/+128
        self.step = 0                  # +132 1024 / envelope period
        self.bend_scale = f32(0.03125) # +140
        self.bend_raw = f32(0.0)       # +144
        self.fine_last = f32(0.0)      # +172
        self.voltrack = 128            # +190
        self.sent_vol = 0xFFFF         # +192
        self.bcount = 1; self.ccount = 1   # +194 / +196
        self.dur = 1                   # +186 the note's length
        self.tempo_scale = 128         # +184
        self.tempo = TEMPO0            # +188
        self.tempo_base = TEMPO0       # +200
        self.elapsed = 0               # +202 ticks since the note started
        self.fixed_dur = 0             # +204
        self.instr = 0                 # +206
        self.panscale = 128            # +208
        self.gate_abs = 0; self.gate_rel = 0   # +210 / +212
        self.read_dur_once = 0         # +215
        self.porta = 0                 # +216
        self.transpose = 0; self.no_transpose = 0   # +217 / +218
        self.vel = 0                   # +219
        self.vol = 127                 # +220
        self.pan = 64                  # +221
        self.sent_pan = 255            # +222
        self.e_period = 1; self.e_l0 = 0; self.e_l1 = 127; self.e_l2 = 127   # +223..+226
        self.e_phase = 0; self.e_level = 0; self.e_count = 0                 # +227..+229
        self.e_t1 = 1; self.e_t2 = 255; self.e_t3 = 15                         # +230..+232
        self.voice_on = 0              # +233
        self.fx = 0; self.fx_base = 0; self.sent_fx = 255   # +234..+236
        self.e_rel_level = 0           # +237
        self.tr_on = 0; self.tr_off = 0; self.tr_count = 0; self.tr_val = 0; self.tr_amt = 0   # +238 +239 +240 +241 +248
        self.vel_each = 0; self.vel_default = 127   # +242 / +243
        self.legato = 0; self.tie = 0  # +246 / +247
        self.loops = []                # +251.. the loop stack: [pc, bp, cp, vol, count, bend_raw, bcount, ccount]

    def b(self):
        v = self.song.d[self.pc]; self.pc += 1; return v

    def len_(self, first):
        """a length: one byte under 0x80, else 15 bits in two"""
        if first < 0x80: return first
        return ((first & 0x7F) << 8) + self.b()

class Player:
    """Revenge's sequencer over one song; tick() = one call of its ALPlayer (0x80014500) -> the synthesizer calls it made:
    (voice, "start", wave) / ("stop",) / ("pitch", ratio) / ("vol", v, us) / ("pan", p) / ("fx", f)"""
    def __init__(self, song, bank):
        self.song, self.bank = song, bank
        self.tracks = [Track(song, bank, *t) for t in song.tracks]
        self.out = []

    def tick(self):
        self.out = []
        for v, t in enumerate(self.tracks):
            if not t.pc: continue
            if t.wave_due is not None: self.start_voice(t, v)
            t.now += t.tempo
            if t.dur != 0x7FFF:
                while t.next < t.now:
                    if not t.pc: break
                    self.event(t, v)
                if not t.pc: continue
            if t.bp and t.bt < t.now: self.vol_stream(t)
            if t.cp and t.ct < t.now: self.bend_stream(t)
            if t.voice_on:
                if t.e_phase: self.envelope(t)
                f20 = t.fine
                if t.tr_on: f20 = f32(f20 + self.trill(t))
                if t.wave_due is None:
                    self.set_pitch(t, v, f20)
                    self.set_vol_pan(t, v)
            t.elapsed = ((t.now - t.start) >> 8) & 0xFFFF
        return self.out

    # -- the synthesizer's calls
    def start_voice(self, t, v):                       # 0x80014D78
        if t.voice_on: self.out.append((v, "stop"))
        t.voice_on = 1
        self.out.append((v, "start", t.wave_due))
        t.wave_due = None

    # -- events (0x8001488C)
    def event(self, t, v):
        while True:
            op = t.song.d[t.pc]
            if op < 0x80: break
            t.pc += 1
            getattr(self, "op_%02x" % op, self.op_unknown)(t, op)
            if not t.pc:
                if t.voice_on:
                    t.voice_on = 0
                    self.out.append((v, "vol", 0, RAMP_US)); self.out.append((v, "stop"))
                return
        t.porta_from = t.porta_at
        note = t.b()
        t.vel = t.b() if t.vel_each else t.vel_default
        if t.fixed_dur:
            if t.read_dur_once:
                t.read_dur_once = 0; t.dur = t.len_(t.b())
            else: t.dur = t.fixed_dur
        else: t.dur = t.len_(t.b())
        t.elapsed = 0; t.tr_val = 0
        t.start = t.next; t.tr_count = t.tr_off
        t.next = (t.next + (t.dur << 8)) & M64
        if note == 0x60:                                   # a rest: the note's release
            if t.e_phase < 4:
                t.e_phase = 4; t.e_count = 1; t.release = t.now; t.e_rel_level = t.e_level
            return
        if t.legato == 0: self.env_start(t)
        instr = t.instr
        if instr >= len(t.bank.waves): raise ValueError("song: an instrument the bank has not got (%d)" % instr)
        if not t.tie:
            t.wave_due = instr
            if t.voice_on and t.sent_vol != 0:
                t.sent_vol = 0; self.out.append((v, "vol", 0, RAMP_US))
            else: self.start_voice(t, v)
        p = f32(float(note) + t.bank.tuning[instr])
        tr = s8((t.transpose * (1 - t.no_transpose)) & 0xFF)
        t.pitch = f32(p + float(tr))
        if t.fx != t.sent_fx:
            t.sent_fx = t.fx
            self.out.append((v, "fx", (t.fx_base + (((128 - t.fx_base) * t.fx) >> 7)) & 0xFF))

    def env_start(self, t):                            # 0x80015068
        if t.dur == 0x7FFF: t.release = 0x7FFFFFFF
        elif t.gate_abs: t.release = (t.start + (t.gate_abs << 8)) & M64
        else: t.release = (t.next - (t.gate_rel << 8)) & M64
        t.e_phase = 1; t.e_level = t.e_l0; t.e_count = t.e_period

    def set_env(self, t, e):                           # a 7-byte envelope (0x90 / 0x84 / the drum table)
        a = e[0] or 1
        t.e_period = a; t.step = trunc(1024 / a)
        t.e_l0 = e[1]
        t.e_t1 = e[2]; t.rate1 = f32((1.0 / f32(float(e[2]))) * f32(float(e[3] - t.e_l0)))
        t.e_l1 = e[3]
        t.e_t2 = e[4]; t.rate2 = f32((1.0 / f32(float(e[4]))) * f32(float(e[5] - e[3])))
        t.e_l2 = e[5]
        t.e_t3 = e[6]; t.rate3 = f32(1.0 / f32(float(e[6])))

    def op_unknown(self, t, op):
        raise NotImplementedError("Revenge song command 0x%02X (not used by the songs this player was made for)" % op)
    def op_80(self, t, op):                            # end of the track
        t.bp = t.cp = 0; t.pc = 0; t.wave_due = None
    def op_81(self, t, op):                            # instrument
        t.instr = t.len_(t.b())
    def op_82(self, t, op):                            # portamento length
        t.porta = t.b()
        if t.porta: t.porta_at = t.pitch
    def op_83(self, t, op): t.porta = 0
    def op_84(self, t, op):                            # an envelope, in the stream
        e = t.song.d[t.pc:t.pc + 7]; t.pc += 7; self.set_env(t, e)
    def op_85(self, t, op):                            # tempo (0x800162EC): for every track of the song
        base = (t.b() * 24576) // 120 // 60
        for u in self.tracks:
            u.tempo_base = base; u.tempo = (base * u.tempo_scale) >> 7
    def op_86(self, t, op):                            # release a fixed time after the start
        hi = t.b(); t.gate_rel = 0; t.gate_abs = (hi << 8) | t.b()
    def op_87(self, t, op):                            # release a time before the end
        t.gate_abs = 0; t.gate_rel = t.b()
    def op_8a(self, t, op): pass                       # vibrato off (the songs here use no vibrato: 0x88 / 0x89)
    def op_8b(self, t, op):                            # fixed note length
        t.fixed_dur = t.len_(t.b())
    def op_8c(self, t, op): t.read_dur_once = 1
    def op_8d(self, t, op): t.transpose = t.b()
    def op_8e(self, t, op): t.no_transpose = 1
    def op_8f(self, t, op):                            # fine tune (cents)
        f = f32(f32(float(s8(t.b()))) / 100.0)
        t.fine = f32(f32(t.fine - t.fine_last) + f); t.fine_last = f
    def op_90(self, t, op):                            # an envelope from the song's table
        n = t.len_(t.b())
        o = t.song.env + 7 * n
        self.set_env(t, t.song.d[o:o + 7])
    def op_91(self, t, op): t.legato = 1
    def op_92(self, t, op): t.legato = 0
    def op_93(self, t, op): t.tie = 1
    def op_94(self, t, op): t.tie = 0
    def op_95(self, t, op):                            # loop start (count; 255 = for ever)
        n = t.b()
        # (the bend is kept as a byte and read back unsigned: a game quirk, mirrored)
        t.loops.append([t.pc, t.bp, t.cp, t.vol, n, trunc(t.bend_raw) & 0xFF, t.bcount, t.ccount])
    def op_96(self, t, op):                            # loop end (0x800133AC)
        k = len(t.loops) - 1
        L = t.loops[k]
        if L[4] != 255:
            L[4] = (L[4] - 1) & 0xFF
            if L[4] == 0: t.loops.pop(); k = -1
        if k >= 0:
            t.pc, t.bp, t.cp, t.vol = L[0], L[1], L[2], L[3]
            t.bend_raw = f32(float(L[5]))
            t.bend = f32(t.bend_raw * t.bend_scale)
            t.bcount, t.ccount = L[6], L[7]
        t.sent_vol = 0xFFFF; t.sent_fx = 255; t.sent_pan = 255; t.pan = 64
        t.last_pitch = f32(99.9); t.bend_scale = f32(0.03125)
    def op_97(self, t, op):                            # trill: amount, on / off ticks
        t.tr_amt = t.b(); t.tr_on = t.b(); t.tr_off = t.b()
    def op_98(self, t, op): t.tr_on = 0
    def op_99(self, t, op): t.vel_each = 1
    def op_9a(self, t, op): t.vel_each = 0
    def op_9b(self, t, op): t.vel_each = 0; t.vel_default = t.b()
    def op_9c(self, t, op): t.sent_pan = 255; t.pan = t.b() >> 1
    def op_9d(self, t, op): t.pc += 2                   # (two bytes skipped: 0x80016730)
    def op_9f(self, t, op): pass                       # drum table off (no song here has one)
    def op_a0(self, t, op): t.pc += 1                   # (a byte skipped: 0x8001677C)
    def op_a2(self, t, op): t.fx = t.b()
    def op_a6(self, t, op): t.vol = t.b()
    def op_a8(self, t, op):                            # pitch-bend range (0x800169D4: bend = raw * raw, as the game does)
        t.bend = f32(t.bend_raw * t.bend_raw)
        t.bend_scale = f32(float(t.b()) * 0.015625)
    def op_a9(self, t, op):
        if t.b(): raise NotImplementedError("Revenge song command 0xA9 (pan sweep)")
    def op_aa(self, t, op): t.pc += 1

    # -- per tick
    def envelope(self, t):                             # 0x80015118
        if not (t.now < t.release) and t.e_phase < 4:
            t.e_phase = 4; t.e_count = 1; t.e_rel_level = t.e_level
        t.e_count = (t.e_count - 1) & 0xFF
        if t.e_count: return
        t.e_count = t.e_period
        def x_of(d):                                   # ((d >> 8) * step) >> 10 in 64 bits, its low word signed
            return s32((((d & M64) >> 8) * (t.step & M64) & M64) >> 10)
        ph = t.e_phase
        if ph == 1:
            x = x_of(t.now - t.start)
            if x < t.e_t1: t.e_level = (t.e_l0 + trunc(f32(t.rate1 * f32(float(x))))) & 0xFF
            else: t.e_phase = 2; t.e_level = t.e_l1
        elif ph == 2:
            d8 = (((t.now - t.start) & M64) >> 8) - t.e_t1
            x = s32((((d8 & M64) * (t.step & M64)) & M64) >> 10)
            if x < t.e_t2: t.e_level = (t.e_l1 + trunc(f32(t.rate2 * f32(float(x))))) & 0xFF
            else: t.e_phase = 3; t.e_level = t.e_l2
        elif ph == 4:
            x = x_of(t.now - t.release)
            if x < t.e_t3:
                f = f32(f32(t.rate3 * f32(float(x))) * f32(float(t.e_rel_level)))
                t.e_level = (t.e_rel_level - trunc(f)) & 0xFF
            else: t.e_phase = 5; t.e_level = 0

    def vol_stream(self, t):                           # 0x80015628
        d = t.song.d
        while True:
            t.bcount = (t.bcount - 1) & 0xFFFF; t.bt += 256
            if t.bcount == 0:
                c = d[t.bp]; t.bp += 1
                if c < 0x80: t.vol = c; t.bcount = 1
                else:
                    t.vol = c & 0x7F
                    n = d[t.bp]; t.bp += 1
                    if n < 0x80: t.bcount = n + 2
                    else: t.bcount = (((n & 0x7F) << 8) + 2 + d[t.bp]) & 0xFFFF; t.bp += 1
            if not (t.bt < t.now): break

    def bend_stream(self, t):                          # 0x80015718
        d = t.song.d
        while True:
            t.ccount = (t.ccount - 1) & 0xFFFF; t.ct += 256
            if t.ccount == 0:
                c = d[t.cp]; t.cp += 1
                t.bend_raw = f32(f32(float(c & 0x7F)) - 64.0)
                t.bend = f32(t.bend_raw * t.bend_scale)
                if c < 0x80: t.ccount = 1
                else:
                    n = d[t.cp]; t.cp += 1
                    if n < 0x80: t.ccount = n + 2
                    else: t.ccount = (((n & 0x7F) << 8) + 2 + d[t.cp]) & 0xFFFF; t.cp += 1
            if not (t.ct < t.now): break

    def trill(self, t):                                # 0x80015574
        t.tr_count = (t.tr_count - 1) & 0xFF
        if t.tr_count == 0:
            if s8(t.tr_val) == 0: t.tr_val = t.tr_amt; t.tr_count = t.tr_on
            else: t.tr_val = 0; t.tr_count = t.tr_off
        return float(s8(t.tr_val))

    def set_pitch(self, t, v, f8):                     # 0x80014F5C
        f6 = t.pitch
        if t.porta:
            if t.porta < t.elapsed: t.porta_at = f6
            else:
                f0 = f32(f32(f32(t.pitch - t.porta_from) / float(t.porta)) * float(t.elapsed))
                f6 = f32(t.porta_from + f0); t.porta_at = f6
        f6 = f32(f6 + f32(f8 + t.bend))
        if f6 == t.last_pitch: return
        t.last_pitch = f6
        r = pow2(f32(f6 * (1.0 / 12.0)))
        if 2.0 < r: r = 2.0; t.vel = 0
        self.out.append((v, "pitch", r))

    def set_vol_pan(self, t, v):                       # 0x80014DF4
        a = ((((t.vol * t.e_level) & 0xFFFFFFFF) * t.vel & 0xFFFFFFFF) * s16(t.voltrack)) & 0xFFFFFFFF
        a >>= 13
        if a > 32767: a = 32767
        a = ((a * MASTER) & 0xFFFFFFFF) >> 15
        if a != t.sent_vol:
            t.sent_vol = a
            self.out.append((v, "vol", s16(a), RAMP_US))
        p = ((t.pan * s16(t.panscale)) & 0xFFFFFFFF) >> 7 & 0x7F
        if p != t.sent_pan:
            t.sent_pan = p
            self.out.append((v, "pan", p))

C_LN2 = (0.693147180559945, 0.240226506959101, 0.0555041086648216, 0.00961812910762848, 0.00133335581464284,
         0.000154035303933816)                     # Revenge's 2^x series (0x8003B348..)
def pow2(x):
    """Revenge's 2^x (0x80015858): its series in double precision on single-precision powers; 1 / that for x < 0"""
    if x == 0.0: return 1.0
    c1, c2, c3, c4, c5, c6 = C_LN2
    if 0.0 < x:
        x2 = f32(x * x)
        a = x * c1; b = x2 * c2; c = f32(x2 * x) * c3
        x4 = f32(x2 * x2)
        d = x4 * c4; e = f32(x4 * x) * c5; g = f32(x4 * x2) * c6
        r = a + 1.0; r += b; r += c; r += d; r += e; r += g
        return f32(r)
    x = -x
    x2 = f32(x * x)
    a = x * c1; b = x2 * c2; c = f32(x2 * x) * c3
    x4 = f32(x2 * x2)
    d = x4 * c4; e = f32(x4 * x) * c5; g = f32(x4 * x2) * c6
    r = a + 1.0; r += b; r += c; r += d; r += e; r += g
    return f32(1.0 / r)


# ---------------------------------------------------------------------------------------------------- the synthesizer
import numpy as np
CHUNK = 184                   # n_audio works in 184-sample chunks; parameter changes land on their boundaries
TICK = 480                    # the sequencer's period in output samples (16666 us at the game's 28800 Hz)
RAMP = (TICK + CHUNK // 2) // CHUNK * CHUNK      # a volume ramp: 16666 us -> 480 samples -> 552 (whole chunks)
MAX_RATIO = f32(1.99996)
FX_LEN = 4000                 # BIGROOM: the delay line (100 ms at libultra's 40 samples a millisecond)
# (input tap, output tap, ff, fb, gain) per section. The fourth section's low-pass is left out: n_alFxPull (Revenge
# 0x80019970) filters that section's output AFTER using it for the feedback, then saves it where nothing reads it before
# the next input overwrites it -- so it changes nothing.
BIGROOM = ((0, 2640, -9830, 9830, 0), (880, 2160, -3276, 3276, 0x3FFF), (2640, 3640, -3276, 3276, 0x3FFF),
           (0, 3760, 0, 8000, 0))

def clamp16a(a): return np.clip(a, -32768, 32767)
def vmulf(x, g):
    """the microcode's mix multiply: (x * g + 0x4000) >> 15"""
    return (x * g + 0x4000) >> 15

def get_rate(vol, tgt, count):
    """libultra n_audio's _getRate (Revenge 0x80018EE4): the linear ramp's step per 8 samples, 16.16 -> (ratm, ratl)"""
    if count == 0:
        return (0x7FFF, 0xFFFF) if vol <= tgt else (0, 0)
    invn = 1.0 / count
    if tgt < 1.0: tgt = 1.0
    if vol <= 0.0: vol = 1.0
    a = (tgt - vol) * invn * 8.0
    m = trunc(a)
    f = a - float(s16(m))
    f = f + 1.0
    a0 = trunc(f)
    f = f - float(s16(a0))
    f2 = f * 65535.0
    ratel = (trunc(f2 - 2147483648.0) | 0x8000) & 0xFFFF if 2147483648.0 <= f2 else trunc(f2) & 0xFFFF
    return s16(m - 1 + a0), ratel

def get_vol(ivol, samples, ratm, ratl):
    """libultra n_audio's _getVol (0x80019024): where the linear ramp is after 'samples'"""
    samples >>= 3
    if samples == 0: return ivol
    return s16(ivol + (s32(ratl * samples) >> 16) + s32(ratm * samples))

def cdiv(a, b):
    """C's integer division (toward zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q

class Voice:
    def __init__(self):
        self.stream = None          # (first, loop) samples of the wave playing
        self.pos = 0                # 16.16 position in [4 zeros] + the wave
        self.pitch = 0              # 16.16 step per output sample
        self.volume = 1; self.pan = 64; self.dry = 32767; self.wet = 0
        self.cvol = [1, 1]; self.rat = [(0, 0), (0, 0)]
        self.delta = 0; self.seg = 0; self.dirty = False
        self.ramp = [[0, 0, 0], [0, 0, 0]]   # the microcode's [value, target, step] per side (16.16)

    def samples(self, lo, hi):
        """samples lo..hi-1 of [0, 0, 0, 0] + the wave (its loop repeated for ever; zeros past an unlooped end)"""
        first, seg = self.stream
        idx = np.arange(lo, hi, dtype=np.int64) - 4
        out = np.zeros(hi - lo, np.int64)
        a = (idx >= 0) & (idx < len(first))
        out[a] = first[idx[a]]
        if seg is not None:
            b = idx >= len(first)
            out[b] = seg[(idx[b] - len(first)) % len(seg)]
        return out

def synth_update(v, ev, eq, first):
    """one of the sequencer's calls as n_audio's voice and envelope mixer take it (Revenge 0x80018898, 0x80018A58 ..).
    first: the voice's first change at this chunk boundary. n_audio renders up to a change's time before taking it; for the
    changes after the first at the same time there is nothing left to render, and its envelope mixer then takes the ramp as
    finished (the volume at its target at once) -- which is how Revenge's notes with a pan or effect change after their
    volume start at full volume, with no ramp."""
    kind = ev[1]
    if kind == "start":                 # the wave from its start (the decoder and resampler begin afresh)
        v.stream = ev[2]; v.pos = 0
        return
    if kind == "stop":                  # AL_FILTER_RESET: silent, its volume 1, the ramp to be recomputed
        v.stream = None; v.volume = 1; v.dirty = True
        return
    if kind == "pitch":
        r = ev[2]
        if r > 1.99996: r = MAX_RATIO
        v.pitch = trunc(f32(r * 32768.0)) * 2
        return
    # volume / pan / fx: where the ramp is now, then the change
    if not first or v.delta >= v.seg:
        v.cvol = [(v.volume * eq[v.pan]) >> 15, (v.volume * eq[127 - v.pan]) >> 15]
        v.delta = v.seg
    else:
        v.cvol = [get_vol(v.cvol[k], v.delta, *v.rat[k]) for k in (0, 1)]
    v.cvol = [c or 1 for c in v.cvol]
    if kind == "pan": v.pan = ev[2]
    elif kind == "vol":
        v.delta = 0; v.volume = (ev[2] * ev[2]) >> 15; v.seg = RAMP
    elif kind == "fx": v.dry = eq[ev[2]]; v.wet = eq[127 - ev[2]]
    v.dirty = True

def ramp_run(r, n):
    """n samples of the microcode's linear ramp (value += step until it reaches the target; 32-bit) -> volumes (16-bit)"""
    value, target, step = r
    vals = value + step * np.arange(1, n + 1, dtype=np.int64)
    vals = ((vals + (1 << 31)) & 0xFFFFFFFF) - (1 << 31)          # 32-bit wrap, as the microcode's registers
    reached = (vals <= target) if step <= 0 else (vals >= target)
    hit = np.flatnonzero(reached)
    if len(hit):
        k = int(hit[0]); vals[k:] = target; r[0] = target; r[2] = 0
    else: r[0] = int(vals[-1])
    return (((vals >> 16) + 32768) & 0xFFFF) - 32768

class Synth:
    """n_audio for one song: voices, the main and effect buses, BIGROOM; chunk() -> one chunk of stereo"""
    def __init__(self, rom, nvoices):
        rd = lambda a, n: struct.unpack_from(">%dh" % n, rom, a - REV_CODE)
        self.lut = np.array(rd(RESAMPLE_LUT, 256), np.int64).reshape(64, 4)
        self.eq = rd(EQPOWER, 128)
        assert self.lut[0].tolist() == [0x0C39, 0x66AD, 0x0D46, -33] and self.eq[0] == 32767 and self.eq[127] == 0
        self.voices = [Voice() for _ in range(nvoices)]
        self.line = np.zeros(FX_LEN, np.int64); self.w = 0

    def chunk(self):
        n = CHUNK
        z = lambda: np.zeros(n, np.int64)
        dl, dr, wl, wr = z(), z(), z(), z()
        for v in self.voices:
            if v.stream is None: continue          # a stopped voice: not pulled (its ramp's clock stands still)
            if v.dirty:
                tg = [(v.volume * self.eq[v.pan]) >> 15, (v.volume * self.eq[127 - v.pan]) >> 15]
                v.rat = [get_rate(float(v.cvol[k]), float(tg[k]), v.seg) for k in (0, 1)]
                for k in (0, 1):
                    rate = s32((v.rat[k][0] << 16) | v.rat[k][1])
                    v.ramp[k] = [v.cvol[k] << 16, tg[k] << 16, cdiv(rate, 8)]
                v.dirty = False
            if True:
                p = v.pos + v.pitch * np.arange(n, dtype=np.int64)
                i = p >> 16
                x = v.samples(int(i[0]), int(i[-1]) + 4)
                j = i - i[0]
                c = self.lut[(p & 0xFFFF) >> 10]
                s = clamp16a((x[j] * c[:, 0] + x[j + 1] * c[:, 1] + x[j + 2] * c[:, 2] + x[j + 3] * c[:, 3]) >> 15)
                v.pos += v.pitch * n
                lv = ramp_run(v.ramp[0], n); rv = ramp_run(v.ramp[1], n)
                g0 = clamp16a((lv * v.dry + 0x4000) >> 15); g1 = clamp16a((rv * v.dry + 0x4000) >> 15)
                g2 = clamp16a((lv * v.wet + 0x4000) >> 15); g3 = clamp16a((rv * v.wet + 0x4000) >> 15)
                dl = clamp16a(dl + vmulf(s, g0)); dr = clamp16a(dr + vmulf(s, g1))
                wl = clamp16a(wl + vmulf(s, g2)); wr = clamp16a(wr + vmulf(s, g3))
            v.delta += n
        fx = self.reverb(wl, wr)
        return clamp16a(dl + vmulf(fx, 0x7FFF)), clamp16a(dr + vmulf(fx, 0x7FFF))

    def reverb(self, al, ar):
        """libultra's BIGROOM (n_alFxPull) on one chunk: the aux bus in, the effect out (to both sides)"""
        n = CHUNK; L = self.line; w = self.w
        def at(o): return (w - o + np.arange(n)) % FX_LEN
        inp = clamp16a(al + vmulf(al, -9597))           # 0xDA83: .707 L = L - .293 L
        inp = clamp16a(inp + vmulf(ar, 0x5A82))         # + .707 R
        L[at(0)] = inp
        out = np.zeros(n, np.int64)
        for i_, o_, ff, fb, gain in BIGROOM:
            b1 = L[at(i_)]; b2 = L[at(o_)]
            if ff: b2 = clamp16a(b2 + vmulf(b1, ff))
            if fb:
                b1 = clamp16a(b1 + vmulf(b2, fb)); L[at(i_)] = b1
            L[at(o_)] = b2
            if gain: out = clamp16a(out + vmulf(b2, gain))
        self.w = (w + n) % FX_LEN
        return out

def tick_chunk(k):
    """the chunk where tick k's calls land: the tick runs at sample 480 k, its changes take effect at the next chunk
    boundary after it (matched against the recording of Revenge playing the song: this rule, and no other, makes the
    two the same waveform -- 0.98 correlation)"""
    return (k * TICK) // CHUNK + 1

def render(song, bank, rom, nchunks):
    """the song from its start, 'nchunks' chunks -> stereo int16 [nchunks * 184, 2]"""
    P = Player(song, bank)
    S = Synth(rom, len(P.tracks))
    out = np.zeros((nchunks * CHUNK, 2), np.int16)
    k = 0; due = {}
    for c in range(nchunks):
        while tick_chunk(k) <= c:
            due.setdefault(tick_chunk(k), []).extend(P.tick()); k += 1
        seen = set()
        for ev in due.pop(c, []):
            if ev[1] == "start": ev = (ev[0], "start", bank.waves[ev[2]].pcm())
            synth_update(S.voices[ev[0]], ev, S.eq, ev[0] not in seen); seen.add(ev[0])
        l, r = S.chunk()
        out[c * CHUNK:(c + 1) * CHUNK, 0] = l; out[c * CHUNK:(c + 1) * CHUNK, 1] = r
    return out


# ---------------------------------------------------------------------------------------------------- the song's loop
def song_loop(song, bank, max_ticks=20000):
    """(the time all its tracks are in their endless loops, the loop's length, the tempo) in the sequencer's time units
    (256 = a duration unit; a tick advances 'tempo'), found by playing the sequence: every track's 'for ever' loop (0x95 255
    .. 0x96), its body timed by two passes; the song's period = the bodies' least common multiple"""
    P = Player(song, bank)
    starts, ends = {}, {}
    o95, o96 = P.op_95, P.op_96
    def op95(t, op):
        if t.song.d[t.pc] == 255: starts.setdefault(t.num, t.next)
        o95(t, op)
    def op96(t, op):
        if t.loops and t.loops[-1][4] == 255: ends.setdefault(t.num, []).append(t.next)
        o96(t, op)
    P.op_95, P.op_96 = op95, op96
    P.tick()
    tempo = P.tracks[0].tempo
    for k in range(max_ticks):
        if len(ends) == len(P.tracks) and all(len(e) >= 3 for e in ends.values()): break
        P.tick()
        assert all(t.tempo == tempo for t in P.tracks), "a tempo change after the start"
    assert len(ends) == len(P.tracks), "a track that does not loop for ever"
    start, period = 0, 1
    for n, e in ends.items():
        body = e[1] - e[0]
        assert e[2] - e[1] == body and body > 0 and e[0] - starts[n] == body
        start = max(start, starts[n])
        a, b = period, body
        while b: a, b = b, a % b
        period = period * body // a
    return start, period, tempo

# ---------------------------------------------------------------------------------------------------- deterministic sin
PI = 3.141592653589793
PI_A = 3.1415926218032837        # pi in two parts (Cody-Waite): PI_A has only 27 bits, so k * PI_A is exact
PI_B = 3.178650954705639e-08
def dsin(x):
    """sin(x), only IEEE double + - * (and floor): the same bits in Python and C++ (no library sin)"""
    import math
    k = math.floor(x / PI + 0.5)
    r = (x - k * PI_A) - k * PI_B
    r2 = r * r
    p = 1.0 / 121645100408832000.0                      # Taylor to r^19 (19!), Horner: |r| <= pi/2, error < 1e-16
    for d in (355687428096000.0, 1307674368000.0, 6227020800.0, 39916800.0, 362880.0, 5040.0, 120.0, 6.0):
        p = 1.0 / d - r2 * p                            # (17! .. 3!)
    s = r * (1.0 - r2 * p)
    return -s if k & 1 else s

def fir_table(q, W=96, P=1024):
    """the anti-aliasing interpolator for a rate change by q (< 1): P phases x 2W taps, each phase's taps summing to
    65536 exactly -- a windowed sinc (Blackman), its cut-off 0.46 of the new rate"""
    fc = 0.46 * q
    rows = []
    for ph in range(P):
        h = []
        for k in range(-W + 1, W + 1):
            t = k - ph / P
            if t == 0.0: s = 2.0 * fc
            else: s = dsin(2.0 * PI * fc * t) / (PI * t)
            u = t / W                                     # Blackman over -W..W
            w = 0.42 + 0.5 * dsin(PI * u + PI / 2) + 0.08 * dsin(2.0 * PI * u + PI / 2)
            h.append(s * w if -1.0 < u < 1.0 else 0.0)
        tot = 0.0
        for v in h: tot += v
        c = [rnd(v * 65536.0 / tot) for v in h]
        big = max(range(len(h)), key=lambda i: (abs(h[i]), -i))
        c[big] += 65536 - sum(c)
        rows.append(c)
    return np.array(rows, np.int64)

def rnd(x):
    """round half away from zero"""
    import math
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))

# ---------------------------------------------------------------------------------------------------- the theme sample
OUT_RATE = 479.93 * 60.0176   # the game's sound per second of the game (gen/revmusic.py: what the recording was taken at)
THEME_RATE = 10787.0          # a theme's samples per second of the game in No Mercy (gen/revmusic.py)
PEAK = 30000

def theme(rom, f):
    """Revenge's song f (its file number) as a No Mercy theme sample, as gen/revmusic.py's theme() gives it ->
    (PCM int16 at 'rate', its loop start, rate, about): mono, the song from its first sound; its loop exactly one period
    of the sequence (all tracks in their endless loops), starting a little after the loop's first entry so that the
    loop's last FADE samples can ease into the same place one period earlier (whose continuation is the loop's start: a
    seam the song's own); resampled so the loop is a whole number of 16-sample frames (a change in pitch under 0.1%);
    its peak at most 30000"""
    song, bank = Song(game_files(rom)(f)), Bank(rom)
    S, U, tempo = song_loop(song, bank)
    Ls = U * TICK / tempo                                   # the loop in output samples (fractional)
    Lr = int(np.floor(Ls * THEME_RATE / OUT_RATE / 16 + 0.5)) * 16
    num, den = Lr * tempo, U * TICK                         # output sample j <-> render sample o + j * den / num
    o = tick_chunk(0) * CHUNK                               # the song's first sound
    a = tick_chunk(S // tempo) * CHUNK                      # where its loop's first notes sound
    ls = ((a - o) * num + den - 1) // den + FADE
    ls = (ls + 15) // 16 * 16
    n_out = ls + Lr
    W = 96
    x = render(song, bank, rom, (o + ((n_out - 1) * den) // num + W + 1) // CHUNK + 1)
    m = x[:, 0].astype(np.int64) + x[:, 1].astype(np.int64)
    H = fir_table(num / den, W)
    P = H.shape[0]
    y = np.zeros(n_out, np.int64)
    taps = np.arange(-W + 1, W + 1, dtype=np.int64)
    for j0 in range(0, n_out, 4096):
        j = np.arange(j0, min(n_out, j0 + 4096), dtype=np.int64)
        pn = j * den
        ph = ((pn % num) * P * 2 + num) // (2 * num)        # the nearest phase
        i = o + pn // num + ph // P; ph = ph % P
        acc = (m[i[:, None] + taps[None, :]] * H[ph]).sum(1)
        y[j] = (acc + (1 << 16)) >> 17                       # / 65536 (the taps) / 2 (mono)
    k = np.arange(FADE, dtype=np.int64)                     # the seam: the end eases into one period before it
    e = np.arange(n_out - FADE, n_out)
    y[e] = rdiv(y[e] * (FADE - 1 - k) + y[e - Lr] * (k + 1), FADE)
    peak = int(np.abs(y).max())
    if peak > PEAK: y = rdiv(y * PEAK, peak)
    y = np.clip(y, -32768, 32767).astype(np.int16)
    rate = float(Lr * tempo) * OUT_RATE / float(U * TICK)
    return y, ls, rate, ("sequence", S, U, tempo, Lr)

FADE = 1024                   # the loop seam's cross-fade (samples of the theme: ~95 ms)

def rdiv(a, b):
    """a / b rounded half away from zero (b > 0), integers"""
    return np.sign(a) * ((np.abs(a) * 2 + b) // (2 * b))
