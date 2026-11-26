#!/usr/bin/env python3
"""ports_check.py (phase3 p3ports, 2026-11-19): the built-in ports' tables and the mod's extras made natively (ri_ports, ri_photos,
ri_content, ri_wmport, ri_build's modextra.bin maker) against the Python chain's outputs, byte for byte.

    python ports_check.py --ri <ri_cli.exe> --private <private> --game <scratch game folder> --out <scratch out>
<game> is laid out as the package has firstrun\\ (firstrun\\romimport\\ our data, firstrun\\hdrfill.bin of the build checked against).

1. ri_cli ports (revanims, mkroster, mkparts, movelib, mkovr) -> rev_anims.json, movex_data.h (romonly data\\gen_data),
   roster_ext.h, parts_ext.h, parts_ovr.h, move_lib.h (generated mod\\src), parts.pkl by value (romonly mod_build).
2. ri_cli modextra -> every header text the extras holes read, each against its reference: the latest NEW_REFS overlay
   (reference\\romrender: rev_photos.h, jarrett_photo.h, meanie_photo.h, wmport_files.h), content_index.h against the no-music
   Python run (wt\\p3ref\\mod\\src), the rest generated\\mod\\src.
3. modextra.bin against the build's (<plan>\\modextra.bin, --build-modextra) with the build's own versions of the headers whose
   ROM-only text differs on purpose (--over: decision A's photos / wmport_files.h, the developer's content_index.h with his
   songs): every hole SAME but arcade_extras_in (the source bits). (no2k15, 2026-11-23: hd_index*.h, the WWE 2K15 test
   slots' -- no ROM gave them -- are gone with the slots.)"""
import argparse, json, os, pickle, shutil, subprocess, sys
sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import modextra_cmp as MC


def same(a, b):
    try: return open(a, 'rb').read() == open(b, 'rb').read()
    except OSError as e: return 'missing: %s' % e


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--ri', required=True); ap.add_argument('--private', required=True)
    ap.add_argument('--game', required=True); ap.add_argument('--out', required=True)
    ap.add_argument('--build-modextra', help="the build's modextra.bin (default: <private>/scratch/p3hdrfill/out/modextra.bin)")
    a = ap.parse_args()
    P, out = a.private, a.out
    roms = os.path.join(P, 'roms')
    ri_data = os.path.join(os.path.dirname(HERE))
    os.makedirs(out, exist_ok=True)
    bad = 0
    def say(name, r):
        nonlocal bad
        ok = r is True
        bad += not ok
        print('%-26s %s' % (name, 'SAME' if ok else ('DIFFERENT' if r is False else r)))
    # 1. the ports' steps
    for f in ('models.txt',):
        if os.path.exists(os.path.join(out, f)): os.remove(os.path.join(out, f))
    subprocess.run([a.ri, 'ports', roms, ri_data, out, 'revanims', 'mkroster', 'mkparts', 'movelib', 'mkovr'], check=True)
    gd, gen = os.path.join(P, 'romonly', 'data', 'gen_data'), os.path.join(P, 'generated', 'mod', 'src')
    say('rev_anims.json', same(os.path.join(out, 'rev_anims.json'), os.path.join(gd, 'rev_anims.json')))
    say('movex_data.h', same(os.path.join(out, 'movex_data.h'), os.path.join(gd, 'movex_data.h')))
    for h in ('roster_ext.h', 'parts_ext.h', 'parts_ovr.h', 'move_lib.h'):
        say(h, same(os.path.join(out, h), os.path.join(gen, h)))
    j = json.load(open(os.path.join(out, 'parts_pkl.json')))
    mine = dict(keys=[bytes.fromhex(x) for x in j['keys']], uns=[bytes.fromhex(x) for x in j['uns']],
                nm_uns=[bytes.fromhex(x) for x in j['nm_uns']], files={int(k): v for k, v in j['files'].items()})
    say('parts.pkl (by value)', mine == pickle.load(open(os.path.join(P, 'romonly', 'mod_build', 'parts.pkl'), 'rb')))
    # 2. the extras' header texts
    mx = os.path.join(out, 'modextra'); shutil.rmtree(mx, ignore_errors=True)
    subprocess.run([a.ri, 'modextra', roms, a.game, mx], check=False)
    rr = os.path.join(P, 'reference', 'romrender', 'generated', 'mod', 'src')
    for h in sorted(os.listdir(os.path.join(mx, 'src'))):
        if h == 'hands.c.wm_pose': continue
        ref = os.path.join(rr, h) if os.path.exists(os.path.join(rr, h)) else \
              os.path.join(P, 'wt', 'p3ref', 'mod', 'src', h) if h == 'content_index.h' else os.path.join(gen, h)
        say('src\\' + h, same(os.path.join(mx, 'src', h), ref))
    # 3. modextra.bin against the build's, with the build's own text of the five headers that differ on purpose
    over = os.path.join(out, 'over_build'); shutil.rmtree(over, ignore_errors=True); os.makedirs(over)
    for h in ('wmport_files.h', 'rev_photos.h', 'jarrett_photo.h', 'meanie_photo.h', 'content_index.h'):
        shutil.copy(os.path.join(gen, h), over)
    mxb = os.path.join(out, 'modextra_build'); shutil.rmtree(mxb, ignore_errors=True)
    subprocess.run([a.ri, 'modextra', roms, a.game, mxb, '--over', over], check=True)
    want = a.build_modextra or os.path.join(P, 'scratch', 'p3hdrfill', 'out', 'modextra.bin')
    holes = MC.H.read_holes(os.path.join(a.game, 'firstrun', 'hdrfill.bin'))
    A, B = MC.H.read_modextra(os.path.join(mxb, 'modextra.bin')), MC.H.read_modextra(want)
    diff = [(sym, header) for (s, size, flags, header, sym), (_, x), (_, y) in zip(holes, A, B) if x != y]
    expected = [d for d in diff if d[0] == 'arcade_extras_in']
    print('modextra.bin holes: %d, different %d (expected: %s)' % (len(holes), len(diff), ', '.join(s for s, _ in expected)))
    say('modextra.bin (build headers)', len(diff) == len(expected))
    print('ALL SAME' if not bad else '%d DIFFERENT' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
