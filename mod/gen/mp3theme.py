"""A theme from a music file (the game folder's assets/music: <Superstar's name>.mp3 -- copied to
gen_data/music) as one of the arcade's theme instruments, made like No Mercy Plus's themes: one long
sample, mono, at the rate the game plays a theme's samples (THEME_RATE), looping on a stretch of the
song that comes round again (its seam cross-faded), as loud as the game's themes.

Loudness: No Mercy Plus's 52 themes, decoded (gen/mkcontent.py's pack), measured in 0.4 s windows: the
median window's RMS about 8800 (most 7000..11000), the loud ones' (90th percentile) about 10800, their
peaks at full scale -- they're mastered hot. A song file is brought to that median, its peaks held under
full scale by a look-ahead limiter (no clipping)."""
import os, subprocess, numpy as np
from scipy import signal, ndimage

THEME_RATE = 10787.0          # (gen/revmusic.py: a theme's samples per second of the game)
SRC_RATE = 44100
TARGET_MED = 8800.0           # the game's themes (No Mercy Plus's): median 0.4 s RMS
CEIL = 31000.0                # the limiter's ceiling
MAX_LEN_S = 150.0             # the sample's end at most this far in (an entrance is a minute or so)

def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SRC_RATE), "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype="<i2").astype(np.float64)

def find_loop(x, R):
    """(t0, L): a stretch from t0, L long, that the song plays again right after (the best match of a 4 s
    window at some anchor, 20 s or more later), ending by MAX_LEN_S; refined to the sample at the seam"""
    best = None
    env = np.sqrt(signal.fftconvolve(x * x, np.ones(4 * R), mode="valid"))
    for at in np.arange(12.0, 70.0, 3.0):
        a = int(at * R); seg = x[a:a + 4 * R]
        if len(seg) < 4 * R or np.linalg.norm(seg) < 1e3 * np.sqrt(len(seg)) * 0.05: continue
        c = signal.fftconvolve(x, seg[::-1], mode="valid")
        nc = c / (env * np.linalg.norm(seg) + 1e-9)
        lo, hi = a + 20 * R, min(len(nc), int(MAX_LEN_S * R) - 6 * R)
        if hi <= lo: continue
        k = lo + int(np.argmax(nc[lo:hi]))
        if best is None or nc[k] > best[2]: best = (a, k - a, float(nc[k]))
    if best is None or best[2] < 0.3:
        return None
    a, L, like = best
    # the seam in the middle of the stretch that matched (so what comes before it and after it both match
    # their places one loop on), to the sample (+-20 ms): 1 s each side
    t0 = a + 2 * R; N = R; ref = x[t0 - N:t0 + N]
    def sim(d):
        b = x[t0 + L + d - N:t0 + L + d + N]
        return float(np.dot(b, ref) / (np.linalg.norm(b) * np.linalg.norm(ref) + 1e-9))
    s, d = max((sim(d), d) for d in range(-int(0.02 * R), int(0.02 * R) + 1))
    return t0, L + d, like, s

def limit(y, ceil=CEIL, R=THEME_RATE):
    """look-ahead peak limiter: the gain each sample needs, held 5 ms ahead, released over 120 ms"""
    need = np.minimum(1.0, ceil / np.maximum(np.abs(y), 1e-9))
    la = max(1, int(0.005 * R))
    g = ndimage.minimum_filter1d(need, size=2 * la + 1, origin=0)
    rel = np.exp(-1.0 / (0.120 * R))
    out = np.empty_like(g); cur = 1.0
    for i in range(len(g)):                     # (instant attack, smooth release)
        cur = g[i] if g[i] < cur else g[i] + (cur - g[i]) * rel
        out[i] = cur
    return y * out

def loudness(y, R=THEME_RATE):
    W = int(0.4 * R); k = len(y) // W
    r = np.sqrt((y[:k * W].reshape(k, W) ** 2).mean(1))
    r = r[r > 200]
    return float(np.median(r)), float(np.percentile(r, 90))

def theme(path):
    """PCM (int16, ~THEME_RATE), its loop start (samples), the rate, info -- the song from its first sound
    to the loop's end; the loop a whole number of 16-sample frames (the rate moved under 0.1% for it)"""
    x = load(path); R = SRC_RATE
    start = int(np.argmax(np.abs(x) > 64))
    lp = find_loop(x, R)
    if lp:
        t0, L, like, seam = lp
    else:                                       # (no stretch comes round again: the whole song loops)
        t0, L, like, seam = start, min(len(x), int(MAX_LEN_S * R)) - start, 0.0, 0.0
    x = x.copy()
    N = int(0.25 * R); a_ = np.linspace(0, 1, N)
    # (what comes just before the loop's start; a song looping whole from its first sample has silence there -- the
    # Redux themes, 30 s clips, 2026-10-29)
    pre = x[t0 - N:t0] if t0 >= N else np.concatenate([np.zeros(N - t0, dtype=x.dtype), x[:t0]])
    x[t0 + L - N:t0 + L] = x[t0 + L - N:t0 + L] * (1 - a_) + pre * a_
    Lr = int(round(L * THEME_RATE / R / 16)) * 16
    rate = Lr * R / L
    seg = x[start:t0 + L]
    # anti-aliased down to the theme's rate (polyphase, Kaiser window)
    from fractions import Fraction
    fr = Fraction(rate / R).limit_denominator(4000)
    y = signal.resample_poly(seg, fr.numerator, fr.denominator, window=("kaiser", 8.0))
    ls = int(round((t0 - start) * rate / R)) // 16 * 16
    y = y[:ls + Lr]
    if len(y) < ls + Lr: y = np.concatenate([y, np.zeros(ls + Lr - len(y))])
    med, _ = loudness(y, rate)
    y = limit(y * (TARGET_MED / med), R=rate)
    for _ in range(3):                          # (the limiter takes a little off: brought back to the median)
        med, _ = loudness(y, rate)
        if abs(med / TARGET_MED - 1) < 0.01: break
        y = limit(y * (TARGET_MED / med), R=rate)
    pcm = np.clip(np.round(y), -32768, 32767).astype(np.int16)
    m2, p90 = loudness(pcm.astype(np.float64), rate)
    info = dict(start=start / R, loop_from=t0 / R, loop_len=L / R, likeness=round(like, 3), seam=round(seam, 3),
                rms_med=round(m2), rms90=round(p90), peak=int(np.abs(pcm).max()), seconds=round(len(pcm) / rate, 1))
    return pcm, ls, rate, info

# The music files and the Superstar each is for: the file's name is his (the roster's names, and short
# names, as the select shows them; case, spaces and punctuation aside)
def norm(s): return "".join(c for c in s.lower() if c.isalnum())
def files(folder):
    return sorted(f for f in os.listdir(folder) if f.lower().endswith((".mp3", ".wav", ".ogg", ".flac", ".m4a")))

if __name__ == "__main__":
    import sys
    pcm, ls, rate, info = theme(sys.argv[1])
    print(len(pcm), ls, rate, info)
    if len(sys.argv) > 2:
        import wave
        w = wave.open(sys.argv[2], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(round(rate)))
        w.writeframes(pcm.astype("<i2").tobytes()); w.close()
