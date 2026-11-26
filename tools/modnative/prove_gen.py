#!/usr/bin/env python3
"""Tester entries for the mod's functions made native (scratch/agents/modnative1, 2026-11-12): step 2 of making the mod native.

For each chosen function of the mod (by its address A in the mod's build):
  ORIGINAL   Stage 1's linked translation, nm_aot_nomercy_arcade_<A> (scratch/agents/final081/aot/mod_aot: the live mod's
             build of 12:49), under the name modrep_nat_<A>, its calls named as modrep's translation names them (modfn_<A'>,
             func_X): written to tools/lift/modrep/out/nat_originals.c, read through LIFT_EXTRA_ORIGINALS (modglue's
             switch). The modrep_ prefix makes the tester place and fill the mod's memory for it as for the mod's copies.
  CONVERTED  the mod's own C for that function, compiled for the PC: native.py's preprocessed, pointer-rewritten, view-bound
             source (pp/<file>.native.cpp) -- the function and the functions of its file it calls (which the mod's compiler
             inlined: a function here is chosen only if its translation calls nothing, or only what it inlined) -- in a namespace
             of its own, with the mod's PRIVATE data it touches bound to the tester's PC objects (the confirmed rule:
             'private' in its candidates.json entry), every other global of the mod a view at its N64 address (lift_mod_address: where
             the tester put the mod), and a thin entry taking the parameters from the N64's registers.
Everything else is the pipeline's own: `lift.py prove`, the same tester, the same bar.

    python prove_gen.py list                 the functions this can make entries for now, and why each other one can't
    python prove_gen.py put <A> [<A> ...]    add them to tools/lift/candidates.json (from candidates_base.json)
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
sys.path.insert(0, HERE)
import native

DEV = native.DEV
LIFT = os.path.join(DEV, 'tools', 'lift')
# (2026-11-13, modnative5: NM_STAGE=<folder> writes candidates.json and nat_originals.c there instead -- for making and
#  checking entries while a run in the tools copy reads its own)
STAGE = os.environ.get('NM_STAGE')
OUT_ORIGINALS = os.path.join(STAGE, 'nat_originals.c') if STAGE else os.path.join(LIFT, 'modrep', 'out', 'nat_originals.c')
OUT_CANDIDATES = os.path.join(STAGE, 'candidates.json') if STAGE else os.path.join(LIFT, 'candidates.json')
AOT = os.path.join(HERE, 'w', 'mod_aot')   # (modnative5: this folder's Stage 1 translation of its own mod build, aot_copy.py)
INT_TYPES = {'int': 's32', 'unsigned int': 'u32', 'unsigned': 'u32', 's32': 's32', 'u32': 'u32', 's16': 's32', 'u16': 's32', 's8': 's32',
             'u8': 's32', 'short': 's32', 'unsigned short': 's32', 'char': 's32', 'signed char': 's32', 'unsigned char': 's32',
             'long': 's32', 'unsigned long': 'u32', 'bool': 's32', '_Bool': 's32'}


PRIVATE_ROWS = json.load(open(os.path.join(LIFT, 'private_data.json')))
# (2026-11-13, modnative5) The user's rules of 2026-11-13, each with a switch here as in the tester (NM_NO_<rule>=1: as before):
#   (2) shrunk booleans -- NShrunk bindings (NM_NO_SHRUNK)      (1) narrow parameters -- the entry's list (NM_NO_NARROW)
#   (3) fastcc callees' stubs and the function's own fastcc registers (NM_NO_FAST), loop ticks (NM_NO_LOOPS)
use_shrunk = lambda: os.environ.get('NM_NO_SHRUNK') != '1'
use_narrow = lambda: os.environ.get('NM_NO_NARROW') != '1'
use_fast = lambda: os.environ.get('NM_NO_FAST') != '1'
use_loops = lambda: os.environ.get('NM_NO_LOOPS') != '1'
# (2026-11-14, modnative7) The rules the user approved on 2026-11-14 for the native mod, each with a switch (all tester-side
#  changes apply to native-mod candidates only):
#   memcpy        (on; NM_NO_MEMCPY=1 off) a structure copy the mod's compiler made into a call to memcpy runs through its caller
#   narrow args   (on; NM_NO_LOWBITS=1 off) a stand-in compares only the low 8 / 16 bits of a callee's narrow argument -- ONLY
#                 where narrow_proof.py proves from the callee's own code that it ignores the bits above (strict form)
#   narrow results (on, lift.py LIFT_NM_NARROW_RESULTS=0 off) made strict the same way: only where the caller is proved to
#                 ignore the high bits after every call to that callee (narrow_proof.caller_ignores_high)
#   private values (OFF until an independent review: NM_RULE_WRITES=1) a private scalar's test values only those the mod's
#                 writes can give it (private_values.py, conservative)
#   (and the mod built with -fwrapv -fno-strict-aliasing: build_mod_copy.ps1, gen_dwarf_ir.py)
#  lift.py reads the same switches: what a candidate's entry says under one is used only while it is on there too.
rule_writes = lambda: os.environ.get('NM_RULE_WRITES') == '1'
rule_memcpy = lambda: os.environ.get('NM_NO_MEMCPY') != '1'
rule_lowbits = lambda: os.environ.get('NM_NO_LOWBITS') != '1'
SHRUNK_USED = set()     # (the shrunk ones the candidate being made binds: its entry lists them for the tester)
PRIVATE_SIZES = {r['name']: r['size'] for r in PRIVATE_ROWS}


def load():
    inv = json.load(open(os.path.join(HERE, 'inventory.json')))
    census = json.load(open(os.path.join(HERE, 'census.json')))
    data, private = native.map_data()
    return inv, census, data, private


def file_items(base, cache={}):
    """(tokens, items) of pp/<base>.native.cpp."""
    if base not in cache:
        text = open(os.path.join(native.PP, base + '.native.cpp'), encoding='utf8').read()
        toks = native.tokenize(text)
        cache[base] = (toks, native.split_items(toks))
    return cache[base]


def closure(base, name, stubs=()):
    """The functions of the file the function needs (its own definition and those it calls, by name, recursively) -- not the
    ones its original calls (stubs: those go to the tester's stand-ins)."""
    toks, its = file_items(base)
    defs = {n: (a, b) for kind, n, a, b in its if kind == 'function'}
    need, todo = [], [name]
    while todo:
        n = todo.pop()
        if n in need or n not in defs or (n in stubs and n != name):
            continue
        need.append(n)
        a, b = defs[n]
        body = [t for _, t in toks[a:b + 1]]
        for k, t in enumerate(body[:-1]):
            if body[k + 1] == '(' and t in defs and t not in need:
                todo.append(t)
    return need


def inline_plan(a, r, inv, calls, thru=()):
    """(part 1 of the Linux work, 2026-11-14) inline_sites.plan for the candidate: None for a function none of whose callees is
    both called and inlined by the original (then closure() is right as it is, and the candidate's text is what it was), else
    the bodies the native code needs and what each of their calls calls. NM_NO_SITES=1: off (as before)."""
    if os.environ.get('NM_NO_SITES') == '1' or not os.path.isdir(os.path.join(HERE, 'irg')):
        return None
    import inline_sites
    toks, its = file_items(r['object'])
    key = (a, tuple(sorted(calls)), tuple(thru))
    if key not in _PLANS:
        try:
            _PLANS[key] = inline_sites.plan(r['object'], r['name'], calls, ([t for _, t in toks], its), [inv[t]['name'] for t in thru])
        except inline_sites.Unresolved as e:
            _PLANS[key] = str(e)
    return _PLANS[key]


_PLANS = {}


def needed(a, r, inv, calls, thru=()):
    """The functions of the file whose bodies the native code needs: closure(), or the plan's when a callee is inlined at
    some calls and called at others."""
    plan = inline_plan(a, r, inv, calls, thru)
    if isinstance(plan, dict):
        return plan['names']
    return closure(r['object'], r['name'], calls)


def declared_signature(base, name):
    """(return type, [(type, name)], internal) from the function's definition in the file, or else its prototype there."""
    sig = signature(base, name)
    if sig:
        return sig + (is_internal(base, name),)
    toks, its = file_items(base)
    for kind, n, a, b in its:
        if kind != 'other':
            continue
        ts = drop_attributes([t for _, t in toks[a:b + 1]])
        for k in range(len(ts) - 1):
            if ts[k] == name and ts[k + 1] == '(' and '=' not in ts[:k] and '{' not in ts:
                sig = signature_at(ts, k)
                return sig + ('static' in ts[:k],)
    # (a function's own `extern T name(...);`: block scope)
    ts = [t for _, t in toks]
    for k in range(1, len(ts) - 1):
        if ts[k] == name and ts[k + 1] == '(':
            j = k - 1
            while j > 0 and ts[j] not in (';', '{', '}'):
                j -= 1
            if 'extern' in ts[j + 1:k]:
                e = k
                while e < len(ts) and ts[e] != ';':
                    e += 1
                return signature_at(ts[j + 1:e + 1], k - j - 1) + (False,)
            # (part 1 of the Linux work, 2026-11-14: the same without `extern` -- `{ void save_removal_report(void);
            #  save_removal_report(); }`, `int variants_removed_with(u16 base);` in a body: a type, the name, its parameters
            #  and a ';'. Four functions were refused for a callee "not declared in its file".)
            head = ts[j + 1:k]
            if head and head[0] not in ('return', 'else', 'do', 'case', 'goto', 'sizeof') and re.fullmatch(
                    r'(?:NPtr < )*[A-Za-z_]\w*(?: [A-Za-z_]\w*)*(?: >| >>)*', ' '.join(head)):
                e, depth = k + 1, 0
                while e < len(ts):
                    depth += (ts[e] == '(') - (ts[e] == ')')
                    if depth == 0:
                        break
                    e += 1
                if ts[e + 1:e + 2] == [';']:
                    return signature_at(ts[j + 1:e + 2], k - j - 1) + (False,)
    return None


def signature_at(ts, k):
    """(return type, [(type, name)]) of the declaration whose name is ts[k]. A parameter with no name gets p<i>."""
    name = ts[k]
    ret = [t for t in ts[:k] if t not in ('static', 'inline', '__inline', 'extern', 'RECOMP_PATCH')]
    while ret and ret[0] == '(':
        ret = ret[1:]
    depth, args, cur = 0, [], []
    j = k + 2
    while j < len(ts):
        t = ts[j]
        if t == '(':
            depth += 1
        elif t == ')':
            if depth == 0:
                break
            depth -= 1
        if t == ',' and depth == 0:
            args.append(cur)
            cur = []
        else:
            cur.append(t)
        j += 1
    if cur:
        args.append(cur)
    params = []
    for i, arg in enumerate(args):
        arg = [t for t in arg if t not in ('const', 'volatile')]
        if arg == ['void']:
            continue
        if len(arg) > 1 and re.match(r'[A-Za-z_]\w*$', arg[-1]) and arg[-1] not in native.SPEC and arg[-2] not in ('struct', 'union', 'enum'):
            params.append((tight(' '.join(arg[:-1])), arg[-1]))
        else:
            params.append((tight(' '.join(arg)), 'p%d' % i))
    return tight(' '.join(ret)), params


def signature(base, name):
    """(return type, [(type, name)]) of a function's (rewritten) definition."""
    toks, its = file_items(base)
    for kind, n, a, b in its:
        if kind == 'function' and n == name:
            ts = drop_attributes([t for _, t in toks[a:b + 1]])
            k = ts.index(name)
            ret = [t for t in ts[:k] if t not in ('static', 'inline', '__inline', '__attribute__', 'RECOMP_PATCH')]
            # (RECOMP_HOOK("x") and the like have gone in preprocessing: the mod's modding.h makes them attributes)
            while ret and ret[0] == '(':
                ret = ret[1:]
            depth, args, cur = 0, [], []
            j = k + 2
            while j < len(ts):
                t = ts[j]
                if t == '(':
                    depth += 1
                elif t == ')':
                    if depth == 0:
                        break
                    depth -= 1
                if t == ',' and depth == 0:
                    args.append(cur)
                    cur = []
                else:
                    cur.append(t)
                j += 1
            if cur:
                args.append(cur)
            params = []
            for arg in args:
                arg = [t for t in arg if t not in ('const', 'volatile')]
                if arg == ['void']:
                    continue
                params.append((tight(' '.join(arg[:-1])), arg[-1]))
            return tight(' '.join(t for t in ret if not t.startswith('__attribute__'))), params
    return None


def drop_attributes(ts):
    # (__attribute__((...)) groups: the mod's hook and section attributes, nothing a PC build needs)
    out, k = [], 0
    while k < len(ts):
        if ts[k] == '__attribute__':
            depth, k = 0, k + 1
            while k < len(ts):
                if ts[k] == '(':
                    depth += 1
                elif ts[k] == ')':
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            k += 1
            continue
        out.append(ts[k])
        k += 1
    return out


GPR_NAMES = {4: 'a0', 5: 'a1', 6: 'a2', 7: 'a3', 8: 't0', 9: 't1', 10: 't2', 11: 't3', 12: 't4', 13: 't5', 14: 't6', 15: 't7', 24: 't8', 3: 'v1'}


def is_internal(base, name):
    """Whether the function has internal linkage (static) in its file: the mod's compiler (LLVM) may then give it its fast
    calling convention, which passes floats in f0, f2, ... and integers in a0-a3, then t0-t8, v1 -- not the o32 rules."""
    toks, its = file_items(base)
    for kind, n, x, y in its:
        if kind == 'function' and n == name:
            ts = drop_attributes([t for _, t in toks[x:y + 1]])
            return 'static' in ts[:ts.index(name)]
    return False


# ---- The interface the mod's compiler really gave a function (2026-11-13, scratch/agents/modnative3) ----
# For an internal (static) function LLVM may change the interface its C declares: dead-argument elimination drops a parameter
# every caller passes the same constant for (after interprocedural constant propagation put that constant in the body) or that
# nothing reads, and the parameters after it move up a register; a result no caller uses goes (v0 is then whatever it was).
# say_text(s, style, cx, y, rgba) is say_text(s, style, y) in the mod: the entry read y from a3 where the original reads it
# from a2 (calls@5), and menu_choose_action(screen, action), sprite_hide(id) take nothing at all. The mod's own LLVM IR says
# what it did: ir/<file>.ll (build_mod.ps1's flags, -S -emit-llvm -fno-discard-value-names) has each function's final
# `define`, and ir/<file>.before_dae.ll (-mllvm -print-before=deadargelim) its calls as they were just before the parameters
# went, with the constants they passed. The entry then takes the parameters that are left, from the registers they are left
# in, gives each one that went the constant every call passed (or 0 when the function never reads it), and returns nothing
# when the compiler made the result go.
IR_DIR = os.path.join(HERE, 'ir')
_IR_DEFINE = re.compile(r'^define (?P<head>[^@]*)@(?P<name>[\w.]+)\((?P<params>.*?)\)[^{]*\{', re.M)


def _ir_split(params):
    out, depth, cur = [], 0, ''
    for ch in params + ',':
        if ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == ',' and depth == 0:
            if cur.strip():
                out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    return out


def _ir_defines(path, cache={}):
    if path not in cache:
        text = open(path, encoding='utf8', errors='replace').read() if os.path.exists(path) else ''
        cache[path] = (text, {m.group('name'): m for m in _IR_DEFINE.finditer(text)})
    return cache[path]


def compiled_interface(base, name, c_params):
    """None when the IR says nothing (no file, no define); else {'fastcc': bool, 'params': [C names in register order],
    'dropped': {C name: constant (an int) or None (never read: any value)}, 'void': the result went, 'why': trouble or None}."""
    text, defines = _ir_defines(os.path.join(IR_DIR, base + '.ll'))
    m = defines.get(name)
    if m is None:
        return None
    head = m.group('head').split()
    c_names = [n for _, n in c_params]
    left = []
    for p in _ir_split(m.group('params')):
        nm = p.split()[-1]
        nm = nm[1:] if nm.startswith('%') else None
        # (LLVM gives a parameter its C name, with a number after it when the name was taken: movex_chain_pick's entry1)
        if nm not in c_names and nm is not None:
            nm = next((c for c in c_names if re.fullmatch(re.escape(c) + r'\d+', nm)), nm)
        left.append(nm)
    out = {'fastcc': 'fastcc' in head, 'params': left, 'dropped': {}, 'void': False, 'why': None}
    ret_words = [w for w in head if w not in ('internal', 'dso_local', 'fastcc', 'noundef', 'signext', 'zeroext', 'hidden')]
    out['void'] = bool(ret_words) and ret_words[-1] == 'void'
    if any(n not in c_names for n in left):
        out['why'] = 'its IR parameters %s are not its C parameters %s' % (left, c_names)
        return out
    if [n for n in c_names if n in left] != left:
        out['why'] = 'its IR parameters %s are in another order than its C parameters %s' % (left, c_names)
        return out
    gone = [n for n in c_names if n not in left]
    if not gone:
        return out
    # The calls as they were before dead-argument elimination: each gone parameter's value at every one.
    before, before_defs = _ir_defines(os.path.join(IR_DIR, base + '.before_dae.ll'))
    mb = before_defs.get(name)
    if mb is None:
        out['why'] = 'parameters %s went, and the IR before that is not there' % gone
        return out
    before_names = [p.split()[-1].lstrip('%') for p in _ir_split(mb.group('params'))]
    if before_names != c_names:
        out['why'] = 'its IR parameters before dead-argument elimination %s are not its C parameters %s' % (before_names, c_names)
        return out
    values = {n: set() for n in gone}
    for call in re.finditer(r'\bcall [^@\n]*@%s\((.*)\)' % re.escape(name), before):
        args = _ir_split(call.group(1))
        if len(args) != len(c_names):
            out['why'] = 'a call with %d arguments for %d parameters' % (len(args), len(c_names))
            return out
        for n in gone:
            values[n].add(args[c_names.index(n)].split()[-1])
    fn_text = before[mb.start():before.find('\n}\n', mb.start())]
    for n in gone:
        vs = values[n]
        used = re.search(r'%' + re.escape(n) + r'\b', fn_text[fn_text.index('{'):]) is not None
        if len(vs) == 1 and re.fullmatch(r'-?\d+', next(iter(vs))):
            out['dropped'][n] = int(next(iter(vs)))
        elif len(vs) == 1 and next(iter(vs)) == 'null':
            out['dropped'][n] = 0
        elif not used:
            out['dropped'][n] = None
        else:
            out['why'] = 'parameter %s went, and its callers pass %s' % (n, ', '.join(sorted(vs)) or 'nothing')
            return out
    return out


# ---- Narrow parameters (2026-11-13, modnative5: the user's rule (1) of 2026-11-13) ----
# "Narrow C-typed parameters get only properly extended values." A parameter the C declares u8 / s8 / u16 / s16 / bool comes in
# a 32-bit register (or stack word), and the mod's compiler takes it as the calling rules say the caller left it: extended from
# its own width, zero- or sign- as its type is (LLVM's zeroext / signext on the parameter in the mod's IR), never with other
# high bits. variant_base_of compares a0 with 0x10A whole; a random 0xDA0817DC there is no call the game can make. So the
# tester extends such a parameter's random value as its type says before each test (fuzz_main.cpp, use_nm_narrow), for this
# function's own parameters only. The kinds, by where each comes in: 'zb' 'sb' 'zh' 'sh' (8 / 16 bits, zero / sign), 'z1' (a
# bool: 0 or 1).
NARROW_KINDS = {('i8', 'zeroext'): 'zb', ('i8', 'signext'): 'sb', ('i16', 'zeroext'): 'zh', ('i16', 'signext'): 'sh', ('i1', 'zeroext'): 'z1'}


def narrow_inputs(base, name, params, regs):
    """[[where, kind]] for each of the function's parameters (the ones the compiler left) the mod's IR gives a narrow type with
    zeroext / signext: where is the context field the entry reads it from ('r4', ..., 'stack16')."""
    text, defines = _ir_defines(os.path.join(IR_DIR, base + '.ll'))
    m = defines.get(name)
    if m is None:
        return []
    ir = {}
    for p in _ir_split(m.group('params')):
        words = p.split()
        nm = words[-1][1:] if words[-1].startswith('%') else None
        if nm is not None:
            ir[nm] = words
    names = [n for _, n in params]
    out = []
    for (t, n), reg in zip(params, regs):
        words = ir.get(n) or next((w for k, w in ir.items() if re.fullmatch(re.escape(n) + r'\d+', k)), None)
        if not words:
            continue
        ext = 'zeroext' if 'zeroext' in words else 'signext' if 'signext' in words else None
        kind = NARROW_KINDS.get((words[0], ext))
        if kind:
            out.append([reg, kind])
    return out


def narrow_results(base, calls):
    """(PROPOSED, 2026-11-13, modnative5: rule (1) for results -- not approved) {stand-in symbol: kind} for each call whose
    callee the file's IR declares or defines with a narrow, extended result (zeroext / signext i8, i16, i1): the mod's compiler
    takes that result as extended, as it takes a narrow parameter."""
    text, _ = _ir_defines(os.path.join(IR_DIR, base + '.ll'))
    out = {}
    for cname, (sym, kind) in calls.items():
        m = re.search(r'^(?:declare|define)\b[^@\n]*\b(zeroext|signext) (i8|i16|i1) @%s\(' % re.escape(cname), text, re.M)
        if m and NARROW_KINDS.get((m.group(2), m.group(1))):
            out[sym] = NARROW_KINDS[(m.group(2), m.group(1))]
    return out


_PORT_MASKS = None


def port_import_masks():
    """(2026-11-14, NM_DECLARED_PARAMS -- APPROVED by the user for 0.84, and by the independent review (scratch/agents/reviewnm084,
    2026-11-15; REVIEW_declared_params.txt)) {import name: a0-a3 mask} for each host import the port implements -- the name it registers
    (register_base_export("<name>", <fn>)) and the parameters its C++ reads (_arg<k, T>(rdram, ctx)). Left out (so the stand-in
    keeps the analysis's mask): one that reads a register or a stack parameter any other way, passes rdram/ctx on to another
    function, or reads a parameter past a3."""
    global _PORT_MASKS
    if _PORT_MASKS is not None:
        return _PORT_MASKS
    _PORT_MASKS = {}
    src = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp', 'src')
    texts = []
    for root, _, files in os.walk(src):
        for fn in files:
            if fn.endswith('.cpp') and fn not in ('lifted.cpp', 'mod_native.cpp', 'mod_converted.cpp'):
                texts.append(open(os.path.join(root, fn), encoding='utf8', errors='replace').read())
    text = '\n'.join(texts)
    for name, fn in re.findall(r'register_base_export\("(\w+)",\s*(\w+)\)', text):
        m = re.search(r'\b(?:void|static void)\s+%s\s*\(\s*uint8_t\s*\*\s*rdram\s*,\s*recomp_context\s*\*\s*ctx\s*\)\s*\{' % re.escape(fn), text)
        if not m:
            continue
        depth, k = 0, m.end() - 1
        while k < len(text):
            depth += {'{': 1, '}': -1}.get(text[k], 0)
            if depth == 0:
                break
            k += 1
        body = text[m.end():k]
        # (2026-11-15, the review of 0.84 (reviewnm084), fixes (a) and (b): an import that takes a float or a double (_arg<k, float>)
        #  is left out -- its registers are not a0-a3 -- and so is one whose body uses ctx in any way but _arg<k, T>(rdram, ctx) and
        #  _return<T>(ctx, v): handing ctx to a helper, reading ctx->rN or ctx->fN, a macro. What is left reads its arguments only
        #  through _arg.)
        if re.search(r'_arg<\s*\d+\s*,\s*(?:float|double|f32|f64)\b', body):
            continue
        rest = re.sub(r'_arg<\s*\d+\s*,[^>]*>\s*\(\s*rdram\s*,\s*ctx\s*\)', '', body)
        rest = re.sub(r'_return<[^>]*>\s*\(\s*ctx\s*,', '(', rest)
        if re.search(r'\bctx\b', rest):
            continue
        ks = [int(x) for x in re.findall(r'_arg<\s*(\d+)\s*,', body)]
        if any(x > 3 for x in ks):
            continue
        _PORT_MASKS[name] = sum(1 << x for x in set(ks))
    return _PORT_MASKS


_SCALAR_ALIGN = {'void': 1, 'char': 1, 'u8': 1, 's8': 1, 'unsigned char': 1, 'signed char': 1, 'bool': 1, '_Bool': 1,
                 'u16': 2, 's16': 2, 'short': 2, 'unsigned short': 2, 'signed short': 2,
                 'u32': 4, 's32': 4, 'int': 4, 'unsigned': 4, 'unsigned int': 4, 'signed int': 4, 'long': 4, 'unsigned long': 4,
                 'float': 4, 'f32': 4, 'size_t': 4,
                 'u64': 8, 's64': 8, 'double': 8, 'f64': 8, 'long long': 8, 'unsigned long long': 8}


def pointee_alignment(inner, text, depth=0):
    """(2026-11-15, the aligned rule's fix) The alignment of a native parameter's pointee type, from its name and the candidate's
    own C++ (its file's typedefs and structures, as native.py wrote them): 1, 2, 4 or 8, or None when it cannot be worked out."""
    inner = re.sub(r'\b(?:const|volatile|struct|union)\b', ' ', inner).strip()
    inner = re.sub(r'\s+', ' ', inner)
    if depth > 8:
        return None
    if inner.startswith('NPtr<') or inner.endswith('*') or inner.startswith(('P<', 'NArr<')):
        return 4
    if inner in _SCALAR_ALIGN:
        return _SCALAR_ALIGN[inner]
    if not re.fullmatch(r'\w+', inner):
        return None
    if re.search(r'\benum\s+%s\b|typedef\s+enum\b[^;{]*\{[^}]*\}\s*%s\s*;' % (inner, inner), text):
        return 4
    m = re.search(r'typedef\s+([\w\s<>*]+?)\s+%s\s*;' % inner, text)
    if m and '{' not in m.group(1):
        return pointee_alignment(m.group(1), text, depth + 1)
    m = re.search(r'(?:typedef\s+(?:struct|union)\s*\w*\s*\{(?P<b1>[^{}]*)\}\s*%s\s*;)|(?:(?:struct|union)\s+%s\s*\{(?P<b2>[^{}]*)\})' % (inner, inner), text)
    if not m:
        return None
    body = m.group('b1') if m.group('b1') is not None else m.group('b2')
    best = 1
    for d in body.split(';'):
        d = re.sub(r'\[[^\]]*\]', '', d).split('=')[0].split(':')[0].strip()
        if not d:
            continue
        if 'NPtr<' in d or '*' in d:
            al = 4
        else:
            first = d.split(',')[0].strip()
            names = re.findall(r'[A-Za-z_]\w*', first)
            if len(names) < 2:
                return None
            al = pointee_alignment(' '.join(names[:-1]), text, depth + 1)
        if al is None:
            return None
        best = max(best, al)
    return best


def ir_argument_words(base, name):
    """(2026-11-15, the review of 0.84 (reviewnm084), fix (c) of NM_DECLARED_PARAMS; decl_vs_ir.py's way) How many of a0-a3 the
    compiled function's argument registers are, from its definition in the build's optimised IR (ir/<file>.ll) -- what the code
    can read, not what the C's text says: under o32 each i32 / i16 / i8 / i1 / pointer one word, an i64 two (even-aligned).
    None -- the stand-in keeps the analysis's mask -- for: no definition there; the fast convention (fastcc, or any calling
    convention but the default); a float or double (they go in f12 / f14); a structure, array, byval or sret argument;
    varargs; or more than four words."""
    text, defines = _ir_defines(os.path.join(IR_DIR, base + '.ll'))
    m = defines.get(name)
    if m is None:
        return None
    head = m.group('head')
    if re.search(r'\b(?:fastcc|coldcc|cc\s*\d+|\w+_cc)\b', head):
        return None
    words = 0
    for p in _ir_split(m.group('params')):
        p = p.strip()
        if not p:
            continue
        if p == '...' or re.search(r'\b(?:byval|sret|inalloca|preallocated)\b', p) or p.startswith(('{', '[', '<', '%')) or \
                re.match(r'(?:float|double|half|fp128|x86_fp80)\b', p):
            return None
        if re.match(r'i64\b', p):
            words += words % 2
            words += 2
        elif re.match(r'(?:i32|i16|i8|i1|ptr)\b', p):
            words += 1
        else:
            return None
    return words if words <= 4 else None


def declared_masks(base, calls, inv):
    """(2026-11-14, NM_DECLARED_PARAMS) {stand-in symbol: a0-a3 mask} for the callees whose parameters are known for certain:
    one of the mod's own functions (the parameters of its definition in its own file, all integers or pointers, at most four,
    not of the fast convention), and a host import (port_import_masks). The tester's stand-in then notes and compares only
    those registers (lift.py stand_in). Game functions keep the analysis's mask: the mod's declaration of one is not its code."""
    out = {}
    pm = port_import_masks()
    for cname, (sym, kind) in calls.items():
        if sym in FAST_STUBS:
            continue
        if kind == 'import':
            if cname in pm:
                out[sym] = pm[cname]
            continue
        if kind != 'mod' or not sym.startswith('modfn_'):
            continue
        r = inv.get(sym[len('modfn_'):])
        if r is None:
            continue
        words = ir_argument_words(r['object'], r['name'])
        if words is None:
            continue
        out[sym] = (1 << words) - 1
    return out


def narrow_args(base, calls):
    """(2026-11-14, modnative7, rule NM_NO_LOWBITS) {stand-in symbol: [[GPR, kind]]} for each call
    whose callee the file's IR declares or defines with a narrow, extended parameter (zeroext / signext i8, i16, i1) among its
    first four, in a0-a3 by o32 (every parameter before it an integer or a pointer; not a fast-convention callee, whose
    registers fast_stub gives) AND whose own code ignores the bits above them (narrow_proof.callee_ignores_high): the tester's
    stand-in compares only that argument's low 8 / 16 bits (lift.py stand_in). (Approved by the user 2026-11-14, strict form.)"""
    import narrow_proof
    text, _ = _ir_defines(os.path.join(IR_DIR, base + '.ll'))
    out = {}
    for cname, (sym, kind) in calls.items():
        if sym in FAST_STUBS:
            continue
        m = re.search(r'^(?:declare|define)\b[^@\n]*@%s\((.*)\)' % re.escape(cname), text, re.M)
        if not m:
            continue
        regs = []
        for i, p in enumerate(_ir_split(m.group(1))[:4]):
            words = p.split()
            if not words or words[0] in ('float', 'double') or words[0].startswith(('{', '[', '<')):
                break
            ext = 'zeroext' if 'zeroext' in words else 'signext' if 'signext' in words else None
            k = NARROW_KINDS.get((words[0], ext))
            # (strict: only where the callee's own code is proved to ignore the bits above -- narrow_proof.py)
            if k and narrow_proof.callee_ignores_high(sym, 4 + i, 16 if k in ('zh', 'sh') else 8):
                regs.append([4 + i, k])
        if regs:
            out[sym] = regs
    return out


def interface_of(base, name):
    """compiled_interface for the function's own C signature (None if no IR)."""
    sig = signature(base, name)
    return compiled_interface(base, name, sig[1]) if sig else None


def parameter_registers(base, name, params):
    """Where each parameter comes in, as a context field (r4, f12, ...): o32 for a function other files can call, LLVM's
    fastcc for an internal one (is_internal). The analysis of the original (analysis_mod.json's 'use') must agree: see why_not.
    (2026-11-13: params are the ones the compiler left, compiled_interface; and fastcc only where the IR says so -- flush_cb
    is internal but its address is taken, so it keeps the o32 rules.)"""
    out = []
    iface = interface_of(base, name)
    if (iface['fastcc'] if iface else is_internal(base, name)):
        ints = [4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 3]
        fl = 0
        for t, n in params:
            if t in ('float', 'f32'):
                out.append('f%d' % (2 * fl))
                fl += 1
            else:
                out.append('r%d' % ints.pop(0))
        return out
    fl_ok, fl = True, 0
    for i, (t, n) in enumerate(params):
        if t in ('float', 'f32') and fl_ok and i < 2:
            out.append('f%d' % (12 + 2 * i))
        elif i >= 4:
            # (2026-11-13, modnative2) a fifth parameter on: o32 passes it in the caller's frame, at sp + 16 + 4 per one after
            #  the fourth (the form lift.py's own conversions read them in)
            out.append('stack%d' % (16 + 4 * (i - 4)))
        else:
            fl_ok = False
            out.append('r%d' % (4 + i))
    return out


def source(wt, reg):
    """How the entry reads a parameter of type wt from where it comes in (a register field, or stackN)."""
    if reg.startswith('stack'):
        word = '(u32)*P<u32>((u32)ctx->r29 + %d)' % int(reg[5:])
        return '__builtin_bit_cast(f32, %s)' % word if wt == 'f32' else 'lift_arg<%s>(%s)' % (wt, word)
    if wt == 'f32' and reg.startswith('r'):
        # (2026-11-14, modnative6) a float o32 passes in an integer register: its bits there
        return '__builtin_bit_cast(f32, (u32)ctx->%s)' % reg
    return 'ctx->%s.fl' % reg if wt == 'f32' else 'lift_arg<%s>(ctx->%s)' % (wt, reg)


def register_name(field):
    return GPR_NAMES.get(int(field[1:]), field) if field.startswith('r') else field


_ANALYSIS = None


def original_reads(a):
    global _ANALYSIS
    if _ANALYSIS is None:
        _ANALYSIS = json.load(open(os.path.join(LIFT, 'analysis_mod.json')))
    return set(_ANALYSIS.get('modfn_' + a, {}).get('own', [])) - {'sp', 'ra', 'zero'}


def tight(t):
    # (a type as one reads it: NPtr<char>, not NPtr < char >)
    return re.sub(r' ?([<>]) ?', lambda m: m.group(1), t).strip()


def strip_attributes(ret):
    return re.sub(r'__attribute__\s*\(\(.*?\)\)', '', ret).strip()


FLOAT = ('float', 'f32')


# ---- Internal functions tested through their callers (2026-11-13, modnative5: the user's rule (5) of 2026-11-13) ----
# "Test internal functions through their callers." An internal (static) function of the mod whose behaviour is only defined
# for what its callers give it -- or whose result its callers rely on being in range -- is not tested alone with random
# parameters, nor replaced by a stand-in with random results in its callers' tests: in a caller's test it runs for real on both
# sides, the native function in the native code (no stub: the file's code calls it as written) and its own Stage 1 original
# in the original's (modrep_natthru_<A> in nat_originals.c, which lift.py runs as a piece of the caller's original, its
# instructions numbered with the caller's: native['thru']). Its own calls go to stand-ins as the caller's do. Each one is
# listed here with why; NM_NO_THRU=1 makes every one a stand-in again, as before.
THRU = {
    # (not ext_of_id: run for real in its callers' tests it walks folder_n -- private data the tester gives any value, the game
    #  at most FOLDER_MAX -- so most tests ran past the access limit and roster_id_to_ord's took 5 minutes. Its callers' use of
    #  its result is made well-defined in roster.c instead, the user's rule (5)'s other way: mod_src_roster.c.diff)
    'moves_whose': 'roster.c: its result is a WHOSE_ value, which cells_on compares unsigned',
    'ext_whose': 'roster.c: as moves_whose',
    'walls_take': 'arena.c: every caller passes an arena in range, and the compiler left out the test of it',
    'panel_apply': 'tron.c: every caller passes a panel in range (patch[i])',
    'ext_file': 'files.c: every caller passes a file at or past FILES_N, and the compiler left out that test',
    'head_on_screen': 'arcctl.c: writes its two results through pointers into the caller\'s frame (a stand-in reads a window round them)',
    # (2026-11-14, modnative6) its one caller, emit_list, passes a char of a string -- signed, so at most 127 -- and the compiler
    #  left out emit_char's `c > 127` test (its original tests c < 32 alone); alone it is given any int.
    'emit_char': 'draw.c: its caller passes a char of a string (at most 127), and the compiler left out the c > 127 test',
    # (2026-11-14, modnative6: not menu_change, though its original has no `next < 0` test -- run through menu_choose and
    #  survival_open it made both DIFFERENT, calls@7, in run m6d: not understood yet)
}
use_thru = lambda: os.environ.get('NM_NO_THRU') != '1'
# (2026-11-14: APPROVED by the user for 0.84, from the Linux part 2's proposal -- but NOTHING COUNTS from it until an
#  independent review approves it: REVIEW_pure_thru.txt. Off unless NM_PURE_THRU=1.) Rule (5) extended to an internal function
#  the mod's compiler proved PURE (no memory, no calls: Newton's square roots) and relied on in a caller, or inlined at some of a
#  caller's call sites only: hc_camera's first hc_sqrt call has constant operands, so it was worked out at compile time and is
#  never made; story_cam_cut's two cam_sqrt calls are made in the other order; commentary_frame calls say() at some sites and
#  has its body inlined at others. As stand-ins every call is noted in C's order, so the callers differ at their first call.
#  With the switch such a callee runs for real in its callers (as THRU's do) AND stays a candidate of its own.
PURE_THRU = {
    'hc_sqrt': "tron.c: pure; hc_camera's first call (constant operands) was folded at compile time",
    'cam_sqrt': "story.c: pure; story_cam_cut's two calls were reordered round its early return",
    'say': "commentary.c: inlined at some of commentary_frame's call sites, called at others",
} if os.environ.get('NM_PURE_THRU') == '1' else {}


def thru_calls(a, r, inv, seen=None):
    """(calls, thru): the calls a candidate's native code makes to stand-ins -- callees(), less the internal functions run
    through it (THRU, same file, internal), plus theirs, recursively -- and the addresses of those run through it, in order."""
    calls = callees(a, r, inv)
    thru = []
    if not use_thru():
        return calls, thru
    seen = set() if seen is None else seen
    for cname, (sym, kind) in list(calls.items()):
        # (2026-11-14, modnative7, approved by the user 2026-11-14, NM_NO_MEMCPY=1 off: a call to memcpy the C never makes -- the compiler's way of
        #  copying a structure, so memcpy is not declared in the caller's file -- runs through the caller as rule (5)'s do,
        #  though memcpy is util.c's: the native code copies the structure itself, with no call)
        made_copy = rule_memcpy() and kind == 'mod' and cname == 'memcpy' and declared_signature(r['object'], cname) is None
        if kind != 'mod' or (cname not in THRU and cname not in PURE_THRU and not made_copy):
            continue
        ca = sym[len('modfn_'):]
        cr = inv.get(ca)
        if cr is None or (not made_copy and (cr['object'] != r['object'] or not is_internal(r['object'], cname))):
            continue
        del calls[cname]
        if ca in seen:
            continue
        seen.add(ca)
        thru.append(ca)
        sub, sub_thru = thru_calls(ca, cr, inv, seen)
        for k, v in sub.items():
            if k != r['name']:
                calls.setdefault(k, v)
        thru += [x for x in sub_thru if x not in thru]
    return calls, thru


def callees(a, r, inv):
    """{C name: (the tester's symbol, kind)} for each call the original makes ('game', 'import', 'mod')."""
    out = {}
    for c in r['calls_game']:
        out[c] = (modrep_game_name(c), 'game')
    for c in r['calls_import']:
        out[c] = (c, 'import')
    for c in r['calls_mod']:
        if c not in inv:
            # (2026-11-13, modnative2) One of the mod's copies of a game function (mod.S's edited copies, .recomp_patch), which
            #  the mod's C calls by the game function's name and the mod's linker binds to the copy: the call goes to the
            #  tester's stand-in for modrep's translation of that copy, modrep_func_X -- a translated function with its own
            #  analysis, so the stand-in notes what the copy reads. The original's direct call is renamed to the same
            #  (original_text). One modrep doesn't list, or that the mod also hooks, stays refused (why_not).
            rec = copy_record(c)
            if rec is None or rec['hooked_entry'] or rec['hooked_return']:
                out['?copy_' + c] = ('modfn_' + c, 'copy')
            else:
                out[rec['replaces']] = (rec['name'], 'game')
            continue
        out[inv[c]['name']] = ('modfn_' + c, 'mod')
    return out


_COPIES = None


def copy_record(c):
    """modrep's record (modrep_index.json) of the mod's copy at address c (hex text), or None."""
    global _COPIES
    if _COPIES is None:
        _COPIES = {e['mod_vram']: e for e in json.load(open(os.path.join(LIFT, 'modrep', 'out', 'modrep_index.json')))}
        names = set(json.load(open(os.path.join(LIFT, 'analysis.json'))))
        _COPIES = {k: e for k, e in _COPIES.items() if e['name'] in names}     # (only those with an analysis: the stand-in's)
    return _COPIES.get(int(c, 16))


def modrep_game_name(c):
    """The game function's name as the tester's translation calls it (modrep.game_names: osRecvMesg -> osRecvMesg_recomp)."""
    sys.path.insert(0, LIFT)
    import modrep
    return re.match(r'(\w+)\(', modrep.game_names('%s(rdram, ctx);' % c)).group(1)


def why_not_callee(base, cname):
    """None if a call to cname can go to the tester's stand-in through a typed stub (lift_call), else why not."""
    sig = declared_signature(base, cname)
    if sig is None:
        return 'a callee not declared in its file (%s)' % cname
    ret, params, internal = sig
    if any(t.startswith('struct') or (t[:1].isupper() and not t.startswith('NPtr<')) or t in ('double', 'f64', 'u64', 's64', 'long long', 'unsigned long long', '...')
           for t, _ in params) or ret in ('double', 'f64', 'u64', 's64', 'long long', 'unsigned long long') or (ret[:1].isupper() and not ret.startswith('NPtr<')):
        return 'a callee taking or giving a structure or a 64-bit value (%s)' % cname
    if internal:
        # (2026-11-13: the parameters the compiler left it, compiled_interface -- the stub passes those)
        iface = compiled_interface(base, cname, params)
        if iface and iface['why']:
            return 'a callee whose interface the compiler changed in a way a stub cannot follow (%s: %s)' % (cname, iface['why'])
        if iface:
            params = [(t, n) for t, n in params if n in iface['params']]
    if internal and (any(t in FLOAT for t, _ in params) or len(params) > 4):
        # (2026-11-13, modnative5: the user's rule (3) -- the stub puts them in the fast convention's registers, and the tester's
        #  stand-in notes those registers: fast_stub. Only while they all fit in registers: 14 integer ones, 10 float ones.)
        ints = [t for t, _ in params if t not in FLOAT]
        if not use_fast() or len(ints) > 14 or len(params) - len(ints) > 10:
            return "a callee with the mod compiler's fast calling convention and floats or over four parameters (%s)" % cname
    floats = [t for t, _ in params if t in FLOAT]
    fast = internal and use_fast() and (any(t in FLOAT for t, _ in params) or len(params) > 4)   # (fast_stub: any order)
    if floats and not fast and (any(t not in FLOAT for t, _ in params[:len(floats)]) or len(floats) > 2):
        # (2026-11-14, modnative6) o32 puts a float that is not among the first two parameters, or comes after an integer, in
        #  the integer register of its slot (a0-a3, its bits): o32_stub does that by hand, while all of them fit in a0-a3.
        if len(params) > 4 or os.environ.get('NM_NO_O32_STUB') == '1':
            return 'a callee with a float parameter after an integer (%s)' % cname
    return None


WHOLE_INLINE = {}      # (address -> the callees J compiles into it)
WIDE = ('u64', 's64', 'unsigned long long', 'long long')


def wide_ok(sig):
    """(K) A signature o32 can pass with 64-bit integers among its parameters, all in a0-a3: [(type, first register)] or None."""
    if not sig:
        return None
    ret, params, internal = sig
    if internal or ret in WIDE or ret in ('double', 'f64') or not any(t in WIDE for t, _ in params):
        return None
    out, slot = [], 0
    for t, n in params:
        if t in WIDE:
            slot += slot % 2
            out.append((t, 4 + slot))
            slot += 2
        elif t in ('float', 'f32', 'double', 'f64') or t.startswith('struct') or (t[:1].isupper() and not t.startswith('NPtr<')):
            return None
        else:
            out.append((t, 4 + slot))
            slot += 1
    return out if slot <= 4 else None


def wide_stub(cname, symbol, sig):
    """(K) A stub filling o32's registers by hand: a 64-bit integer in its pair, the high word first."""
    ret, params, _ = sig
    sets = []
    for i, (t, reg) in enumerate(wide_ok(sig)):
        if t in WIDE:
            sets.append('nm_ctx->r%d = (gpr)(int64_t)(s32)(u32)((u64)p%d >> 32); nm_ctx->r%d = (gpr)(int64_t)(s32)(u32)p%d;' % (reg, i, reg + 1, i))
        elif t.startswith('NPtr<'):
            sets.append('nm_ctx->r%d = (gpr)(int64_t)(s32)p%d.a;' % (reg, i))
        else:
            sets.append('nm_ctx->r%d = (gpr)(int64_t)(s32)p%d;' % (reg, i))
    back = '' if ret == 'void' else ' return %s((u32)nm_ctx->r2);' % ret if ret.startswith('NPtr<') else ' return (%s)(u32)nm_ctx->r2;' % ret
    ps = ', '.join('%s p%d' % (t, i) for i, (t, _) in enumerate(params))
    return ('static %s %s(%s) {   // (o32, a 64-bit integer in a register pair: the stand-in)' + chr(10) +
            '    %s ::%s(lift_rdram, nm_ctx);%s' + chr(10) + '}') % (ret, cname, ps, ' '.join(sets), symbol, back)


def block_declares(body, c):
    """(2026-11-14, modwhole, G) Whether a function's body declares c itself -- `u16 roster_stand_in(u32 id, int nth);`: a return
    type that is no keyword, and parameters each a type and a name (or none, or void) -- not a statement that calls it."""
    for m in re.finditer(r'(?:[;{}]|\*/)\s*((?:const\s+)?[A-Za-z_][\w<> ]*?)\s*\b%s\s*\(([^;{}()]*)\)\s*;' % re.escape(c), body):
        head, params = m.group(1).split(), m.group(2).strip()
        if not head or head[0] in ('return', 'else', 'do', 'case', 'goto', 'sizeof', 'if', 'while', 'for', 'switch'):
            continue
        if params in ('', 'void') or all(len(p.replace('*', ' * ').split()) >= 2 for p in params.split(',')):
            return True
    return False


def called(text, c):
    """(2026-11-14, modwhole) Whether the text calls c: `c(` anywhere but in a declaration of it (a type, then c, then typed
    parameters or none, then ';')."""
    for m in re.finditer(r'\b%s\s*\(' % re.escape(c), text):
        line_start = text.rfind('\n', 0, m.start()) + 1
        before = text[line_start:m.start()]
        end = text.find(';', m.end())
        params = text[m.end():end] if end >= 0 else ''
        decl = re.fullmatch(r'\s*(?:(?:extern|static|inline)\s+)*(?:const\s+)?[A-Za-z_][\w<> *]*\s+', before) and \
            not re.match(r'\s*(?:return|else|do|case|goto)\b', before) and re.fullmatch(r'[^;{}()]*\)\s*', params or '') and (
            params.strip() in (')', 'void)') or all(len(x.replace('*', ' * ').split()) >= 2 for x in params.rstrip(') \n').split(',')))
        if not decl:
            return True
    return False


def extra_stubs(a, r, inv, base, out, calls):
    """(2026-11-14, modwhole; NM_WHOLE=1) {name: stand-in symbol} for each function the kept code calls that the text only declares
    (no body, no stub): a call the original never makes. Its declaration in `out` becomes a stub to its stand-in -- one of the mod's
    functions by its address, an import, or a game function -- or it stays as it is when it is none of those."""
    text = '\n'.join(out)
    defined = set(re.findall(r'\b(\w+)\s*\([^;{}()]*(?:\([^)]*\)[^;{}()]*)*\)\s*\{', text))
    by_name = {row['name']: addr for addr, row in inv.items()}
    imports = {c for row in inv.values() for c in row['calls_import']}
    games = {c for row in inv.values() for c in row['calls_game']}
    done = {}
    for k, piece in enumerate(out):
        m = re.fullmatch(r'\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w<> ]*?\b(\w+)\s*\(([^;{}]*)\)\s*;\s*', piece)
        if not m or m.group(1) in defined or m.group(1) in calls or m.group(1) == r['name'] or m.group(1) in done:
            continue
        c = m.group(1)
        if not called(text.replace(piece, ''), c):
            continue        # (declared, never called)
        sym = ('modfn_' + by_name[c]) if c in by_name else c if c in imports else modrep_game_name(c) if (c in games or re.fullmatch(r'func_[0-9A-F]{8}', c)) else None
        sig = declared_signature(base, c)
        if sym is None or sig is None:
            continue
        out[k] = '\n' + stub(c, sym, sig, base).replace('(the original calls it: the stand-in)', '(the original never makes this call: its stand-in)')
        done[c] = sym
    # (and those declared only inside a body -- `extern int media_folder_video(u32 id);`: the stub before the first body kept)
    first = next((k for k, piece in enumerate(out) if re.match(r'\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w<> ]*?\b\w+\s*\([^;{}]*\)\s*\{', piece)), None)
    text = '\n'.join(out)
    defined = set(re.findall(r'\b(\w+)\s*\([^;{}()]*(?:\([^)]*\)[^;{}()]*)*\)\s*\{', text))
    more = []
    # (the brace depth at each place of the text, literals and comments left out: a declaration at depth 0 is the file's)
    depth, at, d = [0] * (len(text) + 1), 0, 0
    for m_ in re.finditer(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|/\*.*?\*/|//[^\n]*|[{}]', text, re.S):
        for q in range(at, m_.start()):
            depth[q] = d
        at = m_.start()
        if m_.group(0) == '{':
            d += 1
        elif m_.group(0) == '}':
            d -= 1
    for q in range(at, len(text) + 1):
        depth[q] = d
    for m in re.finditer(r'\bextern\s+[A-Za-z_][\w<> ]*?\b(\w+)\s*\(([^;{}()]*)\)\s*;', text):
        c = m.group(1)
        if first is None or c in defined or c in calls or c in done or c == r['name'] or depth[m.start()] == 0:
            continue        # (only one declared inside a body: a file-scope one is H's, above)
        if not called(text[:m.start()] + text[m.end():], c):
            continue        # (declared, never called)
        sym = ('modfn_' + by_name[c]) if c in by_name else c if c in imports else modrep_game_name(c) if (c in games or re.fullmatch(r'func_[0-9A-F]{8}', c)) else None
        sig = declared_signature(base, c)
        if sym is None or sig is None:
            continue
        more.append(stub(c, sym, sig, base).replace('(the original calls it: the stand-in)', '(the original never makes this call: its stand-in)'))
        done[c] = sym
    if more:
        out.insert(first, '\n' + '\n'.join(more))
    return done


def frame_size(a):
    """The original's frame (its first `addiu $sp, $sp, -N`), 0 if it makes none."""
    text = original_text(a) or ''
    m = re.search(r'// 0x[0-9A-F]{8}: addiu +\$sp, \$sp, -0x([0-9A-F]+)', text)
    return int(m.group(1), 16) if m else 0


def constant_pools(cache=[]):
    """(lo, hi): the compiler's unnamed constants in the mod build -- from its first .rodata.cst / .rodata.str section to its first
    code (mod.map). (2026-11-14, modnative7: read from the build's map; it was the 0.81 build's 0x8109CDE8..0x8109EF04, written
    in, which the 0.82 mod's moved to 0x810920A8..0x81094A00.)"""
    if not cache:
        lo = hi = None
        for line in open(os.path.join(MOD_DIR, 'build', 'mod_six', 'mod.map'), encoding='utf8', errors='replace'):
            m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +[0-9a-f]+ +\d+ \.(rodata\.(?:cst|str)|text)\S*\s*$', line)
            if m:
                a = int(m.group(1), 16)
                if m.group(2) == 'text':
                    hi = a if hi is None else min(hi, a)
                elif lo is None:
                    lo = a
        cache.append((lo or 0, hi or 0))
    return cache[0]


# (2026-11-14, modwhole: the user's decision of 2026-11-14, "B now, C later" -- the whole mod native, a GAMEPLAY tier beside the
#  proved one) NM_WHOLE=1: `list` and `put` make an entry also for a function refused only for the tester's sake, so the game can
#  link it natively and the gameplay A/B runs can verify it (tools/aot/mod_native.py release --whole). Each such refusal is noted
#  in the entry (native.whole_refusals) and the work goes on; the entry is still compiled alone by `check`, and an entry the
#  tester cannot run at all (its original stops for good) is marked whole_only: no original is written for it and it is never
#  proved. Nothing changes without the switch. The refusals it lets through:
#    - does not compile (the census compiles native.py's text of the whole file; the candidate's own text -- with its locals in
#      the original's frame, prove_gen.py frame_arrays -- may compile: `check` decides);
#    - tested through its callers (rule (5)): an entry of its own, as before rule (5) (the fast convention's registers, rule (3));
#    - its original reads registers its C parameters do not explain (the native code takes what its C declares);
#    - its original stops for good (pause_self): whole_only;
#    - a function's own static variable native.py left unbound (the map has no symbol for it: the compiler folded it, so the
#      original keeps no storage for it): a PC static in the native code.
use_whole = lambda: os.environ.get('NM_WHOLE') == '1'
WHOLE_LETS = ('does not compile', 'tested through its callers', 'its original reads registers its C parameters do not explain',
              'its original stops for good', "a function's own static variable")
WHOLE_ONLY = ('its original stops for good',)
WHOLE_NOTES = {}        # (address -> the refusals NM_WHOLE let through: the entry's native.whole_refusals)


def why_not(a, r, inv, census, data, private):
    """None if an entry can be made for the function now, else why not. (NM_WHOLE=1: the refusals WHOLE_LETS are noted in
    WHOLE_NOTES[a] and the checks go on.)"""
    WHOLE_NOTES.pop(a, None)
    w = why_not_tester(a, r, inv, census, data, private)
    while w is not None and use_whole() and w.startswith(WHOLE_LETS):
        WHOLE_NOTES.setdefault(a, []).append(w)
        w = why_not_tester(a, r, inv, census, data, private, let=tuple(WHOLE_NOTES[a]))
    return w


def why_not_tester(a, r, inv, census, data, private, let=()):
    """why_not's checks; let: refusals already noted, passed over."""
    def refuse(why):
        return None if why in let else why
    base = r['object']
    if use_thru() and r['name'] in THRU and is_internal(base, r['name']) and refuse('tested through its callers (rule (5))'):
        # (2026-11-13, modnative5: the user's rule (5) -- tested through its callers, never alone: its parameters are what its
        #  callers give it. It counts as proved with a caller's proof that runs it: m5_analyse.py.)
        return 'tested through its callers (rule (5))'
    if r['other_calls']:
        return 'calls through a pointer (not in this batch)'
    if base not in census:
        return 'its file is not compiled'
    calls, thru = thru_calls(a, r, inv)      # (2026-11-13, modnative5: rule (5), internal callees run through it)
    if any(k == 'copy' for _, k in calls.values()):
        return "calls one of the mod's copies of a game function directly (not in this batch)"
    for c in calls:
        w = why_not_callee(base, c)
        if w and use_whole() and w.startswith(('a callee taking or giving a structure', 'a callee whose interface the compiler changed')) \
                and any(k_ == 'function' and n_ == c for k_, n_, _, _ in file_items(base)[1]):
            WHOLE_INLINE.setdefault(a, set()).add(c)     # (2026-11-14, modwhole, J: compiled in, not called)
            continue
        if w and use_whole() and w.startswith('a callee taking or giving a structure') and wide_ok(declared_signature(base, c)):
            continue        # (K: 64-bit integer arguments, a stub fills their register pairs)
        if w:
            return w
    calls = {c: v for c, v in calls.items() if c not in WHOLE_INLINE.get(a, ())}
    # (a call to itself: to its stand-in, as any other -- converted()'s nm_self_<name>, 2026-11-13)
    if 'pause_self(' in (original_text(a) or '') and refuse('its original stops for good (pause_self)'):
        # (2026-11-13: a `j` to itself -- the C's endless loop on a failure -- which the translator writes as pause_self, a
        #  runtime function the tester has none of)
        return 'its original stops for good (pause_self)'
    if isinstance(inline_plan(a, r, inv, calls, thru), str):
        return 'a callee inlined at some calls and called at others, not resolved: ' + inline_plan(a, r, inv, calls, thru)
    names = needed(a, r, inv, calls, thru)
    if not names:
        return 'its definition not found in the preprocessed file'
    errors = [(n, census[base]['functions'].get(n)) for n in names if census[base]['functions'].get(n)]
    if errors and refuse('does not compile: ' + '; '.join(sorted({c for _, cs in errors for c in cs}))):
        return 'does not compile: ' + '; '.join(sorted({c for _, cs in errors for c in cs}))
    # (a function's own static variable: on the N64 it is the mod's data at a place of its own, which the native code would keep
    #  as a PC static nobody gives the test's values -- not bound yet)
    #  (2026-11-13, modnative2: native.py bind_local_statics binds those mod.map names -- a view, or the tester's PC object for
    #  a private one; one it couldn't place is still refused here)
    toks_, its_ = file_items(base)
    for kind, n, x, y in its_:
        if kind == 'function' and n in names:
            body = [t for _, t in toks_[x:y + 1]]
            body = body[body.index('{'):]
            for k, t in enumerate(body):
                if t == 'static' and not (body[k + 1:k + 2] in (['NVar'], ['NArr']) or 'lift_mod_private_' in ' '.join(body[k:k + 40]) or
                                          'NM_PC_CONST' in body[k + 1:k + 4]):
                    if refuse("a function's own static variable (needs binding to its N64 place or the tester's PC object)"):
                        return "a function's own static variable (needs binding to its N64 place or the tester's PC object)"
    if any(census[base]['sizeof_expr'].get(n) for n in names):
        return 'sizeof of an expression (a view would change it): by hand'
    sig = signature(base, r['name'])
    if sig is None:
        return 'no signature'
    ret, params = sig
    ret = strip_attributes(ret)
    internal = is_internal(base, r['name'])
    # (2026-11-13, modnative2: more than four parameters are read from the caller's frame -- parameter_registers)
    # (2026-11-13, modnative3) The interface the compiler gave it: the parameters it left (compiled_interface).
    iface = interface_of(base, r['name'])
    if iface and iface['why']:
        return 'the compiler changed its interface in a way the entry cannot follow: ' + iface['why']
    if iface:
        internal = iface['fastcc']
        params = [(t, n) for t, n in params if n in iface['params']]
    if any(t in ('double', 'f64', 'u64', 's64', 'long long', 'unsigned long long') for t, _ in params) and not (
            use_whole() and wide_ok((ret, params, internal))):
        return '64-bit parameters (not in this first batch)'
    if not internal:
        floats = [t for t, _ in params if t in ('float', 'f32')]
        if floats and any(t not in ('float', 'f32') for t, _ in params[:len(floats)]):
            # (2026-11-14, modnative6: o32 gives such a float its slot's integer register, as its bits -- parameter_registers
            #  names it r5..r7 and source() reads the bits from there; with NM_NO_O32_STUB=1 refused as before)
            if os.environ.get('NM_NO_O32_STUB') == '1' or len(params) > 4:
                return 'float parameters after an integer (o32 puts them in integer registers: not in this first batch)'
    if any(t.startswith('struct') or (t[:1].isupper() and not t.startswith('NPtr<')) for t, _ in params):
        return 'a structure passed by value (not in this first batch)'
    # The registers the original reads before it sets them (analyse.py) must be the parameters' (or fewer: one it ignores).
    regs = {register_name(x) for x in parameter_registers(base, r['name'], params)}     # (the ones left: see above)
    extra = original_reads(a) - regs
    if extra and refuse('its original reads registers its C parameters do not explain: %s' % ', '.join(sorted(extra))):
        return 'its original reads registers its C parameters do not explain: %s' % ', '.join(sorted(extra))
    if ret in ('double', 'f64', 'u64', 's64', 'unsigned long long', 'long long'):
        return '64-bit result (not in this first batch)'
    # The mod's data it uses: private (bound to the tester's PC objects) or a view; a function's own static is not handled yet.
    toks, _ = file_items(base)
    used = set()
    for kind, n, x, y in file_items(base)[1]:
        if kind == 'function' and n in names:
            used |= {t for _, t in toks[x:y + 1]}
    for s in r['data_private']:
        # (a function's own private static: bound in its body by native.py bind_local_statics, 2026-11-13 -- but only those of
        #  the functions here; one of another function's is data this one should not reach)
        if '.' in s and s.split('.')[0] not in names and s.split('.')[0] in {n_ for k_, n_, _, _ in file_items(base)[1] if k_ == 'function'}:
            return "a function's own static among its private data, of a function not compiled with it (%s)" % s
    left = census[base]['stats'].get('left_on_pc', [])
    bad = sorted(n for n in left if n in used and n not in private)
    if bad:
        return 'uses mod data left on the PC that is not private (needs a view type): ' + ', '.join(bad[:4])
    pools = constant_pools()
    unknown = sorted(s for s in r['data_shared'] if s.startswith('?') and not (pools[0] <= int(s[1:], 16) < pools[1]))
    # (2026-11-14, modnative6) A place no symbol names is most often no place the function reaches: the compiler's base for an
    #  index (table[i - 100] made as (table - 400)[i]), inside a function's code or past 32 bits (?180FFFABC). The native code
    #  reaches each object by its name, and the tester compares every access either way, so this is only a first sorting, not
    #  part of the proof: NM_UNNAMED_OK=1 lets such a function through to be compiled and tested (the tester decides).
    if unknown and os.environ.get('NM_UNNAMED_OK') == '1':
        unknown = []
    if unknown:
        return 'reaches mod data no symbol names: ' + ', '.join(unknown[:3])
    return None


_ORIGINALS = {}


def original_text(a):
    """nm_aot_nomercy_arcade_<a> from Stage 1's output, as modrep_nat_<a>, its calls by modrep's names (each read once:
    original_text_uncached)."""
    if a not in _ORIGINALS:
        _ORIGINALS[a] = original_text_uncached(a)
    return _ORIGINALS[a]


def original_text_uncached(a):
    for path in sorted(os.listdir(AOT)):
        if not re.match(r'mod_aot_nomercy_arcade_\d+\.c$', path):
            continue
        text = open(os.path.join(AOT, path), encoding='utf8', errors='replace').read()
        m = re.search(r'^RECOMP_FUNC void nm_aot_nomercy_arcade_%s\(uint8_t\* rdram, recomp_context\* ctx\) \{\n.*?^;\}\n' % a, text, re.M | re.S)
        if m:
            body = m.group(0).replace('nm_aot_nomercy_arcade_%s(' % a, 'modrep_nat_%s(' % a, 1)
            body = re.sub(r'nm_aot_nomercy_arcade_game_funcs\[\d+\]\(rdram, ctx\);(\s*)// (\w+)', r'\2(rdram, ctx);\1// \2', body)
            body = re.sub(r'nm_aot_nomercy_arcade_imports\[\d+\]\(rdram, ctx\);(\s*)// (\w+)', r'\2(rdram, ctx);\1// \2', body)
            body = re.sub(r'nm_aot_nomercy_arcade_([0-9A-F]{8})\(rdram, ctx\)', r'modfn_\1(rdram, ctx)', body)
            # (2026-11-13, modnative2) A direct call to one of the mod's copies of a game function: under modrep's name for
            #  it, as the native code's call goes (callees).
            body = re.sub(r'\bmodfn_([0-9A-F]{8})\(rdram, ctx\)',
                          lambda m: '%s(rdram, ctx)' % copy_record(m.group(1))['name'] if copy_record(m.group(1)) else m.group(0), body)
            return body
    return None


def source_path(r):
    # (the file the mod's build compiled it from: its object in mod.map)
    return 'mod/recomp/hooks_recomp.c' if r['object'] == 'hooks_recomp' else 'mod/src/%s.c' % r['object']


def stub(cname, symbol, sig, base=None):
    """A call the original makes, as the native code makes it: a typed function of the same name whose body is lift_call to
    the tester's stand-in for it (in the game: whatever the loader bound to that name). The mod's C calls it unchanged.
    (2026-11-13: to one of the file's internal functions whose interface the compiler changed -- compiled_interface -- only
    the parameters it left are passed, in the registers they are left in, as the original's call passes them.)"""
    ret, params, internal = sig
    if use_whole() and wide_ok(sig):
        return wide_stub(cname, symbol, sig)        # (2026-11-14, modwhole, K)
    ps = ', '.join('%s p%d' % (t, i) for i, (t, _) in enumerate(params))
    iface = compiled_interface(base, cname, params) if base and internal else None
    keep = range(len(params)) if not iface or iface['why'] else [i for i, (_, n) in enumerate(params) if n in iface['params']]
    kept = [params[i] for i in keep]
    if internal and use_fast() and (iface['fastcc'] if iface else True) and (any(t in FLOAT for t, _ in kept) or len(kept) > 4):
        return fast_stub(cname, symbol, ret, params, keep, base)
    floats = [t for t, _ in kept if t in FLOAT]
    if floats and (any(t not in FLOAT for t, _ in kept[:len(floats)]) or len(floats) > 2) and len(kept) <= 4:
        return o32_stub(cname, symbol, ret, params, keep)
    args = ', '.join('p%d' % i for i in keep)
    call = 'lift_call<%s>(nm_ctx, ::%s)(%s);' % (ret, symbol, args)
    return 'static %s %s(%s) { %s%s }   // (the original calls it: the stand-in)' % (ret, cname, ps, '' if ret == 'void' else 'return ', call)


COMPOUND_PADS = json.load(open(os.path.join(HERE, 'compound_pads.json'))) if os.path.exists(os.path.join(HERE, 'compound_pads.json')) else {}


def compound_padding(fname, text):
    """(2026-11-13, modnative5: the user's rule (6)) The function's compound-literal assignments `x = (T){ ... };`, in order:
    each one compound_pads.json says the original stores whole (its padding as zeros) becomes `nm_assign_whole(x, (T){ ... });`
    (nm_native.h); the others stay as they are. Only for a function the file lists; refused (SystemExit) if the count differs."""
    info = COMPOUND_PADS.get(fname)
    if not info or os.environ.get('NM_NO_PADS') == '1':
        return text
    toks = native.tokenize(text)
    ts = [t for _, t in toks]
    sites = [k for k in range(len(ts) - 3) if ts[k] == '(' and ts[k + 1] == info['struct'] and ts[k + 2] == ')' and ts[k + 3] == '{'
             and ts[k - 1] == '=']
    if len(sites) != len(info['writes_padding']):
        raise SystemExit('%s: %d compound literals, compound_pads.json has %d' % (fname, len(sites), len(info['writes_padding'])))
    for k, whole in zip(sites, info['writes_padding']):
        if not whole:
            continue
        s_ = k - 2
        while ts[s_] not in (';', '{', '}', ')', ':') or (ts[s_] == ')' and ts[s_ + 1] == '{'):
            s_ -= 1
        # (the statement's start: after `;`, `{`, `}`, or an `if (...)` / `else`)
        lhs_start = s_ + 1
        if ts[lhs_start] == 'else':
            lhs_start += 1
        depth, e_ = 0, k + 3
        while True:
            if ts[e_] == '{':
                depth += 1
            elif ts[e_] == '}':
                depth -= 1
                if depth == 0:
                    break
            e_ += 1
        assert ts[e_ + 1] == ';', (fname, ts[k - 4:e_ + 2])
        toks[lhs_start][1] = 'nm_assign_whole(' + toks[lhs_start][1]
        toks[k - 1][1] = ','        # (the `=`)
        toks[e_][1] = '})'
    return native.untokenize(toks)


def loop_ticks(text):
    """(2026-11-13, modnative5: the user's rule (3), the register-only-loop budget) Each loop condition of a native function
    counts one pass: `while (c)` -> `while ((NM_TICK(), (c)))`, the same for do ... while, and `for (a; c; b)` ->
    `for (a; (NM_TICK(), (c)); b)` (an empty c: true). nm_native.h's NM_TICK is the tester's count in the test build and
    nothing in the game's. The conditions are evaluated as before, once per pass, in the same order."""
    toks = native.tokenize(text)
    ts = [t for _, t in toks]
    changed = False
    for k, t in enumerate(ts):
        if t not in ('while', 'for') or k + 1 >= len(ts) or ts[k + 1] != '(':
            continue
        depth, j, semis = 0, k + 1, []
        while j < len(ts):
            if ts[j] in ('(', '[', '{'):
                depth += 1
            elif ts[j] in (')', ']', '}'):
                depth -= 1
                if depth == 0:
                    break
            elif ts[j] == ';' and depth == 1:
                semis.append(j)
            j += 1
        if t == 'while':
            toks[k + 1][1] = '((NM_TICK(), ('
            toks[j][1] = ')))'
        elif len(semis) == 2:
            a_, b_ = semis
            if b_ == a_ + 1:
                toks[b_][1] = ' (NM_TICK(), true);'
            else:
                toks[a_][1] = '; (NM_TICK(), ('
                toks[b_][1] = '));'
        else:
            continue
        changed = True
    return native.untokenize(toks) if changed else text


# ---- Calls with the mod compiler's fast convention (2026-11-13, modnative5: the user's rule (3) of 2026-11-13) ----
# An internal function the mod's compiler gave its fast convention (fastcc: compiled_interface) takes integers in a0-a3, t0-t7,
# t8, v1 and floats in f0, f2, ... f18 (nooddspreg), where o32 puts the fifth on the stack and floats after an integer in
# integer registers. lift_call only knows o32, so a call to such a function with a float or more than four parameters is made
# here by hand: each one into its register, then the stand-in. The tester's stand-in for it notes exactly those registers
# (lift.py: the candidate's 'fast_callees'), so a parameter the native code gets wrong is a difference as for any call.
FAST_STUBS = {}     # (the candidate being made: stand-in symbol -> the registers its stub fills)


def o32_stub(cname, symbol, ret, params, keep):
    """(2026-11-14, modnative6) A call by o32 with a float that lift_call cannot place: o32 gives each of the first four
    parameters a slot, a0-a3; only a float first parameter goes in f12, and a float second one after a float first in f14 --
    every other float goes in its slot's integer register as its bits (a call `f(int, float)` passes the float in a1). The
    stub fills those registers by hand, then the stand-in, which notes the registers the callee reads (analyse.py)."""
    kept = [(i, params[i]) for i in keep]
    sets = []
    for slot, (i, (t, _)) in enumerate(kept):
        if t in FLOAT and (slot == 0 or (slot == 1 and kept[0][1][0] in FLOAT)):
            sets.append('nm_ctx->f%d.fl = p%d;' % (12 + 2 * slot, i))
        elif t in FLOAT:
            sets.append('{ float v_ = p%d; u32 b_; std::memcpy(&b_, &v_, 4); nm_ctx->r%d = (gpr)(int64_t)(s32)b_; }' % (i, 4 + slot))
        elif t.startswith('NPtr<'):
            sets.append('nm_ctx->r%d = (gpr)(int64_t)(s32)p%d.a;' % (4 + slot, i))
        else:
            sets.append('nm_ctx->r%d = (gpr)(int64_t)(s32)p%d;' % (4 + slot, i))
    if ret == 'void':
        back = ''
    elif ret in FLOAT:
        back = ' return nm_ctx->f0.fl;'
    elif ret.startswith('NPtr<'):
        back = ' return %s((u32)nm_ctx->r2);' % ret
    else:
        back = ' return (%s)(u32)nm_ctx->r2;' % ret
    ps = ', '.join('%s p%d' % (t, i) for i, (t, _) in enumerate(params))
    return ('static %s %s(%s) {   // (the original calls it by o32, a float in an integer register: the stand-in)' + chr(10) +
            '    %s ::%s(lift_rdram, nm_ctx);%s' + chr(10) + '}') % (ret, cname, ps, ' '.join(sets), symbol, back)


def fast_stub(cname, symbol, ret, params, keep, base):
    kept = [(i, params[i]) for i in keep]
    regs = parameter_registers(base, cname, [p for _, p in kept])
    sets = []
    for (i, (t, _)), reg in zip(kept, regs):
        if t in FLOAT:
            sets.append('nm_ctx->%s.fl = p%d;' % (reg, i))
        elif t.startswith('NPtr<'):
            sets.append('nm_ctx->%s = (gpr)(int64_t)(s32)p%d.a;' % (reg, i))
        else:
            sets.append('nm_ctx->%s = (gpr)(int64_t)(s32)p%d;' % (reg, i))
    FAST_STUBS[symbol] = regs
    if ret == 'void':
        back = ''
    elif ret in FLOAT:
        back = ' return nm_ctx->f0.fl;'
    elif ret.startswith('NPtr<'):
        back = ' return %s((u32)nm_ctx->r2);' % ret
    else:
        back = ' return (%s)(u32)nm_ctx->r2;' % ret
    ps = ', '.join('%s p%d' % (t, i) for i, (t, _) in enumerate(params))
    return ('static %s %s(%s) {   // (the original calls it with the fast convention, %s: the stand-in)' + chr(10) +
            '    %s ::%s(lift_rdram, nm_ctx);%s' + chr(10) + '}') % (
        ret, cname, ps, ', '.join(register_name(x) for x in regs), ' '.join(sets), symbol, back)


def converted(a, r, census, private_names, inv):
    """The candidate's C++: the native code in namespace nm_<a>, and its entry modrep_nat_<a>."""
    FAST_STUBS.clear()
    base = r['object']
    calls, thru = thru_calls(a, r, inv)      # (2026-11-13, modnative5: rule (5), internal callees run through it)
    inlined_here = sorted(c for c in calls if c in WHOLE_INLINE.get(a, ()))
    calls = {c: v for c, v in calls.items() if c not in WHOLE_INLINE.get(a, ())}     # (2026-11-14, modwhole, J: compiled in)
    plan = inline_plan(a, r, inv, calls, thru)
    if isinstance(plan, str):
        raise SystemExit('%s: %s' % (a, plan))
    names = needed(a, r, inv, calls, thru)
    toks, its = file_items(base)
    used = set()
    for kind, n, x, y in its:
        if kind == 'function' and n in names:
            used |= {t for _, t in toks[x:y + 1]}
    # Names the needed code uses, and the declarations they need in turn (a type used in a kept declaration).
    out = []
    keep_bodies = set(names)
    frame_report = {}
    private_set = {p_ for p_ in private_names if '.' not in p_}     # (a function's own: bound in its body, native.py)
    # (2026-11-13, modnative5) A file-scope array the mod's compiler split into one variable per element: x.0, x.1, ... (NSplit)
    file_functions = {n_ for k_, n_, _, _ in file_items(r['object'])[1] if k_ == 'function'}
    private_split = {}
    for p_ in private_names:
        m_ = re.fullmatch(r'(\w+)\.(\d+)', p_)
        if m_ and m_.group(1) not in file_functions:
            private_split.setdefault(m_.group(1), {})[int(m_.group(2))] = p_
    # (every element of such an array, not only those the original reaches: the native code's indexing may name any, and one
    #  it reaches that the original doesn't is then a difference the tester sees, never a null element -- arcade_number_setup_done
    #  crashed on np_page_index_was[0] so in run m5rules)
    private_names = list(private_names)
    for b_, els_ in private_split.items():
        for p_ in PRIVATE_ROWS:
            m_ = re.fullmatch(re.escape(b_) + r'\.(\d+)', p_['name'])
            if m_ and p_['private'] and int(m_.group(1)) not in els_:
                els_[int(m_.group(1))] = p_['name']
                private_names.append(p_['name'])
    stubs_left = dict(calls)
    frame_ext = {'expr': '0x%Xu' % ((frame_size(a) + 7) & ~7), 'items': []}     # (frame_extend: past the original's frame)
    # (2026-11-13, modnative2) A function that calls itself: the original's call goes to the tester's stand-in for it, like
    #  any other call -- so the native function's own calls go to a stub, nm_self_<name>, put before its body, and the body
    #  calls that instead of itself.
    self_call = stubs_left.pop(r['name'], None)
    bound_private = set()
    def declares(kind, n, ts, c):
        return (kind == 'function' and n == c) or (kind == 'other' and '{' not in ts and any(
            ts[k] == c and ts[k + 1] == '(' for k in range(len(ts) - 1)))
    # (a callee the file declares only inside a function -- `extern void f(void);` in a body -- gets its stub before the first
    #  function kept: by then the file's types are all there)
    at_top = {c for c in calls if any(declares(kind, n, [t for _, t in toks[x:y + 1]], c) for kind, n, x, y in its)}
    # (2026-11-14, modwhole: a callee a kept body also declares inside itself -- `u16 roster_stand_in(u32 id, int nth);` -- gets
    #  its stub before the first body: after that block declaration a static one is a C++ error)
    for kind, n, x, y in its:
        if kind == 'function' and n in names:
            body_ = ''.join(ws + t for ws, t in toks[x:y + 1])
            body_ = body_[body_.find('{'):]
            at_top -= {c for c in at_top if block_declares(body_, c)}
    # (part 1 of the Linux work, 2026-11-14) Private data of ANOTHER file, which this one declares only inside a function's
    #  body -- `{ extern u32 dbg_ann_kept; ... }` -- has no file-scope declaration to bind at: it is bound to the tester's PC
    #  object before the first function kept, exactly as a file-scope one is, and the block's declaration is taken out (the
    #  namespace's name is then the one meant, as native.py remove_block_externs does for views).
    file_declared = set()
    for kind, n, x, y in its:
        if kind == 'other':
            file_declared |= {d[0] for d in (native.declarators([t for _, t in toks[x:y + 1]]) or [])}
    block_private = {}
    for kind, n, x, y in its:
        if kind == 'function' and n in keep_bodies:
            ts_ = [t for _, t in toks[x:y + 1]]
            for k in range(ts_.index('{') + 1 if '{' in ts_ else len(ts_), len(ts_)):
                if ts_[k] == 'extern' and ';' in ts_[k:] and '(' not in ts_[k:k + ts_[k:].index(';')]:
                    d_ = native.declarators(ts_[k + 1:k + ts_[k:].index(';') + 1])
                    if d_ and len(d_) == 1 and use_whole() and d_[0][0] not in private_set and d_[0][0] not in file_declared and \
                            d_[0][0] in PRIVATE_SIZES and d_[0][0] not in bound_private and '.' not in d_[0][0]:
                        # (2026-11-14, modwhole, L: another file's private data the original never touches, bound all the same)
                        private_set.add(d_[0][0])
                        private_names.append(d_[0][0])
                    if d_ and len(d_) == 1 and d_[0][0] in private_set and d_[0][0] not in file_declared:
                        t_ = ' '.join(t for t in ts_[k + 1:k + 1 + d_[0][1]] if t not in native.STORAGE)
                        if block_private.setdefault(d_[0][0], (t_, d_[0][3])) != (t_, d_[0][3]):
                            raise SystemExit('%s: private data %s declared with two types inside functions' % (a, d_[0][0]))
    block_pieces = []
    for nm, (t, dims) in sorted(block_private.items()):
        c = re.sub(r'\W', '_', nm)
        size = PRIVATE_SIZES[nm]
        if dims:
            d = ''.join('[%s]' % x_ for x_ in dims)
            block_pieces.append('static %s (&%s)%s = *reinterpret_cast<%s(*)%s>(lift_mod_private_%s); static_assert(sizeof(%s) == %d, "%s");' % (
                t, nm, d, t, d, c, nm, size, nm))
        elif nm in native.SHRUNK and size == 1 and use_shrunk():
            sh = native.SHRUNK[nm]
            block_pieces.append('static NShrunk<%s, %d, %d> %s(lift_mod_private_%s);   // (shrunk to one byte by the mod compiler)' % (
                t, sh['initial'], sh['other'], nm, c))
            SHRUNK_USED.add(nm)
        else:
            block_pieces.append('static %s& %s = *reinterpret_cast<%s*>(lift_mod_private_%s); static_assert(sizeof(%s) == %d, "%s");' % (
                t, nm, t, c, nm, size, nm))
        private_set.discard(nm)
        bound_private.add(nm)

    def without_block_externs(seg):
        """The function's tokens without its `extern T name;` of a private bound above."""
        if not block_private:
            return seg
        seg = [list(q) for q in seg]
        k, depth = 0, 0
        while k < len(seg):
            t = seg[k][1]
            depth += (t == '{') - (t == '}')
            if t == 'extern' and depth > 0:
                j = k
                while seg[j][1] != ';':
                    j += 1
                if '(' not in [q[1] for q in seg[k:j]] and any(q[1] in block_private for q in seg[k:j]):
                    for q in range(k, j + 1):
                        seg[q] = [seg[q][0] if q == k else '', '']
                k = j
            k += 1
        return seg

    def variant_tokens(n, x, y, vname, renames):
        """(a callee inlined at some calls and called at others: inline_sites.py) The function's tokens as one variant: its
        own name the variant's, and each call the plan names calling that variant of its callee."""
        seg = [list(q) for q in toks[x:y + 1]]
        for off, target in renames.items():
            seg[off][1] = target
        if vname != n:
            for k in range(len(seg) - 1):
                if seg[k][1] == '{':
                    break
                if seg[k][1] == n and seg[k + 1][1] == '(':
                    seg[k][1] = vname
                    break
        return seg
    first_kept = True
    in_view = False
    for kind, n, x, y in its:
        ts = [t for _, t in toks[x:y + 1]]
        text = ''.join(ws + t for ws, t in toks[x:y + 1])
        # (a structure's view -- native.py struct_views, between /*NMVIEW*/ and /*NMVIEW_END*/ -- is kept whole)
        if in_view or 'NMVIEW' in ts:
            out.append(text)
            in_view = ('NMVIEW' in ts or in_view) and 'NMVIEW_END' not in ts
            continue
        if kind == 'function' and n in keep_bodies:
            for c in [c for c in stubs_left if c not in at_top]:
                out.append(stub(c, stubs_left.pop(c)[0], declared_signature(base, c), base))
            if first_kept:
                first_kept = False
                if block_pieces:
                    out.append('\n' + '\n'.join(block_pieces))
                if plan:
                    # (each variant under a name of its own, declared before any body: a caller may come first in the file)
                    for kind2, n2, x2, y2 in its:
                        if kind2 == 'function' and n2 in plan['variants']:
                            for vname, _ in plan['variants'][n2]:
                                if vname != n2:
                                    h_ = drop_attributes([t for _, t in variant_tokens(n2, x2, y2, vname, {})])
                                    out.append(' '.join(h_[:h_.index('{')]) + ';   // (%s where the original has it inline: inline_sites.py)' % n2)
        # A call the original makes: its stub where the file first declares or defines it, and that declaration left out.
        here = [c for c in stubs_left if declares(kind, n, ts, c)]
        if here:
            for c in here:
                out.append(stub(c, stubs_left.pop(c)[0], declared_signature(base, c), base))
            if not (plan and kind == 'function' and n in keep_bodies):
                continue        # (with a plan: a callee the original also has inline -- its stand-in above, its variants below)
        if kind == 'function':
            for vname, renames in (plan['variants'][n] if plan and n in keep_bodies else [(n, {})] if n in keep_bodies else []):
                if plan or block_private:
                    seg_ = without_block_externs(variant_tokens(n, x, y, vname, renames))
                    text = ''.join(ws_ + t_ for ws_, t_ in seg_)
                # (without the mod's section attributes: a hook's .recomp_hook section means nothing on the PC)
                body = drop_attributes([t for _, t in toks[x:y + 1]])
                ws = [w for w, t in toks[x:y + 1]]
                text_ = re.sub(r'__attribute__\s*\(\((retain|section|used)[^;{]*?\)\)\)?', '', text) if 'section' in text[:300] else text
                # (2026-11-13, modnative3: its local arrays indexed by data, in the original's frame: frame_arrays)
                text_ = frame_arrays(a, r, n, text_, frame_report)
                if use_whole() and n != r['name']:
                    text_ = helper_views(n, text_, frame_report, frame_ext, base)     # (2026-11-14, modwhole, N)
                if use_whole() and a in EXTEND_FOR:
                    # (2026-11-14, modwhole: the gameplay tier only, and only for a function the census found not compiling --
                    #  so no entry that compiles without it changes)
                    text_ = frame_extend(n, text_, frame_report, frame_ext)
                if self_call and n == r['name']:
                    head_, rest_ = text_.split('{', 1)
                    text_ = head_ + '{' + re.sub(r'\b%s(\s*\()' % n, r'nm_self_%s\1' % n, rest_)
                    out.append(stub('nm_self_' + n, self_call[0], declared_signature(base, n), base))
                if use_loops():
                    text_ = loop_ticks(text_)       # (2026-11-13, modnative5: rule (3), each loop pass counted in the test build)
                text_ = compound_padding(n, text_)  # (2026-11-13, modnative5: rule (6), padding as the original writes it)
                out.append(text_)
            continue
        decl = native.declarators(ts)
        if decl and ts[0] != 'typedef' and not (len(ts) > 1 and ts[0] == 'static' and ts[1] in ('NVar', 'NArr')) and ts[0] not in ('NVar', 'NArr'):
            names_here = [d[0] for d in decl]
            if '(' in ts[:decl[0][1]]:
                continue
            if not any(nm in used for nm in names_here):
                continue            # (data the needed code doesn't use: left out)
            # The mod's private data among them: a reference to the tester's PC object (lift_mod_private_<name>), in the N64
            # layout's fields in the PC's byte order, as the tester's table keeps it.
            base_type = [t for t in ts[:decl[0][1]] if t not in native.STORAGE]
            if all(nm in bound_private for nm in names_here) and names_here:
                continue            # (a later declaration of private data already bound: the binding stands for it)
            pieces = []
            for nm, k, e, dims in decl:
                if nm in private_split and dims and len(dims) == 1 and nm not in bound_private:
                    # (2026-11-13, modnative5: split by the mod's compiler into one variable per element -- nm_native.h NSplit)
                    els = private_split[nm]
                    t = ' '.join(base_type)
                    pieces.append('static NSplit<%s, %d> %s = {{%s}};   // (split by the mod compiler into one variable per element)' % (
                        t, max(els) + 1, nm, ', '.join('lift_mod_private_%s' % re.sub(r'\W', '_', els[i]) if i in els else 'nm_split_none()'
                                                         for i in range(max(els) + 1))))
                    bound_private.add(nm)
                    continue
                if nm in private_set:
                    t = ' '.join(base_type)
                    c = re.sub(r'\W', '_', nm)
                    size = PRIVATE_SIZES[nm]
                    # (the C type must be the N64 object's size: the mod's compiler may have shrunk an int it only ever sets
                    #  to 0 or 1 to a byte -- then the PC object, which is the N64's, is not the C type)
                    if dims:
                        d = ''.join('[%s]' % x_ for x_ in dims)
                        pieces.append('static %s (&%s)%s = *reinterpret_cast<%s(*)%s>(lift_mod_private_%s); static_assert(sizeof(%s) == %d, "%s");' % (
                            t, nm, d, t, d, c, nm, size, nm))
                    elif nm in native.SHRUNK and size == 1 and use_shrunk():
                        # (2026-11-13, modnative5: the user's rule (2)) the mod's compiler shrank it to a byte holding 0 for its
                        #  initial value and 1 for the other (shrunk.py): the native code reads and writes that byte so
                        sh = native.SHRUNK[nm]
                        pieces.append('static NShrunk<%s, %d, %d> %s(lift_mod_private_%s);   // (shrunk to one byte by the mod compiler)' % (
                            t, sh['initial'], sh['other'], nm, c))
                        SHRUNK_USED.add(nm)
                    else:
                        pieces.append('static %s& %s = *reinterpret_cast<%s*>(lift_mod_private_%s); static_assert(sizeof(%s) == %d, "%s");' % (
                            t, nm, t, c, nm, size, nm))
                    private_set.discard(nm)
                    bound_private.add(nm)
            if pieces:
                rest = [(nm, k, e) for nm, k, e, dims in decl if nm not in private_names and nm not in private_split]
                if rest:
                    # (keep the others, with the private names out of the declaration)
                    t2 = list(toks[x:y + 1])
                    for nm, k, e, dims in decl:
                        if nm in private_names or nm in private_split:
                            t2[k] = [t2[k][0], '__nm_pc_' + nm]
                    out.append(''.join(ws + t for ws, t in t2))
                out.append('\n' + '\n'.join(pieces))
                continue
        if ts and ts[0] == 'static' and len(ts) > 1 and ts[1] in ('NVar', 'NArr'):
            # a view: only those used
            vn = [ts[k + 1] for k in range(len(ts) - 1) if ts[k] in ('>', '>>') and re.match(r'[A-Za-z_]\w*$', ts[k + 1]) and ts[k + 2:k + 3] == ['(']]
            if not any(v in used for v in vn):
                continue
        if census[base]['outside'] and decl and not any(d[0] in used for d in decl):
            continue
        out.append(text)
    if private_set:
        raise SystemExit('%s: private data %s not found among its file\'s declarations' % (a, sorted(private_set)))
    ret, all_params = signature(base, r['name'])
    ret = strip_attributes(ret)
    ns = 'nm_%s' % a
    wrapper_params, sources, call_args = [], [], []
    # (2026-11-13) Only the parameters the compiler left come from registers (compiled_interface); each one it took away is
    #  given the constant every call passed it, or 0 when the function never reads it. A result it took away is not one.
    iface = interface_of(base, r['name'])
    params = [(t, n) for t, n in all_params if not iface or n in iface['params']]
    regs = parameter_registers(base, r['name'], params)
    wide_entry = use_whole() and wide_ok((ret, params, is_internal(base, r['name']) if not iface else iface['fastcc']))
    if wide_entry:
        regs = ['r%d' % reg for _, reg in wide_entry]      # (2026-11-14, modwhole, K: o32's slots, a pair for each 64-bit one)
    narrow = narrow_inputs(base, r['name'], params, regs) if use_narrow() and not wide_entry else []
    # (2026-11-13, modnative5: the user's rule (3) -- a function of the mod compiler's fast convention with a float or more than
    #  four parameters takes them in other registers than o32's: the tester learns each one's register and kind)
    fast_params = []
    if use_fast() and iface and iface['fastcc'] and (any(t in FLOAT for t, _ in params) or len(params) > 4):
        for (t, n), reg in zip(params, regs):
            fast_params.append([reg, 'f' if t in FLOAT else 'w' if t.startswith('NPtr<') else 'i'])
    dropped = {}
    if iface:
        for n, k in iface['dropped'].items():
            t = dict((n_, t_) for t_, n_ in all_params)[n]
            dropped[n] = '(%s)%s' % (t, k if k is not None else 0) + ('   /* the compiler took it away: %s */' % (
                'every call passes it' if k is not None else 'never read'))
        if iface['void']:
            ret = 'void'
    for i, ((t, n), reg) in enumerate(zip(params, regs)):
        if t.startswith('NPtr<'):
            inner = t[5:-1]
            kind = {'u8': 'u8', 's8': 's8', 'char': 's8', 'unsigned char': 'u8', 'signed char': 's8', 'u16': 'u16', 's16': 's16'}.get(inner, 'u32')
            wrapper_params.append('P<%s> arg%d' % (kind, i))
            sources.append(source('P<%s>' % kind, reg))
            call_args.append('%s(arg%d.a)' % (t, i))
        elif t in ('float', 'f32'):
            wrapper_params.append('f32 arg%d' % i)
            sources.append(source('f32', reg))
            call_args.append('arg%d' % i)
        elif wide_entry and t in WIDE:
            wrapper_params.append('u64 arg%d' % i)
            sources.append('((u64)(u32)ctx->%s << 32 | (u64)(u32)ctx->r%d)' % (reg, int(reg[1:]) + 1))
            call_args.append('(%s)arg%d' % (t, i))
        else:
            wt = INT_TYPES.get(t, 'u32')
            wrapper_params.append('%s arg%d' % (wt, i))
            sources.append(source(wt, reg))
            call_args.append('(%s)arg%d' % (t, i))
    # (the native function takes them all, as its C declares: the ones left from the registers, the others their constants)
    left_args = dict(zip([n for _, n in params], call_args))
    call_args = [left_args[n] if n in left_args else dropped[n] for _, n in all_params]
    if ret == 'void':
        wret, call = 'void', '%s::%s(%s);' % (ns, r['name'], ', '.join(call_args))
    elif ret in ('float', 'f32'):
        wret, call = 'f32', 'return %s::%s(%s);' % (ns, r['name'], ', '.join(call_args))
    elif ret.startswith('NPtr<'):
        wret, call = 'u32', 'return %s::%s(%s).a;' % (ns, r['name'], ', '.join(call_args))
    else:
        wret, call = INT_TYPES.get(ret, 'u32'), 'return (%s)%s::%s(%s);' % (INT_TYPES.get(ret, 'u32'), ns, r['name'], ', '.join(call_args))
        # (the N64 hands a narrow result back widened as its own type: (s32)(u8) etc. -- the cast through the C type first)
        if ret in INT_TYPES and ret not in ('int', 's32', 'u32', 'unsigned int', 'unsigned', 'long', 'unsigned long'):
            call = 'return (%s)(%s)%s::%s(%s);' % (INT_TYPES[ret], ret, ns, r['name'], ', '.join(call_args))
    head = ('}  // (namespace game: the native mod\'s header goes outside it)\n'
            '#include "nm_native.h"\nnamespace game {\n')
    # (2026-11-13, modnative2: no list of its private data's places here any more -- lift.py run_fuzz takes a native function's
    #  places in the mod from its original's relocations, original_mod_places, never from this text.)
    # (The entry first -- lift.py reads a candidate's signature from its first definition -- after a declaration of the
    #  native function it calls.)
    declaration = None
    for kind, n, x, y in its:
        if kind == 'function' and n == r['name']:
            ts = drop_attributes([t for _, t in toks[x:y + 1]])
            declaration = ' '.join(ts[:ts.index('{')]) + ';'
    if stubs_left:
        raise SystemExit('%s: no declaration found for the calls %s' % (a, sorted(stubs_left)))
    extra = extra_stubs(a, r, inv, base, out, calls) if use_whole() else {}
    # (The native function's own types may be the mod's structures, declared further down: the entry calls nm_entry, which
    #  takes the entry's plain parameters and is defined at the end of the namespace, where they are all known.)
    plain_args = ', '.join('arg%d' % i for i in range(len(wrapper_params)))
    cpp = head + 'namespace %s { %s nm_entry(%s);%s }\n' % (ns, wret, ', '.join(wrapper_params), ' u32 nm_frame_bytes();' if frame_ext['items'] else '')
    entry_call = '%s%s::nm_entry(%s);' % ('' if wret == 'void' else 'return ', ns, plain_args)
    frame = frame_size(a)
    if frame_ext['items']:
        # (2026-11-14, modwhole: locals past the original's frame -- a bigger frame, so the game only: whole_only. Its size is
        #  nm_frame_bytes(), defined at the namespace's end, where the compiler knows every type's size)
        frame = 1
    if calls or frame:
        # (The frame the original makes -- the same size, so a stand-in's stack parameters are where the original's are -- with
        #  its saved registers kept where the original keeps them, so that if the code overruns into its own frame the
        #  registers come back as the original's epilogue gives them (lift_call.h LiftFrame, the user's rule of 2026-11-11);
        #  and the context its calls go through, nm_ctx.)
        # (2026-11-13, modnative2: and where the frame is, for the locals the native code keeps there -- native.py frame_locals,
        #  and modnative3's prove_gen.py frame_arrays)
        lines = (['LiftFrame frame(ctx, %s);' % ('%s::nm_frame_bytes()' % ns if frame_ext['items'] else '0x%X' % frame)] if frame else []) + kept_registers(a) + (['nm_ctx = ctx;'] if calls else []) + (
            ['nm_frame_base = frame.base;'] if frame and (frame_report.get('moved') or any('nm_frame_base' in piece for piece in out)) else []) + [entry_call]
        cpp += '%s modrep_nat_%s(%s) {\n    %s\n}\n' % (wret, a, ', '.join(['recomp_context* ctx'] + wrapper_params), '\n    '.join(lines))
    else:
        cpp += '%s modrep_nat_%s(%s) {\n    %s\n}\n' % (wret, a, ', '.join(wrapper_params), entry_call)
    out.append('%s nm_entry(%s) {\n    %s\n}' % (wret, ', '.join(wrapper_params), call.replace('%s::' % ns, '')))
    if frame_ext['items']:
        out.append('// (the frame: the original\'s, then the locals it keeps in registers -- frame_extend)\nu32 nm_frame_bytes() { return %s; }' % frame_ext['expr'])
    # (A structure's view is a specialization of nm_native.h's templates, so it goes at file scope: out of both namespaces and
    #  back, with NM_SELF naming the structure's namespace meanwhile.)
    body = '\n'.join(out).replace('/*NMVIEW*/', '\n}  }  // (a structure\'s view: at file scope)\n#undef NM_SELF\n#define NM_SELF game::%s\n' % ns).replace(
        '/*NMVIEW_END*/', '\n#undef NM_SELF\n#define NM_SELF\nnamespace game { namespace %s {\n' % ns)
    cpp += ('// The mod\'s %s (%s, %s), compiled for the PC: scratch/agents/modnative1 native.py, prove_gen.py (2026-11-12)\n'
            'namespace %s {\n%s\n}  // namespace %s' % (r['name'], source_path(r), a, ns, body, ns))
    if frame_report.get('moved') and not frame:
        raise SystemExit('%s: local arrays moved into a frame the original does not make' % a)
    if frame_ext['items']:
        entry_whole_only = 'locals past the original\'s frame (it keeps them in registers): %s' % '; '.join(frame_ext['items'])
    else:
        entry_whole_only = None
    if frame_report:
        FRAME_REPORT[a] = frame_report
    entry = {'cpp': cpp, 'ret': wret, 'sources': sources, 'size': r['instructions'], 'callees': sorted({s for s, _ in calls.values()} | set(extra.values())),
             'private': sorted(private_names), 'native': {'name': r['name'], 'file': source_path(r), 'address': a}}
    # (2026-11-15, the review of 0.84 (reviewnm084), the aligned rule's fix: each pointer parameter's kind for the tester -- lift.py
    #  param_kinds -- from its C pointee's alignment, not P<u32>'s: 'b' for void*, char*, a structure of bytes only, or a pointee
    #  not worked out (every address drawn); 'h' for halfwords; 'w' for anything holding a word or more.)
    kinds = {}
    for i, ((t_, n_), reg) in enumerate(zip(params, regs)):
        if t_.startswith('NPtr<'):
            al = pointee_alignment(t_[5:-1], '\n'.join(out))
            kinds[str(i)] = 'w' if al and al >= 4 else 'h' if al == 2 else 'b'
    if kinds:
        entry['native']['pointer_kinds'] = kinds
    # (2026-11-13, modnative5: what the user's rules of 2026-11-13 need the tester to know, each only where it applies)
    shrunk = sorted(p for p in private_names if re.search(r'NShrunk<[^>]*> \w+\(lift_mod_private_%s\)' % re.escape(re.sub(r'\W', '_', p)), cpp)
                    or (native.SHRUNK.get(p, {}).get('split') and use_shrunk()))
    if shrunk:
        entry['native']['shrunk'] = shrunk           # (2) their bytes hold 0 or 1 only
    if narrow:
        entry['native']['narrow'] = narrow           # (1) extended as their types say
    if fast_params:
        entry['native']['fast_params'] = fast_params     # (3) its own parameters' registers and kinds
    if FAST_STUBS:
        entry['native']['fast_callees'] = dict(FAST_STUBS)   # (3) the registers each fast-convention callee is given
    if use_loops():
        entry['native']['loop_budget'] = 1           # (3) loop passes counted, both versions
    if thru:
        entry['native']['thru'] = ['modrep_natthru_%s' % t for t in thru]    # (5) run for real, pieces of its original
        entry['native']['thru_names'] = [inv[t]['name'] for t in thru]
    # (PROPOSED, not approved: a callee whose result the mod's IR declares narrow -- `declare zeroext i16 @variant_base_of` --
    #  as the stand-in's result kind; lift.py extends the stand-in's v0 so only with LIFT_NM_NARROW_RESULTS=1. Recorded always.)
    nres = narrow_results(r['object'], calls)
    # (2026-11-14, modnative7: the STRICT form the user approved -- a callee's narrow result is taken by its low bits only where
    #  this function's own code is proved to ignore the bits above after every call to it; the others are listed as unproved,
    #  and lift.py then compares all of that callee's bits in the whole run)
    import narrow_proof
    proved_res, unproved_res = {}, []
    for sym, kind in sorted(nres.items()):
        at = re.search(r'[0-9A-F]{8}', sym)
        if at and narrow_proof.caller_ignores_high('nm_aot_nomercy_arcade_%s' % a, int(at.group(0), 16), 16 if kind in ('zh', 'sh') else 8):
            proved_res[sym] = kind
        else:
            unproved_res.append(sym)
    if proved_res:
        entry['native']['narrow_results'] = proved_res
    if unproved_res:
        entry['native']['narrow_results_unproved'] = unproved_res
    # (2026-11-14, modnative7: the new rules' data, only under their switches -- see rule_writes above)
    if rule_lowbits():
        nargs = narrow_args(r['object'], calls)
        if nargs:
            entry['native']['narrow_args'] = nargs
    if os.environ.get('NM_DECLARED_PARAMS') == '1':
        dm = declared_masks(r['object'], calls, inv)     # (NM_DECLARED_PARAMS: see declared_masks)
        if dm:
            entry['native']['declared_masks'] = dm
    if rule_writes():
        import private_values
        vals = {p: v for p in sorted(private_names) for v in [private_values.values_of(p)] if v}
        if vals:
            entry['native']['values'] = vals
    if calls or frame:
        entry['ctx'] = True
    if inlined_here:
        entry['native']['compiled_in'] = inlined_here
        entry_whole_only = entry_whole_only or 'callees compiled in where the original calls them (%s)' % ', '.join(inlined_here)
    if wide_entry:
        entry_whole_only = entry_whole_only or '64-bit integer parameters (o32 register pairs)'
    if extra:
        entry['native']['extra_callees'] = sorted(extra)
        entry_whole_only = entry_whole_only or 'calls the original never makes (%s): stubs to their stand-ins' % ', '.join(sorted(extra))
    if entry_whole_only:
        entry['native']['whole_only'] = entry_whole_only
    return entry


# ---- Local arrays in the original's frame (2026-11-13, scratch/agents/modnative3) ----
# A local array the C indexes by data can be run past its end: on the N64 the store lands in the function's own frame -- another
# local, or a saved register's slot, which the epilogue then loads back (LiftFrame keeps those) -- where on the PC a host
# array would overrun the host stack. resolve_slots' `u16 used[4]` takes up to eight (`used[nu++]` in two loops): the
# original's 0x40($sp) runs into s0's and s1's slots at 0x48, and its later `same_man(id, used[u])` reads them back. So each
# local array a function of the closure indexes by anything but an integer literal (or hands on as a pointer) is an NArr view at
# the place the ORIGINAL keeps it, frame base + its offset: the mod's own debug information says where (dwarf/<file>.elf, the
# file compiled with build_mod.ps1's flags and -g, linked alone; llvm-symbolizer's FRAME lists each local of a function with its
# frame offset, its inlined callees' too). -g leaves the code as it is (dwarf_same.txt, section by section) but for one
# function, resolve_slots, whose prologue it moves (the frame and its slots the same): so each place is also checked against the
# original, which must make that address itself (`addiu $r, $sp, off`).
# The names are modnative2's frame_locals' (nm_frame_base, set by the entry from its LiftFrame), so the two merge.
DWARF_DIR = os.path.join(HERE, 'dwarf')
_SYMBOLIZER = os.environ.get('NM_SYMBOLIZER') or os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'llvm-symbolizer.exe')   # (Linux: NM_SYMBOLIZER=llvm-symbolizer)
_NM = os.environ.get('NM_NM') or os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'llvm-nm.exe')   # (Linux: NM_NM=llvm-nm)
FRAME_REPORT = {}


