import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Abstract interpretation of the game's MIPS (from RecompiledFuncs comments): which registers hold a
# wrestler slot index, slot*k, per-slot array element addresses, wrestler struct pointers.
# Output: per-slot array accesses, slot-bounded loops, slot bit masks, slot compares.
import pickle, re, glob, collections, sys

SRC = nmp("/home/devbox/src/WWFNoMercyRecomp/RecompiledFuncs")
secs, fn2sec, insns = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
TB = 0x80159990          # wrestler array (true base)
WS = 0x358
SECS = ('main_1050', 'ovl_e', 'entry')

# ---- switch tables from the C ----
def load_switches():
    sw = {}
    rxa = re.compile(r"^\s*// 0x([0-9A-F]{8}): jr ")
    rxc = re.compile(r"case \d+: goto L_([0-9A-F]{8});")
    for f in sorted(glob.glob(SRC + "/funcs_*.c")):
        last = None
        for line in open(f):
            m = rxa.match(line)
            if m: last = int(m.group(1), 16); continue
            if last is not None and 'switch (' in line:
                sw.setdefault(last, [])
                continue
            if last is not None:
                for m in rxc.finditer(line): sw.setdefault(last, []).append(int(m.group(1), 16))
                if 'default:' in line: last = None
    return sw
SW = load_switches()

funcs = collections.defaultdict(list)
for s, f, a, asm in insns:
    if s in SECS: funcs[f].append((a, asm))
for f in funcs: funcs[f].sort()
func_at = {}
for f, l in funcs.items():
    if l: func_at[l[0][0]] = f

ROM = open(nmp('/home/devbox/src/WWFNoMercyRecomp/nomercy.z64'), 'rb').read()
def rom32(v, sec='ovl_e'):
    e = secs[sec]; o = e['rom'] + (v - e['vram'])
    return int.from_bytes(ROM[o:o + 4], 'big')
def table_funcs(base, sec='ovl_e'):
    out = []
    for i in range(256):
        w = rom32(base + 4 * i, sec)
        if w in func_at: out.append(func_at[w])
        else: break
    return out
INDIRECT = {0x800E5D88: table_funcs(0x80144CF0), 0x800F0CE8: table_funcs(0x80145A10)}

def parse(asm):
    p = asm.split(None, 1); op = p[0]
    args = [x.strip() for x in p[1].split(',')] if len(p) > 1 else []
    return op, args
def ival(s):
    return int(s, 0)

