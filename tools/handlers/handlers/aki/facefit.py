"""Per-wrestler face fixes, as data (2026-11-16, womenfix2 round 4).

The user, on Revenge's Elizabeth after the stance, hand and skin fixes: "Her face is still a bit scrunched up, too short ...
though I'm not sure if that isn't just the Revenge model" -- it is Revenge's model (womenfix2 work\face_round3.txt: No Mercy
draws her head at a uniform 0.97, as Revenge at 0.96; her face mesh is 20 high and 14 wide, 1.43, against No Mercy's Stephanie's
21 x 14, 1.50). Then: "Yeah let's make it slightly longer. It's ok if we improve things while staying faithful."

So a wrestler's fixes file (handlers\revenge\model_fixes.json) can ask for
    "face_lengthen": {"part": 5, "factor": 1.05, "pivot_y": 73}
-- his face mesh (Revenge's part 5) made `factor` times as long by bringing its LOWER face down: each vertex under pivot_y
(the eye line, model units) moves down by (factor - 1) x the face's height x (pivot_y - y) / (pivot_y - the chin's y),
rounded to the mesh's unit; the eyes, forehead and everything above stay. A vertex at the very place of a vertex of the neck
(part 4) or the hair (part 20) -- where the face joins them -- stays too, so nothing opens. The picture keeps its mapping (the
vertices keep their u, v: the lower face's few texels stretch over the extra unit).
"""
import json, os, struct

def _s8(v): return v - 256 if v > 127 else v

def _verts(m):
    sc = (m[0] & 0x7F) + 1; nv = m[1] & 0x7F
    off = [_s8(x) * 16 for x in m[4:7]]
    return sc, off, [tuple((_s8(m[8 + 8 * j + q]) + off[q]) * sc for q in range(3)) for j in range(nv)]

def face_lengthen(face, keep, factor, pivot_y):
    """the face mesh lengthened downward (see above); keep: positions that must not move -> (mesh, [(vertex, dy)])"""
    sc, off, V = _verts(face)
    y0, y1 = min(v[1] for v in V), max(v[1] for v in V)
    extra = (factor - 1.0) * (y1 - y0)
    d = bytearray(face); moved = []
    for j, v in enumerate(V):
        if v in keep or v[1] >= pivot_y: continue
        t = min(1.0, (pivot_y - v[1]) / float(pivot_y - y0))
        dy = -int(round(extra * t / sc)) * sc
        if not dy: continue
        b = _s8(d[8 + 8 * j + 1]) + dy // sc
        if not -128 <= b <= 127: raise ValueError("face_lengthen: vertex %d out of the mesh's range" % j)
        d[8 + 8 * j + 1] = b & 0xFF; moved.append((j, dy))
    return bytes(d), moved

HERE_REV = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "revenge")

def model_fixes(path=os.path.join(HERE_REV, "model_fixes.json")):
    return json.load(open(path, encoding="utf-8")).get("wrestlers", {}) if os.path.exists(path) else {}

def apply(descs, pool, report, key, fixes=None):
    """his model fixes (model_fixes.json) on each costume's descriptor -> descriptors"""
    fx = (fixes if fixes is not None else model_fixes()).get(key, {})
    fl = fx.get("face_lengthen")
    if not fl: return descs
    k = fl.get("part", 5)
    out = []
    for c, d in enumerate(descs):
        d = bytearray(d)
        m = struct.unpack_from(">H", d, 8 * k)[0]
        if not pool.ours(m): out.append(bytes(d)); continue
        keep = set()
        for o in fl.get("keep_parts", [4, 20]):
            n = struct.unpack_from(">H", d, 8 * o)[0]
            if pool.ours(n): keep |= set(_verts(pool.get(n))[2])
        new, moved = face_lengthen(pool.get(m), keep, fl["factor"], fl["pivot_y"])
        struct.pack_into(">H", d, 8 * k, pool.one(new))
        report.append("%s %d: face (part %d) lengthened x%.2f below y %d: %d vertices moved %s" % (key, c, k, fl["factor"], fl["pivot_y"], len(moved), moved))
        out.append(bytes(d))
    return out