def frame_variables(base, function, cache={}):
    """[(function the variable is in, its name, its frame offset or None)] for the original function `function` of the file."""
    import subprocess
    key = (base, function)
    if key not in cache:
        elf = os.path.join(DWARF_DIR, base + '.elf')
        out = []
        if os.path.exists(elf):
            syms = subprocess.run([_NM, elf], capture_output=True, text=True).stdout
            m = re.search(r'^([0-9a-f]+) [tT] %s$' % re.escape(function), syms, re.M)
            if m:
                p = subprocess.run([_SYMBOLIZER, '--obj=' + elf], input='FRAME 0x%X\n' % (int(m.group(1), 16) + 4),
                                   capture_output=True, text=True)
                lines = p.stdout.splitlines()
                for k in range(0, len(lines) - 3, 4):
                    fn, var, _, nums = lines[k:k + 4]
                    off = nums.split()[0] if nums.split() else '??'
                    out.append((fn.strip(), var.strip(), int(off) if re.fullmatch(r'-?\d+', off) else None))
        cache[key] = out
    return cache[key]


def frame_arrays(a, r, fname, text, report):
    """The function's text with each local array it indexes by data (see above) an NArr in the original's frame."""
    toks = native.tokenize(text)
    ts = [t for _, t in toks]
    if '{' not in ts:
        return text
    body_start = ts.index('{')
    changed = False
    sized = []
    for k in range(body_start + 1, len(ts) - 4):
        # `T name [ N ] ;` at a statement's start (native.py split each declaration on its own): a one-dimension array
        if not (re.match(r'[A-Za-z_]\w*$', ts[k]) and ts[k + 1] == '[' and ts[k - 1] not in ('.', '->', ',', '(', '[', '=', 'return')):
            continue
        j = k - 1
        while j > body_start and ts[j] not in (';', '{', '}'):
            j -= 1
        type_toks = ts[j + 1:k]
        if not type_toks or any(t in ('=', '(', '[', ')', '*', ',', 'static', 'extern', 'return', '?', ':') for t in type_toks):
            continue
        if not (type_toks[-1] in native.BUILTIN or re.match(r'[A-Za-z_]\w*$', type_toks[-1]) or type_toks[-1] == '>'):
            continue
        close = ts.index(']', k + 1)
        if ts[close + 1] != ';':
            continue        # (an initializer, or more dimensions: left as it is)
        name = ts[k]
        # How the function uses it: indexed by an integer literal only, or by anything else / as a value.
        by_data = False
        for q in range(close + 1, len(ts) - 1):
            if ts[q] != name or ts[q - 1] in ('.', '->'):
                continue
            if ts[q + 1] == '[':
                e = ts.index(']', q + 2)
                if not (e == q + 3 and re.fullmatch(r'\d+|0[xX][0-9a-fA-F]+', ts[q + 2])):
                    by_data = True
            else:
                by_data = True
        if not by_data:
            continue
        places = {off for fn, var, off in frame_variables(r['object'], r['name']) if fn == fname and var == name}
        if len(places) != 1 or None in places:
            report.setdefault('kept', []).append('%s.%s: %s' % (fname, name, 'no frame place' if not places or None in places else
                                                                 'several places %s' % sorted(places)))
            continue
        off = places.pop()
        # (and the original itself makes that address -- `addiu $r, $sp, off` -- as a cross-check of the debug information:
        #  -g moved resolve_slots' prologue (dwarf_same.txt), and only the address the original makes counts)
        if not re.search(r'// 0x[0-9A-F]{8}: addiu +\$\w+, \$sp, 0x%X$' % off, original_text(a) or '', re.M):
            report.setdefault('kept', []).append('%s.%s: the original makes no address 0x%X($sp)' % (fname, name, off))
            continue
        t = ' '.join(type_toks)
        # (the declaration, `T name[N];`, becomes the view: T's NArr at the original's frame offset, with the C's size noted)
        toks[j + 1][1] = "NArr<%s> %s(nm_frame_base + 0x%X);   /* %s %s[%s], at the original's 0x%X($sp) */" % (
            t, name, off, t, name, ' '.join(ts[k + 2:close]), off)
        for q in range(j + 2, close + 2):
            toks[q] = ['', '']
        report.setdefault('moved', []).append('%s.%s at 0x%X' % (fname, name, off))
        sized.append((name, t, ' '.join(ts[k + 2:close])))
        changed = True
    if not changed:
        return text
    out = native.untokenize(toks)
    for name, t, n in sized:
        # (2026-11-14, modwhole: `sizeof` of the array -- which native.py wrote as NSizeOf of its type, then a host array -- is
        #  the C type's N64 size: an NArr view's own type has no length, and NSizeOf refuses one)
        out = re.sub(r'NSizeOf<std::remove_cv_t<std::remove_reference_t<decltype\(\s*%s\s*\)>>>::value' % re.escape(name),
                     'NSize<%s[%s]>::value' % (t, n), out)
    return out


