"""How a splice piece was cut from its line: place, gain and the fades at its two ends (2026-11-14, commentary agent).

The pieces in data/extracted/keep/Keep are stretches of the line's PCM (WWE '13: the clip as the game has it, 20 kHz,
after the silence trim; Legends: its mp3 through the lost Legends filter) times a gain, tapered at the ends (the
mknames cuts fade in and out over a few ms; measured against the local caches the ends drop to 0 within ~30 ms).
Model: piece[t] = gain * src[start + t] * w(t), w a linear ramp up over fade_in samples and down over fade_out.
With the exact source (the disc's own clip) the fit should come out near-exact; against the caches' lossy copies it
can only be checked to the codec's noise (~10-15 dB SNR for Vorbis q0 waveforms).
"""
import numpy as np

def window(n, fin, fout):
    w = np.ones(n)
    if fin: w[:fin] = np.arange(1, fin + 1) / (fin + 1)
    if fout: w[n - fout:] = np.arange(fout, 0, -1) / (fout + 1)
    return w

def fit(piece, src, start, step=10, most=1200):
    """-> (gain, fade_in, fade_out, snr dB) for piece against src[start:start + len(piece)]"""
    n = len(piece)
    s = src[start:start + n]
    if len(s) < n: s = np.concatenate([s, np.zeros(n - len(s))])
    best = None
    for fin in range(0, min(most, n // 3) + 1, step):
        for fout in range(0, min(most, n // 3) + 1, step):
            m = s * window(n, fin, fout)
            g = float(np.dot(m, piece) / (np.dot(m, m) + 1e-12))
            e = float(np.sum((piece - g * m) ** 2))
            if best is None or e < best[0]: best = (e, g, fin, fout)
    e, g, fin, fout = best
    snr = 10 * np.log10((np.sum(piece ** 2) + 1e-12) / (e + 1e-12))
    return g, fin, fout, snr

def apply(src, start, length, gain, fin, fout):
    """the cutfit model (gain, (t+1)/(F+1) ramps): what fit() measures; the pack's pieces are made by apply_exact"""
    s = src[start:start + length]
    if len(s) < length: s = np.concatenate([s, np.zeros(length - len(s))])
    return gain * s * window(length, fin, fout)

def apply_exact(line, first, start, length, gain, fin, fout):
    """[commdisc 2026-11-18] How the developer's pieces were really cut -- found on the real disc, all 151 the same samples:
    the WHOLE line's 16-bit samples (line: before the silence trim; start counts from the trimmed start, first, and may be
    negative), as floats x / 32768, times gain (1) and numpy.linspace(0, 1, F) fades at the two ends, written back to 16
    bits by truncation ((int)(f * 32767), as vgmstream writes). The native builder (packbuild/comm_builder.cpp) does the
    same in the same order. Returns the 16-bit samples as numbers."""
    at = first + start + np.arange(length)
    v = np.where((at >= 0) & (at < len(line)), np.asarray(line, dtype=np.float64)[np.clip(at, 0, len(line) - 1)], 0.0)
    w = np.ones(length)
    if fin >= 2: w[:fin] = np.arange(fin) / (fin - 1)
    if fout >= 2: w[length - fout:] = np.minimum(w[length - fout:], np.arange(fout - 1, -1, -1) / (fout - 1))
    return np.clip(np.trunc(v / 32768.0 * gain * w * 32767.0), -32768, 32767)
