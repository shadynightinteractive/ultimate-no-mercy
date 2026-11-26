#!/usr/bin/env python3
"""Makes an arena folder to edit, as a copy of one of the added arenas.

    python tools/arena_folder.py --list
    python tools/arena_folder.py <folder> --based-on 13 --name "MY ARENA"

Writes into <folder>:
    arena.json       its name and the arena it's based on.
    label.bin        its row on the ARENA page, with its name on it.
    files\\XXXX.bin   the base arena's stage set: each part's model and its pictures, named by game file ID. Change or delete
                     any of them: whatever's there replaces the game's file with that ID while this arena is in play, and
                     whatever isn't there stays the base arena's.

Those files are in the game's formats: a palette is 16 colours (RGBA5551, 32 bytes), and the file after it is its 64 x 64
picture at 4 bits a pixel (an 8-byte header, then 2048 bytes). --swap-colours swaps red and blue in the palettes: that's how
the QA test makes an arena that's visibly its own.
"""
import argparse, json, os, re, struct
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mod')


def table(text, name):
    """A generated two-dimensional u16 table, as rows of numbers."""
    body = re.search(name + r'\[[^\]]*\]\[\d+\] = \{(.*?)\n?\};', text, re.S).group(1)
    return [[int(x, 0) for x in row.split(',') if x.strip()] for row in re.findall(r'\{([^{}]*)\}', body)]


def flat(text, name):
    body = re.search(name + r'\[[^\]]*\] = \{(.*?)\}', text, re.S).group(1)
    return [int(x, 0) for x in body.split(',') if x.strip()]


class Game:
    def __init__(self):
        self.pack = open(os.path.join(ROOT, 'build', 'wmport.bin'), 'rb').read()
        index = open(os.path.join(ROOT, 'src', 'wmport_files.h'), encoding='utf8').read()
        self.first = int(re.search(r'#define WMP_FILE0\s+(\S+)', index).group(1).rstrip('u'), 0)
        body = re.search(r'wmp_files\[[^\]]*\]\[2\]\s*=\s*\{(.*?)\};', index, re.S).group(1)
        self.rows = [(int(a, 0), int(b, 0)) for a, b in re.findall(r'\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}', body)]
        self.arenas = open(os.path.join(ROOT, 'src', 'arena_files.h'), encoding='utf8').read()
        self.ring = open(os.path.join(ROOT, 'src', 'arena_ring_files.h'), encoding='utf8').read()
        self.names = re.findall(r'"([^"]+)"', re.search(r'xarena_names\[XARENA_N\] = \{(.*?)\}', self.arenas, re.S).group(1))

    def file(self, file_id):
        at, size = self.rows[file_id - self.first]
        return self.pack[at:at + size]

    def is_ours(self, file_id):
        return self.first <= file_id < self.first + len(self.rows)


def swap_colours(palette):
    out = bytearray()
    for i in range(0, len(palette), 2):
        c = struct.unpack('>H', palette[i:i + 2])[0]
        r, g, b, a = c >> 11, (c >> 6) & 31, (c >> 1) & 31, c & 1
        out += struct.pack('>H', b << 11 | g << 6 | r << 1 | a)
    return bytes(out)


def make_label(game, base_k, name, path):
    """Keeps the base arena's label file's header and palette, and draws the name in its palette's brightest colour."""
    source = game.file(flat(game.arenas, 'xarena_label_file')[base_k])
    header, palette = source[:16], source[16:48]
    w, h = struct.unpack('>HH', source[4:8])
    colours = struct.unpack('>16H', palette)
    bright = max(range(16), key=lambda i: (colours[i] >> 11) + ((colours[i] >> 6) & 31) + ((colours[i] >> 1) & 31))
    dark = min(range(16), key=lambda i: (colours[i] >> 11) + ((colours[i] >> 6) & 31) + ((colours[i] >> 1) & 31))
    image = Image.new('L', (w, h), 0)
    draw = ImageDraw.Draw(image)
    font = ImageFont.load_default()
    box = draw.textbbox((0, 0), name, font=font)
    draw.text(((w - (box[2] - box[0])) // 2, (h - (box[3] - box[1])) // 2 - box[1]), name, fill=255, font=font)
    pixels = [bright if p > 127 else dark for p in image.tobytes()]
    data = bytes(pixels[i] << 4 | pixels[i + 1] for i in range(0, len(pixels), 2))
    open(path, 'wb').write(header + palette + data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder', nargs='?')
    ap.add_argument('--based-on', type=int)
    ap.add_argument('--name')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--swap-colours', action='store_true')
    a = ap.parse_args()
    game = Game()
    if a.list:
        for k, name in enumerate(game.names):
            print(10 + k, name)
        return
    if not a.folder or a.based_on is None or not a.name:
        raise SystemExit('needs a folder, --based-on and --name (or --list)')
    k = a.based_on - 10
    if not 0 <= k < len(game.names):
        raise SystemExit('--based-on must be one of the added arenas: see --list')
    files = os.path.join(a.folder, 'files')
    os.makedirs(files, exist_ok=True)
    json.dump({'name': a.name, 'based_on': a.based_on}, open(os.path.join(a.folder, 'arena.json'), 'w'), indent=2)
    make_label(game, k, a.name, os.path.join(a.folder, 'label.bin'))
    count = flat(game.ring, 'xring_set_n')[k]
    written = 0
    for mesh, palette in zip(table(game.ring, 'xring_set_mesh')[k][:count], table(game.ring, 'xring_set_pal')[k][:count]):
        for file_id, data in ((mesh, None), (palette, None), (palette + 1, None)):
            if not game.is_ours(file_id):
                continue
            data = game.file(file_id)
            if a.swap_colours and file_id == palette:
                data = swap_colours(data)
            open(os.path.join(files, '%04X.bin' % file_id), 'wb').write(data)
            written += 1
    print('wrote', a.folder, written, 'files, based on', game.names[k])


if __name__ == '__main__':
    main()
