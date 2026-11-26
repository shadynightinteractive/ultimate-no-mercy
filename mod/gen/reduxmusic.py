"""reduxmusic.py (reduxthemes, 2026-11-18): WCW/nWo Revenge Redux's entrance themes, built from the player's own Redux patch
(the user's decision C) -- no longer the MP3s the project once rendered from it, which can't ship.

Revenge Redux is a third-party patch over WCW/nWo Revenge (USA) (version 05/20/2026: tools\\romimport\\roms.py knows it by
fingerprint -- the patched ROM itself, or Revenge plus the patch's xdelta, alone or in the release's .zip). It gives every
character an entrance theme:
  * 42 songs, 0x18..0x41 (its Change Log names them: 0x18 "nWo", 0x19 "Wolfpac", 0x1B "Steiners", ... 0x41 "Wrath"), each a
    sequence file of its own (file 0x3F00 + song - 0x18, 352 bytes: one track, one endless loop, one long note) on an
    instrument of its own in its music bank (waves 0x36..0x5F: one 60-second clip each, VADPCM, made at 11025 Hz);
  * the THEME LIST (ROM 0xDAF60, which the game copies to RAM 0x80400300): 8-byte rows -- a model code, a song for each of
    the four attires, 0 -- ending with a row of 0xFFFF. Songs 0x10 / 0x11 ("Original 1 / 2") are Revenge's own: no Redux
    song for him;
  * its music bank, the bank's samples and the file table moved past vanilla's 16 MB (its boot code says where:
    gen/revplay.py reads them there).

THE SOUND (the user, 2026-11-18: "like the old MP3s"): each theme is Redux's clip itself, at its own speed and pitch -- not
as the N64 plays it (Redux's note and tuning play the 11025 Hz clips at 10,790 a second: 2.1 % slower, 37 cents lower) --
and as loud as the old MP3-made themes (gen/mp3theme.py's level: No Mercy Plus's median 0.4 s RMS of 8800, peaks held under
31000 by a look-ahead limiter). Made by clip_theme(), integer arithmetic only (the C++ twin, revplay.cpp clip_theme, gives
the same bytes: tools\\romimport\\native\\revplay_check.py --redux):
  1. the clip decoded (VADPCM, revplay.Bank);
  2. resampled to the theme's rate so the whole clip is a whole number of 16-sample frames (revplay's windowed-sinc table;
     the rate moves under 0.1 % for it), its last FADE samples faded out (it loops whole, from its start);
  3. brought to the level: a gain from the median window's mean square (isqrt), then the limiter (5 ms look-ahead, instant
     attack, 120 ms release as a fixed-point constant), repeated (up to 3 more times) while the median is off by 1 % or more.

Who gets which: every Revenge Superstar whose model code has a Redux song in the THEME LIST (the song of his first attire;
Sting and Scott Steiner keep the attires the earlier renders chose). PORTS names the built-in ports (mkcontent matches the
roster by these names, the importer's folders by their IDs); the never-ported men are found by their Revenge model code
(wrestler.json source.code). A folder has one theme: Redux's per-attire songs (Kanyon's Mortis 0x1F, Sting's Wolfpac /
Surfer Sting, Hogan's American Made) need the game to pick a theme by costume -- not there yet.

    rom = reduxmusic.redux_rom()                     # the patched ROM (bytes), or None: no Redux patch in roms\\
    reduxmusic.song_for(rom, code)                   # his Redux song, or None
    reduxmusic.theme(rom, song)                      # -> (PCM int16, loop start, rate, about)

Nothing here reads anything but the ROMs; no file is written."""
import math, os, struct, sys

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(_HERE)), "tools", "romimport"))
import revplay
import numpy as np

SEQ0, SONG0, SONG_LAST = 0x3F00, 0x18, 0x41     # song s is file SEQ0 + s - SONG0
THEME_LIST = 0xDAF60                            # ROM: (model code, 4 songs, 0) rows up to 0xFFFF
SEQ_INSTRUMENT = 342                            # (the one byte that differs between the songs' files: the instrument)

