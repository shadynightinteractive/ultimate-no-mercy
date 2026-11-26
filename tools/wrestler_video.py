#!/usr/bin/env python3
"""Makes a wrestler folder's Titantron video from any video file.

    python tools/wrestler_video.py <folder> <video file> [--seconds 60]

ffmpeg reads the video and takes 7.5 pictures a second, square-cropped to 64 x 64, the size every Titantron frame in the
game is. Each frame gets its own 256-colour palette, the way the game's are kept. Writes <folder>/video.bin: the frames
one after another, each a 16-byte header, the palette (512 bytes, RGBA5551) and the picture (4096 bytes).

The big screen in the arena plays all of it. The small video panel on some screens only plays the first 72 frames, because
it goes through the game's own frame files, and there are 72 of those.
"""
import argparse, os, struct, subprocess
from PIL import Image

FRAME_W = FRAME_H = 64
FRAME_BYTES = 16 + 512 + FRAME_W * FRAME_H
PICTURES_A_SECOND = 7.5
# A frame file's header: "TEX", then the size and format, the same as the game's own frames (read from content.bin).
HEADER = bytes.fromhex('54455800004000400802010000000000')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('video')
    ap.add_argument('--seconds', type=float, default=60.0)
    a = ap.parse_args()
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-t', str(a.seconds), '-i', a.video, '-vf',
                          'fps=%g,crop=min(iw\\,ih):min(iw\\,ih),scale=%d:%d' % (PICTURES_A_SECOND, FRAME_W, FRAME_H),
                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], check=True, capture_output=True).stdout
    one = FRAME_W * FRAME_H * 3
    os.makedirs(a.folder, exist_ok=True)
    count = 0
    with open(os.path.join(a.folder, 'video.bin'), 'wb') as out:
        for i in range(0, len(raw) - one + 1, one):
            image = Image.frombytes('RGB', (FRAME_W, FRAME_H), raw[i:i + one]).quantize(256)
            palette = image.getpalette()
            palette += [0] * (768 - len(palette))
            out.write(HEADER)
            for c in range(256):
                r, g, b = palette[c * 3:c * 3 + 3]
                out.write(struct.pack('>H', (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1))
            out.write(image.tobytes())
            count += 1
    print('wrote', a.folder, count, 'frames')


if __name__ == '__main__':
    main()