# ---- Locals with no place in the original's frame, in an extension of it (2026-11-14, modwhole; NM_WHOLE=1, the game only) ----
# A local the native code hands on as an N64 pointer -- `bits(src, &pos, 7)`, `regular_pads(ob, ...)` with `u16 ob[4]` -- must be in
# game memory. native.py frame_locals and frame_arrays above put it at the original's own frame offset; when the original has no
# such place (it inlined the callee and kept the local in registers: "no frame place"), the gameplay tier puts it past the
# original's frame, in the same frame made bigger (LiftFrame of the original's size + the extension). The C's meaning is kept --
# the same values, each local alone in its slot -- but the frame is not the original's, so the entry is whole_only (never proved).
EXT_SIZES = {'char': 1, 's8': 1, 'u8': 1, 'signed char': 1, 'unsigned char': 1, 'bool': 1, '_Bool': 1, 'short': 2, 's16': 2, 'u16': 2,
             'unsigned short': 2, 'int': 4, 's32': 4, 'u32': 4, 'unsigned': 4, 'unsigned int': 4, 'long': 4, 'unsigned long': 4,
             'float': 4, 'f32': 4, 'double': 8, 'f64': 8, 's64': 8, 'u64': 8, 'long long': 8, 'unsigned long long': 8}


EXTEND_FOR = set()     # (put: the functions NM_WHOLE let through although the census found them not compiling)


