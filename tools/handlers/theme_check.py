# theme_check.py (themefix2, 2026-11-13): is a folder theme the game's theme, played at the right speed?
#   python theme_check.py <folder with theme.bin + theme.meta> [<rig recording .raw of his entrance in the source game>]
# 1. SPEED / "is it music like the game's": theme.bin decoded as the exe decodes it (the SDK's decoder), at the exe's 10787
#    a second: how high its sound reaches (the top of its spectrum, 45 dB below its loudest) against No Mercy's own themes (the
#    content pack's No Mercy Plus instruments, the same measure). A theme converted at the wrong rate moves the whole sound
#    up or down: the old Badd Blood conversion (2.67 times too slow) sat far below every No Mercy theme -- the "roar".
# 2. THE SOURCE: the recording (stereo s16 at the core's 28,803 a second) resampled to 10787 a second, and searched for in
#    the decoded theme by GCC-PHAT (each channel, 4 loud stretches of 1.5 s): the peak over the median of the correlation,
#    at the exact rate and, to show the peak is the rate's and not chance, at rates 0.5 % and 2 % off.
import os, sys, re, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
DEV = next(d for d in (os.path.normpath(os.path.join(HERE, *[".."] * i)) for i in range(8)) if os.path.exists(os.path.join(d, "nmpaths.py")))
sys.path.insert(0, DEV); sys.path.insert(0, os.path.join(DEV, "mod", "gen"))
import nmpaths, vadpcm              # (vadpcm.decode: the N64 SDK's own decoder, sample for sample what the exe plays)
def _dec(data, meta_or_wave):
    order, npred = struct.unpack_from(">ii", meta_or_wave, 0x18)
    return vadpcm.decode(data, meta_or_wave[0x18:0x20 + order * npred * 16]).astype(np.float64)
RATE, RIG = 10787.0, 28803.1
# (Badd Blood playing each one's entrance in the libretro rig, its sound kept: themefix2's rig_rec.py, from imports2's
#  rig\cap vsel / bbgdsel states -- A A A, the VS screen's 4 A then START, A, then the entrance left to run)
# (2026-11-14, folders082: the rig FOLDER under its own name -- imported as RIG it replaced the rig's sample rate above,
#  and against_recording failed with "can't multiply sequence by non-int")
from handlers import RIG as RIG_DIR
RECORDINGS = {k: os.path.join(RIG_DIR, "rec", "bb_%s_entrance.raw" % k) for k in ("vader", "goldust")}
REACH_MIN = 2500.0          # Hz: a theme whose sound stops below this is played too slow (see the notes in main)
def decode_folder(d):
    meta = open(os.path.join(d, "theme.meta"), "rb").read(); data = open(os.path.join(d, "theme.bin"), "rb").read()
    ls, le = struct.unpack_from(">II", meta, 0xA0)
    return _dec(data, meta)[:le or None], ls
def half_freq(x, fs):
    """how high its sound reaches: the highest frequency whose level (Welch's average, 1/3-octave smoothed) is within 45 dB
    of the loudest -- a sound made from samples at R a second can't reach past R / 2 when it's played at the right speed,
    and a slowed one ends that much lower"""
    from scipy import signal
    f, p = signal.welch(x, fs, nperseg=2048); db = 10 * np.log10(p + 1e-9)
    sm = np.array([db[(f >= v / 1.12) & (f <= v * 1.12)].mean() if v > 0 else db[0] for v in f])
    return float(f[np.nonzero(sm >= sm.max() - 45)[0][-1]])
_nm = None
def nm_range():
    """the measure over No Mercy Plus's own themes (content pack instruments, tuning -48: made at 10787 a second)"""
    global _nm
    if _nm is None:
        h = open(os.path.join(DEV, "mod", "src", "content_index.h")).read()
        rows = re.findall(r"\{ 0x([0-9A-F]+), 0x([0-9A-F]+), \{ ([0-9A-Fx,]+) \}, (\d+), 0x([0-9A-F]+), (\d) \},  // its instrument", h)
        cb = open(os.path.join(DEV, "mod", "build", "content.bin"), "rb").read(); v = []
        for off, ln, w, lv, tu, lp in rows:
            off, ln = int(off, 16), int(ln, 16); w = bytes(int(x, 16) for x in w.split(","))
            v.append(half_freq(_dec(cb[off:off + min(ln, 9 * 16384)], w), RATE))
        _nm = sorted(v)
    return _nm
def gcc(src, seg, N):
    X = np.fft.rfft(src, N) * np.conj(np.fft.rfft(seg, N)); X /= np.abs(X) + 1e-9
    c = np.fft.irfft(X, N)[:len(src)]; i = int(np.argmax(c)); return float(c[i] / (np.median(np.abs(c)) + 1e-12)), i
def against_recording(pcm, ls, raw):
    st = np.frombuffer(open(raw, "rb").read(), dtype="<i2").reshape(-1, 2).astype(np.float64)
    src = np.concatenate([pcm, pcm[ls:], pcm[ls:]]); N = 1 << int(np.ceil(np.log2(len(src) + 40000)))
    L = int(1.5 * RIG); mono = st.mean(1)
    starts = [c for c in range(int(2 * RIG), len(mono) - L, int(3 * RIG)) if np.sqrt(np.mean(mono[c:c + L] ** 2)) > 300][:4]
    out = {}
    for k in (1.0, 1.005, 0.995, 1.02, 0.98):
        pk = []
        for ch in (0, 1):
            for c in starts:
                t = c + np.arange(int(1.5 * RATE)) * (RIG / (RATE * k))
                seg = np.interp(t, np.arange(len(st)), st[:, ch]); seg -= seg.mean(); pk.append(round(gcc(src, seg, N)[0]))
        out[k] = pk
    return out
def check(d, raw=None):
    """-> (lines, speed_ok, source_ok or None): the two checks above for folder d (and the recording raw, if given)"""
    pcm, ls = decode_folder(d); hf = half_freq(pcm, RATE); nm = nm_range(); lines = []
    # (No Mercy's themes reach 4.7-5.4 kHz; the Badd Blood sounds, made at ~7,000 a second, reach 3.5 kHz when converted at
    #  the right rate and ~1.5 kHz at the old, 2.67 times too slow one: 2.5 kHz tells the two apart with room either side)
    inside = hf >= REACH_MIN
    lines.append("%s: %.1f s; its sound reaches %.0f Hz -- No Mercy's themes: %.0f..%.0f Hz (median %.0f): %s" % (
        d, len(pcm) / RATE, hf, nm[0], nm[-1], nm[len(nm) // 2], "FULL SPEED (reaches %.0f Hz or more)" % REACH_MIN if inside else "TOO SLOW: it stops below %.0f Hz (a roar)" % REACH_MIN))
    good = None
    if raw:
        r = against_recording(pcm, ls, raw)
        for k, pk in r.items(): lines.append("  vs the recording, rate x %.3f: GCC-PHAT peaks (L, R) %s" % (k, pk))
        # (a match: its four best stretches well clear of chance -- ~10-15 -- and every peak off the rate under half the best)
        good = min(sorted(r[1.0])[-4:]) > 20 and max(max(v) for k, v in r.items() if k != 1.0) < 0.5 * max(r[1.0])
        lines.append("  -> " + ("MATCHES what the source game plays (%s)" % os.path.basename(raw) if good else "does NOT match the recording"))
    return lines, inside, good

if __name__ == "__main__":
    lines, _, _ = check(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
    for l in lines: print(l)
