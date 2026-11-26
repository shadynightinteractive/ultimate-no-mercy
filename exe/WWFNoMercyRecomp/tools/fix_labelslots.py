#!/usr/bin/env python3
"""Post-process RecompiledFuncs: put every label back before its own instruction (agent "labels", 2026-11-13).

THE BUG CLASS: N64Recomp's second pass (src/recompilation.cpp) writes the function's labels from a sorted set with
    if (cur_label != branch_labels.end() && vram >= *cur_label) { emit_label(*cur_label); ++cur_label; }
-- AT MOST ONE label per instruction. A label below the function's first instruction (an overlapping mid-entry's branch or `j`
back into the function it was carved out of, or a `j` tail call to an earlier address) is due at the first instruction and takes
its turn; every later label that is due waits one more instruction, until an instruction has none waiting. A goto to a label so
pushed lands one or more instructions past its target, and the instructions in between never run on that path.

fix_selfentry.py (session 5) repaired the one case seen then, a function looping back to its own first instruction
(func_800032A4: the exploded-wrestler render bug). This does the same for every label: any label whose address is inside the
function and is not right before its own instruction is moved to just before that instruction's own step of the translation
(the LAST `    // 0x<label>:` comment line of the function: a delay-slot instruction's comment is also written, earlier, inside
the branch before it, and a goto to it must run it from its own step). The labels below the function's start stay where
they are: fix_backbranches turns the gotos to them into tail calls, and the rest (j targets) have no goto at all.

The one label this moves in the current translation: func_801209EC's L_801209F4 (the loop back-edge of the strike hit test's
body-part loop, bne at 0x80120A64), which the C had after 0x801209F4 `sll $a1, $s0, 1`.

Run after fix_backbranches.py and fix_selfentry.py, every regen. Idempotent.
    python tools/fix_labelslots.py [--root <recomp repo>] [--audit] [--out <folder>]
--audit reports without writing; --out writes the changed files to that folder instead of in place.
"""
import argparse, glob, os, re, sys

ap = argparse.ArgumentParser()
ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ap.add_argument('--audit', action='store_true', help='report only, do not modify files')
ap.add_argument('--out', default=None, help='write changed files here instead of in place')
args = ap.parse_args()

func_re = re.compile(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{.*?^;\}', re.S | re.M)
ins_re = re.compile(r'^\s*// 0x([0-9A-F]{8}):', re.M)
label_re = re.compile(r'^L_([0-9A-F]{8}):\r?\n', re.M)


def fix_body(name, body):
    """The function with each misplaced in-span label moved; and the labels moved."""
    addrs = [int(a, 16) for a in ins_re.findall(body)]
    if not addrs:
        return body, []
    start, end = min(addrs), max(addrs) + 4
    moved = []
    for lm in list(label_re.finditer(body)):
        label = int(lm.group(1), 16)
        if not (start <= label < end):
            continue        # below or above the function: not a place in it (see the docstring)
        nxt = ins_re.search(body, lm.end())
        if nxt and int(nxt.group(1), 16) == label:
            continue        # already right before its own instruction
        moved.append(label)
    for label in moved:
        lm = label_re.search(body, 0)
        while int(lm.group(1), 16) != label:
            lm = label_re.search(body, lm.end())
        line = lm.group(0)
        body = body[:lm.start()] + body[lm.end():]
        own = [m.start() for m in re.finditer(r'^    // 0x%08X:' % label, body, re.M)]
        if not own:
            raise SystemExit('fix_labelslots: %s has no step for L_%08X' % (name, label))
        body = body[:own[-1]] + line + body[own[-1]:]
    return body, moved


total = 0
for path in sorted(glob.glob(os.path.join(args.root, 'RecompiledFuncs', 'funcs_*.c'))):
    src = open(path, 'rb').read().decode('utf-8', errors='replace')
    parts, pos = [], 0
    for m in func_re.finditer(src):
        body, moved = fix_body(m.group(1), m.group(0))
        if moved:
            parts.append(src[pos:m.start()]); parts.append(body); pos = m.end()
            total += len(moved)
            print('%s %s: moved %s' % (os.path.basename(path), m.group(1), ', '.join('L_%08X' % a for a in moved)))
    if parts and not args.audit:
        parts.append(src[pos:])
        dest = os.path.join(args.out, os.path.basename(path)) if args.out else path
        open(dest, 'wb').write(''.join(parts).encode('utf-8'))
print(total, 'labels moved to their own instruction')