BR1 = {'beq', 'bne', 'beql', 'bnel'}
BR0 = {'bgez', 'bltz', 'blez', 'bgtz', 'bgezl', 'bltzl', 'blezl', 'bgtzl', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl', 'b'}
LIKELY = {'beql', 'bnel', 'bgezl', 'bltzl', 'blezl', 'bgtzl', 'bc1tl', 'bc1fl'}
MEM = {'lb': 1, 'lbu': 1, 'lh': 2, 'lhu': 2, 'lw': 4, 'lwu': 4, 'ld': 8, 'sb': 1, 'sh': 2, 'sw': 4, 'sd': 8,
       'lwc1': 4, 'swc1': 4, 'ldc1': 8, 'sdc1': 8, 'lwl': 4, 'lwr': 4, 'swl': 4, 'swr': 4}
CALLEE_SAVED = {'$s0', '$s1', '$s2', '$s3', '$s4', '$s5', '$s6', '$s7', '$fp', '$sp', '$gp'}

# abstract values (tuples):
# ('C', c)  constant          ('S',)  slot index (conf)    ('N',) small counter (0..5)
# ('M', k)  slot*k            ('P', base, k)  base + slot*k  (address or offset)
# ('W', off) pointer to some wrestler struct + off           ('B',) 1<<slot
TOP = None
def join(a, b):
    if a == b: return a
    if a is None or b is None: return None
    ta, tb = a[0], b[0]
    if ta == 'C' and tb == 'C':
        return None
    sl = ('S', 'N')
    if ta in sl and tb in sl: return ('S',) if 'S' in (ta, tb) else ('N',)
    if ta in sl and tb == 'C' and 0 <= b[1] <= 6: return a
    if tb in sl and ta == 'C' and 0 <= a[1] <= 6: return b
    if ta == 'W' and tb == 'W' and a[1] == b[1]: return a
    return None

class Facts:
    def __init__(self):
        self.slot_fields_W = set()     # wrestler-struct offsets that hold slot numbers
        self.ptr_fields_W = set()      # wrestler-struct offsets that hold wrestler pointers
        self.slot_globals = set()      # absolute addresses holding a slot number
        self.ptr_globals = set()       # absolute addresses holding a wrestler pointer
        self.slot_psa = set()          # (base,k) per-slot arrays whose elements hold slot numbers
        self.arg_slot = collections.defaultdict(set)  # func -> {argreg}
        self.arg_wptr = collections.defaultdict(set)
        self.ret_slot = set(); self.ret_wptr = set()
        self.arg_const = collections.defaultdict(dict)
F = Facts()
# seeds (from earlier study): wrestler +0x00,+0x08 (targets), current-slot global 0x80159080
F.slot_fields_W |= {0x00, 0x08}
F.slot_globals |= {0x80159080}
F.ptr_globals |= {0x801590AC, 0x801590B0, 0x801590B4, 0x801590B8, 0x801590BC, 0x8016B92C}

class Out:
    def __init__(self): self.reset()
    def reset(self):
        self.psa = collections.defaultdict(set)      # (base,k) -> {(pc,op)}
        self.loops = set()                          # (pc, func, imm)
        self.bits = set()                           # (pc, func)
        self.cmps = set()                           # (pc, func, imm)
        self.wacc = collections.Counter()
O = Out()

COUNTERS = {}
def counters(fname):
    if fname in COUNTERS: return COUNTERS[fname]
    inc = set(); cmp = set()
    for a, asm in funcs[fname]:
        op, args = parse(asm)
        if op == 'addiu' and len(args) == 3 and args[0] == args[1] and ival(args[2]) == 1: inc.add(args[0])
        if op in ('slti', 'sltiu') and ival(args[2]) in (3, 4): cmp.add(args[1])
    COUNTERS[fname] = inc & cmp
    return COUNTERS[fname]
CUR = [None]
def analyze(fname, record):
    lst = funcs[fname]
    if not lst: return False
    CUR[0] = counters(fname)
    addrs = [a for a, _ in lst]
    idx = {a: i for i, a in enumerate(addrs)}
    changed_facts = False
    # successors (a branch's delay slot runs before it lands)
    succ = {}
    n = len(lst)
    for i, (a, asm) in enumerate(lst):
        op, args = parse(asm)
        if i + 1 >= n: continue
        if op in BR1 or op in BR0:
            tgt = int(args[-1][2:], 16) if args and args[-1].startswith('L_') else None
            s = [idx[tgt]] if tgt is not None and tgt in idx else []
            if op != 'b': s.append(i + 2)
            succ[i] = [i + 1]; succ[i + 1] = s
        elif op == 'j':
            tgt = int(args[0][2:], 16) if args[0].startswith('L_') else int(args[0], 16)
            succ[i] = [i + 1]; succ[i + 1] = [idx[tgt]] if tgt in idx else []
        elif op == 'jr':
            t = SW.get(a)
            succ[i] = [i + 1]; succ[i + 1] = [idx[x] for x in (t or []) if x in idx]
    def successors(i):
        if i in succ: return [j for j in succ[i] if j < n]
        return [i + 1] if i + 1 < n else []
    entry = {}
    for r, v in F.arg_const.get(fname, {}).items():
        if v != 'X': entry[r] = ('C', v)
    for r in F.arg_slot.get(fname, ()): entry[r] = ('S',)
    for r in F.arg_wptr.get(fname, ()): entry[r] = ('W', 0)
    state = {0: entry}
    work = [0]
    visits = collections.Counter()
    while work:
        i = work.pop()
        visits[i] += 1
        if visits[i] > 40: continue
        st = dict(state[i])
        a, asm = lst[i]
        op, args = parse(asm)
        if op in ('jal', 'jalr'):
            res = False
        else:
            res = step(fname, a, op, args, st, record)
            if i > 0:
                pop, pargs = parse(lst[i - 1][1])
                if pop in ('jal', 'jalr'):
                    if step(fname, lst[i - 1][0], pop, pargs, st, record): res = True
        if res: changed_facts = True
        for j in successors(i):
            if j not in state:
                state[j] = dict(st); work.append(j)
            else:
                old = state[j]; new = {}
                for r in set(old) | set(st):
                    v = join(old.get(r), st.get(r))
                    if v is not None: new[r] = v
                if new != old: state[j] = new; work.append(j)
    return changed_facts

def old_known(callee, r):
    v = F.arg_const[callee].get(r)
    return v is not None and v != 'X'
def kill(st, r):
    if r in st: del st[r]

def memaddr(st, arg):
    m = re.match(r'(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)', arg)
    if not m: return None, None, None
    off = ival(m.group(1)); r = m.group(2)
    return off, r, st.get(r)

def step(fname, pc, op, args, st, record):
    ch = False
    g = lambda r: (('C', 0) if r == '$zero' else st.get(r))
    def setr(r, v):
        if r == '$zero': return
        if v is None: kill(st, r)
        else: st[r] = v
    if op in MEM:
        off, base, bv = memaddr(st, args[1])
        dst = args[0]
        ea_kind = None
        if bv is not None:
            if bv[0] == 'C':
                ea = (bv[1] + off) & 0xFFFFFFFF; ea_kind = ('abs', ea)
            elif bv[0] == 'P':
                b2 = (bv[1] + off) & 0xFFFFFFFF; ea_kind = ('psa', b2, bv[2])
            elif bv[0] == 'W':
                ea_kind = ('w', bv[1] + off)
            elif bv[0] in ('M',) and bv[1] == WS:
                ea_kind = None
        isload = op[0] == 'l'
        if record and ea_kind:
            if ea_kind[0] == 'psa':
                b2, k = ea_kind[1], ea_kind[2]
                if k == WS and TB <= b2 < TB + WS: O.wacc[b2 - TB] += 1
                else: O.psa[(b2, k)].add((pc, op, fname))
            elif ea_kind[0] == 'w': O.wacc[ea_kind[1]] += 1
        if isload:
            v = None
            if ea_kind:
                if ea_kind[0] == 'w' and ea_kind[1] in F.slot_fields_W: v = ('S',)
                elif ea_kind[0] == 'w' and ea_kind[1] in F.ptr_fields_W: v = ('W', 0)
                elif ea_kind[0] == 'psa' and ea_kind[2] == WS and TB <= ea_kind[1] < TB + WS:
                    o = ea_kind[1] - TB
                    if o in F.slot_fields_W: v = ('S',)
                    elif o in F.ptr_fields_W: v = ('W', 0)
                elif ea_kind[0] == 'psa' and (ea_kind[1], ea_kind[2]) in F.slot_psa: v = ('S',)
                elif ea_kind[0] == 'abs' and ea_kind[1] in F.slot_globals: v = ('S',)
                elif ea_kind[0] == 'abs' and ea_kind[1] in F.ptr_globals: v = ('W', 0)
            if op in ('lwc1', 'ldc1'): return ch
            setr(dst, v)
        else:
            sv = g(dst) if not op.endswith('c1') else None
            if sv and ea_kind:
                if sv[0] in ('S','N'):
                    if ea_kind[0] == 'w' and ea_kind[1] not in F.slot_fields_W and op in ('sw', 'sh', 'sb'):
                        F.slot_fields_W.add(ea_kind[1]); ch = True
                    elif ea_kind[0] == 'psa' and ea_kind[2] == WS and TB <= ea_kind[1] < TB + WS and op in ('sw', 'sh', 'sb'):
                        o = ea_kind[1] - TB
                        if o not in F.slot_fields_W: F.slot_fields_W.add(o); ch = True
                    elif ea_kind[0] == 'abs' and ea_kind[1] not in F.slot_globals:
                        F.slot_globals.add(ea_kind[1]); ch = True
                    elif ea_kind[0] == 'psa' and (ea_kind[1], ea_kind[2]) not in F.slot_psa and ea_kind[2] != WS:
                        F.slot_psa.add((ea_kind[1], ea_kind[2])); ch = True
                if sv[0] == 'W' and sv[1] == 0 and op == 'sw':
                    if ea_kind[0] == 'w' and ea_kind[1] not in F.ptr_fields_W: F.ptr_fields_W.add(ea_kind[1]); ch = True
                    elif ea_kind[0] == 'abs' and ea_kind[1] not in F.ptr_globals: F.ptr_globals.add(ea_kind[1]); ch = True
        return ch
    if op == 'lui':
        setr(args[0], ('C', (ival(args[1]) << 16) & 0xFFFFFFFF)); return ch
    if op in ('addiu', 'addi', 'daddiu') and args[0] in CUR[0] and (args[1] == '$zero' or args[1] == args[0]):
        setr(args[0], ('N',)); return ch
    if op in ('addu', 'or') and args[0] in CUR[0] and args[1] == '$zero' and args[2] == '$zero':
        setr(args[0], ('N',)); return ch
    if op in ('addiu', 'addi', 'daddiu'):
        v = g(args[1]); im = ival(args[2]); d = args[0]
        if v is None: setr(d, None)
        elif v[0] == 'C': setr(d, ('C', (v[1] + im) & 0xFFFFFFFF))
        elif v[0] == 'P': setr(d, ('P', (v[1] + im) & 0xFFFFFFFF, v[2]))
        elif v[0] == 'M': setr(d, ('P', im & 0xFFFFFFFF, v[1]))
        elif v[0] == 'W': setr(d, ('W', v[1] + im))
        elif v[0] in ('S', 'N'):
            if im in (1, -1) and d == args[1]: setr(d, v)       # counter step
            else: setr(d, ('P', im & 0xFFFFFFFF, 1))
        else: setr(d, None)
        return ch
    if op in ('ori', 'xori', 'andi'):
        v = g(args[1]); im = ival(args[2])
        if v and v[0] == 'C':
            r = v[1] | im if op == 'ori' else (v[1] ^ im if op == 'xori' else v[1] & im)
            setr(args[0], ('C', r))
        elif v and v[0] in ('S', 'N') and op == 'andi' and im in (0xFF, 0xFFFF): setr(args[0], v)
        elif v and v[0] in ('S', 'N') and op == 'xori':
            if record: O.cmps.add((pc, fname, 'xori', im))
            setr(args[0], None)
        else: setr(args[0], None)
        return ch
    if op in ('sll', 'dsll'):
        v = g(args[1]); sh = ival(args[2])
        if v and v[0] in ('S', 'N'): setr(args[0], ('M', 1 << sh))
        elif v and v[0] == 'M': setr(args[0], ('M', v[1] << sh))
        elif v and v[0] == 'C': setr(args[0], ('C', (v[1] << sh) & 0xFFFFFFFF))
        else: setr(args[0], None)
        return ch
    if op in ('sra', 'srl'):
        v = g(args[1]); sh = ival(args[2])
        if v and v[0] in ('S', 'N') and sh in (16, 24) : setr(args[0], v)
        elif v and v[0] == 'M' and sh >= 16: setr(args[0], None)
        elif v and v[0] == 'C': setr(args[0], ('C', (v[1] >> sh)))
        else: setr(args[0], None)
        return ch
    if op == 'sllv':
        v = g(args[2])   # shift amount
        if v and v[0] in ('S', 'N'):
            setr(args[0], ('B',))
            if record: O.bits.add((pc, fname))
        else: setr(args[0], None)
        return ch
    if op in ('srav', 'srlv'):
        v = g(args[2])
        if v and v[0] in ('S', 'N') and record: O.bits.add((pc, fname))
        setr(args[0], None); return ch
    if op in ('addu', 'add', 'daddu'):
        a, b = g(args[1]), g(args[2]); d = args[0]
        def norm(x):
            if x and x[0] in ('S', 'N'): return ('M', 1)
            return x
        a, b = norm(a), norm(b)
        if a is None or b is None:
            # pointer + unknown stays unknown
            setr(d, None); return ch
        if a[0] == 'C' and b[0] == 'C': setr(d, ('C', (a[1] + b[1]) & 0xFFFFFFFF))
        elif a[0] == 'C' and b[0] == 'M': setr(d, ('P', a[1], b[1]))
        elif a[0] == 'M' and b[0] == 'C': setr(d, ('P', b[1], a[1]))
        elif a[0] == 'M' and b[0] == 'M': setr(d, ('M', a[1] + b[1]))
        elif a[0] == 'P' and b[0] == 'C': setr(d, ('P', (a[1] + b[1]) & 0xFFFFFFFF, a[2]))
        elif a[0] == 'C' and b[0] == 'P': setr(d, ('P', (a[1] + b[1]) & 0xFFFFFFFF, b[2]))
        elif a[0] == 'P' and b[0] == 'M' or a[0] == 'M' and b[0] == 'P':
            setr(d, None)
        else: setr(d, None)
        # wrestler pointer recognition: base TB + slot*0x358
        v = st.get(d)
        if v and v[0] == 'P' and v[2] == WS and (TB - 0x10) <= v[1] <= TB + 0x10:
            st[d] = ('W', (v[1] - TB) & 0xFFFFFFFF if v[1] >= TB else -(TB - v[1]))
        return ch
    if op in ('subu', 'sub', 'dsubu'):
        a, b = g(args[1]), g(args[2])
        def norm(x):
            if x and x[0] in ('S', 'N'): return ('M', 1)
            return x
        a, b = norm(a), norm(b)
        if a and b and a[0] == 'M' and b[0] == 'M': setr(args[0], ('M', a[1] - b[1]))
        elif a and b and a[0] == 'C' and b[0] == 'C': setr(args[0], ('C', (a[1] - b[1]) & 0xFFFFFFFF))
        else: setr(args[0], None)
        return ch
    if op in ('or',):
        a, b = g(args[1]), g(args[2])
        if b == ('C', 0): setr(args[0], a)
        elif a == ('C', 0): setr(args[0], b)
        else: setr(args[0], None)
        return ch
    if op in ('slti', 'sltiu'):
        v = g(args[1]); im = ival(args[2])
        if v and v[0] in ('S', 'N') and record:
            O.loops.add((pc, fname, im))
        setr(args[0], None); return ch
    if op in ('slt', 'sltu'):
        a, b = g(args[1]), g(args[2])
        if record and ((a and a[0] in ('S', 'N') and b and b[0] == 'C') or (b and b[0] in ('S', 'N') and a and a[0] == 'C')):
            O.cmps.add((pc, fname, 'slt', (a or b)))
        setr(args[0], None); return ch
    if op in BR1:
        a, b = g(args[0]), g(args[1])
        if record and ((a and a[0] in ('S', 'N') and b and b[0] == 'C' and b[1] != 0) or (b and b[0] in ('S', 'N') and a and a[0] == 'C' and a[1] != 0)):
            O.cmps.add((pc, fname, op, (a if a[0] == 'C' else b)[1]))
        return ch
    if op == 'jal':
        tgt = int(args[0], 16)
        callee = func_at.get(tgt)
        if callee:
            for r in ('$a0', '$a1', '$a2', '$a3'):
                v = st.get(r)
                if v and v[0] in ('S','N') and r not in F.arg_slot[callee]: F.arg_slot[callee].add(r); ch = True
                if v and v[0] == 'W' and v[1] == 0 and r not in F.arg_wptr[callee]: F.arg_wptr[callee].add(r); ch = True
                if v and v[0] == 'C':
                    old = F.arg_const[callee].get(r)
                    if old is None: F.arg_const[callee][r] = v[1]; ch = True
                    elif old != v[1] and old != 'X': F.arg_const[callee][r] = 'X'; ch = True
                elif old_known(callee, r) and (v is None or v[0] != 'C'):
                    F.arg_const[callee][r] = 'X'; ch = True
        for r in list(st):
            if r not in CALLEE_SAVED: del st[r]
        if callee in F.ret_slot: st['$v0'] = ('S',)
        if callee in F.ret_wptr: st['$v0'] = ('W', 0)
        return ch
    if op == 'jalr':
        for callee in INDIRECT.get(pc, []):
            for r in ('$a0', '$a1', '$a2', '$a3'):
                v = st.get(r)
                if v and v[0] == 'C':
                    old = F.arg_const[callee].get(r)
                    if old is None: F.arg_const[callee][r] = v[1]; ch = True
                    elif old != v[1] and old != 'X': F.arg_const[callee][r] = 'X'; ch = True
                if v and v[0] in ('S', 'N') and r not in F.arg_slot[callee]: F.arg_slot[callee].add(r); ch = True
                if v and v[0] == 'W' and v[1] == 0 and r not in F.arg_wptr[callee]: F.arg_wptr[callee].add(r); ch = True
        for r in list(st):
            if r not in CALLEE_SAVED: del st[r]
        return ch
    if op == 'jr' and args and args[0] == '$ra':
        v = st.get('$v0')
        if v and v[0] == 'S' and fname not in F.ret_slot: F.ret_slot.add(fname); ch = True
        if v and v[0] == 'W' and v[1] == 0 and fname not in F.ret_wptr: F.ret_wptr.add(fname); ch = True
        return ch
    if op in ('mult', 'multu'):
        a, b = g(args[0]), g(args[1])
        k = None
        if a and a[0] in ('S', 'N') and b and b[0] == 'C': k = b[1]
        if b and b[0] in ('S', 'N') and a and a[0] == 'C': k = a[1]
        st['$lo'] = ('M', k) if k is not None else None
        if st['$lo'] is None: del st['$lo']
        return ch
    if op == 'mflo':
        setr(args[0], st.get('$lo')); return ch
    if op in ('mfhi',):
        setr(args[0], None); return ch
    # generic: an op writing its first register arg
    if args and args[0].startswith('$') and not args[0].startswith('$f') and op not in ('mtc1', 'dmtc1', 'ctc1') and op not in BR0:
        if op not in ('nop', 'break', 'syscall', 'cache', 'sync', 'jr', 'j', 'b'):
            setr(args[0], None)
    return ch

def run(passes=8):
    names = [f for f in funcs]
    for p in range(passes):
        ch = False
        for f in names:
            if analyze(f, False): ch = True
        print("pass", p, "slot fields", sorted(hex(x) for x in F.slot_fields_W)[:40], len(F.slot_globals), len(F.slot_psa),
              sum(len(v) for v in F.arg_slot.values()), sum(len(v) for v in F.arg_wptr.values()), len(F.ret_slot), file=sys.stderr)
        if not ch: break
    O.reset()
    for f in names: analyze(f, True)

if __name__ == "__main__":
    run()
    pickle.dump((F.__dict__, dict(O.psa), O.loops, O.bits, O.cmps, O.wacc), open(nmp('/home/devbox/work/six/slotflow.pkl'), 'wb'))
    print("per-slot arrays:", len(O.psa), "loops:", len(O.loops), "bits:", len(O.bits), "cmps:", len(O.cmps))
