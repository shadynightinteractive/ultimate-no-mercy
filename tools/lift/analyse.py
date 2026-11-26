#!/usr/bin/env python3
# Step 4, first measure: reads RecompiledFuncs (each instruction's MIPS is in a comment, its call by name in the code),
# works out per function which registers it reads before writing (its real parameters), and how many functions keep
# to the MIPS calling rules a tool can rely on. Output: tools/lift/analysis.json, and a summary.
import re, glob, json, collections, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

SRC = sys.argv[1] if len(sys.argv) > 1 else 'exe/WWFNoMercyRecomp/RecompiledFuncs'
FUNC = re.compile(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\)')
INS = re.compile(r'^\s*// 0x([0-9A-F]{8}): (\S+)\s*(.*)$')
CALL = re.compile(r'\b(\w+)\(rdram, ctx\);')

funcs = {}
# (The mod's replacement functions too, as modrep_<the game function's name> (modrep.py), and -- for what they read, never
#  converted or written out -- the mod's other functions, modfn_<address>: see "The mod's copies" below.)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modrep
for path in sorted(glob.glob(SRC + '/*.c')) + modrep.sources() + modrep.analysis_sources():
    cur = None
    last = None
    own = modrep.is_source(path)     # (the mod's calls under the game's names: modrep.game_names, 2026-11-12)
    for line in open(path, encoding='utf8', errors='replace'):
        if own:
            line = modrep.game_names(line)
        m = FUNC.match(line)
        if m:
            cur = {'name': m.group(1), 'ins': {}, 'calls': {}, 'lookup': set()}
            funcs[cur['name']] = cur
            last = None
            continue
        if cur is None:
            continue
        m = INS.match(line)
        if m:
            a = int(m.group(1), 16)
            last = a
            cur['ins'].setdefault(a, (m.group(2), m.group(3).strip()))
            continue
        if last is not None:
            for c in CALL.findall(line):
                cur['calls'].setdefault(last, c)
            if 'LOOKUP_FUNC' in line:
                cur['lookup'].add(last)

from mips import *

# The jump tables, so a switch's cases are followed (to_asm.py reads them from the translated code).
import to_asm
for name, f in to_asm.read_functions(merge=False, extra=modrep.analysis_sources()).items():
    if name in funcs:
        funcs[name]['tables'] = f['tables']
# A function the translator split in pieces is read as the whole of it, under its entry's name (merge.py): what the pieces read is
# what it reads. The pieces keep their own entries, as before. Their sizes stay their own, so the totals don't count the shared
# instructions twice. (Merging split functions: the split-function agent's prototype, 2026-11-07, scratch/lift_merge.)
import merge
own_size = {n: len(f['ins']) for n, f in funcs.items()}
funcs = merge.apply(funcs)
MERGED = {n for n, f in funcs.items() if 'pieces' in f}

HAS_EFFECTS = {'sb', 'sh', 'sw', 'sd', 'swl', 'swr', 'swc1', 'sdc1', 'jr', 'jal', 'jalr', 'j', 'b', 'syscall', 'break', 'cache', 'sync',
               'mtc0', 'ctc1', 'cfc1', 'eret', 'tlbp', 'tlbr', 'tlbwi', 'tlbwr'}


def result_unused(mn, defs, live_after):
    """An instruction whose results nobody reads, and which does nothing else: what it reads doesn't matter either. (The allocator,
    func_800007D4, copies s1 into s0 in a delay slot on the way out, and s0 is restored straight after: counting that as reading
    s1 made every function calling the allocator look as if it passed s1 in.)"""
    if not defs or mn in HAS_EFFECTS or mn in BRANCH or mn.startswith('c.'):
        return False
    # What the function may return: liveness stops at `jr $ra`, so it can't see the caller reading v0/v1/f0/f1. Until this was
    # added (found by the 2026-11-05 review), `jr $ra; lw $v0, 4($a0)` looked as if it didn't read a0, and converted callers
    # dropped parameters (func_80003F3C passed nothing to func_80003EA4).
    if defs & {'v0', 'v1', 'f0', 'f1'}:
        return False
    return not (defs & live_after)


