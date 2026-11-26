#!/usr/bin/env python3
"""Makes a wrestler folder's full-detail model files from a model already built by data/austin2k15/hd_pack.py.

    python tools/wrestler_folder.py <folder> --index <the hd_index header hd_pack.py wrote> --pack <the pack it wrote>
                                    --pictures <its pictures file> --first-picture <n> --photo-from <n>

(no2k15, 2026-11-23) The game has no built-in full-detail men any more: the two WWE 2K15 test slots (0x011A, 0x011B), whose
models were the ones hd_pack.py had built (mod/src/hd_index.h, hd_index_rock.h, mod/build/hd.bin, hd_rock.bin), are removed.
A folder made with this is the way a full-detail model comes in now; his wrestler.json's "based_on" must be a built-in
WCW/nWo Revenge port (mod src/parts.c).

Writes into <folder>:
    hd.bin        the model: points, display lists, stand-in pictures. Loaded into game memory as it is.
    hd.json       where everything is in hd.bin, and the model's bones and joints (mod src/hd.c, HdModel).
    pictures.pak  the six full-size pictures, in the renderer's format: "NMT1", a count, then for each its
                  width, height, byte count and RGBA pixels (little-endian numbers).
    photo.bin     his select-screen photo: a 16-colour palette (32 bytes, RGBA5551) then 64 x 64 pixels at
                  4 bits each (2048 bytes), as the game's photo files hold them.

It doesn't write wrestler.json, which is the person's own: see wrestlers/_example.
"""
import argparse, json, os, re, struct
from PIL import Image


def read_index(path):
    """Reads a generated hd_index*.h into the fields hd.json holds."""
    text = open(path, encoding='utf8').read()
    out = {}
    for name, value in re.findall(r'#define HD\d+_(\w+) (0x[0-9A-Fa-f]+|\d+)u', text):
        out[name.lower()] = int(value, 0)
    # The first array is the 28 bones, of six numbers each; the second is the joints: bone, parent, then x, y, z.
    bones_text = re.search(r'hd_bones_\d+\[28\] = \{(.*?)\n\};', text, re.S).group(1)
    bones = [[int(x, 0) for x in row.split(',')] for row in re.findall(r'\{\s*([^{}]+?)\s*\}', bones_text)]
    joints_text = re.search(r'hd_joints_\d+\[HD_JOINTS\] = \{(.*?)\n\};', text, re.S).group(1)
    joints = []
    for row in re.findall(r'\{\s*([^{}]+?)\s*\}', joints_text):
        parts = [p.strip().rstrip('f') for p in row.split(',')]
        joints.append([int(parts[0], 0), int(parts[1], 0)] + [float(p) for p in parts[2:5]])
    if len(bones) != 28 or len(joints) != 15:
        raise SystemExit('%s: expected 28 bones and 15 joints, found %d and %d' % (path, len(bones), len(joints)))
    out['bones'] = bones
    out['joints'] = joints
    return out


def read_pictures(path, first, count):
    """Returns (width, height, RGBA bytes) for pictures first .. first + count - 1 of a .pak."""
    pictures = []
    with open(path, 'rb') as f:
        f.read(4)
        total = struct.unpack('<I', f.read(4))[0]
        for i in range(min(total, first + count)):
            w, h, n = struct.unpack('<III', f.read(12))
            if i >= first:
                pictures.append((w, h, f.read(n)))
            else:
                f.seek(n, 1)
    return pictures


def write_photo(path, picture):
    """A 64 x 64, 16-colour photo from the top of a picture (the head's map has the face there)."""
    w, h, rgba = picture
    image = Image.frombytes('RGBA', (w, h), rgba).convert('RGB')
    image = image.crop((w // 4, 0, w * 3 // 4, h // 2)).resize((64, 64), Image.LANCZOS).quantize(16)
    palette = image.getpalette()[:48]
    palette += [0] * (48 - len(palette))
    tlut = bytearray()
    for i in range(16):
        r, g, b = palette[i * 3:i * 3 + 3]
        tlut += struct.pack('>H', (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1)
    px = image.tobytes()
    ci4 = bytes(px[i] << 4 | px[i + 1] for i in range(0, len(px), 2))
    open(path, 'wb').write(bytes(tlut) + ci4)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('--index', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--pictures', required=True)
    ap.add_argument('--first-picture', type=int, required=True)
    ap.add_argument('--photo-from', type=int, required=True, help='which picture in the .pak his photo is made from')
    a = ap.parse_args()
    os.makedirs(a.folder, exist_ok=True)
    json.dump(read_index(a.index), open(os.path.join(a.folder, 'hd.json'), 'w'), indent=1)
    open(os.path.join(a.folder, 'hd.bin'), 'wb').write(open(a.pack, 'rb').read())
    pictures = read_pictures(a.pictures, a.first_picture, 6)
    with open(os.path.join(a.folder, 'pictures.pak'), 'wb') as f:
        f.write(b'NMT1' + struct.pack('<I', len(pictures)))
        for w, h, rgba in pictures:
            f.write(struct.pack('<III', w, h, len(rgba)) + rgba)
    write_photo(os.path.join(a.folder, 'photo.bin'), read_pictures(a.pictures, a.photo_from, 1)[0])
    print('wrote', a.folder)


if __name__ == '__main__':
    main()
