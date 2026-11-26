#!/usr/bin/env python3
"""wmmedia_check.py (phase3 p3wmmedia, 2026-11-19): the developer's check of ri_wmmedia (the WrestleMania 2000 men's themes
and Titantrons made natively) against the Python reference's folders.

    ri_cli wmmedia <roms> <load maps> <wm2000 fixes folder> <out>
    python wmmedia_check.py <out> <reference folders> [<overlay folders>]

<out> holds what ri_cli wrote per man (theme.bin, theme.meta, video.bin, source_theme.json, source_video.json); <reference
folders> the reference's "WrestleMania 2000" folder of wrestler folders, <overlay folders> the same folder of the
reference overlay (a file there wins over the reference's own: wrestler.json). Compared, per man:
  theme.bin / theme.meta / video.bin   byte for byte (both missing counts as the same: the Hos has no theme, some no video)
  source_theme / source_video          the dict (or "none" text) against wrestler.json's "source"."theme" / "source"."video"
Prints SAME / DIFFERENT counts per kind and the men that differ; exit status 1 if anything differs.
"""
import json, os, sys

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    out, ref = sys.argv[1], sys.argv[2]
    ovl = sys.argv[3] if len(sys.argv) > 3 else None
    kinds = ["theme.bin", "theme.meta", "video.bin", "source_theme", "source_video"]
    same = {k: 0 for k in kinds}; diff = {k: [] for k in kinds}
    men = sorted(d for d in os.listdir(out) if os.path.isdir(os.path.join(out, d)))
    for sid in men:
        def ref_file(name):
            if ovl and os.path.exists(os.path.join(ovl, sid, name)): return os.path.join(ovl, sid, name)
            return os.path.join(ref, sid, name)
        for k in kinds[:3]:
            a, b = os.path.join(out, sid, k), ref_file(k)
            ea, eb = os.path.exists(a), os.path.exists(b)
            ok = (ea == eb) and (not ea or open(a, "rb").read() == open(b, "rb").read())
            (same.__setitem__(k, same[k] + 1) if ok else diff[k].append(sid))
        w = json.load(open(ref_file("wrestler.json"), encoding="utf-8"))
        for k, key in (("source_theme", "theme"), ("source_video", "video")):
            mine = json.load(open(os.path.join(out, sid, k + ".json"), encoding="utf-8"))
            ok = json.dumps(mine) == json.dumps(w["source"].get(key))     # (as text: floats, bools and key order too)
            (same.__setitem__(k, same[k] + 1) if ok else diff[k].append(sid))
    bad = False
    for k in kinds:
        print("%-13s SAME %d  DIFFERENT %d%s" % (k, same[k], len(diff[k]), ("  " + ", ".join(diff[k])) if diff[k] else ""))
        bad |= bool(diff[k])
    print("%d men checked" % len(men))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
