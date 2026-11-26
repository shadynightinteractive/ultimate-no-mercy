"""vadpcm_check.py (phase3, 2026-11-19): proves the C++ VADPCM encoder and decoder (exe\\WWFNoMercyRecomp\\src\\romimport\\
ri_vadpcm.cpp) and the Python reference (mod\\gen\\vadpcm.py, which runs the N64 SDK's tabledesign / vadpcm_enc / vadpcm_dec)
give the same bytes: the book, the ADPCM data and the sample count of encode(), and the PCM of decode().

    python vadpcm_check.py [--synthetic | --all]    (--synthetic: only the made-up sounds, no ROM needed;
                                                     --all: every Redux song, not the first 6)

The cases: made-up sounds (silence, sines, white noise from a fixed seed, clicks, full-scale squares, lengths that are not
a multiple of 16, very short ones), the decoding of random frames, and themes the build encodes from the player's ROMs
(roms\\ or $UNM_ROMS, by fingerprint: tools\\romimport\\roms.py): Revenge's song 0x214 (gen/revplay.py theme(), as
gen/mkcontent.py rev_theme encodes it) and Revenge Redux's songs (gen/reduxmusic.py theme(), as gen/mkcontent.py
redux_theme encodes them; the first one's first 15 seconds too: the encoder's timing case).

Builds ri_vadpcm.cpp with VADPCM_MAIN (and ri_core.cpp) with clang++ / g++ into a temporary folder (-std=c++20 -O2
-ffp-contract=off). Runs at low priority. Prints SAME / DIFFERENT per case; exit 1 when any differs."""
import hashlib, os, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
RI = os.path.join(SRC, "exe", "WWFNoMercyRecomp", "src", "romimport")
sys.path.insert(0, os.path.join(SRC, "mod", "gen")); sys.path.insert(0, os.path.dirname(HERE)); sys.path.insert(0, HERE)
import numpy as np
import vadpcm
from revplay_check import compiler

if os.name == "nt":       # the machine is shared: spare CPU only (IDLE_PRIORITY_CLASS; the C++ test inherits it)
    import ctypes
    ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x40)

