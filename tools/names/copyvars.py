# (names5, 2026-11-13) Rule C: an m2c temp written once with the word (half, byte) at offset OFF of one place --
# `t = M2C_FIELD(src, P<T> , OFF);` or `t = lift_unaligned_load32(src, OFF);` -- and read once, by the store of the same offset of
# another place -- `M2C_FIELD(dst, P<T> , OFF) = t;` or `lift_unaligned_store32(dst, OFF, t);` -- only carries that word across
# (a block copy the compiler unrolled, a record copied field by field): it is named copied_<off> (copied_0, copied_4, copied_c,
# lower-case hex; _2, _3 in order of the code). The ABI's `lift_out_<reg> = t;` mirrors don't count as reads. Names that clash
# with any identifier of the function are skipped. Works on the release's raw C++ (the offsets are the same in the fielded one).
# Writes copy_edits.json for put.py.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re, collections
names = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
merged = dict(names); merged.update(ch)
cands = json.load(open(CANDS))
NUM = r'(0x[0-9A-F]+|\d+)'
WR = re.compile(r'^\s*(temp_\w+) = (?:\([\w<> ]+\) )?(?:M2C_FIELD\((.+), P<[\w<> ]+> , %s\)|lift_unaligned_load32\((.+), %s\));$' % (NUM, NUM))
ST1 = r'^\s*M2C_FIELD\((.+), P<[\w<> ]+> , %s\) = (?:\([\w<> ]+\) )?%s;$'
ST2 = r'^\s*lift_unaligned_store32\((.+), %s, %s\);$'
out = {}; stats = collections.Counter()
for f in sorted(merged):
    if f not in cands or 'cpp' not in cands[f] or f.startswith('modrep_'): continue
    info = merged[f]; raw = re.sub(r'//.*', '', cands[f]['cpp'])
    ids = set(re.findall(r'\b[A-Za-z_]\w*\b', raw))
    vars_ = dict(info.get('vars', {}))
    used = set(vars_.values()) | {info['name']}
    lines = raw.split('\n')
    wanted = []
    for i, l in enumerate(lines):
        m = WR.match(l)
        if not m: continue
        t = m.group(1)
        if t in vars_: continue
        src = m.group(2) or m.group(4); off = int(m.group(3) or m.group(5), 0)
        occ = [j for j, y in enumerate(lines) if re.search(r'(?<![\w.>])%s\b' % re.escape(t), y)]
        decl = [j for j in occ if re.match(r'\s*(?:const )?[\w<>* ]+? %s;$' % re.escape(t), lines[j])]
        mirror = [j for j in occ if re.match(r'\s*lift_out_\w+ = (?:\([\w<> ]+\) )?%s;$' % re.escape(t), lines[j])]
        rest = [j for j in occ if j not in decl and j not in mirror and j != i]
        if len(rest) != 1 or lines[i].count(t) != 1: stats['not_once'] += 1; continue
        r = lines[rest[0]]
        mm = re.match(ST1 % (NUM, re.escape(t)), r)
        if mm: dst, doff = mm.group(1), int(mm.group(2), 0)
        else:
            mm = re.match(ST2 % (NUM, re.escape(t)), r)
            if mm: dst, doff = mm.group(1), int(mm.group(2), 0)
            else:
                # (a store into a stack slot or through a pointer: the word kept on the stack, the offset its source's)
                mm = re.match(r'^\s*(sp[0-9A-F]+|\*\w+) = (?:\([\w<> ]+\) )?%s;$' % re.escape(t), r)
                if not mm: stats['other_read'] += 1; continue
                dst, doff = mm.group(1), off
        if doff != off or dst.strip() == src.strip(): stats['not_copy'] += 1; continue
        wanted.append((t, 'copied_%x' % off, i))
    seen = collections.Counter()
    for t, cand, _ in sorted(wanted, key=lambda x: (x[1], x[2])):
        seen[cand] += 1
        nm = cand if seen[cand] == 1 else '%s_%d' % (cand, seen[cand])
        if nm in ids or nm in used:
            stats['skipped'] += 1; continue
        used.add(nm); out.setdefault(f, {})[t] = nm; stats['named'] += 1
json.dump({f: {'vars+': v} for f, v in out.items()}, open(B + 'tools/copy_edits.json', 'w'), indent=0)
print(stats, len(out))
