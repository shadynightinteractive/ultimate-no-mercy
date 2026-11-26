#!/usr/bin/env python3
"""mask_proof.py: the proof that the mod's masked ranges are DATA only (startup, 2026-11-19).

    python tools/aot/mask_proof.py [--build mod/build/mod_six] [--plan <a package>/firstrun/plan.bin]
                                   [--player <a player's nomercy_arcade.nrm> ...] [--out <report file>]

The exe's linked translation of the mod (tools/aot/aot_tool.cpp) carries, besides the hash of the mod's two files, a second
hash with the image's EXTRAS holes taken as zero (librecomp/aot_mod.h nm_aot_hash_masked): the tables a player's first
start fills from their own ROMs (exe src/main/arcade_firstrun.cpp apply_extras and the extras' flag), which differ from
player to player. The loader takes the linked translation for a mod whose masked hash matches. That is right only if those
ranges hold nothing the translation depends on. This checks it from the build itself, independently of aot_tool:

  A. the masks are exactly tools/firstrun/mkplan.py's extras holes (find_holes), and -- with --plan -- exactly the extras
     holes of the plan the package ships (the first start writes the extras ONLY there: apply_extras refuses anything else);
  B. from mod.elf: no mask touches an executable section (SHF_EXECINSTR), a function symbol (STT_FUNC), a jump table's
     section (.rodata.jt_* / .rodata of a code object's switch: every non-symbol byte of .rodata outside named objects is
     listed), or bytes outside a named data object (each mask lies inside one STT_OBJECT symbol, in a non-executable section);
  C. no relocation of an instruction (the code's HI16 / LO16 / 26 fields) lies in a mask -- only data words;
  D. with --player: each player's image differs from the build's ONLY inside the masks, the symbol file is the build's, and
     the masked hashes are equal (so the loader takes the linked translation for it).
aot_tool itself checks, at every build, that no function's bytes are in a mask and that the whole mod translated again with
every masked byte scrambled gives the same C (its own check 2). Exit 0 when every check passes.
"""
import argparse, os, struct, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'firstrun'))
import mkplan, modelf   # noqa: E402

SHF_EXECINSTR = 4
STT_OBJECT, STT_FUNC = 1, 2
# MIPS relocations of an instruction's field: R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_GPREL16, R_MIPS_GOT16, R_MIPS_CALL16
INSTRUCTION_RELOCS = {4, 5, 6, 7, 9, 11}


def fnv(h, data):
    for b in data: h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def masked_hash(syms, binary, masks):
    b = bytearray(binary)
    for o, n in masks: b[o:o + n] = bytes(n)
    return fnv(fnv(0xCBF29CE484222325, syms), b)