def synthetic():
    """(name, PCM int16, rate) made-up sounds"""
    rng = np.random.default_rng(20261119)
    t = np.arange(22050)
    def i16(x): return np.clip(np.round(x), -32768, 32767).astype(np.int16)
    out = [
        ("silence 1000", np.zeros(1000, np.int16), 22050),
        ("sine 440 Hz, 1 s", i16(12000 * np.sin(2 * np.pi * 440 * t / 22050)), 22050),
        ("sine 3 kHz full scale, 5001", i16(32767 * np.sin(2 * np.pi * 3000 * t[:5001] / 22050)), 22050),
        ("two sines + quiet noise, 7777", i16(6000 * np.sin(2 * np.pi * 220 * t[:7777] / 22050) +
                                              3000 * np.sin(2 * np.pi * 1760 * t[:7777] / 22050) +
                                              rng.normal(0, 40, 7777)), 16000),
        ("white noise, 4000", rng.integers(-32768, 32768, 4000).astype(np.int16), 22050),
        ("white noise quiet, 3333", rng.integers(-3, 4, 3333).astype(np.int16), 22050),
        ("click", np.where(np.arange(800) == 300, 32767, 0).astype(np.int16), 22050),
        ("negative click", np.where(np.arange(801) == 17, -32768, 0).astype(np.int16), 22050),
        ("square full scale, 2000", np.where((np.arange(2000) // 25) % 2, 32767, -32768).astype(np.int16), 22050),
        ("alternating +-32768, 512", np.where(np.arange(512) % 2, 32767, -32768).astype(np.int16), 22050),
        ("sawtooth, 1001", i16((np.arange(1001) % 100) * 600 - 30000), 11025),
        ("decaying sine, 37", i16(20000 * np.exp(-np.arange(37) / 10) * np.sin(np.arange(37))), 22050),
        ("1 sample", np.array([12345], np.int16), 22050),
        ("5 samples", np.array([1, -2, 300, -4000, 32767], np.int16), 22050),
        ("15 samples", i16(rng.normal(0, 8000, 15)), 22050),
        ("16 samples", i16(rng.normal(0, 8000, 16)), 22050),
        ("17 samples", i16(rng.normal(0, 8000, 17)), 22050),
        ("33 samples", i16(rng.normal(0, 8000, 33)), 22050),
    ]
    return out

def real():
    """(name, PCM int16, rate) themes from the player's ROMs, as the build encodes them"""
    import revplay, reduxmusic
    from roms import Roms
    out = []
    rom = Roms().rom("revenge")
    if rom is None: print("(Revenge's ROM is not in roms\\: its song skipped)")
    else:
        y, ls, rate, info = revplay.theme(rom, 0x214)
        out.append(("Revenge song 0x214", np.asarray(y, np.int16), int(round(rate))))
    rr = reduxmusic.redux_rom()
    if rr is None: print("(no Revenge Redux in roms\\: its songs skipped)")
    else:
        songs = sorted({s for c in reduxmusic.theme_list(rr) for s in [reduxmusic.song_for(rr, c)] if s})
        for s in songs if "--all" in sys.argv[1:] else songs[:6]:
            y, ls, rate, about = reduxmusic.theme(rr, s)
            y = np.asarray(y, np.int16)
            if not out or not out[-1][0].startswith("Revenge Redux"):    # the encoder's timing case: a 15 s theme
                out.append(("Revenge Redux song 0x%02X, first 15 s" % s, y[:int(15 * rate)], int(round(rate))))
            out.append(("Revenge Redux song 0x%02X" % s, y, int(round(rate))))
    return out

def main():
    cases = synthetic()
    if "--synthetic" not in sys.argv[1:]:
        r = real()
        if len(r) < 2: raise SystemExit("no theme from the ROMs (roms\\ or UNM_ROMS): run with --synthetic for the made-up sounds only")
        cases += r
    tmp = tempfile.mkdtemp(prefix="vadpcm_")
    bad = n_cases = 0
    def p(name): return os.path.join(tmp, name)
    def le(a): return np.asarray(a, np.int16).astype("<i2").tobytes()
    def cpp_dec(data, book, n):
        open(p("d.data"), "wb").write(bytes(data)); open(p("d.book"), "wb").write(bytes(book))
        subprocess.check_call([exe, "dec", p("d.data"), p("d.book"), str(n), p("d.pcm")])
        return open(p("d.pcm"), "rb").read()
    try:
        exe = p("vadpcm.exe")
        subprocess.check_call([compiler(), "-std=c++20", "-O2", "-ffp-contract=off", "-DVADPCM_MAIN", "-I" + RI, "-o", exe,
                               os.path.join(RI, "ri_vadpcm.cpp"), os.path.join(RI, "ri_core.cpp")])
        for name, pcm, rate in cases:
            # encode: the Python (the SDK tools through WSL) and the C++
            t0 = time.time()
            book, data, ns = vadpcm.encode(pcm, rate)
            t_py = time.time() - t0
            open(p("in.pcm"), "wb").write(le(pcm))
            line = subprocess.check_output([exe, "enc", p("in.pcm"), p("o.book"), p("o.data")], text=True).split()
            facts = dict(zip(line[0::2], line[1::2]))
            cb, cd = open(p("o.book"), "rb").read(), open(p("o.data"), "rb").read()
            same_enc = cb == bytes(book) and cd == bytes(data) and int(facts["samples"]) == ns
            # decode: the Python's of its own encoding, the C++'s of the same bytes
            pd = le(vadpcm.decode(data, book, ns))
            same_dec = cpp_dec(data, book, ns) == pd
            n_cases += 2; bad += (not same_enc) + (not same_dec)
            print("%s  encode %-34s %7d samples (%5.1f s at %d), book %s, data %d bytes md5 %s; C++ %.1f ms, Python %.1f s" % (
                "SAME     " if same_enc else "DIFFERENT", name, ns, ns / rate, rate, hashlib.md5(bytes(book)).hexdigest()[:8],
                len(data), hashlib.md5(bytes(data)).hexdigest()[:8], float(facts["ms"]), t_py))
            if not same_enc:
                print("           C++ book %s, data %d bytes md5 %s, samples %s; first differing data byte %s" % (
                    hashlib.md5(cb).hexdigest()[:8], len(cd), hashlib.md5(cd).hexdigest()[:8], facts["samples"],
                    next((i for i in range(min(len(cd), len(data))) if cd[i] != data[i]), "-")))
            print("%s  decode %-34s %7d samples, md5 %s" % ("SAME     " if same_dec else "DIFFERENT", name, ns,
                                                          hashlib.md5(pd).hexdigest()[:8]))
        # decode of frames no encoder made: random residuals and scales (the predictor index kept within the book's 4)
        rng = np.random.default_rng(64)
        book = vadpcm.encode(cases[1][1], 22050)[0]
        for n in (16, 100, 1000, 4097):
            fr = rng.integers(0, 256, ((n + 15) // 16, 9), dtype=np.uint8)
            fr[:, 0] &= 0xF3
            data = fr.tobytes()
            pd = le(vadpcm.decode(data, book, n))
            same = cpp_dec(data, book, n) == pd
            n_cases += 1; bad += not same
            print("%s  decode random frames, %d samples, md5 %s" % ("SAME     " if same else "DIFFERENT", n,
                                                                    hashlib.md5(pd).hexdigest()[:8]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("%d of %d cases the same" % (n_cases - bad, n_cases))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
