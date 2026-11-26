"""Index the N64Recomp output (asm comments) of WWF No Mercy for address cross-references.

python3 xref.py build            -> writes index.pkl
python3 xref.py refs 0x800457A4  -> functions + instructions that reference that address (+-0 exact)
python3 xref.py func func_80111158 [n]  -> print the function's asm
python3 xref.py callers func_xxx -> functions that jal it
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import re, sys, os, pickle, glob

SRC = nmp("/home/devbox/src/WWFNoMercyRecomp/RecompiledFuncs")
IDX = nmp("/home/devbox/work/xref_index.pkl")
FUNC_RE = re.compile(r"^RECOMP_FUNC void (\w+)\(")
ASM_RE = re.compile(r"^\s*// 0x([0-9A-F]{8}): (\w+(?:\.\w+)*)\s*(.*)$")
REGS = "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split()

def num(s):
    s = s.strip()
    neg = s.startswith("-")
    v = int(s.lstrip("-"), 0)
    return -v if neg else v

def build():
    funcs = {}
    for fn in sorted(glob.glob(SRC + "/funcs_*.c")):
        cur = None; seen = set()
        for line in open(fn):
            m = FUNC_RE.match(line)
            if m:
                cur = m.group(1); funcs[cur] = []; seen = set(); continue
            m = ASM_RE.match(line)
            if m and cur:
                addr = int(m.group(1), 16)
                if addr in seen:  # delay-slot duplicates
                    continue
                seen.add(addr)
                funcs[cur].append((addr, m.group(2), m.group(3)))
    refs = {}   # address -> list of (func, insn_addr, text)
    calls = {}  # target -> set(callers)
    for f, ins in funcs.items():
        hi = {}
        for addr, op, args in ins:
            a = [x.strip() for x in args.split(",")] if args else []
            if op == "lui":
                hi[a[0]] = num(a[1]) << 16
                continue
            if op in ("jal",):
                calls.setdefault(a[0], set()).add(f)
            target = None
            if op in ("addiu", "ori") and len(a) == 3 and a[1] in hi:
                base = hi[a[1]]
                target = (base | num(a[2])) if op == "ori" else (base + num(a[2]))
                if a[0] != a[1]:
                    pass
                hi.pop(a[0], None)
                if op == "addiu":
                    hi_val = target
                    # keep as possible pointer: record
            elif len(a) == 2 and "(" in a[1]:
                m = re.match(r"(-?0x[0-9A-Fa-f]+|-?\d+)\(\$(\w+)\)", a[1])
                if m and ("$" + m.group(2)) in hi:
                    target = hi["$" + m.group(2)] + num(m.group(1))
                if not op.startswith("s") and a[0] in hi:
                    hi.pop(a[0], None)
            elif op in ("addu", "add") and len(a) == 3 and (a[1] in hi or a[2] in hi):
                hi[a[0]] = hi[a[1]] if a[1] in hi else hi[a[2]]
            else:
                if a and a[0] in hi:
                    hi.pop(a[0], None)
            if target is not None:
                target &= 0xFFFFFFFF
                refs.setdefault(target, []).append((f, addr, f"{op} {args}"))
    pickle.dump((funcs, refs, calls), open(IDX, "wb"))
    print(len(funcs), "functions,", len(refs), "referenced addresses")

def load():
    return pickle.load(open(IDX, "rb"))

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "build":
        build(); sys.exit()
    funcs, refs, calls = load()
    if cmd == "refs":
        lo = int(sys.argv[2], 16); hi_ = int(sys.argv[3], 16) if len(sys.argv) > 3 else lo
        for t in sorted(refs):
            if lo <= t <= hi_:
                for f, a, txt in refs[t]:
                    print(f"{t:08X}  {f:28s} {a:08X}  {txt}")
    elif cmd == "func":
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 100000
        for addr, op, args in funcs[sys.argv[2]][:n]:
            print(f"{addr:08X}  {op:8s} {args}")
    elif cmd == "callers":
        print(sorted(calls.get(sys.argv[2], [])))
    elif cmd == "find":
        for f in funcs:
            if re.search(sys.argv[2], f): print(f, hex(funcs[f][0][0]) if funcs[f] else "")