def ext_slot(ext, what, ty, n, unit):
    """(2026-11-15) The next slot past the original's frame for n elements of type ty (unit: its N64 size if this knows it, else
    the compiler's NSize): (its offset as a C++ expression, ''), and ext['expr'] moved past it."""
    size = '%d' % (((unit * n) + 7) & ~7) if unit else '((NSize<%s>::value * %du + 7u) & ~7u)' % (ty, n)
    at = '(%s)' % ext['expr']
    ext['expr'] += ' + ' + size
    ext['items'].append('%s at %s (%s bytes)' % (what, at, size))
    return at, ''


def helper_array_length(base, fname, name):
    """A helper's local array's declared length, from the function in the preprocessed C (pp/<file>.i), or None."""
    path = os.path.join(native.PP, base + '.i')
    if not os.path.exists(path):
        return None
    text = open(path, encoding='utf8', errors='replace').read()
    m = re.search(r'\b%s\s*\([^;{]*\)\s*\{' % re.escape(fname), text)
    if not m:
        return None
    body = text[m.end():m.end() + 20000]
    d = re.search(r'\b%s\s*\[\s*([^\]]+?)\s*\]' % re.escape(name), body)
    return d.group(1) if d else None


def helper_views(fname, text, report, ext, base=None):
    """(N) A helper's own frame views (native.py frame_locals': no "at the original's" note, which frame_arrays' views of the
    caller's frame have) moved past the caller's frame: ext as frame_extend's, each slot its C size."""
    def move(m):
        kind, ty, name, off = m.group(1), m.group(2), m.group(3), m.group(4)
        unit = 4 if ty.startswith('NPtr') else EXT_SIZES.get(ty.replace('const ', '').strip())
        if kind == 'NArr':
            n = helper_array_length(base, fname, name) if base else None
            if n is None:
                return m.group(0)       # (its length unknown: left where it was)
            size = '((NSize<%s>::value * (u32)(%s) + 7u) & ~7u)' % (ty, n)
            at = '(%s)' % ext['expr']
            ext['expr'] += ' + ' + size
            ext['items'].append('%s.%s at %s (%s bytes; its own frame had it at %s)' % (fname, name, at, size, off))
        else:
            at, _ = ext_slot(ext, '%s.%s (its own frame had it at %s)' % (fname, name, off), ty, 1, unit)
        return '%s<%s> %s(nm_frame_base + %s); /* %s: its own frame had it at %s -- in this caller, past the caller frame */' % (
            kind, ty, name, at, name, off)
    out = re.sub(r"\b(NVar|NArr)<([^;()]*?)> (\w+)\(nm_frame_base \+ (0x[0-9A-Fa-f]+)\);(?![^\n]*at the original)(?![^\n]*past the)", move, text)
    if out != text:
        report.setdefault('moved', []).append('helper views moved: ' + ', '.join(ext['items']))
    return out