def jump_targets(f):
    """Where f jumps into another function (a `j` the translator wrote as a call then a return): {address noted at: target}."""
    return {a: c for a, c in f['calls'].items()
            if f['ins'].get(a - 4, ('',))[0] not in ('jal', 'jalr') and f['ins'].get(a, ('',))[0] not in ('jal', 'jalr')}


# The mod's plat_game_call(fn, a, b, c) is a switch over its game functions (mod/recomp/hooks_recomp.c), so as a callee it reads what
# all of them read: a thin copy like story_card_text (func_800DD740) took on the s3, s4, s7, fp and f26 of functions it can't reach,
# and its callers were held to registers no path reads (2026-11-09: three functions proved in 0.71.0 and 0.72.0 came up different only
# on those registers, func_80107D84, func_801249AC, func_801278DC). A call whose first parameter is a constant set in the same run of
# instructions goes to that one case: what that case's game function reads. Anything else keeps the whole switch. The cases come from
# the source the mod is built from; the mod's map says which function is plat_game_call. No map, no narrowing.
#
# (2026-11-13, agent 78dc) The map is the one of the build modrep.py translated (modrep.MAP: LIFT_MOD_BUILD's, as modrep.py and its
#  c_patch_ranges read it), and the source the mod tree that build belongs to (<mod>/build/mod_six -> <mod>/src, <mod>/recomp).
#  Until now both were always the live mod's. final082's staged run translated its own mod build, where plat_game_call had moved
#  to 0x811131E8, while the live map still had the 0.81 build's 0x8110ECB8: no function of the translated mod starts there, so
#  nothing was narrowed and func_800DD740's copy took on all of the switch's s3, s4, s7, fp and f26 again -- the 2026-11-09 case
#  once more. func_801278DC came out DIFFERENT (calls@A:converted=DB04,original=70707070: the s7 the stand-in for func_800DD6C4
#  was said to read, which the original has loaded from its 6th parameter and the converted function rightly never sets). So a
#  map that names no translated function of plat_game_call's size now stops the analysis (check_game_call, below).
def game_call_table():
    root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
    mod_root = os.path.dirname(os.path.dirname(os.path.abspath(modrep.MOD_BUILD)))
    if not os.path.exists(os.path.join(mod_root, 'recomp', 'hooks_recomp.c')):
        mod_root = os.path.join(root, 'mod')      # (a build folder on its own: the live mod's source, as before)
    try:
        the_map = open(modrep.MAP, encoding='utf8', errors='replace').read()
        header = open(os.path.join(mod_root, 'src', 'arcade.h'), encoding='utf8', errors='replace').read()
        hooks = open(os.path.join(mod_root, 'recomp', 'hooks_recomp.c'), encoding='utf8', errors='replace').read()
    except OSError:
        return None, {}
    at = re.search(r'^\s*([0-9a-f]{8})\s+[0-9a-f]{8}\s+([0-9a-f]+)\s+\d+\s+plat_game_call\s*$', the_map, re.M)
    if at:
        GAME_CALL_SIZE[0] = int(at.group(2), 16)
    enum = re.search(r'enum\s*\{([^}]*\bGF_PAGE_INIT\b[^}]*)\}', header)
    body = re.search(r'^s32 plat_game_call\(int fn, u32 a0, u32 a1, u32 a2\) \{\n    switch \(fn\) \{\n(.*?)^    \}\n', hooks, re.M | re.S)
    if not (at and enum and body):
        return None, {}
    lines = [re.sub(r'//.*', '', l).strip() for l in enum.group(1).split('\n')]
    names = [l.rstrip(',') for l in lines if l]
    if not all(re.fullmatch(r'GF_\w+', n) for n in names):
        return None, {}             # (an explicit value, or anything else this doesn't count through)
    table = {}
    for case, rest in re.findall(r'^\s*case (GF_\w+):(.*)$', body.group(1), re.M):
        called = [c for c in re.findall(r'\b([A-Za-z_]\w*)\s*\(', rest) if c != 'return']
        if called and all(re.fullmatch(r'func_[0-9A-F]{8}(?:_ovl_\w+)?', c) and c in funcs for c in called) and case in names:
            table[names.index(case)] = tuple(called)
    return 'modfn_' + at.group(1).upper(), table

