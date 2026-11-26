#!/usr/bin/env python3
"""Makes a wrestler folder's select-screen photo from any picture.

    python tools/wrestler_photo.py <folder> <picture>

The picture is cropped to a square from its centre, scaled to 64 x 64 and reduced to 16 colours, which is what the game's
photos are. Writes <folder>/photo.bin: the palette (32 bytes, RGBA5551) and the picture at 4 bits a pixel (2048 bytes).
A close-up of the face works best: the box on the select screen is small.
"""
import os, struct, sys
from PIL import Image


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    folder, picture = sys.argv[1], sys.argv[2]
    image = Image.open(picture).convert('RGB')
    w, h = image.size
    side = min(w, h)
    image = image.crop(((w - side) // 2, (h - side) // 2, (w + side) // 2, (h + side) // 2))
    image = image.resize((64, 64), Image.LANCZOS).quantize(16)
    palette = image.getpalette()[:48]
    palette += [0] * (48 - len(palette))
    tlut = bytearray()
    for i in range(16):
        r, g, b = palette[i * 3:i * 3 + 3]
        tlut += struct.pack('>H', (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1)
    pixels = image.tobytes()
    data = bytes(pixels[i] << 4 | pixels[i + 1] for i in range(0, len(pixels), 2))
    os.makedirs(folder, exist_ok=True)
    open(os.path.join(folder, 'photo.bin'), 'wb').write(bytes(tlut) + data)
    print('wrote', os.path.join(folder, 'photo.bin'))


if __name__ == '__main__':
    main()
