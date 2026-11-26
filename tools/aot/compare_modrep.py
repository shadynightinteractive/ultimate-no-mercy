# Compares the ahead-of-time translation's function bodies with tools/lift/modrep's translation of the same mod build
# (the same front end and C generator, run by another tool): every line that isn't a call or a hook must be identical.
import re, glob, sys
def funcs(paths):
    out = {}
    for p in paths:
        cur = None
        for line in open(p, encoding='utf8', errors='replace'):
            m = re.match(r'RECOMP_FUNC void (\w+)\(', line)
            if m:
                cur = []; out[m.group(1)] = cur; continue
            if cur is not None:
                cur.append(line.rstrip())
    return out
def body(lines):
    keep = []
    for l in lines:
        if '(rdram, ctx)' in l or 'nm_aot_run_hook' in l: continue
        if l.strip() in ('}', 'return;', '') or l.startswith('// The mod'): continue
        keep.append(l.strip())
    return keep
# (Run from the project's folder -- exe and tools/lift -- or from Stage 1's work folder: exe_src and tools_lift. 2026-11-12)
import os
PORT, LIFT = ('exe', 'tools/lift') if os.path.isdir('exe/WWFNoMercyRecomp') else ('exe_src', 'tools_lift')
aot = funcs(sorted(glob.glob(PORT + '/WWFNoMercyRecomp/src/game/mod_aot/mod_aot_[0-9]*.c')))
ref = funcs([LIFT + '/modrep/out/modrep_modfuncs.c', LIFT + '/modrep/out/modrep_originals.c'])
by_first = {}
for n, l in ref.items():
    first = next((x for x in l if x.strip().startswith('// 0x')), None)
    if first: by_first[first.strip()[:13]] = n
same = diff = missing = 0
for n, l in aot.items():
    key = '// 0x' + n[len('nm_aot_'):]
    r = by_first.get(key)
    if r is None: missing += 1; continue
    if body(l) == body(ref[r]): same += 1
    else:
        diff += 1
        if diff <= 3: print('DIFF', n, r)
print('same', same, 'different', diff, 'not in modrep out', missing, 'of', len(aot))
