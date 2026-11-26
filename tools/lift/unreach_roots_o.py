# The builder of unreach_roots.json, the unreachable-line checker's other ways into each function (in tools/lift since 0.80.0,
# 2026-11-12, agent "final080"). It is agent "partial"'s roots_o.py: scratch\overlay_roots\build.py (11-11, approved and in use
# until then) with one change, "rule O", approved by the user 2026-11-12: the code's own ways in are taken per overlay too. A jump, branch, call or jump-table entry in
# function g, landing at address t inside another function n, does not count as a way into n when g is only in overlays n is not
# in, and t lies inside the memory range of each of g's overlays: when g's instruction runs, g's overlay is loaded, so the bytes
# at t are g's overlay's, not n's (two overlays at overlapping places can't both be in). Built addresses (lui/addiu) are NOT
# filtered: such an address can be kept and used later, after another overlay is loaded. Everything else is build.py's.
# Writes unreach_roots.json (what lift.py reads) and data_roots.json (the memory words' part, to look at) into the folder given,
# by default this one, after comparing with the unreach_roots.json here (the one in use). Not while a run uses the tools.
#   python -B unreach_roots_o.py [snapshots per scenario, default 3] [folder to write to]
# (build.py's heading follows.)
# 2026-11-11: the unreachable-line checker's "other ways in" (unreach_roots.json) with the memory words taken PER OVERLAY, as the user
# approved: a word in a running game's memory counts as a way into a function only in snapshots where that function's own overlay
# was loaded -- several snapshots per overlay, every way in from any of them counted. The code's own ways in (jumps, calls, built
# addresses, jump tables) are exactly as unreach_roots.py finds them. Writes unreach_roots_per_overlay.json here; doesn't touch
# tools\lift (a run is using it).
#   python -B build.py [snapshots per scenario, default 3]
import ctypes, collections, glob, json, os, re, sys
k32 = ctypes.windll.kernel32
k32.GetCurrentProcess.restype = ctypes.c_void_p
k32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
k32.SetPriorityClass(k32.GetCurrentProcess(), 0x4000)
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
D = os.path.abspath(os.path.join(HERE, '..', '..'))      # (the project: this folder's grandparent, as lift.py finds it)
sys.path.insert(0, HERE)
import to_asm, unreach_roots
OUT = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else HERE
ROM = open(os.path.join(os.path.dirname(D), 'wwf.nomercy.us.z64'), 'rb').read()
INL = D + r'\exe\WWFNoMercyRecomp\RecompiledFuncs\recomp_overlays.inl'
PER = int(sys.argv[1]) if len(sys.argv) > 1 else 3

# The sections: ROM place, memory place, size, and which functions each lists.
text = open(INL, encoding='utf8').read()
sections = {}
for m in re.finditer(r'\{ \.rom_addr = (0x[0-9A-F]+), \.ram_addr = (0x[0-9A-F]+), \.size = (0x[0-9A-F]+), \.funcs = section_\d+_(\w+)_funcs', text):
    sections[m.group(4)] = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16))
section_of = collections.defaultdict(set)
cur = None
for line in text.split('\n'):
    m = re.match(r'static FuncEntry section_\d+_(\w+)_funcs\[\]', line)
    if m: cur = m.group(1); continue
    m = re.search(r'\.func = (\w+),', line)
    if m and cur: section_of[m.group(1)].add(cur)
SWAPPED = {s for s in sections if s.startswith('ovl_')}      # (main, entry and rwblob are always there)
print('sections', {k: (hex(v[1]), hex(v[2])) for k, v in sections.items()})