def frame_extend(fname, text, report, ext):
    """The function's text with each local it hands on that is still a PC local (`T x;`, `T x = v;`, `T x[N];` at a statement's
    start) a view past the original's frame: ext['next'] is the next free offset (it starts at the original's size), ext['items']
    lists them."""
    comments = []

    def hide(m):
        comments.append(m.group(0))
        return ' nm_comment_%d_ ' % (len(comments) - 1)
    original = text
    text = re.sub(r'/\*.*?\*/', hide, text, flags=re.S)
    toks = native.tokenize(text)
    ts = [t for _, t in toks]
    if '{' not in ts:
        return original
    body = ts.index('{')
    changed = False
    sized = []
    starts = [k for k in range(body + 1, len(ts)) if ts[k - 1] in (';', '{', '}')]
    for s in starts:
        # the declaration: type tokens, a name, then ';' / '=' / '['
        k = s
        while k < len(ts) and (re.match(r'[A-Za-z_]\w*$', ts[k]) or ts[k] in ('<', '>', '>>')) and ts[k] not in ('return', 'static', 'extern',
                'typedef', 'goto', 'case', 'default', 'else', 'do', 'if', 'for', 'while', 'switch', 'break', 'continue', 'sizeof'):
            k += 1
        if k - s < 2 or k >= len(ts) or ts[k] not in (';', '=', '['):
            continue
        name, type_toks = ts[k - 1], ts[s:k - 1]
        if type_toks[0] in ('NArr', 'NVar', 'NPtr') and '<' not in type_toks or type_toks[0] in ('NArr', 'NVar', 'LiftFrame', 'NShrunk', 'NSplit'):
            continue
        ty = ' '.join(type_toks)
        dims, init = None, None
        e = k
        if ts[k] == '[':
            # `T x[N];`, `T x[N][M];`, or a one-dimension one with an initializer (`= "MORE"`, `= { b[--n], 0 }`): stored below
            dl, q = [], k
            while ts[q] == '[':
                close = ts.index(']', q)
                dl.append(' '.join(ts[q + 1:close]))
                q = close + 1
            e = q
            if not all(re.fullmatch(r'\d+|0[xX][0-9A-Fa-f]+', d) for d in dl) or len(dl) > 2:
                continue        # (a length that is not a number, or more dimensions: left as it is)
            if ts[e] == '=':
                f, depth = e + 1, 0
                while f < len(ts) and not (ts[f] == ';' and depth == 0):
                    depth += (ts[f] in '([{') - (ts[f] in ')]}')
                    f += 1
                init = ''.join(ws + tk for ws, tk in toks[e + 1:f]).strip()
                if len(dl) != 1 or not (init.startswith('{') or init.startswith('"')):
                    continue
                e = f
            if ts[e] != ';':
                continue
            dims = dl
        elif ts[k] == '=':
            # `T x = v;` -- v up to the ';' at depth 0
            e, depth = k + 1, 0
            while e < len(ts) and not (ts[e] == ';' and depth == 0):
                depth += (ts[e] in '([{') - (ts[e] in ')]}')
                e += 1
            if '{' in ts[k + 1:e]:
                continue
        # handed on: `&name` as a value, or an array used as a value (not indexed, not sizeof)
        esc = False
        for q in range(e + 1, len(ts) - 1):
            if ts[q] != name or ts[q - 1] in ('.', '->'):
                continue
            if ts[q - 1] == '&' and ts[q - 2] in ('(', ')', ',', '=', 'return', '?', ':', '{', ';', '+'):
                esc = True
            elif dims and ts[q + 1] != '[' and ts[q - 1] in ('(', ')', ',', '=', 'return', '+', '?', ':') and not (ts[q - 1] == '(' and ts[q - 2] == 'sizeof'):
                esc = True
            elif dims and ts[q + 1] == '[' and ts[q - 1] == '&':
                esc = True
            elif dims and len(dims) == 2 and ts[q + 1] == '[' and ts[q - 1] in ('(', ')', ',', '=', 'return', '+', '?', ':'):
                # (a row of a two-dimension array used as a value: `(u32)info[0]`)
                z, depth = q + 1, 0
                while z < len(ts):
                    depth += (ts[z] == '[') - (ts[z] == ']')
                    if depth == 0:
                        break
                    z += 1
                if z + 1 < len(ts) and ts[z + 1] != '[':
                    esc = True
        if not esc:
            continue
        unit = 4 if ty.startswith('NPtr') else EXT_SIZES.get(ty)
        n = 1
        for d in dims or []:
            n *= int(d, 0)
        # (the slot: the C type's N64 size -- a structure's from the compiler, NSize -- rounded up to 8)
        off, check = ext_slot(ext, '%s.%s' % (fname, name), ty, n, unit)
        if dims:
            sized.append((name, ty, dims))
            row = ''.join('[%s]' % d for d in dims[1:])
            toks[s][1] = 'NArr<%s%s> %s(nm_frame_base + %s);%s   /* %s %s%s: past the frame of the original (which keeps it in registers) */' % (
                ty, row, name, off, check, ty, name, ''.join('[%s]' % d for d in dims))
            if init is not None:
                # (the C's initializer -- the rest of the array zero, as in C -- stored element by element)
                toks[s][1] += ' { const %s nm_init[%s] = %s; for (int nm_i = 0; nm_i < %s; nm_i++) %s[nm_i] = nm_init[nm_i]; }' % (
                    ty, dims[0], init, dims[0], name)
        elif ts[k] == '=':
            toks[s][1] = 'NVar<%s> %s(nm_frame_base + %s);%s %s = (%s)(' % (ty, name, off, check, name, ty)
            toks[e][1] = ');'
            for q in range(s + 1, k + 1):
                toks[q] = ['', '']
            changed = True
            continue
        else:
            toks[s][1] = 'NVar<%s> %s(nm_frame_base + %s);%s   /* %s %s: past the frame of the original */' % (ty, name, off, check, ty, name)
        for q in range(s + 1, e + 1):
            toks[q] = ['', '']
        changed = True
    if not changed:
        return original
    report.setdefault('moved', []).append('extended: ' + ', '.join(ext['items']))
    out = native.untokenize(toks)
    for name, ty, dims in sized:
        out = re.sub(r'NSizeOf<std::remove_cv_t<std::remove_reference_t<decltype\(\s*%s\s*\)>>>::value' % re.escape(name),
                     'NSize<%s%s>::value' % (ty, ''.join('[%s]' % d for d in dims)), out)
    return re.sub(r' nm_comment_(\d+)_ ', lambda m: comments[int(m.group(1))], out)