# the built-in ports: (his name -- as mkcontent matches the roster and the music files are named --, his folders' IDs -- a
# second look nested in his folder takes its base's --, his model code in Redux, the attire whose song is his)
PORTS = [
    ("Booker T",        ("booker_t", "booker_t_b"),         0x000B, 0),   # 0x27 "Harlem Heat"
    ("Bret Hart",       ("bret_hart", "bret_hart_b"),       0x0008, 0),   # 0x23 "Bret" (the player's own song comes first)
    ("Eric Bischoff",   ("eric_bischoff",),                 0x020D, 0),   # 0x40 "Bischoff"
    ("Goldberg",        ("goldberg",),                      0x000A, 0),   # 0x22 "Goldberg"
    ("Hollywood Hogan", ("hollywood_hogan",),               0x0201, 0),   # 0x3B "Hollywood" (his attire 2: 0x3C "American Made")
    ("Jim Neidhart",    ("jim_neidhart",),                  0x010E, 0),   # 0x2A "Jim Neidhart"
    ("Kevin Nash",      ("kevin_nash",),                    0x0203, 0),   # 0x19 "Wolfpac"
    ("Lex Luger",       ("lex_luger",),                     0x0003, 0),   # 0x1D "Lex"
    ("Mr Perfect",      ("mr_perfect",),                    0x0207, 0),   # 0x18 "nWo" (the player's own song comes first)
    ("Randy Savage",    ("randy_savage", "randy_savage_b"), 0x0202, 0),   # 0x1E "Macho Man"
    ("Rey Mysterio",    ("rey_mysterio", "rey_mysterio_b"), 0x0304, 0),   # 0x34 "Rey"
    ("Rick Steiner",    ("rick_steiner",),                  0x0005, 0),   # 0x1B "Steiners"
    ("Roddy Piper",     ("roddy_piper",),                   0x0007, 0),   # 0x39 "Piper"
    ("Scott Hall",      ("scott_hall",),                    0x0204, 0),   # 0x18 "nWo"
    ("Scott Steiner",   ("scott_steiner", "scott_steiner_b"), 0x0206, 2), # 0x1B "Steiners" (attires 0-1: 0x18 "nWo")
    ("Stevie Ray",      ("stevie_ray", "stevie_ray_b"),     0x0108, 0),   # 0x27 "Harlem Heat"
    ("Sting",           ("sting",),                         0x0001, 1),   # 0x1C "Sting" (attire 0: 0x19, 2: 0x3D "Surfer Sting")
    ("The Giant",       ("the_giant",),                     0x0002, 0),   # 0x18 "nWo" (Redux has none of his own)
]
ATTIRE = {code: attire for _, _, code, attire in PORTS}
def norm(s): return "".join(c for c in s.lower() if c.isalnum())     # (gen/mp3theme.py norm: how names are matched)

_rom = {}
def redux_rom(folder=None):
    """the player's Revenge Redux ROM (bytes, big-endian), or None when roms\\ holds neither it nor Revenge + its patch
    (looked for once a run)"""
    if folder not in _rom:
        from roms import Roms
        _rom[folder] = Roms(folder).rom("revenge_redux")
    return _rom[folder]

def theme_list(rom):
    """Redux's THEME LIST -> {model code: [the song of each of his four attires]}"""
    out, at = {}, THEME_LIST
    while True:
        code, a, b, c, d, z = struct.unpack_from(">HBBBBH", rom, at)
        if code == 0xFFFF: return out
        assert z == 0 and all(SONG0 <= s <= SONG_LAST or s in (0x10, 0x11) for s in (a, b, c, d)), "Redux's THEME LIST"
        out.setdefault(code, [a, b, c, d]); at += 8

def song_for(rom, code):
    """the Redux song for the Superstar with this Revenge model code (his first attire's; a port's chosen attire), or None"""
    row = theme_list(rom).get(code)
    if not row: return None
    s = row[ATTIRE.get(code, 0)]
    return s if SONG0 <= s <= SONG_LAST else None

def names(): return [p[0] for p in PORTS]
def of_name(name):
    """the port's name as PORTS has it (matched as gen/mp3theme.py matches music files), or None"""
    return next((p[0] for p in PORTS if norm(p[0]) == norm(name)), None)
def song_of(rom, name):
    """a port's Redux song (by his name), or None"""
    c = next((p[2] for p in PORTS if norm(p[0]) == norm(name)), None)
    return None if c is None else song_for(rom, c)
def code_of_folder(fid, source_code=None):
    """the Revenge model code a folder's theme comes from: a port by his folder ID, else the folder's source.code"""
    c = next((p[2] for p in PORTS if fid in p[1]), None)
    if c is None and isinstance(source_code, str) and source_code.lower().startswith("0x"): c = int(source_code, 16)
    return c

# ---------------------------------------------------------------------------------------------------- the theme sample
CLIP_RATE = 11025              # the clips' own rate (their music at its own speed and pitch)
THEME_RATE = 10787             # a theme's samples per second of the game in No Mercy (tune 0xC2400000)
FADE = 1024                    # the clip's end faded out over this many samples (it loops whole, into its silent start)
W_FIR = 96                     # revplay.fir_table's half width
TARGET = 8800                  # the old themes' level: median 0.4 s RMS (gen/mp3theme.py TARGET_MED)
CEIL = 31000                   # the limiter's ceiling (mp3theme.py CEIL)
REL_Q16 = 65485                # the limiter's release per sample, exp(-1 / (0.120 * 10787)) in 1/65536 (a constant: no exp)

def rdiv_a(a, b):
    """an int64 array / b rounded half away from zero (b > 0)"""
    q = (np.abs(a) * 2 + b) // (2 * b)
    return np.where(a < 0, -q, q)

def median_ms(y, W):
    """the median of the theme's whole W-sample windows' sums of squares, over the windows above RMS 200 (int; an even count:
    the two middle ones' mean, rounded down)"""
    k = len(y) // W
    s = sorted(int(v) for v in (y[:k * W].astype(np.int64).reshape(k, W) ** 2).sum(1) if v > 200 * 200 * W)
    if not s: return 0
    m = len(s) // 2
    return s[m] if len(s) % 2 else (s[m - 1] + s[m]) // 2