def loaded(snap, name):
    """Whether section `name`'s own code is in this snapshot: 256 words spread over it, compared with the ROM (big-endian there,
    little-endian words in the snapshot). Zero words on both sides don't count either way."""
    rom, ram, size = sections[name]
    same = differ = 0
    for i in range(256):
        off = (size // 4 * i // 256) * 4
        a = ram + off - 0x80000000
        if a + 4 > len(snap): continue
        r = int.from_bytes(ROM[rom + off:rom + off + 4], 'big')
        s = int.from_bytes(snap[a:a + 4], 'little')
        if r == 0 and s == 0: continue
        same += r == s; differ += r != s
    return same + differ > 32 and same >= 0.9 * (same + differ)

# The snapshots: the latest PER of each scenario.
runs = collections.defaultdict(list)
for p in sorted(glob.glob(D + r'\qa\runs\*\state.rdram.bin')):
    if os.path.basename(os.path.dirname(p)) > '2026-11-11_090751': continue
    runs[re.sub(r'^\d{4}-\d\d-\d\d_\d{6}_', '', os.path.basename(os.path.dirname(p)))].append(p)
chosen = [p for ps in runs.values() for p in ps[-PER:]]
print('snapshots chosen', len(chosen), 'from', len(runs), 'scenarios')

fs = {n: f for n, f in to_asm.read_functions().items() if not n.startswith('modrep_')}
by_addr = collections.defaultdict(set)
for n, f in fs.items():
    for a in f['ins']:
        by_addr[a].add(n)
starts = {min(f['ins']) for f in fs.values() if f['ins']}
lo = min(by_addr); hi = max(by_addr)

def sections_of(n):
    return section_of.get(n) or section_of.get(re.sub(r'_ovl_[a-e]$', '', n)) or set()

# The code's own ways in (no memory words): as unreach_roots.py, except a jump / branch / call / table entry from another overlay
# into that overlay's own range (rule O, above).
def swapped_ranges_of(g):
    """The memory ranges of g's overlays, or None if g is in a section that is always loaded (or unknown): then nothing is dropped."""
    secs = sections_of(g)
    if not secs or not secs <= SWAPPED:
        return None
    return [(sections[s][1], sections[s][1] + sections[s][2]) for s in secs]

def entries_per_overlay(fs):
    import unreach
    from mips import BRANCH
    pieces_of = {}
    for n, f in fs.items():
        for p in f.get('pieces', [n]):
            pieces_of.setdefault(p, set()).add(n)
    out = collections.defaultdict(set)
    dropped = collections.Counter()
    def enter(a, source, kind):
        if a in starts:
            return
        own = kind in ('jump', 'table') and source in fs and a in fs[source]['ins']
        rng = swapped_ranges_of(source) if kind in ('jump', 'table', 'call') else None
        for n in by_addr.get(a, ()):
            if n == source:
                continue
            if own and not fs[n]['ins'].keys() <= fs[source]['ins'].keys():
                continue
            if own and n in pieces_of.get(source, ()) and source in pieces_of.get(n, ()):
                continue
            if kind in ('jump', 'table') and not own and source in fs[n].get('pieces', []):
                continue
            if rng is not None and sections_of(n) and not (sections_of(source) & sections_of(n)) and all(lo_ <= a < hi_ for lo_, hi_ in rng):
                dropped[kind] += 1
                continue        # (rule O: g's overlay is loaded over t whenever g runs, so t isn't n's code then)
            out[n].add(a)
    for g, f in fs.items():
        lui = {}
        for a in sorted(f['ins']):
            mn, ops = f['ins'][a]
            if mn in BRANCH or mn in ('j', 'b'):
                t = unreach.target(ops)
                if t is not None:
                    enter(t, g, 'jump')
            elif mn == 'jal':
                t = unreach.target(ops)
                if t is not None:
                    enter(t, g, 'call')
            r = unreach.REG.findall(ops)
            if mn == 'lui' and r:
                m = re.search(r',\s*(0[xX][0-9A-Fa-f]+|\d+)$', ops)
                if m:
                    lui[r[0]] = int(m.group(1), 0) << 16
            elif mn in ('addiu', 'ori') and len(r) == 2 and r[1] in lui:
                m = re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ops)
                if m:
                    k = int(m.group(1), 0)
                    v = (lui[r[1]] + unreach.sext16(k)) if mn == 'addiu' else (lui[r[1]] | (k & 0xFFFF))
                    enter(v & 0xFFFFFFFF, g, 'built')
        for table in f['tables'].values():
            for t in table.values():
                enter(t, g, 'table')
    print('rule O dropped (per function a way in leads to):', dict(dropped))
    return out

roots = entries_per_overlay(fs)
code_only = {n: set(v) for n, v in roots.items()}
old_code = unreach_roots.entries(fs, set())
print('code ways in: before %d, per overlay %d' % (sum(len(v) for v in old_code.values()), sum(len(v) for v in roots.values())))
data_roots = collections.defaultdict(set)
per_section_snaps = collections.Counter()
# First pass: which overlays each snapshot has. An overlay seen in fewer than MIN_SNAPS of them (the editor's, ovl_d: no game test
# opens the editor) keeps the old rule -- every word of every snapshot counts as a way into its functions -- so nothing is ever
# judged against fewer ways in than before for want of a snapshot of it.
MIN_SNAPS = 3
have_of = {}
for p in chosen:
    snap = open(p, 'rb').read()
    have_of[p] = {s for s in SWAPPED if loaded(snap, s)}
    for s in have_of[p]: per_section_snaps[s] += 1
too_few = {s for s in SWAPPED if per_section_snaps[s] < MIN_SNAPS}
print('overlays kept on the old rule (fewer than %d snapshots): %s' % (MIN_SNAPS, sorted(too_few)))
for k, p in enumerate(chosen):
    snap = open(p, 'rb').read()
    have = have_of[p] | too_few
    w = np.frombuffer(snap[:len(snap) & ~3], dtype='<u4')
    w = w[(w >= lo) & (w <= hi) & ((w & 3) == 0)]
    for v in np.unique(w).tolist():
        if v in starts: continue
        for n in by_addr.get(v, ()):
            secs = sections_of(n)
            if not secs or not (secs & SWAPPED) or (secs & have):     # (an always-loaded function, or its overlay is in)
                data_roots[n].add(v)
    if k % 20 == 0: print('  %d/%d %s loaded: %s' % (k + 1, len(chosen), os.path.basename(os.path.dirname(p)), sorted(have)), flush=True)
print('snapshots with each overlay loaded:', dict(per_section_snaps))
for n, v in data_roots.items():
    roots[n] = set(roots.get(n, set())) | v
old = json.load(open(os.path.join(HERE, 'unreach_roots.json')))['roots']
json.dump({n: sorted(v) for n, v in data_roots.items()}, open(os.path.join(OUT, 'data_roots.json'), 'w'), indent=0)
again = {n: set(old_code.get(n, ())) | set(data_roots.get(n, ())) for n in set(old_code) | set(data_roots)}
same = {n: sorted(v) for n, v in again.items() if v} == {n: sorted(v) for n, v in old.items() if v}
print('without rule O this gives the roots in use exactly:', same)
print('functions with other ways in: old %d, new %d' % (len(old), len(roots)))
print('ways in: old %d, new %d' % (sum(len(v) for v in old.values()), sum(len(v) for v in roots.values())))
json.dump({'snapshot': 'per overlay: %d snapshots (latest %d of each of %d scenarios), %s' % (len(chosen), PER, len(runs), dict(per_section_snaps)),
           'roots': {n: sorted(v) for n, v in sorted(roots.items())}}, open(os.path.join(OUT, 'unreach_roots.json'), 'w', newline='\n'), indent=0)
print('written')