SAVED = {'s0': 16, 's1': 17, 's2': 18, 's3': 19, 's4': 20, 's5': 21, 's6': 22, 's7': 23, 'fp': 30, 'ra': 31}


def kept_registers(a):
    """frame.keep lines for the registers the original's prologue saves (sw $sN / $fp / $ra, sdc1 $f20-$f30, before its first
    branch or call), at the same offsets."""
    out = []
    for m in re.finditer(r'// 0x[0-9A-F]{8}: (\w+) +(.*)$', original_text(a) or '', re.M):
        op, args = m.group(1), m.group(2)
        if op in ('jal', 'jalr', 'jr') or op.startswith('b'):
            break
        k = re.match(r'\$(\w+), (-?0x[0-9A-F]+|\d+)\(\$sp\)', args)
        if not k:
            continue
        off = int(k.group(2), 16) if 'x' in k.group(2) else int(k.group(2))
        if op == 'sw' and k.group(1) in SAVED:
            line = 'frame.keep(0x%X, ctx->r%d);' % (off, SAVED[k.group(1)])
        elif op == 'sdc1' and k.group(1).startswith('f') and int(k.group(1)[1:]) >= 20:
            line = 'frame.keep_double(0x%X, ctx->%s.u64);' % (off, k.group(1))
        else:
            continue
        if line not in out:
            out.append(line)
    return out


