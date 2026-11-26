# wm_sel.py (wm2kall, 2026-11-13): WrestleMania 2000's own render of a Superstar in a costume, on its Superstar Select --
# the source pictures for the visual check (visual.py) and his photo (aki/photo.py).
# From cap\sel1.st (Exhibition, Single Match, 1P vs CPU, the cursor on the list's first entry, Steve Austin = record 1): the
# entry's record pointer (the roster's table, RAM 0x800411B0) given his record (and his given the first's, so the table
# stays whole), the model loader's costume forced (0x800146CC, wm_cap.py), then the cursor down and back up -- the select
# loads the model of the entry under the cursor again, now his. Pictures at set frames after: the first before his model is
# in (the plate: the same screen without him), then his poses as he stands and turns.
#   python wm_sel.py <tag> k:c [k:c ...]      -> cap\<tag>_k<k>_c<c>_plate.png, ..._p<i>.png
import os, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import wm, wm_cap

ROSTER, ENTRY = 0x800411B0, 1
START = "sel1"
PLATE_AT = int(os.environ.get("WMSEL_PLATE", "2"))          # (frames after the cursor comes back: before the model is in)
POSES = [int(x) for x in os.environ.get("WMSEL_POSES", "60,90,120,150,180,210,240,270").split(",")]

def render(M, tag, k, c):
    wm.session(START)                                         # (the select again, as it was saved)
    ptr = [M.r32(ROSTER + 4 * i) for i in range(0x4C)]
    M.w32(ROSTER + 4 * ENTRY, ptr[k]); M.w32(ROSTER + 4 * k, ptr[ENTRY])
    # (Only his FIRST costume can be shown here. Tried 2026-11-13 and none changed the model the select draws: the model
    #  loader's costume forced (0x800146CC, which does change it in a match: wm_cap.py), the other looks call's costume
    #  forced (0x80015AA8), his record's four looks pointers all made costume c's, and in the match intro the per-slot
    #  costume table 0x8007E288 and the picks' costume bytes -- the select and the intro draw a model of his first costume
    #  that doesn't come from his looks at the time (Al Snow's four, Jeff Jarrett's four: the same picture each time).
    #  So c is only a name here; the visual check compares his other costumes with this one where they must agree: his head.)
    M.load(M.save())
    M.press("DOWN", hold=3, after=40)
    M.run(3, 0, ("UP",))
    t = 0
    name = "%s_k%d_c%d" % (tag, k, c)
    for i, at in enumerate([PLATE_AT] + POSES):
        M.run(at - t); t = at
        wm.step(M, "shot:%s_%s" % (name, "plate" if i == 0 else "p%02d" % (i - 1)))
        t += 4                                                     # (a shot runs four frames: n64.py shot)
    # (the plate's name box still says the man the cursor was on before -- the record swap's -- and his poses' says his
    #  name: the box is drawn over his belly, so the plate takes it from his last pose, and only he differs from it)
    from PIL import Image
    cap = os.path.join(wm.RIG, "cap")
    pl = Image.open(os.path.join(cap, name + "_plate.png")).convert("RGB")
    last = Image.open(os.path.join(cap, name + "_p%02d.png" % (len(POSES) - 1))).convert("RGB")
    w, h = pl.size
    bx = tuple(int(round(v)) for v in (NAME_BOX[0] * w, NAME_BOX[1] * h, NAME_BOX[2] * w, NAME_BOX[3] * h))
    pl.paste(last.crop(bx), bx[:2])
    pl.save(os.path.join(cap, name + "_plate.png"))

NAME_BOX = (0.49, 0.69, 0.92, 0.87)     # (his name, height and weight, as fractions of the select picture)

if __name__ == "__main__":
    t0 = time.time()
    tag = sys.argv[1]
    M = wm.session(START)
    for kc in sys.argv[2:]:
        k, c = (int(x) for x in kc.split(":"))
        render(M, tag, k, c)
    print("done %.0f s" % (time.time() - t0))