def limit(y, g, la):
    """y times g / 65536 through the look-ahead limiter -> int64 array"""
    v = (y.astype(np.int64) * g + 32768) >> 16                               # (a floor shift; C++: fdiv)
    a = np.abs(v)
    need = np.where(a <= CEIL, 65536, (CEIL * 65536) // np.maximum(a, 1))  # the gain each sample may have (1/65536)
    n = len(v)
    pad = np.concatenate([np.full(la, 65536, np.int64), need, np.full(la, 65536, np.int64)])
    gmin = pad[0:n].copy()
    for d in range(1, 2 * la + 1): np.minimum(gmin, pad[d:d + n], out=gmin)     # the least over i-la .. i+la
    gain = np.empty(n, np.int64); cur = 65536
    for i, gi in enumerate(gmin.tolist()):                                   # instant attack, smooth release
        cur = gi if gi < cur else gi + ((cur - gi) * REL_Q16) // 65536
        gain[i] = cur
    return np.clip(rdiv_a(v * gain, 65536), -32768, 32767)

_fir = {}
def clip_theme(rom, song):
    """Redux's song as a No Mercy theme sample, its clip at its own speed and the old themes' level -> (PCM int16 at
    'rate', loop start 0, rate, about)"""
    seq = revplay.game_files(rom)(SEQ0 + song - SONG0)
    assert seq is not None and len(seq) > SEQ_INSTRUMENT and seq[SEQ_INSTRUMENT] == song + 0x1E, "Redux's song 0x%02X" % song
    inst = seq[SEQ_INSTRUMENT]
    x = revplay.Bank(rom).waves[inst].pcm()[0].astype(np.int64)            # the clip, 11025 Hz
    n = len(x)
    Lr = (n * THEME_RATE + 8 * CLIP_RATE) // (16 * CLIP_RATE) * 16           # its length in theme samples: 16-sample frames
    num, den = Lr, n                                                         # theme sample j <-> clip sample j * den / num
    if (num, den) not in _fir: _fir[(num, den)] = revplay.fir_table(num / den, W_FIR)
    H = _fir[(num, den)]; P = H.shape[0]
    m = np.concatenate([np.zeros(W_FIR, np.int64), x, np.zeros(W_FIR + 2, np.int64)])
    taps = np.arange(-W_FIR + 1, W_FIR + 1, dtype=np.int64)
    y = np.zeros(Lr, np.int64)
    for j0 in range(0, Lr, 4096):
        j = np.arange(j0, min(Lr, j0 + 4096), dtype=np.int64)
        pn = j * den
        ph = ((pn % num) * P * 2 + num) // (2 * num)                         # the nearest phase
        i = pn // num + ph // P; ph = ph % P
        acc = (m[W_FIR + i[:, None] + taps[None, :]] * H[ph]).sum(1)
        y[j] = (acc + (1 << 15)) >> 16                                       # / 65536 (the taps)
    k = np.arange(FADE, dtype=np.int64)
    y[Lr - FADE:] = rdiv_a(y[Lr - FADE:] * (FADE - 1 - k), FADE)
    rate = Lr * float(CLIP_RATE) / n
    W = int(0.4 * rate); T = TARGET * TARGET * W; la = int(0.005 * rate)
    out = limit(y, math.isqrt(65536 * 65536 * T // median_ms(y, W)), la)
    for _ in range(3):                                                       # (the limiter takes a little off)
        med = median_ms(out, W)
        if 50 * abs(med - T) < T: break
        out = limit(out, math.isqrt(65536 * 65536 * T // med), la)
    about = ("Revenge Redux's song 0x%02X (its file 0x%04X, instrument 0x%02X): its own clip at its own speed, as loud as the "
             "earlier themes (%.1f s, looping whole)" % (song, SEQ0 + song - SONG0, inst, Lr / rate))
    return out.astype(np.int16), 0, rate, about

_made = {}
def theme(rom, song):
    """clip_theme, made once a run per song"""
    if song not in _made: _made[song] = clip_theme(rom, song)
    return _made[song]

if __name__ == "__main__":
    # python reduxmusic.py [<song, hex> <out.wav>]: the ports' songs; one theme written as a WAV (at its rate)
    r = redux_rom()
    if r is None: raise SystemExit("no WCW/nWo Revenge Redux in roms\\ (the patched ROM, or Revenge + the Redux xdelta)")
    for nm in names(): print("%-16s song %s" % (nm, "0x%02X" % song_of(r, nm) if song_of(r, nm) else "none"))
    if len(sys.argv) > 2:
        import wave
        y, ls, rate, about = theme(r, int(sys.argv[1], 16))
        w = wave.open(sys.argv[2], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(round(rate)))
        w.writeframes(y.astype("<i2").tobytes()); w.close()
        print(about)