GAME_CALL_SIZE = [0]        # (plat_game_call's size in bytes, from the map: check_game_call)
GAME_CALL, GAME_CALL_CASES = game_call_table()


def check_game_call():
    """(2026-11-13, agent 78dc) With the mod's own functions read (modrep_modfuncs.c), the map's plat_game_call must be one of
    them, all of it: else the map is another build's than the one translated, and every call through it would keep the whole
    switch without a word. Stops the analysis then, rather than write registers no path reads into analysis.json."""
    if not modrep.analysis_sources() or GAME_CALL is None:
        return
    f = funcs.get(GAME_CALL)
    if f is None or len(f['ins']) * 4 != GAME_CALL_SIZE[0]:
        raise SystemExit('analyse.py: the mod map %s puts plat_game_call at %s (0x%X bytes), which is %s of the translated mod '
                         '(%s): the map is not the translated build\'s -- set LIFT_MOD_BUILD to the build modrep.py translated'
                         % (modrep.MAP, GAME_CALL, GAME_CALL_SIZE[0], 'no function' if f is None else
                            'a function of 0x%X bytes' % (len(f['ins']) * 4), modrep.analysis_sources()[0]))

check_game_call()
CONSTANT_A0 = re.compile(r'^\$a0,\s*\$(?:zero|0),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$')

def constant_a0(f, a):
    """The constant in a0 at the call at a (its delay slot included), when one instruction in the same run of instructions sets it."""
    def value(mn, ops):
        m = CONSTANT_A0.match(ops.strip()) if mn in ('addiu', 'ori') else None
        return int(m.group(1), 0) if m else None
    if 'preds' not in f:
        f['preds'] = collections.defaultdict(list)
        for x in f['addrs']:
            for s in f['succ'][x]:
                f['preds'][s].append(x)
    if a + 4 in f['ins']:
        mn, ops = f['ins'][a + 4]
        if 'a0' in uses_defs(mn, ops)[1]:
            return value(mn, ops)
    x = a
    for _ in range(64):
        ps = f['preds'].get(x, [])
        if len(ps) != 1:
            return None
        x = ps[0]
        mn, ops = f['ins'][x]
        if mn in ('jal', 'jalr'):
            return None
        if 'a0' in uses_defs(mn, ops)[1]:
            return value(mn, ops)
    return None

def game_call_target(f, a, c):
    """The callee a call at a really has: one case of plat_game_call, when its first parameter says which."""
    if c == GAME_CALL and c is not None:
        k = constant_a0(f, a)
        if k in GAME_CALL_CASES:
            return GAME_CALL_CASES[k]
    return c

