"""A low-detail model in a wrestler folder (2026-11-12, handlers): the format the handlers write and the game would read.

No Mercy draws a wrestler from a model descriptor (0x270 bytes, func_80018A3C's output): per part (1-20, then the second
head piece and the split chest and arms, then the attachments) the files of its mesh, picture and palette, and after them
what the skeleton reads (skel.h: the ported models' flags, hands, joins). Today the ported wrestlers' descriptors are
compiled into the mod (parts_ovr.h) and their files packed into build/wmport.bin, numbered from 0x5000.

In a folder, each costume is
    model/c<N>.bin   the descriptor, 0x270 bytes, with every file field that names one of the model's own files zeroed
    model/model.json { "format": 1, "files": [ "files/<sha1>.bin", ... ],
                       "costumes": [ { "descriptor": "c0.bin", "keep": [596],              (bytes the game's own model keeps:
                                       "fields": [ [offset, file index], ... ] }, ... ] }    0x254, its state byte)
The game gives the folder's files numbers of its own, one after another in list order (a picture's palette is listed
just before the picture and gets the number before it, as AKI's files are), and writes them into the fields. A field
that names one of the cartridge's own files (No Mercy's hand meshes, say) keeps its number and isn't listed.
"""
import hashlib, json, os, struct

POOL0 = 0x5000          # the numbers the build pool hands out (gen/wmport.py WMP_FILE0) -- any base works
DESC = 0x270
KEEP = [0x254]           # (gen/mkovr.py: a ported model's every byte but 0x254 is put in)

def part_fields():
    """every offset in a descriptor that can name a file: parts 1-20 at 8k, part 21 (the second head piece) at 0xA8,
    22-24 (the split chest and arms) at 0xB0.., 25-27 (the attachments) at 0xC8..; mesh, picture, palette"""
    out = []
    for k in range(1, 28):
        o = 8 * k if k <= 20 else 0xA8 + 8 * (k - 21)
        out += [o, o + 2, o + 4]
    return out
FIELDS = part_fields()

class Pool:
    """Files by content, numbered from POOL0 as they're first seen; a (picture, palette) pair takes two numbers, the
    palette's first (gen/wmport.py's Files, the same numbering, so a model built here is the same model)."""
    def __init__(self): self.data = []; self.by = {}; self.pairs = set()
    def one(self, b):
        if b not in self.by: self.by[b] = POOL0 + len(self.data); self.data.append(b)
        return self.by[b]
    def pair(self, tex, pal):
        key = ("pair", tex, pal)
        if key not in self.by:
            self.by[key] = POOL0 + len(self.data); self.data += [pal, tex]; self.pairs.add(self.by[key])
        q = self.by[key]
        return q + 1, q
    def get(self, i): return self.data[i - POOL0]
    def ours(self, v): return POOL0 <= v < POOL0 + len(self.data)

def write(folder, descriptors, pool):
    """descriptors: [0x270 bytes] per costume, file fields numbered by pool -> model/ in the folder; -> model.json's dict"""
    used = set()
    for d in descriptors:
        for o in FIELDS:
            v = struct.unpack_from(">H", d, o)[0]
            if pool.ours(v): used.add(v)
    keep = set()
    for v in sorted(used):                        # (a pair stays a pair: both or neither)
        keep.add(v)
        if v in pool.pairs: keep.add(v + 1)
        if v - 1 in pool.pairs: keep.add(v - 1)
    order = sorted(keep)
    local = {v: i for i, v in enumerate(order)}
    os.makedirs(os.path.join(folder, "model", "files"), exist_ok=True)
    names = []
    for v in order:
        b = pool.get(v)
        n = "files/%s.bin" % hashlib.sha1(b).hexdigest()[:16]
        open(os.path.join(folder, "model", n), "wb").write(b)
        names.append(n)
    cos = []
    for c, d in enumerate(descriptors):
        d = bytearray(d); fields = []
        for o in FIELDS:
            v = struct.unpack_from(">H", d, o)[0]
            if pool.ours(v): fields.append([o, local[v]]); struct.pack_into(">H", d, o, 0)
        open(os.path.join(folder, "model", "c%d.bin" % c), "wb").write(bytes(d))
        cos.append({"descriptor": "c%d.bin" % c, "keep": KEEP, "fields": fields})
    j = {"format": 1, "about": "his own low-detail model, one descriptor per costume (handlers/aki/model.py has the format)",
         "files": names, "costumes": cos}
    json.dump(j, open(os.path.join(folder, "model", "model.json"), "w"), indent=1)
    return j

def read(folder, first=0x6400):
    """the folder's model as the game would have it: [(descriptor with the files numbered from `first`)], {number: bytes}"""
    j = json.load(open(os.path.join(folder, "model", "model.json")))
    files = {first + i: open(os.path.join(folder, "model", n), "rb").read() for i, n in enumerate(j["files"])}
    out = []
    for c in j["costumes"]:
        d = bytearray(open(os.path.join(folder, "model", c["descriptor"]), "rb").read())
        for o, i in c["fields"]: struct.pack_into(">H", d, o, first + i)
        out.append(bytes(d))
    return out, files