# Negative controls: a native function with one mistake each, which the tester must find (python prove_gen.py neg, after put).
# Each is tested against its own unchanged original, under the name modrep_natneg<k>_<A>.
# (2026-11-13, modnative5: each by its function's NAME, its address looked up in the inventory -- a rebuilt mod moves them)
MUTANTS = [
    ('str_len', 'str_len: the count one short past 40', '    return n;', '    return n - (n > 40);'),
    ('qa_hook_preset', 'qa_hook_preset: a private store wrong when dst is 0', 'qa_preset_dst = dst;', 'qa_preset_dst = dst | (dst == 0);'),
    ('arcade_fast_getup', 'arcade_fast_getup: a view read at the wrong width', 'return fast_getup;', 'return (s32)(s16)fast_getup;'),
    ('arcade_on_demo_setup', 'arcade_on_demo_setup: its call made twice', '    arcade_demo_setup_done();', '    arcade_demo_setup_done(); arcade_demo_setup_done();'),
    ('str_num', 'str_num: the tens digit off by one for 50..59', "(n / 10) % 10)", "(n / 10) % 10 + ((n / 10) % 10 == 5))"),
    # (2026-11-13, modnative2: one for each new mechanism -- each must come out DIFFERENT)
    ('nm_mode', 'nm_mode: a string view one byte off (string_views)', 'plat_config_set(NPtr<char>(lift_mod_address(0, {OFF})), 0);',
     'plat_config_set(NPtr<char>(lift_mod_address(0, {OFF+1})), 0);'),
    # (2026-11-14, modwhole: the 0.83 mod's variant_random_pick has no frame array f -- its frame view now is variant_family_at's
    #  `out`; the same mistake there)
    # (2026-11-18, negfix, approved by the user: 0.84's variant_random_pick is DIFFERENT itself -- that very `out` is handed on
    #  0x14 off in its frame, a conversion bug of its own. The same mutation on variant_family_at, where `out` is its own frame
    #  view and which is proved.)
    ('variant_family_at', 'variant_family_at: a frame view 4 bytes off (frame_locals)', 'NVar<u16> out(nm_frame_base + 0x10);',
     'NVar<u16> out(nm_frame_base + 0x14);'),
    ('plat_game_call', "plat_game_call: one of the mod's copies called with its parameter changed (calls to the copies)",
     '::modrep_func_8012D2EC)(p0);', '::modrep_func_8012D2EC)(p0 ^ 1);'),
    ('announce_file', 'announce_file: a table one word short (rewrite_sizeof, a view global by its map size)', 'n = ((u32)280);',
     'n = ((u32)276);'),   # (2026-11-14, modnative7: native.py writes the mod's size_t as u32 now)
    # (2026-11-14, modnative7: roster_folder_real_key is gone from the 0.82 mod -- the same mechanism in stock_looks_offset)
    ('stock_looks_offset', 'stock_looks_offset: the wrong column of a multi-dimensional view', 'return fields[i][0];',
     'return fields[i][1];'),
    ('commentary_settings', "commentary_settings: its private static compared one off (bind_local_statics)", 'if (key == last) return;',
     'if (key == last + 1) return;'),
    ('draw_fill', 'draw_fill: its fifth parameter, from the stack, taken as the fourth (stack parameters)',
     'draw_fill((int)arg0, (int)arg1, (int)arg2, (int)arg3, (u32)arg4);', 'draw_fill((int)arg0, (int)arg1, (int)arg2, (int)arg3, (u32)arg3);'),
    # (2026-11-13, modnative4: one for each of modnative3's mechanisms and for the merge's DWARF places -- each must come out
    #  DIFFERENT)
    ('sprite_hide', "sprite_hide: the constant its dropped parameter gets, one off (compiled_interface)",
     '(u32)268435471   /* the compiler took it away', '(u32)268435470   /* the compiler took it away'),
    ('story_co_call', "story_co_call: a frame local 4 bytes off its DWARF place (frame_locals, DWARF first)",
     'NArr<s32> w(nm_frame_base + 0x24);', 'NArr<s32> w(nm_frame_base + 0x28);'),
    ('mtx_mul', "mtx_mul: its local array 4 bytes off the original's place, into f20's slot (frame_arrays)",
     'NArr<float> t(nm_frame_base + 0x30);', 'NArr<float> t(nm_frame_base + 0x34);'),
    # (2026-11-18, negfix, approved by the user: variant_random_pick is DIFFERENT itself in 0.84. The same mutation on
    #  roster_folder_id_at, a proved base with the same division: a random number's unsigned remainder by a count.)
    ('roster_folder_id_at', "roster_folder_id_at: a remainder made a quotient (n64_divisions' rewrite)",
     'nm_rem(r, (u32)folder_total)', 'nm_div(r, (u32)folder_total)'),
    ('skel_textures', "skel_textures: a frame array placed by DWARF alone (handed on through a cast: no call's vote), 4 bytes off",
     'NArr<u8> colors(nm_frame_base + 0x24);', 'NArr<u8> colors(nm_frame_base + 0x28);'),
    # (2026-11-13, modnative5: one for each of the user's rules of 2026-11-13 and each new binding -- each must come out
    #  DIFFERENT: what each rule lets through is only what it says)
    ('draw_menu_frame', "a shrunk byte read the other way round (rule (2), NShrunk)",
     'static NShrunk<int, 0, 1> menu_frame(', 'static NShrunk<int, 1, 0> menu_frame('),
    # (2026-11-15, the review of 0.84 (reviewnm084): this control and natneg24 sat on bases that are DIFFERENT themselves --
    #  variant_same_man, story_stop -- so they showed nothing. Each is now on a proved base, the same rule.)
    # (2026-11-18, negfix, approved by the user: variant_listed_base is gone from the build -- nothing calls it since 11-14 and
    #  the linker drops it. The same mutation on variant_family_n, a proved base in 0.84 (variant_base_of, its closer twin, is
    #  DIFFERENT itself there): the same fam_of(id) test of its u16 parameter first (id && id < 0x1000), then the family looked
    #  up, its answer or 0.)
    ('variant_family_n', "its u16 parameter's top bit dropped (rule (1): narrow parameters still take every 16-bit value)",
     'return (s32)variant_family_n((u16)arg0);', 'return (s32)variant_family_n((u16)(arg0 & 0x7FFF));'),
    ('ringstage_replay_lift', "a fast-convention callee given its two floats swapped (rule (3), the stand-in's register list)",
     'nm_ctx->f0.fl = p0; nm_ctx->f2.fl = p1;', 'nm_ctx->f0.fl = p1; nm_ctx->f2.fl = p0;'),
    ('wr_put', "its own fast-convention float parameters taken swapped (rule (3), parameters by register)",
     'wr_put((u32)arg0, arg1, arg2);', 'wr_put((u32)arg0, arg2, arg1);'),
    ('qa_sin', "a loop that never ends in the native code only, for 100 < a < 1000 (rule (3), the loop budget: ran-long)",
     'while ((NM_TICK(), (a > pi))) a -= 2.0f * pi;', 'while ((NM_TICK(), (a > pi))) a -= (a > 100.0f && a < 1000.0f ? 0.0f : 2.0f) * pi;'),
    ('say_text', "past the array's end one slot further than the original (rule (4): set aside only where the original went too)",
     'NPtr<Say> t = &says[n_says++];', 'NPtr<Say> t = &says[n_says < 0 ? (n_says++, n_says - 2) : n_says++];'),
    ('page_items', "its first PG site storing fields only, its padding left (rule (6), compound padding)",
     'nm_assign_whole(a[n++] , (MI){ NPtr<char>(lift_mod_address(0, {OFF})), MI_PAGE, 0, 0, 0, 0, 0, 0, PG_GAME });',
     'a[n++] = (MI){ NPtr<char>(lift_mod_address(0, {OFF})), MI_PAGE, 0, 0, 0, 0, 0, 0, PG_GAME };'),
    # (arcade_number_confirm_done's np_page_index_was, swapped, came out the SAME in run r84n: it only compares a row's s8 value
    #  with an element, which random private words almost never equal -- an equivalent mutant. story_install WRITES its split
    #  array, sv_anim[k] for k < 5: swapped, each write lands in the other element.)
    ('story_install', "a split array's elements 1 and 2 swapped (NSplit)",
     'lift_mod_private_sv_anim_1, lift_mod_private_sv_anim_2', 'lift_mod_private_sv_anim_2, lift_mod_private_sv_anim_1'),
    # (2026-11-14, modnative7: one for each rule the user approved on 2026-11-14 -- each must come out DIFFERENT)
    # (2026-11-15: the memcpy rule's only user is tron_scene_end, not proved, so this control cannot sit on a proved base -- and
    #  the rule counts nothing in 0.84. Kept: it still must be DIFFERENT.)
    ('tron_scene_end', "a structure copy (the compiler's memcpy, run through its caller) taken from the element before (memcpy rule)",
     'else { patch[i] = patch[--n_patch]; i--; }', 'else { --n_patch; patch[i] = patch[n_patch > 0 ? n_patch - 1 : n_patch]; i--; }'),
    # (2026-11-14, modwhole: the 0.83 mod's variant_random_pick has no two-parameter stub left: its u16 one, variant_family_n's)
    # (2026-11-18, negfix, approved by the user: 0.84's variant_random_pick is DIFFERENT itself -- variant_family_at's `out`, in
    #  its frame, is handed on 0x14 off -- and so is every caller of variant_family_n's stub. The same mutation on first_free, a
    #  proved base: its u16 stub sel_family's, a callee not proved to ignore the bits above, as variant_family_n was.)
    # (2026-11-18, negfix3, approved by the user: the stub by its name, {STUB:sel_family} -- negatives() puts in the address the
    #  build's stub calls; a rebuild moved it from modfn_810D57DC to modfn_810D5B78 and the literal went stale)
    ('first_free', "a narrow (u16) argument given a high bit, to a callee not proved to ignore it (strict narrow arguments)",
     '::{STUB:sel_family})(p0); }   // (the original calls it', '::{STUB:sel_family})((u32)p0 | 0x10000u); }   // (the original calls it'),
    # (2026-11-14, modnative7: the private-values rule (NM_RULE_WRITES, pending review) -- a difference at a value the mod's writes
    #  DO give the variable (arcade_speed: 0 or 1) must still be DIFFERENT under it)
    ('arcade_match_step', "its private arcade_speed tested the other way round, 1 for 0 (private values: a value the mod writes)",
     'if (!arcade_speed) { step_acc = 0; return; }', 'if (arcade_speed == 1) { step_acc = 0; return; }'),
    # (2026-11-14, modnative7: the private-object guard, approved by the user 2026-11-14 -- each must come out DIFFERENT with it on)
    ('say', "an extra call whenever a callee's stand-in changed the private n_slots during the call (guard A: mirrored writes)",
     'plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16)));',
     '{ const int nm_before = n_slots; plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16))); '
     'if (n_slots != nm_before) plat_comm_event(0, 0, 0, 0); }'),
    ('str_num', "a byte written one past the end through its pointer parameter (guard B: redrawn pointer inputs)",
     '    out[k] = 0;\n}', '    out[k + 1] = 0;\n}'),
]