def entry_reads(f, callee_use):
    """The registers a function reads before setting them, given what each function it calls reads.

    At a return, what the function hands back is read: v0-f1 (result_unused) and its 'results' (HANDED, mips.returned_in).
    After a call, what the callee hands back is its own, not the caller's (as the calling rules' scratch registers are).

    A jump into another function counts as what it is: the target runs next with the registers as they are, and returns for this one.
    So what the target reads is read here too. (Until 2026-11-07 the analysis ended at the jump: a stub that jumps to the mod's / engine's
    rw_ code looked as if it read nothing, and the converted func_80128F5C passed its rw_800EC990 none of a0, a1, a2 -- shipped in 0.61.0,
    found by the split-function agent, scratch/lift_merge; func_80120C00 passed func_8012096C two of its four.)"""
    live = {a: set() for a in f['addrs']}
    jumps_at = jump_targets(f)
    handed = HANDED.get(f['name'], set())      # (beyond v0-f1: liftfix, 2026-11-16)
    again = True
    while again:
        again = False
        for a in reversed(f['addrs']):
            out = set()
            for s in f['succ'][a]:
                out |= live.get(s, set())
            if a in jumps_at and not f['succ'][a]:
                # (Into another function, which returns for this one: what it reads, and what this one hands back that it
                #  leaves as it is.)
                out |= callee_use(jumps_at[a]) | (handed - MAY_CHANGE.get(jumps_at[a], set()))
            elif not f['succ'][a] and is_return_slot(f, a):
                out |= handed
            mn, ops = f['ins'][a]
            u, d = uses_defs_at(f, a, f['succ'])
            if mn in ('jal', 'jalr'):
                c = f['calls'].get(a + 4) or f['calls'].get(a)
                if mn == 'jal' and not c:
                    c = looked_up(ops)
                c = game_call_target(f, a, c)
                after_call = (live.get(a + 8, set()) if (a + 4) in f['ins'] else out)
                after_call = (after_call - CLOBBER - handed_by(c)) | callee_use(c) | u
                if (a + 4) in f['ins']:
                    dmn, dops = f['ins'][a + 4]
                    du, dd = uses_defs_in(f, a + 4)      # (a stack parameter stored in the delay slot is read: mips.pass_on_stores)
                    new = after_call if result_unused(dmn, dd, after_call) else (after_call - dd) | du
                else:
                    new = after_call
            elif a - 4 in f['ins'] and f['ins'][a - 4][0] in ('jal', 'jalr'):
                new = out
            elif result_unused(mn, d, out):
                new = out
            else:
                new = (out - d) | u
            if new != live[a]:
                live[a] = new
                again = True
    return (live[f['addrs'][0]] - {'sp', 'ra', 'zero'}) if f['addrs'] else set()


def read_after_calls(f):
    """{callee: what f's own instructions read after its calls to it} (mips.callers_read_after, with what f hands back read at
    its returns), and for a function f jumps into, f's own 'results' (it returns for f)."""
    found = {}
    for a, (mn, ops) in f['ins'].items():
        if mn not in ('jal', 'jalr'):
            continue
        c = f['calls'].get(a + 4) or f['calls'].get(a) if mn == 'jal' else None
        if mn == 'jal' and not c:
            c = looked_up(ops)
        c = game_call_target(f, a, c)
        if c is None:
            continue
        regs = callers_read_after(f, a, f['succ'], HANDED.get(f['name'], set()), lambda pc: changed_by_call(f, pc))
        for name in (c if isinstance(c, tuple) else (c,)):
            found.setdefault(name, set()).update(regs)
    for a, target in jump_targets(f).items():
        if not f['succ'].get(a):
            found.setdefault(target, set()).update(HANDED.get(f['name'], set()))
    return found


def changed_by_call(f, a):
    """What the call at a may change (mips.may_change; a looked-up call: any of the functions at its address), or None when
    not known (a call through a pointer, a system function: the calling rules' scratch registers then)."""
    mn, ops = f['ins'][a]
    if mn != 'jal':
        return None
    c = f['calls'].get(a + 4) or f['calls'].get(a)
    if not c:
        c = looked_up(ops)
    c = game_call_target(f, a, c)
    names = c if isinstance(c, tuple) else (c,)
    if not names or any(n not in MAY_CHANGE for n in names):
        return None
    return set().union(*(MAY_CHANGE[n] for n in names))


def is_return_slot(f, a):
    """a is the delay slot of a `jr $ra`: the last instruction before the function returns."""
    before = f['ins'].get(a - 4, ('', ''))
    return before[0] == 'jr' and '$ra' in before[1]


