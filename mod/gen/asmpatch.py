#!/usr/bin/env python3
"""Changed copies of the game's own functions, made from the ROM's code (gen/patches.py lists them).

A function is copied instruction for instruction (as .word), with its branches and jumps written as
instructions to labels (so they still land right when instructions are added), and the listed edits
made. Two builds of each:
  build/patches/mod.S   in the mod's .recomp_patch section under the function's own name: the
                        recompiled game uses it in place of the original (RECOMP_PATCH)
  build/patches/test.S  for the test blob as patch_<name>; the rig jumps to it from the original's
                        first instruction (test/patches.inc lists them with their signatures)
and build/patches/gamesyms.ld: the game's functions the copies call, at their addresses (the blob).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import os, sys, struct, pickle, re
W = nmp("/home/devbox/work/")
sys.path.insert(0, W + "arcade/gen")
ROM = open(os.environ.get("ARCADE_ROM", nmp("/home/devbox/src/WWFNoMercyRecomp/nomercy.z64")), "rb").read()   # (the six-man build: its ROM)
secs, fn2sec, _ = pickle.load(open(W + "six/db.pkl", "rb"))
OUT = W + "arcade/build/patches/"
CALLS = {}   # game functions the copies call -> their addresses

def rom_of(sec, vram): s = secs[sec]; return s["rom"] + (vram - s["vram"])
def sec_range(sec): s = secs[sec]; return s["vram"], s["vram"] + s["size"]
by_addr = {}
for name, (sec, vram, size) in fn2sec.items(): by_addr.setdefault(vram, []).append((sec, name))
OVL_SLOT_A = ("ovl_a",)            # loaded with the menus' b / c / d
# the six-man engine's functions (its build's syms/dump.toml): the six-man ROM's copies of the game's functions may
# call them (func_8013486C -> rw_rumble_fill)
SIX_SYMS = {}
for _p in (os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "six-engine", "NoMercySix", "syms", "dump.toml"),):
    if os.path.exists(_p):
        for _m in re.finditer(r'name = "(\w+)", vram = 0x([0-9A-Fa-f]+)', open(_p).read()):
            if int(_m.group(2), 16) >= 0x80700000: SIX_SYMS[int(_m.group(2), 16)] = _m.group(1)
def func_name(target, caller_sec):
    cands = by_addr.get(target, [])
    for want in (caller_sec, "main_1050"):
        for sec, n in cands:
            if sec == want: return n
    if caller_sec in ("ovl_b", "ovl_c", "ovl_d"):
        for sec, n in cands:
            if sec == "ovl_a": return n
    if caller_sec == "ovl_a" and len(set(n for _, n in cands)) == 1: return cands[0][1]
    if target in SIX_SYMS: return SIX_SYMS[target]   # (the six-man engine's own code, which some of the game's now call)
    raise SystemExit("no function at %08x for a call from %s (%s)" % (target, caller_sec, cands))

def branch(w, pc):
    """-> (mnemonic text with %s for the target, target address, is_jump) or None"""
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31
    off = ((w & 0xFFFF) ^ 0x8000) - 0x8000
    tgt = (pc + 4 + (off << 2)) & 0xFFFFFFFF
    if op == 1:
        m = {0: "bltz", 1: "bgez", 2: "bltzl", 3: "bgezl", 16: "bltzal", 17: "bgezal"}.get(rt)
        if m: return "%s $%d, %%s" % (m, rs), tgt, False
    if op in (2, 3):
        return ("j %s" if op == 2 else "jal %s"), ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2), True
    if op in (4, 5, 20, 21):
        m = {4: "beq", 5: "bne", 20: "beql", 21: "bnel"}[op]; return "%s $%d, $%d, %%s" % (m, rs, rt), tgt, False
    if op in (6, 7, 22, 23):
        m = {6: "blez", 7: "bgtz", 22: "blezl", 23: "bgtzl"}[op]; return "%s $%d, %%s" % (m, rs), tgt, False
    if op == 17 and rs == 8:
        m = ["bc1f", "bc1t", "bc1fl", "bc1tl"][rt & 3]; return "%s %%s" % m, tgt, False
    return None

def make(spec):
    name = spec["func"]; sec, start, size = fn2sec[name]
    end = start + size
    words = [struct.unpack_from(">I", ROM, rom_of(sec, a))[0] for a in range(start, end, 4)]
    edits = spec.get("edits", {})
    # every branch target inside gets a label
    labels = set(); tables = []
    for i, w in enumerate(words):
        pc = start + 4 * i; b = branch(w, pc)
        if b and not b[2]:
            if not (start <= b[1] < end): raise SystemExit("%s: branch at %08x leaves the function" % (name, pc))
            labels.add(b[1])
        elif b and b[0].startswith("j ") and start <= b[1] < end: labels.add(b[1])
        if (w >> 26) == 0 and (w & 63) == 8 and ((w >> 21) & 31) != 31 and pc not in edits:
            jt = jump_table(sec, words, start, i, name)
            tables.append(jt); labels.update(jt["targets"])
    for a in edits: assert start <= a < end and a % 4 == 0, hex(a)
    # (labels carry the overlay: ovl_c's and ovl_d's functions share addresses -- both patched, the labels met)
    tag = "_" + sec if sec.startswith("ovl") else ""
    def body(variant):
        out = []; tail = None
        for i, w in enumerate(words):
            pc = start + 4 * i
            lab = "L%s_%08X" % (tag, pc)
            if tail:   # (the delay slot of a tail call out: it, then the call and the return; the mod loader's
                       # recompiler takes no "j" to another module's function -- "Unhandled branch ... to 0x80000000")
                assert pc not in edits and branch(w, pc) is None and pc not in labels, (name, hex(pc))
                out += ["    .word 0x%08X" % w, "    addiu $sp, $sp, -0x18", "    sw $ra, 0x14($sp)",
                        "    jal %s" % tail, "    nop", "    lw $ra, 0x14($sp)", "    jr $ra", "    addiu $sp, $sp, 0x18"]
                tail = None; continue
            if pc in edits:
                lines = edits[pc] if isinstance(edits[pc], list) else [edits[pc]]
                out.append("%s:" % lab)
                for l in lines:
                    m = re.match(r"\s*jal\s+func_([0-9A-F]{8})$", l)
                    if m: CALLS["func_" + m.group(1)] = int(m.group(1), 16)
                    out.append("    " + l)
                continue
            b = branch(w, pc)
            pre = (lab + ": ") if pc in labels else ""
            jt = next((t for t in tables if pc in (t["lui"], t["lw"])), None)
            if jt and pc == jt["lui"]:
                out.append("%s    lui $%d, %%hi(JT%s_%08X)" % (pre, (w >> 16) & 31, tag, jt["jr"])); continue
            if jt and pc == jt["lw"]:
                out.append("%s    lw $%d, %%lo(JT%s_%08X)($%d)" % (pre, (w >> 16) & 31, tag, jt["jr"], (w >> 21) & 31)); continue
            if b is None:
                out.append("%s    .word 0x%08X" % (pre, w))
            elif not b[2]:
                out.append("%s    %s" % (pre, b[0] % ("L%s_%08X" % (tag, b[1]))))
            elif start <= b[1] < end and b[0].startswith("j "):
                out.append("%s    j L%s_%08X" % (pre, tag, b[1]))
            else:
                fn = func_name(b[1], sec)
                CALLS[fn] = b[1]
                if b[0].startswith("j "): out.append("%s    nop" % pre); tail = fn; continue
                out.append("%s    %s" % (pre, b[0] % fn))
        for t in tables:   # the jump tables, made of labels of the copy
            out += ['    .section .rodata.jt%s_%08X,"a",@progbits' % (tag, t["jr"]), "    .align 2", "JT%s_%08X:" % (tag, t["jr"])]
            out += ["    .word L%s_%08X" % (tag, a) for a in t["entries"]]
            out += ["    .previous"]
        return out
    return name, sec, start, words[:2], body

def jump_table(sec, words, start, i, name):
    """jr $r at words[i]: lui $at, hi / addu $at, $at, $x / lw $r, lo($at) before it (sltiu bounds it)"""
    reg = (words[i] >> 21) & 31; lw = lui = None; n = None
    for j in range(i - 1, max(0, i - 16) - 1, -1):     # (down to the function's first word: func_8012DCC0's bound check is it)
        w = words[j]; op = w >> 26
        if lw is None and op == 35 and ((w >> 16) & 31) == reg: lw = j; base = (w >> 21) & 31; lo = ((w & 0xFFFF) ^ 0x8000) - 0x8000; continue
        if lw is not None and lui is None and op == 15 and ((w >> 16) & 31) == base: lui = j; hi = w & 0xFFFF; continue
        if lui is not None and op == 11: n = ((w & 0xFFFF) ^ 0x8000) - 0x8000; break
    if lw is None or lui is None or n is None: raise SystemExit("%s: jump table at %08x not understood" % (name, start + 4 * i))
    addr = ((hi << 16) + lo) & 0xFFFFFFFF
    ents = [struct.unpack_from(">I", ROM, rom_of(sec, addr + 4 * k))[0] for k in range(n)]
    end = start + 4 * len(words)
    assert all(start <= e < end for e in ents), (name, [hex(e) for e in ents])
    return dict(jr=start + 4 * i, lui=start + 4 * lui, lw=start + 4 * lw, entries=ents, targets=set(ents))
def emit(specs):
    os.makedirs(OUT, exist_ok=True)
    mod = ["# generated by gen/asmpatch.py: changed copies of the game's functions (gen/patches.py)",
           "    .set noreorder", "    .set noat"]
    test = list(mod)
    inc = ["// generated by gen/asmpatch.py: { address, first two words, the copy }"]
    for spec in specs:
        name, sec, start, sig, body = make(spec)
        lines = body("mod")
        mod += ["", "# %s (%s) %s" % (name, sec, spec.get("why", "")),
                '    .section .recomp_patch,"axR",@progbits', "    .globl %s" % name,
                "    .type %s, @function" % name, "    .align 2", "%s:" % name] + lines + ["    .size %s, .-%s" % (name, name)]
        tl = [re.sub(r"\b(L|JT)_", lambda m: "P%s_%s_" % (name[5:13], m.group(1)), l) for l in lines]
        test += ["", "# %s (%s)" % (name, sec), '    .section .text.patch_%s,"ax",@progbits' % name,
                 "    .globl patch_%s" % name, "    .type patch_%s, @function" % name, "    .align 2",
                 "patch_%s:" % name] + tl + ["    .size patch_%s, .-patch_%s" % (name, name)]
        inc.append("extern void patch_%s(void);" % name)
        inc.append("#define ASMPATCH_%s { 0x%08Xu, 0x%08Xu, 0x%08Xu, (void*)patch_%s }," % (name, start, sig[0], sig[1], name))
    inc.append("#define ASMPATCHES \\")
    for spec in specs: inc.append("    ASMPATCH_%s \\" % spec["func"])
    inc.append("")
    open(OUT + "mod.S", "w").write("\n".join(mod) + "\n")
    open(OUT + "test.S", "w").write("\n".join(test) + "\n")
    open(OUT + "patches.inc", "w").write("\n".join(inc) + "\n")
    syms = ["/* generated by gen/asmpatch.py */"]
    for c, a in sorted(CALLS.items()):
        syms.append("%s = 0x%08X;" % (c, a))
    open(OUT + "gamesyms.ld", "w").write("\n".join(syms) + "\n")

if __name__ == "__main__":
    import patches
    emit(patches.PATCHES)
    print("%d functions" % len(patches.PATCHES))
