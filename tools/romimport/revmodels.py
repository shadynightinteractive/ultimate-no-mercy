"""revmodels.py (romimport, 2026-11-18): WCW/nWo Revenge's models built by Revenge's own code from the ROM, in place of the
rig's captures -- the ones gen\\wmport.py and the Revenge handler read (revport\\...\\<tag>_ram.bin + <tag>_calls.pkl: the
ported Superstars' costumes) and the one the AKI importer reads (handlers_rig\\revenge\\cap\\auto.pkl: every Superstar not
ported, each costume, as rig\\capture_all.py made it: {(code, costume): {part: (mesh file, picture, palette)}}).

How (Revenge's code in mipsemu, on an image made from the ROM):
  the match image = loadmaps\\rev_bm.json + the costume table the boot builds (make_data.rev_boot_extra: the game's
  0x8002BAB8) + the object pool given a scratch base (0x80700000, count 0); then per slot, one slot after the other as in a
  match: the slot's two display-list arenas (the game's 0x80007A74(0x8007C3D0 + 16 slot, 0x2BC, 0x4B0), as its slot set-up
  does), the model 0x8000CFD4(slot, code, attire, mask; colour rows of the costume on the stack), then the picture step
  0x8000C1B8(slot, 0) and (slot, 1) (the load's states 2 and 3). Hooks: file info 0x800035B4 / read 0x80003684 (the
  unpacked file from the ROM), malloc 0x80000854 (a bump heap), free 0x80000958 (nothing); the part loader 0x8000498C is
  logged (its part struct and mesh: what wmport.rev_capture reads from a capture's calls).
The recipes (which codes / costumes / attires / masks a capture had in its slots) are our data: frozen\\revenge_caps.json.

    python revmodels.py <data folder> [--check <the project's data folder>] [--only ports|auto]"""
import json, os, pickle, struct, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(SRC, "rig", "harness"))
import images, mipsemu
from roms import Roms

def match_image(R):
    import make_data
    m = bytearray(images.build(json.load(open(os.path.join(HERE, "loadmaps", "rev_bm.json"))), R))
    make_data.rev_boot_extra(m, R)
    struct.pack_into(">I", m, 0x46858, 0x80700000); struct.pack_into(">h", m, 0x47060, 0)
    return bytes(m)

def entry(img, code, costume):
    """the costume table's 42-byte entry for (code, costume), as the game's 0x8002B7E0 finds it"""
    E = mipsemu.Emu(img)
    return E.rbytes(E.call(0x8002B7E0, [code, costume]), 42)

def load(img, F, men):
    """men: [(code, costume, attire, mask)] by slot -> (RAM after the loads, the part loader's calls)"""
    E = mipsemu.Emu(img)
    H = [0x80600000]
    def malloc(x): n = (x.r[4] + 15) & ~15; x.r[2] = H[0]; H[0] += n; return True
    def rd_info(x): x.w32(x.r[4] + 8, len(F(x.r[5] & 0xFFFF))); x.r[2] = x.r[4]; return True
    def rd_file(x): d = F(x.r[5] & 0xFFFF); x.w32(x.r[4] + 8, len(d)); x.wbytes(x.r[6], d); x.r[2] = x.r[4]; return True
    calls = []
    def part(x): calls.append((2, x.r[4], x.r[5], x.r[6])); return False
    E.hooks.update({0x80000854: malloc, 0x80000958: lambda x: True, 0x800035B4: rd_info, 0x80003684: rd_file, 0x8000498C: part})
    s8 = lambda b: (b - 256 if b > 127 else b) & 0xFFFFFFFF
    for slot, (code, c, att, mask) in enumerate(men):
        e = entry(img, code, c)
        E.call(0x80007A74, [0x8007C3D0 + 16 * slot, 0x2BC, 0x4B0])
        E.call(0x8000CFD4, [slot, code, att, mask], stack_args=[s8(e[38]), s8(e[39]), s8(e[40])], max_steps=30_000_000)
        for half in (0, 1): E.call(0x8000C1B8, [slot, half], max_steps=30_000_000)
    return bytes(E.m), calls

def auto_plan():
    """rig\\capture_all.py's plan: every Superstar not ported (the handler's fixes) with a moveset, costume-major, two a run"""
    sys.path.insert(0, os.path.join(SRC, "tools", "handlers"))
    from handlers.revenge import game as G
    ported = {int(x["revenge"]["code"], 16) for x in G.fixes().values()}
    codes = [e["code"] for e in G.roster() if e["code"] not in ported and e["moveset_file"]]
    return [(c, k) for k in range(4) for c in codes]

def main():
    out = sys.argv[1]
    ref = sys.argv[sys.argv.index("--check") + 1] if "--check" in sys.argv else None
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    sys.path.insert(0, os.path.join(SRC, "mod", "gen"))
    import wmport as W
    R = Roms(); F = R.files("revenge"); img = match_image(R)
    J = json.load(open(os.path.join(HERE, "frozen", "revenge_caps.json")))
    bad = n = 0
    if only in (None, "ports"):
        for tag, rc in J["tags"].items():
            ram, calls = load(img, F, [tuple(s) for s in rc["slots"]])
            d = os.path.join(out, *rc["dir"].split("/")); os.makedirs(d, exist_ok=True)
            open(os.path.join(d, tag + "_ram.bin"), "wb").write(ram)
            pickle.dump(calls, open(os.path.join(d, tag + "_calls.pkl"), "wb"))
            msg = ""
            if ref:
                q = os.path.join(ref, *rc["dir"].split("/")) + "/" + tag
                cram, ccalls = open(q + "_ram.bin", "rb").read(), pickle.load(open(q + "_calls.pkl", "rb"))
                res = ["SAME" if W.rev_capture(ram, calls, s) == W.rev_capture(cram, ccalls, s) else "DIFFERENT" for s in range(2)]
                n += 2; bad += res.count("DIFFERENT"); msg = " ".join(res)
            print("%-34s %s" % (rc["dir"] + "/" + tag, msg), flush=True)
    if only in (None, "auto"):
        plan = auto_plan(); sub = {int(k): v for k, v in J["auto_stand_ins"].items() if k != "about"}
        store = {}
        t = time.time()
        for i in range(0, len(plan), 2):
            pair = plan[i:i + 2]
            men = []
            for code, k in pair:
                lc, lk = sub.get(code, (code, k))
                e = entry(img, lc, lk); men.append((lc, lk, e[37], e[36]))
            ram, calls = load(img, F, men)
            for s, key in enumerate(pair): store[key] = W.rev_capture(ram, calls, s)
        p = os.path.join(out, "handlers_rig", "revenge", "cap", "auto.pkl"); os.makedirs(os.path.dirname(p), exist_ok=True)
        b = pickle.dumps(store); open(p, "wb").write(b)
        msg = ""
        if ref:
            c = pickle.load(open(os.path.join(ref, "handlers_rig", "revenge", "cap", "auto.pkl"), "rb"))
            nd = sum(1 for k in c if store.get(k) != c[k]) + len(set(store) - set(c))
            n += len(c); bad += nd
            msg = "SAME (%d models, key order %s)" % (len(c), "same" if list(c) == list(store) else "DIFFERENT") if not nd else "%d of %d DIFFERENT" % (nd, len(c))
            if list(c) != list(store): bad += 1
        print("%-34s %s (%.0f s)" % ("handlers_rig/revenge/cap/auto.pkl", msg, time.time() - t), flush=True)
    if ref: print("models: %d checked, %s" % (n, "ALL SAME" if not bad else "%d DIFFERENT" % bad))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