def handed_by(c):
    """What the callee(s) of a call hand back beyond v0-f1 (a tuple: any of the functions at a looked-up address)."""
    return set().union(*(HANDED.get(x, set()) for x in c)) if isinstance(c, tuple) else HANDED.get(c, set())


state = {n: set() for n in funcs}
for f in funcs.values():
    f['addrs'], f['succ'] = cfg(f)
# What each function hands back beyond v0-f1 (mips.returned_in; worked out with the read sets below), and the registers each may
# leave changed (mips.may_change: only those can be handed back -- one it never touches is its caller's own, still there).
HANDED = {}
MAY_CHANGE = may_change(funcs)

# A jal the translator resolves at run time (LOOKUP_FUNC: a call into whichever overlay is loaded there) goes to a fixed address: it
# reads what the functions there read, any of them, as a tuple of their names. Not a tail the translator started part-way through a
# function, which no working call reaches (mips.functions_at). (Until 2026-11-07 such a call was taken to read all of a0-a3, f12 and
# f14: func_8012D614 calls func_800D9AB0 with three parameters, so it looked as if it passed a3, f12 and f14 through -- and its
# callers in turn.) If no function starts there, the old assumption stays.
AT_ADDRESS = functions_at(funcs)
def looked_up(ops):
    try:
        names = AT_ADDRESS.get(int(ops.strip(), 0))
    except ValueError:
        return None
    return tuple(names) if names else None

# What the runtime's own functions read (libultra as the port runs it: osRecvMesg_recomp and the rest), from their C++ source in the
# port's N64ModernRuntime (system_reads.py, checked by hand 2026-11-07). Until then every one of them was taken to read all of a0-a3,
# f12 and f14, as a function it can't see into. One whose source can't be read that way -- a stack parameter, ctx handed on --
# keeps that assumption.
import system_reads
SYSTEM = system_reads.table(sorted({c for f in funcs.values() for c in f['calls'].values() if c not in funcs}))
def callee_use(c):
    if isinstance(c, tuple):
        return set().union(*(callee_use(x) for x in c))
    if c in state:
        return state[c]
    return SYSTEM.get(c, ABI_ARGS)
# Registers a function reads on some path, but whose value never changes what it does: the random-input tester checked
# (lift.py independence). They aren't inputs, so they aren't passed up to its callers either.
INDEPENDENT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'independent.json')
independent = {n: set(r) for n, r in json.load(open(INDEPENDENT)).items()} if os.path.exists(INDEPENDENT) else {}
# And ones read by hand, where the test can't decide: each with its reasons in the file.
BY_HAND = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'independent_by_hand.json')
for n, v in (json.load(open(BY_HAND)).items() if os.path.exists(BY_HAND) else []):
    if n != 'about':
        independent.setdefault(n, set()).update(v['registers'])
# (The independence test ran a split function's entry with stand-ins for its pieces, so what it found doesn't hold for the whole: dropped
#  until it's run on the put-together function.)
for n in MERGED:
    independent.pop(n, None)

# (The instruction after a call, its delay slot, runs first, then the call. The translator writes the call after the delay slot,
# so it's noted against that instruction's address. Until 2026-11-05 this took the delay slot as the call, ignoring what it sets,
# and didn't find the call itself: most functions looked as if they read registers they don't, and every call as if it read all
# four parameters. entry_reads has it right.)
# The mod's copies (modrep.py). In the game a replaced function's entry leads to the mod's copy; without the mod (or where the
# loader refuses it) to the game's own. So what a replaced function reads, for its callers, is what EITHER reads: the game's
# func_X takes on its copy modrep_func_X's reads too, and its callers are checked against both (2026-11-08: no mod copy reads
# more itself, but some reach -- through the mod's plat_game_call -- game functions that read s3, s4, fp or f26 on paths that
# never set them, and converted callers that change those registers were never checked against that). Without the mod's
# copies (LIFT_MODREP=0, or none generated) nothing here changes.
MOD_TWIN = {modrep.replaced(n): n for n in funcs if modrep.is_modrep(n) and modrep.replaced(n) in funcs}
changed = True
rounds = 0
while changed and rounds < 40:
    changed = False
    rounds += 1
    after = {}
    for n, f in funcs.items():
        use = entry_reads(f, callee_use) - independent.get(n, set())
        for c, regs in read_after_calls(f).items():
            after.setdefault(c, set()).update(regs)
        if n in MOD_TWIN:
            use |= state[MOD_TWIN[n]]
        if use != state[n]:
            state[n] = use
            changed = True
    # (What a replaced function's callers read after it, its mod copy hands back too: the copy runs in its place.)
    for n, copy in MOD_TWIN.items():
        after.setdefault(copy, set()).update(after.get(n, set()))
    handed = {n: r for n in funcs for r in [(after.get(n, set()) & MAY_CHANGE[n]) - RETURN_VALUE - {'sp', 'ra', 'zero'}] if r}
    if handed != HANDED:
        HANDED = handed
        changed = True
