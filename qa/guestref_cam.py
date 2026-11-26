"""guestref (2026-11-22): who the match camera shows, from a QA run's state.csv (scenarios with "qa_camprobe": 1).

The probe (mod/src/logic.c qa_camera_probe) writes the frame's final camera each step: lj_a = eye x | eye z << 16, lj_b =
eye y | looked-at y << 16, lj_c = looked-at x | looked-at z << 16 (signed 16-bit whole numbers), lj_d = the camera's mode |
its shot << 16, lj_e = the special-shot flag | the referee's count << 16. The men's places are s<k>_x / _y / _z.

For every row of the match (match_state 2, the match on) it projects each man (his place, 60 up: his middle) through that camera -- the
game's own projection: 33 degrees high, 4:3 (set_camera_view) -- and reports, per man, the share of rows he is on screen
(inside the picture with a margin), his mean distance from the picture's middle, and how often the camera's looked-at point
is nearer the referee than the competitors' middle. The coordinate sense of the camera against the places is found from
the data (the sign of z that puts the looked-at point nearest the men), so nothing is assumed about it.

    python guestref_cam.py <run folder> [--ref 2] [--comp 0,1] [--from S] [--to S]
"""
import csv, math, os, sys


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def rows_of(run):
    stock = os.path.join(run, "stock_cam.csv")
    if os.path.exists(stock):
        # (the stock game's, from the rig -- private\scratch\guestref\stockref.py: the final camera c0..c5 as floats, the
        #  men's places s<k>_x / _y / _z; put in the probe's words so the same measures apply)
        out = []
        with open(stock, newline="") as f:
            for r in csv.DictReader(f):
                c = [int(round(float(r["c%d" % k]))) & 0xFFFF for k in range(6)]
                d = dict(r)
                d.update({"match_state": "2", "match_frames": r["mf"], "lj_a": str(c[0] | c[2] << 16),
                          "lj_b": str(c[1] | c[4] << 16), "lj_c": str(c[3] | c[5] << 16),
                          "lj_d": str((int(r["mode"]) & 0xFFFF) | int(r["shot"]) << 16)})
                out.append(d)
        return out
    with open(os.path.join(run, "state.csv"), newline="") as f:
        return list(csv.DictReader(f))


def num(r, k):
    v = r.get(k, "0") or "0"
    return int(v, 0) if v.lower().startswith("0x") else (int(float(v)) if "." not in v else float(v))


def cam_of(r):
    a, b, c = num(r, "lj_a"), num(r, "lj_b"), num(r, "lj_c")
    eye = (s16(a), s16(b), s16(a >> 16))
    at = (s16(c), s16(b >> 16), s16(c >> 16))
    return eye, at


def project(eye, at, p):
    """p through the look-at camera: (sx, sy) in -1..1 across the picture, or None behind the camera"""
    fx, fy, fz = at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]
    n = math.sqrt(fx * fx + fy * fy + fz * fz) or 1.0
    fx, fy, fz = fx / n, fy / n, fz / n
    rx, ry, rz = fz, 0.0, -fx                       # (forward x up(0,1,0)), then normalised
    n = math.sqrt(rx * rx + rz * rz) or 1.0
    rx, rz = rx / n, rz / n
    ux, uy, uz = ry * fz - rz * fy, rz * fx - rx * fz, rx * fy - ry * fx
    dx, dy, dz = p[0] - eye[0], p[1] - eye[1], p[2] - eye[2]
    depth = dx * fx + dy * fy + dz * fz
    if depth <= 1.0:
        return None
    t = math.tan(math.radians(33.0) / 2.0)
    sx = (dx * rx + dy * ry + dz * rz) / (depth * t * 4.0 / 3.0)
    sy = (dx * ux + dy * uy + dz * uz) / (depth * t)
    return abs(sx), abs(sy)


