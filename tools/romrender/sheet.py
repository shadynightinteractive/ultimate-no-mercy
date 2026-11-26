"""sheet.py (romrender, 2026-11-18): a contact sheet of the pictures the renderer replaced -- the old (made from emulator
screenshots) beside the new (rendered), for the user to look at. A developer's tool: reads two builds, writes one PNG.

    python sheet.py <old wrestlers> <new wrestlers> <old wmport.bin> <new wmport.bin> <wmport_files.h> <arena_files.h> <out.png>
                    [<old src folder> <new src folder>]     (+ the built-in ports' photos: rev_photos.h, jarrett_ / meanie_photo.h)

Rows: every imported wrestler's photo (old | new, 2x); every arena's VS backdrop and ARENA page (old | new); Bret's and
Neidhart's old Titantron frames (every 6th), marked REMOVED (the user's decision, 2026-11-18: their files are empty now).
The old pack is read with the old src folder's wmport_files.h when one is given."""
import os, re, struct, sys
from PIL import Image, ImageDraw

def rgb(c): return (((c >> 11) & 31) * 255 // 31, ((c >> 6) & 31) * 255 // 31, ((c >> 1) & 31) * 255 // 31)

def photo_img(b, bg=(48, 0, 48)):
    pal = struct.unpack(">16H", b[:32]); im = Image.new("RGB", (64, 64))
    im.putdata([rgb(pal[i]) if i else bg for i in ((b[32 + (k >> 1)] >> (0 if k & 1 else 4)) & 15 for k in range(4096))])
    return im

def pack_files(pack, index_h):
    s = open(index_h).read()
    offs = [(int(a, 16), int(n)) for a, n in re.findall(r"\{ (0x[0-9A-F]+), (\d+) \}", s)]
    d = open(pack, "rb").read()
    return [d[o:o + n] for o, n in offs]

def backdrop_img(files, first):
    pal = struct.unpack(">16H", files[first][:32]); im = Image.new("RGB", (320, 240))
    for t in range(40):
        px = files[first + t][32 if t == 0 else 0:]
        for i in range(64 * 30):
            k = (px[i >> 1] >> (0 if i & 1 else 4)) & 15
            im.putpixel((64 * (t % 5) + i % 64, 30 * (t // 5) + i // 64), rgb(pal[k]))
    return im

def ci8_img(b, off, w, h, bg=(48, 0, 48)):
    pal = struct.unpack(">256H", b[off:off + 512]); im = Image.new("RGB", (w, h))
    im.putdata([rgb(pal[i]) if pal[i] & 1 or i else bg for i in b[off + 512:off + 512 + w * h]])
    return im

def page_img(files, first):
    im = Image.new("RGB", (128, 96), (48, 0, 48))
    for k in range(6):
        im.paste(ci8_img(files[first + k], 16, 64, 32), (64 * (k // 3), 32 * (k % 3)))
    return im

def header_photos(folder):
    """{array prefix: photo.bin bytes} of the ports' photo headers in a src folder"""
    out = {}
    for fn in ("rev_photos.h", "jarrett_photo.h", "meanie_photo.h"):
        s = open(os.path.join(folder, fn)).read()
        for name, body in re.findall(r"static const u8 (\w+)_photo_tlut\[32\] = \{([^}]*)\}", s):
            ci = re.search(r"static const u8 %s_photo_ci4\[2048\] = \{([^}]*)\}" % name, s).group(1)
            out[name] = bytes(int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", body + ci))
    return out

def main():
    ow, nw, op, np_, idx, af, out = sys.argv[1:8]
    cols = ["Revenge", "WrestleMania 2000", "Imported"]
    pairs = []
    for c in cols:
        for sid in sorted(os.listdir(os.path.join(nw, c))):
            a, b = os.path.join(ow, c, sid, "photo.bin"), os.path.join(nw, c, sid, "photo.bin")
            if os.path.exists(a) and os.path.exists(b): pairs.append((sid, open(a, "rb").read(), open(b, "rb").read()))
    if len(sys.argv) > 9:
        oh, nh = header_photos(sys.argv[8]), header_photos(sys.argv[9])
        pairs += [("built-in port: " + k, oh[k], nh[k]) for k in nh if k in oh]
    old_idx = os.path.join(sys.argv[8], "wmport_files.h") if len(sys.argv) > 9 else idx
    OF, NF = pack_files(op, old_idx), pack_files(np_, idx)
    s = open(af).read()
    prev = [int(x, 16) - 0x5000 for x in re.search(r"xarena_preview_file\[XARENA_N\] = \{([^}]*)\}", s).group(1).split(",")]
    back = [int(x, 16) - 0x5000 for x in re.search(r"xarena_backdrop_file\[XARENA_N\] = \{([^}]*)\}", s).group(1).split(",")]
    names = re.search(r"xarena_names\[XARENA_N\] = \{([^}]*)\}", s).group(1).replace('"', "").split(", ")
    trons = [i for i in range(len(NF)) if len(OF[i]) == 4624 and OF[i][:3] == b"TEX" and NF[i] != OF[i]]
    # layout
    per_row = 8; cell = 2 * 128 + 12
    ph_rows = (len(pairs) + per_row - 1) // per_row
    W = per_row * cell + 20
    H = 40 + ph_rows * 160 + 30 + len(back) * 260 + 30 + (2 * 160 if trons else 0) + 20
    S = Image.new("RGB", (W, H), (24, 24, 24)); d = ImageDraw.Draw(S)
    y = 10
    d.text((10, y), "Wrestler photos: left = old (cropped emulator screenshot), right = new (rendered by tools\\romrender from the model)  [%d]" % len(pairs), fill=(255, 255, 0)); y += 26
    for i, (sid, a, b) in enumerate(pairs):
        x0, y0 = 10 + (i % per_row) * cell, y + (i // per_row) * 160
        S.paste(photo_img(a).resize((128, 128)), (x0, y0)); S.paste(photo_img(b).resize((128, 128)), (x0 + 130, y0))
        d.text((x0, y0 + 131), sid[:38], fill=(220, 220, 220))
    y += ph_rows * 160 + 10
    d.text((10, y), "Arenas: VS backdrop old | new, ARENA page old | new", fill=(255, 255, 0)); y += 20
    for k in range(len(back)):
        S.paste(backdrop_img(OF, back[k]), (10, y)); S.paste(backdrop_img(NF, back[k]), (340, y))
        S.paste(page_img(OF, prev[k]).resize((256, 192)), (680, y)); S.paste(page_img(NF, prev[k]).resize((256, 192)), (946, y))
        d.text((1220, y + 4), names[k].strip(), fill=(220, 220, 220)); y += 250
    y += 10
    if not trons: S = S.crop((0, 0, W, y)); S.save(out); print(out, S.size, "photos", len(pairs), "arenas", len(back), "no Titantron frames changed"); return
    d.text((10, y), "Titantron: Bret Hart's and Jim Neidhart's old frames (every 6th) -- REMOVED: their %d files are empty now, "
              "no Titantron unless the player's folder gives one" % len(trons), fill=(255, 255, 0)); y += 20
    half = len(trons) // 2
    for m, who in enumerate(("Bret Hart", "Jim Neidhart")):
        ids = trons[m * half:(m + 1) * half]
        for j, i in enumerate(ids[::6]):
            S.paste(ci8_img(OF[i], 16, 64, 64).resize((128, 128)), (10 + j * 132, y))
            d.line((10 + j * 132, y, 138 + j * 132, y + 128), fill=(255, 40, 40), width=3)
        d.text((10, y + 131), "%s: REMOVED (new: %s)" % (who, "every file empty" if all(len(NF[i]) == 0 for i in ids) else "NOT EMPTY"),
               fill=(255, 80, 80)); y += 160
    S.save(out); print(out, S.size, "photos", len(pairs), "arenas", len(back), "Titantron frames removed", len(trons))

if __name__ == "__main__":
    main()