# What each function reads itself, not counting what the functions it calls read: where an odd read starts.
own = {n: entry_reads(f, lambda c: set()) for n, f in funcs.items()}

print('rounds', rounds)
clean = 0
odd = collections.Counter()
args = collections.Counter()
oddnames = []
for n, f in funcs.items():
    u = state[n]
    bad = u - ABI_ARGS
    if not bad:
        clean += 1
    else:
        oddnames.append((n, sorted(bad), len(f['ins'])))
        for b in bad:
            odd[b] += 1
    args[len(u & {'a0', 'a1', 'a2', 'a3'})] += 1
tot = sum(len(f['ins']) for f in funcs.values())
good = sum(len(funcs[n]['ins']) for n in funcs if not state[n] - ABI_ARGS)
print('functions', len(funcs), 'instructions', tot)
print('read only their arguments at entry: %d (%.1f%%), %.1f%% of the instructions' % (clean, 100 * clean / len(funcs), 100 * good / tot))
print('reads of other registers at entry:', odd.most_common(14))
print('argument counts:', sorted(args.items()))
print('largest odd ones:', sorted(oddnames, key=lambda x: -x[2])[:14])
print('with an indirect call:', sum(1 for f in funcs.values() if any(m == 'jalr' for m, _ in f['ins'].values())))
print('with a jump table (jr not $ra):', sum(1 for f in funcs.values() if any(m == 'jr' and '$ra' not in o for m, o in f['ins'].values())))
print('with cop0 or cache instructions:', sum(1 for f in funcs.values() if any(m in ('mfc0', 'mtc0', 'cache', 'tlbp', 'eret') for m, _ in f['ins'].values())))
# (Not the mod's other functions, modfn_<address>: read only for what the copies read through them, never converted.)
# ('results': what a function hands back beyond v0-f1, mips.returned_in -- only where there is any, so every other entry is as it was.)
def entry(n):
    e = {'use': sorted(state[n]), 'own': sorted(own[n]), 'n': own_size[n]}
    if n in HANDED:
        e['results'] = sorted(HANDED[n])
    return e
print('functions handing back more than v0-f1:', ', '.join('%s %s' % (n, ','.join(sorted(r))) for n, r in sorted(HANDED.items())) or 'none')
json.dump({n: entry(n) for n in funcs if not modrep.is_mod_own(n)}, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'analysis.json'), 'w'))
# The mod's other functions on their own, in analysis_mod.json (2026-11-12, modcopies): what each reads, for a copy of the mod's
# that calls one (lift.py with_system puts them in beside the game's, for the copies' calls to them and the tester's stand-ins).
# Apart from analysis.json, whose totals are the game's own instructions (lift.py score), and so that nothing that reads it
# changes. Not written without the mod's functions.
mod_own = {n: entry(n) for n in funcs if modrep.is_mod_own(n)}
if mod_own:
    json.dump(mod_own, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'analysis_mod.json'), 'w'))