# (2026-11-14, modnative7: F6 of rule (1)'s review) Two more negative controls for rule (1), made only with it on (NM_RULE_WRITES=1;
#  negatives() adds them after MUTANTS): each must come out DIFFERENT under the rule.
#   - a difference only at a RANGE bound's endpoint (rr_walk_rule is 0..1024 by its writes: only 1024 differs);
#   - a difference only at a value a mod stand-in WRITES mid-run (n_slots, 0..255 by its writes: only when plat_comm_event's
#     stand-in changed it to 7).
RULE1_MUTANTS = [
    ('rr_walk_in_step', "rr_walk_rule left out only at its bound's endpoint, 1024 (rule (1): range bounds)",
     '(*(NPtr<u32>)(0x800A9950u)) |= rr_walk_rule; rr_walk_rule = 0;',
     '(*(NPtr<u32>)(0x800A9950u)) |= (rr_walk_rule == 1024 ? 0u : rr_walk_rule); rr_walk_rule = 0;'),
    ('say', "an extra call only when a stand-in wrote n_slots to 7 mid-run (rule (1): values written by a stand-in)",
     'plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16)));',
     '{ const int nm_before = n_slots; plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16))); '
     'if (n_slots != nm_before && n_slots == 7) plat_comm_event(0, 0, 0, 0); }'),
]



# (2026-11-14, modnative7) One deliberately wrong control for each of 0.84's three new rules (approved by the user, each pending
# its own review: REVIEW_pure_thru.txt, REVIEW_declared_params.txt, REVIEW_frame_words.txt), made only with that rule on
# (negatives() adds them after MUTANTS). Each must come out DIFFERENT under its rule.
FIXES084_MUTANTS = [
    # (one Newton step fewer, i < 15, came out the SAME in run c84 -- 16 steps from max(v, 1) have converged for every value the
    #  tests gave, so that mutant changes nothing: an equivalent mutant, not a control. Replaced by 3 steps, which have not.)
    ('NM_PURE_THRU', 'hc_camera', "its pure callee hc_sqrt, run through it, given 3 Newton steps for 16 (NM_PURE_THRU: a through "
     "callee's own native code is compared, by its caller's effects)",
     'for (int i = 0; (NM_TICK(), ( i < 16)); i++) x = 0.5f * (x + v / x); return x; }',
     'for (int i = 0; (NM_TICK(), ( i < 3)); i++) x = 0.5f * (x + v / x); return x; }'),
    # (2026-11-15, the review of 0.84 (reviewnm084): hd_frame is DIFFERENT itself, so its control showed nothing -- replaced by
    #  the reviewer's two, on proved bases. The frame-word mark was REJECTED by the review and is gone, with its control.)
    ('NM_DECLARED_PARAMS', 'say', "plat_comm_event's 4th declared parameter (a3) given its low bit flipped (NM_DECLARED_PARAMS: "
     "every declared parameter is still compared)",
     'plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16)));',
     'plat_comm_event(ev, fa, fb, (int)((u32)(arg & 0xFF) | ((fa >= 0 && female((s16)fa)) ? 0x100u : 0u) | (m << 16)) ^ 1);'),
    # (2026-11-18, negfix, approved by the user, the edit placed as the review asked: 0.84's variant_pick_member has no
    #  `member_n(f[i])` and is DIFFERENT itself -- variant_family_at's `out` handed on 0x14 off in its frame. The same control on
    #  fam_head, a proved base: in its family loop the stub roster_id_on (modfn_810C3418, declared mask 1) is given the next id,
    #  as member_n's stub was -- only that stub's declared a0 changes, no branch and no other call.)
    ('NM_DECLARED_PARAMS', 'fam_head', "roster_id_on given the next id in its family loop (NM_DECLARED_PARAMS: a mod "
     "callee's declared parameter is still compared)",
     'return !variant_wrestler_hidden(id) && roster_id_on(id);', 'return !variant_wrestler_hidden(id) && roster_id_on((u16)(id ^ 1u));'),
    # (the aligned structure-pointer rule, approved 00:40 -- tester FUZZ_NO_NM_ALIGNED turns it off; this control is made with
    #  NM_ALIGNED=1: a wrong field written for every a, aligned ones too, must stay DIFFERENT)
    ('NM_ALIGNED', 'page_items', "its first PG site's page number written one too high (aligned rule: a difference on aligned data "
     "still counts)",
     'nm_assign_whole(a[n++] , (MI){ NPtr<char>(lift_mod_address(0, {OFF})), MI_PAGE, 0, 0, 0, 0, 0, 0, PG_GAME });',
     'nm_assign_whole(a[n++] , (MI){ NPtr<char>(lift_mod_address(0, {OFF})), MI_PAGE, 0, 0, 0, 0, 0, 0, PG_GAME + 1 });'),
]
fixes084_on = lambda: [m[1:] for m in FIXES084_MUTANTS if os.environ.get(m[0]) == '1']

# (2026-11-14, modnative7) The private-values rule's POSITIVE control: a difference only at values the mod never writes
# (arcade_speed is only ever 0 or 1). DIFFERENT without the rule, the same as the original with it. `prove_gen.py pos`.
POSITIVE = [('arcade_match_step', "arcade_speed tested as != 1 for == 0 -- the same for 0 and 1, the only values the mod writes",
             'if (!arcade_speed) { step_acc = 0; return; }', 'if (arcade_speed != 1) { step_acc = 0; return; }')]


def negatives(mutants=None, prefix='natneg'):
    cands = json.load(open(OUT_CANDIDATES))
    text = open(OUT_ORIGINALS, encoding='utf8').read()
    names = []
    by_name = {r['name']: a for a, r in json.load(open(os.path.join(HERE, 'inventory.json'))).items()}
    if mutants is None:
        mutants = MUTANTS + (RULE1_MUTANTS if rule_writes() else [])     # (F6: rule (1)'s own, only with it on)
        mutants = mutants + fixes084_on()     # (0.84's three rules: each one's own, only with it on)
    for k, (fname, what, old, new) in enumerate(mutants):
        a = by_name[fname]
        v = dict(cands['modrep_nat_%s' % a])
        if '{OFF}' in old:
            # (2026-11-14, modnative7: a place in the mod's image, which moves with every build: the one the C++ has there now --
            #  {OFF} -- and, in the mutant, {OFF+1} the byte after it)
            m = re.search(re.escape(old).replace(re.escape('{OFF}'), '(0x[0-9A-F]+)'), v['cpp'])
            assert m, (a, old)
            old, new = old.replace('{OFF}', m.group(1)), new.replace('{OFF+1}', '0x%X' % (int(m.group(1), 16) + 1)).replace('{OFF}', m.group(1))
        for stub in sorted(set(re.findall(r'\{STUB:(\w+)\}', old + new))):
            # (2026-11-18, negfix3, approved by the user: a mod callee's stub is named by its address, modfn_<A>, which moves with
            #  every mod build -- natneg26 went stale so on 11-18. {STUB:<name>} is the address the base's own stub for <name>
            #  calls now: its one line `static ... <name>(...) { return lift_call<...>(nm_ctx, ::modfn_<A>)...`. Not exactly one
            #  such line: the control can't be made -- it stops here, saying which.)
            hits = re.findall(r'^static [^\n]*?\b%s\([^)\n]*\) \{ return lift_call<[^\n]*?>\(nm_ctx, ::(modfn_[0-9A-F]+)\)' % re.escape(stub),
                              v['cpp'], re.M)
            if len(hits) != 1:
                raise SystemExit('negative control %d (%s on %s %s): the stub {STUB:%s} is not one stub line in its C++ (%d found: %s)'
                                 ' -- not making the controls' % (k, what, fname, a, stub, len(hits), ', '.join(hits) or 'none'))
            old, new = old.replace('{STUB:%s}' % stub, hits[0]), new.replace('{STUB:%s}' % stub, hits[0])
        if v['cpp'].count(old) != 1 and '((u32)' in old:
            # (candidates made before native.py wrote the mod's size_t as u32: the same place, as size_t)
            old, new = old.replace('((u32)', '((size_t)'), new.replace('((u32)', '((size_t)')
        assert v['cpp'].count(old) == 1, (a, old)
        n = 'modrep_%s%d_%s' % (prefix, k, a)
        v['cpp'] = v['cpp'].replace('nm_%s' % a, 'nm%s%d_%s' % (prefix[3:], k, a)).replace(old, new).replace('modrep_nat_%s(' % a, '%s(' % n).replace('// The mod\'s', '// (negative control: %s)\n// The mod\'s' % what)
        v['native'] = dict(v['native'], negative=what)
        cands[n] = v
        m = re.search(r'^RECOMP_FUNC void modrep_nat_%s\(.*?^;\}\n' % a, text, re.M | re.S)
        text += '\n' + m.group(0).replace('modrep_nat_%s(' % a, '%s(' % n, 1)
        names.append(n)
    open(OUT_ORIGINALS, 'w', newline='\n').write(text)
    json.dump(cands, open(OUT_CANDIDATES, 'w'))
    print(' '.join(names))


def check_one(job):
    """Compiles one candidate's C++ alone, as the tester's generated file has it (syntax only). (name, None) or (name, errors)."""
    import subprocess
    name, v, work = job
    port = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp')
    lines = ['#include "fuzz.h"', '#include "lift_call.h"', '#include <cstring>',
             'extern "C" int32_t lift_mod_section_address[];',
             'inline u32 lift_mod_address(int section, u32 offset) { return (u32)lift_mod_section_address[section] + offset; }']
    lines += ['extern "C" void %s(uint8_t* rdram, recomp_context* ctx);' % c for c in v.get('callees', [])]
    lines += ['extern "C" u8 lift_mod_private_%s[];' % re.sub(r'\W', '_', s) for s in v.get('private', [])]
    lines += ['namespace game {', v['cpp'], '}']
    path = os.path.join(work, name + '.cpp')
    open(path, 'w', newline='\n').write('\n'.join(lines))
    cmd = [native.CLANGXX, '-std=c++20', '-fsyntax-only', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-Wno-everything',
           '-DARCADE_LIFT_FUZZ', '-I', os.path.join(port, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), '-I', os.path.join(port, 'include'),
           '-I', native.GAME_SRC, '-I', os.path.join(LIFT, 'fuzz'), path]
    p = subprocess.run(cmd, capture_output=True, text=True)
    errs = [l.split(' error: ', 1)[1] for l in p.stderr.splitlines() if ' error: ' in l]
    return name, (errs[:3] if p.returncode else None)


def check(names):
    """Each named candidate compiled alone; those that fail are taken out of candidates.json and listed (check_failed.json)."""
    import concurrent.futures
    cands = json.load(open(OUT_CANDIDATES))
    work = os.path.join(HERE, 'check')
    os.makedirs(work, exist_ok=True)
    failed = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for name, errs in pool.map(check_one, [(n, cands[n], work) for n in names]):
            if errs:
                failed[name] = errs
                del cands[name]
    json.dump(cands, open(OUT_CANDIDATES, 'w'))
    json.dump(failed, open(os.path.join(HERE, 'check_failed.json'), 'w'), indent=1)
    print('compiled alone: %d, failed: %d' % (len(names), len(failed)))
    for n, e in failed.items():
        print(' ', n, e[0][:160])
    open(os.path.join(HERE, 'batch_checked.txt'), 'w').write(' '.join(n for n in names if n not in failed))


def main():
    if sys.argv[1] == 'check':
        check(sys.argv[2:])
        return
    inv, census, data, private = load()
    if sys.argv[1] == 'neg':
        negatives()
        return
    if sys.argv[1] == 'pos':
        negatives(POSITIVE, 'natpos')
        return
    if sys.argv[1] == 'list':
        ok, why = [], {}
        for a, r in sorted(inv.items(), key=lambda x: x[1]['instructions']):
            w = why_not(a, r, inv, census, data, private)
            if w is None:
                ok.append(a)
            else:
                why.setdefault(re.sub(r':.*', '', w) if w.startswith(('does not', 'uses', 'reaches')) else w, []).append(a)
        print('entries possible now: %d functions, %d instructions' % (len(ok), sum(inv[a]['instructions'] for a in ok)))
        if use_whole():
            lets = {a: WHOLE_NOTES[a] for a in ok if a in WHOLE_NOTES}
            print('of them only with NM_WHOLE=1 (refusals for the tester\'s sake let through): %d functions, %d instructions' % (
                len(lets), sum(inv[a]['instructions'] for a in lets)))
            json.dump(lets, open(os.path.join(HERE, 'whole_lets.json'), 'w'), indent=1)
        print(' '.join(ok))
        for w, l in sorted(why.items(), key=lambda x: -len(x[1])):
            print('%4d (%6d ins) %s' % (len(l), sum(inv[a]['instructions'] for a in l), w))
        json.dump({'ok': ok, 'why': why}, open(os.path.join(HERE, 'provable_now.json'), 'w'), indent=1)
    elif sys.argv[1] == 'put':
        # (2026-11-14, modnative7: NM_CANDIDATES_BASE names another base -- tools/modnative: the project's own candidates.json,
        #  so the game's candidates stay in it; any native entries already there are made again)
        base_path = os.environ.get('NM_CANDIDATES_BASE') or os.path.join(HERE, 'candidates_base.json')
        if not os.path.exists(base_path):
            import shutil
            shutil.copyfile(os.path.join(LIFT, 'candidates.json'), base_path)
        cands = {k: v for k, v in json.load(open(base_path)).items() if not k.startswith(('modrep_nat_', 'modrep_natneg', 'modrep_natpos'))}
        originals = []
        thru_written = {}       # (rule (5): each internal function run through a caller, its original once)
        for a in sys.argv[2:]:
            r = inv[a]
            w = why_not(a, r, inv, census, data, private)
            if w:
                print('%s %s: %s' % (a, r['name'], w))
                continue
            text = original_text(a)
            if text is None:
                print('%s: no Stage 1 translation' % a)
                continue
            # (2026-11-13, modnative5: rule (5) -- the internal functions run through it: their private data is the test's too,
            #  their originals go in as modrep_natthru_<A>, and every call to one, in the caller or in another, goes there)
            _, thru = thru_calls(a, r, inv)
            private_here = sorted(set(r['data_private']).union(*[set(inv[t]['data_private']) for t in thru])) if thru else r['data_private']
            for t in thru:
                text = re.sub(r'\bmodfn_%s\(rdram, ctx\)' % t, 'modrep_natthru_%s(rdram, ctx)' % t, text)
                if t not in thru_written:
                    body = original_text(t)
                    for t2 in thru:
                        body = re.sub(r'\bmodfn_%s\(rdram, ctx\)' % t2, 'modrep_natthru_%s(rdram, ctx)' % t2, body)
                    thru_written[t] = body.replace('RECOMP_FUNC void modrep_nat_%s(' % t, 'RECOMP_FUNC void modrep_natthru_%s(' % t, 1)
            # (2026-11-13: one the entry can't be made for is reported and left out, not the end of the batch)
            lets = list(WHOLE_NOTES.get(a, []))
            if any(w.startswith('does not compile') for w in lets):
                EXTEND_FOR.add(a)
            try:
                entry = converted(a, r, census, private_here, inv)
            except SystemExit as e:
                print('%s %s: %s' % (a, r['name'], e))
                continue
            if lets:
                # (2026-11-14, modwhole: NM_WHOLE=1 -- what the tester-side checks said, kept with the entry)
                entry['native']['whole_refusals'] = lets
                only = [w for w in lets if w.startswith(WHOLE_ONLY)]
                if only:
                    entry['native']['whole_only'] = only[0]      # (the tester can't run its original: the gameplay tier only)
            if not entry['native'].get('whole_only'):
                originals.append(text)
            cands['modrep_nat_%s' % a] = entry
        open(OUT_ORIGINALS, 'w', newline='\n').write(
            "// The mod's own functions as Stage 1 translates them (scratch/agents/final081/aot/mod_aot, nm_aot_nomercy_arcade_<A>), as\n"
            "// modrep_nat_<A>: the originals the native mod is proved against (scratch/agents/modnative1 prove_gen.py, 2026-11-12).\n\n" +
            '\n'.join(originals + list(thru_written.values())))
        json.dump(cands, open(OUT_CANDIDATES, 'w'))
        # (2026-11-13: which local arrays went into the original's frame, and which could not: frame_arrays)
        json.dump(FRAME_REPORT, open(os.path.join(HERE, 'frame_arrays.json'), 'w'), indent=1)
        print('put %d' % len(originals))


if __name__ == '__main__':
    main()
