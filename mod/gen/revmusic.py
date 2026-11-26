"""WCW/nWo Revenge's entrance themes as No Mercy themes. Revenge's songs are sequences (its files 0x210-0x224, played on
its own instruments); since 2026-11-18 (revmusic, the user's decision B) a song is PLAYED BY US from the player's Revenge
ROM -- Revenge's own sequencer and the N64 audio library's synthesizer, mirrored in gen/revplay.py (its C++ twin:
tools\\romimport\\native\\revplay.cpp, the same bytes) -- no longer recorded from an emulator.

The theme sample is made as No Mercy Plus's themes are: mono, at the rate the game plays its themes' samples (THEME_RATE:
measured, a 1 kHz tone in a theme's place came out at 489.2 Hz), its loop the song's own (exactly one period of the
sequence, its seam cross-faded into the same place one period earlier).

    theme(f) -> (PCM int16, loop start, rate, about)     f: the song's file number in hex ("0214")"""
import os as _os, sys as _sys
_HERE = _os.path.dirname(_os.path.abspath(__file__))
_sys.path.insert(0, _HERE)
_sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.dirname(_HERE)), "tools", "romimport"))
import revplay
OUT_RATE = revplay.OUT_RATE          # the game's sound per second of the game (479.93 samples a frame at 60.0176 frames)
THEME_RATE = revplay.THEME_RATE      # a theme's samples, per second of the game (tune 0xC2400000)

_rom = None
def _revenge_rom():
    """the player's Revenge ROM (tools\\romimport\\roms.py: found in roms\\ by its fingerprint)"""
    global _rom
    if _rom is None:
        from roms import Roms
        _rom = Roms().rom("revenge")
        if _rom is None: raise RuntimeError("WCW/nWo Revenge's ROM is not in roms\\ (its songs are played from it)")
    return _rom

def theme(f):
    """PCM (int16, THEME_RATE adjusted), its loop start (samples), the rate, about -- the song from its first sound, its
    loop one period of the sequence"""
    return revplay.theme(_revenge_rom(), int(f, 16))