def main():
    run = sys.argv[1]
    args = sys.argv[2:]
    ref = int(args[args.index("--ref") + 1]) if "--ref" in args else 2
    comp = [int(x) for x in args[args.index("--comp") + 1].split(",")] if "--comp" in args else [0, 1]
    t0 = float(args[args.index("--from") + 1]) if "--from" in args else 0.0
    t1 = float(args[args.index("--to") + 1]) if "--to" in args else 1e9
    rows = [r for r in rows_of(run) if num(r, "match_state") == 2 and num(r, "lj_a") != 0]
    rows = [r for r in rows if t0 <= num(r, "match_frames") / 30.0 <= t1]
    if not rows:
        print("no match rows with the camera probe")
        return
    men = comp + [ref]
    place = lambda r, k: (float(r["s%d_x" % k]), float(r["s%d_y" % k]) + 60.0, float(r["s%d_z" % k]))
    # the camera's z sense against the places: the one that puts the looked-at point nearest the men's middle
    best = None
    for zs in (1, -1):
        for xs in (1, -1):
            d = 0.0
            for r in rows:
                eye, at = cam_of(r)
                mx = sum(place(r, k)[0] for k in men) / len(men)
                mz = sum(place(r, k)[2] for k in men) / len(men)
                d += math.hypot(at[0] * xs - mx, at[2] * zs - mz)
            if best is None or d < best[0]:
                best = (d, xs, zs)
    _, xs, zs = best
    print("rows %d (match seconds %.0f..%.0f); camera sense x %+d z %+d" % (len(rows), num(rows[0], "match_frames") / 30.0,
          num(rows[-1], "match_frames") / 30.0, xs, zs))
    on = {k: 0 for k in men}
    off_all = 0
    ref_nearer = 0
    comp_on_both = 0
    spread, height, pitch, fill = [], [], [], []
    for r in rows:
        eye, at = cam_of(r)
        eye = (eye[0] * xs, eye[1], eye[2] * zs)
        at = (at[0] * xs, at[1], at[2] * zs)
        vis = {}
        for k in men:
            p = project(eye, at, place(r, k))
            vis[k] = p is not None and p[0] <= 0.9 and p[1] <= 0.9
            on[k] += vis[k]
        if all(vis[k] for k in comp):
            comp_on_both += 1
        if not any(vis[k] for k in men):
            off_all += 1
        cx = sum(place(r, k)[0] for k in comp) / len(comp)
        cz = sum(place(r, k)[2] for k in comp) / len(comp)
        rp = place(r, ref)
        if math.hypot(at[0] - rp[0], at[2] - rp[2]) < math.hypot(at[0] - cx, at[2] - cz):
            ref_nearer += 1
        spread.append(math.hypot(eye[0] - at[0], eye[2] - at[2]))
        height.append(eye[1] - at[1])
        pitch.append(math.degrees(math.atan2(eye[1] - at[1], spread[-1] or 1.0)))
        ps = [project(eye, at, place(r, k)) for k in comp]
        if all(p is not None for p in ps):
            fill.append(max(p[0] for p in ps))       # (how far across the picture the farther competitor is: 1 the edge)
    n = len(rows)
    for k in men:
        print("slot %d (%s): on screen %5.1f%%" % (k, "referee" if k == ref else "competitor", 100.0 * on[k] / n))
    print("both competitors on screen %5.1f%%; nobody on screen %5.1f%%; looked-at point nearer the referee than the"
          " competitors' middle %5.1f%%" % (100.0 * comp_on_both / n, 100.0 * off_all / n, 100.0 * ref_nearer / n))
    spread.sort()
    print("camera distance (eye to looked-at, across the floor): median %.0f, 90th %.0f, max %.0f" %
          (spread[n // 2], spread[(9 * n) // 10], spread[-1]))
    for name, v in (("eye height over the looked-at point", height), ("angle down (degrees)", pitch),
                    ("the farther competitor across the picture (0 middle, 1 edge)", fill)):
        v.sort()
        if v: print("%s: median %.2f, 10th %.2f, 90th %.2f" % (name, v[len(v) // 2], v[len(v) // 10], v[(9 * len(v)) // 10]))
    shots = {}
    for r in rows:
        d = num(r, "lj_d")
        k = (d & 0xFFFF, (d >> 16) & 0x3F)
        shots[k] = shots.get(k, 0) + 1
    print("camera mode / shot (low 6 bits): " + ", ".join("%d/0x%02X %d%%" % (m, s, 100 * c // n) for (m, s), c in sorted(shots.items())))


if __name__ == "__main__":
    main()
