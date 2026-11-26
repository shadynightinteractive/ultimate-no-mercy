#!/usr/bin/env python3
"""The mod's variables its compiler shrank to one byte (modnative5, 2026-11-13: the user's rule (2) of 2026-11-13, "shrunk
booleans: private bytes LLVM shrank are filled only with 0/1, with a byte proxy on the native side").

LLVM's global optimiser shrinks an internal variable that is only ever stored its initial value or ONE other value to a
boolean (GlobalOpt, TryToShrinkGlobalToBoolean): the byte holds 0 for the initial value and 1 for the other, every store
becomes a store of 0 or 1, and every load becomes `byte ? OTHER : INITIAL`. mod.map then gives the variable 1 byte, where its
C type is 4 (`static int walls_done;`), so the native code's `int` cannot be the N64 object. The native side gets a byte
proxy (nm_native.h NShrunk<T, INITIAL, OTHER>) that reads and writes the byte exactly that way.

Which variables, and their two values, from the mod's own IR (gen_dwarf_ir.py):
  ir/<file>.ll        `@x = internal unnamed_addr global i1 false`: shrunk
  ir/<file>.front.ll  the front end's IR, before any optimisation: x's C type and initial value (`@x = internal global i32 0`)
                      and every value the C stores in it (`store i32 1, ptr @x`). Exactly one stored value other than the
                      initial one is OTHER; anything else (a stored value that isn't a constant there, or two) and the
                      variable is listed as not understood, and stays refused (its static_assert).
    python shrunk.py      -> shrunk.json: {name: {"type": "i32", "initial": 0, "other": 1, "file": base}} and "refused": {...}
"""
import glob, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
IR = os.path.join(HERE, 'ir')


def main():
    out, refused = {}, {}
    for path in sorted(glob.glob(os.path.join(IR, '*.ll'))):
        if path.endswith(('.before_dae.ll', '.front.ll')):
            continue
        base = os.path.basename(path)[:-3]
        names = re.findall(r'^@([\w.]+) = internal unnamed_addr global i1 (?:false|true)', open(path, encoding='utf8').read(), re.M)
        if not names:
            continue
        front = open(os.path.join(IR, base + '.front.ll'), encoding='utf8').read()
        for n in names:
            m = re.search(r'^@%s = internal (?:unnamed_addr )?global (i\d+) (-?\d+|true|false)' % re.escape(n), front, re.M)
            split = re.fullmatch(r'(.+)\.(\d+)', n)
            if not m and split and re.search(r'^@%s = internal (?:unnamed_addr )?global \[\d+ x i8\] zeroinitializer' % re.escape(split.group(1)), front, re.M):
                # (an element of a byte array the compiler split into one variable per element and then shrank: pad_coin_latch[2]
                #  is pad_coin_latch.0 and .1. Its initial value is 0 (zeroinitializer); the byte is read as the u8 itself, so
                #  0 and 1 are themselves -- were the other value anything but 1 the native code, which reads the byte as it
                #  is, would differ from the original and the test would say so: never hidden.)
                out[n] = {'type': 'i8', 'initial': 0, 'other': 1, 'file': base, 'split': True}
                continue
            if not m:
                refused[n] = 'its front-end declaration is not a plain integer variable'
                continue
            ty, init = m.group(1), m.group(2)
            init = {'true': 1, 'false': 0}.get(init, None) if init in ('true', 'false') else int(init)
            stored = set(re.findall(r'\bstore %s (-?\d+|%%[\w.]+), ptr @%s,' % (ty, re.escape(n)), front))
            other = {int(v) for v in stored if re.fullmatch(r'-?\d+', v)} - {init}
            odd = [v for v in stored if not re.fullmatch(r'-?\d+', v)]
            if odd or len(other) != 1:
                refused[n] = 'stores %s' % sorted(stored)
                continue
            if re.search(r'@%s\b(?!,| = )' % re.escape(n), re.sub(r'\b(?:store|load) [^\n]*', '', front)):
                # (its address is used otherwise than by a load or a store of it: not the simple case)
                refused[n] = 'used otherwise than by loads and stores'
                continue
            out[n] = {'type': ty, 'initial': init, 'other': other.pop(), 'file': base}
    json.dump({'shrunk': out, 'refused': refused}, open(os.path.join(HERE, 'shrunk.json'), 'w'), indent=1)
    print('shrunk: %d understood, %d not: %s' % (len(out), len(refused), refused))


if __name__ == '__main__':
    main()
