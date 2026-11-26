#!/usr/bin/env python3
"""Makes a wrestler folder's entrance theme from a music file.

    python tools/wrestler_theme.py <folder> <music file>

The file can be anything ffmpeg reads (mp3, wav, ogg, flac, m4a). It's made the way every added theme is
(mod/gen/mp3theme.py): mono, at the game's theme rate, as loud as the game's themes, looping on a stretch that comes
round again. Writes into <folder>:
    theme.bin    the sound, compressed the way the game's sound code plays it (VADPCM).
    theme.meta   its wave table: the game's 0xD0-byte record, then a byte for its level, a byte for whether it loops,
                 two spare bytes and 4 bytes of tuning (mod src/media.c reads these through the exe).
"""
import os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'mod', 'gen'))
import mp3theme, vadpcm


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    folder, music = sys.argv[1], sys.argv[2]
    pcm, loop_start, rate, info = mp3theme.theme(music)
    book, data, samples = vadpcm.encode(pcm, int(round(rate)))
    decoded = vadpcm.decode(data, book, samples)
    # The sample state just before the loop point, which the game's loop record holds.
    state = decoded[loop_start - 16:loop_start] if loop_start >= 16 else [0] * 16
    wave = bytearray(0xD0)
    struct.pack_into('>IIBB', wave, 0, 0, len(data), 0, 1)
    wave[0x18:0x18 + len(book)] = book
    struct.pack_into('>IIi16h', wave, 0xA0, loop_start, samples, -1, *[int(v) for v in state])
    os.makedirs(folder, exist_ok=True)
    open(os.path.join(folder, 'theme.bin'), 'wb').write(bytes(data))
    level, loops, tune = 0, 1, 0xC2400000      # As mkcontent.py gives every theme made from a music file.
    open(os.path.join(folder, 'theme.meta'), 'wb').write(bytes(wave) + struct.pack('>BBxxI', level, loops, tune))
    print('wrote', folder, 'rate %.1f' % rate, 'samples', samples, 'loop from', loop_start, info)


if __name__ == '__main__':
    main()