def plan_extras(path):
    """the extras holes [(offset, size)] of a plan.bin (exe arcade_firstrun.cpp read_plan's layout)"""
    b = open(path, 'rb').read()
    assert b[:8] == b'UNMPLAN1', 'not a plan'
    at = 8 + 12 + 4 * 32
    (np_,) = struct.unpack_from('<I', b, at); at += 4 + np_ * (32 + 32 + 8)
    (nh,) = struct.unpack_from('<I', b, at); at += 4
    out = []
    for i in range(nh):
        s, n, x = struct.unpack_from('<IIB', b, at); at += 12 + 32
        if x: out.append((s, n))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--build', default=os.path.join(ROOT, 'mod', 'build', 'mod_six'))
    ap.add_argument('--plan')
    ap.add_argument('--player', action='append', default=[])
    ap.add_argument('--out')
    a = ap.parse_args()
    lines, bad = [], []
    say = lines.append

    image, holes, relocs = mkplan.find_holes(a.build)
    masks = [(s, e - s) for s, e, cls, label in holes if cls == 'extras']
    labels = {s: label for s, e, cls, label in holes if cls == 'extras'}
    syms_bin = open(os.path.join(a.build, 'mod_syms.bin'), 'rb').read()
    say('build: %s (image %d bytes)' % (a.build, len(image)))
    say('masks: %d ranges, %d bytes (mkplan.find_holes extras holes)' % (len(masks), sum(n for o, n in masks)))

    # A
    if a.plan:
        pe = plan_extras(a.plan)
        if pe == masks: say('A. the plan\'s extras holes == the masks: SAME (%d)' % len(pe))
        else: bad.append('A. the plan\'s extras holes differ from the masks (%d vs %d)' % (len(pe), len(masks)))
    else:
        say('A. (no --plan given: the masks are find_holes\' own list)')

    # B
    elf, secs, syms = modelf.read_elf(os.path.join(a.build, 'mod.elf'))
    B = modelf.BASE
    exe_secs = [(s['addr'] - B, s['addr'] - B + s['size'], s['name']) for s in secs if s['flags'] & 2 and s['flags'] & SHF_EXECINSTR and s['size']]
    funcs = [(y['addr'] - B, y['addr'] - B + max(y['size'], 4), y['name']) for y in syms if y['alloc'] and y['type'] == STT_FUNC]
    objs = [(y['addr'] - B, y['addr'] - B + y['size'], y['name'], y['sec']) for y in syms if y['alloc'] and y['type'] == STT_OBJECT and y['size']]
    sec_of = lambda off: next((s['name'] for s in secs if s['flags'] & 2 and s['size'] and s['addr'] - B <= off < s['addr'] - B + s['size']), '?')
    n_code = n_func = n_outside = 0
    in_secs = {}
    for o, n in masks:
        e = o + n
        for f, t, name in exe_secs:
            if o < t and f < e: n_code += 1; bad.append('B. mask 0x%X+0x%X (%s) is in the executable section %s' % (o, n, labels[o], name))
        for f, t, name in funcs:
            if o < t and f < e: n_func += 1; bad.append('B. mask 0x%X+0x%X (%s) touches the function %s' % (o, n, labels[o], name))
        owner = [x for x in objs if x[0] <= o and e <= x[1]]
        if not owner: n_outside += 1; bad.append('B. mask 0x%X+0x%X (%s) is not inside one data object' % (o, n, labels[o]))
        sec = sec_of(o)
        if sec.startswith('.text') or 'jt' in sec: bad.append('B. mask 0x%X+0x%X (%s) in section %s' % (o, n, labels[o], sec))
        in_secs[sec] = in_secs.get(sec, 0) + n
    say('B. executable sections: %d (.text.*, .recomp_hook*, .recomp_patch ...); masks in one: %d' % (len(exe_secs), n_code))
    say('   function symbols: %d; masks touching one: %d' % (len(funcs), n_func))
    say('   masks not inside a single data object (STT_OBJECT): %d' % n_outside)
    say('   bytes masked by section: %s' % ', '.join('%s %d' % kv for kv in sorted(in_secs.items())))

    # C
    n_ins = n_data = 0
    for addr, typ in relocs.items():
        off = addr - B
        if any(o <= off < o + n for o, n in masks):
            if typ in INSTRUCTION_RELOCS: n_ins += 1; bad.append('C. an instruction relocation (type %d) at 0x%X is in a mask' % (typ, off))
            else: n_data += 1
    say('C. relocations in the masks: %d of data words (R_MIPS_32: the link\'s pointers, the loader relocates them), %d of instructions' % (n_data, n_ins))

    # D
    want = masked_hash(syms_bin, image, masks)
    say('D. the build\'s masked hash %016X (plain %016X)' % (want, fnv(fnv(0xCBF29CE484222325, syms_bin), image)))
    for p in a.player:
        with zipfile.ZipFile(p) as z:
            ps, pb = z.read('mod_syms.bin'), z.read('mod_binary.bin')
        diff = [i for i in range(min(len(pb), len(image))) if pb[i] != image[i]]
        outside = [i for i in diff if not any(o <= i < o + n for o, n in masks)]
        got = masked_hash(ps, pb, masks)
        ok = ps == syms_bin and len(pb) == len(image) and not outside and got == want
        say('   player %s: syms %s, size %s, %d bytes differ from the build (%d outside the masks), masked hash %016X: %s' % (
            p, 'SAME' if ps == syms_bin else 'DIFFERENT', 'SAME' if len(pb) == len(image) else 'DIFFERENT', len(diff), len(outside),
            got, 'MATCH' if ok else 'NO MATCH'))
        if not ok: bad.append('D. %s doesn\'t match' % p)

    say('RESULT: %s' % ('PASS' if not bad else 'FAIL (%d)' % len(bad)))
    text = '\n'.join(lines + bad) + '\n'
    sys.stdout.write(text)
    if a.out: open(a.out, 'w', encoding='utf-8').write(text)
    sys.exit(0 if not bad else 1)


if __name__ == '__main__':
    main()
