"""Today's baked-in port data, read back (2026-11-12, handlers): what the mod compiles in for the ported wrestlers, so the
handlers' folders can be compared with it (P0) and, where the format can't yet carry it, copied (said so where it is).
Read only: mod\\src\\roster_ext.h, parts_ext.h, rev_photos.h, wmport_files.h, mod\\build\\wmport.bin,
data\\gen_data\\wmport_models.pkl (the models gen/mkovr.py compiles into parts_ovr.h)."""
import os, pickle, re, struct
from . import DEV     # (the Development folder: handlers/__init__.py, 2026-11-13)
SRC = os.path.join(DEV, "mod", "src")

def _arrays(text, name):
    m = re.search(r"%s\[[^\]]*\]\[[^\]]*\] = \{(.*?)\n\};" % re.escape(name), text, re.S)
    return [bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})\b", row)) for row in re.findall(r"\{([^{}]*)\}", m.group(1))]

class Baked:
    def __init__(self):
        rx = open(os.path.join(SRC, "roster_ext.h")).read()
        self.late = [dict(id=int(a, 16), name=n, short=s, music=int(mu), video=int(v), h=int(h), w=int(w))
                     for a, n, s, mu, v, h, w in re.findall(r'\{ 0x([0-9A-F]{4}), \d+, "([^"]*)", "([^"]*)", "", \{ (\d+), \d+ \}, \{ (\d+), \d+ \}, (\d+), (\d+) \}', rx.split("ext_late[EXT_LATE_N]")[1].split("};")[0])]
        self.late_row = [int(x) for x in re.search(r"ext_late_moveset\[EXT_LATE_N\] = \{([^}]*)\}", rx).group(1).split(",")]
        self.movesets = _arrays(rx, "moveset_key")
        cells = re.search(r"moveset_cells\[MOVESETS_N\]\[MOVESET_CELLS\] = \{(.*?)\n\};", rx, re.S).group(1)
        ncells = [int(x) for x in re.search(r"moveset_ncells\[MOVESETS_N\] = \{([^}]*)\}", rx).group(1).split(",")]
        rows = re.findall(r"^    \{ (.*) \},$", cells, re.M)
        self.cells = [[tuple(int(v) for v in t) for t in re.findall(r"\{ (\d+), (\d+), (\d+) \}", r)][:ncells[i]] for i, r in enumerate(rows)]
        px = open(os.path.join(SRC, "parts_ext.h")).read()
        self.looks_key = _arrays(px, "ext_looks_key")
        self.looks = _arrays(px, "ext_looks")          # (2026-11-13, handlers1: the costumes unpacked, 0x3F bytes each)
        rp = open(os.path.join(SRC, "rev_photos.h")).read()
        self.photos = {}
        for key in set(re.findall(r"static const u8 (\w+)_photo_tlut", rp)):
            t = re.search(r"%s_photo_tlut\[32\] = \{(.*?)\};" % key, rp, re.S).group(1)
            c = re.search(r"%s_photo_ci4\[2048\] = \{(.*?)\};" % key, rp, re.S).group(1)
            self.photos[key] = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", t)) + bytes(int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", c))
        wf = open(os.path.join(SRC, "wmport_files.h")).read()
        self.wmp0 = int(re.search(r"#define WMP_FILE0 0x([0-9A-F]+)u", wf).group(1), 16)
        self.wmp = [(int(o, 16), int(n)) for o, n in re.findall(r"\{ 0x([0-9A-F]{6}), (\d+) \}", wf)]
        self.pack = open(os.path.join(DEV, "mod", "build", "wmport.bin"), "rb").read()
        self.models = pickle.load(open(os.path.join(DEV, "data", "gen_data", "wmport_models.pkl"), "rb"))
    def late_index(self, game_id):
        return next(i for i, e in enumerate(self.late) if e["id"] == game_id)
    def file(self, fid):
        """a file a baked model names: the pack's (WMP_FILE0..), else the cartridge's own"""
        if fid >= self.wmp0 and fid - self.wmp0 < len(self.wmp):
            o, n = self.wmp[fid - self.wmp0]; return self.pack[o:o + n]
        p = os.path.join(DEV, "data", "extracted", "nmfiles", "f", "%04x.bin" % fid)
        return open(p, "rb").read() if os.path.exists(p) else None
