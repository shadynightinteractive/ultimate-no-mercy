#!/usr/bin/env python3
"""Step 4: turns the game's functions into C++ with m2c, and writes them into the game's source.

    python tools/lift/lift.py candidates <asm folder> <work folder>    m2c + filters; writes tools/lift/candidates.json
    python tools/lift/lift.py twin                                     writes src/game with every candidate, for the proof build
    python tools/lift/lift.py release <report> [<report> ...]          writes src/game with only the functions the reports prove
    python tools/lift/lift.py score <report> [<report> ...]            prints what the reports prove, without writing anything
    python tools/lift/lift.py fuzz [tests]                             random-input test of every candidate, away from the game
                                                                       (a function unchanged since a run that proved it, or found it
                                                                       different, takes that run's line from fuzz/cache/: run_fuzz.
                                                                       LIFT_NO_CACHE=1 runs every one)

This pass takes only the plain case: functions that call nothing else and use no jump tables. A function that doesn't fit stays
as the translated original, with the reason in candidates.json. Nothing is lost by leaving one out.

A function is PROVED, and goes into the game, only when it shows no difference in real play (src/game/lift_twin.cpp) or on random
inputs (fuzz/fuzz_main.cpp), and every one of its instructions ran during those tests. Static checks here come first, because neither
test can see everything: the original is what really runs, so its callers still get what it leaves behind. The normal build then has to
pass the full test set with the originals gone. See mod/NOTES.md 0.53.0 for the traps these are here for.
"""
import collections, concurrent.futures, glob, hashlib, json, os, re, subprocess, sys, threading, zlib
# The independence check's and the random-input tester's runs at once (LIFT_WORKERS; 3 until 2026-11-06, from a night
# memory ran short -- each tester reserves its 4 GB of game memory but commits only what it touches)
# Priority (2026-11-11). LIFT_PRIORITY=low runs the whole pipeline at below-normal priority, and everything it starts -- testers,
# m2c, the compiler -- inherits that: for when the user is using the PC, so their programs and the game tests go first. By default
# it runs at normal priority: the user, 2026-11-11, with agents working overnight, "Put the run first" (at low priority it got ~3.8
# of the PC's cores). Either way the testers' limits are CPU time (fuzz_main.cpp fuzz_clock_ms), so priority never changes a result.
# (The handle and argument types are given: without them ctypes passed GetCurrentProcess's -1 as a 32-bit int, which on 64-bit
#  Windows isn't the current process, and the call did nothing -- 0.76.0's workers were all Normal.)
if os.name == 'nt' and os.environ.get('LIFT_PRIORITY', 'normal') == 'low':
    import ctypes
    _k32 = ctypes.windll.kernel32
    _k32.GetCurrentProcess.restype = ctypes.c_void_p
    _k32.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
    _k32.SetPriorityClass(_k32.GetCurrentProcess(), 0x4000)   # BELOW_NORMAL_PRIORITY_CLASS
# (And as many workers as the PC has threads, less four, leaving room for the game tests and the desktop; with LIFT_PRIORITY=low
#  the extra workers only use time nothing else wants. Was a fixed 6 workers. LIFT_WORKERS still sets it.)
TEST_WORKERS = max(1, int(os.environ.get('LIFT_WORKERS', str(max(4, (os.cpu_count() or 8) - 4)))))
# LIFT_SUBSET=<a file of function names>: candidates and fuzz for those only, merged into what's there (2026-11-06)
def subset_names():
    path = os.environ.get('LIFT_SUBSET')
    return set(open(path).read().split()) if path else None

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import to_asm
import port_additions
import division_checks
import unreach      # (instructions that can never run, beyond the division checks: approved by the user 2026-11-09, strict mode only)
import abi      # (functions that don't keep to the calling rules, with their extra registers made explicit: abi.py, 2026-11-07)
import modrep   # (the mod's replacement functions, convertible as modrep_<name>: modrep.py, 2026-11-08)
import hooks    # (the game functions the mods only hook, converted with the hook calls: hooks.py, 2026-11-12)
import tidy     # (the readability passes on the finished C++: tidy.py, 2026-11-12)
from mips import uses_defs, cfg, BRANCH, uses_defs_at, uses_defs_in, pops_a_frame_it_never_made, returned_in
import mips

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
GAME = os.path.join(PORT, 'src', 'game')
MOD = os.path.join(ROOT, 'mod')
M2C = os.path.join(HERE, 'third_party', 'm2c')
CANDIDATES = os.path.join(HERE, 'candidates.json')
CLANG = os.path.join(ROOT, 'toolchain', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang++.exe')
MIN_CHECKED = 2
# A function counts as proved only after this many compared tests on random inputs (the standard as stated to the user:
# 200,000). The tester stops a slow function at its time limit (fuzz_main.cpp MOST_MILLISECONDS), and until 2026-11-08 a
# function so stopped could be proved on as few as MIN_CHECKED tests -- 0.65.0 shipped some proved on a few hundred. Those
# now wait as 'too few tests' (fuzz/too_few.txt) until a longer run gives them the full count. (LIFT_FULL_TESTS can lower it
# for a quick look; a shipping run leaves it alone.)
FULL_TESTS = int(os.environ.get('LIFT_FULL_TESTS', '200000'))

# Instructions this pass won't touch: system-level ones, and 64-bit memory the comparison doesn't note.
# (Not the unaligned word instructions, lwl / lwr / swl / swr, since 2026-11-07 (scratch/lift_unal): m2c writes them exactly with
# --exact-unaligned, as calls to UNALIGNED_HELPERS, and the tester notes the originals' unaligned writes (UNALIGNED_ORIGINALS).
# The 64-bit unaligned ones, ldl / ldr / sdl / sdr, are still refused.)
# (Not `break` since 2026-11-07: the compiler's checks after a division -- `bnez $q; break 7` for zero, and the `break 6` for
# the most negative number divided by -1 -- are what m2c's DivPattern / DivuPattern take out, leaving the division, which stops
# the PC on the same inputs: the game ends either way (do_break exits it). Any other break m2c writes as M2C_BREAK, which
# refuse_c refuses as an m2c stand-in. The random-input tester stops on both and compares what happened up to there:
# fuzz_main.cpp, stop_point.)
REFUSED = {'cache', 'mtc0', 'mfc0', 'syscall', 'sync', 'cfc1', 'ctc1', 'tlbp', 'tlbr', 'tlbwi', 'tlbwr', 'eret', 'll', 'sc',
           'ld', 'sd', 'ldl', 'ldr', 'sdl', 'sdr', 'dmult', 'dmultu', 'ddiv', 'ddivu',
           'dsll', 'dsrl', 'dsra', 'dsll32', 'dsrl32', 'dsra32', 'dsllv', 'dsrlv', 'dsrav', 'daddu', 'dsubu', 'dadd', 'dsub',
           'dmfc1', 'dmtc1', 'lld', 'scd'}
TYPES = r'(?:s8|u8|s16|u16|s32|u32|s64|u64|f32|f64|void|M2C_UNK8|M2C_UNK16|M2C_UNK32|M2C_UNK64|M2C_UNK)'
POINTER = re.compile(r'\b(' + TYPES + r')\b((?:\s*\*)+)')
SIGNATURE = re.compile(r'^(.+?)\s*\b(func_[0-9A-F]{8}|\w+)\((.*)\) \{$', re.M)
NUMBER = r'(?:0x[0-9A-Fa-f]+|\d+)\b'
INT_TYPES = {'s8', 'u8', 's16', 'u16', 's32', 'u32', 'M2C_UNK', 'M2C_UNK8', 'M2C_UNK16', 'M2C_UNK32'}

# ---- System functions (2026-11-07, scratch/lift_sys) ----
#
# The game calls libultra through the runtime's own versions, osRecvMesg_recomp(rdram, ctx) and the rest (N64ModernRuntime, C++).
# They aren't in the analysis, which only reads the translated game, so until now a function calling one was refused outright
# ("calls a system function": 149 functions, 27,367 instructions). system_reads.model() now gives, for each one whose source has been
# read by hand, the registers it reads, the stack words it reads, the result registers it writes and the memory it writes through its
# parameters. with_system() puts the registers it reads into the analysis like a game function's (so the checks that use them -- what
# a call must pass, what a later call reads -- treat it as one), and keeps the rest in SYSTEM: the declaration given m2c (prototype),
# the calls turned into lift_call (convert_calls), and the tester's stand-in (stand_in_line). A converted function calls the same
# runtime function the translated original calls, with the same registers set, so in the game it's the same call; the tester proves
# the rest. The ones model() refuses stay "calls a system function".
import system_reads
SYSTEM = {}             # name -> system_reads.model() entry, for the system functions handled
SYSTEM_REFUSED = {}     # name -> why not
MOD_IMPORTS = set()     # the port's exports a C patch's glue may call, read as callees (with_system, 2026-11-12 modglue)


def with_system(analysis, functions):
    """The analysis with the handled system functions added as entries of their own ('use': what they read, 'n': 0 instructions of
    the game's), and SYSTEM / SYSTEM_REFUSED filled. The system functions are the callees the translated game names that aren't
    functions of its own."""
    # (Not the mod's own functions, modfn_<address>, which the mod's copies call -- the edited copies' callees (modcopies) and the
    #  C patches' glue (modglue): they are no system functions. They come in below, from analysis_mod.json, 2026-11-12.)
    names = sorted({c for f in functions.values() for c in f['calls'].values() if c not in functions and c not in analysis
                    and not modrep.is_mod_own(c)})
    handled, refused = system_reads.model(names)
    SYSTEM.clear()
    SYSTEM.update(handled)
    SYSTEM_REFUSED.clear()
    SYSTEM_REFUSED.update(refused)
    # (2026-11-12, scratch/agents/modglue) The port's exports the mod imports (recomp_arcade_*), which only the mod's copies
    #  call: model() never reads them (it looks for a definition under the export's own name), so they stayed "calls a system
    #  function". Those whose registered function's reads system_reads.table() can tell become callees like a game function's:
    #  in the analysis with what they read, and stood in for by the ordinary stand-in (fuzz_stand_in: it notes what they read,
    #  hands back random results and scrambles every scratch register, as a callee that hands ctx on to game code may). Only
    #  a C patch's copy gets as far as asking: every other copy calling one is refused first (modrep.why_not).
    MOD_IMPORTS.clear()
    exported = [n for n in sorted(refused) if n.startswith('recomp_')]
    if exported:
        known = system_reads.table(exported)
        for n in exported:
            if n in known and n in system_reads.exports():
                analysis[n] = {'use': sorted(known[n]), 'own': sorted(known[n]), 'n': 0}
                MOD_IMPORTS.add(n)
                del SYSTEM_REFUSED[n]
    for n, m in handled.items():
        analysis[n] = {'use': m['use'], 'own': m['use'], 'n': 0}
    # The mod's own functions a copy of the mod's calls (2026-11-12, modcopies; "calls between mod functions rewritten to
    # stand-ins in the test build", the user's approval of the same day). What each reads is analyse.py's, worked out from its
    # translation (modrep_modfuncs.c) the way a game function's is, and kept apart in analysis_mod.json. With it here, a copy's
    # call to one is checked as a call to a game function is: it must pass all it reads, and read nothing it can't be given;
    # m2c is told its parameters (prototype), the call becomes lift_call (convert_calls), and the tester stands in for it on
    # both sides (run_fuzz's stand_in) -- the original calls modfn_<address> directly, the converted copy through lift_call,
    # and both reach the same recording stand-in. Their 'n' counts nothing of the game's: no total reads this (score reads
    # analysis.json itself).
    for n, v in mod_own_analysis().items():
        analysis.setdefault(n, v)
    if MOD_OWN_ANALYSIS and not MOD_OWN:
        MOD_OWN.update(mod_own_functions())
    return analysis


MOD_OWN = {}            # modfn_<address> -> its translation as to_asm reads it: for what a copy's call to one needs to know of it
MOD_OWN_ANALYSIS = {}   # modfn_<address> -> analyse.py's entry for it (analysis_mod.json)


def mod_own_analysis():
    """analysis_mod.json (analyse.py): what each of the mod's own functions reads. Empty without it, or with LIFT_MODREP=0 --
    then every copy calling one stays refused, as before (modrep.why_not)."""
    path = os.path.join(HERE, 'analysis_mod.json')
    if not MOD_OWN_ANALYSIS and os.path.exists(path) and modrep.analysis_sources():
        MOD_OWN_ANALYSIS.update(json.load(open(path)))
    return MOD_OWN_ANALYSIS


def mod_own_functions():
    """The mod's own functions, translated (modrep_modfuncs.c), read on their own: kept apart from the game's functions, so that
    nothing worked out over all of those (callers' reliance, the stand-ins' numbers, the totals) changes. Only looked at for a
    copy's callee: its stack parameters, and the result registers it always sets."""
    return to_asm.read_functions(extra=modrep.analysis_sources(), game=False) if modrep.analysis_sources() else {}


def callee_function(name, functions):
    """A callee's translation: a game function's, or one of the mod's own (MOD_OWN), or None (a system function)."""
    return functions.get(name) or MOD_OWN.get(name)


def callee_pattern():
    """A call's name as m2c writes it: a game function, an overlay call (to_asm.py), or one of the system functions handled.
    SYSTEM is filled by with_system at run time, so this is worked out when it's used, not once at import."""
    # (And the mod's own functions, which the mod's copies call: modfn_<address>, 2026-11-12, with_system. And the port's exports
    #  a C patch's glue calls, MOD_IMPORTS: modglue.)
    return r'(?:func_\w+|modfn_[0-9A-F]{8}|lift_overlay_[0-9A-F]{8}|lift_pointer_[a-z][a-z0-9]' + ''.join('|' + re.escape(n) for n in sorted(set(SYSTEM) | MOD_IMPORTS, key=len, reverse=True)) + r')'


def to_game_pointers(text):
    """`s32 *` becomes `P<s32>`, however many stars: game pointers, not PC ones."""
    def one(m):
        out = m.group(1)
        for _ in range(m.group(2).count('*')):
            out = 'P<' + out + '>'
        return out + ' '
    return POINTER.sub(one, text)


WIDER = {'s8': 's32', 's16': 's32', 'M2C_UNK8': 's32', 'M2C_UNK16': 's32', 'u8': 'u32', 'u16': 'u32'}
NARROW = '|'.join(WIDER)


def shift_cuts(f):
    """Where the function cuts a value down with a shift left and back by 16 or 24, `sll $t, $s, 16; sra $d, $t, 16` (or srl):
    (address of the sll, s, t, d, the shift back's mnemonic, the count)."""
    addrs = sorted(f['ins'])
    found = []
    for i, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        m = re.match(r'\$(\w+),\s*\$(\w+),\s*(\d+)$', ops)
        if mn != 'sll' or not m or int(m.group(3)) not in (16, 24):
            continue
        for b in addrs[i + 1:i + 5]:
            back = f['ins'][b]
            n = re.match(r'\$(\w+),\s*\$%s,\s*%s$' % (m.group(1), m.group(3)), back[1])
            if back[0] in ('sra', 'srl') and n:
                found.append((a, m.group(2), m.group(1), n.group(1), back[0], int(m.group(3))))
                break
    return found


# What a shift left and back by 16 or 24 leaves: the narrow types m2c shows that cut with (signed for sra, unsigned for srl).
CUT_TYPES = {('sra', 16): {'s16', 'M2C_UNK16'}, ('srl', 16): {'u16', 'M2C_UNK16'},
             ('sra', 24): {'s8', 'M2C_UNK8'}, ('srl', 24): {'u8', 'M2C_UNK8'}}
TEMP_REGISTER = re.compile(r'temp_([a-z]+[0-9]*?)(?:_\d+)?$')


def locals_narrowed(f):
    """The registers the function cuts down itself (shift_cuts), and the narrow types that cut can show as: {register: {types}}.
    The registers are the value cut, the shifted one and the result.

    m2c can leave such a cut out of the C and let a local's narrow type do it: func_801272A4 builds a 16-bit value, stores it with
    sh, then compares its sign-extended form with -1. m2c declared the value s16 and compared it, so widening that local to s32
    lost the cut and the compare (2026-11-07). Only m2c's temp_ locals use this (one value each): a var_ local is several values
    joined, and keeping one narrow because its register is cut somewhere else broke func_8012E784, whose var_v0 holds arg1 + 3."""
    found = {}
    for a, s, t, d, back, count in shift_cuts(f):
        for r in (s, t, d):
            found.setdefault(r, set()).update(CUT_TYPES[(back, count)])
    return found


def only_loaded_as(text, name, kind):
    """Whether the C sets this local in one place only, to a load of that unsigned type: `temp_v1 = M2C_FIELD(x, u16 *, 2);`
    or `temp_v1 = *(u16 *)0x80012345;` (2026-11-07).

    Such a value is the zero-extended halfword or byte (lhu, lbu), 0 to 0xFFFF, so s32 holds it exactly -- and s32 is how C uses
    a u16 or u8 in an expression (promoted to int). u32 isn't: `temp_v0 >= (temp_v1 & 0xFFF)` with temp_v0 an s32 becomes an
    unsigned compare, where the original's slt is signed. m2c keeps such a load as part of the expression it's used in, unless
    it has to give it a temp (to keep it before a store: pin_pending_reads in its translate.py), and func_8010B6F0 went from
    proved to different that way."""
    sets = re.findall(r'^\s*' + re.escape(name) + r' = (.*);$', text, re.M)
    # (Set once: one plain assignment, and no other assignment to it of any kind.)
    writes = re.findall(r'\b' + re.escape(name) + r'\s*(?:[-+*/%&|^]|<<|>>)?=(?!=)', text)
    if len(sets) != 1 or len(writes) != 1:
        return False
    rhs = sets[0].strip()
    if re.fullmatch(r'\*\(' + kind + r' \*\)0x[0-9A-Fa-f]+', rhs):
        return True
    if rhs.startswith('M2C_FIELD('):
        _, close = split_arguments(rhs, len('M2C_FIELD('))
        return close == len(rhs) - 1 and re.search(r',\s*' + kind + r' \*,\s*-?(?:0x[0-9A-Fa-f]+|\d+)\)$', rhs) is not None
    return False


def to_register_width(text, f):
    """Local variables, parameters and results become as wide as the registers they were.

    m2c types a variable by how it's used, so one later stored as a byte gets declared s8. But on the N64 it was a 32-bit register
    the whole time: a sum could pass 127 and still compare as a large number. Declared s8, it would wrap and compare as a negative
    one (func_80111EF4 did exactly this). Real narrowing is still there, as the explicit masks and casts and in the stores to memory.
    Stack locals (spXX) keep their size: those were memory on the N64 too. So do parameters on the stack (arg4 and up): the N64 reads
    just their size from the slot (func_8011BD90 reads one byte), so the rest of the slot isn't part of the value. And so do
    temp_ locals named for a register the function cuts down the same way, when the type is the one that cut gives (locals_narrowed).

    Register parameters (arg0-arg3) and the result are always widened, since m2c runs with --register-cuts (2026-11-07): every cut
    the instructions make is a cast in the expression, `(s32) (s16) x`, so no type has to stand for one any more. Until then a
    parameter the function cut itself kept its narrow type (params_narrowed, func_80005D34), and so did a narrow result, because m2c
    had folded the cut into the type. But a narrow type cuts every use: func_8012FABC loads s0 with lh, later puts a whole word in
    it and returns it, and its s16 result cut 0x801005B0 to 0x5B0."""
    cut = locals_narrowed(f)
    # (2026-11-10) Not any more: since --register-cuts (2026-11-07, run_m2c) m2c writes every such cut itself, `(s32) (s16)
    # temp_v0 == -1` for locals_narrowed's own func_801272A4 shape (tried on the folder's m2c: sh, then sll/sra 16, then the
    # compare), so no narrow type has to stand for one. And the exception went by the register's NAME: one cut of v1 anywhere
    # in the function kept every s16 temp_v1_N narrow. func_801099A4 cuts v1 once near its end; its temp_v1_3 and temp_v1_4,
    # `(count - 8) * 14` -- typed s16 by m2c only for the sh they are stored with afterwards -- stayed s16, so the compare
    # before the store (`slt $v0, $v1, $a0`, all 32 bits) saw 458,626 as -126 and took the other branch (memory-count, 4,308
    # of 59,740 tests). With the exception gone they are s32, and the store still cuts (its place is s16).
    # UNSURE (see NOTES.md): proved on that function by hand; the whole run has to show nothing else relied on it.
    cut = {}
    def local(m):
        r = TEMP_REGISTER.match(m.group(4)[:-1])
        if r and m.group(2) in cut.get(r.group(1), ()):
            return m.group(0)
        if m.group(2) in ('u8', 'u16') and only_loaded_as(text, m.group(4)[:-1], m.group(2)):
            return m.group(1) + 's32' + m.group(3) + m.group(4)
        return m.group(1) + WIDER[m.group(2)] + m.group(3) + m.group(4)
    text = re.sub(r'^(\s+)(' + NARROW + r')(\s+)(?!sp[0-9A-Fa-f]+\b)(\w+;)', local, text, flags=re.M)
    def signature(m):
        params = re.sub(r'\b(' + NARROW + r') (arg[0-3])\b', lambda p: WIDER[p.group(1)] + ' ' + p.group(2), m.group(3))
        pointer = m.group(2).lstrip().startswith('*')      # (a pointer's target type stays: `u8 *` is no register)
        return (m.group(1) if pointer else WIDER.get(m.group(1), m.group(1))) + m.group(2) + params + ') {'
    # (The name may follow a pointer's star, `s32 *func_80009E88(s32 arg0, s16 arg1) {`: until 2026-11-08 the pattern wanted a
    #  space and the name, so a function returning a pointer kept its narrow parameters. func_80009E88's s16 arg1 cut the a1 it
    #  compares whole with a halfword it loads, `bne $v0, $s1`, so a1 = 0x40000000 matched a halfword 0.)
    return re.sub(r'^(\w+)(\s*\**\s*\w+\()(.*)\) \{$', signature, text, count=1, flags=re.M)


def float_at_returns(f):
    """Every return of f has f0 written in the few instructions before it (its delay slot included) and v0 not: a float
    result, even where v0 is used for something else inside (the sine and cosine func_80036460 / func_80034EF0, 2026-11-06)."""
    if not f:
        return False
    addrs = sorted(f['ins'])
    returns = [i for i, a in enumerate(addrs) if f['ins'][a][0] == 'jr' and f['ins'][a][1].strip() == '$ra']
    if not returns:
        return False
    for i in returns:
        last = set()
        for a in addrs[max(0, i - 4):i + 2]:   # (four before the jr, the jr, its delay slot)
            last |= uses_defs(*f['ins'][a])[1]
        if 'f0' not in last or 'v0' in last:
            return False
    return True


# Double parameters and results (2026-11-12, agent "refused2"; the user approved them for 0.81 at 16:3x). By the N64's calling
# rules a double parameter comes in a pair of float registers, f12:f13 for the first and f14:f15 for the second (when only floats
# come before it), and a double result goes back in f0:f1: libm's frexp and ldexp (func_800325E8, func_800326C8) and the
# functions around them (func_800326EC, func_8002C7AC). Until now a function reading f13 or f15 was refused before m2c ("reads a
# register other than its parameters"), and a call to one too (convert_calls). m2c types such a parameter f64 itself, from the
# double instructions that read it; the converted function then takes it from the context's pair (arg_sources: ctx->f12.d, the
# even register's 64 bits, its high word the odd register, as the translated code keeps them), lift_call.h puts one there, and
# the tester draws doubles for it and compares a double result's whole 64 bits (fuzz_main.cpp, result 3).
def reads_as_double(mn):
    """Whether an instruction reads its float operands as doubles, each a pair of registers (abi.py plan's reads_double)."""
    return bool(mn.endswith('.d') or mn in ('ldc1', 'sdc1') or re.match(r'(cvt|trunc|round|floor|ceil)\.\w\.[dl]$', mn))


def double_parameter_halves(f):
    """The odd registers, of f13 and f15, that are the second halves of double parameters: the function reads the pair, and no
    instruction anywhere in it reads either register of the pair as a single float. Any single read of either -- a float
    parameter in f12 that is used as a float, or half of the pair moved on its own -- and the pair is not taken as a double:
    the function is refused as before. (Not for a function with a call: what a callee leaves in f12..f15 is no parameter.)"""
    if not f or f.get('calls'):
        return set()
    single, double = set(), set()
    for mn, ops in f['ins'].values():
        regs = {r for r in uses_defs(mn, ops)[0] if re.fullmatch(r'f1[2-5]', r)}
        (double if reads_as_double(mn) else single).update(regs)
    return {odd for even, odd in (('f12', 'f13'), ('f14', 'f15')) if {even, odd} <= double and not {even, odd} & single}


def double_at_returns(f):
    """Every return of f has f0 written last, in the few instructions before it (its delay slot runs last), by a double
    instruction that writes the pair f0:f1, and v0 not: a double result (float_at_returns, for both halves). A last write of
    f0 alone -- `cvt.s.d $f0, $f2`, a float made from a double -- is a float result, as before."""
    if not float_at_returns(f):
        return False
    addrs = sorted(f['ins'])
    for i, a in enumerate(addrs):
        if f['ins'][a][0] == 'jr' and f['ins'][a][1].strip() == '$ra':
            window = addrs[max(0, i - 4):i] + addrs[i + 1:i + 2]       # (in the order they run: the delay slot last)
            last = [b for b in window if 'f0' in uses_defs(*f['ins'][b])[1]]
            if not last or 'f1' not in uses_defs(*f['ins'][last[-1]])[1]:
                return False
    return True


def double_result(name, functions):
    """Whether a callee hands back a double its callers can take as its result: on every way through it to each return, the
    last instruction to write f0 is a double one that writes f1 with it -- so the pair it leaves is all its own -- and v0 is
    not its result (float_at_returns). rw_contact_cap, which writes f0:f1 only when s4 >= 10 (`mov.d $f0, $f2`) and otherwise
    hands back its caller's, is not: func_800EBD2C converted with it as a double result read the context's stale pair where
    the original reads its own (calls@19, 11 of 249,996 tests, 2026-11-12) -- abi.py's call_store_sites keeps a caller's v0
    or f0 in step before such a call, but not a pair. Nor is a function with a call: what a callee leaves isn't followed here.
    (2026-11-12, agent "refused2")"""
    f = functions.get(name)
    if not f or not f['ins'] or f.get('calls') or not float_at_returns(f):
        return False
    addrs, succ = cfg(f)
    # (state: on every way here, f0 was last written together with f1. Not yet at the start.)
    state, work = {addrs[0]: False}, [addrs[0]]
    while work:
        pc = work.pop()
        d = uses_defs(*f['ins'][pc])[1]
        now = ('f1' in d) if 'f0' in d else state[pc]
        if f['ins'][pc][0] == 'jr' and f['ins'][pc][1].strip() == '$ra':
            slot = pc + 4
            if slot not in f['ins']:
                return False
            ds = uses_defs(*f['ins'][slot])[1]
            if not (('f1' in ds) if 'f0' in ds else now):
                return False
            continue
        for n in succ.get(pc, []):
            new = now if n not in state else (state[n] and now)
            if n not in state or new != state[n]:
                state[n] = new
                work.append(n)
    return True


def double_kinds(use, halves):
    """A function's parameter kinds, by the N64's rules, when it has double parameters (double_parameter_halves): the floats
    first -- f64 for a pair, f32 for f12 or f14 alone -- then an s32 for each integer register from the first one the floats
    leave, up to the last one it reads. None if it reads an integer register a double's place covers (no such declaration)."""
    kinds, slot = [], 0
    if 'f13' in halves:
        kinds, slot = ['f64'], 2
    elif 'f12' in use or 'f14' in use or 'f15' in halves:
        kinds, slot = ['f32'], 1          # (a second float needs a first one in f12, read or not)
    if 'f15' in halves:
        kinds, slot = kinds + ['f64'], 4
    elif 'f14' in use:
        kinds, slot = kinds + ['f32'], slot + 1
    ints = [i for i, r in enumerate(('a0', 'a1', 'a2', 'a3')) if r in use]
    if [i for i in ints if i < slot]:
        return None
    return kinds + ['s32'] * (max(ints, default=slot - 1) + 1 - slot)


def parameter_slots_of(kinds):
    """How many of the four-byte argument slots these parameter kinds fill: a double two, at an even slot."""
    slot = 0
    for k in kinds:
        slot = (slot + 1) // 2 * 2 + 2 if k in ('f64', 's64', 'u64') else slot + 1
    return slot


RESULT_REGISTERS = {}       # (function name -> the result registers it always sets; see result_registers)


def result_registers(name, functions):
    """Which of v0 and f0 the function always sets itself before it returns: on every way through it, an instruction of its own
    writes the register, or a function it calls that always sets it does (2026-11-07). Once set, a later call doesn't undo
    that: what's there afterwards was still worked out inside the function, from what it was given.

    That is what makes a result exact in the C++. A converted caller calls the translated function on the N64's registers and
    reads its result from one of them (lift_call: f0 for an f32, v0 otherwise). A register the function always sets holds
    the same there as in the original. One it may leave alone holds what was there before the call -- which, if the caller's
    own instructions put it there, the C++ never wrote (they're C variables). So a call's result is only taken from a register
    in this set (prototype). Not known (a system function, a call through a pointer, a way out that isn't a return, a
    function calling itself round again): left out, which only means not relied on."""
    if name in SYSTEM:
        # (A system function the tester stands in for, 2026-11-07: the result registers its runtime source always writes, read
        #  by hand -- system_reads.MEMORY. Its stand-in writes those, and leaves every other register as it was, as the source does.)
        return set(SYSTEM[name]['results']) & {'v0', 'f0'}
    if name in RESULT_REGISTERS:
        return RESULT_REGISTERS[name]
    RESULT_REGISTERS[name] = set()      # (while it's worked out: a call back into it counts as setting nothing)
    f = callee_function(name, functions)     # (one of the mod's own too, for a copy calling it: with_system, 2026-11-12)
    if not f or not f['ins']:
        return set()
    addrs, succ = cfg(f)
    WANTED = {'v0', 'f0'}
    state = {addrs[0]: frozenset()}     # (what's surely set on the way into each instruction; missing: not reached yet)
    work = [addrs[0]]
    exits = []
    while work:
        pc = work.pop()
        mn, ops = f['ins'][pc]
        now = set(state[pc]) | (uses_defs(mn, ops)[1] & WANTED)
        if mn == 'jal':
            # (The callee runs after the delay slot, which only adds to what's set: both go in here, so a branch straight
            #  into the delay slot, which skips the call, doesn't count it.)
            callee = f['calls'].get(pc + 4) or f['calls'].get(pc)
            now |= result_registers(callee, functions) if callee else set()
        # (A call through a pointer, jalr, sets nothing known.)
        following = succ.get(pc, [])
        if not following:
            before = f['ins'].get(pc - 4, ('', ''))
            if before[0] == 'jr' and '$ra' in before[1]:
                exits.append(now)
            else:
                exits.append(set())     # (a jump out of the function, or off its end: not known)
        for n in following:
            new = frozenset(now) if n not in state else state[n] & frozenset(now)
            if n not in state or new != state[n]:
                state[n] = new
                work.append(n)
    found = set.intersection(*[set(e) for e in exits]) if exits else set()
    # (A function the mod replaces: what runs is the mod's copy, or the game's own without the mod -- a result register only
    #  when both always set it. modrep.py; the same reasoning as analyse.py's union of what the two read.)
    twin = modrep.PREFIX + name
    if twin in functions and not modrep.is_modrep(name):
        RESULT_REGISTERS[name] = found
        found = found & result_registers(twin, functions)
    RESULT_REGISTERS[name] = found
    return found


def prototype(callee, functions, analysis, reads_after, caller_reads=None):
    """A function's parameters and result, from the register analysis, as a C declaration for m2c's context.

    Without one, m2c works out a call's parameters from the registers set just before it, and drops one that's passed through
    unchanged (func_80141E64 calls itself with its own a1 still in a1). A parameter it can't find set is then a warning, which is
    refused, not a guess.

    caller_reads: what the caller this declaration is for reads after its calls to the function (callers_rely_on's by_caller).
    When that is one of v0 and f0 only, and the function always sets that one (result_registers), the result is declared in it,
    for this caller: the converted call then reads the same register the original does, holding the function's own value
    (2026-11-07). When the caller reads neither itself and the function always sets only one, that one. Otherwise the result is chosen as before, from all callers' reads and the function's own code. That choice is
    one result for every caller, and it went wrong two ways, each refused as a bit-cast of a call's result (an m2c stand-in,
    47 functions, 9,040 instructions):
      - func_8012EC5C (`Vec3f *f(s16 *in, Vec3f *out)`, 10 callers) returns `out` in v0, which it sets first, and its last
        instruction before the return writes f0 (a float it converts). float_at_returns took that for a float result, and its
        callers, which read v0, got M2C_BITWISE(s32, ...) -- the f0 bits, not the pointer;
      - func_8012EA74 and func_8012ED0C (42 callers) end by returning what the square root func_80034B40 leaves in f0, and set
        neither register themselves; being called through a pointer as well (after_indirect, which has v0 and f0), and passing
        results on, gave them both in reads_after, so s32 -- and the callers that read f0 got M2C_BITWISE(f32, ...).
    Either way the converted call read the other register. A caller reading both v0 and f0 after it still gets the old choice,
    and the bit-cast m2c writes for one of them is still refused (bitwise_casts)."""
    use = set(analysis.get(callee, {}).get('use', []))
    kinds = []
    for i, (whole, single) in enumerate((('a0', 'f12'), ('a1', 'f14'), ('a2', ''), ('a3', ''))):
        later = [r for r in ('a0', 'a1', 'a2', 'a3')[i + 1:] if r in use] + ([r for r in ('f14',) if r in use] if i == 0 else [])
        if whole in use:
            kinds.append('s32')
        elif single and single in use and all(k == 'f32' for k in kinds):
            kinds.append('f32')
        elif later:
            kinds.append('s32')
    # (2026-11-12, agent "refused2": a function with double parameters is declared with them, f64, by the N64's rules -- see
    #  double_parameter_halves. Every other function's declaration is as it was.)
    halves = double_parameter_halves(functions.get(callee)) & use
    if halves and double_kinds(use, halves) is not None:
        kinds = double_kinds(use, halves)
    # (callee_function: one of the mod's own functions too, for a copy calling it, 2026-11-12)
    stack = stack_parameters(callee_function(callee, functions)) if callee_function(callee, functions) else SYSTEM.get(callee, {}).get('stack', 0)
    if stack:
        kinds += ['s32'] * (4 - parameter_slots_of(kinds)) + ['s32'] * stack
    if callee in SYSTEM:
        # A system function (2026-11-07): its result is what its runtime source always writes, f0 or v0 (result_registers). One
        # that writes neither is declared void: __osDisableInt's N64 version returned the old interrupt mask in v0, the runtime's
        # sets nothing, so what an original reads in v0 after it is what was there before -- which, if the original's own
        # instructions put it there, the C++ never wrote. m2c then has no value for such a read, and a function that uses one is
        # refused for its warning; one that only hands it on to something that reads nothing (__osRestoreInt) is fine. Its
        # parameters are what its source reads (with_system).
        always = result_registers(callee, functions)
        result = 'f32' if always == {'f0'} else 's32' if 'v0' in always else 'void'
        return '%s %s(%s);' % (result, callee, ', '.join(kinds) or 'void')
    after = reads_after.get(callee, set())
    # (a float result: its callers read only f0 after it -- or it writes f0 itself and never v0, as func_80034B40,
    #  `jr ra; sqrt.s f0, f12`, does: some caller read v0 after it for something of its own, and the declaration said
    #  s32, so m2c wrapped the call in an M2C_BITWISE stand-in -- func_800E28C0 refused, 2026-11-06)
    own_defs = set()
    for mn, ops in (callee_function(callee, functions)['ins'].values() if callee_function(callee, functions) else ()):
        own_defs |= uses_defs(mn, ops)[1]
    result = 'f32' if ('f0' in after and 'v0' not in after) or ('f0' in own_defs and 'v0' not in own_defs) or float_at_returns(callee_function(callee, functions)) else 's32'
    # (This caller's own reads, where they settle it: see above. And where it reads neither itself -- it ignores the result, or
    #  returns it as its own -- and the function always sets just one of them, that one: func_80138FBC ends by returning what
    #  func_8010791C leaves in v0, and was given it as an f32, from what other callers of both read.)
    read = (caller_reads or set()) & {'v0', 'f0'}
    always = result_registers(callee, functions)
    if len(read) == 1 and read <= always:
        result = 'f32' if read == {'f0'} else 's32'
    elif not read and len(always) == 1:
        result = 'f32' if always == {'f0'} else 's32'
    # (2026-11-12, agent "refused2") A float result that is the pair f0:f1, written by a double instruction before every return
    # (double_at_returns), for a caller that reads f1 after it: a double. Declared f32, m2c's caller took a float made from f0
    # alone, where the original reads both (func_800EBD2C's `c.lt.d $f0, $f2` after rw_contact_cap). A caller that doesn't
    # read f1 keeps the declaration it had, and so its C++.
    if result == 'f32' and 'f1' in (caller_reads or set()) and double_result(callee, functions):
        result = 'f64'
    return '%s %s(%s);' % (result, callee, ', '.join(kinds) or 'void')


def callee_context(name, functions, analysis, reads_after, work, by_caller=None, extra=()):
    """m2c's context for a function: a declaration for each function it calls, then `extra`, its overlay calls' (overlay_prototype)."""
    callees = sorted(set(functions[name]['calls'].values()))
    if not callees and not extra:
        return None
    path = os.path.abspath(os.path.join(work, name + '.h'))
    # (A tail call's stub, <target>__tail (to_asm.py), is declared as its target is: the same parameters and result.)
    tails = [prototype(c, functions, analysis, reads_after, (by_caller or {}).get((name, c))).replace(' %s(' % c, ' %s__tail(' % c, 1)
             for c in sorted(set(functions[name].get('tail', {}).values()))]
    open(path, 'w').write('\n'.join([prototype(c, functions, analysis, reads_after, (by_caller or {}).get((name, c)))
                                     for c in callees] + tails + list(extra)) + '\n')
    return path


def computed_frame_reads(text):
    """(C2, 2026-11-11, scratch/different_overnight) Whether a function's assembly both works out an address in its own frame from a
    register -- `addu $x, $sp, $y` -- and stores a word in the outgoing stack-argument area, 0x10..0x3C($sp), a call's fifth or
    later argument. Such an address can land on an argument's slot between its store and the call: func_800F00C0's `sll $v0, $s4,
    2; addu $v0, $sp, $v0; lw $a1, 0x20($v0)` with s4 = -3 reads the 0x14($sp) it stored just before (calls@31: 0 for 1, 2,967 of
    250,000 tests in 0.75.0). By the instructions' text only, not their flow: more functions, never fewer. (Also counting an address
    from a local's address and a register took in 136 functions, 134 with no difference; this takes in 16.)"""
    computed = stack_arg_store = False
    for line in text.split('\n'):
        m = re.match(r'^\s+(\w+(?:\.\w+)*)\s+(.*?)\s*$', line)
        if not m:
            continue
        mn, ops = m.group(1), m.group(2)
        regs = re.findall(r'\$(\w+)', ops)
        if mn in ('addu', 'subu') and len(regs) == 3 and regs[0] != 'sp' and 'sp' in regs[1:] and \
                [r for r in regs[1:] if r not in ('sp', 'zero')]:
            computed = True
        s = re.match(r'\$\w+,\s*(0x[0-9A-Fa-f]+|\d+)\(\$sp\)$', ops)
        if mn in ('sw', 'swc1') and s and 0x10 <= int(s.group(1), 0) <= 0x3C:
            stack_arg_store = True
    return computed and stack_arg_store


# m2c's results, kept (2026-11-12, the speed agent): the candidates stage ran m2c afresh on every function every night, though
# most are given exactly the same assembly and context as the night before. m2c is a program of the input alone -- the same
# assembly, context and options give the same C (checked on 0.79.0's inputs: candidates.json came out the same with the cache
# as without it and as 0.79.0's own, every function's C++ included) -- so its output is kept in m2c_cache/, one file per key: a
# hash of m2c's own sources (every .py under third_party/m2c), the Python running it, the code here that runs it (its options and
# retries), the files' paths, and the assembly's and the context's text. A time-out isn't kept. (The only other differences were
# a refused function's compile error, which names the work folder, and one modrep reason that changes from run to run anyway:
# modrep.why_not takes a set's first callee.) LIFT_NO_CACHE=1: run all (still written). Delete the folder to start again.
M2C_CACHE = os.path.join(HERE, 'm2c_cache')
_m2c_source_hash = None


def m2c_source_hash():
    global _m2c_source_hash
    if _m2c_source_hash is None:
        h = hashlib.sha256(('m2c-cache-1\0%s\0' % sys.version).encode())
        for root, dirs, files in os.walk(M2C):
            dirs[:] = sorted(d for d in dirs if d not in ('__pycache__', '.git'))
            for f in sorted(files):
                if f.endswith('.py'):
                    path = os.path.join(root, f)
                    h.update(b'\0file\0' + os.path.relpath(path, M2C).replace('\\', '/').encode() + b'\0' + open(path, 'rb').read())
        # (and how lift.py runs it: the text of run_m2c_fresh, which has the options and the retries, and of
        #  computed_frame_reads, which picks one of them)
        import inspect
        h.update(('\0run\0' + inspect.getsource(run_m2c_fresh) + '\0' + inspect.getsource(computed_frame_reads)).encode())
        _m2c_source_hash = h.hexdigest()
    return _m2c_source_hash


def unchecked_divisions(text):
    """(2026-11-12, agent "refused") Whether a function's assembly has fewer of the compiler's `break` than divisions: code with
    divisions no check guards (hand-written: rw_80118A20, rw_800EC484), as refuse_c counts them. m2c then writes each div and
    divu with --n64-division, as LIFT_DIV(a, b) and the like (lift_mem.h), which give the N64's results by zero and for the
    most negative number by -1, where C++'s / would stop the PC. A function whose divisions all have the checks keeps C++'s /:
    it stops where the original stops, at the break (fuzz_main.cpp stop_on_division_trap)."""
    kinds = re.findall(r'^\s+(div|divu|break)(?=\s)', text, re.M)     # (not div.s, div.d: float divisions)
    divisions = sum(1 for k in kinds if k != 'break')
    return divisions > kinds.count('break')


def run_m2c(job):
    name, asm, context = job if len(job) == 3 else (job[0], job[1], None)
    # (The key: m2c's sources and how it is run (m2c_source_hash), the files' paths, and the assembly's and the context's text.)
    key = hashlib.sha256('\0'.join([m2c_source_hash(), asm, context or '', open(asm).read(),
                                    open(context).read() if context else '']).encode()).hexdigest()
    cached = os.path.join(M2C_CACHE, key[:2], key + '.json')
    if os.environ.get('LIFT_NO_CACHE') != '1' and os.path.exists(cached):
        try:
            return name, json.load(open(cached))['c']
        except (ValueError, KeyError, OSError):
            pass
    name, c, timed_out = run_m2c_fresh(name, asm, context)
    if not timed_out:
        os.makedirs(os.path.dirname(cached), exist_ok=True)
        with open(cached + '.%d.tmp' % os.getpid(), 'w') as f:
            json.dump({'name': name, 'c': c}, f)
        os.replace(cached + '.%d.tmp' % os.getpid(), cached)
    return name, c


def run_m2c_fresh(name, asm, context):
    """m2c on one function, as run_m2c always ran it: (name, its C or None, whether it timed out)."""
    try:
        # (Not --reg-vars, though it keeps reads in the N64's order: it gets branch delay slots wrong. func_80005D34 came out as
        # `var_v0 = 1; if (var_v0 == NULL)`. m2c's normal mode can move a read past a write that changes it, as in func_80142BE8, and
        # the random-input tester is what catches that.)
        # The context gives the functions it calls their real parameters (see callee_context), so a call passes all of them.
        # --no-switches: m2c's "irregular" switches, rebuilt from trees of compares, treat signed and unsigned compares as one number
        # line, so a tree mixing them can give a negative value the wrong case. Jump-table switches are unaffected.
        # --register-cuts (our m2c option, 2026-11-07): a cut by shifts, `sll $v0, $s2, 16; sra $s1, $v0, 16`, comes out as
        # `(s32) (s16) x`, two casts that change no type. Without it m2c made x itself s16 where it could -- a parameter, or a
        # variable -- and dropped the cast, so a value the function also uses whole was cut there too, or the cut was lost when
        # to_register_width widened the type back: func_800E6274 passed func_80014E44 the whole of a0 where the original passed it
        # cut (0xF19E880E for 0xFFFF880E), func_800F25D4 cut every later value of its s0 as well as the first (0x80000000 to 0). The
        # tester's commonest DIFFERENT on 2026-11-07. See third_party/m2c/m2c/evaluate.py handle_shift_right.
        # --access-types (also ours, 2026-11-07): each memory access is written with its own instruction's size and sign where
        # m2c's one type for that place differs. m2c types everything read through one pointer or field alike, so func_8003D780's
        # lb read a byte as u8 because another instruction read it with lbu, and func_800F25D4 read a whole word (lw 0x80) as one
        # byte because a lbu elsewhere read that field's first byte. See third_party/m2c/m2c/translate.py StructAccess.access_type.
        # --left-registers (ours, 2026-11-07): a parameter register a callee reads but the caller doesn't set -- sprintf's a2 and a3,
        # stored for its list whether the format uses them or not -- is M2C_LEFT(a3) where m2c can show that the C++ already holds
        # there what the original passes (left by the calls before, or as the function came in), instead of the error "Read from
        # unset register", the commonest warning refused (55 functions). See translate.py LeftInRegister, and pass_left_registers.
        # --exact-unaligned (ours, 2026-11-07, scratch/lift_unal): an lwl at an address and the lwr at the address + 3 (swl and
        # swr to write) that are one unaligned word come out as lift_unaligned_load32(base, offset) (lift_unaligned_store32), and
        # one of those instructions alone as lift_lwl / lift_lwr / lift_swl / lift_swr: UNALIGNED_HELPERS, which do exactly what
        # the instructions do. m2c's own M2C_UNALIGNED32 meant the address in one place and the value in another, and it left
        # out an swr, guessing it paired with an swl. It changes nothing in a function without those instructions.
        # --compare-types (ours, 2026-11-07): each compare is written in its own instruction's signedness -- slt's signed,
        # sltu's unsigned -- with a cast on a side C++ could compare the other way. m2c chose no cast by its own types, which
        # aren't the C++'s: to_register_width widens a u8 or u16 local to u32, so func_8000CEEC's slt came out as
        # `temp_a0_2 < (var_s2 & 0xFF)` with var_s2 u32, an unsigned compare where a negative temp_a0_2 counted as large.
        # See third_party/m2c/m2c/translate.py format_compare. (The way --access-types makes each load follow its instruction.)
        # --early-stack-args (ours, 2026-11-11, scratch/different_overnight C2): only for a function that also works out an address
        # in its frame from a register (computed_frame_reads), each stack argument's store is written to its slot where the
        # original makes it, and the call reads the slot. Elsewhere m2c folds the store into the call, and the converted function
        # writes the slot only at the call -- the same unless something reads the slot in between, which only such an address can.
        command = ([sys.executable, 'm2c.py', '--target', 'mips-gcc-c', '--valid-syntax', '--no-switches', '--register-cuts',
                    '--access-types', '--left-registers', '--exact-unaligned', '--compare-types'] +
                   (['--early-stack-args'] if computed_frame_reads(open(asm).read()) else []) +
                   # (2026-11-13, the ARM64 proof: both for every function. A division the compiler's check guards was left as
                   #  C++'s / or %, which is undefined for the very operands the check is there for -- on x86 it traps, on
                   #  ARM64 it gives 0 and goes on, and a compiler may take the check after it away. LIFT_DIV and the rest
                   #  have a result in every case, and LIFT_DIV_CHECK stays where the game's break was. And every float-to-word
                   #  conversion through LIFT_TRUNC_W_S / _D: see m2c's N64_CONVERSION.)
                   ['--n64-division', '--n64-conversion'] +
                   (['--context', context] if context else []) + [asm])
        p = subprocess.run(command, cwd=M2C, capture_output=True, text=True, timeout=120)
        # --split-delay-slots (ours, 2026-11-07), only where m2c stopped on a branch whose delay slot updates what it compares
        # (`bne $s0, $v0, .Lloop; addiu $s0, $s0, 1`, a loop's count, 7 functions): the delay slot is copied into both ways
        # out of the branch, so the compare reads the value from before it. Not for every function -- it can change how m2c
        # lays out one it already translated (see m2c's flow_graph.py build_blocks).
        # m2c stops so more often since its pin_pending_control also counts a variable the delay slot assigns again (it used
        # to print func_8014434C's `bne $s0, $v0; addiu $s0, $s0, -1` as `var_s0 -= 1; if (var_s0 == 4)`, the new value). One
        # that still stops after this has a delay slot that sets the compared variable afresh (`bne $v0, $zero, ...; addiu
        # $v0, $zero, 1`), so it's tried last with --split-all-delay-slots, which splits those too; a function the first
        # split translated keeps that translation.
        if p.returncode == 0 and 'A branch condition is changed by its delay slot' in p.stdout:
            p = subprocess.run(command[:-1] + ['--split-delay-slots', asm], cwd=M2C, capture_output=True, text=True, timeout=120)
        if p.returncode == 0 and 'A branch condition is changed by its delay slot' in p.stdout:
            p = subprocess.run(command[:-1] + ['--split-all-delay-slots', asm], cwd=M2C, capture_output=True, text=True, timeout=120)
        # (2026-11-12, agent "refused") m2c holds a stack argument as an expression until its call; where what it is made from
        # changes before the call and can't be pinned where it was stored (a variable assigned again: func_800EFFD4), it writes
        # the error "Stack arg changed before the call". --early-stack-args writes every stack argument to its slot where the
        # original stores it, and the call reads the slot -- what the original does -- so that can't arise. Only for such a
        # function, as the retries above: elsewhere it lays the C out differently.
        if p.returncode == 0 and 'Stack arg changed before the call' in p.stdout and '--early-stack-args' not in command:
            command = command[:-1] + ['--early-stack-args', asm]
            p = subprocess.run(command, cwd=M2C, capture_output=True, text=True, timeout=120)
            if p.returncode == 0 and 'A branch condition is changed by its delay slot' in p.stdout:
                p = subprocess.run(command[:-1] + ['--split-delay-slots', asm], cwd=M2C, capture_output=True, text=True, timeout=120)
            if p.returncode == 0 and 'A branch condition is changed by its delay slot' in p.stdout:
                p = subprocess.run(command[:-1] + ['--split-all-delay-slots', asm], cwd=M2C, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return name, None, True
    return name, p.stdout if p.returncode == 0 else None, False


# Comments m2c writes that aren't warnings: notes on how it laid the C out. (The irreducible one, 2026-11-07: a flow graph with a
# loop entered in two places can't be written as if/while, so m2c writes every block as it is, with gotos between them --
# func_8003F150. The blocks are translated the same way either way; only their layout differs. The random tester decides on it.)
M2C_NOTES = (r'fallthrough|switch \d+|Duplicate return node #\d+\. Try simplifying control flow for better match|'
             r'Flowgraph is not reducible, falling back to gotos-only mode\.')


def exact_spill_slots(c, f):
    """m2c's note `/* compiler-managed */` on a stack local, taken out where the local is exact as it's declared (2026-11-07).

    m2c writes it on a stack slot it saw stored with values of different types -- a register kept there across calls, an int
    one time and a pointer another (func_80011554's sp3C, func_801089E8's sp80-sp88, func_80129C90's sp18) -- and declares it
    once, with one of them. Every store and load of it is still in the C. Where every instruction that uses that slot is a
    whole-word lw or sw and the local is declared a 32-bit integer or a pointer, the local holds exactly the word the
    instructions move, whichever type each value had: the note is taken out. Otherwise it stays, and is refused as a warning
    (a float kept there with swc1 and read back as an integer would be converted, not moved)."""
    kinds = {}
    for mn, ops in f['ins'].values():
        m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if m:
            kinds.setdefault(int(m.group(1), 0), set()).add(mn)
    def one(m):
        offset = int(m.group(3), 16)
        declared = m.group(2).strip()
        word = re.fullmatch(r'(?:s32|u32|M2C_UNK32|\w+ \*+)', declared)
        if word and kinds.get(offset) and kinds[offset] <= {'lw', 'sw'}:
            return m.group(1)
        # (2026-11-12, agent "refused") A word moved both ways, lw / sw and lwc1 / swc1 -- func_8012BF50's sp18 and
        # modrep_func_8012CB8C's sp10-sp34, floats stored with swc1 and handed on with lw, or a callee's results read back
        # with lwc1 -- and declared a 32-bit integer or a float: the word itself, LiftWord<T> (lift_mem.h Ref<LiftWord<T>>),
        # which reads and stores the bits whichever kind each instruction moves, and converts nothing. Anything else done with
        # it doesn't compile, and is refused there: func_801089E8 does float arithmetic on its sp80, which m2c declared s32.
        both = kinds.get(offset, set())
        if declared in ('s32', 'u32', 'M2C_UNK32', 'M2C_UNK', 'f32') and both and both <= {'lw', 'sw', 'lwc1', 'swc1'} \
                and both & {'lw', 'sw'} and both & {'lwc1', 'swc1'}:
            t = {'u32': 'u32', 'f32': 'f32'}.get(declared, 's32')
            return '%sLiftWord<%s> sp%s;' % (re.match(r'\s*', m.group(1)).group(0), t, m.group(3))
        return m.group(0)
    return re.sub(r'^(\s*(\w+(?:\s*\*+)?)\s*sp([0-9A-F]+);)\s*/\* compiler-managed \*/', one, c, flags=re.M)


def jump_index_locals(c, name):
    """(2026-11-12, agent "refused2") A table jump's index as the translator switches on it, lift_ji_<jr> (to_asm.py
    find_slot_tables: stored at the sll, read for the lookup): m2c takes the name for a global, `extern s32 lift_ji_...;`. It is
    the function's own: a local, s32, declared first in its body. (Nothing else is called that; without one the text is as it was.)"""
    names = sorted(set(re.findall(r'\blift_ji_[0-9A-F]{8}\b', c)))
    if not names:
        return c
    c = '\n'.join(l for l in c.splitlines() if not re.match(r'^extern\b.*\blift_ji_[0-9A-F]{8};', l))
    m = re.search(r'^.*\b' + re.escape(name) + r'\(.*\) \{$', c, re.M)
    if not m:
        return c
    return c[:m.end()] + ''.join('\n    s32 %s = 0;' % n for n in names) + c[m.end():]


def read_v1_after_calls(c, name):
    """v1 read after a call (2026-11-09). m2c takes a call's v1 to be the low half of a 64-bit result and writes `(u32) (u64)
    func_X(...)`, or `(u32) (u64) temp_ret` -- the value of v0 over again in C++, where lift_call gives back v0 alone. The original
    reads what the callee left in v1, and that's in ctx->r3 right after the call: callers_rely_on sees this read in the MIPS, so a
    converted callee keeps v1 (LiftAbiOut). Here the read becomes that: straight after the call, kept in its own variable if
    the value is used later. (What it can't place -- no declaration, no assignment on one line -- stays and is refused below.)
    func_800DA494 (osGetTime) stays as it is, so it stays refused."""
    if name == 'func_800DA494':
        return c
    # (func_X(...) inline: the call, then the register.)
    pattern = re.compile(r'\(u32\)\s*\(u64\)\s*((?:func_\w+|lift_overlay_[0-9A-F]{8}|rw_\w+)\()')
    at = 0
    while True:
        m = pattern.search(c, at)
        if not m:
            break
        depth, i = 1, m.end()
        while i < len(c) and depth:
            depth += {'(': 1, ')': -1}.get(c[i], 0)
            i += 1
        if depth:
            break
        c = c[:m.start()] + '((void) ' + c[m.start(1):i] + ', (s32) ctx->r3)' + c[i:]
        at = m.start() + 8
    # (temp_ret_N = func_X(...); ... (u32) (u64) temp_ret_N ...: a variable set just after the call.)
    for t in sorted(set(re.findall(r'\(u32\)\s*\(u64\)\s*(temp_ret(?:_\d+)?)\b', c))):
        v1 = 'lift_v1_' + t
        d = re.search(r'^([ \t]*)s32 ' + t + r';[ \t]*$', c, re.M)
        a = re.search(r'^([ \t]*)' + t + r' = [^\n]*;[ \t]*$', c, re.M)
        if not d or not a:
            continue
        c = c[:a.end()] + '\n' + a.group(1) + v1 + ' = (s32) ctx->r3;' + c[a.end():]
        c = c[:d.end()] + '\n' + d.group(1) + 's32 ' + v1 + ';' + c[d.end():]
        c = re.sub(r'\(u32\)\s*\(u64\)\s*' + t + r'\b', v1, c)
    return c


def only_unset_registers(c):
    """Whether every comment m2c left in this C is its "Read from unset register $x" (2026-11-11): the one warning abi.py's
    round can answer, by loading the register from the context after the call that left it (abi.reload_sites)."""
    without = re.sub(r'/\* (?:extern|' + M2C_NOTES + r') \*/', '', c)
    left = re.findall(r'/\*.*?\*/|//[^\n]*', without, flags=re.S)
    return bool(left) and all(re.fullmatch(r'/\* Read from unset register \$\w+ \*/', x) for x in left)


def refuse_c(c, f):
    """Why this C can't be taken as it stands, or None."""
    without_extern = re.sub(r'/\* (?:extern|' + M2C_NOTES + r') \*/', '', c)
    if '/*' in without_extern or '//' in without_extern:
        return 'm2c left a comment (a warning)'
    # (M2C_LEFT, a register passed on as the calls before left it, is turned into C++ by pass_left_registers.)
    if re.search(r'\bM2C_(?!FIELD\b|UNK|BITWISE\b|STACK_ARG\b|LEFT\b)', c):
        return 'uses an m2c stand-in'
    # v1 read after a call (2026-11-08). m2c takes every call's v1 to be the low half of a 64-bit result,
    # `(u32) (u64) func_X(...)` -- its arch_mips.py function_return -- but no call here has one (convert_calls refuses a 64-bit
    # result), and lift_call gives back v0 alone: the cast then gives v0's bits again, where the original reads whatever the
    # callee left in v1. func_800DA4DC ends `jal func_80040130; ...; addu $v0, $v1, $zero` and came out returning the call's
    # v0; func_800063BC, func_8003FC84 and func_800075DC read v1 after a call the same way. Refused: nothing in the C++ holds
    # the callee's v1.
    if re.search(r'\(u32\)\s*\(u64\)\s*(?:func_\w+\(|lift_overlay_[0-9A-F]{8}\(|rw_\w+\(|temp_ret\b|temp_ret_\d+\b)', c):
        return 'reads v1 after a call (what the callee left there, not its result)'
    # Whole-number division: dividing by zero, or the most negative number by -1. The N64 compiler checked both after each division
    # and stopped the program (`break 7`, `break 6`), and the translated original divides first, which crashes the PC on zero: so
    # where every division has the compiler's checks, the converted one crashing on the same inputs is no different. Without them
    # (hand-written code), the N64 gives some result and the C++ would crash: refused.
    # The random-input tester counts a crash as a difference, so one that divides by zero on random inputs isn't taken: what's
    # taken guards its divisions itself.
    kinds = [mn for mn, _ in f['ins'].values()]
    divisions = sum(1 for mn in kinds if mn in ('div', 'divu'))
    if divisions and re.search(r'[/%]=?\s*(?!' + NUMBER + r')', without_extern) and kinds.count('break') < divisions:
        return 'divides by a variable'
    return None


def bitwise_casts(c, is_a_move=None):
    """M2C_BITWISE(T, x) becomes __builtin_bit_cast(T, lift_plain(x)): the same bits, read as the other type.

    m2c writes it where a value moves between an integer and a float register without converting (mfc1 / mtc1): most often a float
    passed to a function in an integer register, as the MIPS rules have it after an integer parameter (func_800225BC's third). Its
    own header defines it as a plain cast, which converts the number instead. lift_plain reads a stack local (Ref<T>) first, so the
    bits are its value's and not its address's. If the sizes differ (a double), the cast doesn't compile, and the function is
    refused there. Until 2026-11-07 any M2C_BITWISE was refused ('uses an m2c stand-in', 64 of the functions of 100+
    instructions).

    Not when the value is a call's result: then it shows the declaration given m2c for that function (prototype) has the wrong
    register for its result, not a move. func_8012DAF0 reads v0 after func_8012EC5C, declared f32 (it writes only f0), and m2c
    wrote M2C_BITWISE(s32, func_8012EC5C(...)): the f0 bits, where the original takes v0. The tester showed it different; None
    here, so it's refused before that."""
    while True:
        found = list(re.finditer(r'\bM2C_BITWISE\(', c))
        if not found:
            return c
        m = found[-1]           # (the last first: one inside another's value is then already done)
        depth, i = 0, m.end()
        while c[i] != ',' or depth:
            depth += {'(': 1, '[': 1, ')': -1, ']': -1}.get(c[i], 0)
            i += 1
        _, close = split_arguments(c, i + 1)
        # (is_a_move, 2026-11-08, scratch/lift_regs: what the caller reads after this call is only the register the declaration
        #  gives its result in, so this cast is an mfc1 / mtc1 moving that result on -- a float passed in an integer register, as
        #  func_8010FD74 passes func_80131C40's result in a1 -- and not a misdeclared result. The tester compares the parameter.)
        target = c[m.end():i].strip()
        inline = re.match(r'\s*(?:\((?:' + TYPES + r')\)\s*)*\(?\s*(' + callee_pattern() + r')\(', c[i + 1:close])     # (A system function's call counts too, since 2026-11-07: callee_pattern.)
        if inline:
            if not (is_a_move and is_a_move(inline.group(1), target)):
                return None
        # (The same where the call has its own temp, temp_ret: that's the call's result itself, before any register takes it,
        # as the call written in place was. m2c gives a call one when it has to stay before a later read -- since
        # 2026-11-07, see pin_pending_calls in m2c's translate.py -- and func_801384A0's `temp_ret = func_8012EA74(...);
        # temp_f0 = M2C_BITWISE(f32, temp_ret);` got through that way. A register's copy, `temp_f0 = func_80013F00(...)` then
        # M2C_BITWISE(s32, temp_f0) for an mfc1 (func_800120DC), is a move, as before.)
        temp = re.fullmatch(r'\s*(?:\((?:' + TYPES + r')\)\s*)*(temp_ret(?:_\d+)?)\s*', c[i + 1:close])
        if temp:
            made = re.findall(r'\b' + temp.group(1) + r'\s*=\s*(?:\((?:' + TYPES + r')\)\s*)*(func_\w+|lift_overlay_[0-9A-F]{8})\(', c[:m.start()])
            if not (made and is_a_move and is_a_move(made[-1], target)):
                return None
        c = c[:m.start()] + '__builtin_bit_cast(' + c[m.end():i] + ', lift_plain(' + c[i + 1:close].strip() + '))' + c[close + 1:]


def mask_shift_counts(c):
    """x << n becomes x << (n & 31): the N64 only looks at the count's low 5 bits, and in C++ a count of 32 or more is undefined."""
    out = []
    at = 0
    for m in re.finditer(r'(?:<<|>>)=?\s*', c):
        if m.start() < at:
            continue
        i = m.end()
        # The count: any casts or stars in front, then a name or a bracket, then anything following on from it.
        while True:
            cast = re.compile(r'\((?:' + TYPES + r')\)\s*').match(c, i)
            if cast:
                i = cast.end()
            elif c[i] in '*-~!':
                i += 1
            else:
                break
        if re.compile(NUMBER).match(c, i):
            continue            # (A fixed count: nothing to do.)
        name = re.compile(r'\w+').match(c, i)
        if name:
            i = name.end()
        while i < len(c) and c[i] in '([':
            depth = 0
            while True:
                if c[i] in '([':
                    depth += 1
                elif c[i] in ')]':
                    depth -= 1
                i += 1
                if depth == 0:
                    break
        out += [c[at:m.end()], '(', c[m.end():i], ' & 31)']
        at = i
    out.append(c[at:])
    return ''.join(out)


ACCESS_BYTES = {'lb': 1, 'lbu': 1, 'sb': 1, 'lh': 2, 'lhu': 2, 'sh': 2, 'lw': 4, 'sw': 4, 'lwc1': 4, 'swc1': 4, 'ldc1': 8, 'sdc1': 8}
TYPE_BYTES = {'s8': 1, 'u8': 1, 'M2C_UNK8': 1, 's16': 2, 'u16': 2, 'M2C_UNK16': 2, 's32': 4, 'u32': 4, 'f32': 4, 'void': 4, 'M2C_UNK': 4,
              'M2C_UNK32': 4, 'f64': 8, 's64': 8, 'u64': 8, 'M2C_UNK64': 8}


MOVED_BY = re.compile(r'\$(\w+),\s*\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$')
FOLD_DEPTH = 4


def base_steps(f):
    """For each memory access (not to the stack): its offset and its base register, and every constant the base could have been
    stepped by since an earlier value, through `addiu r, s, K` and plain copies (`addu r, s, $zero`, `or r, s, $zero`).

    m2c folds such a step into the access: `addiu $a1, $a1, 1; lbu $v1, 0($a1)` comes out as M2C_FIELD(arg1, u8 *, 1), the field
    at offset 1 of the pointer before the step (func_80029CF0), and display-list writes `addiu $a2, $a2, 8; sw $a1, 0($a2)` as
    offset 8 (func_80032A10). The offset in the C is then the instruction's plus the steps, so that's what the check compares.
    Found by walking back from the access to each definition of its base on every path, FOLD_DEPTH steps at most."""
    addrs, succ = cfg(f)
    pred = {}
    for a, nexts in succ.items():
        for n in nexts:
            pred.setdefault(n, []).append(a)

    def step_of(a):
        """(source register, constant) if the instruction at a sets its register to another plus a constant, else None."""
        mn, ops = f['ins'][a]
        if mn == 'addiu':
            m = MOVED_BY.match(ops)
            if m and m.group(2) not in ('sp', 'zero'):
                return m.group(2), int(m.group(3), 0)
        if mn in ('addu', 'or', 'daddu', 'move'):
            regs = re.findall(r'\$(\w+)', ops)
            if len(regs) == 3 and 'zero' in regs[1:] and regs[1:] != ['zero', 'zero']:
                return [r for r in regs[1:] if r != 'zero'][0], 0
        return None

    def definitions(at, reg):
        """The instructions that last set reg on some path to the one at `at` (not counting `at` itself)."""
        found, seen, work = set(), set(), list(pred.get(at, []))
        while work:
            a = work.pop()
            if a in seen:
                continue
            seen.add(a)
            if reg in uses_defs(*f['ins'][a])[1]:
                found.add(a)
                continue
            work += pred.get(a, [])
        return found

    def slot_stores(at, offset):
        """(2026-11-14, rest100) The instructions that last wrote the frame word at `offset` from $sp on some path to `at`, or
        None if $sp itself changes on the way (then which word is which isn't known)."""
        found, seen, work = set(), set(), list(pred.get(at, []))
        while work:
            a = work.pop()
            if a in seen:
                continue
            seen.add(a)
            mn, ops = f['ins'][a]
            m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
            width = {'sb': 1, 'sh': 2, 'sw': 4, 'swl': 4, 'swr': 4, 'swc1': 4, 'sd': 8, 'sdc1': 8}.get(mn)
            if m and width and int(m.group(1), 0) - 4 < offset < int(m.group(1), 0) + width:     # (it overlaps the word)
                found.add(a)
                continue
            if 'sp' in uses_defs(mn, ops)[1]:
                return None
            work += pred.get(a, [])
        return found

    def steps(at, reg, depth):
        out = {0}
        if depth == 0:
            return out
        for d in definitions(at, reg):
            s = step_of(d)
            if s:
                out |= {k + s[1] for k in steps(d, s[0], depth - 1)}
                continue
            # (2026-11-14, rest100) A pointer kept in a frame word and read back (`sw $v0, 0x38($sp)` ... `lw $v0, 0x38($sp)`):
            # m2c follows the value through the slot and folds the step made before the store into the access, as it does
            # through a register -- func_80124E50's `M2C_FIELD(temp_v0, s32 *, 0x10)` is `sw $a0, 0($v0)` with v0 reloaded
            # from the slot that held temp_v0 + 0x10, refused until now. Only where every last write of that word on every
            # path is a whole-word `sw` (anything else -- a part of the word, a float store -- gives no step).
            mn, ops = f['ins'][d]
            m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
            if mn == 'lw' and m:
                stores = slot_stores(d, int(m.group(1), 0))
                same_word = lambda x: int(re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)', f['ins'][x][1]).group(1), 0) == int(m.group(1), 0)
                if stores and all(f['ins'][x][0] == 'sw' and same_word(x) for x in stores):
                    for x in stores:
                        out |= steps(x, re.match(r'\$(\w+)', f['ins'][x][1]).group(1), depth - 1)
        return out

    found = []
    for a in addrs:
        mn, ops = f['ins'][a]
        m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)', ops)
        if mn in ACCESS_BYTES and m and m.group(2) != 'sp':
            found.append((mn, int(m.group(1), 0), steps(a, m.group(2), FOLD_DEPTH)))
    return found


def fields_disagree(c, f):
    """Whether m2c's memory accesses might not be the N64's. Comparison can't be relied on to catch this: reading one byte of a
    2-byte value only goes wrong when the top byte isn't zero (func_8000C1A8). So it's checked against the instructions themselves.

    m2c treats a base register and an offset as one field with one type. So it's refused if the C's size for an offset isn't what
    the instructions at that offset use -- counting an offset m2c folded a pointer step into (base_steps) as that offset too.
    (2026-11-07 prototype: until then only the instruction's own offset counted, which refused every folded step.)"""
    at = {}
    for mn, offset, steps in base_steps(f):
        for k in steps:
            at.setdefault(offset + k, set()).add(mn)
    # (Until 2026-11-05 an offset read at two sizes or signs was refused too, as random values in play might not show it. The
    # random-input tester does, with random memory in every byte, so that's left to it.)
    # Every M2C_FIELD, one inside another's first argument too: its type and offset are its last two arguments. (Until
    # 2026-11-07 a regular expression took the first `, type *, offset)` after each M2C_FIELD( -- an inner one's, for a field
    # whose base is a field -- so the outer one was never checked. m2c's offset-0 accesses are fields since then, and nest more.)
    for start in [m.end() for m in re.finditer(r'\bM2C_FIELD\(', c)]:
        _, close = split_arguments(c, start)
        depth, cuts = 0, []
        for i in range(start, close):
            depth += {'(': 1, '[': 1, ')': -1, ']': -1}.get(c[i], 0)
            if c[i] == ',' and depth == 0:
                cuts.append(i)
        m = re.fullmatch(r'\s*(\w+)\s*(\*+)\s*,\s*(-?(?:0x[0-9A-Fa-f]+|\d+))\s*', c[cuts[-2] + 1:close]) if len(cuts) >= 2 else None
        if not m:
            return True
        # Each access on its own (2026-11-07, scratch/lift_reads): a read must match a load at that offset in size and, for a byte
        # or a halfword, in sign; a write must match a store in size. Until then a size any instruction at the offset used was
        # enough, so m2c's one type for a place passed whatever each access really was: func_800F25D4 read a word (lw 0x80) as a
        # byte, because a lbu elsewhere read the same field's first byte, and func_800199CC read an lb's byte as u8. (m2c
        # --access-types now writes each access with its own instruction's type; this catches anything it doesn't cover.)
        kinds = at.get(int(m.group(3), 0))
        reads, writes = access_use(c, close + 1, start - len('M2C_FIELD('))
        if kinds is None or not access_matches(kinds, m.group(1), m.group(2), reads, writes):
            return True
    # The same for an access to a fixed address, `*(s8 *)0x8016B9E0`, where the instructions show which address it is.
    fixed = fixed_address_kinds(f)
    for m in re.finditer(r'\*\((\w+) (\*+)\)(0x[0-9A-Fa-f]{8})\b(?!\s*\+)', c):
        kinds = fixed.get(int(m.group(3), 16))
        reads, writes = access_use(c, m.end(), m.start())
        if kinds and m.group(1) != 'void' and not access_matches(kinds, m.group(1), m.group(2), reads, writes):
            return True
    return False

LOADS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwc1', 'ldc1'}
SIGNED_LOADS = {'lb', 'lh'}
UNSIGNED_LOADS = {'lbu', 'lhu'}
UNSIGNED_TYPES = {'u8', 'u16'}       # (and s8, s16, M2C_UNK8, M2C_UNK16 are signed: lift_mem.h's M2C_UNK8 is s8)


def access_use(c, end, start):
    """Whether the access whose text runs from `start` to `end` is read, written, or both: `x = v` writes, `x += v` and `x++` do
    both, anything else reads. Dereferenced, `*M2C_FIELD(x, s32 **, 0x14) = v`, it's read -- the pointer it holds -- and what
    it points to is written (func_8001062C)."""
    if start > 0 and c[start - 1] == '*':
        return True, False
    m = re.compile(r'\s*(\+\+|--|(<<|>>|[-+*/%&|^])?=(?!=))').match(c, end)
    if not m:
        return True, False
    return m.group(1) in ('++', '--') or bool(m.group(2)), True


def access_matches(kinds, t, stars, reads, writes):
    """Whether the instructions at a place (mnemonics) include one that does this access: a load of the C type's size and, for a
    byte or halfword, its sign, if it reads; a store of its size, if it writes."""
    size = 4 if len(stars) > 1 else TYPE_BYTES.get(t)
    if size is None:
        return False
    if reads:
        loads = [k for k in kinds if k in LOADS and ACCESS_BYTES[k] == size]
        if size in (1, 2):
            loads = [k for k in loads if (k in UNSIGNED_LOADS) == (t in UNSIGNED_TYPES)]
        if not loads:
            return False
    if writes and not any(k not in LOADS and ACCESS_BYTES[k] == size for k in kinds):
        return False
    return True


def fixed_address_kinds(f):
    """The fixed addresses the function's loads and stores reach through `lui` / `addiu` (as restore_lost_sizes follows them), and
    the mnemonics used at each: {address: {mnemonics}}."""
    known, values = {}, {}
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        regs = re.findall(r'\$(\w+)', ops)
        if mn == 'lui':
            values[regs[0]] = int(ops.split(',')[-1], 0) << 16
            continue
        if mn == 'addiu' and len(regs) == 2 and regs[1] in values:
            values[regs[0]] = (values[regs[1]] + int(ops.split(',')[-1], 0)) & 0xFFFFFFFF
            continue
        place = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)', ops)
        if mn in ACCESS_BYTES and place and place.group(2) in values:
            known.setdefault((values[place.group(2)] + int(place.group(1), 0)) & 0xFFFFFFFF, set()).add(mn)
        for r in uses_defs(mn, ops)[1]:
            values.pop(r, None)
    return known


SIZES = {'lb': 's8', 'lbu': 'u8', 'lh': 's16', 'lhu': 'u16', 'lw': 's32', 'sb': 'u8', 'sh': 'u16', 'sw': 's32',
         'lwc1': 'f32', 'swc1': 'f32', 'ldc1': 'f64', 'sdc1': 'f64'}     # (floats: lwc1/swc1 one float register, ldc1/sdc1 a pair)


def restore_lost_sizes(c, f):
    """m2c sometimes loses the size of a fixed address and writes *(void *)0x80012345, or just *0x80012345. The N64 instruction
    still says what it was."""
    if '*(void *)0x' not in c and not re.search(r'(?<![\w)])\*0x[0-9A-Fa-f]{8}', c) and not re.search(r'\*\(0x[0-9A-Fa-f]{8} \+ ', c):
        return c
    known = {}
    values = {}
    indexed = {}   # (a register holding a known address plus an index: the table's address)
    kinds = {}     # (every type the instructions use each address at: a float address also read as an integer isn't given one)
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        regs = re.findall(r'\$(\w+)', ops)
        if mn == 'lui':
            values[regs[0]] = int(ops.split(',')[-1], 0) << 16
            continue
        if mn == 'addiu' and len(regs) == 2 and regs[1] in values:
            values[regs[0]] = (values[regs[1]] + int(ops.split(',')[-1], 0)) & 0xFFFFFFFF
            continue
        if mn == 'addu' and len(regs) == 3 and (regs[1] in values) != (regs[2] in values):
            indexed[regs[0]] = values[regs[1] if regs[1] in values else regs[2]]
            values.pop(regs[0], None)
            continue
        place = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)', ops)
        if mn in SIZES and place and (place.group(2) in values or place.group(2) in indexed):
            address = ((values[place.group(2)] if place.group(2) in values else indexed[place.group(2)]) + int(place.group(1), 0)) & 0xFFFFFFFF
            # What a load says outranks a store, which doesn't show whether the value is signed.
            if mn[0] == 'l' or address not in known:
                known[address] = SIZES[mn]
            kinds.setdefault(address, set()).add(SIZES[mn])
        for r in uses_defs(mn, ops)[1]:
            values.pop(r, None)
            indexed.pop(r, None)
    # (A float address the instructions also use at another type -- a lw that moves a float's bits, say -- stays void: which accesses
    #  m2c printed that way isn't known, and a load read as the wrong one of f32 and s32 would convert the number. It doesn't compile.)
    for address, types in kinds.items():
        if types & {'f32', 'f64'} and len(types) > 1:
            known.pop(address, None)
    def one(m):
        t = known.get(int(m.group(1), 16))
        return '*(%s *)%s' % (t, m.group(1)) if t else m.group(0)
    c = re.sub(r'\*\(void \*\)(0x[0-9A-Fa-f]+)', one, c)
    def table(m):   # (*(0xADDR + index) -> *(T *)(0xADDR + index))
        t = known.get(int(m.group(1), 16))
        return '*(%s *)(%s + ' % (t, m.group(1)) if t else m.group(0)
    c = re.sub(r'\*\((0x[0-9A-Fa-f]{8}) \+ ', table, c)
    return re.sub(r'(?<![\w)])\*(0x[0-9A-Fa-f]{8})\b', one, c)


def parse_signature(cpp):
    m = SIGNATURE.search(cpp)
    if not m:
        return None
    ret = m.group(1).strip()
    args = []
    if m.group(3).strip() not in ('', 'void'):
        for a in m.group(3).split(','):
            a = a.strip()
            t, n = a.rsplit(' ', 1)
            args.append((t.strip(), n))
    return ret, args, m.start(), m.end()


def arg_sources(args):
    """Where each parameter comes from on the N64: registers for the first four, the stack after that."""
    out = []
    floats_ok = True
    doubles = any(t == 'f64' for t, _ in args)
    for position, (t, n) in enumerate(args):
        # m2c numbers a parameter by where it arrives, leaving out any the function ignores: "arg2" is a2 even if it's the first.
        m = re.fullmatch(r'arg(\d+)', n)
        if not m or t in ('s64', 'u64', 'M2C_UNK64') or (t == 'f64' and not floats_ok):
            return None
        i = int(m.group(1))
        if out and position and int(re.fullmatch(r'arg(\d+)', args[position - 1][1]).group(1)) >= i:
            return None         # (m2c put them out of order, func_8001C62C's `f32 arg1, s16 arg0`: it read registers the rules don't explain)
        if doubles and floats_ok and t in ('f32', 'f64'):
            # (2026-11-12, agent "refused2") With a double among them (double_parameter_halves): the first two parameters, while
            # only floats come before, are in f12 and f14 -- a double the pair f12:f13 or f14:f15, the even register's 64 bits in
            # the context -- and m2c names them by those registers, arg0 and arg1, as it does floats: (f64 arg0, f64 arg1),
            # (f32 arg0, f64 arg1), (f64 arg0, f32 arg1). An integer after them is named by its argument slot, a double
            # filling two (f64 arg0, s32 arg2), as the rules place it. Any other numbering is refused, as for floats, and a
            # double after an integer (in a2:a3, or on the stack) above.
            if position >= 2 or i != position:
                return None
            out.append('ctx->f%d.%s' % (12 + 2 * position, 'd' if t == 'f64' else 'fl'))
            continue
        if t == 'f64':
            return None
        if t == 'f32':
            # Floats arrive in f12 and f14 only if nothing before them is an integer, and only the first two.
            if floats_ok and i < 2 and position == i:
                out.append('ctx->f%d.fl' % (12 + 2 * i))
                continue
            if floats_ok and position != i:
                return None     # (one before it is ignored, and might have been a float or a double: where this one came is unknown)
            # Otherwise, by the MIPS rules, as a word in the integer register or stack slot an integer would use: the float's bits
            # (func_80027954(s32, f32) reads its float from a1, 2026-11-07).
            floats_ok = False
            word = '(u32)ctx->r%d' % (4 + i) if i < 4 else '(u32)*P<u32>((u32)ctx->r29 + %d)' % (4 * i)
            out.append('__builtin_bit_cast(f32, %s)' % word)
        else:
            floats_ok = False
            if i < 4:
                out.append('lift_arg<%s>(ctx->r%d)' % (t, 4 + i))
            else:
                # A stack parameter fills a whole 4-byte slot, a small one in the slot's last bytes: read the slot, then narrow it.
                out.append('lift_arg<%s>((u32)*P<u32>((u32)ctx->r29 + %d))' % (t, 4 * i))
    return out


def callers_rely_on(functions, analysis, by_caller=None):
    """For every function, the registers something reads after calling it without setting them first: what it must keep doing.

    A caller that returns straight after the call passes a value in v0 or v1 on to its own caller, so those count if its caller
    relies on them. Returned too: the same for calls through a pointer, and which functions pass their callee's result on.

    by_caller, if given (a dict), is filled with what each caller itself reads after its calls to each function, before
    setting it: {(caller, callee): registers} -- one caller's own reads, for its own declaration of that function's result
    (prototype, 2026-11-07)."""
    reads_after = {}
    after_indirect = set()
    passes_on = {}          # callee -> [(caller, register)]: the caller returns with this register as its callee left it.
    for name, f in functions.items():
        addrs, succ = cfg(f)
        # (What goes back through a return: v0-f1 and what this function hands back besides -- mips.returned_in, 2026-11-16.)
        RETURN_VALUE = returned_in(analysis, name)
        for a in addrs:
            mn, ops = f['ins'][a]
            if mn not in ('jal', 'jalr'):
                continue
            target = f['calls'].get(a + 4) or f['calls'].get(a) if mn == 'jal' else None    # (The translator writes a call after its delay slot.)
            seen = {}
            result = set()
            returned_with = set()
            # Follow every path after the call, until each register has been written, noting any read before that.
            stack = [(s, frozenset()) for s in succ.get(a + 4, [])]
            while stack:
                pc, written = stack.pop()
                if pc in seen and seen[pc] <= written:
                    continue
                seen[pc] = written if pc not in seen else seen[pc] & written
                m, o = f['ins'][pc]
                if m in ('jal', 'jalr', 'jr'):
                    # The instruction after a call or a return runs before it takes effect.
                    if pc + 4 in f['ins']:
                        u, d = uses_defs_in(f, pc + 4)      # (uses_defs_in: a saved register passed on as a stack parameter is read)
                        result |= {r for r in u if r not in written}
                        written = written | d
                    if m == 'jal':
                        callee = f['calls'].get(pc + 4) or f['calls'].get(pc)
                        use = set(analysis.get(callee, {}).get('use', [])) if callee else set()
                        result |= {r for r in use if r not in written}
                        written = written | returned_in(analysis, callee)
                    elif m == 'jalr':
                        result |= {r for r in uses_defs_in(f, pc)[0] if r not in written}
                        written = written | returned_in(analysis, None)
                    else:
                        returned_with |= {r for r in RETURN_VALUE if r not in written}
                        continue
                    for s in succ.get(pc + 4, []):
                        stack.append((s, frozenset(written)))
                    continue
                u, d = uses_defs_in(f, pc)
                result |= {r for r in u if r not in written}
                written = written | d
                for s in succ.get(pc, []):
                    stack.append((s, frozenset(written)))
            if target:
                reads_after.setdefault(target, set()).update(result)
                if by_caller is not None:
                    by_caller.setdefault((name, target), set()).update(result)
                for r in returned_with:
                    passes_on.setdefault(target, []).append((name, r))
            else:
                after_indirect |= result
                # (An overlay call: this caller's own reads after it, under its overlay_name, for overlay_prototype. Only in by_caller,
                #  which no other function's declarations read; after_indirect stays as it was.)
                if mn == 'jal' and by_caller is not None and overlay_target(ops) is not None:
                    by_caller.setdefault((name, overlay_name(overlay_target(ops))), set()).update(result)
    return reads_after, after_indirect, passes_on

# ---- What callers rely on, exactly (prototype, scratch/lift_callers, 2026-11-07) ----
#
# callers_rely_on above over-counts in one place and under-counts in others. Measured on the 2026-11-07 run: it refused 579 functions
# (59,615 instructions), and only 50 of them because a direct caller's own instructions read the register. callers_rely_exactly
# replaces it for the "a caller reads what it leaves" check. A register counts against a function only if, on some path after
# some call to it, it's read before it's written:
#   - by the caller's own instructions;
#   - by a function the caller calls later, entered or jumped into (its analysis `use`; one that isn't in the analysis -- a system
#     function, or one found at run time -- reads all the parameter registers, as analyse.py assumes);
#   - by a later call through a pointer (its target register, and all the parameter registers: the target is unknown);
#   - or by the caller's own callers, when the path returns, or leaves by a jump into another function, with the register still
#     as the call left it. Any register, not only v0/v1/f0/f1: the caller hands back whatever it didn't change.
# Calls through a pointer (jalr) can reach any function whose address is taken, so for those what any jalr site relies on counts
# (after_indirect). A `jal` with no named callee is not one of those: the translator finds its overlay function at run time, but
# its address is fixed, so it counts for the functions at that address only. (Until now it went into after_indirect: one such call,
# func_801214C8's to 0x800D9AB0, reads a0 and a1 after it, which counted against all 560 functions whose address is taken.)

# What an unknown function is taken to read, and what goes back through a return. These keep callers_rely_on's assumptions --
# a function not in the analysis (a system function) reads nothing, a later call through a pointer reads only the register it jumps
# through, and only v0, v1, f0, f1 go back through a return -- which trust the calling rules. The fully cautious settings
# ('params', 'params', 'all') refuse 632 of the 2026-11-07 candidates: a policy question, see the report.
# (Since 2026-11-07 the system functions system_reads.model() handles are in the analysis with what their source reads (with_system),
# so this is only for the ones it refuses, osDriveRomInit and osAiGetLength. Counting the others' exact reads refused no function
# that was a candidate before.)
UNKNOWN_READS = 'none'         # 'params': every parameter register; 'none': nothing (as callers_rely_on had it)
JALR_READS = 'target'          # 'params': its target register and every parameter register; 'target': only the target
FLOW = 'result'                # 'all': every register; 'result': v0, v1, f0, f1 only
# And switches to measure each correction on its own, against callers_rely_on:
OVERLAY = 'whole'              # a jal looked up at run time reaches: 'whole': the whole functions at its address (not a fragment
                               # starting mid-function, func_801104E8_ovl_c); 'all': every function there; 'indirect': as a jalr
TABLES = 'follow'              # after a call, a jump through a table: 'follow' its cases; 'stop' as if it returned (callers_rely_on)
OUT = 'handle'                 # a branch or jump into another function: 'handle' it; 'ignore' it, ending the path (callers_rely_on)


def functions_at(functions):
    """Each address, and the functions at it that a call can reach: the main one and its overlay copies (func_8010B468,
    func_8010B468_ovl_d). Not a tail the translator started part-way through a function (pops_a_frame_it_never_made)."""
    return mips.functions_at(functions, whole=OVERLAY == 'whole')


def callees_of_site(f, a, at_address):
    """The functions the call at a can reach: its named callee, or for a `jal` the translator looks up at run time, the
    functions at its address. None for a call through a pointer."""
    mn, ops = f['ins'][a]
    if mn == 'jalr':
        return None
    named = f['calls'].get(a + 4) or f['calls'].get(a)
    if named:
        return [named]
    if OVERLAY == 'indirect':
        return None
    try:
        return at_address.get(int(ops, 0), [])
    except ValueError:
        return []


def reads_of(callees, analysis):
    """What a call to these reads: their analysis `use`, or every parameter register if one isn't in the analysis."""
    found = set()
    for c in callees if callees else [None]:
        found |= set(analysis[c]['use']) if c in analysis else set(PARAMETER_REGISTERS) if UNKNOWN_READS == 'params' else set()
    return found


def after_call(f, a, analysis, at_address, every):
    """After the call at a: the registers read before they're written (by the caller, or a function it calls later), and the
    registers that go back to the caller's own caller unchanged on some path. Follows every path, as callers_rely_on does."""
    addrs, succ = cfg(f)
    # (What goes back through this function's return: with FLOW 'result', v0-f1 and what it hands back besides --
    #  mips.returned_in, 2026-11-16.)
    if FLOW == 'result':
        every = returned_in(analysis, f.get('name'))
    reads, flows = set(), set()
    seen = {}

    def outside(pc):
        """If the instruction at pc branches or jumps out of the function: the function it goes into (a list, maybe empty)."""
        mn, ops = f['ins'][pc]
        m = re.search(r'L_([0-9A-F]{8})', ops)
        if m and int(m.group(1), 16) not in f['ins'] and (mn in ('j', 'b') or mn in BRANCH):
            named = f['calls'].get(pc + 4) or f['calls'].get(pc)
            return [named] if named else []
        return None

    stack = [(s, frozenset()) for s in succ.get(a + 4, [])]
    while stack:
        pc, written = stack.pop()
        if pc in seen and seen[pc] <= written:
            continue
        seen[pc] = written if pc not in seen else seen[pc] & written
        m, o = f['ins'][pc]
        gone = outside(pc) if OUT == 'handle' else None
        if gone is not None:
            # Into another function, which returns to this one's caller: what it reads, and everything still unchanged.
            w = written
            if pc + 4 in f['ins']:
                u, d = uses_defs_in(f, pc + 4)      # (uses_defs_in: a saved register passed on as a stack parameter is read)
                reads |= {r for r in u if r not in w}
                w = w | d
            reads |= {r for r in reads_of(gone, analysis) if r not in w}
            flows |= every - w
            if m in ('j', 'b'):
                continue
            # (A conditional branch also falls through: cfg gives that path.)
        if m in ('jal', 'jalr', 'jr'):
            if pc + 4 in f['ins']:
                u, d = uses_defs_in(f, pc + 4)
                reads |= {r for r in u if r not in written}
                written = written | d
            if m == 'jal':
                callees = callees_of_site(f, pc, at_address)
                reads |= {r for r in reads_of(callees, analysis) if r not in written}
                written = written | (set.intersection(*[returned_in(analysis, c) for c in callees]) if callees else returned_in(analysis, None))
            elif m == 'jalr':
                reads |= {r for r in uses_defs_in(f, pc)[0] | (PARAMETER_REGISTERS if JALR_READS == 'params' else set()) if r not in written}
                written = written | returned_in(analysis, None)
            elif '$ra' in o or TABLES == 'stop':
                flows |= every - written
                continue
            # (A jump through a table goes on to its cases: cfg gives them as the delay slot's successors.)
            for s in succ.get(pc + 4, []):
                stack.append((s, frozenset(written)))
            continue
        # (uses_defs_at: the first half of an unaligned word load whose partner is next reads nothing. All the sampled "a caller's own
        #  instructions read a0 / a1 after the call" refusals were such a pair: func_800399D4's `lwl $a0, 0x8($a2); lwr $a0, 0xB($a2)`
        #  after calling func_80037430.)
        u, d = uses_defs_at(f, pc, succ)
        reads |= {r for r in u if r not in written}
        written = written | d
        for s in succ.get(pc, []):
            stack.append((s, frozenset(written)))
    return reads, flows



def callers_rely_exactly(functions, analysis, taken):
    """For every function, the registers something may read after calling it before writing them (see the notes above), and the
    same for calls through a pointer (after_indirect, which counts for every function in `taken`)."""
    at_address = functions_at(functions)
    every = set()
    for f in functions.values():
        for a in f['ins']:
            u, d = uses_defs_in(f, a)
            every |= u | d
    every -= {'sp', 'ra', 'zero'}
    if FLOW == 'result':
        every = {'v0', 'v1', 'f0', 'f1'}
    direct = {}
    flows = {}              # callee -> [(caller, registers going back to the caller's caller unchanged)]
    indirect_reads, indirect_flows = set(), []
    for name, f in functions.items():
        for a in sorted(f['ins']):
            if f['ins'][a][0] not in ('jal', 'jalr'):
                continue
            reads, out = after_call(f, a, analysis, at_address, every)
            callees = callees_of_site(f, a, at_address)
            if callees is None:
                indirect_reads |= reads
                indirect_flows.append((name, out))
                continue
            for c in callees:
                direct.setdefault(c, set()).update(reads)
                flows.setdefault(c, []).append((name, out))
    relied = {n: set(direct.get(n, ())) for n in functions}
    after_indirect = set(indirect_reads)
    changed = True
    while changed:
        changed = False
        more = set()
        for caller, out in indirect_flows:
            more |= out & relied[caller]
        if not more <= after_indirect:
            after_indirect |= more
            changed = True
        for n in functions:
            new = set(relied[n])
            if n in taken:
                new |= after_indirect
            for caller, out in flows.get(n, ()):
                new |= out & relied.get(caller, set())
            if new != relied[n]:
                relied[n] = new
                changed = True
    return relied, after_indirect


def kept_floats(f):
    """The callee-saved float registers (f20-f31) the function stores in its frame and loads back from the same place, as compiled
    code does to keep them for its caller. A converted function never changes them at all, so what it leaves there is the same.
    (Until 2026-11-07 only integer registers counted as kept, so writing f20 at all refused a function whose callers rely on it
    being kept -- 61 refusals named f20-f28. The tester checks the original really does give them back: fuzz_main.cpp.)"""
    stored, loaded = set(), set()
    for mn, ops in f['ins'].values():
        m = re.match(r'\$(f\d+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if not m or not 20 <= int(m.group(1)[1:]) <= 31:
            continue
        if mn in ('sdc1', 'swc1'):
            stored.add((mn == 'sdc1', m.group(1), int(m.group(2), 0)))
        elif mn in ('ldc1', 'lwc1'):
            loaded.add((mn == 'ldc1', m.group(1), int(m.group(2), 0)))
    kept = set()
    for double, reg, offset in stored & loaded:
        kept.add(reg)
        if double:
            kept.add('f%d' % (int(reg[1:]) + 1))
    return kept


def address_taken(functions):
    """Functions something could call through a pointer: their address is in the ROM as a word, or is built in code."""
    rom = open(os.path.join(PORT, 'nomercy.z64'), 'rb').read()
    words = {int.from_bytes(rom[i:i + 4], 'big') for i in range(0, len(rom) - 3, 4)}
    for f in functions.values():
        hi = {}
        for a in sorted(f['ins']):
            mn, ops = f['ins'][a]
            r = re.findall(r'\$(\w+)', ops)
            if mn == 'lui':
                hi[r[0]] = int(ops.split(',')[-1], 0) << 16
            elif mn == 'addiu' and len(r) == 2 and r[1] in hi:
                low = int(ops.split(',')[-1], 0)
                words.add((hi[r[1]] + low) & 0xFFFFFFFF)
    return {n for n in functions if re.match(r'func_[0-9A-F]{8}', n) and int(n[5:13], 16) in words}


PARAMETER_REGISTERS = {'a0', 'a1', 'a2', 'a3', 'f12', 'f14'}


CALLEE_SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp'} | {'f%d' % i for i in range(20, 32)}


def registers_set(f):
    """The registers a function really sets: not counting putting back a saved register from the stack at the end."""
    found = set()
    for mn, ops in f['ins'].values():
        if mn in ('lw', 'ld', 'lwc1', 'ldc1') and re.match(r'\$(\w+),\s*-?(?:0[xX])?[0-9A-Fa-f]+\(\$sp\)$', ops):
            r = re.match(r'\$(\w+)', ops).group(1)
            if r in CALLEE_SAVED:
                continue
        found |= uses_defs(mn, ops)[1]
    return found


def port_why(name, pieces, functions, separate, analysis, found):
    """Why the port's changes to a function's translated version keep it from being converted, or None (port_additions.py).

    Each piece's text is split into the port's additions and what the translator wrote. The additions must be of a kind this pass
    carries, at a place it can carry them to (only the entry piece's: a later piece of a function put together from pieces
    starts where a jump lands, not where the function is called); what's left is checked against the copy of the translation
    taken before the port's later changes (RecompiledFuncs.stock) as before, now with the translator's renumbered labels and
    redirected call lines set aside. A function with no such copy (the port's own MIPS, rw_*) is taken on what's left alone:
    nothing of the port's in it (no comment, no call it doesn't make with a jal, no declaration), so any change there is plain
    code on the N64's registers and memory, which the random-input tester compares. Records the additions in found[name]."""
    found[name] = []
    for piece in pieces:
        callees = set(separate[piece]['calls'].values())
        adds, rest, why = port_additions.find(piece, translated_text(piece), callees)
        if why:
            return why
        if adds and piece != name:
            return 'the port added code to a piece of a function put together from pieces'
        for a in adds:
            why = port_additions.check(a, functions[name], analysis, lambda c: result_registers(c, functions))
            if why:
                return why
        stock = translated_text(piece, 'RecompiledFuncs.stock')
        if stock is not None:
            _, stock_rest, why = port_additions.find(piece, stock, callees)
            if why or not port_additions.same_as_translated(rest, stock_rest):
                return 'the port changed its translated version'
        found[name].append((piece, adds))
    return None


def not_plain_calls(f, analysis, abi_ok=False, at_address=None):
    """Why this function's calls can't be converted yet, or None.

    A converted function only sets the parameter registers before a call. Any other register a callee reads is what the caller was
    given, as long as it's one callees must keep and this function never changes it. (The analysis counts a register as read when
    something further down saves and restores it, so taking it at its word would refuse most of the game.)

    abi_ok (abi.py, 2026-11-07): a callee reading other registers is fine -- the converted function keeps them in step in the
    context -- so only the other reasons count.

    at_address (functions_at): the functions at each address, for the calls into an overlay (see "Calls into overlays" below).
    Without it, as before 2026-11-07, any such call is refused. (abi.py's second round asks without it: its own assembly would
    name an overlay call's address as one function, so those stay refused there.)"""
    # A jal with no named callee is a call the translator resolves at run time (LOOKUP_FUNC), to whichever overlay's copy is loaded.
    # The conversion makes it the same way (lift_lookup), so it can reach any of the functions there: each is checked as a named
    # callee is, below. With nothing whole there to check, it's refused.
    looked_up = []
    for a, (mn, ops) in f['ins'].items():
        if mn == 'jal' and not (f['calls'].get(a + 4) or f['calls'].get(a)):
            if at_address is None:
                return 'calls an overlay function through the lookup table'
            there = at_address.get(overlay_target(ops), [])
            if not there:
                return 'calls an overlay address where no whole function starts'
            looked_up += there
    changed = None
    # (The same checks, in the same order, for a named callee as before; then for each function an overlay call can reach.)
    for a, callee in list(f['calls'].items()) + [(None, c) for c in looked_up]:
        # (A tail call -- merge.py build_group: a jump out with the frame released, to a function that returns for this one --
        #  is a call like any other here, made after the frame is released: TAIL_HELPER. Any other jump out is refused.)
        if a is not None and a not in f.get('tail', {}) and f['ins'].get(a - 4, ('',))[0] != 'jal' and f['ins'].get(a, ('',))[0] != 'jal':
            return 'jumps into another function'
        # (A system function the tester can stand in for is in the analysis since 2026-11-07, from with_system: only the others,
        # those system_reads.model() refuses, are still refused here.)
        if callee not in analysis:
            return 'calls a system function'
        if abi_ok:
            continue
        extra = set(analysis[callee]['use']) - PARAMETER_REGISTERS
        if extra - CALLEE_SAVED:
            return 'calls something that reads scratch registers'
        if extra:
            changed = registers_set(f) if changed is None else changed
            if extra & changed:
                return 'calls something that reads a register it changes'
    return None


# ---- Calls into overlays (2026-11-07, scratch/lift_ovl) ----
#
# A `jal` the translator couldn't name is a call into an overlay: code the game loads at run time into a range of addresses it
# shares with other overlays, so different overlays have different functions at the same address. The translator writes it as
# LOOKUP_FUNC(0x800D9AB0)(rdram, ctx): get_function (the runtime's overlays.cpp) finds whatever function the loaded overlay has
# there, and calls it on the registers as they are. A converted function makes the same call the same way: lift_call puts the
# parameters in their registers, then lift_lookup<0x800D9AB0u> (OVERLAY_HELPER) does that same lookup, at that moment, and calls
# what it finds. So whichever overlay is loaded decides the target, for the converted function exactly as for the original.
#
# Which one that will be can't be known here, so the call is declared for all of them: every whole function the translator has
# at the address (functions_at -- as analyse.py and callers_rely_exactly count such a call already). Its parameters are what any
# of them reads, its stack parameters the most any of them reads, and its result is read from a register only when every one of
# them always sets that register (result_registers). The random-input tester sees each of these calls: its get_function hands
# back a recording stand-in for each address (run_fuzz, fuzz_main.cpp), on both sides, so a wrong address, a missing call, or a
# different parameter is a difference.

OVERLAY_HELPER = [
    '// A call into an overlay, made as the translated original makes it (tools/lift/lift.py, "Calls into overlays"): whatever',
    '// function the overlay loaded at that moment has at the address, looked up by the runtime (LOOKUP_FUNC, get_function).',
    'template <uint32_t ADDRESS> void lift_lookup(uint8_t* rdram, recomp_context* ctx) { LOOKUP_FUNC(ADDRESS)(rdram, ctx); }',
    '']
# ---- Calls through a pointer (2026-11-11, trial 5) ----
#
# `jalr $v0`: the translator writes LOOKUP_FUNC(ctx->r2)(rdram, ctx) -- the runtime finds whatever function is at the address
# the register holds at that moment, and calls it on the registers as they are. A converted function makes the same call the
# same way: lift_pointer<2> (POINTER_HELPER) does that same lookup of the same register in the context. Nothing is guessed at
# translation time: not the target, and not its parameters. So that the context is what the original's registers are there,
# abi.py keeps in step every parameter register this function writes and the register holding the address (its plan: the
# call's entry says 'pointer'), and lift_call passes nothing. For m2c the jalr is written as a jal to lift_pointer_<register>,
# declared `s32 lift_pointer_v0(void)`: a result in v0 if the function reads one; anything else it reads after the call is
# m2c's "unset register" warning, as after any call. The tester sees each such call: its lookup hands back, on both sides, a
# stand-in that notes the address looked up and all six parameter registers (fuzz_main.cpp fuzz_overlay_unknown_address), so
# a wrong address, a missing call or a parameter register out of step is a difference.
POINTER_HELPER = [
    '// A call through a pointer, made as the translated original makes it (tools/lift/lift.py, "Calls through a pointer"): the',
    '// address in that register of the context, looked up by the runtime at that moment (LOOKUP_FUNC, get_function).',
    'template <int REGISTER> void lift_pointer(uint8_t* rdram, recomp_context* ctx) { LOOKUP_FUNC((&ctx->r0)[REGISTER])(rdram, ctx); }',
    '']
POINTER_REASON = 'calls through a pointer'
# (2026-11-12, trial 5; approved by the user 2026-11-11.) The first round converts a function with its registers in C
# variables, and m2c reads v1 after any call as that call's -- written `ctx->r3` by read_v1_after_calls. Where the callee
# leaves v1 alone, the original reads its OWN v1 there, which the converted function never put in the context. The plain
# functions' tester can't catch it: its stand-ins change v1 on every call. So a function with such a read
# (abi.reads_own_v1_after_call) is refused here and handed to abi.py's round, whose prepare() keeps v1 in step for it.
V1_REASON = 'reads its own v1 after a call that may leave it as it was'


def pointer_name(register):
    return 'lift_pointer_' + register


def pointer_sites(f):
    """A function's calls through a pointer: ({the jalr's address: the register}, None), or (None, why they can't be made this
    way): a jalr that isn't plainly `jalr $register`, one through a register no C value stands for, or one whose delay slot
    writes the register it jumps through (abi.py moves a delay slot that sets a mirror register before its call, which is
    only the same program when the call doesn't read it)."""
    sites = {}
    for a, (mn, ops) in f['ins'].items():
        if mn != 'jalr':
            continue
        m = re.fullmatch(r'\$(\w+)', ops.strip())
        if not m or m.group(1) not in abi.GPR or m.group(1) in abi.NEVER:
            return None, 'calls through a pointer in a way this pass refuses'
        delay = f['ins'].get(a + 4)
        if delay is None or m.group(1) in uses_defs(*delay)[1]:
            return None, "calls through a pointer whose delay slot changes the pointer's register"
        sites[a] = m.group(1)
    return sites, None


def unread_parameters(c, analysis):
    """(2026-11-12, agent "refused") A parameter register a callee never reads, which m2c had no value for: passed on as the
    context holds it.

    prototype() declares a function with stack parameters with all of a0-a3 before them, whether it reads them or not -- the
    stack words come after the four -- so m2c wants a value for each. func_800E0304 calls func_800E0D20, which reads a0 and
    five stack words, and m2c had nothing for a3 (it is what the calls before left): `M2C_ERROR(/* Read from unset register
    $a3 */)`, refused as a warning. The original passes whatever a3 holds there, and the callee never reads it (the analysis,
    which every check of a call relies on; the tester's stand-in notes only the registers it reads, the same analysis), so no
    value of it can change what happens. Only for a register at its own place (a3 as the fourth parameter) of a callee the
    analysis has, and that the analysis says doesn't read it. It is passed as the context holds it, not as some number:
    lift_call writes it back there, and in abi.py's round a register kept in step must stay the original's -- func_80116AC4's
    a2, which its callers read afterwards, came out 0 where the original left FFFFFDFF when a 0 was passed (39,844 of
    250,000).)"""
    # (Written as a loop, not re.sub: each call's arguments are found from its own text, in place.)
    pieces, at = [], 0
    for m in re.finditer(call_pattern(), c):
        if m.start() < at:
            continue
        callee = m.group(1)
        use = analysis.get(callee)
        if use is None:
            continue
        spans = argument_spans(c, m.end())[0][:4]
        for i, (s, e) in enumerate(spans):
            a = re.fullmatch(r'\s*M2C_ERROR\(/\* Read from unset register \$a([0-3]) \*/\)\s*', c[s:e])
            if a and int(a.group(1)) == i and 'a%d' % i not in use['use']:
                pieces += [c[at:s], (' ' if c[s] == ' ' else '') + '(s32) ctx->r%d' % (4 + i)]
                at = e
    return ''.join(pieces) + c[at:]


def call_pattern():
    """A call in m2c's C, group 1 the name: a named function, a port's rw_ helper, an overlay call (to_asm.py), or a system function handled (callee_pattern)."""
    return r'\b(' + callee_pattern() + r'|rw_\w+)\('


# ---- Tail calls (2026-11-08, scratch/lift_split) ----
#
# A function can end by jumping to another instead of calling it -- `return f(x);` compiled as `lw $ra; j f; addiu $sp, $sp, N`:
# its frame released and its saved registers put back first, so f runs with the stack where this function found it, and f's
# return goes straight to this function's caller. merge.py build_group finds them (a jump out at stack offset 0, to a function
# the game also calls on its own), to_asm.py writes each as a call to <f>__tail followed by a return, and convert_calls makes
# that `lift_call<R>(ctx, lift_tail(frame, ::f))(...)`: the frame released -- the stack pointer put back where it was on the way
# in -- before the parameters are put in place and f is called, as the original's jump does. (C++ works out the function being
# called before its arguments, so the release comes first; a frame slot read in an argument is still at the same address, as
# the memory is.) The frame's own end then sets the same stack pointer again. The saved registers need nothing: a converted
# function never changes them in ctx. f's results, in whatever registers it leaves them, are what this function's caller finds,
# for the converted function exactly as for the original, since nothing runs between f's return and this one's.
TAIL_HELPER = [
    '// A tail call (tools/lift/lift.py, "Tail calls"): the frame released first -- the stack pointer back where it was on the way',
    '// in -- as the original does before it jumps to the function, which then returns for it.',
    'template <class F> inline F lift_tail(const LiftFrame& frame, F function) { frame.ctx->r29 = (gpr)(int64_t)(s32)(frame.base + frame.size); return function; }',
    '']
TAIL_USE = 'lift_tail(frame, '


def overlay_target(ops):
    """The address a jal's operand names (0x800D9AB0), or None."""
    try:
        return int(ops.strip(), 0)
    except ValueError:
        return None


def overlay_sites(f, pieces):
    """The function's calls into an overlay: {the jal's address: the address it looks up}, or None and why not.

    Each must be in the translated text as the translator writes it, LOOKUP_FUNC(<the jal's own target>)(rdram, ctx) after the
    jal's delay slot -- not something the port changed to look up elsewhere. (port_why compares the text with the stock copy with
    call lines set aside, a LOOKUP_FUNC line among them: rw_run_overlay in place of LOOKUP_FUNC(0x800D9960) is a named call. This
    makes sure that what's left as a lookup still looks up the jal's own target.)"""
    sites = {a: overlay_target(ops) for a, (mn, ops) in f['ins'].items()
             if mn == 'jal' and not (f['calls'].get(a + 4) or f['calls'].get(a))}
    if not sites:
        return {}, None
    found = {}
    for piece in pieces:
        last = None
        for line in (translated_text(piece) or '').split('\n'):
            m = re.match(r'^\s*// 0x([0-9A-F]{8}): ', line)
            if m:
                last = int(m.group(1), 16)
                continue
            m = re.search(r'\bLOOKUP_FUNC\(([^)]*)\)', line)
            if m and last is not None:
                found[last] = m.group(1)
    for a, target in sites.items():
        # (The translator writes the call after the delay slot's comment, so it's noted against that instruction, a + 4.)
        text = found.get(a + 4, found.get(a))
        if target is None or text is None or overlay_target(text) != target:
            return None, 'the port changed an overlay call'
    return sites, None


def overlay_name(target):
    """An overlay call's name in the asm given m2c and in m2c's C (to_asm.py overlay_names)."""
    return 'lift_overlay_%08X' % target


def overlay_prototype(target, at_address, functions, analysis, caller_reads, may_leave=False):
    """An overlay call's declaration for m2c's context, from every whole function at its address (see above). Returns the
    declaration, its result type and the registers it reads; or None, None and why not.

    caller_reads: what this caller reads after its calls to that address (callers_rely_on's by_caller). The result is the
    register it reads, when every function there always sets it; reading one that some of them may leave alone is refused, since
    a converted caller would read what the C++ left there and not what the original did (prototype has the same reasoning for one
    function). When the caller reads neither: the one they all always set, or no result.

    may_leave (2026-11-12, agent "refused"): for abi.py's round, which keeps such a register in step. There a call's result is
    taken from the context too (lift_call hands back ctx->r2), and before a call that may leave the register as it was, the
    function's own value is stored there (abi.call_store_sites, C1): the context then holds the original's register after the
    call whichever function ran, as it does for a named callee that may not set it. So the result is declared in the register
    read, as for one that always sets it. Only one of v0 and f0."""
    there = at_address.get(target, [])
    use = set()
    for n in there:
        use |= set(analysis[n]['use'])
    # (The parameters as prototype() works them out from what's read: each register up to the last one read; f12 and f14 as
    #  floats only while everything before them is a float.)
    kinds = []
    for i, (whole, single) in enumerate((('a0', 'f12'), ('a1', 'f14'), ('a2', ''), ('a3', ''))):
        later = [r for r in ('a0', 'a1', 'a2', 'a3')[i + 1:] if r in use] + ([r for r in ('f14',) if r in use] if i == 0 else [])
        if whole in use:
            kinds.append('s32')
        elif single and single in use and all(k == 'f32' for k in kinds):
            kinds.append('f32')
        elif later:
            kinds.append('s32')
    stack = max(stack_parameters(functions[n]) for n in there)
    if stack:
        kinds += ['s32'] * (4 - len(kinds)) + ['s32'] * stack
    always = set.intersection(*[set(result_registers(n, functions)) for n in there])
    read = (caller_reads or set()) & {'v0', 'f0'}
    if read:
        if len(read) != 1 or not (read <= always or may_leave):
            return None, None, 'reads a result an overlay function may not set'
        result = 'f32' if read == {'f0'} else 's32'
    else:
        result = 'f32' if always == {'f0'} else 's32' if 'v0' in always else 'void'
    return '%s %s(%s);' % (result, overlay_name(target), ', '.join(kinds) or 'void'), result, use


def stack_parameters(f):
    """How many words of parameters a function reads from the stack, counted from the first (sp+0x10 on entry)."""
    frame = 0
    words = 0
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        m = SP_STEP.match(ops)
        if mn == 'addiu' and m and int(m.group(1), 0) < 0:
            frame = -int(m.group(1), 0)
            continue
        m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if m and mn[0] == 'l' and int(m.group(1), 0) >= frame + 0x10:
            words = max(words, (int(m.group(1), 0) - frame - 0x10) // 4 + 1)
    return words


def frame_size(f):
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        m = SP_STEP.match(ops)
        if mn == 'addiu' and m and int(m.group(1), 0) < 0:
            return -int(m.group(1), 0)
    return 0


def split_arguments(text, start):
    """From just after an opening bracket: the number of arguments, and where its closing bracket is."""
    depth = 0
    count = 0
    seen = False
    i = start
    while True:
        ch = text[i]
        if ch in '([':
            depth += 1
            seen = True
        elif ch in ')]':
            if depth == 0:
                return count + (1 if seen else 0), i
            depth -= 1
        elif ch == ',' and depth == 0:
            count += 1
        elif not ch.isspace():
            seen = True
        i += 1


WIDE_RESULT = {'s8': 's32', 's16': 's32', 'u8': 'u32', 'u16': 'u32', 'M2C_UNK8': 's32', 'M2C_UNK16': 's32', 'M2C_UNK': 's32', 'M2C_UNK32': 's32'}


LEFT_ARGUMENT = re.compile(r'\s*(?:\((?:' + TYPES + r')\)\s*)*M2C_LEFT\((a[0-3])\)\s*')


def argument_spans(text, start):
    """From just after a call's opening bracket: where each of its arguments starts and ends, and where its closing bracket is."""
    depth, spans, at, i = 0, [], start, start
    while True:
        ch = text[i]
        if ch in '([':
            depth += 1
        elif ch in ')]':
            if depth == 0:
                if text[at:i].strip():
                    spans.append((at, i))
                return spans, i
            depth -= 1
        elif ch == ',' and depth == 0:
            spans.append((at, i))
            at = i + 1
        i += 1


def pass_left_registers(c, name):
    """M2C_LEFT(a3), m2c's argument for a parameter register the C++ already holds as the original passes it (see m2c's
    translate.py LeftInRegister, which shows that for every way to the call), made into C++ (2026-11-07). Returns the new
    text, or None and why not.

    Such arguments at the end of a call are left out of it: lift_call then puts nothing in those registers, and the callee
    finds them as they were -- the original's bits exactly, even after a call in another of its arguments ran first, as on
    the N64. They stay in the text as M2C_LEFT(aN) for convert_calls to count (a call has to pass everything its function
    reads), and are taken out after it (drop_left_registers). One before an argument that is passed has to be given as a
    value: the register as it is, ((s32)ctx->r7) -- read where the arguments are worked out, so only where no other call is
    among them (C++ doesn't say which argument it works out first). Each must be in its own register's place: aN as
    argument N."""
    while True:
        found = list(re.finditer(r'\bM2C_LEFT\(', c))
        if not found:
            return c, None
        left = found[-1]
        call = None
        # (The call can be an overlay call or a system function's too, since 2026-11-07: call_pattern. m2c's check that the register
        #  holds what the original passes walks past a call not given it as "the same callee, run on the same register", which holds
        #  for the runtime's versions as well: they leave it as it was.)
        for k in reversed(list(re.finditer(call_pattern(), c[:left.start()]))):
            _, close = split_arguments(c, k.end())
            if close > left.start():
                call = k
                break
        if call is None:
            return None, 'passes on a register in a way this pass refuses'
        spans, close = argument_spans(c, call.end())
        texts = [c[s:e] for s, e in spans]
        kinds = [LEFT_ARGUMENT.fullmatch(t) for t in texts]
        if not any(kinds):
            return None, 'passes on a register in a way this pass refuses'
        for i, k in enumerate(kinds):
            if k and k.group(1) != 'a%d' % i:
                return None, 'passes on a register in a way this pass refuses'
        last_given = max([i for i, k in enumerate(kinds) if not k], default=-1)
        inside = c[call.end():close]
        out = []
        for i, (t, k) in enumerate(zip(texts, kinds)):
            if not k:
                out.append(t.strip())
            elif i > last_given:
                out.append('LIFT_LEFT_%s' % k.group(1))
            elif re.search(call_pattern(), inside):
                return None, 'passes on a register next to another call in its arguments'
            else:
                out.append('((s32)ctx->r%d)' % (4 + int(k.group(1)[1])))
        c = c[:call.end()] + ', '.join(out) + c[close:]


UNSET_ARGUMENT = re.compile(r'\s*(?:\((?:' + TYPES + r')\)\s*)*M2C_ERROR\(/\* Read from unset register \$(a[0-3]) \*/\)\s*')


def unread_parameters_left(c, analysis, overlays=None):
    """m2c's "Read from unset register $aN" as argument N of a call to a function that never reads aN, made M2C_LEFT(aN)
    (2026-11-12, modleft; named so by final081: agent "refused"'s unread_parameters, above, runs first and takes such an
    argument in its own place as ((s32) ctx->rN); this takes what that leaves -- overlay calls, tail calls). Returns the new text.

    A declaration (prototype) gives a function every parameter up to the last register it reads: func_8014175C reads a3 only,
    so it is s32 func_8014175C(s32, s32, s32, s32), and m2c wants a value for a0, a1 and a2 at each call. Where the caller
    sets none of them -- modrep_func_80111158's `lw $a3, ...; jal func_8014175C`, its a0-a2 whatever its own code or the calls
    before left -- m2c writes the error, and the function was refused for it. But the value there makes no difference: the
    callee never reads the register (the analysis's `use`, which only ever counts too many reads, never too few -- every
    refusal of a call relies on it the same way), and the tester's stand-in records only the registers in `use`. So such an
    argument is passed as the register is, as pass_left_registers passes one m2c shows the C++ already holds: at the end of the
    call it is left out (lift_call writes nothing there), before an argument that is passed it is the context's own value,
    ((s32)ctx->rN). Either way the context's register is as it was. Only that exact error, in its own register's place, for a
    callee whose reads are known; any other unset register is still refused."""
    overlays = overlays or {}
    out, at = [], 0
    for m in re.finditer(r'M2C_ERROR\(/\* Read from unset register \$(a[0-3]) \*/\)', c):
        call = None
        for k in reversed(list(re.finditer(call_pattern(), c[:m.start()]))):
            _, close = split_arguments(c, k.end())
            if close > m.start():
                call = k
                break
        if call is None:
            continue
        callee = call.group(1)
        if callee.endswith('__tail'):
            callee = callee[:-len('__tail')]
        if callee in overlays:
            use = set(overlays[callee])
        elif callee in analysis:
            use = set(analysis[callee].get('use', []))
        else:
            continue
        spans, _ = argument_spans(c, call.end())
        place = [i for i, (s, e) in enumerate(spans) if s <= m.start() < e]
        if not place or not UNSET_ARGUMENT.fullmatch(c[spans[place[0]][0]:spans[place[0]][1]]):
            continue
        i, register = place[0], m.group(1)
        # (its own register's place, and neither it nor the float register in that place -- f12 for a0, f14 for a1 -- read)
        if register != 'a%d' % i or register in use or {0: 'f12', 1: 'f14'}.get(i) in use:
            continue
        out += [c[at:m.start()], 'M2C_LEFT(%s)' % register]
        at = m.end()
    return ''.join(out) + c[at:]


def drop_left_registers(c):
    """The end-of-call arguments pass_left_registers kept for counting (LIFT_LEFT_aN), taken out of the calls."""
    c = re.sub(r',\s*LIFT_LEFT_a[0-3]\b', '', c)
    return re.sub(r'\(\s*LIFT_LEFT_a[0-3]\s*\)', '()', c)


def convert_calls(c, name, declared, analysis, overlays=None, functions=None, by_caller=None):
    """Turns m2c's calls into lift_call. Returns the new text and the functions it calls, or None and why not.

    Each call has to pass everything the function reads. m2c works out parameters from what was set just before the call, so one
    it missed would mean the converted function passes nothing where the original passed whatever was in that register.
    (A register passed on as it was left, M2C_LEFT, counts as passed: see pass_left_registers.)

    overlays: {overlay_name: the registers any function there reads}, the function's calls into an overlay. Each becomes
    lift_call<R>(ctx, lift_lookup<address>)(...): the same lookup the translated original makes (see "Calls into overlays").
    They aren't among the functions returned: there's no one function there to declare."""
    overlays = overlays or {}
    c, why = pass_left_registers(c, name)
    if c is None:
        return None, why
    callees = set()
    out = []
    at = 0
    first = re.search(r'\b' + re.escape(name) + r'\(', c)
    # (A call to a system function is turned into lift_call the same way, to the runtime's own version that the translated original
    # calls: call_pattern, 2026-11-07. What it reads is in the analysis by then, from with_system.)
    for m in re.finditer(call_pattern(), c):
        if first and m.start() == first.start():
            continue
        callee = m.group(1)
        tail = callee.endswith('__tail')        # (a tail call's stub: "Tail calls")
        if tail:
            callee = callee[:-len('__tail')]
        if callee.startswith('lift_overlay_') and callee not in overlays:
            return None, 'the port changed an overlay call'       # (only from the asm, so never: but not a guess)
        if callee.startswith('lift_pointer_'):
            # (2026-11-11, trial 5, "Calls through a pointer": no parameters passed -- the registers are in step in the context.)
            count, _ = split_arguments(c, m.end())
            if count:
                return None, 'a call through a pointer was given parameters'
            out += [c[at:m.start()], 'lift_call<%s>(ctx, lift_pointer<%d>)(' % (declared.get(callee, 's32'), abi.GPR.index(callee[len('lift_pointer_'):]))]
            at = m.end()
            continue
        use = set(overlays[callee]) if callee in overlays else set(analysis.get(callee, {}).get('use', []))
        halves = (use & {'f13', 'f15'}) & double_parameter_halves((functions or {}).get(callee)) if callee not in overlays else set()
        if halves and halves == use & {'f13', 'f15'} and double_kinds(use, halves) is not None:
            # (2026-11-12, agent "refused2") Its double parameters, declared f64 (prototype, double_kinds): the call has to pass
            # every parameter of that declaration, and lift_call puts each double in its pair of float registers.
            count, _ = split_arguments(c, m.end())
            if count < len(double_kinds(use, halves)):
                return None, 'a call passes fewer parameters than the function reads'
        elif use & {'f13', 'f15'}:
            # (2026-11-12, agent "refused") A double parameter, f12:f13 or f14:f15 (func_800326EC, func_8002C7AC: libm's
            # ldexp and the like). prototype() declares it f32 and m2c passes `(f32) x` there, a float made from the double,
            # where the original hands on the double's two words: func_80032314 and func_8002C58C, converted once nothing
            # else stopped them, were DIFFERENT so (calls@14:converted=437F0000,original=0). Neither m2c's declaration nor
            # lift_call has a double parameter.
            return None, 'passes a double parameter, which its declaration takes as a float'
        needed = 0
        for i, (whole, single) in enumerate((('a0', 'f12'), ('a1', 'f14'), ('a2', ''), ('a3', ''))):
            if whole in use or single in use:
                needed = i + 1
        if halves and halves == use & {'f13', 'f15'}:
            needed = 0          # (counted by the declaration's parameters, above)
        count, _ = split_arguments(c, m.end())
        if count < needed:
            return None, 'a call passes fewer parameters than the function reads'
        if tail and count > 4:
            # (Its parameters on the stack go above the released frame, in this function's caller's: not handled.)
            return None, 'a tail call passes parameters on the stack'
        ret = declared.get(callee, 's32')
        # (A narrow result type stays: m2c relies on it to cut the value down where the original did, func_80004B7C. If it's wrong
        # the other way, the random-input tester sees it.)
        ret = {'M2C_UNK': 's32', 'M2C_UNK32': 's32', 'M2C_UNK8': 's32', 'M2C_UNK16': 's32'}.get(ret, ret)
        if ret in ('s64', 'u64', 'M2C_UNK64') or '?' in ret or (ret == 'f64' and not double_result(callee, functions or {})):
            return None, 'a call has a 64-bit result'       # (a double, f0:f1, only as prototype declares one: 2026-11-12, "refused2")
        if ret == 'f32' and 'f1' in (by_caller or {}).get((name, callee), set()):
            # (2026-11-12, agent "refused2") The caller reads f1 after it as well -- the pair f0:f1, a double -- where the
            # declaration has a float: m2c's C would make a double from the float in f0 (func_800EBD2C's `c.lt.d $f0, $f2`
            # after rw_contact_cap, which double_result can't vouch for). Only that function, of all that convert.
            return None, 'reads a double result its declaration takes as a float'
        if callee in overlays:
            out += [c[at:m.start()], 'lift_call<%s>(ctx, lift_lookup<0x%su>)(' % (ret, callee[len('lift_overlay_'):])]
            at = m.end()
            continue
        callees.add(callee)
        out += [c[at:m.start()], ('lift_call<%s>(ctx, ' + TAIL_USE + '::%s))(' if tail else 'lift_call<%s>(ctx, ::%s)(') % (ret, callee)]
        at = m.end()
    out.append(c[at:])
    text = drop_left_registers(''.join(out))
    if re.search(r'\bM2C_LEFT\(|\bLIFT_LEFT_', text):
        return None, 'passes on a register in a way this pass refuses'
    return text, sorted(callees)


GPR_FIELD = {'s0': 'r16', 's1': 'r17', 's2': 'r18', 's3': 'r19', 's4': 'r20', 's5': 'r21', 's6': 'r22', 's7': 'r23', 'gp': 'r28', 'fp': 'r30',
             'ra': 'r31'}


def saved_slots(f):
    """Where the original saves registers in its frame: (offset, register), integer ones (sw) and doubles (sdc1 of f20-f30).

    Only the prologue's saves (2026-11-10): the first store of each such register to the frame, made while the register still
    holds what it came in with -- no instruction at a lower address has written it -- in code that can run, at an offset
    inside the frame. Until then this was every `sw $sN, off($sp)` in the function, and add_context_and_frame wrote the
    register's value on the way in to each of those slots at the start. But most of those stores are not saves: a callee's
    fifth parameter (`sw $s5, 0x10($sp)`), a local kept across a call (`sw $s1, 0x3C($sp)`), each of a value the function
    has worked out by then. The converted function then had rubbish from its caller's registers in its locals and outgoing
    parameters from the first instruction on, where the original has whatever was there. A callee given the address of a
    local beside one sees the difference (the tester's `calls`: func_8011E2B4, 214,076 of 450,000 tests; func_8011F60C;
    func_801337E0; 15 of the 43 functions DIFFERENT in 0.73.0 had such slots). And for a mod's copy that is a short wrapper
    with the game's old body left after its return (modrep_func_800EDCF4: nine instructions, then code that never runs),
    the old body's saves were written above the wrapper's 0x18-byte frame, into the caller's (memory-count@80600010 on
    every test)."""
    addrs, succ = cfg(f)
    reachable = set()
    work = addrs[:1]
    while work:
        a = work.pop()
        if a not in reachable:
            reachable.add(a)
            work.extend(succ.get(a, ()))
    frame = frame_size(f)
    found = []
    written = set()
    saved = set()
    for a in addrs:
        mn, ops = f['ins'][a]
        m = re.match(r'\$(\w+),\s*((?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if m and a in reachable and m.group(1) not in written and m.group(1) not in saved:
            size = 4 if mn == 'sw' and m.group(1) in GPR_FIELD else 8 if mn == 'sdc1' and re.fullmatch(r'f(2[02468]|30)', m.group(1)) else 0
            if size and int(m.group(2), 0) + size <= frame:
                found.append((int(m.group(2), 0), m.group(1)))
                saved.add(m.group(1))
        written |= uses_defs(mn, ops)[1]
        if mn in ('jal', 'jalr', 'bal', 'bgezal', 'bltzal'):
            written.add('ra')       # (uses_defs leaves a call's ra out: after one, ra is not what came in)
    return found


def frame_float_reads_into_ints(cpp):
    """A word read from the frame with lwc1 into a variable m2c has as an integer (2026-11-09): `temp_f0 = Ref<f32>(frame.at(0x64));` with
    temp_f0 an s32, which C++ converts as a number. func_80136240 reads three floats this way and stores them with swc1, on a path
    that joins one storing integers (`sw`), so m2c makes the variables integers. The instruction only moves the bits, so the read is
    written as the integer it's going into."""
    ints = set(re.findall(r'^\s*(?:s32|u32|M2C_UNK32) (\w+);', cpp, re.M))
    def one(m):
        return '%s = Ref<s32>(frame.at(%s));' % (m.group(1), m.group(2)) if m.group(1) in ints else m.group(0)
    cpp = re.sub(r'\b(\w+) = Ref<f32>\(frame\.at\((\w+)\)\);', one, cpp)
    # (2026-11-10) And a float read from anywhere else, the same way: a fixed address, `temp_f2 = *(P<f32> )0x8016CB74;`, or a
    # field, `temp_f2 = M2C_FIELD(p, P<f32> , 0x10);`, the whole of an assignment to an integer variable. func_801404F4 loads
    # 1.0f with lwc1 and passes it on in a stack slot (`swc1 $f2, 0x14($sp)`), which makes temp_f2 an s32 for m2c; C++ then
    # converted the number, and the callee got 1 where the original gives it 0x3F800000 (calls@12, every one of 250,000
    # tests). Only the place's type changes: the same 32 bits are read.
    def fixed(m):
        return '%s%s = *(P<s32> )%s;' % (m.group(1), m.group(2), m.group(3)) if m.group(2) in ints else m.group(0)
    cpp = re.sub(r'^([ \t]*)(\w+) = \*\(P<f32> \)(0x[0-9A-Fa-f]+);$', fixed, cpp, flags=re.M)
    def field(m):
        return '%s%s = M2C_FIELD(%s, P<s32> , %s);' % (m.group(1), m.group(2), m.group(3), m.group(4)) if m.group(2) in ints else m.group(0)
    cpp = re.sub(r'^([ \t]*)(\w+) = M2C_FIELD\(([^=;]*), P<f32> , (-?\w+)\);$', field, cpp, flags=re.M)
    # (2026-11-12, agent "refused2") And the other way: a frame word read with lw into a variable m2c has as a float, the whole
    # of an assignment, `temp_v1_6 = Ref<s32>(frame.at(0xC4));` with temp_v1_6 an f32 -- func_8012069C moves a vector's words
    # with lw and sw into spB4, which it reads back with lwc1, so m2c made the variables floats, and C++ converted the number:
    # the callee got 1.0f where the original hands on 1 (calls@A1, 6 of 238,513 tests). The read is written as the float it's
    # going into: the same 32 bits. Only a frame read, as the first case above: it is the one seen.
    floats = set(re.findall(r'^\s*f32 (\w+);', cpp, re.M))
    def frame_int(m):
        return '%s = Ref<f32>(frame.at(%s));' % (m.group(1), m.group(2)) if m.group(1) in floats else m.group(0)
    return re.sub(r'\b(\w+) = Ref<(?:s32|u32)>\(frame\.at\((\w+)\)\);', frame_int, cpp)


# ---- C++ every compiler takes (the Linux proof, 2026-11-13; into 0.81 by agent "linuxfix") ----
# clang takes two things m2c writes that GCC (11, 12) and the C++20 standard refuse; lifted.cpp had 24 such spots in 7 functions:
#   - a label with nothing after it but the closing brace (`case 7:` / `block_75:` then `}`): a label must label a statement
#     until C++23. An empty statement after it changes nothing a compiler may do.
#   - a case value outside the switch's 32-bit type (`case -4294933220:` on an s32, func_8012D918: m2c's sum of a 16-bit
#     compare constant, not brought back into range). clang and `g++ -w` both read it as the value's low 32 bits; written
#     that way here, so it's what the text says. (Checked: func_8012D918 reaches all 226 instructions on both, no difference.)
# Applied last, to the text as it is stored and tested (after tidy.py), so every other function's text is as it was.
LABEL_BEFORE_BRACE = re.compile(r'(?m)^([ \t]*(?:case [^:\n]+|default|[A-Za-z_]\w*):)[ \t]*(?=\n[ \t]*\})')
CASE_VALUE = re.compile(r'(?m)^([ \t]*)case (-?(?:0[xX][0-9A-Fa-f]+|[1-9]\d*|0)):')


def every_compilers_cpp(cpp):
    def case_value(m):
        v = int(m.group(2), 0)
        if -2 ** 31 <= v < 2 ** 32:
            return m.group(0)
        w = v & 0xFFFFFFFF
        return '%scase %d:' % (m.group(1), w - 2 ** 32 if w >= 2 ** 31 else w)
    # (2026-11-13, the ARM64 proof) A float clamp at zero, `if (x < 0.0f) { x = 0.0f; }`, keeps its branch: LIFT_BRANCH
    # (lift_mem.h). clang for ARM64 makes the select `x < 0 ? 0 : x` an fmax instruction, which gives +0.0 for x = -0.0 where
    # the N64's c.lt.s / bc1f keeps -0.0 (and x86's maxss, and GCC). Three candidates have one: func_800FD6DC (1,015
    # differences in 200,000 tests on ARM64, none with this), func_8010D9A4, modrep_func_8001FFA8. A clamp at any other
    # constant is exact as fmax / fmin and is left alone.
    cpp = re.sub(r'if \(((?:\(f64\) )?\w+ < 0\.0f?)\) \{(\n\s*\w+ = 0\.0f;\n\s*\})', r'if (LIFT_BRANCH(\1)) {\2', cpp)
    return CASE_VALUE.sub(case_value, LABEL_BEFORE_BRACE.sub(r'\1 ;', cpp))


def stores_of_the_other_kind(cpp):
    """A 32-bit store whose place is printed as the other kind than what it stores (2026-11-09): `*(P<f32> )0x800698D0 = arg1;` with
    arg1 an s32 parameter, `M2C_FIELD(p, P<s32> , 0) = temp_f;` with temp_f an f32. C++ would convert the number; the N64's sw or
    swc1 moves the bits. The place's type is written to match what is stored, which moves the same 32 bits."""
    head = cpp.split('{', 1)[0]
    ints = set(re.findall(r'\b(?:s32|u32|M2C_UNK32) (\w+)\s*[,)]', head)) | set(re.findall(r'^\s*(?:s32|u32|M2C_UNK32) (\w+);', cpp, re.M))
    floats = set(re.findall(r'\bf32 (\w+)\s*[,)]', head)) | set(re.findall(r'^\s*f32 (\w+);', cpp, re.M))
    pfloats = set(re.findall(r'\bP<f32> (\w+)\s*[,)]', head)) | set(re.findall(r'^\s*P<f32> (\w+);', cpp, re.M))
    pints = set(re.findall(r'\bP<(?:s32|u32)> (\w+)\s*[,)]', head)) | set(re.findall(r'^\s*P<(?:s32|u32)> (\w+);', cpp, re.M))
    def kind(rhs):
        if rhs.startswith('*'):
            return 'f' if rhs[1:] in pfloats else 'i' if rhs[1:] in pints else None
        return 'f' if rhs in floats else 'i' if rhs in ints else None
    def one(m):
        place, rhs = m.group(2), m.group(3)
        k = kind(rhs)
        if k is None:
            return m.group(0)
        typed = list(re.finditer(r'P<(f32|s32|u32)>', place))[-1]
        if (typed.group(1) == 'f32') == (k == 'f'):
            return m.group(0)
        return m.group(1) + place[:typed.start()] + ('P<f32>' if k == 'f' else 'P<s32>') + place[typed.end():] + ' = ' + rhs + ';'
    return re.sub(r'^([ \t]*)(\*\(P<(?:f32|s32|u32)> \)0x[0-9A-Fa-f]+|M2C_FIELD\([^=;]*, P<(?:f32|s32|u32)> , \w+\)) = (\*?\w+);$', one, cpp, flags=re.M)


def home_spills(f):
    """The parameter registers the original stores into their own places in its caller's frame, as it came in (2026-11-12,
    modleft): [(offset from its stack pointer, register number)]. Only in a function that hands the address of a place in its
    frame to something (an `addiu $x, $sp, N`, x not sp): where nothing is given such an address, nothing can see those places.

    The calling rules give each function the 16 bytes at its stack pointer on the way in, one word for each of a0-a3, and the
    compiler stores a parameter there to keep it across a call (modrep_func_8012F9CC: `sw $a1, 0x3C($sp); sw $a2, 0x40($sp)`
    with a 0x38 frame, read back after its first call). m2c takes such a store for the parameter itself and leaves it out, so
    the converted function never wrote those words. The tester does not compare them -- they are the function's own, as its frame
    is (fuzz_main.cpp own_memory_end) -- but a stand-in given the address of a local notes the 8 words from it, and in
    modrep_func_8012F9CC those run on into the word a1 is stored in (calls@10: what was there, 0x80100614, for a1's -115; 58,405
    of 61,943 tests). So the converted function writes them too, as the original does, with the same value: each store counted
    here is in the straight run of instructions from the function's start, before any branch, jump or call, and nothing before
    it writes the register, so what it stores is the register as it came in, which the converted function has in ctx. Nothing
    reads the place before the store (no load from it in that run), so writing it at the start is the same."""
    frame = frame_size(f)
    if not frame or not any(mn in ('addiu', 'addu') and re.match(r'\$(?!sp\b)\w+,\s*\$sp\b', ops) for mn, ops in f['ins'].values()):
        return []
    found, written, read = [], set(), set()
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        if mn in BRANCH or mn in ('j', 'jr', 'jal', 'jalr', 'b', 'bal', 'eret', 'syscall', 'break'):
            break
        m = re.match(r'\$(\w+),\s*((?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if m and mn in ('lw', 'lh', 'lhu', 'lb', 'lbu', 'lwc1', 'ldc1', 'lwl', 'lwr', 'ld'):
            read.add(int(m.group(2), 0) & ~3)
        if m and mn == 'sw' and m.group(1) in ('a0', 'a1', 'a2', 'a3') and m.group(1) not in written:
            n = int(m.group(1)[1])
            offset = int(m.group(2), 0)
            if offset == frame + 4 * n and offset not in read:
                found.append((offset, 4 + n))
        written |= uses_defs(mn, ops)[1]
    return found


def add_context_and_frame(cpp, name, frame, saves=(), homes=()):
    """A function that makes calls needs the N64 registers, and a real stack frame. Its locals (spXX) move into that frame, at the same
    offsets as the original's, so a local whose address is passed on, or a parameter passed on the stack, is where the callee looks.
    The registers the original saves there are written there too, so the frame is the same bytes: they're the values on the way in,
    since a converted function never changes them."""
    def signature(m):
        params = m.group(2).strip()
        keep = ''.join(('\n    frame.keep(0x%X, ctx->%s);' % (offset, GPR_FIELD[reg])) if reg in GPR_FIELD else
                       ('\n    frame.keep_double(0x%X, ctx->%s.u64);' % (offset, reg)) for offset, reg in saves)
        # (And the parameters it stores in its caller's frame as they came in: home_spills, 2026-11-12.)
        keep += ''.join('\n    lift_write32(frame.at(0x%X), (u32)ctx->r%d);   // (as the original stores it there: home_spills)' % (offset, n)
                        for offset, n in homes)
        return m.group(1) + '(recomp_context* ctx' + ('' if params in ('', 'void') else ', ' + params) + ') {\n    LiftFrame frame(ctx, 0x%X);' % frame + keep
    cpp = re.sub(r'^(.*\b' + re.escape(name) + r')\((.*)\) \{$', signature, cpp, count=1, flags=re.M)
    def local(m):
        offset = int(m.group(3), 16)
        if m.group(4):
            return '%sP<%s> sp%s(frame.at(0x%X));' % (m.group(1), m.group(2).strip(), m.group(3), offset)
        return '%sRef<%s> sp%s(frame.at(0x%X));' % (m.group(1), m.group(2).strip(), m.group(3), offset)
    # Only declarations: a type (a word, or a game pointer P<...>) and the name. (A statement that ends in a local, such as
    # `*arg0 = sp10;`, was taken for one until 2026-11-05.)
    # (And LiftWord<T>, exact_spill_slots' word moved both as an integer and as a float, 2026-11-12.)
    return re.sub(r'^(\s+)((?!return\b)(?:P<[^=;()]*>|LiftWord<\w+>|\w+))\s+sp([0-9A-Fa-f]+)(\[\w*\])?;$', local, cpp, flags=re.M)


# (`u16 sp10[];`, 2026-11-11: an array m2c declares without a length -- one named only through its elements, see m2c's
#  if_statements.py -- is a local like `u16 sp10[4];`: P<u16> at its place in the frame. The pattern took only a length given.)
LOCAL_DECLARATION = re.compile(r'^(\s+)((?!return\b)(?:P<[^=;()]*>|LiftWord<\w+>|\w+))\s+sp([0-9A-Fa-f]+)(\[\w*\])?;$', re.M)
FRAME_NAME = re.compile(r'(?<![\w.>])(?:unk)?sp([0-9A-F]+)\b')
BARE_SP = re.compile(r'(?<![\w.>])sp(?!\w)')
SAVED_REG = re.compile(r'\bsaved_reg_(\w+)\b')
SLOT_LOADS = {'lb': 's8', 'lbu': 'u8', 'lh': 's16', 'lhu': 'u16', 'lw': 's32', 'lwc1': 'f32'}
SLOT_STORES = {'sb': 'u8', 'sh': 'u16', 'sw': 's32', 'swc1': 'f32'}


def frame_needs(cpp):
    """What m2c uses in a function without declaring it (2026-11-07; 59 of the big functions refused for not compiling had one):

    - frame slots by name, spXX or unkspXX, that it never declared: it declares a local it takes the address of (sp10) as one
      thing, and then names a read inside it on its own (`lw $a0, 0x18($sp)` after `addiu $a0, $sp, 0x10` was passed on, as
      sp18), or a slot it gave no field (unksp2E);
    - the frame itself, `sp`: an address worked out from the stack pointer (`addu $t0, $sp, $t1`, a local array indexed);
    - saved_reg_s1 and the like: a callee-saved register's value on the way in, read on some path before the function set it.
    Returns (the undeclared slot names, whether the bare `sp` is used, the saved registers used)."""
    declared = {'sp' + m.group(3) for m in LOCAL_DECLARATION.finditer(cpp)}
    undeclared = {m.group(0) for m in FRAME_NAME.finditer(cpp)} - declared
    return undeclared, bool(BARE_SP.search(cpp)), set(SAVED_REG.findall(cpp))


def name_frame_slots(cpp, f, undeclared):
    """Gives the frame slots m2c named without declaring them, and the frame itself, their place in the frame (after
    add_context_and_frame, which puts the declared ones there): spXX becomes Ref<T>(frame.at(0xXX)), the memory the instructions
    use, at the type they use it as, and `sp` the frame's start. m2c's sp is the stack pointer after the function made its frame
    (it reads `addiu $sp, $sp, -N` as bookkeeping, and numbers its slots from there), which is frame.at(0).

    The type is what the instructions at that offset of the stack pointer load (signed or not as lb/lbu say), or with no load,
    what they store. Two different loads there are refused: which one the name stands for isn't known. Returns (cpp, None), or
    (None, why)."""
    loads, stores, doubles = {}, {}, {}
    for mn, ops in f['ins'].values():
        m = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if not m:
            continue
        offset = int(m.group(1), 0)
        if mn in SLOT_LOADS:
            loads.setdefault(offset, set()).add(SLOT_LOADS[mn])
        elif mn in SLOT_STORES:
            stores.setdefault(offset, set()).add(SLOT_STORES[mn])
        elif mn in ('ldc1', 'sdc1'):
            doubles.setdefault(offset, set()).add('f64')
    types = {}
    for name in undeclared:
        offset = int(FRAME_NAME.match(name).group(1), 16)
        # (A slot only ldc1 or sdc1 use, 2026-11-12, agent "refused": a double, Ref<f64>. func_800DB2A4 saves f26:f27 with sdc1
        #  where m2c doesn't see a save -- the function reads f26 before -- and names the slot unkspC0. Only where no other
        #  instruction gives the slot a type, which was refused here before: a slot that has one keeps it.)
        kinds = loads.get(offset) or stores.get(offset) or doubles.get(offset)
        if not kinds or len(kinds) != 1:
            return None, 'names a frame slot it never declared, %s' % ('used at two sizes' if kinds else 'and no instruction there gives its type')
        types[name] = 'Ref<%s>(frame.at(0x%X))' % (next(iter(kinds)), offset)
    cpp = FRAME_NAME.sub(lambda m: types.get(m.group(0), m.group(0)), cpp)
    return BARE_SP.sub('P<void>(frame.at(0))', cpp), None


# (The address-of `&arg2`, as m2c writes it: no space after it, and nothing before it that it could be the and of: `x & arg1`.)
PARAMETER_ADDRESS = re.compile(r'(?<![\w)\]])&arg(\d+)\b')
WORD_PARAMETER_TYPES = re.compile(r'(?:s32|u32|f32|M2C_UNK|M2C_UNK32|P<.*>)')


def parameter_slots(cpp, f, args, sources):
    """A parameter whose address the function takes, `&arg2` (2026-11-12, agent "refused"): its home slot in game memory.

    The N64's calling rules give each of the first four parameters a word in the caller's frame, just above the callee's
    (sp + 4N on the way in). A function that needs a parameter in memory -- vsprintf's list of extra arguments (func_80036AD0
    stores a1-a3 there and passes &a2's slot), or one that hands a callee pointers to its parameters to change (func_80034B50,
    `addiu $a0, $sp, 0x50` with a2 stored at 0x50) -- stores the register there and from then on uses the slot. m2c reads
    all of that as the parameter itself: the store is left out, the slot's later loads and stores are `arg2`, its address
    `&arg2`, the address of a C++ local -- which does not compile (a PC pointer cast to s32), and if it did would be the
    wrong memory: the callee changes the game's word, and the function reads it back.

    So every use of such a parameter becomes the slot, Ref<T>(frame.at(frame size + 4N)), and the function first stores the
    value it came in with there, as the original's own `sw $aN` does. That store must be one of the function's first
    instructions, before anything has branched, called or written the register (it is taken to be the very first thing the
    function does); and the parameter a whole word, so the slot holds exactly it. A stack parameter (N >= 4) is in its slot
    already, written there by the caller: its uses become the slot, with nothing stored. Returns (cpp, None), or (None, why).
    """
    wanted = sorted({int(m.group(1)) for m in PARAMETER_ADDRESS.finditer(cpp)})
    if not wanted:
        return cpp, None
    # (Only the address itself, handed on. Arithmetic on it, `&arg33 + var_a2` or `(&arg33)[i]`, walks the caller's frame as
    #  an array -- rw_80119C28, hand-written, came out DIFFERENT so (memory-count, 66,316 of 237,517) -- and m2c's element or
    #  byte steps there are not known to be the instructions'.)
    if re.search(r'\(&arg\d+\)\s*\[|(?<![\w)\]])&arg\d+\s*[-+]', cpp):
        return None, "does arithmetic on a parameter's address"
    size = frame_size(f)
    # (The first straight run of instructions: until the first branch or jump, and its delay slot.)
    # stored: {offset in the frame: the register stored there unchanged since the function began}. A store before the frame is
    # made (`sw $a1, 0x4($sp)`) is at the frame's size more.
    moved, written, stored, in_delay_slot = False, set(), {}, False
    for a in sorted(f['ins']):
        mn, ops = f['ins'][a]
        if in_delay_slot == 'likely':
            break               # (a branch-likely's delay slot runs only when it is taken)
        m = re.match(r'\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)$', ops)
        if mn in ('sw', 'swc1') and m and m.group(1) not in written:
            stored.setdefault(int(m.group(2), 0) + (0 if moved else size), m.group(1))
        if mn == 'addiu' and SP_STEP.match(ops):
            if moved or int(SP_STEP.match(ops).group(1), 0) != -size:
                break
            moved = True
        written |= uses_defs(mn, ops)[1]
        if in_delay_slot:
            break               # (the delay slot of the first branch or jump was the last of the run)
        in_delay_slot = 'likely' if mn in BRANCH and mn.endswith('l') and mn not in ('bltzal', 'bgezal') else mn in mips.JUMPS
    names = {n: (t, s) for (t, n), s in zip(args, sources)}
    for n in wanted:
        t, source = names.get('arg%d' % n, (None, None))
        if t is None:
            return None, 'takes the address of a parameter it is not given'
        if n < 4:
            register = 'f%d' % (12 + 2 * n) if source.startswith('ctx->f') else 'a%d' % n
            if stored.get(size + 4 * n) != register:
                return None, "takes the address of a parameter it doesn't store in its home slot at its start"
    # Every home slot the function stores at its start, not only the ones whose address it takes: a callee given one address
    # may read on from it -- vsprintf's list goes on from a2's slot to a3's, and func_80036AD0 stores a1-a3 -- so each holds
    # what the original's own store puts there, and each such parameter is its slot from then on. (One m2c left out of the
    # signature, seeing no other use of it, is stored from its register as it came in: the context's, which nothing has
    # changed yet.)
    slots = set(wanted)
    stores = []
    for n in range(4):
        t, source = names.get('arg%d' % n, (None, None))
        register = 'f%d' % (12 + 2 * n) if source and source.startswith('ctx->f') else 'a%d' % n
        if stored.get(size + 4 * n) != register:
            continue
        if t is None:
            stores.append('    lift_write32(frame.at(0x%X), (u32)ctx->r%d);' % (size + 4 * n, 4 + n))
            continue
        slots.add(n)
        stores.append('    Ref<%s>(frame.at(0x%X)) = arg%d;' % (t, size + 4 * n, n))
    for n in slots:
        if not WORD_PARAMETER_TYPES.fullmatch(names['arg%d' % n][0]):
            return None, 'takes the address of a parameter, and one it keeps in memory is smaller than a word'
    sig = SIGNATURE.search(cpp)
    head, body = cpp[:sig.end()], cpp[sig.end():]
    # (After the frame and its saves: the frame is made first. The function's own first lines follow.)
    frame = re.match(r'\n    LiftFrame frame\(ctx, 0x[0-9A-F]+\);(?:\n    frame\.keep(?:_double)?\(0x[0-9A-F]+, ctx->[\w.]+\);)*', body)
    if not frame:
        return None, "takes the address of a parameter, and its frame couldn't be found"
    rest = body[frame.end():]
    for n in sorted(slots):
        rest = re.sub(r'(?<![\w.>])arg%d\b' % n, 'Ref<%s>(frame.at(0x%X))' % (names['arg%d' % n][0], size + 4 * n), rest)
    return head + body[:frame.end()] + ''.join('\n' + s for s in stores) + rest, None


def entry_values(cpp, abi_plan=None):
    """saved_reg_XX, a callee-saved register's value on the way in (m2c's name for what the register held before the function
    set it), becomes that register: ((s32)ctx->r17) for s1. The converted function never writes those registers in ctx, and what
    it calls keeps them, so they still hold the value they came in with. A saved float register's: see below (2026-11-12; until
    then refused)."""
    # (A saved float register's, 2026-11-12, agent "refused": f20-f31, its 32 bits as a float -- an even one is the low word of
    #  its pair in the context, an odd one the high word of the even one's, as the translated code keeps them (the N64's
    #  32-bit float mode, ctx->f_odd). func_800DB2A4 stores f26:f27 whole with sdc1 where m2c sees no save, and the slot's
    #  double is GLUE_F64(saved_reg_f27, var_f26). Not one abi.py keeps in step: the context's copy changes as it runs.)
    floats = {}
    for reg in set(SAVED_REG.findall(cpp)):
        m = re.fullmatch(r'f(\d+)', reg)
        if m and 20 <= int(m.group(1)) <= 31:
            even = int(m.group(1)) & ~1
            if {'f%d' % even, 'f%d' % (even + 1)} & set((abi_plan or {}).get('mirror', ())):
                return None, "reads a saved register's value on the way in that this pass doesn't handle"
            floats[reg] = '__builtin_bit_cast(f32, ctx->f%d.%s)' % (even, 'u32h' if int(m.group(1)) & 1 else 'u32l')
        elif reg not in GPR_FIELD:
            return None, "reads a saved register's value on the way in that this pass doesn't handle"
    return SAVED_REG.sub(lambda m: floats.get(m.group(1)) or '((s32)ctx->%s)' % GPR_FIELD[m.group(1)], cpp), None


def jumped_into(functions):
    """Functions another function jumps into, not calls. The translator splits a function wherever code jumps past a label it thinks
    starts a new one, so one of these can be the tail of its caller, still using its caller's stack frame (func_8002C57C was)."""
    return {target for _, target in jump_edges(functions)}


def jump_edges(functions):
    """Each (jumper, target): a function that jumps or branches into the start of another, which the translator writes as a call
    followed by a return (a `j` or a branch, not a jal or jalr)."""
    found = set()
    for f in functions.values():
        for a, callee in f['calls'].items():
            if f['ins'].get(a - 4, ('',))[0] not in ('jal', 'jalr') and f['ins'].get(a, ('',))[0] not in ('jal', 'jalr'):
                found.add((f['name'], callee))
    return sorted(found)


def relied_through_jumps(relied, edges):
    """What a jumped-into function's callers rely on (2026-11-07, scratch/lift_ovl). A function another jumps into returns for
    the jumper: whatever it leaves goes back to the jumper's callers, as if the jumper had left it. So it must leave what they
    rely on -- relied[jumper] -- as well as what its own callers do. Added to relied[target], through chains of jumps (a piece
    jumped into from a piece), until nothing changes. callers_rely_exactly counted only calls (jal), so a function reached only
    by a jump -- rw_800EC990, which the port's stub func_800EC990 jumps to -- had nobody relying on anything, and was refused
    outright ('another function jumps into it') rather than checked. Changes relied in place."""
    changed = True
    while changed:
        changed = False
        for jumper, target in edges:
            more = relied.get(jumper, set()) - relied.setdefault(target, set())
            if more:
                relied[target] |= more
                changed = True


KEPT_REGISTERS = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp', 'ra'}
SP_STEP = re.compile(r'\$sp,\s*\$sp,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$')


def second_frame(f):
    """A second frame at the end, made inside the first's place (2026-11-12, modleft): (shift, release, remake, last), or None.

    The mod's copy modrep_func_8013486C ends with what its patch added after the game's own epilogue:

        lw $ra, 0xB4($sp) ... lw $s0, 0x90($sp)     the saved registers put back, as at any end
        addiu $sp, $sp, 0xB8        (release)       the game's frame given back
        addiu $sp, $sp, -0x18       (remake)        a small frame of the patch's own
        sw $ra, 0x14($sp) ; jal rw_rumble_fill ; nop ; lw $ra, 0x14($sp) ; jr $ra ; addiu $sp, $sp, 0x18

    m2c numbers every stack slot from the first frame (not_a_whole_function's refusal, fix J: the ra slot came out 0xA0 away).
    But the second frame lies wholly inside the first's place: its stack pointer is the first's plus shift (0xB8 - 0x18 = 0xA0),
    and the only slot it uses, where it keeps ra across its call, is the first frame's own ra slot (0x14 + 0xA0 = 0xB4), which
    already holds that ra (it was put back from there). So the end is, exactly, a call made with the stack pointer moved on by
    shift, then the return: second_frame_asm writes it so for m2c, and the converted function moves the stack pointer for that
    call (lift_second_frame). Only this form: the epilogue's loads straight before the release (each saved register from its
    own slot), the release and the remake one straight after the other with no way into them but falling through, then
    straight code of calls, nops and the ra save and load at that slot, ending with the return, the small frame given back in
    its delay slot."""
    addrs = sorted(f['ins'])
    first = frame_size(f)
    targets = set()
    for mn, ops in f['ins'].values():
        m = re.search(r'(?:L_|\.L)([0-9A-Fa-f]{8})\s*$', ops)
        if (mn in BRANCH or mn in ('j', 'b')) and m:
            targets.add(int(m.group(1), 16))
    for v in f.get('tables', {}).values():
        targets |= set(v.values())
    slots = dict((r, o) for o, r in saved_slots(f))
    for i, a in enumerate(addrs[:-1]):
        mn, ops = f['ins'][a]
        nxt = addrs[i + 1]
        m1, m2 = SP_STEP.match(ops), SP_STEP.match(f['ins'][nxt][1])
        if not (mn == 'addiu' and m1 and int(m1.group(1), 0) == first and f['ins'][nxt][0] == 'addiu' and m2 and nxt == a + 4):
            continue
        second = -int(m2.group(1), 0)
        shift = first - second
        if not 0 < second < first or a in targets or nxt in targets or 'ra' not in slots:
            return None
        before = f['ins'].get(a - 4, ('', ''))[0]
        if before in BRANCH or before in ('j', 'jr', 'jal', 'jalr', 'b'):
            return None         # (the release in a delay slot)
        rest = [x for x in addrs if x > nxt]
        if len(rest) < 2 or tuple(f['ins'][rest[-2]]) != ('jr', '$ra') or f['ins'][rest[-1]][0] != 'addiu':
            return None
        end = SP_STEP.match(f['ins'][rest[-1]][1])
        if not end or int(end.group(1), 0) != second:
            return None
        for x in rest[:-2]:
            xm, xops = f['ins'][x]
            m = re.fullmatch(r'\$ra,\s*((?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)', xops)
            if x in targets or not (xm in ('nop', 'jal') or (xm in ('sw', 'lw') and m and int(m.group(1), 0) + shift == slots['ra'])):
                return None
        return shift, a, nxt, rest[-1]
    return None


def second_frame_asm(text, f):
    """The function's assembly with its second frame (second_frame) written as part of the first, for m2c: the epilogue's loads
    of the saved registers before the release, the release, the remake, and the ra save and load at the first frame's ra slot
    taken out (nop: each puts back or keeps what is already there), the last step giving back the first frame -- so m2c sees
    the one frame end where it always does -- and before each call there a store to lift_second_frame, which the converted
    function turns into putting the stack pointer where the original's is for that call (finish_second_frame). The text as it
    was where there is no such frame, or where it can't be matched line by line."""
    found = second_frame(f)
    if not found:
        return text
    shift, release, remake, last = found
    lines = text.split('\n')
    # (to_asm writes the instructions in address order, one a line, after the labels and tables.)
    code = [i for i, l in enumerate(lines) if re.match(r'^\s+[a-z]', l)]
    order = sorted(f['ins'])
    if len(code) != len(order) or any(lines[i].split()[0] != f['ins'][a][0] for i, a in zip(code, order)):
        return text
    at = dict(zip(order, code))
    slots = set((o, r) for o, r in saved_slots(f))
    out = list(lines)
    x = release - 4         # (the epilogue's loads, back from the release)
    while x in f['ins']:
        mn, ops = f['ins'][x]
        m = re.fullmatch(r'\$(\w+),\s*((?:0[xX])?[0-9A-Fa-f]+)\(\$sp\)', ops)
        if mn == 'lw' and m and (int(m.group(2), 0), m.group(1)) in slots:
            out[at[x]] = '    nop'
        elif mn != 'nop':
            break
        x -= 4
    for a in order:
        if a < release:
            continue
        i = at[a]
        if a == last:
            out[i] = '    addiu $sp, $sp, 0x%X' % frame_size(f)       # (the first frame given back, which is all m2c sees)
        elif f['ins'][a][0] == 'jal':
            out[i] = '    sw $zero, lift_second_frame\n' + out[i]
        elif a in (release, remake) or f['ins'][a][0] in ('sw', 'lw'):
            out[i] = '    nop'        # (the release, the remake, and the ra save and load: see second_frame)
    return '\n'.join(out)


def finish_second_frame(cpp, f):
    """The store second_frame_asm puts before each call in a second frame, as m2c writes it (`lift_second_frame = 0;`), made
    what it stands for: the context's stack pointer moved to where the original's is there, the first frame's plus shift. The
    frame puts it back on the way out (LiftFrame). None if any is left that isn't on a line of its own."""
    found = second_frame(f)
    if not found:
        return cpp
    cpp = re.sub(r'^(\s*)lift_second_frame = 0;$', lambda m: '%sctx->r29 = (gpr)(int64_t)(s32)frame.at(0x%X);   // (the call in the '
                 "patch's second frame finds the stack pointer there: second_frame)" % (m.group(1), found[0]), cpp, flags=re.M)
    cpp = '\n'.join(l for l in cpp.split('\n') if not re.match(r'^\s*extern s32 lift_second_frame;', l))
    return None if 'lift_second_frame' in cpp else cpp


def not_a_whole_function(f):
    """Why this isn't a complete function by the calling rules, or None. m2c treats stack changes and restoring saved registers as
    bookkeeping and leaves them out, which is only right if they cancel out. So: it must restore everything it changes, and every
    way out must be a return with the stack where it started."""
    saved = set()
    changed = set()
    for mn, ops in f['ins'].values():
        m = re.match(r'\$(\w+),\s*-?(?:0[xX])?[0-9A-Fa-f]+\(\$sp\)$', ops)
        if mn == 'sw' and m:
            saved.add(m.group(1))
        defs = uses_defs(mn, ops)[1]
        changed |= defs & KEPT_REGISTERS
        if 'sp' in defs and not (mn == 'addiu' and SP_STEP.match(ops)):
            return 'changes the stack in an unusual way'
    if changed - saved:
        return 'changes a register without saving it'
    addrs, succ = cfg(f)
    late = second_frame(f)
    seen = {}
    work = [(addrs[0], 0)]
    while work:
        pc, offset = work.pop()
        mn, ops = f['ins'][pc]
        if mn == 'addiu':
            m = SP_STEP.match(ops)
            if m:
                offset += int(m.group(1), 0)
        elif '$sp' in ops and offset not in (0, -frame_size(f)) and not (late and pc > late[2]):
            # (late: a second frame written as part of the first for m2c, second_frame, 2026-11-12: allowed.)
            # (2026-11-11, trial 5: m2c numbers every stack slot from one frame, the first the function makes. The mod's copy
            #  modrep_func_8013486C releases its 0xB8 frame, makes one of 0x18 and saves ra at 0x14 of that: m2c wrote it as
            #  slot 0x14 of the first frame, 0xA0 away, and the tester saw the difference (calls@15, 805FFF48 for 805FFFE8).
            #  A second frame of the same size, func_80040280's, is the same numbering and stays.)
            return 'uses the stack in a second frame of another size: m2c numbers every slot from the first'
        if pc in seen:
            if seen[pc] != offset:
                return 'its stack differs between paths'
            continue
        seen[pc] = offset
        following = succ.get(pc, [])
        if not following and pc in f.get('tail', {}):
            # (A tail call, merge.py: the jump's delay slot is the last of this function to run, and the stack must be back
            #  where it started there, its frame released.)
            if offset != 0:
                return 'makes a tail call with the stack moved'
        elif not following:
            before = f['ins'].get(pc - 4, ('', ''))
            if before[0] != 'jr' or '$ra' not in before[1]:
                return 'has a way out that is not a return'
            if offset != 0:
                return 'returns with the stack moved'
        for n in following:
            work.append((n, offset))
    return None


def patched_by_mod():
    """Functions the mods patch or hook: those stay the translated original, the mods replace or wrap them there. Every mod the
    game loads, not only ours (2026-11-12): the arcade mod as built, and the other mods the game ships with (mods_as_built)."""
    names = set()
    replaced_names = set()      # (2026-11-12, modhooks: which of them a mod replaces, apart from those it only hooks -- REPLACED_BY_MOD)
    for base, _, files in os.walk(MOD):
        if 'build' in base.split(os.sep):
            continue
        for fn in files:
            if fn.endswith(('.c', '.h', '.cpp', '.py', '.toml', '.ld')):
                text = open(os.path.join(base, fn), encoding='utf8', errors='replace').read()
                # (Only the name a RECOMP_PATCH line defines and the one a RECOMP_HOOK names, 2026-11-12: every func_ name on such
                #  a line was taken until then, and hooks_recomp.c's `RECOMP_PATCH void func_800132D8(s32 slot, s32 n) {
                #  func_80012B24(...` kept func_80012B24 out, which nothing patches or hooks: it is only called there.
                #  scratch/agents/modnative PLAN.md, "Kind E". With an overlay's name too, as below.)
                for m in re.finditer(r'\bRECOMP_PATCH\s+[A-Za-z_][\w \t*]*?\b(func_[0-9A-F]{8}(?:_ovl_\w+)?)\s*\(', text):
                    names.add(m.group(1))
                    replaced_names.add(m.group(1))
                for m in re.finditer(r'\bRECOMP_HOOK(?:_RETURN)?\s*\(\s*"(func_[0-9A-F]{8}(?:_ovl_\w+)?)"\s*\)', text):
                    names.add(m.group(1))
    # And the game's functions the mod replaces with changed copies of their own instructions (gen/patches.py and the other
    # generators -> gen/asmpatch.py -> build/patches/mod.S, in the mod's .recomp_patch section under the function's own name).
    # The source never says RECOMP_PATCH for them, so the search above missed them: 0.65.0 shipped 36 of them converted, and
    # converted callers then reached the converted copy, not the mod's -- the mod's change lost on those paths (0.66.0's
    # crashes in func_801371EC, the QA forced move in func_800EC698; 2026-11-08). The built file is the whole list.
    built = os.path.join(MOD, 'build', 'patches', 'mod.S')
    if not os.path.exists(built):
        raise SystemExit('lift.py: %s is missing -- build the mod first (the functions it replaces must not be converted)' % built)
    # (an overlay's function has its overlay in the name, func_80110FD0_ovl_c: \b after the 8 digits missed those, and two the mod
    #  replaces -- func_80110FD0_ovl_c, func_80120D98_ovl_c -- were converted in 0.67.0 to 0.69.0's first build, 2026-11-08)
    names.update(re.findall(r'^\s*\.globl\s+(func_[0-9A-F]{8}(?:_ovl_\w+)?)\s*$', open(built, encoding='utf8', errors='replace').read(), re.M))
    replaced_names.update(re.findall(r'^\s*\.globl\s+(func_[0-9A-F]{8}(?:_ovl_\w+)?)\s*$', open(built, encoding='utf8', errors='replace').read(), re.M))
    # And what the built mods themselves say they replace and hook (2026-11-12): each .nrm's symbol file, as the mod loader reads
    # it. The searches above read only our mod's sources, and the other mods the game loads hook game functions those never
    # name -- aki_quickmatch's hooks on func_80106AB0 (the intro, each frame) and func_80113E30 (match set-up) -- so 0.79.0
    # converted func_80106AB0, and converted callers' calls of it skipped the hook (the qa agent, 2026-11-12). For our mod the
    # .nrm agrees with the lists above: its 165 replacements and 52 hook places are all in them (checked 2026-11-12).
    for nrm in mods_as_built():
        replaced, hooked = mod_targets(nrm)
        names.update(replaced)
        names.update(hooked)
        replaced_names.update(replaced)
    REPLACED_BY_MOD.clear()
    REPLACED_BY_MOD.update(replaced_names)
    return names


# The functions patched_by_mod found replaced by a mod (a RECOMP_PATCH, a changed copy in mod.S, a replacement in a built .nrm),
# apart from those only hooked. Filled by patched_by_mod. (2026-11-12, modhooks: a function only hooked converts, hooks.py.)
REPLACED_BY_MOD = set()


def hooked_only(patched):
    """The functions of `patched` the mods hook without any replacing them, and that convert with their hook calls: name ->
    {'entry': the registers its entry hooks read, or None for every register; 'return': the same at its returns; a key left
    out where it has no such hook}, as hooks.py has them. And {name: why not} for the other hooked ones. (2026-11-12, modhooks)"""
    ok, why_not = {}, {}
    nrms = mods_as_built()
    for name, h in sorted(hooks.hooked().items()):
        if name not in patched or name in REPLACED_BY_MOD:
            continue
        text, why = hooks.with_hooks(name, translated_text(name), REPLACED_BY_MOD, set(helpers_writing_saved_registers()))
        if text is None:
            why_not[name] = why
            continue
        ok[name] = {at: hooks.reads(name, at == 'return', nrms) for at in ('entry', 'return') if h[at]}
    return ok, why_not


def hooked_copies(modreps, functions):
    """The mod's copies (modrep_func_X) whose game function something also hooks, and that convert with their hook calls: name ->
    {'entry' / 'return': the registers the hooks there read, None for every one}, as hooked_only has it; and {name: why not} for
    the others. A copy is one of these if modrep's index says our mod hooks func_X, or Stage 1's table has hook calls in its copy
    (another mod's hook: aki_quickmatch's on func_80111158). (2026-11-12, kindd: kind D, hooks.copy_with_hooks.)"""
    ok, why_not = {}, {}
    nrms = mods_as_built()
    for name, rec in sorted(modreps.items()):
        if name not in functions:
            continue
        c = hooks.copies().get(rec['replaces'])
        if not (rec['hooked_entry'] or rec['hooked_return'] or c):
            continue
        text, why = hooks.copy_with_hooks(name, translated_text(name), modreps)
        if text is None:
            why_not[name] = why
            continue
        ok[name] = {at: hooks.copy_reads(name, at == 'return', modreps, nrms) for at in ('entry', 'return') if c[at] is not None}
    return ok, why_not


def mods_as_built():
    """The .nrm files of every mod the game loads, as tools/aot/aot.py takes them: the arcade mod as built (mod/build/mod_six), and
    each other mod in the source tree's exe/WWFNoMercyRecomp/assets/mods (the AkiMods the game ships with). LIFT_MOD_NRMS: a ';'-separated list instead."""
    if os.environ.get('LIFT_MOD_NRMS'):
        return [p for p in os.environ['LIFT_MOD_NRMS'].split(';') if p]
    ours = os.path.join(MOD, 'build', 'mod_six', 'nomercy_arcade.nrm')
    if not os.path.exists(ours):
        raise SystemExit('lift.py: %s is missing -- build the mod first (the functions it replaces must not be converted)' % ours)
    # (newuser 2026-11-17: the AkiMods as the source tree has them -- exe/WWFNoMercyRecomp/assets/mods, what every game folder
    #  gets (install_base.ps1, make_package.ps1) and what tools/aot/aot.py reads: the score no longer depends on a game folder)
    shipped = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp', 'assets', 'mods')
    others = sorted(p for p in glob.glob(os.path.join(shipped, '*.nrm')) if os.path.basename(p).lower() != 'nomercy_arcade.nrm')
    # (2026-11-17, pipefix: no installed game, or one with none of the other mods -- another agent had moved game\ aside -- and the
    #  AkiMods' hooks went unread without a word: func_80106AB0, which aki_quickmatch hooks, would have converted, its hook
    #  skipped by converted callers, as in 0.79.0. The game always ships them (tools\layout\install_base.ps1): their absence
    #  stops lift.py. LIFT_MOD_NRMS lists the mods outright. (newuser: since read from the source tree, not the game folder.)
    if not others:
        raise SystemExit("lift.py: no other mods in %s -- the AkiMods are read for the functions they hook (or set "
                         "LIFT_MOD_NRMS)" % shipped)
    return [ours] + others


_game_function_at = None


def mod_targets(nrm):
    """The game functions one built mod replaces and hooks, by name: ([replaced], [hooked]). From its mod_syms.bin (inside the
    .nrm, a zip), N64Recomp's format version 1 (lib/N64ModernRuntime/N64Recomp/src/mod_symbols.cpp parse_v1): each replacement
    and each hook names its game function by its section's ROM address and its own address, and the game's section table
    (RecompiledFuncs/recomp_overlays.inl) has the name there. Anything this can't read stops lift.py: a function a mod changes
    must never be converted by mistake."""
    import struct, zipfile
    global _game_function_at
    if _game_function_at is None:
        text = open(os.path.join(PORT, 'RecompiledFuncs', 'recomp_overlays.inl'), encoding='utf8').read()
        starts = {m.group(3): (int(m.group(1), 16), int(m.group(2), 16)) for m in re.finditer(
            r'\.rom_addr = (0x[0-9A-Fa-f]+), \.ram_addr = (0x[0-9A-Fa-f]+), .*?\.funcs = (section_\d+_\w+_funcs)', text)}
        _game_function_at, section = {}, None
        for line in text.split('\n'):
            m = re.match(r'static FuncEntry (section_\d+_\w+_funcs)\[\]', line)
            if m:
                section = m.group(1)
                continue
            m = re.search(r'\.func = (\w+), \.offset = (0x[0-9A-Fa-f]+)', line)
            if m and section in starts:
                rom, ram = starts[section]
                _game_function_at[(rom, ram + int(m.group(2), 16))] = m.group(1)
    try:
        with zipfile.ZipFile(nrm) as z:
            data = z.read('mod_syms.bin')
    except (OSError, KeyError, zipfile.BadZipFile) as e:
        raise SystemExit('lift.py: cannot read %s (%s): the functions the mods change must be known' % (nrm, e))
    if data[:8] != b'N64RSYMS' or struct.unpack_from('<I', data, 8)[0] != 1:
        raise SystemExit('lift.py: %s: not a mod symbol file of version 1' % nrm)
    (sections, dependencies, imports, dependency_events, replacements, exports, callbacks, events, hooks,
     strings) = struct.unpack_from('<10I', data, 12)
    at = 12 + 40 + strings      # (the file's header, the counts, the names)
    for _ in range(sections):   # (each: flags, file offset, vram, size, bss size, its functions' count, its relocations' count)
        nf, nr = struct.unpack_from('<2I', data, at + 20)
        at += 28 + 8 * nf + 16 * nr
    at += 12 * dependencies + 12 * imports + 12 * dependency_events
    replaced = [struct.unpack_from('<4I', data, at + 16 * i)[1:3] for i in range(replacements)]     # (function, section, vram, flags)
    at += 16 * replacements + 12 * exports + 8 * callbacks + 8 * events
    hooked = [struct.unpack_from('<4I', data, at + 16 * i)[1:3] for i in range(hooks)]               # (function, section, vram, flags)
    at += 16 * hooks
    if at != len(data):
        raise SystemExit('lift.py: %s: its mod symbol file did not read as expected (%d of %d bytes)' % (nrm, at, len(data)))

    def name(place):
        n = _game_function_at.get(tuple(place))
        if n is None:
            raise SystemExit('lift.py: %s changes a game function at %08X in the section at ROM %08X, where the game has no '
                             'function' % (nrm, place[1], place[0]))
        return n
    return sorted({name(p) for p in replaced}), sorted({name(p) for p in hooked})


# The port's own MIPS helpers (rw_*) don't all keep the calling rules: some hand back a result in a saved register (rw_contacts_s0
# sets s0 to the moved contact table). m2c takes it that a call keeps s0-s7 and fp, and the tester's stand-in for the helper leaves
# them alone too, so the two sides of the comparison agree while the game's don't: 0.71.0's first build, func_8012C474 read through
# the s0 it came in with and left the helper's in the context, crashing 13 of 14 tests (2026-11-09). A helper is one of these if it
# writes a saved register it never stored on the stack. A function calling one can't be proved until the stand-ins run the helper.
#
# (2026-11-11, trial 5: now they can be. abi.py's round takes a function that calls one, with the registers the helper hands
#  back -- handed_back -- in its plan's reload set. A reload register is a mirror register, so every write of it by the
#  function's own instructions goes to the context, and after each call m2c is given the context's value (`lw $s5,
#  lift_ctx_s5`, abi.reload_sites): what the helper put there, or after any other call the function's own last value. A
#  saved register in the mirror set is also in the keep set, put back on the way out as it came in. And the tester's
#  stand-in for the helper changes those registers, on both sides: run_fuzz's stand_in, fuzz_main.cpp fuzz_stand_in_abi.
#  A converted function that read its old s5 after the call then differs from the original, which reads the stand-in's.)
SAVED_REGISTERS = set(range(16, 24)) | {30}
SAVED_REGISTER_HELPERS = None
HELPER_REASON = 'a port helper that hands back a saved register'


def helpers_writing_saved_registers():
    """{helper: the saved registers it writes without having stored them on the stack, as abi.py names them}."""
    global SAVED_REGISTER_HELPERS
    if SAVED_REGISTER_HELPERS is None:
        translated_text('rw_contacts_s0')       # (reads them all, once)
        SAVED_REGISTER_HELPERS = {}
        for name, text in TRANSLATED['RecompiledFuncs'].items():
            if name.startswith('rw_'):
                written = {int(r) for r in re.findall(r'ctx->r(\d+) = ', text)} & SAVED_REGISTERS
                unsaved = {r for r in written if not re.search(r'MEM_[WD]\([^\n]*ctx->r29\) = ctx->r%d;' % r, text)}
                if unsaved:
                    SAVED_REGISTER_HELPERS[name] = {abi.GPR[r] for r in unsaved}
    return SAVED_REGISTER_HELPERS


def handed_back(pieces):
    """The saved registers the port's helpers hand back to any of these translated pieces (2026-11-11, trial 5)."""
    helpers = helpers_writing_saved_registers()
    found = set()
    for piece in pieces:
        for called in re.findall(r'\b(rw_\w+)\(rdram, ctx\)', translated_text(piece) or ''):
            found |= helpers.get(called, set())
    return found


def calls_saved_register_helper(pieces):
    """The first of the port's helpers that write a saved register (above) that any of these translated pieces calls, or None."""
    helpers = helpers_writing_saved_registers()
    for piece in pieces:
        for called in re.findall(r'\b(rw_\w+)\(rdram, ctx\)', translated_text(piece) or ''):
            if called in helpers:
                return called
    return None


def callee_declarations(callees):
    return ['extern "C" void %s(uint8_t* rdram, recomp_context* ctx);' % c for c in callees]


INCLUDE = [os.path.join(PORT, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), os.path.join(PORT, 'include'), GAME]


def covered_switch(cpp, f):
    """(2026-11-14, rest100) The C++ with its one jump-table switch's last case also marked `default:`, or None where that isn't
    sure to change nothing.

    A jump table is only reached past its bounds check (`sltiu` on the index: the translator sizes the table from that same
    bound), so the index is always one of the table's entries. When the switch m2c writes has no default and its cases are
    exactly 0 .. n-1 for the table's n entries, every value that can get there has its own case, and the compiler, which can't
    see the bounds check, believes the switch can be left with none -- and so that a function with a result can reach its end
    without returning one (func_801409D4: `switch (arg5)` after `(u32) arg5 < 0xB`, cases 0-10, each returning; refused since
    0.82 for -Werror=return-type). Writing `default:` beside the last case gives that impossible value a case, so every path
    returns, and changes nothing for the values that occur. Only one table and one switch, so which is which is sure."""
    tables = f.get('tables', {})
    switches = re.findall(r'\bswitch \(', cpp)
    if len(tables) != 1 or len(switches) != 1 or re.search(r'^\s*default:', cpp, re.M):
        return None
    n = len(next(iter(tables.values())))
    cases = [int(k, 0) for k in re.findall(r'^\s*case (-?(?:0x[0-9A-Fa-f]+|\d+)):', cpp, re.M)]
    if sorted(cases) != list(range(n)):
        return None
    last = re.search(r'^([ \t]*)case %s:' % (n - 1), cpp, re.M) or re.search(r'^([ \t]*)case 0x%X:' % (n - 1), cpp, re.M)
    if not last:
        return None
    return cpp[:last.end()] + ' default:      // (the bounds check before the table lets no other value here: covered_switch)' + cpp[last.end():]


def compiles(job):
    name, code, work, callees = job
    path = os.path.join(work, name + '.cpp')
    helpers = [abi.CPP_HELPERS] if 'LiftAbi' in code else []      # (abi.py's, for a function that uses them)
    helpers += OVERLAY_HELPER if 'lift_lookup<' in code else []     # (the overlay calls': "Calls into overlays")
    helpers += POINTER_HELPER if 'lift_pointer<' in code else []    # (calls through a pointer, 2026-11-11)
    helpers += TAIL_HELPER if TAIL_USE in code else []              # (tail calls': "Tail calls")
    helpers += modrep.CPP_HELPER if 'lift_mod_address(' in code else []   # (a copy of the mod's that uses the mod's data: modrep.py)
    # (With the unaligned word helpers when it uses them, as lifted.cpp and the tester have them: UNALIGNED_HELPERS.)
    open(path, 'w').write('\n'.join(['#include "lift_call.h"'] + helpers + callee_declarations(callees) + unaligned_helpers([code]) + [code, '']))
    # (2026-11-13, the Linux proof; agent "linuxfix") And with clang's -Wreturn-type as an error: a function with a result that can
    # reach its end without a return. That is undefined in C++ -- a compiler may do anything on that path, and GCC and clang
    # both warned for func_801409D4 (s32), which passed every test only because the path's result happened to be what the
    # original left in v0. clang's check is on its own view of the paths, so it can also refuse one whose end can't be reached
    # in fact: that only refuses, never lets through.
    command = [CLANG, '-std=c++20', '-fsyntax-only', '-Wno-everything', '-Werror=return-type']
    for folder in INCLUDE:
        command += ['-I', folder]
    p = subprocess.run(command + [path], capture_output=True, text=True)
    return name, p.returncode == 0, p.stderr[:300], '[-Werror,-Wreturn-type]' in p.stderr


def candidates(asm, work):
    os.makedirs(work, exist_ok=True)
    functions = to_asm.read_functions()
    index = json.load(open(os.path.join(asm, 'index.json')))
    # (With the system functions handled added as callees of known reads: with_system, 2026-11-07. game_analysis, without them,
    #  is what abi.py's round is given: it takes a callee not in the analysis for a system function and assumes nothing of it,
    #  where with them it would take one for a game function, and v0, v1, f0, f1 as surely written -- which the runtime's
    #  versions don't all do (__osDisableInt sets none). So a function abi.py plans for still treats its system calls as unknown.)
    game_analysis = json.load(open(os.path.join(HERE, 'analysis.json')))
    analysis = with_system(game_analysis, functions)
    print('system functions handled: %d; still refused: %s' % (len(SYSTEM), ', '.join('%s (%s)' % kv for kv in sorted(SYSTEM_REFUSED.items())) or 'none'))
    patched = patched_by_mod()
    # (2026-11-12, modhooks) The functions the mods only hook: converted with the hook calls in their wrapper (hooks.py).
    hookable, hook_why = hooked_only(patched)
    # (The pieces of a split function stay refused on their own: one is only part of a function, reached by a jump that leaves its
    #  jumper's callers relying on whatever it leaves. Its entry, put together with it (merge.py), is converted instead. So this looks
    #  at the functions as the translator wrote them.)
    separate = to_asm.read_functions(merge=False)
    fragments = jumped_into(separate)
    # (What each middle of a function is part of, for its reason: the entry it's put together under, or what holds it whole.)
    import merge
    middle_of = {}
    merge_plan = merge.plan(separate)
    for entry, v in sorted(merge_plan.items()):
        for p in v['pieces'][1:]:
            middle_of.setdefault(p, 'a piece of %s%s' % (entry, '' if v['why'] is None else ', not put together: ' + v['why']))
    for n, h in sorted(merge.holders(separate).items()):
        middle_of.setdefault(n, 'held whole by %s' % h)

    def jumps_why(name):
        """(2026-11-12, middles) What a function refused for jumping into another one really is. The jump check comes before
        the middle-of-a-function check (first_refusal), so 24 middles and pieces of functions (2,261 instructions on 0.80's
        candidates) were said to "jump into another function", with nothing to say whose they are, and 21 entries merge.py
        didn't put together with nothing to say why not. Only the words change: each is refused either way. An entry not put
        together says why not; a piece or a middle says what holds it, as a middle refused for itself does; anything else is
        said as before."""
        why = merge_plan.get(name, {}).get('why')
        if why and not why.startswith('the middle of'):
            return 'jumps into another function (not put together: %s)' % why
        if name in middle_of:
            return 'is the middle of a function (%s)' % middle_of[name]
        return 'jumps into another function'
    pieces_of = lambda n: functions[n].get('pieces', [n])
    # (by_caller: each caller's own reads after each function it calls, for its own declarations -- see prototype.)
    by_caller = {}
    reads_after, after_indirect, passes_on = callers_rely_on(functions, analysis, by_caller)
    taken = address_taken(functions)
    # A function called through a pointer has unknown callers: they may rely on whatever any such caller relies on.
    for n in taken:
        reads_after.setdefault(n, set()).update(after_indirect)
    # A result passed up: it matters if the function passing it on has a caller relying on it. Repeat until nothing changes.
    changed = True
    while changed:
        changed = False
        for callee, links in passes_on.items():
            have = reads_after.setdefault(callee, set())
            for caller, r in links:
                if r not in have and r in reads_after.get(caller, set()):
                    have.add(r)
                    changed = True
    print('registers read after a call through a pointer, without being set:', sorted(after_indirect))
    print('functions whose address is taken:', len(taken))
    # (reads_after still gives m2c's context its result types, in prototype(), as before. What a converted function must leave
    # for its callers is checked against callers_rely_exactly.)
    relied, relied_indirect = callers_rely_exactly(functions, analysis, taken)
    print('exactly, registers read after a call through a pointer:', sorted(relied_indirect))
    # (And a function jumped into must leave what its jumpers' callers rely on: relied_through_jumps. The edges are between the
    #  functions as the translator wrote them, pieces apart, which is how the jumps are made.)
    relied_through_jumps(relied, jump_edges(separate))
    at_address = functions_at(functions)        # (what an overlay call can reach: see "Calls into overlays")
    # The mod's copies (modrep.py): modrep_func_X runs in func_X's place, reached through func_X's entry by every caller of
    # func_X, by a call through a pointer to it, and by a jump to it. So what any of those relies on, the copy must leave:
    # func_X's reliance, worked out above, is the copy's. (Nothing calls modrep_func_X by that name.)
    modreps = modrep.index() if any(modrep.is_modrep(n) for n in functions) else {}
    for n in modreps:
        if n in functions:
            x = modrep.replaced(n)
            relied[n] = set(relied.get(n, set())) | set(relied.get(x, set()))
            reads_after[n] = set(reads_after.get(n, set())) | set(reads_after.get(x, set()))
    # (2026-11-12, kindd) The mod's copies something also hooks: converted with their hook calls in the wrapper, as the functions
    # only hooked are (hooks.copy_with_hooks). From here on they are in hookable with those; copy_hook_why: why each other one
    # isn't (modrep.why_not says it).
    copy_hookable, copy_hook_why = hooked_copies(modreps, functions)
    hookable.update(copy_hookable)
    copy_hook_why.update({n: None for n in copy_hookable})
    # (2026-11-12, modhooks) A return hook reads the registers as the function leaves them, after its return: what it reads, the
    # converted function must leave as the original does, as for a caller. (sp: LiftFrame puts it back. Every register, for a
    # place another mod hooks too, whose hook functions aren't analysed: such a function is then refused here, or kept to the
    # original by abi.py's round.)
    for n, at in hookable.items():
        if 'return' in at:
            read = set(at['return']) if at['return'] is not None else set(abi.GPR[1:26] + abi.GPR[28:]) | {'f%d' % k for k in range(32)}
            relied[n] = set(relied.get(n, set())) | (read - {'sp'})

    results = {}
    port_found = {}     # name -> [(piece, its additions)], from port_why: what carry() puts into the conversion
    def refuse(name, why):
        results[name] = {'why': why, 'size': index[name]['size']}

    plain = []
    only = subset_names()
    # (The checks before m2c, as one function so abi.py's second round can ask them again with its own reasons let through:
    #  abi_ok. Otherwise the same checks in the same order as before, 2026-11-07.)
    def first_refusal(name, v, abi_ok=False):
        if modrep.is_mod_own(name):
            return "the mod's own function"         # (never: they aren't read here. modrep.py)
        if modrep.is_modrep(name):
            # (The mod's own functions it may call: those analyse.py has read, with_system, 2026-11-12.)
            why = modrep.why_not(name, modreps, functions[name], translated_text(name), set(MOD_OWN_ANALYSIS) & set(MOD_OWN),
                                 hooked=copy_hook_why)      # (kind D: hooked_copies, 2026-11-12)
            if why:
                return why
        if v['indirect_call'] and not abi_ok:
            # (2026-11-11, trial 5: abi.py's round takes these -- "Calls through a pointer" -- so only the first round refuses.)
            return POINTER_REASON
        if v['jump_table'] and not v.get('tables_found'):
            return "has a jump table that couldn't be found"
        # (2026-11-11, trial 5: abi.py's round is asked with the overlay addresses too. It used to be asked without them, so a
        #  function with a looked-up call was refused there outright -- 10 functions, 3,951 instructions, counted under "calls
        #  something that reads ..." -- because the round's assembly named such a call as one function. abi_round now writes it
        #  as the first round does: see overlay_info and prepare.)
        if not_plain_calls(functions[name], analysis, abi_ok, at_address):
            why = not_plain_calls(functions[name], analysis, abi_ok, at_address)
            return jumps_why(name) if why == 'jumps into another function' else why
        if set(pieces_of(name)) & patched:
            # (2026-11-12, modhooks: one the mods only hook converts, with the hook calls in its wrapper -- hooks.py -- if it is
            #  whole, not put together from pieces; one whose hooked copy isn't its translated original says why.)
            if pieces_of(name) == [name] and name in hookable:
                pass
            elif pieces_of(name) == [name] and name in hook_why:
                return hook_why[name]
            else:
                return 'the mod patches it'
        if calls_saved_register_helper(pieces_of(name)) and not abi_ok:
            # (2026-11-11, trial 5: abi.py's round takes these -- see helpers_writing_saved_registers -- so only the first
            #  round refuses.)
            return "calls %s, a port helper that hands back a saved register: neither m2c nor the tester's stand-in sees it" % calls_saved_register_helper(pieces_of(name))
        # (A function another jumps into is no longer refused for that alone (2026-11-07): it goes through every check below as
        #  any function does, with its jumpers' callers' reliance added to its own (relied_through_jumps). A piece that is only
        #  the tail of its jumper, using the jumper's stack frame or registers, is no whole function: not_a_whole_function
        #  refuses it, and its entry, put together with it (merge.py), is converted instead, as before.)
        if mips.pops_a_frame_it_never_made(functions[name]):
            # (The middle of a function: it returns through a stack frame it never made, so no call can reach it -- only a jump
            #  from the rest of its function, which is converted whole under its entry's name (merge.py), or, where another
            #  translated function holds the same instructions, as part of that one. Said as what it is since 2026-11-08; it was
            #  refused as below before, for the registers it puts back without having saved them.)
            return 'is the middle of a function (%s)' % middle_of.get(name, 'no call reaches it')
        if not_a_whole_function(functions[name]):
            return not_a_whole_function(functions[name])
        if port_why(name, pieces_of(name), functions, separate, analysis, port_found):
            # The port added code to the translated version (frame interpolation, ray tracing, a netplay guard, widescreen). m2c
            # only sees the N64's instructions, so a conversion would lose it: port_additions.py finds each addition's place, and
            # the kinds it can carry go into the conversion at the same place (port_additions.carry, below). This refuses the
            # rest. (Until 2026-11-07 any call in lower case, or any changed stretch without an instruction comment, was refused
            # here, which also caught the translator's own helpers -- do_break, do_lwl -- and the labels it renumbers.)
            return port_why(name, pieces_of(name), functions, separate, analysis, port_found)
        # (2026-11-12, agent "refused2": and f13 and f15 where they are halves of double parameters, double_parameter_halves.)
        if set(analysis.get(name, {}).get('use', [])) - {'a0', 'a1', 'a2', 'a3', 'f12', 'f14', 'gp'} -                 double_parameter_halves(functions[name]) and not abi_ok:
            return 'reads a register other than its parameters'
        if {mn for mn, _ in functions[name]['ins'].values()} & REFUSED:
            return 'uses an instruction this pass refuses'
        # (2026-11-12, trial 5: V1_REASON, above. Last, so no function refused for something else is said to be refused for
        #  this. A call through a pointer or into an overlay is in no list of what surely writes v1: it may leave it.)
        if not abi_ok and abi.reads_own_v1_after_call(functions[name], surely_written()):
            return V1_REASON
        return None

    _surely = []
    def surely_written():
        """abi.must_write's sets, worked out once, the first time a function gets as far as the v1 check."""
        if not _surely:
            _surely.append(abi.must_write(functions, as_sets=True))
        return _surely[0]

    # abi.py's second round: functions refused only for registers the calling rules don't explain, tried again with them made
    # explicit (abi_retry: name -> the first round's reason). The first round is exactly as before.
    abi_retry = {}
    for name, v in index.items():
        if v['size'] == 0 or (only is not None and name not in only):
            continue
        why = first_refusal(name, v)
        if why:
            refuse(name, why)
            second = abi.ENABLED and (why in abi.FIRST_REASONS | {POINTER_REASON, V1_REASON} or HELPER_REASON in why)     # (HELPER_REASON: 2026-11-11, trial 5)
            if second and not functions[name].get('tail') and first_refusal(name, v, abi_ok=True) is None:
                abi_retry[name] = why
            elif second:
                # (2026-11-11: why abi.py's round isn't tried, beside the first reason -- it was silent, so 20 functions the
                #  mod replaces or hooks were counted under "calls something that reads ..." with nothing saying so.)
                results[name]['abi_why'] = 'not tried: ' + ('it ends in a tail call' if functions[name].get('tail') else first_refusal(name, v, abi_ok=True))
        else:
            plain.append(name)

    # A function's calls into an overlay (see "Calls into overlays"): each still the translator's lookup of the jal's own target,
    # and declared for m2c from every function at that address. overlays[name]: 'decls', the declarations for its context;
    # 'types', each call's result type as declared; 'use', the registers each reads; 'targets', the addresses, for the tester.
    overlays = {}
    def overlay_info(name, may_leave=False):
        """(info, None) for a function's overlay calls -- info None when it has none -- or (None, why not). As it was written
        in the loop below until 2026-11-11; a function of its own so abi.py's round can ask for its functions too. (may_leave:
        abi.py's round, overlay_prototype.)"""
        sites, why = overlay_sites(functions[name], pieces_of(name))
        if why:
            return None, why
        if not sites:
            return None, None
        info = {'decls': [], 'types': {}, 'use': {}, 'targets': sorted(set(sites.values())), 'sites': sites}
        for target in info['targets']:
            decl, result, use = overlay_prototype(target, at_address, functions, analysis,
                                                  by_caller.get((name, overlay_name(target))), may_leave)
            if decl is None:
                return None, use
            info['decls'].append(decl)
            info['types'][overlay_name(target)] = result
            info['use'][overlay_name(target)] = use
        return info, None
    for name in list(plain):
        info, why = overlay_info(name)
        if info:
            overlays[name] = info
        if why:
            refuse(name, why)
            plain.remove(name)
            overlays.pop(name, None)

    # The game keeps a value in the gp register the whole time: it's loaded once (rw_run_overlay, the port's version of where the
    # game did it) and only ever read, as a count of wrestler slots (func_800EB8B0 loops up to it over 0x358-byte slots). m2c would
    # see gp as never set. So a function that reads it is given to m2c with gp loaded from a name at the start, lift_gp_value,
    # which becomes the register itself in the C++.
    # (A function with overlay calls is written out again with them named lift_overlay_<address>: to_asm.py overlay_names.)
    def asm_for(n):
        path = os.path.abspath(os.path.join(asm, n + '.s'))
        # (and a second frame at the end, written as part of the first: second_frame_asm, 2026-11-12)
        if 'gp' not in analysis.get(n, {}).get('own', []) and n not in overlays and not second_frame(functions[n]):
            return path
        text = to_asm.to_asm(functions[n], overlay_names=True) if n in overlays else open(path).read()
        text = second_frame_asm(text, functions[n])
        if 'gp' in analysis.get(n, {}).get('own', []):
            text = text.replace('glabel %s\n' % n, 'glabel %s\n    lui $gp, %%hi(lift_gp_value)\n    lw $gp, %%lo(lift_gp_value)($gp)\n' % n, 1)
        copy = os.path.abspath(os.path.join(work, n + '.s'))
        open(copy, 'w').write(text)
        return copy

    with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
        made = list(pool.map(run_m2c, [(n, asm_for(n), callee_context(n, functions, analysis, reads_after, work, by_caller,
                                                                       overlays.get(n, {}).get('decls', ()))) for n in plain]))

    # abi.py's second round, once the first round has gone through the loop below (which adds the functions refused only because
    # "a caller reads what it leaves" to abi_retry): each gets its plan, its assembly with the plan's loads and stores, and m2c
    # again. The loop then takes them exactly as it takes the first round's, with abi_plans saying what else to do.
    abi_plans = {}
    reload_why = {}
    def abi_round():
        sure = abi.must_write(functions, as_sets=True)
        def with_overlay_calls(n):
            """(2026-11-11, trial 5) n as abi.py is to see it when it has overlay calls: (function, analysis, sure), with each
            looked-up jal a call to lift_overlay_<address> -- the name to_asm.py's overlay_names gives it -- which reads what any
            function at that address reads, and surely writes only what every one of them surely writes. abi.py then treats
            the call as it treats a named one: a register the callee reads is kept in step, one it may leave is passed on. The
            shared tables aren't changed: these are copies. (A function without overlay calls: as it was.)"""
            pointers = pointer_sites(functions[n])[0] or {}
            if n not in overlays and not pointers:
                return functions[n], game_analysis, sure
            f = dict(functions[n])
            f['calls'] = dict(f['calls'])
            known = dict(game_analysis)
            there = {}
            for site, target in overlays.get(n, {}).get('sites', {}).items():
                f['calls'][site + 4] = overlay_name(target)
                there[overlay_name(target)] = at_address.get(target, [])
            for o, names in there.items():
                use = set().union(*[set(game_analysis.get(x, {}).get('use', [])) for x in names]) if names else set()
                known[o] = {'use': sorted(use), 'own': sorted(use), 'n': 0}
            # (A call through a pointer, "Calls through a pointer": it may read any parameter register, and reads the one
            #  holding the address; nothing is sure of what it writes.)
            for site, register in pointers.items():
                f['calls'][site + 4] = pointer_name(register)
                use = sorted(abi.PARAMETERS | {register})
                known[pointer_name(register)] = {'use': use, 'own': use, 'n': 0, 'pointer': True}
            def sure_here(c):
                if c.startswith('lift_pointer_'):
                    return set()
                if c in there:
                    return set.intersection(*[set(sure(x)) for x in there[c]]) if there[c] else set()
                return sure(c)
            return f, known, sure_here
        def prepare(n, bounds=False, reload=()):
            """n's plan, its assembly and context written for m2c: (its job, None), or (None, why not)."""
            f_abi, analysis_abi, sure_abi = with_overlay_calls(n)
            # (2026-11-11, trial 5: with the saved registers a port helper hands back to it -- helpers_writing_saved_registers.)
            reload = sorted(set(reload) | handed_back(pieces_of(n)))
            # (2026-11-11, trial 5: and v1, where the function reads its own v1 after a call that may have left it alone --
            #  abi.reads_own_v1_after_call. Not where v1 is in step already: those functions are as they were.)
            first, _ = abi.plan(n, f_abi, functions, analysis_abi, relied, kept_floats(functions[n]), reload)
            if first is not None and 'v1' not in first['mirror'] and abi.reads_own_v1_after_call(f_abi, sure_abi):
                reload = sorted(set(reload) | {'v1'})
            p, why = abi.plan(n, f_abi, functions, analysis_abi, relied, kept_floats(functions[n]), reload)
            # (C1, 2026-11-11: the context first, so rewrite_asm knows which register each call's result is taken from --
            #  abi.call_store_sites stores it before a call that may leave it.)
            context = callee_context(n, functions, analysis, reads_after, work, by_caller, overlays.get(n, {}).get('decls', ()))
            results = abi.declared_results(open(context).read()) if context else {}
            if p is not None:
                # (With overlay calls: the assembly as the first round writes it for such a function, to_asm.py overlay_names.)
                source = to_asm.to_asm(functions[n], overlay_names=True) if n in overlays else open(os.path.join(asm, n + '.s')).read()
                text, why = abi.rewrite_asm(source, n, f_abi, p, analysis_abi, sure_abi, bounds, results)
            if p is None or text is None:
                return None, why
            # ("Calls through a pointer": each jalr as a call m2c can name, and its declaration with the context below.)
            pointers = sorted(set((pointer_sites(functions[n])[0] or {}).values()))
            text = re.sub(r'^(\s+)jalr \$(\w+)\s*$', lambda m: '%sjal %s' % (m.group(1), pointer_name(m.group(2))), text, flags=re.M)
            if 'gp' in analysis.get(n, {}).get('own', []):      # (as asm_for does)
                text = text.replace('glabel %s\n' % n, 'glabel %s\n    lui $gp, %%hi(lift_gp_value)\n    lw $gp, %%lo(lift_gp_value)($gp)\n' % n, 1)
            path = os.path.abspath(os.path.join(work, n + ('.abi_bounds.s' if bounds else '.abi.s')))
            open(path, 'w').write(text)
            header = os.path.abspath(os.path.join(work, n + '.abi.h'))
            open(header, 'w').write((open(context).read() if context else '') + '\n'.join(abi.context_lines(p)) + '\n' +
                                    ''.join('s32 %s(void);\n' % pointer_name(r) for r in pointers))
            abi_plans[n] = p
            return (n, path, header), None
        jobs = []
        for n in sorted(abi_retry):
            if pointer_sites(functions[n])[1]:
                results[n]['abi_why'] = pointer_sites(functions[n])[1]
                continue
            if n not in overlays:
                # (2026-11-11: its overlay calls, if it has any, as the first round's loop works them out for a plain function.
                #  2026-11-12, agent "refused": with a result some function at the address may not set -- this round keeps it
                #  in step, overlay_prototype's may_leave.)
                info, why = overlay_info(n, may_leave=True)
                if why:
                    results[n]['abi_why'] = why
                    continue
                if info:
                    overlays[n] = info
            job, why = prepare(n)
            if job is None:
                results[n]['abi_why'] = why
            else:
                jobs.append(job)
        with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
            done = dict(pool.map(run_m2c, jobs))
        # (v2, 2026-11-08) Where m2c's switch came out with cases outside it -- a table jump whose bounds check's branch got its
        # stores -- once more with the bounds check left as it is (abi.py rewrite_asm's bounds). If that can't be done (a
        # register it leaves out of step would be looked at), the first stays, and is refused when it doesn't compile.
        again = []
        for n, c in sorted(done.items()):
            if c and abi.cases_outside_switch(c):
                job, _ = prepare(n, bounds=True)
                if job is None:
                    prepare(n)          # (its plan and files as they were)
                else:
                    again.append(job)
        with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
            done.update(dict(pool.map(run_m2c, again)))
        # (regs, 2026-11-08: m2c's 'Read from unset register': a register a call left as it was. Once more with the context's value
        #  loaded there (abi.reload_sites). If that can't be done, the translation before stands, and its warning refuses it.)
        bounded = {j[0] for j in again}
        retry = []
        for n, c in sorted(done.items()):
            regs = sorted(set(re.findall(r'Read from unset register \$(\w+)', c or '')))
            if regs:
                job, why = prepare(n, n in bounded, regs)
                if job is None:
                    # (2026-11-12, agent "refused": why the loads couldn't be placed, kept beside the reason. It was dropped, so
                    #  a function refused for m2c's warning said nothing of how far this got.)
                    reload_why[n] = why
                    prepare(n, n in bounded)
                else:
                    retry.append(job)
        with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
            done.update(dict(pool.map(run_m2c, retry)))
        return sorted(done.items())
    def every_translation():
        yield from made
        if abi.ENABLED:
            yield from abi_round()

    to_compile = []
    pending = {}
    names_now = read_names()        # (for tidy.py: the temporaries names.json names stay, 2026-11-12)
    for name, c in every_translation():
        abi_plan = abi_plans.get(name)
        if os.environ.get('LIFT_KEEP_C') and c:
            # (2026-11-11: m2c's own text, kept beside the work files for looking at a refusal: <name>.m2c.c, or .abi.m2c.c.)
            open(os.path.join(work, name + ('.abi' if abi_plan else '') + '.m2c.c'), 'w').write(c)
        if not c:
            refuse(name, 'm2c failed')
            continue
        c = exact_spill_slots(c, functions[name])
        c = jump_index_locals(c, name)       # (2026-11-12, agent "refused2")
        c = read_v1_after_calls(c, name)
        c = unread_parameters(c, analysis)        # (2026-11-12, agent "refused")
        # (2026-11-11, trial 5) A jump table's address as a value: `lift_out_s3 = (s32) &jtbl_801552A0;`. Where the compiler
        # keeps a table's address in a register (to_asm.py find_kept_tables) and that register is one abi.py keeps in step,
        # the store after its `addiu $s3, $s3, %lo(jtbl_801552A0)` comes out so, and nothing in the C++ is called that
        # (func_8013843C: "does not compile"). The name is the address -- to_asm.py makes it from the lui and addiu it
        # replaces -- and the address is what the original's register holds there. Until now such a line never compiled,
        # so no converted function has one. Only that whole form, the address itself assigned: a table read as data
        # (`*(&jtbl_... + x)`) still doesn't compile, and is refused as before.
        c = re.sub(r'= \(s32\) &jtbl_([0-9A-F]{8});', r'= (s32) 0x\1;', c)
        # (A parameter the callee never reads, left unset: unread_parameters, 2026-11-12, modleft.)
        c = unread_parameters_left(c, analysis, overlays.get(name, {}).get('use'))
        why = refuse_c(c, functions[name])
        if why:
            refuse(name, why)
            # (2026-11-11, trial 5) A plain function whose only warning is m2c's "Read from unset register": it reads a register
            # as a call before left it -- sprintf's a3 after an earlier call, a t-register a callee set -- where the value there
            # is one the function's own instructions wrote before that call, so --left-registers can't show the C++ holds it
            # (13 functions, 6,670 instructions, most of "m2c left a comment"). abi.py's round already handles exactly this for
            # its own functions (regs, 2026-11-08): the register goes in the mirror set, so every write of it by the function
            # is stored to the context, and it is loaded from the context after each call (abi.reload_sites). So the function
            # is handed to that round, with this reason as its first. Nothing is skipped: if the round can't place the loads,
            # m2c's warning is still there and still refuses it. (Not one with a tail call: as for the others the round takes.)
            if abi.ENABLED and abi_plan is None and only_unset_registers(c) and not functions[name].get('tail'):
                abi_retry.setdefault(name, why)
            continue
        declared = {m.group(2): m.group(1).strip() for m in re.finditer(r'^(.+?)\s*\b(func_\w+)\(.*\);\s*/\* extern \*/', c, re.M)}
        # (The ones given in the context don't get an extern line: their results are what the context said.)
        for callee in set(functions[name]['calls'].values()):
            declared.setdefault(callee, prototype(callee, functions, analysis, reads_after, by_caller.get((name, callee))).split()[0])
        for callee in set(functions[name].get('tail', {}).values()):       # (a tail call is declared as its target: "Tail calls")
            declared[callee] = declared.get(callee + '__tail', declared[callee])
        declared.update(overlays.get(name, {}).get('types', {}))      # (the overlay calls', as their context declared them)
        reads_gp = 'lift_gp_value' in c
        if reads_gp:
            c = '\n'.join(l for l in c.splitlines() if not re.match(r'^extern .*\blift_gp_value\b', l))
            c = re.sub(r'&?\blift_gp_value\b', 'LIFT_GP', c)
            if 'LIFT_GP' not in c or re.search(r'LIFT_GP\s*=[^=]', c):
                refuse(name, 'uses gp in a way this pass refuses')
                continue
        c = re.sub(r'/\* (?:' + M2C_NOTES + r') \*/', '', c)
        c = '\n'.join(l for l in c.splitlines() if '/* extern */' not in l).strip()
        if fields_disagree(c, functions[name]):
            refuse(name, "m2c's memory accesses may not match the instructions")
            continue
        def is_a_move(callee, target):
            # (Only when this caller reads exactly the register its declaration has, and the callee always sets it: bitwise_casts.)
            result = declared.get(callee, '')
            register = 'f0' if result == 'f32' else 'v0'
            read = by_caller.get((name, callee), set()) & {'v0', 'f0'}
            return (target == 'f32') != (result == 'f32') and read == {register} and register in result_registers(callee, functions)
        bits = bitwise_casts(c, is_a_move)
        if bits is None:
            refuse(name, "bit-casts a call's result (an m2c stand-in)")
            continue
        c = restore_lost_sizes(mask_shift_counts(bits), functions[name])
        callees = []
        # (2026-11-11: and so is a call through a pointer, "Calls through a pointer".)
        calls_others = bool(index[name]['calls']) or name in overlays or index[name]['indirect_call']     # (an overlay call is a call: frame, registers)
        if calls_others:
            c, callees = convert_calls(c, name, declared, analysis, overlays.get(name, {}).get('use'), functions, by_caller)
            if c is None:
                refuse(name, callees)
                continue
        cpp = to_game_pointers(to_register_width(c, functions[name]))
        if modrep.is_modrep(name):
            # (The mod's own data, at addresses the loader decides: each such number in the C++ becomes where the mod's section
            #  is plus the offset in it, lift_mod_address. modrep.py)
            cpp, why = modrep.relocate(cpp, translated_text(name))
            if cpp is None:
                refuse(name, why)
                continue
        sig = parse_signature(cpp)
        if not sig:
            refuse(name, "couldn't read its signature")
            continue
        ret, args, start, end = sig
        sources = arg_sources(args)
        if sources is None or ret in ('s64', 'u64', 'M2C_UNK64'):
            refuse(name, 'has 64-bit or mixed float parameters')
            continue
        # (2026-11-12, agent "refused2") Each pair let through as a double parameter before m2c (double_parameter_halves) has to be
        # one in m2c's signature too: a double taken from that pair. And a double anywhere else, or a double result, only with
        # those: m2c made its own guess otherwise, from instructions this pass doesn't look at. (f64 was refused here until now.)
        halves = double_parameter_halves(functions[name]) & set(analysis.get(name, {}).get('use', []))
        if {'ctx->f%d.d' % (int(h[1:]) - 1) for h in halves} != {x for x in sources if x.endswith('.d')} or                 (ret == 'f64' and not double_at_returns(functions[name])):
            refuse(name, 'has 64-bit or mixed float parameters')
            continue
        # What the original wrote that the converted function won't leave behind: nothing may read it afterwards.
        # Only what it writes itself: what the functions it calls leave behind is the same either way, since the converted one
        # makes the same calls with the same parameters. (Until 2026-11-05 a function that calls others counted every scratch
        # register as changed, which refused hundreds for nothing.)
        wrote = set()
        for mn, ops in functions[name]['ins'].values():
            wrote |= uses_defs(mn, ops)[1]
        kept = {'sp', 'ra', 's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'gp'} | kept_floats(functions[name])
        if ret == 'f32':
            kept |= {'f0'}
        elif ret == 'f64':
            kept |= {'f0', 'f1'}      # (a double result, the pair: 2026-11-12, agent "refused2")
        elif ret != 'void':
            kept |= {'v0'}
        lost = (wrote - kept) & relied.get(name, set())
        if lost and abi_plan is None:
            refuse(name, 'a caller reads what it leaves in ' + ','.join(sorted(lost)))
            # (Until 2026-11-11 not a function with overlay calls: abi.py's round wrote its assembly without overlay_names, so
            #  m2c would have seen such a call as a call to one function there. It writes it with them now: prepare.)
            if abi.ENABLED and not functions[name].get('tail'):
                abi_retry.setdefault(name, results[name]['why'])     # (tried again in abi.py's round: see abi_round)
            continue
        if lost - set(abi_plan['mirror'] if abi_plan else ()):
            # (Can't happen -- the plan keeps every register a caller relies on in step -- but checked.)
            refuse(name, 'a caller reads what it leaves in ' + ','.join(sorted(lost)))
            continue
        pending[name] = {'cpp': stores_of_the_other_kind(frame_float_reads_into_ints(cpp)), 'ret': ret, 'sources': sources, 'size': index[name]['size']}
        if name in overlays:
            # (The addresses it looks up, for the tester's recording stand-ins and the release's helper: run_fuzz, write_common.)
            pending[name]['overlay'] = ['%08X' % a for a in overlays[name]['targets']]
        if modrep.is_modrep(name):
            # (Which game function it stands in for, and the hash of the mod's copy it was made from: release_text's table,
            #  and the checks that it is still the mod's copy -- write_release, check-modrep, the game's mod loader.)
            pending[name]['modrep'] = modrep.record(modreps[name])
        if name in hookable:
            # (Its hook places, and the registers each one's hooks read -- None: every register: the wrapper's hook calls, the
            #  tester's stand-in, and the loader's table (run_fuzz, release_text; hooks.py). 2026-11-12, modhooks)
            # (A copy of the mod's, kind D: the places of the game function it replaces. 2026-11-12, kindd)
            h = hooks.hooked()[name] if not modrep.is_modrep(name) else {'section': modreps[name]['section_vrom'], 'vram': modreps[name]['vram']}
            pending[name]['hooks'] = {'section': '%08X' % h['section'], 'vram': '%08X' % h['vram'], 'at': hookable[name]}
        if re.search(r'\bgoto\b', cpp):
            pending[name]['gotos'] = True
        # Names m2c uses without declaring them: frame slots and the frame itself, and saved registers' values on the way in
        # (frame_needs). The first two need the frame in game memory, even in a function that calls nothing; the last the registers.
        undeclared, bare_sp, saved = frame_needs(cpp)
        # (And a parameter's address taken, which is its home slot in game memory: parameter_slots, 2026-11-12.)
        needs_frame = calls_others or bool(undeclared) or bare_sp or bool(PARAMETER_ADDRESS.search(cpp))
        # (abi.py: its registers are in the context, so it takes ctx -- see the end of this.)
        if needs_frame or reads_gp or saved or abi_plan:
            if needs_frame:
                cpp = add_context_and_frame(cpp, name, frame_size(functions[name]), saved_slots(functions[name]),
                                            home_spills(functions[name]))
                cpp = finish_second_frame(cpp, functions[name])        # (second_frame, 2026-11-12)
                if cpp is None:
                    del pending[name]
                    refuse(name, 'a second frame this pass refuses')
                    continue
                # A stack argument stored in another block than its call: m2c (patched 2026-11-07, FrameArgSlot) writes and reads
                # it as M2C_STACK_ARG(type, offset), the slot in the frame, which is game memory at the original's offsets.
                # Until then that was an m2c error ('Unable to find stack arg'), and the function was refused.
                # (And a halfword or byte stored there with sh or sb, M2C_STACK_ARG(s16 / s8, ...): m2c's STACK_ARG_CTYPES,
                #  2026-11-12, agent "refused". Ref<s16> stores the register's low 16 bits, as sh does.)
                cpp = re.sub(r'\bM2C_STACK_ARG\((s32|f32|s16|s8), (0x[0-9A-F]+)\)', r'Ref<\1>(frame.at(\2))', cpp)
                cpp, why = name_frame_slots(cpp, functions[name], undeclared)
                if cpp is not None:
                    cpp, why = parameter_slots(cpp, functions[name], args, sources)      # (2026-11-12, agent "refused")
                if cpp is None:
                    del pending[name]
                    refuse(name, why)
                    continue
            else:
                cpp = re.sub(r'^(.*\b' + re.escape(name) + r')\((.*)\) \{$',
                             lambda m: m.group(1) + '(recomp_context* ctx' + ('' if m.group(2).strip() in ('', 'void') else ', ' + m.group(2)) + ') {',
                             cpp, count=1, flags=re.M)
            cpp = cpp.replace('LIFT_GP', '((s32)ctx->r28)')
            cpp, why = entry_values(cpp, abi_plan)
            if cpp is None:
                del pending[name]
                refuse(name, why)
                continue
            if abi_plan:
                # What lift_in_ and lift_out_ stand for, at the top of the function (abi.py cpp_declarations). Every one is a
                # register in the context; m2c's C only names them where the plan's loads and stores were.
                cpp = abi.add_declarations(cpp, name, abi_plan)
                if cpp is None:
                    del pending[name]
                    refuse(name, "ABI: couldn't find where its body starts, or an exact value's place (regs2)")
                    continue
                pending[name]['abi'] = abi_plan
            pending[name].update({'cpp': stores_of_the_other_kind(frame_float_reads_into_ints(cpp)), 'ctx': True, 'callees': callees})
        declarations = []
        if any(adds for _, adds in port_found.get(name, [])):
            # The port's additions, put into the conversion where the port's translated version has them (port_additions.py):
            # code at the start goes in the adapter, code around a call in a wrapper the converted function calls instead.
            cpp, record, why = port_additions.carry(name, cpp, port_found[name], separate)
            if cpp is None:
                del pending[name]
                refuse(name, why)
                continue
            pending[name]['cpp'] = stores_of_the_other_kind(frame_float_reads_into_ints(cpp))
            pending[name]['port'] = record
            declarations = record['declarations']
        # (2026-11-12, the readability track) The tidying passes, last, on the C++ as it is now (tidy.py): an empty first branch
        # turned round; a write of a register kept in step (lift_out_*) that nothing can read before the next write of it
        # dropped; an if or a temporary left doing nothing dropped; a temporary set once and read once put into its use; m2c's
        # loops left as gotos written as do / while / for; a goto to a short tail (3 lines at most) replaced by a copy of it
        # where that goes on the same way. (2026-11-13, tidy2) And a goto that falls, breaks, continues or returns the same way
        # made that; an if whose branches but one jump to the label after the code between turned into if / else; the same last
        # statement of every branch written once after the if; nested ifs joined (else if, &&). Each is exactly the same
        # behaviour by its rules (tidy.py has them), and the tester proves the tidied text like any other. Not a temporary
        # names.json names: its name is kept, so a newly named one changes that function's text and it is tested again. A
        # function tidy.py can't read is left as it was.
        # LIFT_NO_TIDY=1: the text as before.
        if os.environ.get('LIFT_NO_TIDY') != '1':
            pending[name]['cpp'], _ = tidy.tidy(name, pending[name]['cpp'], set(names_now.get(name, {}).get('vars', {})))
            if not re.search(r'\bgoto\b', pending[name]['cpp']):
                pending[name].pop('gotos', None)
        pending[name]['cpp'] = every_compilers_cpp(pending[name]['cpp'])      # (C++ GCC takes too: every_compilers_cpp, 2026-11-13)
        to_compile.append((name, '\n'.join(declarations + [pending[name]['cpp']]), os.path.abspath(work), callees))

    with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
        compiled = list(pool.map(compiles, to_compile))
    # (2026-11-14, rest100) A function clang finds can fall off its end only past a jump table's switch whose cases are every
    # entry of the table gets that switch's last case marked `default:` too, and is compiled again (covered_switch). Only these
    # are touched: every other function's text is as before.
    retry = {}
    for name, ok, err, falls_off in compiled:
        if not ok and falls_off:
            fixed = covered_switch(pending[name]['cpp'], functions[name])
            if fixed is not None:
                retry[name] = fixed
    if retry:
        again = [(n, code.replace(pending[n]['cpp'], retry[n], 1), w, c) for n, code, w, c in to_compile if n in retry]
        with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
            done = {r[0]: r for r in pool.map(compiles, again)}
        for i, r in enumerate(compiled):
            if r[0] in done and done[r[0]][1]:
                pending[r[0]]['cpp'] = retry[r[0]]
                compiled[i] = done[r[0]]
    for name, ok, err, falls_off in compiled:
        if ok:
            results[name] = pending[name]
        else:
            refuse(name, 'can reach its end without returning its result (undefined in C++)' if falls_off else 'does not compile')
            results[name]['error'] = err

    # (abi.py's second round, where it didn't get through: the first round's reason stays the one counted, and the second
    #  round's goes beside it, as abi_why, to say how far it got.)
    for name, first in abi_retry.items():
        r = results[name]
        if 'why' in r and r['why'] != first:
            r['abi_why'], r['why'] = r['why'], first
        elif 'why' not in r:
            r['abi_first'] = first       # (converted with its extra registers: what the first round refused it for)

    for name, why in reload_why.items():
        if 'why' in results.get(name, {}):
            results[name]['reload_why'] = why      # (2026-11-12: see abi_round's retry)
    if only is not None and os.path.exists(CANDIDATES):   # (a subset: merged into the full list)
        merged = json.load(open(CANDIDATES))
        merged.update(results)
        results = merged
    json.dump(results, open(CANDIDATES, 'w'), indent=0)
    total = sum(v['size'] for v in index.values())
    good = [v for v in results.values() if 'cpp' in v]
    print('candidates: %d functions, %d instructions, %.2f%% of the game' % (len(good), sum(v['size'] for v in good),
                                                                            100 * sum(v['size'] for v in good) / total))
    tally = {}
    for v in results.values():
        if 'why' in v:
            t = tally.setdefault(re.sub(r'in .*$', 'in a register', v['why']), [0, 0])
            t[0] += 1
            t[1] += v['size']
    for why, (n, ins) in sorted(tally.items(), key=lambda x: -x[1][1]):
        print('  left out: %-50s %5d functions %7d instructions' % (why, n, ins))


NAMES = os.path.join(HERE, 'names.json')


def read_names():
    """What's been worked out about each function, written by a person: tools/lift/names.json, kept apart so regenerating keeps it.

        "func_80111EF4": {"name": "shift_letter", "about": "What it does.", "vars": {"arg0": "letter"}, "guess": true}

    "guess" means the name is a best guess from the code alone, and the comment says so."""
    return json.load(open(NAMES, encoding='utf8')) if os.path.exists(NAMES) else {}


def with_copy_names(names, cands):
    """names.json, plus a name for each of the mod's converted copies (modrep.py) that has none of its own: the name of the game
    function it stands in for, with _mod_copy after it. (2026-11-10: the copies kept their N64 names, modrep_func_X, while the
    functions they replace were named. Only the function's name is carried over, not the variables': the copy is the mod's code,
    not the game's, so its arguments needn't be the same ones. check-names proves the machine code stays the same either way.)"""
    out = dict(names)
    for n, v in cands.items():
        base = modrep.replaced(n) if modrep.is_modrep(n) else None
        if base and n not in names and base in names and 'cpp' in v:
            out[n] = {'name': names[base]['name'] + '_mod_copy', 'vars': {}, 'guess': names[base].get('guess', False),
                      'about': "The mod's copy of %s (%s): %s" % (names[base]['name'], base, names[base]['about'])}
    return out


FIELDS_FILE = os.path.join(HERE, 'fields.json')
FIELD_STATS = {'accesses': 0, 'functions': set(), 'typed_vars': 0, 'kept_raw': 0}
FIELD_SKIP = set()     # (fields.json _skip_functions: left with raw accesses; see there)
FIELD_PARAMS = {}      # function -> {parameter: kind}: what every call of it passes (field_param_kinds)
FIELD_RETURNS = {}     # function -> kind: what every return of it gives (field_param_kinds)
# The overlays' code, data and bss: what a word there holds depends on which overlay is loaded, so a record found at an address in
# here (fields.json _globals / _arrays / _pointer_arrays) is only taken for functions of the sections its entry names. (2026-11-11)
OVERLAY_MEMORY = (0x800D9960, 0x80194000)
CPP_KEYWORDS = set('alignas alignof and asm auto bool break case catch char class const constexpr continue default delete do double '
                   'else enum explicit export extern false float for friend goto if inline int long mutable namespace new noexcept not '
                   'nullptr operator or private protected public register return short signed sizeof static struct switch template this '
                   'throw true try typedef typeid typename union unsigned using virtual void volatile while xor'.split())


def read_fields():
    """The named fields of the game records (tools/lift/fields.json): {kind: {offset: field}}, and where records are known to be:
    {'sizes': {kind: bytes}, 'macros': {kind: macro}, 'globals': {address: (kind, sections)} for a word that holds a record's
    address, 'arrays': {address: (kind, count, sections)} for records in place there one after another, 'pointer_arrays': {address:
    (kind, sections)} for a word that holds the address of such an array, 'params': {function: {parameter: kind}} and 'returns':
    {function: kind} for functions called only through a table, each entry with the evidence for it}. Empty if there's no file:
    then nothing is rewritten. (2026-11-10; the rest 2026-11-11)"""
    if not os.path.exists(FIELDS_FILE):
        return {}, {}
    data = json.load(open(FIELDS_FILE, encoding='utf8'))
    kinds = {k: {int(o, 16): f for o, f in v['fields'].items()} for k, v in data.items() if not k.startswith('_')}
    for k, fields in kinds.items():
        for f in fields.values():
            if not re.fullmatch(r'[a-z][a-z0-9_]*', f['name']) or f['name'] in CPP_KEYWORDS:
                raise SystemExit('fields.json: %s has a field named %r' % (k, f['name']))
    seen = {}
    for a, v in fixed_places(data).items():
        if not re.fullmatch(r'[a-z][a-z0-9_]*', v['name']) or v['name'] in CPP_KEYWORDS | {'hi', 'lo'} or not re.fullmatch(r'0x8[0-9A-F]{7}', a):
            raise SystemExit('fields.json: _variables %s is named %r' % (a, v['name']))
        if seen.setdefault(v['name'], a) != a:
            raise SystemExit('fields.json: _variables %s and %s are both named %r' % (seen[v['name']], a, v['name']))
    # (An array's name, its start's constant beside the variables': WRESTLER_IN(wrestlers, ..). 2026-11-13)
    for a, v in data.get('_arrays', {}).items():
        if 'name' not in v:
            continue
        if not re.fullmatch(r'[a-z][a-z0-9_]*', v['name']) or v['name'] in CPP_KEYWORDS | {'hi', 'lo'}:
            raise SystemExit('fields.json: _arrays %s is named %r' % (a, v['name']))
        if seen.setdefault(v['name'], a) != a:
            raise SystemExit('fields.json: _arrays %s and %s are both named %r' % (seen[v['name']], a, v['name']))
    FIELD_SKIP.clear()
    FIELD_SKIP.update(data.get('_skip_functions', []))
    def where(v, *more):
        v = {'kind': v} if isinstance(v, str) else v
        return (v['kind'],) + tuple(int(v.get(m, '0'), 0) for m in more) + (frozenset(v.get('sections', ())),)
    sources = {'sizes': {k: int(v['size'], 16) for k, v in data.items() if not k.startswith('_') and 'size' in v},
               'macros': {k: v.get('macro') or re.sub(r'(?<!^)(?=[A-Z])', '_', k).upper() for k, v in data.items() if not k.startswith('_')},
               'globals': {int(a, 16): where(v) for a, v in data.get('_globals', {}).items()},
               'arrays': {int(a, 16): where(v, 'count') for a, v in data.get('_arrays', {}).items()},
               'array_names': {int(a, 16): v['name'] for a, v in data.get('_arrays', {}).items() if 'name' in v},
               'pointer_arrays': {int(a, 16): where(v) for a, v in data.get('_pointer_arrays', {}).items()},
               'params': {f: {p: k for p, k in v.items() if re.fullmatch(r'arg\d+', p)} for f, v in data.get('_params', {}).items()},
               'returns': {f: v['kind'] for f, v in data.get('_returns', {}).items()},
               'elements': {k for k, v in data.items() if not k.startswith('_') and v.get('elements')},
               'roles': {},
               'variables': {int(a, 16): (v['name'], v['type'], frozenset(v.get('sections', ()))) for a, v in fixed_places(data).items()}}
    # (A name may be a role of more than one kind -- `display_list` is the cursor, a Gfx **, in most functions and the list
    #  itself, a Gfx *, in some: field_kinds takes the first whose fit the accesses pass. 2026-11-12)
    # (Which is first can't be fields.json's order: it is written with its keys sorted, and that put Gfx before GfxCursor and
    #  lost about 1,400 names in 0.81's staged set. A role's "order" says it, lower first (default 0), then the kind's name.
    #  2026-11-13)
    for k, v in sorted(data.get('_roles', {}).items(), key=lambda kv: (int(kv[1].get('order', 0)), kv[0])):
        for n in v['names']:
            sources['roles'].setdefault(n, []).append((k, frozenset(v.get('sections', ())), int(v.get('min_named', 2))))
    return kinds, sources


def fixed_places(data):
    """fields.json's named words at fixed addresses: its _variables and its _constants -- (convlits 2026-11-22) the float
    constants and texts the game's code only reads, in its data, named by what the ROM holds there (a reading of the ROM, not a
    guess; tools/lift/fields.json _constants' _about). Written the same way: GAME_VAR(f64_0_5) is *(P<f64> )0x80055C70, and the
    address alone field::var::text_no_data. An address is in one of the two, not both."""
    variables, constants = data.get('_variables', {}), {a: v for a, v in data.get('_constants', {}).items() if not a.startswith('_')}
    both = sorted(set(variables) & set(constants))
    if both:
        raise SystemExit('fields.json: %s is in both _variables and _constants' % both[0])
    return dict(variables, **constants)


FIELD_MACRO = {'StepContext': 'STEP', 'Wrestler': 'WRESTLER', 'AiRecord': 'AI_RECORD'}


def fields_preamble(kinds, sources=None):
    """Named fields as macros: WRESTLER(x, spirit) is M2C_FIELD(x, P<field::Wrestler::spirit_t>, field::Wrestler::spirit), where
    spirit_t is s16 and spirit the int 0xAE: the very expression the conversion had, with names for its type and its offset, so the
    compiler sees nothing new (check-names compares the machine code byte for byte). An access of another width or sign names its
    type: WRESTLER_AS(x, area_bits, s16). (Accessor functions, Wrestler(x).spirit(), read better but changed how clang laid out a
    few large functions -- equivalent code, not the same bytes -- so they're not used. 2026-11-10)"""
    data = json.load(open(FIELDS_FILE, encoding='utf8')) if os.path.exists(FIELDS_FILE) else {}
    macros = (sources or {}).get('macros') or FIELD_MACRO
    out = ['// The game records the converted code reads, with their fields named (tools/lift/fields.json, from the naming',
           "// track's structure notes). WRESTLER(address, spirit) reads or writes his spirit; WRESTLER_AS(address, field, type) another width.", '']
    out.append('namespace field {')
    for kind, fields in sorted(kinds.items()):
        size = data.get(kind, {}).get('size')
        out += ['// %s%s' % (data.get(kind, {}).get('about', kind), ' (0x%X bytes)' % int(size, 16) if size else ''), 'namespace %s {' % kind]
        for off, f in sorted(fields.items()):
            note = f.get('about', '').replace(chr(10), ' ')
            out.append('    // +0x%X: %s%s' % (off, note[:150], ' (a reading)' if f.get('guess') else ''))
            out.append('    constexpr int %s = 0x%X; using %s_t = %s;' % (f['name'], off, f['name'], f['type']))
        out += ['}', '']
    out += ['}  // namespace field', '']
    for kind in sorted(kinds):
        macro = macros.get(kind) or FIELD_MACRO[kind]
        out.append('#define %s(address, name) M2C_FIELD(address, P<field::%s::name##_t>, field::%s::name)' % (macro, kind, kind))
        out.append('#define %s_AS(address, name, type) M2C_FIELD(address, P<type>, field::%s::name)' % (macro, kind))
        if data.get(kind, {}).get('elements'):
            # (A run of these records: the one at that index from the address.)
            out.append('#define %s_AT(address, index, name) M2C_FIELD(address, P<field::%s::name##_t>, field::%s::name + (index) * 0x%X)' % (macro, kind, kind, int(data[kind]['size'], 16)))
            out.append('#define %s_AT_AS(address, index, name, type) M2C_FIELD(address, P<type>, field::%s::name + (index) * 0x%X)' % (macro, kind, int(data[kind]['size'], 16)))
        if data.get(kind, {}).get('size'):
            # (Seen from a field: `at` is the address of the record's `anchor` field, as a variable walking the records at
            #  that field holds it (field_walkers): the field `name` of the same record, or with _AT of the record `index`
            #  records on. 2026-11-13)
            size = int(data[kind]['size'], 16)
            out.append('#define %s_VIA(at, anchor, name) M2C_FIELD(at, P<field::%s::name##_t>, field::%s::name - field::%s::anchor)' % (macro, kind, kind, kind))
            out.append('#define %s_VIA_AS(at, anchor, name, type) M2C_FIELD(at, P<type>, field::%s::name - field::%s::anchor)' % (macro, kind, kind))
            out.append('#define %s_VIA_AT(at, anchor, index, name) M2C_FIELD(at, P<field::%s::name##_t>, field::%s::name + (index) * 0x%X - field::%s::anchor)' % (macro, kind, kind, size, kind))
            out.append('#define %s_VIA_AT_AS(at, anchor, index, name, type) M2C_FIELD(at, P<type>, field::%s::name + (index) * 0x%X - field::%s::anchor)' % (macro, kind, size, kind))
    variables = data.get('_variables', {})
    constants = {a: v for a, v in data.get('_constants', {}).items() if not a.startswith('_')}
    if variables or constants:
        # The game's variables at fixed addresses (fields.json _variables): GAME_VAR(match_rules) is *(P<s32> )0x800A9950, the
        # very expression the conversion had -- the address an unsigned constant, as the literal was -- with names for its
        # type and its address. GAME_VAR_AS(match_rules, u16) reads it at another width. One in the overlays' memory is
        # named only in the functions of the sections its entry gives. (2026-11-12)
        out += ['', '// The game variables at fixed addresses the converted code reads (tools/lift/fields.json _variables).',
                'namespace field {', 'namespace var {']
        for a, v in sorted(variables.items(), key=lambda x: int(x[0], 16)):
            note = v.get('about', '').replace(chr(10), ' ')
            out.append('// %s: %s%s' % (a, note[:150], ' (a reading)' if v.get('guess') else ''))
            out.append('constexpr u32 %s = %s; using %s_t = %s;' % (v['name'], a, v['name'], v['type']))
        out += ["// KSEG0's base: an address in main memory minus it is the physical address (libultra K0_TO_PHYS) -- named_addresses.",
                'constexpr u32 kseg0 = 0x80000000;']
        if constants:
            # (convlits 2026-11-22: the constants -- fixed_places -- in the same namespace, after the variables.)
            out += ['', "// The game's float constants and texts in its data, named by what the ROM holds there; the game's code only",
                    '// reads them (tools/lift/fields.json _constants).']
            for a, v in sorted(constants.items(), key=lambda x: int(x[0], 16)):
                # (A text's about quotes it: no line may end in a backslash, which would join the next line to the comment.)
                out.append(('// %s: %s' % (a, re.sub(r'[\x00-\x1f\x7f]', ' ', v.get('about', ''))[:150])).rstrip('\\'))
                out.append('constexpr u32 %s = %s; using %s_t = %s;' % (v['name'], a, v['name'], v['type']))
        out += ['}', '}  // namespace field', '',
                '#define GAME_VAR(name) *(P<field::var::name##_t> )field::var::name',
                '#define GAME_VAR_AS(name, type) *(P<type> )field::var::name']
        # (An entry of a table there, `bytes` in, as the MIPS code's %hi / %lo pair: (bytes + 0x80540000) and -0x4360 as the
        #  conversion had them, the high half unsigned as its literal was, the low half an int. 2026-11-13)
        # (consteval, not constexpr: a constexpr function's call outside a constant expression is a real call in what clang
        #  hands its optimizer -- folded only later, by inlining -- and that changed the code of play_wrestler_sound and
        #  set_up_match_objects (same size, other bytes; check-names). consteval makes clang put the number itself there, as
        #  the literal was. 2026-11-13, fieldfix)
        out[-5:-5] = ['consteval u32 hi(u32 address) { return (address + 0x8000) & 0xFFFF0000; }',
                      'consteval int lo(u32 address) { return (int) (address - hi(address)); }']
        out += ['#define GAME_TABLE(name, bytes) M2C_FIELD((bytes + field::var::hi(field::var::name)), P<field::var::name##_t>, field::var::lo(field::var::name))',
                '#define GAME_TABLE_AS(name, bytes, type) M2C_FIELD((bytes + field::var::hi(field::var::name)), P<type>, field::var::lo(field::var::name))',
                '#define GAME_ARRAY(name, bytes) M2C_FIELD((field::var::name + bytes), P<field::var::name##_t>, 0)',
                '#define GAME_ARRAY_AS(name, bytes, type) M2C_FIELD((field::var::name + bytes), P<type>, 0)']
    arrays = {a: v for a, v in data.get('_arrays', {}).items() if 'name' in v}
    if arrays:
        # (The named arrays' starts, and a field of the record `bytes` into one -- `bytes` a multiple of the record size --
        #  reached as the MIPS code's %hi / %lo pair of the field's address: WRESTLER_IN(wrestlers, temp_v0, superstar) is
        #  M2C_FIELD((temp_v0 + 0x80180000), P<u16>, -0x70), the very expression the conversion had. _IN_AT names a record
        #  further on, the pair's address being in it. 2026-11-13)
        out += ['', '// The game arrays of records the converted code reads (tools/lift/fields.json _arrays).',
                'namespace field {', 'namespace var {']
        if not variables:
            out += ['consteval u32 hi(u32 address) { return (address + 0x8000) & 0xFFFF0000; }',   # (consteval: as above)
                    'consteval int lo(u32 address) { return (int) (address - hi(address)); }']
        for a, v in sorted(arrays.items(), key=lambda x: int(x[0], 16)):
            out.append('constexpr u32 %s = %s;  // %s records' % (v['name'], a, v['kind']))
        out += ['}', '}  // namespace field', '']
        for kind in sorted({v['kind'] for v in arrays.values()}):
            macro, size = macros.get(kind) or FIELD_MACRO[kind], int(data[kind]['size'], 16)
            at = 'field::var::array + field::%s::name' % kind
            shift = 'field::var::array + (index) * 0x%X + field::%s::name' % (size, kind)
            for suffix, params, where, t in (('_IN', 'array, bytes, name', at, 'field::%s::name##_t' % kind),
                                             ('_IN_AS', 'array, bytes, name, type', at, 'type'),
                                             ('_IN_AT', 'array, bytes, index, name', shift, 'field::%s::name##_t' % kind),
                                             ('_IN_AT_AS', 'array, bytes, index, name, type', shift, 'type')):
                out.append('#define %s%s(%s) M2C_FIELD((bytes + field::var::hi(%s)), P<%s>, field::var::lo(%s))'
                           % (macro, suffix, params, where, t, where))
    return out + ['']


SECTIONS = {}


def function_sections(name):
    """The recompiler sections (main_1050, ovl_a .. ovl_e, rwblob) that list a function (RecompiledFuncs/recomp_overlays.inl); a
    mod's copy (modrep.py) is in its game function's. A function in none is the main segment's if its address is below the overlays."""
    if not SECTIONS:
        section = None
        for line in open(os.path.join(PORT, 'RecompiledFuncs', 'recomp_overlays.inl'), encoding='utf8'):
            m = re.match(r'static FuncEntry section_\d+_(\w+)_funcs\[\]', line)
            if m:
                section = m.group(1)
            m = re.search(r'\.func = (\w+),', line)
            if m and section:
                SECTIONS.setdefault(m.group(1), set()).add(section)
    if modrep.is_modrep(name):
        name = modrep.replaced(name)
    if name in SECTIONS:
        return SECTIONS[name]
    m = re.fullmatch(r'func_([0-9A-F]{8})', name)
    return {'main_1050'} if m and int(m.group(1), 16) < OVERLAY_MEMORY[0] else set()


def field_place_kind(sources, table, address, sections):
    """The kind a fields.json place (table: globals, arrays, pointer_arrays) gives at this address to a function of these sections,
    or None. Its entry's sections must hold all of the function's; an entry in the overlays' memory must name them."""
    entry = sources.get(table, {}).get(address)
    if not entry:
        return None
    allowed = entry[-1]
    if (allowed or OVERLAY_MEMORY[0] <= address < OVERLAY_MEMORY[1]) and not (sections and set(sections) <= allowed):
        return None
    return entry[0]


def field_array_place(sources, address, sections):
    """(kind, offset into the record) for an address inside one of a known array's records, or None."""
    for start, (kind, count, allowed) in sources.get('arrays', {}).items():
        size = sources['sizes'].get(kind)
        if size and start <= address < start + size * max(count, 1) and field_place_kind(sources, 'arrays', start, sections):
            return kind, (address - start) % size
    return None


def field_array_element(sources, address, sections, start_only=False):
    """The kind of the record at this address when it's one of a known array's records (its first only, with start_only: then
    an index is added to it)."""
    for start, (kind, count, allowed) in sources.get('arrays', {}).items():
        size = sources['sizes'].get(kind)
        if size and start <= address and (address - start) % size == 0 and (address == start or (not start_only and (address - start) // size < count)):
            if field_place_kind(sources, 'arrays', start, sections):
                return kind
    return None


def field_in_array(kinds, sources, address, sections):
    """For a word read in place: the kind it holds the address of when it's a field of a known array's record that does
    (points_to), e.g. a menu screen record's rows (0x80158360 + n x 0x138 + 0x14). Else None."""
    for start, (kind, count, allowed) in sources.get('arrays', {}).items():
        size = sources['sizes'].get(kind)
        if size and count and start <= address < start + size * count and field_place_kind(sources, 'arrays', start, sections):
            field = kinds.get(kind, {}).get((address - start) % size)
            if field and field.get('points_to'):
                return field['points_to']
    return None


def closing_paren(text, start):
    """The index of the parenthesis that closes the one at text[start]."""
    depth = 0
    for i in range(start, len(text)):
        depth += {'(': 1, ')': -1}.get(text[i], 0)
        if depth == 0:
            return i
    return -1


def split_top(expr, sep):
    """expr split at each sep outside parentheses."""
    parts, depth, start, i = [], 0, 0, 0
    while i < len(expr):
        if expr[i] == '(':
            depth += 1
        elif expr[i] == ')':
            depth -= 1
        elif depth == 0 and expr.startswith(sep, i):
            parts.append(expr[start:i])
            i += len(sep)
            start = i
            continue
        i += 1
    return parts + [expr[start:]]


FIELD_PARSED = {}      # function -> (its C++, assignments, assignment counts, field uses, scaled): field_kinds' reading of it
FIELD_ADDRESSED = {}   # function -> {stack slot: [(callee, argument index) its address is passed as] or None}: field_kinds
# (And a stack slot, `Ref<P<void>> sp28(frame.at(0x28));`: a value kept in the frame, a display list cursor handed to the
#  drawers by address. field_kinds types one only where its address goes nowhere else. 2026-11-12)
VARIABLE = r'(?:(?:temp|var|arg)\w*|sp[0-9A-F]+)'
FIELD_NAMES = {}       # function -> its names.json entry, for the parameters a stack slot's address is passed as (field_param_kinds)
FIELD_SIGNATURES = {}  # function -> its parameters' raw names in order (field_param_kinds)
NUMBER = r'0x[0-9A-F]+|\d+'
GLOBAL_WORD = r'\*\(P<(?:s32|u32|P<[\w<> ]+?>)> \)0x([0-9A-F]{8})'      # (read as a whole word: an address)


def field_source(expr, typed, kinds, sources, sections, scaled=None):
    """The kind of record the value of a C++ expression is the address of, or None. Only: a variable of a known kind; a pointer-wide
    field of a known kind that holds one (points_to); a global word that holds one; an array's record (its address, or its start
    plus an index times the record size); a known kind's address plus an index times its size, or plus whole records; a call of a
    function that returns one (FIELD_RETURNS). An index times a size may be a variable set once to that (scaled: {variable: size});
    a field may be read through a variable that walks an array at a place inside its records (scaled: {('at', variable): (kind,
    place)}). A cast to s32 / u32 / a pointer in front of a name, an address, a field or a sum in parentheses changes nothing."""
    e = expr.strip()
    m = re.fullmatch(r'\((?:s32|u32|P<[\w<> ]+?>) ?\) ?(%s|0x[0-9A-F]{8}|M2C_FIELD\(.*\))' % VARIABLE, e)
    if m and (not m.group(1).startswith('M2C_FIELD(') or closing_paren(m.group(1), len('M2C_FIELD')) == len(m.group(1)) - 1):
        e = m.group(1)
    m = re.match(r'(?:\((?:s32|u32|P<[\w<> ]+?>) ?\) ?)?\(', e)       # (a cast of a sum in parentheses, `(s32) (temp_s0 + 0x4C8)`)
    if m and closing_paren(e, m.end() - 1) == len(e) - 1:
        e = e[m.end():-1].strip()
    if re.fullmatch(VARIABLE, e):
        return typed.get(e)
    # (A variable followed, `*arg0`: m2c's way of writing the word at offset 0 of a typed pointer, the same as M2C_FIELD(arg0,
    #  P<..>, 0). The display list's cursor: `temp_v0 = *arg0;` is the next free command. 2026-11-12)
    m = re.fullmatch(r'\*(%s)' % VARIABLE, e)
    if m:
        field = kinds.get(typed.get(m.group(1)), {}).get(0)
        return field.get('points_to') if field else None
    m = re.fullmatch(r'0x([0-9A-F]{8})', e)
    if m:
        return field_array_element(sources, int(m.group(1), 16), sections)
    m = re.fullmatch(GLOBAL_WORD, e) or re.fullmatch(r'M2C_FIELD\(\(P<void> \)0x([0-9A-F]{8}), P<(?:s32|u32|P<[\w<> ]+>)> , (%s)\)' % NUMBER, e)
    if m:
        a = int(m.group(1), 16) + (int(m.group(2), 0) if m.lastindex == 2 else 0)     # (a word read as a field of an address)
        return (field_place_kind(sources, 'globals', a, sections) or field_place_kind(sources, 'pointer_arrays', a, sections)
                or field_in_array(kinds, sources, a, sections))
    m = re.fullmatch(r'M2C_FIELD\((.+), P<(?:s32|u32|P<[\w<> ]+>)> , (-?(?:%s))\)' % NUMBER, e)
    if m and len(split_top(m.group(1), ', ')) == 1 and closing_paren(e, len('M2C_FIELD')) == len(e) - 1:
        base, shift = field_source(m.group(1), typed, kinds, sources, sections, scaled), 0
        if not base and (scaled or {}).get(('at', m.group(1))):
            base, shift = scaled[('at', m.group(1))]
        field = kinds.get(base, {}).get(int(m.group(2), 0) + shift) if base else None
        return field.get('points_to') if field else None
    m = re.fullmatch(r'lift_call<[\w<> ]+>\(ctx, ::(\w+)\)\(.*\)', e)
    if m:
        return FIELD_RETURNS.get(m.group(1))
    parts = split_top(e, ' + ')
    # (Several whole numbers after the base are one, `(s32) (temp_s2_2 + 0x10 + 0x10)`: m2c's chains of a pointer stepped
    #  record by record. 2026-11-13)
    if len(parts) > 2 and all(re.fullmatch(NUMBER, p.strip()) for p in parts[1:]):
        parts = [parts[0], '0x%X' % sum(int(p.strip(), 0) for p in parts[1:])]
    if len(parts) != 2:
        return None
    for base, other in (parts, parts[::-1]):
        base, other = base.strip(), other.strip()
        m = re.fullmatch(r'\(?\w+ \* (%s)\)?' % NUMBER, other)
        step = int(m.group(1), 0) if m else (scaled or {}).get(other)
        m = re.fullmatch(NUMBER, other)
        whole = int(m.group(0), 0) if m else None
        a = re.fullmatch(r'(?:\(P<void> \))?0x([0-9A-F]{8})', base)
        p = re.fullmatch(GLOBAL_WORD, base)
        if a and step:
            kind = field_array_element(sources, int(a.group(1), 16), sections, start_only=True)
        elif p and step:
            kind = (field_place_kind(sources, 'pointer_arrays', int(p.group(1), 16), sections)
                    or field_in_array(kinds, sources, int(p.group(1), 16), sections))
        elif re.fullmatch(VARIABLE, base) and (step or whole):
            kind = typed.get(base)
        elif (step or whole) and base.startswith('M2C_FIELD(') and closing_paren(base, len('M2C_FIELD')) == len(base) - 1:
            # (A field that holds a known kind's address, plus whole records: `M2C_FIELD(fx, P<s32> , 0x20) + (i * 0x28)`,
            #  the reverb's i-th section. 2026-11-13)
            kind = field_source(base, typed, kinds, sources, sections, scaled)
        else:
            continue
        size = sources['sizes'].get(kind)
        if kind and size and (step == size or (whole and whole % size == 0)):
            return kind
    return None


def field_width(t):
    """Bytes an access of that type reads: a pointer or an M2C_UNK32 is a word."""
    return {'s8': 1, 'u8': 1, 's16': 2, 'u16': 2, 's64': 8, 'u64': 8, 'f64': 8}.get(t, 4)


def field_kinds(name, cpp, info, kinds, sources):
    """The variables of a function whose kind of record is established: {variable: kind}. Only: a parameter the naming track named
    `step` in the match code (a state step's argument: the step context); a parameter fields.json _params gives a kind, with its
    evidence (a handler table's functions); a parameter every call passes one kind for (FIELD_PARAMS); a parameter named for one
    of a kind's roles (_roles) whose accesses fit the record; a variable every assignment of which is a value of one kind
    (field_source), or adds whole records to it. A variable then used at an offset outside its kind's size isn't typed, nor
    anything typed through it. (2026-11-10; all but the first rule 2026-11-11)"""
    sections = function_sections(name)
    if FIELD_PARSED.get(name, (None,))[0] is not cpp:
        assigns, counts = {}, {}
        # (Not `*temp_v0 = x;`: that writes the word it points at, not the variable. 2026-11-12)
        for m in re.finditer(r'(?<![\w.>*&])((?:temp|var)_\w+|sp[0-9A-F]+) (=|\+=|-=) ([^;]*);', cpp):
            assigns.setdefault(m.group(1), []).append((m.group(2), m.group(3).strip()))
        # (Where a stack slot's address goes: (callee, argument index) for each `&sp28` that is a call's argument; None for a
        #  slot whose address is also used some other way.)
        addressed = {}
        for m in re.finditer(r'lift_call<[\w<> ]+>\(ctx, ::(\w+)\)\(', cpp):
            end = closing_paren(cpp, m.end() - 1)
            for i, arg in enumerate(split_top(cpp[m.end():end], ', ')):
                a = re.fullmatch(r'(?:\(s32\) )?&(sp[0-9A-F]+)', arg.strip())
                if a:
                    addressed.setdefault(a.group(1), []).append((m.group(1), i))
        for slot in set(re.findall(r'&(sp[0-9A-F]+)\b', cpp)):
            if len(re.findall(r'&%s\b' % slot, cpp)) != len(addressed.get(slot, [])):
                addressed[slot] = None
        FIELD_ADDRESSED[name] = addressed
        # (Every assignment to a name, compound ones and ++ / -- too.)
        for m in re.finditer(r'(?<![\w.>*])(\w+)\s*(?:[-+*/&|^]|<<|>>)?=(?!=)|\b(\w+)\s*(?:\+\+|--)|(?:\+\+|--)\s*(\w+)\b', cpp):
            v = m.group(1) or m.group(2) or m.group(3)
            counts[v] = counts.get(v, 0) + 1
        uses = [(m.group(1), int(m.group(3), 0), m.group(2)) for m in re.finditer(r'M2C_FIELD\((%s), P<(.+?)> , (-?(?:%s))\)' % (VARIABLE, NUMBER), cpp)]
        # (And `*arg0`, a typed pointer followed: an access at offset 0 of the type it was declared to point at. m2c writes a
        #  multiplication with spaces round the `*`, so a `*` right before a name is always this. 2026-11-12)
        pointee = {m.group(2): m.group(1) for m in re.finditer(r'\bP<([\w<> ]+)> (%s)(?=[,);])' % VARIABLE, cpp)}
        pointee.update({m.group(2): m.group(1) for m in re.finditer(r'\bRef<P<([\w<> ]+)>> (sp[0-9A-F]+)\(', cpp)})
        uses += [(m.group(1), 0, pointee[m.group(1)]) for m in re.finditer(r'(?<![\w)\]])\*(%s)\b' % VARIABLE, cpp)
                 if pointee.get(m.group(1), 'void') != 'void']
        # (An index times a size kept in a variable of its own, `temp_v0 = arg1 * 0x358;`, or one that only ever counts in
        #  steps of that size from 0: `var_s1 = 0; .. var_s1 += 0x13C;`.)
        scaled = {}
        for v, lst in assigns.items():
            steps = set()
            for op, rhs in lst:
                # (A cast in front of the index, `(s32) (s16) arg0 * 0x8C`, scales it the same. 2026-11-13)
                m = re.fullmatch(r'\(?(?:\((?:s8|u8|s16|u16|s32|u32)\) )*\w+ \* (%s)\)?' % NUMBER, rhs) if op == '=' else None
                c = re.fullmatch(NUMBER, rhs) if op == '+=' else re.fullmatch(r'%s \+ (%s)' % (re.escape(v), NUMBER), rhs)
                # (m2c writes a zero start as NULL where the variable is a pointer: `var_s2_2 = NULL; .. += 0xB4`. 2026-11-13)
                steps.add(int(m.group(1), 0) if m else int(c.group(c.lastindex or 0), 0) if c else 0 if (op, rhs) in (('=', '0'), ('=', 'NULL')) else None)
            sizes = steps - {0}
            if counts.get(v) == len(lst) and len(sizes) == 1 and None not in steps and any(op == '=' for op, _ in lst):
                scaled[v] = sizes.pop()
        # (And a variable that walks an array's records at a place inside them, m2c having moved part of the field offsets into
        #  it: run_menu_frame's `var_s6 = (P<void> )0x801583BC; .. var_s6 += 0x138;`, 0x5C into each screen record. Kept as
        #  ('at', variable): (kind, place), only to follow a field of it that holds a known kind's address; it isn't named.)
        for v, lst in assigns.items():
            starts, steps = set(), []
            for op, rhs in lst:
                m = re.fullmatch(r'(?:\(P<void> \))?0x([0-9A-F]{8})', rhs) if op == '=' else None
                c = re.fullmatch(NUMBER, rhs) if op == '+=' else None
                if m:
                    starts.add(field_array_place(sources, int(m.group(1), 16), sections))
                else:
                    steps.append(int(c.group(0), 0) if c else None)
            if counts.get(v) == len(lst) and len(starts) == 1 and None not in starts and None not in steps:
                kind, shift = next(iter(starts))
                if all(x % sources['sizes'][kind] == 0 for x in steps):
                    scaled[('at', v)] = (kind, shift)
        FIELD_PARSED[name] = (cpp, assigns, counts, uses, scaled)
    _, assigns, counts, uses, scaled = FIELD_PARSED[name]
    plain = {v for v, lst in assigns.items() if counts.get(v, 0) == len(lst)}
    fixed = {}
    # (The match code's only: ease_light_colour in ovl_b has a `step` that's an amount, 2026-11-11.)
    for raw, nice in (info or {}).get('vars', {}).items():
        if nice == 'step' and re.fullmatch(r'arg\d', raw) and 'StepContext' in kinds and not counts.get(raw) and sections == {'ovl_e'}:
            fixed[raw] = 'StepContext'
    for raw, kind in sources.get('params', {}).get(name, {}).items():
        if raw not in fixed and not counts.get(raw):
            fixed[raw] = kind
    for raw, kind in FIELD_PARAMS.get(name, {}).items():
        if raw not in fixed and not counts.get(raw):
            fixed[raw] = kind
    # (A parameter the naming track gave a role name of a kind (fields.json _roles: the wrestler's `man`, `attacker` ..), in a
    #  function of the role's sections, never assigned, whose accesses all fit the record: every one inside it, every one at a
    #  named field of that field's width, at least two at named fields. The name is a reading of the code; the fit is what makes
    #  it more than a name. 2026-11-11)
    # (A kind whose records come in runs ("elements": the vertices, a display list's commands) fits by the place in its record,
    #  the offset modulo the size: vertex 1's x at +0x10 is a Vertex x. 2026-11-12)
    for raw, nice in (info or {}).get('vars', {}).items():
        if raw in fixed or not re.fullmatch(r'arg\d+', raw) or counts.get(raw) or not sections:
            continue
        # (Its accesses are its own and those of its copies, `sp34 = arg0;`: a variable or a stack slot set once, to it, whose
        #  address isn't taken. A role of a kind with one field may ask for fewer than two at named fields: _roles' min_named.
        #  2026-11-12)
        copies, grow = set(), True
        while grow:
            more = {v for v, lst in assigns.items() if len(lst) == 1 and lst[0][0] == '=' and (lst[0][1] == raw or lst[0][1] in copies)
                    and counts.get(v) == 1 and v not in FIELD_ADDRESSED.get(name, {})}
            grow = bool(more - copies)
            copies |= more
        for kind, allowed, least in sources.get('roles', {}).get(nice, []):
            size = sources['sizes'].get(kind)
            if not size or not set(sections) <= allowed:
                continue
            runs = kind in sources['elements']
            mine = [(off if not runs else off % size, t, off) for v, off, t in uses if v == raw or v in copies]
            at_named = [(off, t) for off, t, _ in mine if off in kinds[kind]]
            if (all(0 <= whole and (runs or whole < size) for _, _, whole in mine) and len(at_named) >= least
                    and all(field_width(t) == field_width(kinds[kind][off]['type']) for off, t in at_named)):
                fixed[raw] = kind
                break
    def own_kind(var, typed):
        # (The kind every assignment of var gives, its own steps (var += n, var = var - n) whole records of it; else None.)
        found, steps = set(), []
        for op, rhs in assigns[var]:
            m = re.fullmatch(NUMBER, rhs) if op != '=' else re.fullmatch(r'%s [+-] (%s)' % (re.escape(var), NUMBER), rhs)
            if op != '=' or m:
                steps.append(int(m.group(m.lastindex or 0), 0) if m else None)
            else:
                found.add(field_source(rhs, typed, kinds, sources, sections, scaled))
        if len(found) != 1 or None in found:
            return None
        kind = found.pop()
        size = sources['sizes'].get(kind)
        if steps and (not size or any(s is None or s % size for s in steps)):
            return None
        return kind
    banned = set()
    while True:
        typed = {v: k for v, k in fixed.items() if v not in banned}
        changed = True
        while changed:
            changed = False
            for var in sorted(plain - banned - set(typed)):
                kind = own_kind(var, typed)
                if kind:
                    typed[var] = kind
                    changed = True
            # (Then the loops: variables set from each other round a loop, `var_s1 = *arg0;` before it and `var_s1 = var_s1_2 +
            #  8;` in it, with var_s1_2 set from var_s1 .. -- none is typed first, so none is typed above. Each kind one of
            #  their assignments gives is tried for all the rest at once: the largest set of them whose every assignment gives
            #  that kind when they all are, kept only where it's reached from a value of that kind found above. 2026-11-12)
            rest = plain - banned - set(typed)
            for kind in sorted({field_source(rhs, typed, kinds, sources, sections, scaled) for v in rest for op, rhs in assigns[v]
                                if op == '='} - {None}):
                assumed = plain - banned - set(typed)
                while assumed:
                    view = dict(typed, **{v: kind for v in assumed})
                    drop = {v for v in assumed if own_kind(v, view) != kind}
                    if not drop:
                        break
                    assumed -= drop
                reached, grow = set(), True
                while grow:
                    grow = False
                    view = dict(typed, **{v: kind for v in reached})
                    for v in sorted(assumed - reached):
                        if any(op == '=' and not re.fullmatch(r'%s [+-] (%s)' % (re.escape(v), NUMBER), rhs)
                               and field_source(rhs, view, kinds, sources, sections, scaled) == kind for op, rhs in assigns[v]):
                            reached.add(v)
                            grow = True
                for v in reached:
                    typed[v] = kind
                    changed = True
        bad = set()
        # (A stack slot whose address is passed on holds what the callee leaves there: typed only when every such callee
        #  takes it as a parameter named for a role -- or given -- a kind whose word at 0 holds this kind's address (a
        #  display list cursor for a Gfx). 2026-11-12)
        for slot, calls in FIELD_ADDRESSED.get(name, {}).items():
            kind = typed.get(slot)
            if kind and (calls is None or not all(field_cursor_param(callee, i, kind, kinds, sources) for callee, i in calls)):
                bad.add(slot)
        for var, off, _ in uses:
            size = sources['sizes'].get(typed.get(var))
            # (A kind in runs may be reached before the record too: a display list cursor moved on, `var_v0 += 8`, writes the
            #  command before it at -8 / -4. 2026-11-12)
            if size and typed[var] not in sources['elements'] and not 0 <= off < size:
                bad.add(var)
        if not bad:
            return typed
        banned |= bad


def field_variables(name, cpp, sources):
    """A game variable read or written at its fixed address, `*(P<s32> )0x800A9950`, written with its name (fields.json
    _variables): GAME_VAR(match_rules), or GAME_VAR_AS(match_rules, u16) at another width -- the same tokens with the name for
    the type and the address. Only at the variable's own address; one in the overlays' memory only in a function of the
    sections its entry gives, as field_place_kind. (2026-11-12)"""
    variables = sources.get('variables')
    if not variables:
        return cpp
    sections = function_sections(name)
    def rewrite(m):
        a = int(m.group(2), 16)
        v = variables.get(a)
        if not v or ((v[2] or OVERLAY_MEMORY[0] <= a < OVERLAY_MEMORY[1]) and not (sections and set(sections) <= v[2])):
            return m.group(0)
        FIELD_STATS['variables'] = FIELD_STATS.get('variables', 0) + 1
        if m.group(1) == v[1]:
            return 'GAME_VAR(%s)' % v[0]
        return 'GAME_VAR_AS(%s, %s)' % (v[0], m.group(1))
    cpp = re.sub(r'(?<![\w)])\*\(P<([\w<> ]+?)> \)0x(8[0-9A-F]{7})\b', rewrite, cpp)
    # (And an entry of a table at a variable's address, `M2C_FIELD((var_a1 + 0x80540000), P<s32> , -0x4360)`: the MIPS code's
    #  %hi / %lo pair, an index in bytes plus the high half of the address, the low half the offset. Written
    #  GAME_TABLE(match_slot_superstars, var_a1) -- the same tokens, with field::var::hi() / lo() of the variable's address for
    #  the two numbers -- only where they are exactly that address's halves (the low one signed, -0x8000 .. 0x7FFF) and the
    #  index has no comma outside parentheses (it becomes a macro argument). As GAME_VAR, one in the overlays' memory only in
    #  the sections its entry gives. 2026-11-13)
    start = 0
    while True:
        i = cpp.find('M2C_FIELD((', start)
        if i < 0:
            return cpp
        start = i + 1
        end = closing_paren(cpp, i + len('M2C_FIELD'))
        args = split_top(cpp[i + len('M2C_FIELD('):end], ', ')
        if cpp[i - 1:i] == '&' or len(args) != 3 or closing_paren(args[0], 0) != len(args[0]) - 1:
            continue
        parts = split_top(args[0][1:-1], ' + ')
        ptr = re.fullmatch(r'P<([\w<> ]+?)> ', args[1])
        off = re.fullmatch(r'-?(?:%s)' % NUMBER, args[2])
        high = re.fullmatch(r'0x(8[0-9A-F]{3}0000)', parts[-1])
        # (Or the whole address first and nothing after, `M2C_FIELD((0x80106620 + temp_v1), P<s32> , 0)`: an entry of the
        #  table at a variable's address too, GAME_ARRAY(entrance_actor_phases, temp_v1). 2026-11-13)
        whole = re.fullmatch(r'0x(8[0-9A-F]{7})', parts[0]) if len(parts) >= 2 and args[2] == '0' else None
        if whole and ptr:
            a, index = int(whole.group(1), 16), ' + '.join(parts[1:])
            v = variables.get(a)
            if (v and len(split_top(index, ', ')) == 1
                    and not ((v[2] or OVERLAY_MEMORY[0] <= a < OVERLAY_MEMORY[1]) and not (sections and set(sections) <= v[2]))):
                FIELD_STATS['tables'] = FIELD_STATS.get('tables', 0) + 1
                done = ('GAME_ARRAY(%s, %s)' % (v[0], index) if ptr.group(1) == v[1] else
                        'GAME_ARRAY_AS(%s, %s, %s)' % (v[0], index, ptr.group(1)))
                cpp = cpp[:i] + done + cpp[end + 1:]
            continue
        if len(parts) < 2 or not ptr or not off or not high:
            continue
        a = int(high.group(1), 16) + int(off.group(0), 0)
        index = ' + '.join(parts[:-1])
        v = variables.get(a)
        if (not v or ((a + 0x8000) & 0xFFFF0000) != int(high.group(1), 16) or len(split_top(index, ', ')) != 1
                or ((v[2] or OVERLAY_MEMORY[0] <= a < OVERLAY_MEMORY[1]) and not (sections and set(sections) <= v[2]))):
            continue
        FIELD_STATS['tables'] = FIELD_STATS.get('tables', 0) + 1
        done = ('GAME_TABLE(%s, %s)' % (v[0], index) if ptr.group(1) == v[1] else
                'GAME_TABLE_AS(%s, %s, %s)' % (v[0], index, ptr.group(1)))
        cpp = cpp[:i] + done + cpp[end + 1:]


# (convlits 2026-11-22, the readable track) What is left of a named variable's or array's address after the rewrites above -- the
#  address itself as a number: passed to a function, put in a pointer, `previous = (P<M2C_UNK> )0x80196000;`, the base of an
#  M2C_FIELD -- is written as its constant, field::var::name: a constexpr u32, the very type the literal had (a hex literal over
#  INT_MAX is an unsigned int), with the same value, so the compiler sees the same program (check-names). Only at a variable's or
#  a named array's own address, with the same rule for the overlays' memory as GAME_VAR; never a number with its low half 0
#  (that is the MIPS code's %hi part of some address, not this one).
ADDRESS_LITERAL = re.compile(r'(?<![\w.])0[xX](8[0-9A-Fa-f]{7})[uU]?\b')


def named_addresses(name, cpp, sources):
    """A named variable's or array's address left as a number in a function's C++, written field::var::name (see above)."""
    variables, arrays = sources.get('variables') or {}, sources.get('array_names') or {}
    if not variables and not arrays:
        return cpp
    sections = function_sections(name)
    def rewrite(m):
        a = int(m.group(1), 16)
        if a & 0xFFFF == 0:
            return m.group(0)
        if a in variables:
            shown, given = variables[a][0], variables[a][2]
        elif a in arrays:
            shown, given = arrays[a], sources['arrays'].get(a, (None, None, frozenset()))[-1]
        else:
            return m.group(0)
        if (given or OVERLAY_MEMORY[0] <= a < OVERLAY_MEMORY[1]) and not (sections and set(sections) <= given):
            return m.group(0)
        FIELD_STATS['addresses'] = FIELD_STATS.get('addresses', 0) + 1
        return 'field::var::%s' % shown
    # (And an address made physical, `palette - 0x80000000` -- libultra's K0_TO_PHYS, what osVirtualToPhysical does for main
    #  memory: the base of KSEG0 taken off, written `palette - field::var::kseg0`, a constexpr u32 of the literal's value and
    #  type. Only a subtraction: 0x80000000 or'ed in or tested is a sign bit, not this.)
    cpp = re.sub(r'(?<=[\w)\]]) - 0x80000000(?![\w.])', ' - field::var::kseg0', cpp)
    return ADDRESS_LITERAL.sub(rewrite, cpp)


# (convlits 2026-11-22, the readable track) The MIPS code's %hi part of an address left in a register on its own, a statement
#  `lift_out_v0 = 0x800B0000;` (the register a caller may still read; m2c writes the lui's value where it was), is the high
#  half of the address the lui was for: the fixed place the function reaches with it next -- the first named one with that
#  high half after the statement (GAME_VAR(x), GAME_TABLE(x, ..), field::var::x ..), or else the last one before it. Written
#  field::var::hi(field::var::x): consteval, so the number itself, of the literal's type (u32), is what the compiler sees
#  (check-names). Left as it was when no named place in the function has that high half.
HIGH_HALF_ALONE = re.compile(r'(?<![\w.])= 0x(8[0-9A-F]{3}0000);')
NAMED_PLACE = re.compile(r'\b(?:GAME_VAR(?:_AS)?\(|GAME_TABLE(?:_AS)?\(|GAME_ARRAY(?:_AS)?\(|field::var::)([a-z][a-z0-9_]*)\b')


def named_high_halves(cpp, sources):
    """A %hi part alone in a statement, written field::var::hi(field::var::x) for the named place x it is for (see above)."""
    if 'field::var::' not in cpp and 'GAME_' not in cpp:
        return cpp
    address = {v[0]: a for a, v in (sources.get('variables') or {}).items()}
    address.update({nm: a for a, nm in (sources.get('array_names') or {}).items()})
    places = [(m.start(), m.group(1)) for m in NAMED_PLACE.finditer(cpp) if m.group(1) in address and m.group(1) not in ('hi', 'lo')]
    if not places:
        return cpp
    def rewrite(m):
        high = int(m.group(1), 16)
        same = [(at, nm) for at, nm in places if (address[nm] + 0x8000) & 0xFFFF0000 == high]
        after = [nm for at, nm in same if at > m.start()]
        before = [nm for at, nm in same if at < m.start()]
        chosen = after[0] if after else before[-1] if before else None
        if not chosen:
            return m.group(0)
        FIELD_STATS['high_halves'] = FIELD_STATS.get('high_halves', 0) + 1
        return '= field::var::hi(field::var::%s);' % chosen
    return HIGH_HALF_ALONE.sub(rewrite, cpp)


def field_array_rows(name, cpp, kinds, sources):
    """A named field of a known array's record reached by an index and the MIPS code's %hi / %lo pair,
    `M2C_FIELD((temp_v0 + 0x80180000), P<u16> , -0x70)`: written WRESTLER_IN(wrestlers, temp_v0, superstar) -- the same tokens, with
    field::var::hi() / lo() of the array's start plus the field's offset for the two numbers. Only where: the array has a name
    (fields.json _arrays "name") and is one the function's sections may name (field_array_place); the pair is exactly that
    address's halves; the index is a whole number of records -- a variable set once to n x the size or stepping by it from 0
    (field_kinds' scaled), or `x * size` itself with x a name, a fixed-address word or a macro or field read; the index has no
    comma outside parentheses; and the field there is named at the access's width. (2026-11-13)"""
    names = sources.get('array_names')
    if not names:
        return cpp
    sections = function_sections(name)
    scaled = FIELD_PARSED.get(name, (None,) * 5)[4] or {}
    start = 0
    while True:
        i = cpp.find('M2C_FIELD((', start)
        if i < 0:
            return cpp
        start = i + 1
        end = closing_paren(cpp, i + len('M2C_FIELD'))
        args = split_top(cpp[i + len('M2C_FIELD('):end], ', ')
        if cpp[i - 1:i] == '&' or len(args) != 3 or closing_paren(args[0], 0) != len(args[0]) - 1:
            continue
        parts = split_top(args[0][1:-1], ' + ')
        ptr = re.fullmatch(r'P<([\w<> ]+?)> ', args[1])
        off = re.fullmatch(r'-?(?:%s)' % NUMBER, args[2])
        high = re.fullmatch(r'0x(8[0-9A-F]{3}0000)', parts[-1])
        if len(parts) != 2 or not ptr or not off or not high:
            continue
        a = int(high.group(1), 16) + int(off.group(0), 0)
        index = parts[0]
        place = field_array_place(sources, a, sections) if ((a + 0x8000) & 0xFFFF0000) == int(high.group(1), 16) else None
        if not place or len(split_top(index, ', ')) != 1:
            continue
        kind, at = place
        size = sources['sizes'][kind]
        first = next(s for s, (k, count, allowed) in sources['arrays'].items()
                     if k == kind and s <= a < s + size * max(count, 1) and field_place_kind(sources, 'arrays', s, sections))
        if first not in names:
            continue
        inner = index[1:-1] if index.startswith('(') and closing_paren(index, 0) == len(index) - 1 else index
        times = split_top(inner, ' * ')
        whole = scaled.get(index) == size or (
            len(times) == 2 and re.fullmatch(NUMBER, times[1]) and int(times[1], 0) == size
            and (re.fullmatch(r'\w+|\*\(P<[\w<> ]+?> \)0x[0-9A-F]{8}', times[0])
                 or (re.match(r'[A-Z][A-Z0-9_]*\(', times[0]) and closing_paren(times[0], times[0].index('(')) == len(times[0]) - 1)))
        field = kinds.get(kind, {}).get(at)
        if not whole or not field or field_width(field['type']) != field_width(ptr.group(1)):
            continue
        record = (a - first) // size
        macro = (sources['macros'].get(kind) or FIELD_MACRO[kind]) + '_IN' + ('_AT' if record else '')
        held = [names[first], index] + (['%d' % record] if record else []) + [field['name']]
        if ptr.group(1) != field['type']:
            macro, held = macro + '_AS', held + [ptr.group(1)]
        FIELD_STATS['accesses'] += 1
        FIELD_STATS['functions'].add(name)
        FIELD_STATS.setdefault('use_by', {}).setdefault((kind, at, args[1]), set()).add(name)
        cpp = cpp[:i] + '%s(%s)' % (macro, ', '.join(held)) + cpp[end + 1:]


def field_cursor_param(callee, index, kind, kinds, sources):
    """Whether that parameter of callee is the address of a word holding a `kind` record's address: named for a role (or given
    by _params / every call) of a kind whose field at 0 points to `kind`. (2026-11-12)"""
    params = FIELD_SIGNATURES.get(callee, [])
    if index >= len(params):
        return False
    raw = params[index]
    cursors = {k for k, fields in kinds.items() if fields.get(0, {}).get('points_to') == kind}
    nice = FIELD_NAMES.get(callee, {}).get('vars', {}).get(raw)
    return bool(cursors & ({role[0] for role in sources.get('roles', {}).get(nice, [])} | {sources.get('params', {}).get(callee, {}).get(raw),
                                                                                       FIELD_PARAMS.get(callee, {}).get(raw)}))


def field_walkers(name, typed, kinds, sources, sections):
    """The variables of a function that walk a known kind's records at a named field inside them: {variable: (kind, place)},
    place the field's offset in its record. Only temp_ / var_ variables every assignment of which is seen, each set to a typed
    variable plus a number, `var_s0 = row + 0x4A;`, to a fixed address inside a known array's record, `var_s6 = (P<void>
    )0x801583BC;`, or to another such walker plus a number (m2c's chains of one pointer, `var_a2 = temp_a2_4 + 0x10;`), and
    otherwise only stepped by whole records (`var_s0 += 0x88;`) -- every assignment giving the same place, and the place a
    named field of the record (the anchor its accesses are named from). (2026-11-13)"""
    _, assigns, counts, uses, scaled = FIELD_PARSED[name]
    cands = {v: lst for v, lst in assigns.items()
             if v not in typed and re.fullmatch(r'(?:temp|var)_\w+', v) and counts.get(v) == len(lst)}
    def origin(rhs, known):
        # ((kind, place) an assignment gives, None when it isn't one of the three forms, 'wait' when it is a walker not
        #  placed yet)
        m = re.fullmatch(r'(?:\((?:s32|P<void>)\) )?(%s)(?: \+ (%s))?' % (VARIABLE, NUMBER), rhs)
        a = re.fullmatch(r'(?:\(P<[\w<> ]+?> \))?0x(8[0-9A-F]{7})', rhs)
        if m and typed.get(m.group(1)) and m.group(2):
            kind, place = typed[m.group(1)], int(m.group(2), 0)
        elif m and m.group(1) in known:
            kind, place = known[m.group(1)][0], known[m.group(1)][1] + int(m.group(2) or '0', 0)
        elif m and m.group(1) in cands:
            return 'wait'
        elif a and field_array_place(sources, int(a.group(1), 16), sections):
            kind, place = field_array_place(sources, int(a.group(1), 16), sections)
        else:
            return None
        size = sources['sizes'].get(kind)
        return (kind, place % size) if size else None
    # (First every place an assignment gives, walkers from walkers too; then drop the variables whose assignments don't all
    #  agree, until none is dropped.)
    known = {}
    for _ in range(len(cands) + 1):
        grew = False
        for v, lst in cands.items():
            if v in known:
                continue
            for op, rhs in lst:
                o = origin(rhs, known) if op == '=' else None
                if o and o != 'wait':
                    known[v] = o
                    grew = True
                    break
        if not grew:
            break
    while True:
        bad = set()
        for v, (kind, place) in known.items():
            size = sources['sizes'][kind]
            for op, rhs in cands[v]:
                if op == '=':
                    if origin(rhs, known) != (kind, place):
                        bad.add(v)
                elif not (re.fullmatch(NUMBER, rhs) and int(rhs, 0) % size == 0):
                    bad.add(v)
            if not kinds.get(kind, {}).get(place):
                bad.add(v)
        if not bad:
            return known
        for v in bad:
            del known[v]


def fielded_cpp(name, cpp, info, kinds, sources):
    """Raw field accesses on records whose kind is established (field_kinds), written with the field's name; and those on a global
    word that holds a record's address, or on an array's record, in place; and those on a named field that holds a record's
    address (points_to), `M2C_FIELD(WIDGET(w, vertices), P<s16> , 2)`. Everything else is left as it was. Address-of uses
    (&M2C_FIELD) are never rewritten. A kind whose records come in runs ("elements" in fields.json: the vertices) names an
    access past its first record by the record's index: VERTEX_AT(v, 1, x). (2026-11-10; the last two 2026-11-11)"""
    if not kinds or name in FIELD_SKIP:
        return cpp
    typed = field_kinds(name, cpp, info, kinds, sources)
    sections = function_sections(name)
    FIELD_STATS['typed_vars'] += len(typed)
    walkers = field_walkers(name, typed, kinds, sources, sections)
    by_macro = {sources['macros'].get(k) or FIELD_MACRO[k]: k for k in kinds}
    def named(kind, base, ptr, off, raw):
        size = sources['sizes'].get(kind)
        index, at = divmod(off, size) if size and kind in sources['elements'] else (0, off)
        field = kinds.get(kind, {}).get(at) if kind and (not size or at < size) else None
        if not field:
            if kind:
                FIELD_STATS['kept_raw'] += 1
                FIELD_STATS.setdefault('raw_by', {}).setdefault((kind, off, ptr), set()).add(name)
            return raw
        FIELD_STATS['accesses'] += 1
        FIELD_STATS['functions'].add(name)
        FIELD_STATS.setdefault('use_by', {}).setdefault((kind, at, ptr), set()).add(name)
        macro = sources['macros'].get(kind) or FIELD_MACRO[kind]
        if index:
            macro, base = macro + '_AT', '%s, %d' % (base, index)
        if ptr[2:-1] == field['type']:
            return '%s(%s, %s)' % (macro, base, field['name'])
        return '%s_AS(%s, %s, %s)' % (macro, base, field['name'], ptr[2:-1])
    def walked(base, ptr, off, raw):
        # (A walker's access: the field at its place plus the offset, in the record it is in -- the anchor's own, or with
        #  _VIA_AT one so many records on -- named only when it is a named field at the access's width.)
        kind, place = walkers[base]
        size = sources['sizes'][kind]
        shift, at = divmod(place + off, size)
        field, anchor = kinds[kind].get(at), kinds[kind][place % size]
        if not field or field_width(field['type']) != field_width(ptr[2:-1]):
            return raw
        FIELD_STATS['accesses'] += 1
        FIELD_STATS['functions'].add(name)
        FIELD_STATS.setdefault('use_by', {}).setdefault((kind, at, ptr), set()).add(name)
        macro = (sources['macros'].get(kind) or FIELD_MACRO[kind]) + '_VIA'
        held = [base, anchor['name']]
        if shift - place // size:
            macro, held = macro + '_AT', held + ['%d' % (shift - place // size)]
        held.append(field['name'])
        if ptr[2:-1] != field['type']:
            macro, held = macro + '_AS', held + [ptr[2:-1]]
        return '%s(%s)' % (macro, ', '.join(held))
    def rewrite(m):
        base, ptr, off = m.group(1), m.group(2), int(m.group(3), 0)
        kind = typed.get(base)
        g = re.fullmatch(GLOBAL_WORD, base)
        if g:
            a = int(g.group(1), 16)
            kind = field_place_kind(sources, 'globals', a, sections) or field_place_kind(sources, 'pointer_arrays', a, sections)
        c = re.fullmatch(r'\(P<void> \)0x([0-9A-F]{8})', base)
        if c:
            kind = field_array_element(sources, int(c.group(1), 16), sections)
        return named(kind, base, ptr, off, m.group(0))
    cpp = re.sub(r'(?<!&)M2C_FIELD\((\w+|\*\(P<[\w<> ]+?> \)0x[0-9A-F]{8}|\(P<void> \)0x[0-9A-F]{8}), (P<.+?>) , (%s)\)' % NUMBER, rewrite, cpp)
    # (And the walkers' accesses, a negative offset too: a walker's anchor is often inside the record. 2026-11-13)
    if walkers:
        cpp = re.sub(r'(?<![&\w])M2C_FIELD\((\w+), (P<[\w<> ]+?>) , (-?(?:%s))\)' % NUMBER,
                     lambda m: walked(m.group(1), m.group(2), int(m.group(3), 0), m.group(0)) if m.group(1) in walkers else m.group(0), cpp)
    # (Then the accesses through a named field that holds a known kind's address, outermost last.)
    start = 0
    while True:
        i = cpp.find('M2C_FIELD(', start)
        if i < 0:
            return named_high_halves(named_addresses(name, field_variables(name, field_array_rows(name, cpp, kinds, sources), sources), sources), sources)
        start = i + 1
        end = closing_paren(cpp, i + len('M2C_FIELD'))
        args = split_top(cpp[i + len('M2C_FIELD('):end], ', ')
        inner = re.fullmatch(r'([A-Z][A-Z_]*?)(_AS)?\((.+)\)', args[0]) if len(args) == 3 and cpp[i - 1:i] != '&' else None
        ptr = re.fullmatch(r'(P<.+>) ', args[1]) if inner else None
        off = re.fullmatch(NUMBER, args[2].strip()) if ptr else None
        if not off or inner.group(1) not in by_macro or closing_paren(args[0], len(inner.group(1) + (inner.group(2) or ''))) != len(args[0]) - 1:
            continue
        held = split_top(inner.group(3), ', ')
        field = next((f for f in kinds[by_macro[inner.group(1)]].values() if len(held) > 1 and f['name'] == held[1].strip()), None)
        if field and field.get('points_to') and (inner.group(2) is None or re.fullmatch(r'(?:s32|u32|P<[\w<> ]+>)', held[-1].strip())):
            done = named(field['points_to'], args[0], ptr.group(1), int(off.group(0), 0), None)
            if done:
                cpp = cpp[:i] + done + cpp[end + 1:]
                # (And again from the start: the access this one is inside, passed over while its address was still a raw
                #  M2C_FIELD, may now be a field of the kind this one points to -- a row's style's points,
                #  M2C_FIELD(ROW_STYLE(MENU_ROW(row, style), points), P<s16> , 2). Each pass names one more, so it ends.
                #  2026-11-13)
                start = 0


def field_param_kinds(cands, names, kinds, sources):
    """Fills FIELD_PARAMS and FIELD_RETURNS from all the converted functions. A parameter is of a kind when the function has
    callers in the N64 call graph (to_asm.py), all of them converted, each of their calls of it is seen (as many lift_calls as
    the N64 code has jals to it, with as many arguments as it has parameters), and every call passes a value of that kind there
    (field_source). A function returns a kind when every return gives one. A function called through a pointer (a state step, a
    page's run) has no callers seen, and gets nothing this way. Repeated while it types more. (2026-11-11)"""
    FIELD_PARAMS.clear()
    FIELD_RETURNS.clear()
    FIELD_NAMES.clear()
    FIELD_SIGNATURES.clear()
    if not kinds:
        return
    FIELD_NAMES.update(names)
    FIELD_RETURNS.update(sources.get('returns', {}))
    functions = to_asm.read_functions()
    graph = callers_of(functions)
    have = {n: v['cpp'] for n, v in cands.items() if 'cpp' in v}
    params = {}
    for n, cpp in have.items():
        m = re.match(r'[^\n(]*?\b%s\(([^)\n]*)\)' % re.escape(n), cpp)
        if m:
            params[n] = [p.split()[-1] for p in m.group(1).split(',') if p.strip() and not p.strip().startswith('recomp_context')]
    FIELD_SIGNATURES.update(params)
    sites = {}
    for n, cpp in have.items():
        for m in re.finditer(r'lift_call<[\w<> ]+>\(ctx, ::(\w+)\)\(', cpp):
            depth, i = 1, m.end()
            while depth and i < len(cpp):
                depth += {'(': 1, ')': -1}.get(cpp[i], 0)
                i += 1
            inner = cpp[m.end():i - 1].strip()
            sites.setdefault(m.group(1), []).append((n, [a.strip() for a in split_top(inner, ', ')] if inner else []))
    calls = {}
    for n, f in functions.items():
        for callee in f['calls'].values():
            calls[(n, callee)] = calls.get((n, callee), 0) + 1
    usable = {}
    for f, plist in params.items():
        callers = graph.get(f)
        seen = sites.get(f, [])
        if (callers and all(c in have for c in callers) and all(len(args) == len(plist) for _, args in seen)
                and all(sum(1 for s, _ in seen if s == c) == calls.get((c, f)) for c in callers) and {s for s, _ in seen} == set(callers)):
            usable[f] = seen
    for _ in range(10):
        typed = {n: field_kinds(n, cpp, names.get(n), kinds, sources) for n, cpp in have.items()}
        new_params, new_returns = {}, dict(sources.get('returns', {}))
        for f, seen in usable.items():
            for i, p in enumerate(params[f]):
                found = {field_source(args[i], typed[c], kinds, sources, function_sections(c), FIELD_PARSED[c][4]) for c, args in seen}
                if len(found) == 1 and None not in found:
                    new_params.setdefault(f, {})[p] = found.pop()
        for f, cpp in have.items():
            rets = re.findall(r'\breturn ([^;]+);', cpp)
            found = {field_source(r, typed[f], kinds, sources, function_sections(f), FIELD_PARSED[f][4]) for r in rets}
            if rets and len(found) == 1 and None not in found and f not in new_returns:
                new_returns[f] = found.pop()
        if new_params == FIELD_PARAMS and new_returns == FIELD_RETURNS:
            break
        FIELD_PARAMS.clear()
        FIELD_PARAMS.update(new_params)
        FIELD_RETURNS.clear()
        FIELD_RETURNS.update(new_returns)


def named_cpp(name, v, info):
    """The function's C++ with its worked-out names. Only names change: check-names proves the compiled code stays identical."""
    if not info:
        return v['cpp']
    swaps = dict(info.get('vars', {}))
    swaps[name] = info['name']
    # (Not after `::`: that's the translated function's own C symbol, which keeps its N64 name. A function calling itself through
    #  lift_call(ctx, ::func_X), as shade_bone_tree does, had that renamed too and didn't compile. 2026-11-10)
    pattern = r'(?<!::)\b(' + '|'.join(re.escape(k) for k in sorted(swaps, key=len, reverse=True)) + r')\b'
    return re.sub(pattern, lambda m: swaps[m.group(1)], v['cpp'])


# (convert 2026-11-22, the readable track) A converted function calls others through the N64's registers -- lift_call<R>(ctx,
#  ::func_X)(...) -- by the callee's way in at its N64 address: the translated function in RecompiledFuncs, or a converted one's
#  extern "C" entry below. With names, each such callee that names.json names gets one line ahead of the functions,
#      namespace n64 { inline constexpr auto& load_arena = ::func_8001B548; }
#  and its call sites read lift_call<R>(ctx, n64::load_arena)(...). The reference IS the symbol (no object, no code), so only the
#  text changes; check-names compiles the release with and without names and proves the machine code identical. Kept by address:
#  a callee with no name, a name that is no C++ identifier or is a keyword, a name two callees share, a symbol not declared here.
CALL_SITE = re.compile(r'\(ctx, ::(\w+)\)')


def call_aliases(cands, proved, names):
    """{N64 symbol: name} for the callees the proved functions call through lift_call that names.json names (see CALL_SITE)."""
    if not names:
        return {}
    declared = {c for n in proved for c in cands[n].get('callees', [])}
    called = {s for n in proved for s in CALL_SITE.findall(cands[n].get('cpp', ''))} & declared
    shown = {s: names[s]['name'] for s in called if s in names and names[s].get('name')}
    # (and the mod's own functions -- modfn_X, which the mod's converted copies call -- by their names in the mod's source: the
    #  linker map of the very build the copies were translated from (modrep.MAP), so the name is the function's own, not a
    #  guess. One whose name a game function already shows here keeps its address.)
    own = mod_function_names()
    game_names = set(shown.values())
    shown.update({s: own[s] for s in called if s not in shown and s in own and own[s] not in game_names})
    taken = {}
    for s, nm in shown.items():
        taken.setdefault(nm, []).append(s)
    return {s: nm for s, nm in shown.items()
            if re.fullmatch(r'[A-Za-z_]\w*', nm) and nm not in CPP_KEYWORDS and len(taken[nm]) == 1}


def mod_function_names():
    """{modfn_X: the mod function's name} from the mod build's linker map (modrep.MAP): a symbol line at address X whose own
    section is .text.<name> (the mod builds with a section per function). Empty when there is no map."""
    if not os.path.exists(modrep.MAP):
        return {}
    text = open(modrep.MAP, encoding='utf8', errors='replace').read()
    sections = set(re.findall(r'^[0-9a-f]{8}\s+[0-9a-f]{8}\s+[0-9a-f]+\s+\d+\s+\.text\.([A-Za-z_]\w*)\s*$', text, re.M))
    out = {}
    for address, name in re.findall(r'^([0-9a-f]{8})\s+[0-9a-f]{8}\s+[0-9a-f]+\s+\d+\s+([A-Za-z_]\w*)\s*$', text, re.M):
        if name in sections:
            out['modfn_' + address.upper()] = name
    return out


def call_alias_lines(aliases):
    """The n64 namespace's lines for call_aliases (nothing without any)."""
    if not aliases:
        return []
    return (["// The functions the converted code calls through the N64's registers (lift_call), by name: each name below is that",
             "// function's way in at its N64 address -- its converted entry further down, or its translated version in RecompiledFuncs",
             "// (the mod's own functions, which its converted copies call, under their names in the mod's source).",
             "// A reference only (tools/lift/lift.py call_aliases): the call is the same call, check-names proves the code identical.",
             'namespace n64 {'] +
            ['inline constexpr auto& %s = ::%s;' % (aliases[s], s) for s in sorted(aliases, key=lambda s: aliases[s])] +
            ['}  // namespace n64', ''])


def with_call_aliases(cpp, aliases):
    """A function's C++ with its named callees' call sites as n64::name (call_aliases)."""
    if not aliases:
        return cpp
    return CALL_SITE.sub(lambda m: '(ctx, n64::%s)' % aliases[m.group(1)] if m.group(1) in aliases else m.group(0), cpp)


def comment_lines(text):
    """A names.json about as // lines. (2026-11-10: an about held a carriage return -- `pics\\rv_editmain.png` read as an escape --
    which ends a line for the compiler, so the rest of that // line became code and lifted.cpp didn't compile. Control
    characters are shown instead, and a line can't end in a backslash, which would join the next line to the comment.)"""
    shown = re.sub(r'[\x00-\x08\x0b-\x1f\x7f]', lambda m: '\\x%02X' % ord(m.group(0)), text)
    return ['// ' + line.rstrip('\\') for line in shown.split('\n')]


# (2026-11-13, agent "preserve"; the user's choice) A converted function the mod has a hand in says so in one first line: the mod's
# own code (a C patch, RECOMP_PATCH) is a new feature; a game function it patches (an edited copy of the game's MIPS) or hooks is
# changed for them. Comments only: check-names proves the machine code the same.
NEW_FEATURE = '// New feature (Ultimate No Mercy)'
CHANGED_FOR_NEW_FEATURES = '// Changed for new features (Ultimate No Mercy)'


def mod_mark(name, v):
    """The first comment line of a converted function the mod has a hand in, or None."""
    if v.get('modrep'):
        rec = modrep.index().get(name)
        return NEW_FEATURE if rec and modrep.c_patch(rec) else CHANGED_FOR_NEW_FEATURES
    return CHANGED_FOR_NEW_FEATURES if v.get('hooks') else None


def function_comment(name, v, proved, info=None, callers=None):
    address = name[5:13]
    lines = [line for line in [mod_mark(name, v)] if line]
    if v.get('modrep'):
        # (A copy of the mod's, modrep.py: named for the game function it stands in for.)
        address = modrep.replaced(name)[5:13]
        lines.append("// The mod's copy of the game function at 0x%s, %s (%d N64 instructions), converted: it runs in that function's place" % (
            address, modrep.replaced(name), v['size']))
        if info:
            # (Named after the game function it stands in for: with_copy_names. 2026-11-10)
            lines.append("// only when the mod loader finds the mod's copy has hash %s (tools/lift/modrep.py)." % v['modrep']['hash'])
            lines += comment_lines(info['about'])
            if info.get('guess'):
                lines.append('// The name is a best guess from the code alone.')
        else:
            lines.append("// only when the mod loader finds the mod's copy has hash %s (tools/lift/modrep.py). Not yet explained." % v['modrep']['hash'])
    elif info:
        lines += comment_lines(info['about'])
        lines.append('// From the N64 function at 0x%s (%d instructions).%s' % (
            address, v['size'], ' The name is a best guess from the code alone.' if info.get('guess') else ''))
    else:
        lines.append('// The game function at 0x%s (%d N64 instructions). Converted, not yet explained.' % (address, v['size']))
    if v.get('gotos'):
        lines.append("// Has gotos where m2c couldn't find the loop or the if: its structure is to be tidied by hand.")
    if callers:
        lines.append('// Called by: %s%s.' % (', '.join(callers[:4]), ' and %d more' % (len(callers) - 4) if len(callers) > 4 else ''))
    # (2026-11-16, honesty: the user's approval of the outside review's item 3) What the note says is the evidence there is, in
    #  plain words: "Random-tested: ..." (score's evidence_note), not "Proved". A release's text is always made by score; anything
    #  else -- check-names' builds -- gives '' and gets the plain line below.
    lines.append('// ' + ('%s.' % proved if proved else 'Under comparison with the original: not yet random-tested to the standard.'))
    return '\n'.join(lines)


# The converted code's unaligned word access: what m2c writes for the N64's lwl / lwr / swl / swr with --exact-unaligned
# (third_party/m2c/m2c/arch_mips.py UnalignedPairPattern). Written into lifted.cpp, and into the tester, ahead of the converted
# functions, only when one of them uses it (unaligned_helpers). Built on lift_mem.h's lift_read32 / lift_write8 / lift_write32,
# so the normal build, the proof build and the tester each reach memory their own way, as for every other access.
UNALIGNED_HELPERS = r"""// Unaligned words: the N64's lwl / lwr and swl / swr, which read and write a 32-bit word at any address, not only at a
// multiple of 4. WRITTEN BY tools/lift/lift.py (UNALIGNED_HELPERS). m2c writes an lwl at an address and the lwr at the
// address + 3 -- one word -- as lift_unaligned_load32 (an swl and its swr as lift_unaligned_store32): the four bytes there,
// the first the most significant, big-endian as all of the N64's memory is. An instruction without its partner is lift_lwl,
// lift_lwr, lift_swl or lift_swr: exactly the bytes that instruction reads or writes, the rest of the register kept.
#include <type_traits>

// The value in a register, as the instruction uses it -- a base address it adds its offset to, or the word it stores: a number,
// a game pointer (P), or either read from memory (Ref). A float is refused when this compiles: these are integer-register
// instructions, so a float there would mean m2c had the value's type wrong, and a C++ conversion would change its bits.
template <class X> inline u32 lift_word_bits(const X& x) {
    static_assert(std::is_integral_v<X>, "an unaligned access given a value that isn't a number or a game pointer");
    return (u32)x;
}
template <class U> inline u32 lift_word_bits(const P<U>& x) { return x.a; }
template <class U> inline u32 lift_word_bits(const Ref<P<U>>& x) { return lift_read32(x.a); }
template <class T> inline u32 lift_word_bits(const Ref<T>& x) {
    static_assert(std::is_integral_v<T>, "an unaligned access given a float");
    return (u32)(T)x;
}

// lwl at a, then lwr at a + 3: the word at a. Read the way those two instructions read it -- the aligned word holding a's byte,
// and the one holding a + 3's (the same word when a is a multiple of 4) -- so memory is reached as often as by the original.
inline u32 lift_unaligned_word(u32 a) {
    u32 shift = (a & 3) * 8;
    u32 first = lift_read32(a & ~3u);
    u32 last = lift_read32((a + 3) & ~3u);
    return shift == 0 ? first : (first << shift) | (last >> (32 - shift));
}
template <class B> inline u32 lift_unaligned_load32(const B& base, s32 offset) {
    return lift_unaligned_word(lift_word_bits(base) + (u32)offset);
}

// swl at a, then swr at a + 3: the word at a, its four bytes most significant first. Only those four bytes are written.
template <class B, class V> inline void lift_unaligned_store32(const B& base, s32 offset, const V& value) {
    u32 a = lift_word_bits(base) + (u32)offset;
    u32 v = lift_word_bits(value);
    lift_write8(a, (u8)(v >> 24));
    lift_write8(a + 1, (u8)(v >> 16));
    lift_write8(a + 2, (u8)(v >> 8));
    lift_write8(a + 3, (u8)v);
}

// An lwl alone: the bytes from a to the end of its aligned word go into the register from the top; the register's low (a & 3)
// bytes are kept. (What recomp.h's do_lwl does.)
template <class R, class B> inline u32 lift_lwl(const R& reg, const B& base, s32 offset) {
    u32 a = lift_word_bits(base) + (u32)offset;
    u32 shift = (a & 3) * 8;
    return (lift_read32(a & ~3u) << shift) | (lift_word_bits(reg) & ((1u << shift) - 1));
}
// An lwr alone: the bytes from the start of its aligned word up to a go into the register from the bottom; its high 3 - (a & 3)
// bytes are kept. (do_lwr.)
template <class R, class B> inline u32 lift_lwr(const R& reg, const B& base, s32 offset) {
    u32 a = lift_word_bits(base) + (u32)offset;
    u32 shift = (3 - (a & 3)) * 8;
    return (lift_read32(a & ~3u) >> shift) | (lift_word_bits(reg) & ~(0xFFFFFFFFu >> shift));
}
// An swl alone: the register's bytes from the top go to a and on to the end of its aligned word; the word's other bytes stay.
// Done as the instruction does it (do_swl): the aligned word read, merged and written back whole.
template <class B, class V> inline void lift_swl(const B& base, s32 offset, const V& value) {
    u32 a = lift_word_bits(base) + (u32)offset;
    u32 shift = (a & 3) * 8;
    u32 word = lift_read32(a & ~3u);
    lift_write32(a & ~3u, (word & ~(0xFFFFFFFFu >> shift)) | (lift_word_bits(value) >> shift));
}
// An swr alone: the register's bytes from the bottom go to a and back to the start of its aligned word. (do_swr.)
template <class B, class V> inline void lift_swr(const B& base, s32 offset, const V& value) {
    u32 a = lift_word_bits(base) + (u32)offset;
    u32 shift = (3 - (a & 3)) * 8;
    u32 word = lift_read32(a & ~3u);
    lift_write32(a & ~3u, (word & ~(0xFFFFFFFFu << shift)) | (lift_word_bits(value) << shift));
}""".split('\n')
UNALIGNED_USE = re.compile(r'\blift_(?:unaligned_load32|unaligned_store32|lwl|lwr|swl|swr)\(')


def unaligned_helpers(texts):
    """UNALIGNED_HELPERS, if any of these converted functions' texts uses them; otherwise nothing, so a set without them is
    written exactly as before. (And DIVISION_HELPERS likewise, 2026-11-08: everywhere the unaligned ones go -- the compile
    check, the release, the tester -- so they share this.)"""
    out = UNALIGNED_HELPERS + [''] if any(UNALIGNED_USE.search(t) for t in texts) else []
    return out + (DIVISION_HELPERS + [''] if any(DIVISION_USE.search(t) for t in texts) else [])


# The compiler's checks after a whole-number division, as m2c writes them where they were (2026-11-08: its DivPattern and
# DivuPattern, in third_party/m2c/m2c/arch_mips.py). The N64 code divides, then tests the divisor for zero (`break 7`) and, for
# a signed division, the most negative number divided by -1 (`break 6`): the game stops there (do_break). m2c used to drop the
# checks and leave only C's `/` or `%`, which stops the PC (a trap) where the quotient is worked out -- wherever it's used, after
# whatever comes between, and not at all on a way that doesn't use it (func_800F9894 uses its remainder on one way only; clang
# moves the division there). These stop the same way, at the same place: do_break, as the translated original's break does.
# The division that follows can then never trap.
DIVISION_HELPERS = r"""// The compiler's division checks, where the N64 code made them: a zero divisor, or the most negative number divided by -1,
// stops the game there, as the original's `break` does (tools/lift/lift.py DIVISION_HELPERS).
static inline void lift_div_check(s32 dividend, s32 divisor) {
    if (divisor == 0 || (divisor == -1 && dividend == (s32)0x80000000)) do_break(0);
}
#define LIFT_DIV_CHECK(p, q) lift_div_check((s32)(p), (s32)(q))
#define LIFT_DIVU_CHECK(q) lift_div_check(0, (s32)(q))""".split('\n')
DIVISION_USE = re.compile(r'\bLIFT_DIVU?_CHECK\(')


# The translated originals' unaligned instructions, for the tester and the proof build. recomp.h's do_lwl, do_lwr, do_swl and
# do_swr reach memory through the MEM_W it had when it was included -- the plain one -- so in those builds, where MEM_W is
# redefined to note every write (FuzzMem, LiftRealMem), what an original's swl or swr wrote was never noted: not compared with
# the converted function's writes, nor undone after the test. (Found 2026-11-07, scratch/lift_port.) The same functions again,
# copied from recomp.h, after MEM_W is that build's, under the names the originals call.
UNALIGNED_ORIGINALS = r"""// (recomp.h's unaligned helpers again, now that MEM_W notes what it writes: tools/lift/lift.py UNALIGNED_ORIGINALS.)
static inline gpr lift_original_lwl(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    gpr address = (offset + reg);
    gpr word_address = address & ~0x3;
    uint32_t loaded_value = MEM_W(0, word_address);
    gpr misalignment = address & 0x3;
    gpr masked_value = initial_value & (gpr)(uint32_t)~(0xFFFFFFFFu << (misalignment * 8));
    loaded_value <<= (misalignment * 8);
    return (gpr)(int32_t)(masked_value | loaded_value);
}
static inline gpr lift_original_lwr(uint8_t* rdram, gpr initial_value, gpr offset, gpr reg) {
    gpr address = (offset + reg);
    gpr word_address = address & ~0x3;
    uint32_t loaded_value = MEM_W(0, word_address);
    gpr misalignment = address & 0x3;
    gpr masked_value = initial_value & (gpr)(uint32_t)~(0xFFFFFFFFu >> (24 - misalignment * 8));
    loaded_value >>= (24 - misalignment * 8);
    return (gpr)(int32_t)(masked_value | loaded_value);
}
static inline void lift_original_swl(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    gpr address = (offset + reg);
    gpr word_address = address & ~0x3;
    uint32_t initial_value = MEM_W(0, word_address);
    gpr misalignment = address & 0x3;
    uint32_t masked_initial_value = initial_value & ~(0xFFFFFFFFu >> (misalignment * 8));
    uint32_t shifted_input_value = ((uint32_t)val) >> (misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}
static inline void lift_original_swr(uint8_t* rdram, gpr offset, gpr reg, gpr val) {
    gpr address = (offset + reg);
    gpr word_address = address & ~0x3;
    uint32_t initial_value = MEM_W(0, word_address);
    gpr misalignment = address & 0x3;
    uint32_t masked_initial_value = initial_value & ~(0xFFFFFFFFu << (24 - misalignment * 8));
    uint32_t shifted_input_value = ((uint32_t)val) << (24 - misalignment * 8);
    MEM_W(0, word_address) = masked_initial_value | shifted_input_value;
}
#define do_lwl lift_original_lwl
#define do_lwr lift_original_lwr
#define do_swl lift_original_swl
#define do_swr lift_original_swr""".split('\n')


def unaligned_originals(text):
    """UNALIGNED_ORIGINALS, if the originals' text calls any of recomp.h's unaligned helpers; otherwise nothing."""
    return UNALIGNED_ORIGINALS if re.search(r'\bdo_(?:lwl|lwr|swl|swr)\(', text) else []


# (2026-11-16, honesty: the user's approval of the outside review's items 3 and 4, and the user's naming rule) What lifted.cpp's
#  header says the project is and what the evidence for each function is. The label is the first of the user's three, the one
#  whose gate is met: "non-matching decompilation in progress, N% converted", N the counted figure of the release (score's, as
#  night.ps1 reads it: converted and random-tested). The later labels ("non-matching decompilation", "decompilation") wait for
#  their gates -- every game function converted and RecompiledFuncs out of the build -- and the word "proved" for the in-play
#  twin check's results. Comments only: check-names compiles with and without them.
def header_lines(part, figure, notes=()):
    """notes: the notes of the functions in the file. (twinnotes, 2026-11-18) Where one of them has an in-play count, the header
    says so (in place of "not in these notes yet"), and lifted.cpp's has the approved caller-twin note once where one of them
    refers to it (CALLER_TWIN_V11_NOTE)."""
    if part == 'changed':
        first = ["// lifted_changed.inc: the converted functions the mod (Ultimate No Mercy) changes or adds. WRITTEN BY tools/lift/lift.py:",
                 '// change that, not this.',
                 '//',
                 "// lifted.cpp has the game's own converted functions; this file has, written exactly as they would be there, the ones",
                 '// marked "Changed for new features" (a game function the mod patches with an edited copy of its MIPS, or hooks) and',
                 '// "New feature" (the mod\'s own C code, RECOMP_PATCH, converted), with the mod loader\'s tables for them. They are not the',
                 "// original game's behaviour: they are deliberate changes, tested against the mod's own versions, not the game's."]
    else:
        first = ['// lifted.cpp: game functions converted to C++. WRITTEN BY tools/lift/lift.py: change that, not this.']
    notes = list(notes)
    if any('In play: ' in n for n in notes):
        in_play = [
            '// has the limits. In-play twin checks (the converted and the translated function side by side in real play) have',
            '// compared some of them (tools/lift/inplay, from the twin runs by inplay_from_twin.py): a note gives an in-play count only',
            '// where there is one, and only while the function is converted exactly as it was when it was compared. The translated',
            '// original stays in RecompiledFuncs until the whole program is converted.']
        if any(CALLER_TWIN_REF in n for n in notes):
            if part == 'changed':
                in_play += ["// The caller-twin note the notes refer to is in lifted.cpp's header."]
            else:
                import textwrap
                in_play += ['//'] + ['// ' + l for l in textwrap.wrap(CALLER_TWIN_V11_NOTE, 122, break_long_words=False, break_on_hyphens=False)]
    else:
        in_play = [
            '// has the limits. An in-play twin check (lift_twin.cpp: the converted and the translated function side by side in real',
            "// play) exists, but its results are not in these notes yet: a note gives an in-play count only where there is one. The",
            '// translated original stays in RecompiledFuncs until the whole program is converted.']
    return first + [
        '//',
        '// Status: a non-matching decompilation in progress, %s converted (converted and random-tested: the counted figure of' % (figure or 'N%'),
        "// tools/lift/lift.py score at this release). Non-matching: the C++ is not meant to build the original ROM again byte for",
        '// byte; it is meant to behave as the original does.',
        '//',
        '// Each is a function of the original game, turned into C++ by m2c from its N64 instructions, in place of the translated',
        '// version in RecompiledFuncs (those are weak symbols, so the one here is the one the program uses). Names and explanations',
        "// come from tools/lift/names.json as each area's work is understood; until then a function keeps its N64 address for a name.",
        '//',
        '// The evidence for a function here: it passed the static checks in lift.py; it was tested against the translated original',
        '// on random inputs (tools/lift/fuzz) -- at least 200,000 plain random inputs, no difference, and every instruction reached',
        '// by a compared test except those the reviewed dead-code checker shows can never run (tools/lift/unreach.py,',
        '// division_checks.py: they count as reached without a test); and the normal build passed the whole-game QA runs',
        '// (qa_set.ps1). That is strong evidence, not a mathematical proof: random inputs can miss a case, and mod/NOTES.md 0.53.0'] + in_play


def write_common(names, cands, proved, part=None, figure=None):
    out = header_lines(part, figure, [proved[n] for n in proved if part is None or not in_changed_file(n, cands[n])]) + [
           '// The converted functions use N64 arithmetic: a signed add, subtract or multiply that overflows wraps. C++ only',
           '// promises that with -fwrapv. Without it GCC changed 17 tested functions and clang 94 (the Linux check, 2026-11-13;',
           '// tools/lift/fuzz tests them with -fwrapv -fno-strict-aliasing -ffp-contract=off, and only with those). The build',
           '// passes those three flags for this file and says so with -DLIFT_BUILT_WITH_FWRAPV; GCC is also told here.',
           '#ifndef LIFT_BUILT_WITH_FWRAPV',
           '#error "lifted.cpp must be compiled with -fwrapv -fno-strict-aliasing -ffp-contract=off (and -DLIFT_BUILT_WITH_FWRAPV)"',
           '#endif',
           '#if defined(__GNUC__) && !defined(__clang__)',
           '#pragma GCC optimize ("wrapv")',
           '#endif',
           '#include "lift_call.h"',
           '']
    called = sorted({c for n in names for c in cands[n].get('callees', [])})
    # (abi.py's helpers, only where one of its functions is here: otherwise the file is as it was.)
    helpers = [abi.CPP_HELPERS] if any(cands[n].get('abi') for n in names) else []
    # (And the overlay calls' helper, likewise only where one of these makes such a call: "Calls into overlays".)
    helpers += OVERLAY_HELPER if any(cands[n].get('overlay') for n in names) else []
    helpers += POINTER_HELPER if any('lift_pointer<' in cands[n].get('cpp', '') for n in names) else []
    # (And the tail calls' helper, only where one of these makes one: "Tail calls".)
    helpers += TAIL_HELPER if any(TAIL_USE in cands[n]['cpp'] for n in names) else []
    # (And where the mod's sections are, likewise only where a converted copy of the mod's uses the mod's data: modrep.py.)
    helpers += modrep.CPP_HELPER if any('lift_mod_address(' in cands[n]['cpp'] for n in names) else []
    # (The unaligned word helpers, ahead of the functions, only when one of them uses them: see UNALIGNED_HELPERS.)
    return out + helpers + callee_declarations(called) + [''] + unaligned_helpers([cands[n]['cpp'] for n in names])


def adapter_body(name, v, shown=None):
    call = 'game::%s(%s)' % (shown or name, ', '.join((['ctx'] if v.get('ctx') else []) + v['sources']))
    if v['ret'] == 'void':
        return call + ';'
    if v['ret'] == 'f32':
        return 'ctx->f0.fl = ' + call + ';'
    if v['ret'] == 'f64':
        return 'ctx->f0.d = ' + call + ';'      # (a double result, in the pair f0:f1: 2026-11-12, agent "refused2")
    return 'ctx->r2 = lift_ret<%s>(%s);' % (v['ret'], call)


# (2026-11-16, honesty: the user's approval of the outside review's item 4) The converted functions the mod has a hand in -- the
#  ones mod_mark marks, "Changed for new features" and "New feature" -- are written into a file of their own, lifted_changed.inc,
#  so lifted.cpp's own text is the game's code. Each function's text is exactly what it was in the one file (the tester compiles
#  each function from lift.py's own text, not from these files, so nothing tested changes).
#  ONE translation unit: lifted.cpp #includes lifted_changed.inc at its end, and the build compiles lifted.cpp alone, as before.
#  Measured first (scratch/agents/honesty, logs/split_machine_code.txt): compiled as a file of its own, with the build's own
#  compile line, 382 functions' machine code changed -- all 374 there, which reached lift_rdram through mingw's .refptr
#  indirection (data in another file), 17 of them also called what the one file had inlined; and 8 in lifted.cpp, whose
#  inlining changed. In one translation unit the compiler sees what it saw before; check-names compiles that unit.
#  What goes in lifted_changed.inc: those functions, their port wrappers, the mod loader's tables (the hook places, the mod's
#  copies, the mod functions and port exports the copies call), which only they use, and their ways in. Everything ahead of
#  the functions -- the helpers, the declarations, the named fields -- stays in lifted.cpp, made for all of them as before.
#  LIFT_SPLIT=0 writes the one file as before (and lifted_changed.inc with only a note).
CHANGED_FILE = 'lifted_changed.inc'
INCLUDE_CHANGED = '#include "%s"' % CHANGED_FILE


def split_release():
    return os.environ.get('LIFT_SPLIT', '1') != '0'


def in_changed_file(name, v):
    """Whether a converted function goes into lifted_changed.inc: the mod has a hand in it (mod_mark)."""
    return mod_mark(name, v) is not None


def release_texts(cands, proved, names, callers, figure=None):
    """{file name: its text} for a release: lifted.cpp and lifted_changed.inc (or lifted.cpp alone with LIFT_SPLIT=0)."""
    if not split_release():
        return {'lifted.cpp': release_text(cands, proved, names, callers, figure=figure)}
    # (The named fields worked out once, for every function, as the one file did: field_param_kinds fills in cands.)
    fields = read_fields() if names else ({}, {})
    if fields[0]:
        field_param_kinds(cands, names, *fields)
    return {'lifted.cpp': release_text(cands, proved, names, callers, part='game', figure=figure, fields=fields),
            CHANGED_FILE: release_text(cands, proved, names, callers, part='changed', figure=figure, fields=fields)}


def one_unit(texts):
    """The translation unit the build compiles: lifted.cpp with lifted_changed.inc put in where it is included (check-names)."""
    main = texts['lifted.cpp']
    if CHANGED_FILE not in texts or INCLUDE_CHANGED not in main:
        return main
    return main.replace(INCLUDE_CHANGED, texts[CHANGED_FILE], 1)


def release_text(cands, proved, names, callers, part=None, figure=None, fields=None):
    """A release file's text. part None: the one file with everything (as before the split); 'game': lifted.cpp -- everything
    ahead of the functions, made for all of them, then the functions the mod has no hand in and their ways in, then the
    #include of lifted_changed.inc; 'changed': lifted_changed.inc -- the functions the mod has a hand in, the mod loader's
    tables, their ways in. fields: read_fields()'s, already given to field_param_kinds (release_texts), or None to do that here."""
    these = sorted(n for n in proved if part is None or in_changed_file(n, cands[n]) == (part == 'changed'))
    if part == 'changed':
        out = header_lines(part, figure, [proved[n] for n in these]) + [
            '//',
            '// Not compiled on its own: lifted.cpp includes it at its end, so the compiler sees one translation unit, as it did when',
            '// these were in lifted.cpp (compiled apart, the machine code of 382 functions changed: tools/lift/lift.py CHANGED_FILE).',
            '']
    else:
        out = write_common(proved, cands, proved, part, figure)
        out += ['uint8_t* lift_rdram;', '']
        out += call_alias_lines(call_aliases(cands, proved, names))
        # The port's additions (port_additions.py), where a proved function has any: the port functions they call, and the wrappers
        # for a call the port added code around. (Nothing is written for a release without them: the same text as before.)
        ported_all = [name for name in sorted(proved) if cands[name].get('port')]
        if ported_all:
            out += ["// The port's own code in these functions' translated versions, carried into the conversions (tools/lift/port_additions.py).",
                    '#include <cstring>']
            for name in ported_all:
                out += cands[name]['port']['declarations']
            out += ['']
    ported = [name for name in these if cands[name].get('port')]
    out += ['namespace game {', '']
    # (Named fields come with the names: a release without names -- check-names' plain build -- is the conversion as it was.)
    kinds, sources = fields if fields is not None else read_fields() if names else ({}, {})
    if kinds:
        if part != 'changed':
            out += fields_preamble(kinds, sources)
        if fields is None:
            field_param_kinds(cands, names, kinds, sources)
    # (convert 2026-11-22: the callees' names at the call sites, and the callers' names in "Called by:" -- names only, see
    #  call_aliases. The same table for both release files: it is worked out from every proved function.)
    aliases = call_aliases(cands, proved, names)
    for name in these:
        info = names.get(name)
        v = dict(cands[name], cpp=fielded_cpp(name, cands[name]['cpp'], info, kinds, sources)) if kinds else cands[name]
        shown_callers = [names[c]['name'] if c in names and names[c].get('name') else c for c in callers.get(name) or []]
        out += [function_comment(name, cands[name], proved[name], info, shown_callers), with_call_aliases(named_cpp(name, v, info), aliases), '']
    out += ['}  // namespace game', '']
    for name in ported:
        out += cands[name]['port']['wrappers'] + ['']
    # (The mod loader's tables -- hook places, the mod's copies, what the copies call -- go with the functions the mod has a hand
    #  in, the only ones that use them: lifted_changed.inc, or the one file. Always there, empty without any.)
    tables = part != 'game'
    out += hook_release_table([name for name in these if cands[name].get('hooks')], cands) if tables else []
    out += ["// The way in from code that's still translated: from the N64's registers to parameters, and the result back.", '']
    for name in these:
        shown = names[name]['name'] if name in names else name
        # (One the mods hook: its hook calls around the converted body, and never inlined into a converted caller -- a caller
        #  must reach it through its entry, which the loader points elsewhere when the hooks waiting on it aren't these.
        #  hooks.py, 2026-11-12)
        out += ['extern "C" %svoid %s(uint8_t* rdram, recomp_context* ctx) {' % ('__attribute__((noinline)) ' if cands[name].get('hooks') else '', name),
                '    lift_rdram = rdram;',
                cands[name].get('port', {}).get('entry', '') + '    ' + hook_wrapped(name, cands[name], adapter_body(name, cands[name], shown)), '}', '']
    # The mod's copies converted (modrep.py): the table the game's mod loader reads. (Always written, empty without any, so the
    # loader's side always finds it.)
    if tables:
        out += modrep.release_table([(name, cands[name]['modrep']) for name in these if cands[name].get('modrep')])
        # (And the mod's own functions those copies call, for the loader to fill in: modrep.py release_mod_calls, 2026-11-12.)
        out += modrep.release_mod_calls([cands[name]['cpp'] for name in these if cands[name].get('modrep')])
        # (And the port's exports a C patch's glue calls, for the same: modrep.py release_imports, 2026-11-12, final081.)
        out += modrep.release_imports([cands[name]['cpp'] for name in these if cands[name].get('modrep')])
    if part == 'game':
        out += ["// The converted functions the mod (Ultimate No Mercy) changes or adds, with the mod loader's tables: in a file of",
                "// their own, included here so the compiler sees one translation unit (tools/lift/lift.py CHANGED_FILE).",
                INCLUDE_CHANGED, '']
    return '\n'.join(out)


def hook_release_table(hooked, cands):
    """lifted.cpp's hook places, for the converted functions the mods hook (hooks.py), and the table the game's mod loader reads
    them by. Always written, empty without any, so the loader's side always finds it."""
    places = [(n, at) for n in hooked for at in sorted(cands[n]['hooks']['at'])]
    out = ["// ---- The game functions the mods hook, converted with the hook calls in (tools/lift/hooks.py, 2026-11-12) ----",
           "// Each hook place compiled into a converted function below (its entry, before its frame is made; its return, after the",
           "// frame is released and the result set: where the loader's own hooked copy has them). The game's mod loader (librecomp",
           "// mods.cpp, lift_link_hooked_functions) reads them through lift_hook_place_table. For a function with places here, when",
           "// the hooks waiting on it are exactly these places it gives each its slot and leaves the function's entry as it is, so the",
           "// converted function runs with its hooks. Otherwise the slots stay empty (no hook runs from here) and the loader's own",
           "// hooked copy, linked (Stage 1) or translated live, takes the function's entry, as for any function, so it runs instead.",
           "// A converted copy of the mod's (modrep_func_X: the mod replaces func_X, and something hooks it too) has func_X's places",
           "// here, with the copy as the function (2026-11-12, kindd). The loader gives those their slots where it takes the copy in",
           "// place of the mod's own (load_mod_code's replacements, lift_builtin_replacement), only when the hooks on func_X are",
           "// exactly these; otherwise the mod's own copy, with the loader's hook calls in it, runs, and these slots stay empty.",
           'struct LiftHookPlace {',
           '    uint32_t section_vrom;     // The hooked game function: its section\'s ROM address,',
           '    uint32_t vram;             // its address,',
           '    uint32_t at_return;        // and the place: 0 its entry, 1 its return.',
           '    void (*func)(uint8_t* rdram, recomp_context* ctx);     // the converted function (its entry, for the loader\'s checks)',
           '    size_t slot;               // The loader\'s hook slot for the place, or (size_t)-1: none, no hook runs.',
           '};']
    out += ['extern "C" void %s(uint8_t* rdram, recomp_context* ctx);' % n for n in hooked]
    out += ['extern "C" {',
            'LiftHookPlace lift_hook_places[%d] = {' % max(1, len(places))]
    for n, at in places:
        out.append('    {0x%su, 0x%su, %d, %s, (size_t)-1},' % (cands[n]['hooks']['section'], cands[n]['hooks']['vram'], 1 if at == 'return' else 0, n))
    if not places:
        out.append('    {0, 0, 0, nullptr, (size_t)-1},')
    out += ['};',
            '// The loader\'s run_hook (librecomp mod_hooks.cpp), set by the loader with the slots.',
            'void (*lift_hook_runner)(uint8_t* rdram, recomp_context* ctx, size_t slot) = nullptr;',
            '// The places, how many there are, and where the loader puts its run_hook.',
            'LiftHookPlace* lift_hook_place_table(int* count, void (***runner)(uint8_t* rdram, recomp_context* ctx, size_t slot)) {',
            '    *count = %d;' % len(places),
            '    *runner = &lift_hook_runner;',
            '    return lift_hook_places;',
            '}',
            '}']
    for k, (n, at) in enumerate(places):
        out.append('static LiftHookPlace& %s = lift_hook_places[%d];' % (hooks.place_name(n, at == 'return'), k))
    out += ['// A hook place\'s call: the hooks the loader put in its slot, with the context as it is here (run_hook puts it back after).',
            'static inline void lift_run_hook(uint8_t* rdram, recomp_context* ctx, const LiftHookPlace& place) {',
            '    if (place.slot != (size_t)-1 && lift_hook_runner != nullptr) lift_hook_runner(rdram, ctx, place.slot);',
            '}', '']
    return out


def callers_of(functions):
    """Who calls each function, by their N64 names."""
    callers = {}
    for name, f in functions.items():
        for callee in set(f['calls'].values()):
            callers.setdefault(callee, []).append(name)
    return {k: sorted(v) for k, v in callers.items()}


def write_release(cands, proved, figure=None):
    """lifted.cpp and lifted_changed.inc (release_texts). figure: the counted figure for the header ('92.08%': score's)."""
    os.makedirs(GAME, exist_ok=True)
    names = with_copy_names(read_names(), cands)
    # A converted copy of the mod's goes in only while it is still the mod's copy: the hash it was made from must be the one the
    # mod as it is built now has (modrep.py: the built mod is translated again to see). Otherwise it waits to be converted again.
    copies = [n for n in proved if cands[n].get('modrep')]
    if copies:
        now = modrep.current_hashes()
        stale = sorted(n for n in copies if now.get(n) != cands[n]['modrep']['hash'])
        for n in stale:
            print("not released: %s -- the mod's copy has changed since it was converted" % n)
        proved = {n: v for n, v in proved.items() if n not in stale}
    texts = release_texts(cands, proved, names, callers_of(to_asm.read_functions()), figure)
    # (With LIFT_SPLIT=0 everything is in lifted.cpp, which then does not include lifted_changed.inc; that is still written, with
    #  nothing in it but its note, so no older one is left beside a lifted.cpp it doesn't belong to.)
    texts.setdefault(CHANGED_FILE, '// lifted_changed.inc: empty in this release (LIFT_SPLIT=0: every converted function is in lifted.cpp).\n'
                                   '// WRITTEN BY tools/lift/lift.py.\n')
    for file_name, text in texts.items():
        # (utf8: the names' abouts quote the game's own text, some of it Japanese; the default code page can't write it. 2026-11-10)
        open(os.path.join(GAME, file_name), 'w', newline='\n', encoding='utf8').write(text)
    # (An empty originals file, so the proof build's include stays valid.)
    open(os.path.join(GAME, 'lifted_originals.inc'), 'w', newline='\n').write('// Only filled for the proof build: tools/lift/lift.py twin\n')
    print('in the game: %d functions, %d of them explained' % (len(proved), len([n for n in proved if n in names])))


def check_names():
    """Proves names and comments changed nothing: compiles lifted.cpp with them and without, and compares the machine code byte by byte.
    Only the functions' own symbol names may differ, and those aren't in the code."""
    # (2026-11-16, honesty: both release files, lifted.cpp and lifted_changed.inc -- the functions are in one or the other. What
    #  is compiled is the one translation unit the build compiles: lifted.cpp with the .inc where it includes it, one_unit.)
    source = '\n'.join(open(path, encoding='utf8').read() for path in
                        [os.path.join(GAME, f) for f in ('lifted.cpp', CHANGED_FILE)] if os.path.exists(path))
    cands = json.load(open(CANDIDATES))
    # (and the mod's copies, modrep_func_...: modrep.py. And, 2026-11-13 "preserve", the hooked ones -- `extern "C"
    #  __attribute__((noinline)) void` -- and those with other names: game_main, static_5_..., rw_...; the pattern was for func_X
    #  and modrep_func_X only, so those were left out of both builds. Only names converted.)
    proved = {m: '' for m in re.findall(r'^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{', source, re.M)
              if 'cpp' in cands.get(m, {})}
    build = os.path.join(FUZZ, 'build')
    os.makedirs(build, exist_ok=True)
    tool = os.path.join(os.path.dirname(CLANG), 'llvm-objcopy.exe')
    # (The texts made first, one after the other -- the named one sets the field tables -- then compiled.)
    plain_texts = release_texts(cands, proved, {}, {})
    named = with_copy_names(read_names(), cands)
    named_texts = release_texts(cands, proved, named, {})
    def compiled(label, text):
        path = os.path.join(build, 'check_%s.cpp' % label)
        open(path, 'w', newline='\n', encoding='utf8').write(text)   # (utf8: as write_release)
        obj = os.path.join(build, 'check_%s.o' % label)
        command = [CLANG, '-std=c++20', '-O3', '-DNDEBUG', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-DLIFT_BUILT_WITH_FWRAPV', '-Wno-everything', '-c', '-o', obj, path,
                   '-I', os.path.join(PORT, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), '-I', os.path.join(PORT, 'include'), '-I', GAME]
        p = subprocess.run(command, capture_output=True, text=True)
        if p.returncode != 0:
            raise SystemExit(p.stderr[:3000])
        dump = obj + '.text'
        p = subprocess.run([tool, '--dump-section', '.text=' + dump, obj], capture_output=True, text=True)
        if p.returncode != 0:
            raise SystemExit(p.stderr[:1000])
        return open(dump, 'rb').read()
    # (The two builds at once, 2026-11-12, the speed agent: each is one compiler on one core, and neither needs the other. The
    #  same commands and the same comparison as when they ran one after the other.)
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        sections = list(pool.map(compiled, ['plain', 'named'], [one_unit(plain_texts), one_unit(named_texts)]))
    if sections[0] == sections[1]:
        print('identical: %d bytes of machine code, with %d functions named' % (len(sections[0]), len([n for n in proved if n in named])))
    else:
        raise SystemExit('DIFFERENT machine code: %d bytes against %d. A name or comment changed the program.' % (len(sections[0]), len(sections[1])))


TRANSLATED = {}


def translated_text(name, folder='RecompiledFuncs'):
    """A function's translated text, or None if that folder doesn't have it."""
    if folder not in TRANSLATED:
        found = TRANSLATED[folder] = {}
        paths = [os.path.join(PORT, folder, p) for p in sorted(os.listdir(os.path.join(PORT, folder))) if p.endswith('.c')]
        # (The mod's copies, modrep_<name>, read with the game's translation as to_asm.py reads them: modrep.py.)
        if folder == 'RecompiledFuncs':
            paths += modrep.sources()
        for path in paths:
            text = open(path, encoding='utf8', errors='replace').read()
            if modrep.is_source(path):
                text = modrep.game_names(text)     # (as to_asm.py reads it: modrep.game_names, 2026-11-12)
            for m in re.finditer(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\)', text, re.M):
                # (The translator ends each function with ";}" at the start of a line.)
                found[m.group(1)] = text[m.start():text.index('\n;}', m.start()) + 3]
    return TRANSLATED[folder].get(name)


def original_text(name):
    # (A function converted with its hook calls -- candidates.json's 'hooks' -- is compared with itself as the loader runs it:
    #  the translated original with the hook calls where Stage 1's mod_aot_hooked.c has them. hooks.py; 2026-11-12, modhooks)
    if name in HOOKED_ORIGINALS:
        return HOOKED_ORIGINALS[name]
    return translated_text(name)


HOOKED_ORIGINALS = {}       # (name -> its translated original with its hook calls: use_hooked_originals)


def use_hooked_originals(cands):
    """The originals of the candidates converted with hook calls, from here on: their translated originals with those calls."""
    if not REPLACED_BY_MOD:
        patched_by_mod()        # (which functions a mod replaces, for hooks.with_hooks: filled here if nothing has yet. rest100)
    for n, v in cands.items():
        if 'cpp' in v and v.get('hooks'):
            # (A copy of the mod's, kind D: modrep's translation with the hook calls of Stage 1's copy. 2026-11-12, kindd)
            text, why = hooks.with_hooks(n, translated_text(n), REPLACED_BY_MOD, set(helpers_writing_saved_registers())) if not modrep.is_modrep(n) else \
                hooks.copy_with_hooks(n, translated_text(n), modrep.index())
            if text is None:
                raise SystemExit('lift.py: %s was converted with its hook calls, and now: %s -- run candidates again' % (n, why))
            HOOKED_ORIGINALS[n] = text


def hook_mask(regs):
    """The registers a hook place's hooks read (None: every one), as the tester's mask of them (abi_out's bits)."""
    if regs is None:
        return abi.mask(abi.GPR[1:26] + abi.GPR[28:]) | sum(1 << (32 + k) for k in range(32))
    return abi.mask(regs)


def hook_which(v, at):
    """The number a hook place's stand-in notes in the list of calls: 0x30000000, the function's address's low 24 bits and
    the place (apart from the named callees' numbers, the overlay calls' 0x40000000 and the port's calls' top bit)."""
    return 0x30000000 | ((int(v['hooks']['vram'], 16) & 0xFFFFFF) << 1) | (1 if at == 'return' else 0)


def hook_places(name, v):
    """The tester's definitions of a function's hook places (hooks.py place_name): each one's number and what its hooks read."""
    return ['static const LiftHookPlace %s = {0x%08Xu, 0x%016Xull};' % (hooks.place_name(name, at == 'return'), hook_which(v, at), hook_mask(regs))
            for at, regs in sorted(v.get('hooks', {}).get('at', {}).items())]


def hook_wrapped(name, v, body):
    """The wrapper's body with the hook calls around the converted body: the entry hook first, before the converted function
    makes its frame; the return hook after it has released it and the result is in the context (hooks.py)."""
    if not v.get('hooks'):
        return body
    at = v['hooks']['at']
    before = "lift_run_hook(rdram, ctx, %s);   // (the mods' hook at its entry, before its frame is made)\n    " % hooks.place_name(name, False) if 'entry' in at else ''
    after = "\n    lift_run_hook(rdram, ctx, %s);   // (the mods' hook at its return: its frame released, its result set)" % hooks.place_name(name, True) if 'return' in at else ''
    return before + body + after


# The tester's side of the hook calls: what a hook place is there, and the call (fuzz_main.cpp fuzz_hook_stand_in).
HOOK_TESTER = ['// The mods\' hook places (tools/lift/hooks.py, 2026-11-12): both versions of a hooked function call the same stand-in at each.',
               'struct LiftHookPlace { u32 which; uint64_t reads; };',
               'static inline void lift_run_hook(uint8_t*, recomp_context* ctx, const LiftHookPlace& place) { fuzz_hook_stand_in(place.which, place.reads, ctx); }', '']


SEPARATE = {}


def jumps_to_copies(text, name, group, piece):
    """A piece's translated original, with each of its jumps to a piece of the group (or back to the entry) going to the group's
    own copy of it, original_<entry>__<piece> (original_<entry>): what's compared is the whole N64 function. Only the jumps --
    the calls the translator noted at a jump (merge.py jumps), found by the instruction comment before them, as to_asm.py reads
    them. A jal to a piece stays a call, to the stand-in, as the converted function's is; and so does a tail call, whose target
    isn't in the group. (Until 2026-11-08 every mention of a piece was taken for a jump: merge.py then took no group where a
    piece was called from anywhere, and for those groups it's the same text.)"""
    import merge
    if not SEPARATE:
        SEPARATE.update(to_asm.read_functions(merge=False))      # (the pieces as the translator wrote them, read once)
    jumps = merge.jumps(SEPARATE[piece]) if piece in SEPARATE else {}
    out = []
    last = None
    for line in text.split('\n'):
        m = re.match(r'^\s*// 0x([0-9A-F]{8}): ', line)
        if m:
            last = int(m.group(1), 16)
        elif jumps.get(last) in group:
            other = jumps[last]
            own = name if other == name else '%s__%s' % (name, other)
            line = re.sub(r'\b%s\(rdram, ctx\);' % re.escape(other), 'original_%s(rdram, ctx, reached);' % own, line)
        out.append(line)
    return '\n'.join(out)


def originals_with_notes(names, heading, functions=None, port=None, chunks=None):
    """The translated originals' text, each noting which of its instructions ran. Returns the lines, and each one's instruction count.

    A function put together from pieces (merge.py) comes with its pieces' real originals, as original_<entry>__<piece>, and its jumps
    to them go there: what's compared is the whole N64 function, not the entry with stand-ins where its pieces were. The instructions
    are numbered by address across all of them, so one the pieces share counts once, reached through whichever copy ran it.

    port: name -> the port functions its additions call (port_additions.py). Each line of the port's code that calls one counts as
    an instruction too, numbered after the real ones: the function is only proved once each of the port's calls has run, as each
    of its instructions must (func_80001E08's matrix hook only runs when its second parameter is the camera matrix).

    chunks: if given, gets each function's own lines too, by name (run_fuzz's result cache hashes them)."""
    orig = [heading, '']
    sizes = {}
    for name in names:
        start = len(orig)
        group = (functions or {}).get(name, {}).get('pieces', [name])
        # (2026-11-13, modnative5: the user's rule (5) -- a native candidate's internal callees run through it, their originals
        #  pieces of its own: prove_gen.py THRU. Every other function's group is as before.)
        group = group + [p for p in THRU_GROUPS.get(name, []) if p not in group]
        slots = {}
        def mark(m):
            k = slots.setdefault(m.group(1), len(slots))
            return m.group(0) + '\n    reached[%d] = 1;' % k
        texts = []
        for piece in group:
            t = original_text(piece)
            if name in LOOP_TICK_NAMES:
                t = loop_ticks_original(t)      # (a native candidate's: its loops counted, the user's rule (3). 2026-11-13, modnative5)
            t = re.sub(r'// 0x([0-9A-F]{8}): [^\n]*', mark, t)
            calls = (port or {}).get(name)
            if calls and piece == name:
                lines = t.split('\n')
                for i, l in enumerate(lines):
                    if not l.lstrip().startswith('extern') and re.search(r'\b(?:%s)\s*\(' % '|'.join(map(re.escape, calls)), l):
                        k = slots.setdefault('port:%d' % i, len(slots))
                        lines[i] = re.sub(r'^(\s*)', lambda m: m.group(1) + 'reached[%d] = 1; ' % k, l, count=1)
                t = '\n'.join(lines)
            own = name if piece == name else '%s__%s' % (name, piece)
            if name in THRU_GROUPS:
                # (rule (5): a call to an internal function run through it goes to the group's copy of its original)
                t = re.sub(r'\b(modrep_natthru_[0-9A-F]{8})\(rdram, ctx\);', lambda m: 'original_%s__%s(rdram, ctx, reached);' % (name, m.group(1)), t)
            t = t.replace('RECOMP_FUNC void %s(uint8_t* rdram, recomp_context* ctx)' % piece,
                          'static void original_%s(uint8_t* rdram, recomp_context* ctx, u8* reached)' % own)
            t = jumps_to_copies(t, name, group, piece)
            # A jump table's index is declared where it's set, which C allows a goto to jump past and C++ doesn't: declared at the top.
            addends = re.findall(r'\bgpr (jr_addend_[0-9A-F]{8}) = ', t)
            if addends:
                t = re.sub(r'\bgpr (jr_addend_[0-9A-F]{8}) = ', r'\1 = ', t)
                t = t.replace('{', '{\n    gpr %s;' % ', '.join(addends), 1)
            texts.append(t)
        orig += ['static void original_%s__%s(uint8_t* rdram, recomp_context* ctx, u8* reached);' % (name, other) for other in group[1:]]
        sizes[name] = len(slots)
        SLOTS[name] = {int(a, 16): k for a, k in slots.items() if not a.startswith('port:')}
        for t in texts:
            orig += [t, '']
        if chunks is not None:
            chunks[name] = orig[start:]
    return orig, sizes


SLOTS = {}      # Each original's instruction numbers, by address, as originals_with_notes numbered them.
INDEPENDENT = os.path.join(HERE, 'independent.json')


def run_independence(tests):
    """Tests whether functions that read a register before setting it (on some path) actually depend on its value. A register one
    doesn't depend on goes in independent.json, and analyse.py stops counting it as an input, for that function and its callers.
    Run analyse.py afterwards."""
    functions = to_asm.read_functions()
    analysis = with_system(json.load(open(os.path.join(HERE, 'analysis.json'))), functions)
    origins = {n: sorted(set(v.get('own', [])) - PARAMETER_REGISTERS - {'sp', 'ra'}) for n, v in analysis.items()}
    origins = {n: r for n, r in origins.items() if r and n in functions and translated_text(n)}
    names = sorted(origins)
    build = os.path.join(FUZZ, 'build')
    os.makedirs(build, exist_ok=True)
    chunks = {}     # (each function's own lines, for its cache key)
    orig, sizes = originals_with_notes(names, '// The translated originals, for the independence test. WRITTEN BY tools/lift/lift.py.', functions,
                                       chunks=chunks)
    open(os.path.join(build, 'indep_originals.inc'), 'w', newline='\n').write('\n'.join(orig))
    called = sorted({c for n in names for c in functions[n]['calls'].values()} - set(names))
    out = ['// indep_gen.cpp: originals for the independence test. WRITTEN BY tools/lift/lift.py.', '#include "fuzz.h"', '']
    # (No overlay call is followed here: a lookup skips the test, as before -- fuzz_main.cpp get_function.)
    out += ['recomp_func_t* fuzz_overlay_lookup(int32_t vram) { return nullptr; }', '']
    # (The mod's copies' originals find the mod's sections through section_addresses, as in the random-input tester (fuzz, above):
    #  without it the test didn't link once the copies were in the analysis, 2026-11-09, and analyse.py kept the last list.)
    if any(modrep.is_modrep(n) for n in names):
        places = [a + modrep.TESTER_SHIFT for a, _ in modrep.sections()] or [modrep.TESTER_SHIFT]
        out += ['extern "C" { int32_t lift_mod_section_address[%d] = {%s}; }' % (len(places), ', '.join('(int32_t)0x%08Xu' % a for a in places)),
                'int32_t* section_addresses = lift_mod_section_address;',
                'void fuzz_mod_region(u32* start, u32* bytes) { *start = 0x%08Xu; *bytes = 0x%Xu; }' % (places[0], modrep.sections()[0][1] if modrep.sections() else 0), '']
    out += callee_declarations(sorted(set(called) | set(names))) + ['']
    out += ['#undef MEM_W', '#undef MEM_H', '#undef MEM_B', '#undef MEM_HU', '#undef MEM_BU',
            '#define MEM_W(o, r) (FuzzMem<int32_t>{rdram, (uint64_t)((r) + (o)), 0})',
            '#define MEM_H(o, r) (FuzzMem<int16_t>{rdram, (uint64_t)((r) + (o)), 2})',
            '#define MEM_B(o, r) (FuzzMem<int8_t>{rdram, (uint64_t)((r) + (o)), 3})',
            '#define MEM_HU(o, r) (FuzzMem<uint16_t>{rdram, (uint64_t)((r) + (o)), 2})',
            '#define MEM_BU(o, r) (FuzzMem<uint8_t>{rdram, (uint64_t)((r) + (o)), 3})',
            '#undef SD',
            '#define SD(val, o, r) { FuzzMem<uint32_t>{rdram, (uint64_t)((r) + (o) + 4), 0} = (uint32_t)((gpr)(val)); FuzzMem<uint32_t>{rdram, (uint64_t)((r) + (o)), 0} = (uint32_t)((gpr)(val) >> 32); }',
            '#include "indep_originals.inc"', '']
    # What they call is a stand-in, as in the random-input tester. So is a call from one of these to another of them: each is
    # tested on its own.
    everything = sorted(set(called) | set(names))
    for c in everything:
        out.append(stand_in_line(c, everything.index(c), analysis, functions))
    table = []
    for n in names:
        out += ['static u8 reached_%s[%d];' % (n, sizes[n] + 1), 'static const u32 addresses_%s[] = {0};' % n]
        # (After addresses: constants, constant_count, port_pointers, abi_out, abi, compares, compare_count, input_registers, all empty
        #  here, then system_calls. Given in full so that last value lands in system_calls and not in port_pointers.)
        table.append('    {"%s", original_%s, nullptr, "", 0, reached_%s, %d, addresses_%s, nullptr, 0, 0, 0, 0, nullptr, 0, 0, %d},' % (
            n, n, n, sizes[n], n, calls_system(functions[n])))
    out += ['FuzzFunction fuzz_functions[] = {'] + table + ['};',
            'const int fuzz_function_count = sizeof(fuzz_functions) / sizeof(fuzz_functions[0]);', '']
    gen = os.path.join(build, 'indep_gen.cpp')
    open(gen, 'w', newline='\n').write('\n'.join(out))
    exe = os.path.join(build, 'indep.exe')
    flags = ['-std=c++20', '-O1', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-Wno-everything', '-DARCADE_LIFT_FUZZ',
             '-DNDEBUG', '-static']
    # (FUZZ_MOD_NATIVE=0, 2026-11-14 fastnight: its generated file gives no function a private table, native rules or the mod's
    #  image, so the native-only code would do nothing here -- fuzz.h -- and its key is this build's text: independence_tester_hash)
    command = [CLANG] + flags + ['-DFUZZ_MOD_NATIVE=0', '-o', exe, gen, os.path.join(FUZZ, 'fuzz_main.cpp')]
    for folder in INCLUDE + [FUZZ, build]:
        command += ['-I', folder]
    jobs = []
    for n in names:
        for reg in origins[n]:
            reads = [k for a, k in SLOTS[n].items() if a in functions[n]['ins'] and reg in uses_defs_in(functions[n], a)[0]]
            jobs.append((n, reg, reads))
    # The result cache (2026-11-12, the speed agent): each register's test has a key, a hash of everything it is made from -- the
    # tester's sources, headers, compiler and flags, the FUZZ_ settings, the tests asked for, the generated file's fixed text, and
    # the function's own part of it (its original as noted here, the stand-ins of what it names, its table row), the register and
    # the instructions that read it. Only a verdict a fresh run can't better is kept (independence_keeps). LIFT_NO_CACHE=1: run all.
    fixed = [l for l in out if not l.startswith(('extern "C" void ', 'static u8 reached_', 'static const u32 addresses_', '    {"'))]
    shared = independence_tester_hash(flags, fixed, tests, build)
    every = set(everything)
    stand_ins = {c: stand_in_line(c, everything.index(c), analysis, functions) for c in everything}     # (as written above)
    def function_part(n):
        chunk = '\n'.join(chunks[n])
        named = sorted(every & set(re.findall(r'\b\w+\b', chunk)))
        return [n, chunk] + [stand_ins[c] for c in named] + [
            'static u8 reached_%s[%d];' % (n, sizes[n] + 1), 'static const u32 addresses_%s[] = {0};' % n,
            '%d %d' % (sizes[n], calls_system(functions[n]))]
    parts = {n: function_part(n) for n in names}
    key = lambda n, reg, reads: fuzz_cache_key(shared, parts[n] + [reg, ','.join(map(str, reads)) or '-1'])
    cache_path = os.path.join(FUZZ, 'cache', 'independence.json')
    try:
        cache = json.load(open(cache_path)) if os.path.exists(cache_path) else {}
    except ValueError:
        cache = {}
    use_cache = os.environ.get('LIFT_NO_CACHE') != '1'
    keys = {(n, reg): key(n, reg, reads) for n, reg, reads in jobs}
    cached = {k: cache[k]['result'] for k in keys.values() if use_cache and isinstance(cache.get(k), dict) and 'result' in cache[k]}
    if any(k not in cached for k in keys.values()):
        p = subprocess.run(command, capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stderr[:3000])
            raise SystemExit('the independence test did not compile')
    import time
    def run(n, reg, count, reads):
        # (x4 since 2026-11-11: the limits inside are CPU time now, and on a busy PC the wall clock runs far ahead of them --
        #  0.76.0 had 83 "no result" against 0.75.0's 38 at 120 s.)
        p = subprocess.Popen([exe, '--independent', n, reg, str(count), ','.join(map(str, reads)) or '-1'], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True)
        began = time.monotonic()
        while True:
            try:
                out, _ = p.communicate(timeout=5)
                return out.split() if p.returncode == 0 and out.strip() else None
            except subprocess.TimeoutExpired:
                pass
            # Stopped, with no result, after 480 s of the wall clock as before -- or, since 2026-11-12, once it has used
            # INDEPENDENCE_STUCK_CPU of CPU time: four times the tester's own 40-second limit, which it checks between tests, so
            # it is in one test that never comes back (a loop on random data: 13 of the 650 registers 0.79.0 tested are stuck so,
            # and each sat out the whole 480 s). The most any run that finished took here was 75 s of the wall clock. Only such a
            # stuck one is kept as a time-out (INDEPENDENCE_KEEP 'cautious'); one a busy PC gave too little time to runs again.
            cpu = process_cpu_seconds(p)
            if cpu >= INDEPENDENCE_STUCK_CPU or time.monotonic() - began >= 480:
                if cpu >= INDEPENDENCE_STUCK_CPU:
                    stuck.add((n, reg))
                p.kill()
                p.communicate()
                timed_out.add((n, reg))
                return None
    timed_out, stuck = set(), set()
    def one(job):
        n, reg, reads = job
        k = keys[(n, reg)]
        if k in cached:
            return job, cached[k], 'cache'
        # First the first INDEPENDENCE_FIRST tests alone: the full run's tests start with exactly these (the same random sequence,
        # whatever the count asked for), so a difference among them, with as many comparisons as a verdict needs, is the verdict
        # the full run would give. Most registers tested depend on it (494 of 650 in 0.79.0), and the full run went on to the end
        # however many differences it had found -- for a slow function, its whole 40 seconds. Anything else -- no difference yet,
        # too few compared -- runs in full, as before.
        result = run(n, reg, INDEPENDENCE_FIRST, reads)
        how = 'first'
        # (A first run stopped for time stops there: the full run would come to the same test first, and stop on it too.)
        if independence_verdict(result) != 'depends on it' and (n, reg) not in timed_out:
            result = run(n, reg, tests, reads)
            how = 'full'
        return job, result, how
    found = {}
    tally = collections.Counter()
    hows = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS) as pool:
        for (n, reg, reads), result, how in pool.map(one, jobs):
            verdict = independence_verdict(result)
            tally[verdict] += 1
            hows[how] += 1
            if verdict == 'independent':
                found.setdefault(n, []).append(reg)
            k = keys[(n, reg)]
            if how != 'cache':
                if ((n, reg) not in timed_out or (n, reg) in stuck) and independence_keeps(verdict, result, tests):
                    cache[k] = {'job': '%s %s' % (n, reg), 'result': result}
                else:
                    cache.pop(k, None)
    # (Only this run's keys stay: the file doesn't grow with every old version of a function.)
    cache = {k: v for k, v in cache.items() if k in set(keys.values())}
    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    with open(cache_path + '.tmp', 'w') as f:
        json.dump(cache, f, indent=0, sort_keys=True)
    os.replace(cache_path + '.tmp', cache_path)
    json.dump({n: sorted(r) for n, r in sorted(found.items())}, open(INDEPENDENT, 'w'), indent=0)
    print('%d registers in %d functions tested:' % (len(jobs), len(names)), dict(tally))
    print('  %d from the cache, %d settled by their first %d tests, %d run in full' % (
        hows['cache'], hows['first'], INDEPENDENCE_FIRST, hows['full']))


INDEPENDENCE_FIRST = 3000       # (run_independence: the quick first run's tests)


def independence_verdict(result):
    """What a register's test found (run_independence), from the tester's line: name register tests compared differences
    reads-reached first-difference. In this order, as it always was."""
    if not result:
        return 'no result'
    if int(result[3]) < 1000:
        return 'too few comparisons'
    if int(result[4]) > 0:
        return 'depends on it'
    if result[5] != '1':
        return 'the reads never ran'
    return 'independent'


# Which verdicts the independence cache keeps (run_independence). 'exact': only those no fresh run can better (below). 'cautious'
# adds the ones that keep the register an input -- too few comparisons, the reads never ran, a crash, a time-out in a test that was
# truly stuck (run's INDEPENDENCE_STUCK_CPU; one a busy PC starved runs again) -- which a fresh run on a faster core could only
# turn into 'independent' by getting further in the same 40 seconds: keeping them can never drop an input, only miss one a lucky
# run would have dropped. 'cautious' unless LIFT_INDEPENDENCE_KEEP=exact. (The speed agent's two proposals, 2026-11-12: with
# 'exact' a night with nothing changed still runs 129 of 0.79.0's 650 registers, 'cautious' 3.)
INDEPENDENCE_KEEP = os.environ.get('LIFT_INDEPENDENCE_KEEP', 'cautious')
INDEPENDENCE_STUCK_CPU = 160        # (CPU seconds: four times the tester's own limit, as the wall clock's 480 s was set)


def process_cpu_seconds(p):
    """The CPU time a running child process has used (Windows; 0 where it can't be read)."""
    if os.name != 'nt':
        return 0.0
    import ctypes
    from ctypes import wintypes
    times = [wintypes.FILETIME() for _ in range(4)]
    if not ctypes.windll.kernel32.GetProcessTimes(wintypes.HANDLE(int(p._handle)), *[ctypes.byref(t) for t in times]):
        return 0.0
    to_seconds = lambda t: ((t.dwHighDateTime << 32) | t.dwLowDateTime) / 1e7
    return to_seconds(times[2]) + to_seconds(times[3])      # (kernel and user time)


def independence_keeps(verdict, result, tests):
    """Whether run_independence's cache keeps a verdict for the next run with the same key. Always 'depends on it' (a difference
    found is a difference: the same tests find it again), and 'independent' when every test asked for ran -- a fresh run, from the
    same sequence, can run no more of them. Never an 'independent' the 40-second limit cut short: a run on a faster core gets
    further, and could find what this one didn't. The rest only with INDEPENDENCE_KEEP 'cautious'."""
    if verdict == 'depends on it':
        return True
    if verdict == 'independent':
        return int(result[2]) >= tests
    return INDEPENDENCE_KEEP == 'cautious'


def independence_tester_hash(flags, fixed, tests, build):
    """The part of every independence test's key that all share (run_independence): the tester as the compiler sees it in this
    build (fuzz_main.cpp and the generated file's include, preprocessed with its flags and FUZZ_MOD_NATIVE=0, as fuzz_tester_hash
    does since 2026-11-14), the compiler and its libraries, the flags, the generated file's fixed text, the tests asked for, and
    every FUZZ_ setting in the environment (FUZZ_MS is the time limit here: in the key, not beside it)."""
    h = hashlib.sha256(b'independence-cache-2')
    h.update(b'\0harness\0' + fuzz_preprocessed(os.path.join(FUZZ, 'fuzz_main.cpp'), '-O1', 0, build, flags=flags))
    h.update(b'\0generated\0' + fuzz_preprocessed(os.path.join(build, 'indep_key_prologue.cpp'), '-O1', 0, build, '#include "fuzz.h"\n', flags=flags))
    h.update(fuzz_toolchain_identity().encode())
    h.update(('\0flags\0%r\0tests\0%d\0' % (flags, tests)).encode())
    for k in sorted(os.environ):
        if k.startswith('FUZZ_'):
            h.update(('\0env\0%s=%s\0' % (k, os.environ[k])).encode())
    h.update(('\0fixed\0' + '\n'.join(fixed)).encode())
    return h.hexdigest()


def write_twin(cands):
    os.makedirs(GAME, exist_ok=True)
    # (Functions that call others aren't in the proof build yet: comparing them in play would mean running what they call twice. The
    # random-input tester does cover them, with stand-ins for what they call. Nor do functions with the port's additions: running
    # both versions would run the port's hooks twice, port_additions.py.)
    # (Nor the mod's copies, modrep.py: in play they run only where the mod loader points a game function at them, and their
    #  originals find the mod's sections where the tester puts them, not where the loader does.)
    names = sorted(n for n, v in cands.items() if 'cpp' in v and not v.get('ctx') and not v.get('port') and not v.get('modrep'))
    orig, sizes = originals_with_notes(names, '// lifted_originals.inc: the translated originals, for the proof build only. WRITTEN BY tools/lift/lift.py.',
                                       to_asm.read_functions())
    open(os.path.join(GAME, 'lifted_originals.inc'), 'w', newline='\n').write('\n'.join(orig))

    out = write_common(names, cands, {})
    out[0] = '// lifted.cpp, PROOF BUILD: every candidate runs beside its original. WRITTEN BY tools/lift/lift.py.'
    out += ['#include "lift_twin.h"', '', 'namespace game {', '']
    for name in names:
        out += [function_comment(name, cands[name], ''), cands[name]['cpp'], '']
    out += ['}  // namespace game', '']
    out += ['#undef MEM_W', '#undef MEM_H', '#undef MEM_B', '#undef MEM_HU', '#undef MEM_BU',
            '#define MEM_W(o, r) (LiftRealMem<int32_t>{rdram, (uint64_t)((r) + (o)), 0})',
            '#define MEM_H(o, r) (LiftRealMem<int16_t>{rdram, (uint64_t)((r) + (o)), 2})',
            '#define MEM_B(o, r) (LiftRealMem<int8_t>{rdram, (uint64_t)((r) + (o)), 3})',
            '#define MEM_HU(o, r) (LiftRealMem<uint16_t>{rdram, (uint64_t)((r) + (o)), 2})',
            '#define MEM_BU(o, r) (LiftRealMem<uint8_t>{rdram, (uint64_t)((r) + (o)), 3})',
            '#undef SD',
            '#define SD(val, o, r) { LiftRealMem<uint32_t>{rdram, (uint64_t)((r) + (o) + 4), 0} = (uint32_t)((gpr)(val)); LiftRealMem<uint32_t>{rdram, (uint64_t)((r) + (o)), 0} = (uint32_t)((gpr)(val) >> 32); }']
    # (And the originals' lwl / lwr / swl / swr through that MEM_W, so what they write is noted too: UNALIGNED_ORIGINALS.)
    out += unaligned_originals('\n'.join(orig)) + ['#include "lifted_originals.inc"', '']
    out += ['static u8 lift_not_counted[8192];   // Where an unchecked call notes its instructions: nowhere that counts.', '']
    for name in names:
        out += ['static u8 reached_%s[%d];' % (name, sizes[name])]
    out += ['', 'static LiftTwinEntry lift_table[] = {']
    for name in names:
        out += ['    {"%s", reached_%s, %d},' % (name, name, sizes[name])]
    out += ['};', 'static const int lift_count = sizeof(lift_table) / sizeof(lift_table[0]);', '']
    for i, name in enumerate(names):
        v = cands[name]
        args = ', '.join(v['sources'])
        call = 'game::%s(%s)' % (name, args)
        out += ['extern "C" void %s(uint8_t* rdram, recomp_context* ctx) {' % name,
                '    LiftTwinEntry& entry = lift_table[%d];' % i,
                '    if (!lift_twin_should_check(entry)) {',
                '        lift_twin_side_begin(rdram);',
                '        lift_twin_side_end();',
                '        original_%s(rdram, ctx, lift_not_counted);' % name,
                '        return;',
                '    }',
                '    u32 entry_sp = (u32)ctx->r29;',
                '    LiftKeptRegisters kept(ctx);',
                '    lift_twin_side_begin(rdram);']
        if v['ret'] == 'void':
            out += ['    ' + call + ';', '    lift_twin_side_end();',
                    '    original_%s(rdram, ctx, reached_%s);' % (name, name),
                    '    lift_twin_compare(entry, lift_table, lift_count, entry_sp, false, 0, 0, kept.same_as(ctx));']
        elif v['ret'] == 'f32':
            out += ['    f32 side = ' + call + ';', '    u32 side_bits; std::memcpy(&side_bits, &side, 4);', '    lift_twin_side_end();',
                    '    original_%s(rdram, ctx, reached_%s);' % (name, name),
                    '    lift_twin_compare(entry, lift_table, lift_count, entry_sp, true, side_bits, ctx->f0.u32l, kept.same_as(ctx));']
        elif v['ret'] == 'f64':
            # (A double result: its 64 bits against the pair f0:f1's. 2026-11-12, agent "refused2")
            out += ['    f64 side = ' + call + ';', '    uint64_t side_bits; std::memcpy(&side_bits, &side, 8);', '    lift_twin_side_end();',
                    '    original_%s(rdram, ctx, reached_%s);' % (name, name),
                    '    lift_twin_compare(entry, lift_table, lift_count, entry_sp, true, side_bits, ctx->f0.u64, kept.same_as(ctx));']
        else:
            out += ['    uint64_t side = lift_ret<%s>(%s);' % (v['ret'], call), '    lift_twin_side_end();',
                    '    original_%s(rdram, ctx, reached_%s);' % (name, name),
                    '    lift_twin_compare(entry, lift_table, lift_count, entry_sp, true, side, ctx->r2, kept.same_as(ctx));']
        out += ['}', '']
    open(os.path.join(GAME, 'lifted.cpp'), 'w', newline='\n').write('\n'.join(out))
    print('proof build: %d functions' % len(names))


FUZZ = os.path.join(HERE, 'fuzz')
FUZZ_REPORT = os.path.join(FUZZ, 'report.txt')


def calls_system(f):
    """1 if the function calls a system function the tester stands in for (SYSTEM), else 0: the tester then keeps track of the
    memory outside the frame from the start, for the stand-in to note it at each call (FuzzFunction.system_calls)."""
    return 1 if set(f['calls'].values()) & set(SYSTEM) else 0


def stand_in_double_bits(use):
    """(2026-11-12, agent "refused2") The stand-in's notes of the second halves of double parameters, f13 (0x40) and f15 (0x80), for
    a callee that reads them: a converted caller has to pass the double's whole 64 bits, which f12's and f14's 32 alone don't
    show (func_80032314 and func_8002C58C, passing a float made from a double, showed only in f12's word). 0 for a callee that
    reads neither, so every other stand-in's line is as it was."""
    return (0x40 if 'f13' in use else 0) | (0x80 if 'f15' in use else 0)


def stand_in_line(c, index, analysis, functions):
    """The tester's stand-in for a function the one under test calls, as a line of C++ (see fuzz_main.cpp). A game function's,
    fuzz_stand_in, notes the parameter registers its analysis says it reads and its stack parameters, then behaves as badly as the
    calling rules allow. A system function's (2026-11-07), fuzz_system_stand_in, notes the same from system_reads.model(), and the
    memory outside the caller's frame as it is at the call; then does to the registers exactly what the runtime's version does --
    random values in the result registers its source writes (bit 0 v0, bit 1 v1, bit 2 f0), and nothing else -- and writes random
    bytes over what it writes through its parameters (regions: parameter number, bytes; -1 ends them)."""
    use = set(analysis.get(c, {}).get('use', []))
    mask = sum(1 << i for i, r in enumerate(('a0', 'a1', 'a2', 'a3', 'f12', 'f14')) if r in use) | stand_in_double_bits(use)
    if c in SYSTEM:
        m = SYSTEM[c]
        results = sum(1 << i for i, r in enumerate(('v0', 'v1', 'f0')) if r in m['results'])
        regions = ''.join('{%d, 0x%X}, ' % (p, b) for p, b in m['writes'])
        return ('extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { static const FuzzRegion regions[] = {%s{-1, 0}}; '
                'fuzz_system_stand_in(%d, 0x%X, %d, %d, regions, ctx); }' % (c, regions, index, mask, m['stack'], results))
    slots = stack_parameters(functions[c]) if c in functions else 0
    return 'extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in(%d, 0x%X, %d, ctx); }' % (c, index, mask, slots)


def original_addresses(f):
    """The game addresses (0x80000000..0x80FFFFFF) a function's own MIPS makes, for the tester's address inputs (run_fuzz): every
    `lui` upper half, every value made from one by `addiu` / `ori`, and every fixed place a load or a store reaches through one (its
    offset added) -- the same values m2c folds into the constants of the converted text, worked out from the instructions instead.
    Over the function's control flow (mips.cfg, delay slots included), each register's possible values joined from every way in, so
    it is what any way through makes. (2026-11-12: the "tidy" agent's proposal, approved by the user the same day. Until then the
    addresses were every 0x80xxxxxx in the converted C++, so a change to the text -- tidy.py dropping a temporary -- could change its
    own test's inputs. Now the converted text can never change them. FUZZ_ADDR_FROM_TEXT=1: from the text, as before.)"""
    addrs, succ = cfg(f)
    preds = {}
    for x, ss in succ.items():
        for s in ss:
            preds.setdefault(s, []).append(x)
    state = {}
    work = [x for x in addrs if x not in preds] or addrs[:1]
    for x in work:
        state[x] = {}
    found = set()
    while work:
        pc = work.pop()
        st = dict(state[pc])
        mn, ops = f['ins'][pc]
        regs = re.findall(r'\$(\w+)', ops)
        if mn == 'lui' and regs:
            v = (int(ops.split(',')[-1], 0) << 16) & 0xFFFFFFFF
            found.add(v)
            st[regs[0]] = frozenset([v])
        elif mn in ('addiu', 'ori') and len(regs) == 2 and regs[1] in st:
            k = int(ops.split(',')[-1], 0)
            vs = frozenset(((v | (k & 0xFFFF)) if mn == 'ori' else (v + k) & 0xFFFFFFFF) for v in st[regs[1]])
            found |= vs
            # (A pointer stepped round a loop would take a new value every turn: past 32 values it is no fixed address any more, and
            #  m2c's text has no constant for it either -- its first values are in `found` already.)
            if len(vs) > 32:
                st.pop(regs[0], None)
            else:
                st[regs[0]] = vs
        elif mn in ('addu', 'or', 'daddu') and len(regs) == 3 and (regs[1] in st or regs[2] in st):
            # (a copy -- `addu $d, $s, $zero` -- or a known base plus an index: m2c writes the base as the constant and the index
            #  added to it, and an access's offset onto that base, so the base's values go on in d)
            vs = st.get(regs[1], frozenset()) | st.get(regs[2], frozenset())
            if len(vs) > 32:
                st.pop(regs[0], None)
            else:
                st[regs[0]] = vs
        else:
            place = re.match(r'\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)\(\$(\w+)\)', ops)
            if mn in ACCESS_BYTES and place and place.group(2) in st:
                found |= {(v + int(place.group(1), 0)) & 0xFFFFFFFF for v in st[place.group(2)]}
            for r in uses_defs(mn, ops)[1]:
                st.pop(r, None)
        for s in succ.get(pc, []):
            old = state.get(s)
            if old is None:
                state[s] = st
                work.append(s)
                continue
            merged = {r: old[r] | st[r] for r in set(old) & set(st)}
            merged.update({r: old[r] for r in old if r not in st})
            merged.update({r: st[r] for r in st if r not in old})
            if merged != old:
                state[s] = merged
                work.append(s)
    return {v for v in found if 0x80000000 <= v < 0x81000000}


# Per-function steering hints (2026-11-14, part 1 of the reach job's proposals; reviewed and merged
# onto 0.83 by rules084): numbers the tester should try as inputs for one function, which fuzz_constants /
# fuzz_compare_constants cannot find because they are not written in its instructions. They only add inputs (the dictionary,
# part 3's compare words, the places given random contents): the comparison, the counts and the bar are untouched, and each
# is in its function's cache key (the constants_ / compares_ / addresses_ lines). Each with why.
#   func_80121BA0: its divisor is 14 * s5 + 4, s5 a callee's result (func_8013DDB8 at 80121C98): 0x12492492 makes it 2**32,
#   that is 0, which reaches the compiler's `break 7` after the divisions at 80121F3C and 801220F0.
#   func_80025F40: walks 7 records of 0x8C bytes from 0x800A9C73 (and 0x800A9C9C) and 7 of 0xDC bytes from 0x80084630 by
#   index; only the first of each is an address its instructions make, so the other six keep what the tester's memory was
#   given once per process. With some seeds (FUZZ_SEED=9002, 9003: two of the long pass's four shards) one of those fixed
#   records sends every test past the access limit: no test of the first 256 is compared, the fallback starts, and the
#   shard crawls. 'addresses': every 0x60 bytes of both tables (the tester refreshes -0x20..+0x40 round each).
#   func_8010DAF8 (2026-11-14, part 2 of the reach job, "reach2"; put in this table by collect084): 0x8010E2C0-0x8010E2F0
#   (13 instructions) run when the menu call at 0x8010E210 (func_8010C928) hands back 0x1A or 0x1B in v0 (kept in s3) AND s0
#   is -1 at 0x8010E2AC (func_8013765C's result, or the halfword at 4(s2)). The compare is `sltiu (s3 - 0x1A), 2`, six events
#   after the call, so part 4's sources (the last five) don't hold it and only a dictionary draw for that call's result gets
#   there: 1 in ~55 dictionary draws without the hint. All three numbers are among the function's own already, so they go in
#   as 'repeat': added as given, every copy, to weigh them ('constants' drops a number the function has, and would drop these).
#   (Checked 2026-11-15, rules084: func_80121BA0's 0x12492492 is not among its own 64 constants, and 27 of func_80025F40's 28
#   addresses are not among its own -- both entries do add inputs.)
FUZZ_HINTS = {
    'func_80121BA0': {'constants': [0x12492492]},
    'func_80025F40': {'addresses': [0x800A9C70 + 0x60 * i for i in range(11)] + [0x80084630 + 0x60 * i for i in range(17)]},
    'func_8010DAF8': {'repeat': [0x1A] * 48 + [0x1B] * 48 + [0xFFFFFFFF] * 32},
}


def fuzz_constants(f, most=64):
    """The numbers in a function's own instructions, for the tester to try as inputs (see dictionary_word in fuzz_main.cpp): the
    immediates of its arithmetic, logic and compares, sign- or zero-extended as the instruction does, and lui's upper halves. Not
    the stack's offsets. Sorted, at most `most`, small ones first (a switch's cases are small numbers)."""
    found = set()
    for mn, ops in f['ins'].values():
        m = re.match(r'\$(\w+),\s*(?:\$(\w+),\s*)?(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if not m or m.group(2) == 'sp' or m.group(1) == 'sp':
            continue
        k = int(m.group(3), 0)
        if mn in ('addiu', 'addi', 'slti', 'sltiu'):
            found.add(k & 0xFFFFFFFF)
        elif mn in ('andi', 'ori', 'xori'):
            found.add(k & 0xFFFF)
        elif mn == 'lui':
            found.add((k << 16) & 0xFFFFFFFF)
    found.discard(0)
    return sorted(found, key=lambda k: (min(k, 0x100000000 - k), k))[:most]


COMPARES = {'beq', 'bne', 'beql', 'bnel', 'slt', 'sltu', 'slti', 'sltiu', 'blez', 'bgtz', 'bltz', 'bgez', 'blezl', 'bgtzl',
            'bltzl', 'bgezl', 'bltzal', 'bgezal'}


def mask_in(f, addrs, i, reg):
    """K, if the last instruction before addrs[i] (within 16) that writes reg is `andi $reg, $x, K`: a bit test, whose
    compare with zero changes when x has those bits (`andi $v0, $v1, 0x800; beq $v0, $zero`, a button held)."""
    for b in reversed(addrs[max(0, i - 16):i]):
        mn, ops = f['ins'][b]
        if reg in mips.uses_defs(mn, ops)[1]:
            m = re.match(r'\$(\w+),\s*\$(\w+),\s*((?:0[xX])?[0-9A-Fa-f]+)$', ops)
            return int(m.group(3), 0) & 0xFFFF if mn == 'andi' and m else None
    return None


def constant_in(f, addrs, i, reg):
    """The constant the function puts in reg before the instruction at addrs[i], if the last instruction before it (in address
    order, within 16) that writes reg is one that sets a constant: `addiu $r, $zero, K`, `ori $r, $zero, K`, `lui $r, H`, or
    lui with an addiu or ori to the same register. Only a guess at what the compare sees (another way in may set it otherwise),
    which is all an input needs to be."""
    low = None
    for b in reversed(addrs[max(0, i - 16):i]):
        mn, ops = f['ins'][b]
        if reg not in mips.uses_defs(mn, ops)[1]:
            continue
        m = re.match(r'\$(\w+),\s*\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if mn in ('addiu', 'ori') and m and m.group(2) == 'zero' and low is None:
            k = int(m.group(3), 0)
            return (k if mn == 'addiu' else k & 0xFFFF) & 0xFFFFFFFF
        if mn in ('addiu', 'ori') and m and m.group(2) == reg and low is None:
            low = (mn, int(m.group(3), 0))       # (the low half of lui + addiu/ori: the lui comes before it)
            continue
        m = re.match(r'\$(\w+),\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if mn == 'lui' and m:
            k = (int(m.group(2), 0) << 16) & 0xFFFFFFFF
            if low is not None:
                k = (k + low[1]) if low[0] == 'addiu' else (k | (low[1] & 0xFFFF))
            return k & 0xFFFFFFFF
        return None
    return None


def fuzz_compare_constants(f, most=96):
    """What the function's compares compare with, and the numbers either side, for the tester to try as inputs (2026-11-07;
    its part 3, compare_word in fuzz_main.cpp). A compare's outcome changes between K-1, K and K+1: only those
    inputs show whether the converted code compares what the original does -- the value from before a branch's delay slot
    changes it (func_8014434C: `bne $s0, $v0; addiu $s0, $s0, -1` with v0 = 4 differs only when s0 is 4 or 5). fuzz_constants
    has the numbers too, but among every immediate the function has, cut to 64, and with the neighbours only sometimes; here
    they are the compares' own, each with both neighbours.
    The constants: slti/sltiu's immediate; 0 for a compare with zero (bltz, blez..., or $zero as an operand); for a
    register operand, the constant the function put in it just before (constant_in); and for a bit test against zero, its
    mask (mask_in). Small ones first, at most `most`."""
    found = set()
    addrs = sorted(f['ins'])
    for i, a in enumerate(addrs):
        mn, ops = f['ins'][a]
        if mn not in COMPARES:
            continue
        values = []
        m = re.match(r'\$\w+,\s*\$\w+,\s*(-?(?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if mn in ('slti', 'sltiu') and m:
            values.append(int(m.group(1), 0) & 0xFFFFFFFF)
        regs = re.findall(r'\$(\w+)', ops.split('L_')[0])
        if mn in ('slt', 'sltu', 'slti', 'sltiu'):
            regs = regs[1:]          # (the first is where the result goes)
        if mn.startswith(('blez', 'bgtz', 'bltz', 'bgez')) or 'zero' in regs:
            values.append(0)
        for r in regs:
            if r != 'zero':
                k = constant_in(f, addrs, i, r)
                if k is not None:
                    values.append(k)
                if mn.startswith(('beq', 'bne')) and 'zero' in regs:
                    k = mask_in(f, addrs, i, r)
                    if k:
                        values.append(k)
        for k in values:
            found.update({(k - 1) & 0xFFFFFFFF, k, (k + 1) & 0xFFFFFFFF})
    # (Not 0, 1 and -1: random_word gives those often already.)
    found -= {0, 1, 0xFFFFFFFF}
    return sorted(found, key=lambda k: (min(k, 0x100000000 - k), k))[:most]


def param_kinds(v):
    """The parameters' kinds by number, for the tester: see FuzzFunction in fuzz/fuzz.h."""
    if v.get('native', {}).get('fast_params'):
        return ''       # (2026-11-13, modnative5: the user's rule (3) -- by register instead, native_rules_lines)
    kinds = {}
    params = [(t, n) for t, n in parse_signature(v['cpp'])[1] if n != 'ctx']
    for (t, n), source in zip(params, v['sources']):
        i = int(n[3:])
        m = re.match(r'P<(\w+)', t)
        if m:
            kinds[i] = {'s8': 'b', 'u8': 'b', 's16': 'h', 'u16': 'h'}.get(m.group(1), 'w')
            # (2026-11-15, modnative7, from the review of 0.84 (reviewnm084): a native candidate's pointer parameters are all
            #  P<u32> in its C++, whatever they point to; its kind is the C POINTEE's alignment instead (prove_gen.py
            #  pointer_kinds): void*, char*, a structure of bytes only 'b', halfwords 'h', anything else 'w', and 'b' when the
            #  pointee is not known -- every address drawn. Stricter: before, memcpy's void* d and s were never unaligned.)
            pk = v.get('native', {}).get('pointer_kinds', {})
            if str(i) in pk:
                kinds[i] = pk[str(i)]
        else:
            # (A float that came in an integer register or stack slot: 'g', its bits put there as a word, a number and never NaN.)
            # (A double in f12:f13 or f14:f15: 'd', 2026-11-12, agent "refused2".)
            kinds[i] = 'd' if t == 'f64' else ('f' if source.startswith('ctx->f') else 'g') if t == 'f32' else 'i'
    return ''.join(kinds.get(i, '.') for i in range(max(kinds) + 1)) if kinds else ''


def fuzz_input_registers(v):
    """The registers other than a0-a3 a candidate reads before it sets them, as a mask (GPR n: bit n), for the tester's part 4 to
    change (FuzzFunction's input_registers in fuzz/fuzz.h): abi.py's inputs (s4, v1, t0, ...) and gp for a function that reads it
    (its C++ reads ctx->r28 -- see LIFT_GP in candidates). 0 for most. 2026-11-08, scratch/lift_steer"""
    bits = abi.mask(v['abi']['inputs']) if v.get('abi') else 0
    # (2026-11-13, modnative5: the user's rule (3) -- a native fast-convention function's integer parameters past a0-a3)
    for w, _ in v.get('native', {}).get('fast_params', []):
        if w.startswith('r') and not 4 <= int(w[1:]) <= 7:
            bits |= 1 << int(w[1:])
    if 'ctx->r28' in v['cpp']:
        bits |= 1 << 28
    return bits & 0xFFFFFFFF


FUZZ_SIDE = r'(?:SIGNED\()?(ctx->r\d+|-?0X[0-9A-F]+|0)\)?'
FUZZ_BRANCH = re.compile(r'^(\s*)if \(' + FUZZ_SIDE + r' (?:==|!=|<|>=|<=|>) ' + FUZZ_SIDE + r'\) \{', re.M)
FUZZ_SET_LESS = re.compile(r'^(\s*)ctx->r\d+ = ' + FUZZ_SIDE + r' < ' + FUZZ_SIDE + r' \? 1 : 0;', re.M)


# An instruction's comment in a translated original and the count just after it: its address, and its number in reached[].
FUZZ_INSTRUCTION = re.compile(r'// 0x([0-9A-F]{8}): ([^\n]*)\n\s*FUZZ_REACHED\(reached\[(\d+)\]\);')
FUZZ_OPS = {'==': 0, '!=': 1, '<': 2, '>=': 3, '<=': 4, '>': 5}
# A float comparison as the translator writes it (c.eq.s, c.lt.d, c.le.s ...): the condition bit set from two float registers.
FUZZ_FLOAT_COMPARE = re.compile(r'^(\s*)c1cs = (ctx->f\d+\.(?:fl|d)) (==|<|<=) (ctx->f\d+\.(?:fl|d));', re.M)
# (2026-11-14, ultcheck. TESTER CHANGE, only with N64Recomp's unordered-compare fix) c.ult / c.ule as the N64 does them,
#  `c1cs = !(a >= b);` and `c1cs = !(a > b);`: noted for part 4 as the compare they turn round (< and <=), so its steering
#  still sees them. (c.un and c.ueq, `a != a || b != b` and `!(a < b || a > b)`, are not noted: the mod's c.un tests a value
#  against itself, nothing to aim at.)
FUZZ_FLOAT_COMPARE_UNORDERED = re.compile(r'^(\s*)c1cs = !\((ctx->f\d+\.(?:fl|d)) (>=|>) (ctx->f\d+\.(?:fl|d))\);', re.M)


def fuzz_compares(text):
    """The translated original with FUZZ_COMPARE(a, b) before each branch that compares two registers or a register with a number
    (`if (ctx->r2 != ctx->r4) {`, `if (SIGNED(ctx->r3) < 0) {`) and each set-less-than (`ctx->r2 = SIGNED(ctx->r3) < 0X46 ? 1 : 0;`):
    the tester's steered part notes both sides, to find where one came from and put the other there (input_to_state in
    fuzz/fuzz_main.cpp). The note goes before the comparison, so it sees the registers as the comparison does. 2026-11-07
    Since 2026-11-08 (scratch/lift_steer), for the tester's part 4, the note also says what kind of comparison it is (0 equal or
    not, 1 a signed less-than -- the translator writes SIGNED() for slt and the branches on a sign -- 2 an unsigned one, sltu):
    FUZZ_COMPARE_KIND for a set-less-than. A branch's is FUZZ_BRANCH_AT, which also gives its test (FUZZ_OPS, + 8 when signed)
    and the numbers in reached[] of the instruction each way goes to -- its target, and the one after its delay slot -- or -1
    where that isn't found: so part 4 can tell which way no test has gone yet, and aim at it. A float comparison's is
    FUZZ_FLOAT_COMPARE, with the two values and its test."""
    def kind_of(whole):
        return 0 if (' == ' in whole or ' != ' in whole) else 1 if 'SIGNED(' in whole else 2

    def one_function(chunk):
        index = {}
        for m in FUZZ_INSTRUCTION.finditer(chunk):
            index.setdefault(int(m.group(1), 16), int(m.group(3)))

        def branch(m):
            whole = m.group(0)[len(m.group(1)):]
            op = FUZZ_OPS[re.search(r' (==|!=|<|>=|<=|>) ', whole).group(1)] + (8 if 'SIGNED(' in whole else 0)
            # (The branch: the last instruction before this `if`. Its target from its label; its other way, 8 bytes on.)
            taken = fall = -1
            near = list(FUZZ_INSTRUCTION.finditer(chunk, max(0, m.start() - 600), m.start()))
            if near:
                at = int(near[-1].group(1), 16)
                label = re.search(r'\bL_([0-9A-F]{8})\b', near[-1].group(2))
                if label:
                    taken = index.get(int(label.group(1), 16), -1)
                fall = index.get(at + 8, -1)
            return '%sFUZZ_BRANCH_AT(%s, %s, %d, %d, %d, %d); %s' % (m.group(1), m.group(2), m.group(3), kind_of(whole), op, taken, fall, whole)

        def set_less(m):
            whole = m.group(0)[len(m.group(1)):]
            return '%sFUZZ_COMPARE_KIND(%s, %s, %d); %s' % (m.group(1), m.group(2), m.group(3), kind_of(whole), whole)
        # (A float comparison: FUZZ_FLOAT_COMPARE, with its test as FUZZ_OPS has it. Its branch, bc1t or bc1f, comes later.)
        def float_compare(m):
            whole = m.group(0)[len(m.group(1)):]
            return '%sFUZZ_FLOAT_COMPARE(%s, %s, %d); %s' % (m.group(1), m.group(2), m.group(4), FUZZ_OPS[m.group(3)], whole)
        def unordered_compare(m):
            whole = m.group(0)[len(m.group(1)):]
            return '%sFUZZ_FLOAT_COMPARE(%s, %s, %d); %s' % (m.group(1), m.group(2), m.group(4), FUZZ_OPS['<' if m.group(3) == '>=' else '<='], whole)
        return FUZZ_FLOAT_COMPARE_UNORDERED.sub(unordered_compare,
                                                FUZZ_FLOAT_COMPARE.sub(float_compare, FUZZ_SET_LESS.sub(set_less, FUZZ_BRANCH.sub(branch, chunk))))
    # (Each original on its own: the addresses of overlays' copies can repeat.)
    pieces = re.split(r'(?m)^(?=static void original_)', text)
    return ''.join(one_function(piece) for piece in pieces)


def fuzz_shards(name):
    """(Proposed 2026-11-12 by agent "tail", APPROVED by the user 2026-11-12; off unless LIFT_SHARDS is set: night.ps1's longer pass.) How many tester processes one
    function's tests are split across: LIFT_SHARDS=K, for the functions listed in LIFT_SHARD_ONLY (a file of names; all when
    unset). Each shard runs tests / K of its own random tests (its own FUZZ_SEED: shard 0 the default sequence, shard i seed
    9000 + i), every one exactly as a single run makes and compares them -- the original's whole work, the same limits -- and
    the lines are added up as read_reports already adds reports up: tests, compared, differences and plain tests summed,
    reached instructions or-ed. So 200,000 plain random compared tests stay 200,000, made K at a time on K cores: for the
    functions whose tests are slow, not rare (func_801067B4_ovl_c: ~118 a second; func_8001EB18: ~7)."""
    k = int(os.environ.get('LIFT_SHARDS', '1'))
    only = os.environ.get('LIFT_SHARD_ONLY')
    if k <= 1 or (only and name not in set(open(only).read().split())):
        return 1
    return k


# ---- Game shards on idle threads (2026-11-17, agent shardgame: the user's approval of 2026-11-17 to split a slow game
#      function's random tests across idle threads; TESTER CHANGE in how runs are made and filed -- nothing counts from it
#      until its independent review: scratch/agents/shardgame/REVIEW_SHARDGAME.txt) ----
# LIFT_GAME_SHARDS=K (2..64): every function this fuzz pass runs is K tester processes, exactly as fuzz_shards makes one
# function's K shards -- shard 0 the default sequence, shard i seed 9000 + i (FUZZ_SEED=s: s * 1000 + i), tests / K each, the
# same tester, arguments and limits -- but the K x (functions) processes go into ONE pool of LIFT_WORKERS, slowest functions
# first (game_shard_order), so the pass keeps every worker busy to its end instead of holding K per function with the
# workers cut to a quarter (night.ps1's longer passes). The lines are added up as fuzz_one adds them, with two strict rules
# more (game_shard_line): a shard short of its share makes the whole short, and one in another form makes the whole not done.
# Its lines are kept apart: their own keys (fuzz_tester_hash) and their own place in the cache file (run_fuzz's slot), so an
# unsharded run never overwrites them, nor they it. LIFT_GAME_SHARDS_READ=K: a pass that is not sharded (night.ps1's first)
# also takes a K-shard line that met the bar -- proved or DIFFERENT, under its own key, made with at least the time asked
# for now -- from that place, as the reach pass's lines are taken from theirs. Unset (the default) both: every key, line,
# place and run exactly as before.
GAME_SHARDS = int(os.environ.get('LIFT_GAME_SHARDS', '0') or '0')
GAME_SHARDS_READ = int(os.environ.get('LIFT_GAME_SHARDS_READ', '0') or '0')
GAME_SHARDS_MOST = 64       # (the seeds checked: scratch/agents/shardgame/seeds.py places every start of 9001..9063 exactly)
for _k in (GAME_SHARDS, GAME_SHARDS_READ):
    if _k and not 2 <= _k <= GAME_SHARDS_MOST:
        raise SystemExit('LIFT_GAME_SHARDS / LIFT_GAME_SHARDS_READ: 2 to %d (or unset)' % GAME_SHARDS_MOST)
if (GAME_SHARDS or GAME_SHARDS_READ) and int(os.environ.get('LIFT_SHARDS', '1')) > 1:
    raise SystemExit('LIFT_GAME_SHARDS and LIFT_SHARDS together: one or the other')
if GAME_SHARDS and GAME_SHARDS_READ:
    raise SystemExit('LIFT_GAME_SHARDS_READ is for a pass that is not sharded')


def game_shard_seed(i):
    """Shard i's FUZZ_SEED, as fuzz_one gives it: None (the default sequence) for 0; 9000 + i, or s * 1000 + i under FUZZ_SEED=s."""
    if i == 0:
        return None
    second = os.environ.get('FUZZ_SEED')
    return int(second) * 1000 + i if second else 9000 + i


def game_shard_line(name, parts, per, tests):
    """One function's line from its K shards' (name, line, trouble), in shard order. As fuzz_one: a shard that crashed or timed
    out is the whole function's result (a crash a difference, a timeout no line). Then: every shard's line must be in the
    report's form (8 fields, the function's name and instruction count) or the whole has no line ('not done'); the counts are
    summed, the reached instructions or-ed, the first difference is the first shard's that has one. The plain-random count
    (8th field) is each shard's up to its share `per` (a shard past it only by the NaN make-up of part 1), and if any shard
    is short of its share the whole is short: under `tests`, so never proved.
    (shardfold 2026-11-17, the review's fixes 2 and 3 -- scratch/agents/reviewsh/REVIEW_REVIEWSH.txt:) a crash in ANY shard is the
    whole's line, even when an earlier shard timed out (a known crash is never hidden as 'not done'); the counts (fields 1, 2, 3
    and 7) must be plain non-negative decimal numbers, the reached mask hexadecimal digits, the instruction count a decimal number,
    and the first-difference field '-' exactly when that shard's differences are 0 (the tester sets both together: note() in
    fuzz_main.cpp) -- so the summed differences are 0 exactly when the whole's first difference is '-'. Anything else: no line."""
    crashed = next((part for part in parts if part[2] == 'crashed'), None)
    if crashed is not None:
        return crashed
    for part in parts:
        if part[2]:
            return part
    bad = (name, None, 'a shard line not in the report form')
    number, hexdigits = re.compile(r'[0-9]+'), re.compile(r'[0-9a-fA-F]+')
    fields = [p[1].split() for p in parts]
    if not fields or any(len(f) != 8 or f[0] != name or f[5] != fields[0][5] for f in fields):
        return bad
    for f in fields:
        if not all(number.fullmatch(f[i]) for i in (1, 2, 3, 5, 7)) or not hexdigits.fullmatch(f[4]):
            return bad
        if (f[6] == '-') != (int(f[3]) == 0):
            return bad
    plain = [int(f[7]) for f in fields]
    mask = 0
    for f in fields:
        mask |= int(f[4], 16)
    add = lambda i: sum(int(f[i]) for f in fields)
    counted = sum(min(p, per) for p in plain)
    if min(plain) < per:
        counted = min(counted, tests - 1)
    first = next((f[6] for f in fields if f[6] != '-'), '-')
    if (first == '-') != (add(3) == 0):
        return bad
    return name, '%s %d %d %d %x %s %s %d\n' % (name, add(1), add(2), add(3), mask, fields[0][5], first, counted), ''


def game_shard_order(run):
    """The order the functions' shards go into the pool: the slowest first, so the last to finish are short ones. Slowest: the
    fewest plain random tests in the report as it is now (the last pass's line: these are the ones its time stopped), none
    first. Only the order of the runs: no line depends on it (the testers' limits are CPU time)."""
    seen = {}
    if os.path.exists(FUZZ_REPORT):
        for line in open(FUZZ_REPORT):
            f = line.split()
            if len(f) == 8 and f[7].isdigit():
                seen[f[0]] = int(f[7])
    return sorted(run, key=lambda n: (seen.get(n, -1), n))


def game_shard_results(exe, run, tests, sizes):
    """(name, line, trouble) for each function of `run`, in run's order, each made of GAME_SHARDS shards run in one pool of
    TEST_WORKERS (see GAME_SHARDS above)."""
    k = GAME_SHARDS
    per = -(-tests // k)
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=TEST_WORKERS)
    try:
        futures = {}
        for n in game_shard_order(run):
            futures[n] = [pool.submit(fuzz_one_process, (exe, n, per, sizes[n], game_shard_seed(i))) for i in range(k)]
        for n in run:
            yield game_shard_line(n, [f.result() for f in futures[n]], per, tests)
    finally:
        for fs in futures.values():
            for f in fs:
                f.cancel()
        pool.shutdown(wait=True)


def fuzz_one(job):
    exe, name, tests, total = job
    shards = fuzz_shards(name)
    if shards > 1:
        # (See fuzz_shards. A shard that crashed or timed out makes the whole line that, as for one run.)
        per = -(-tests // shards)
        with concurrent.futures.ThreadPoolExecutor(max_workers=shards) as pool:
            # (PROPOSED 2026-11-14, from the offline mod part 1's P2, written by collect084 -- a TESTER CHANGE, for the user: under
            #  FUZZ_SEED=s a shard i > 0 had seed 9000 + i, the default run's, so a second-seed run split into shards repeated the
            #  first run's sequences in every shard but shard 0. With FUZZ_SEED set, shard i is seed s * 1000 + i; unset, as before.)
            second = os.environ.get('FUZZ_SEED')
            parts = list(pool.map(fuzz_one_process, [(exe, name, per, total, None if i == 0 else (int(second) * 1000 + i if second else 9000 + i))
                                                     for i in range(shards)]))
        for part in parts:
            if part[2]:
                return part
        fields = [p[1].split() for p in parts]
        mask = 0
        for f in fields:
            mask |= int(f[4], 16)
        first = next((f[6] for f in fields if f[6] != '-'), '-')
        add = lambda i: sum(int(f[i]) for f in fields)
        line = '%s %d %d %d %x %s %s %d\n' % (name, add(1), add(2), add(3), mask, fields[0][5], first, add(7))
        return name, line, ''
    return fuzz_one_process((exe, name, tests, total, None))


def fuzz_one_process(job):
    exe, name, tests, total, seed = job
    env = None
    if seed is not None:
        env = dict(os.environ, FUZZ_SEED=str(seed))
    try:
        # (220 s covers the tester's own 40 s limits for parts 1, 2 and 4; with FUZZ_MS, the long-run pass, it grows with it)
        # (x4 since 2026-11-10: those limits are the tester's CPU time now, and at low priority on a busy PC its wall time can be
        #  several times that. A loop that never ends still ends here: this only stops one that's truly stuck.)
        limit = 4 * (220 + 2 * int(os.environ.get('FUZZ_MS', '40000')) // 1000)
        p = subprocess.run([exe, name, str(tests)], capture_output=True, text=True, timeout=limit, env=env)
    except subprocess.TimeoutExpired:
        return name, None, 'timed out: a loop on random data, probably'
    if p.returncode != 0 or not p.stdout.strip():
        # It crashed. Every address is valid there, so that's not a wild pointer: treat it as a difference.
        return name, '%s %d 0 1 0 %d crashed:%X\n' % (name, tests, total, p.returncode & 0xFFFFFFFF), 'crashed'
    # (The step limit, 2026-11-14, rules084: how many of this run's tests it set aside, kept beside the report -- one line per
    #  tester process, with its seed -- so the count is on record for the reviewer and the release notes.)
    noted = [l for l in p.stderr.splitlines() if l.startswith('step limit set aside ')]
    if noted:
        with STEPS_NOTED_LOCK:
            with open(os.path.join(FUZZ, 'steps_set_aside.txt'), 'a', newline='\n') as f:
                f.write(''.join('%s (seed %s, %d tests)\n' % (l, seed if seed is not None else os.environ.get('FUZZ_SEED', 'default'), tests)
                                for l in noted))
    return name, p.stdout, ''


STEPS_NOTED_LOCK = threading.Lock()


def fuzz_original_text(text):
    """A translated original (or several) as the tester runs it: originals_with_notes' text with the tester's changes. Each change
    works line by line, so a function's part of the text comes out the same on its own as among the rest (the result cache,
    run_fuzz, hashes each function's part)."""
    # A division in the originals divides as the N64 does by zero (FUZZ_DIV in fuzz/fuzz.h), so the original goes on to the
    # compiler's check and its break, where the test stops; the translator's line would stop the PC on the division itself.
    # (The translator writes these two lines for div and divu, and nothing else.)
    text = re.sub(r'lo = S32\(S64\(S32\((ctx->r\d+)\)\) / S64\(S32\((ctx->r\d+)\)\)\); hi = S32\(S64\(S32\(\1\)\) % S64\(S32\(\2\)\)\);',
                  r'FUZZ_DIV(\1, \2);', text)
    text = re.sub(r'lo = S32\(U32\((ctx->r\d+)\) / U32\((ctx->r\d+)\)\); hi = S32\(U32\(\1\) % U32\(\2\)\);', r'FUZZ_DIVU(\1, \2);', text)
    # (Here each instruction counts how many times it ran, not just that it did: the tester's steered part keeps a test that ran one
    #  a new number of times, which is how a loop shows it's getting further. See FUZZ_REACHED in fuzz/fuzz.h. 2026-11-07)
    text = re.sub(r'reached\[(\d+)\] = 1;', r'FUZZ_REACHED(reached[\1]);', text)
    # (2026-11-13, final082. TESTER CHANGE, approved by the user) After each float multiplication, division, addition, subtraction
    #  and square root of the originals, FUZZ_NAN_MADE (fuzz/fuzz.h): a NaN made there from numbers sets the test aside, as a NaN
    #  operand's does -- the N64 stops on that invalid operation, and which NaN comes out is the CPU's, not the game's.
    #  (The translator writes these as MUL_S / MUL_D / DIV_S / DIV_D, sqrtf / sqrt, and a + b / a - b of two registers.)
    text = re.sub(r'(ctx->f\d+\.(?:fl|d)) = ((?:MUL_S|MUL_D|DIV_S|DIV_D|sqrtf|sqrt)\([^;]*\)|ctx->f\d+\.(?:fl|d) [+-] ctx->f\d+\.(?:fl|d));',
                  r'\1 = \2; FUZZ_NAN_MADE(\1);', text)
    return fuzz_compares(text)


# The fixed text run_fuzz writes into every tester (and how it compiles it). In one place so the result cache's key can hash it:
# text written only inside run_fuzz is in no function's key, and a change to it (the memory macros below, say) would leave every
# cached proof standing. New fixed text that goes into the generated file goes in here too.
FUZZ_GEN_TEXT = {
    'top': ['#include "fuzz.h"', '#include "lift_call.h"', ''],
    'cstring': ['#include <cstring>', ''],
    'game_open': ['namespace game {', ''],
    'game_close': ['}  // namespace game', ''],
    'memory': ['#undef MEM_W', '#undef MEM_H', '#undef MEM_B', '#undef MEM_HU', '#undef MEM_BU',
               '#define MEM_W(o, r) (FuzzMem<int32_t>{rdram, (uint64_t)((r) + (o)), 0})',
               '#define MEM_H(o, r) (FuzzMem<int16_t>{rdram, (uint64_t)((r) + (o)), 2})',
               '#define MEM_B(o, r) (FuzzMem<int8_t>{rdram, (uint64_t)((r) + (o)), 3})',
               '#define MEM_HU(o, r) (FuzzMem<uint16_t>{rdram, (uint64_t)((r) + (o)), 2})',
               '#define MEM_BU(o, r) (FuzzMem<uint8_t>{rdram, (uint64_t)((r) + (o)), 3})',
               '#undef SD',
               '#define SD(val, o, r) { FuzzMem<uint32_t>{rdram, (uint64_t)((r) + (o) + 4), 0} = (uint32_t)((gpr)(val)); FuzzMem<uint32_t>{rdram, (uint64_t)((r) + (o)), 0} = (uint32_t)((gpr)(val) >> 32); }',
               # (The originals' 64-bit loads, ldc1: the same two words read the same way, and noted for the tester's part 4 only --
               #  until 2026-11-08 they went past FuzzMem, and part 4 couldn't find a double the original compared: fuzz_load_doubleword
               #  in fuzz/fuzz_main.cpp. Nothing else about them changes: not counted as accesses, as before.)
               '#undef LD',
               '#define LD(offset, reg) fuzz_load_doubleword(rdram, (offset), (reg))'],
    'originals': ['#include "originals.inc"', ''],
    'table_start': ['FuzzFunction fuzz_functions[] = {'],
    'table_end': ['};', 'const int fuzz_function_count = sizeof(fuzz_functions) / sizeof(fuzz_functions[0]);', ''],
}
# (The tester's own code, fuzz_main.cpp, is built with more: it doesn't change what a function's two versions do, and all the random numbers
#  and bookkeeping are in it. The generated file -- the two versions under test -- stays at FUZZ_COMPILE_FLAGS' -O1. scratch/lift_fast, 2026-11-09)
FUZZ_HARNESS_LEVEL = '-O2'
FUZZ_COMPILE_FLAGS = ['-std=c++20', '-O1', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-Wno-everything', '-DARCADE_LIFT_FUZZ',
                      '-DNDEBUG', '-static']
# (2026-11-16, agent argcheck: the argument compare, FUZZ_ARGS_STRICT=1 -- approved by the user 2026-11-16, nothing counts from it
#  until its independent review: scratch/agents/argcheck/REVIEW_ARGCHECK.txt. See "The argument compare" in fuzz/fuzz_main.cpp.
#  Off: no flag, every line and key as before.)
ARGS_STRICT = os.environ.get('FUZZ_ARGS_STRICT') == '1'
if ARGS_STRICT:
    FUZZ_COMPILE_FLAGS = FUZZ_COMPILE_FLAGS + ['-DFUZZ_ARGS_STRICT=1']


# ---- The random-input tester's result cache (2026-11-08) ----
# Every pipeline run tested every candidate again from scratch, though most are byte for byte what they were the run before. So
# run_fuzz keeps each function's report line in fuzz/cache/results.json (rebuilt freely: delete it to start again), with a key: a
# hash of everything its test is made from (fuzz_cache_key). A later run uses the line again only when the key is the same, and
# only a line that met the bar: proved (no difference, every instruction reached, at least FULL_TESTS compared, the same test as
# score() makes) or DIFFERENT. A function short of the count, partly reached, never run, timed out or crashed is run again every
# time. LIFT_NO_CACHE=1: run everything (the cache is still written).
#
# Policy on time (FUZZ_MS, FUZZ_STEER_MS): kept beside the line, not in the key, and a line is used again when it was made with at
# least the time asked for now. So a line the long pass proved (night.ps1, FUZZ_MS=300000) IS used again in the next normal pass
# (40 seconds), even though a fresh 40-second run might stop short of the count: without that, the function would fall short
# and go to the long pass again every night. The proof it keeps is a real one: the line recorded the full count, every
# instruction reached and no difference, for the same inputs. What's accepted is that a fresh run with less time might not
# repeat it. It doesn't work the other way: a line made with less time than is asked for now runs again.
FUZZ_CACHE_VERSION = 'fuzz-cache-2'     # (2: the narrow keys, fastnight 2026-11-14 -- see fuzz_tester_hash)
FUZZ_TIME_SETTINGS = {'FUZZ_MS': 40000, 'FUZZ_STEER_MS': 40000}     # (with the tester's defaults)


UNREACH_ROOTS = os.path.join(HERE, 'unreach_roots.json')
_unreach_roots = None


def never_returns(f):
    """1 if a function has no return instruction (no `jr $ra`): a thread's body (func_80000E3C, the VI manager's loop), whose tests
    can't end by themselves -- the tester then ends each at its K-th call (fuzz_main.cpp cut_call; FuzzFunction.never_returns).
    0 for every other function, whose tests stay exactly as they were. (2026-11-12, the rule the user approved.)"""
    return 0 if any(mn == 'jr' and ops.strip() == '$ra' for mn, ops in f['ins'].values()) else 1


def unreach_dead(name, f):
    """The addresses unreach.py proves can never run in this function (strict mode; nothing for the mod's copies or a function it
    refuses). unreach_roots.json gives the other ways into it (unreach_roots.py)."""
    global _unreach_roots
    if modrep.is_modrep(name):
        # (Rule R, proposed 2026-11-12 by agent "partial", approved by the user 2026-11-12. One of the mod's copies is only ever
        #  entered at its start -- the mod's loader puts it in place of the game function, the recompiler's lookup finds a function
        #  by its first address only, and its branches and jump tables stay inside it -- and no other way into it is recorded
        #  (unreach_roots is for the game's functions). So the checker, run with no other ways in, proves what it proves for any
        #  function. Measured on report_078_final: 230 instructions dead in 75 copies, none ever reached by a test; 4 copies whole.)
        if not unreach.RULE_R:
            return set()
        rule_p_ready()
        why, dead = unreach.dead(f, (), keep_saved=False)
        return set() if why else set(dead)
    if _unreach_roots is None:
        _unreach_roots = {n: set(v) for n, v in json.load(open(UNREACH_ROOTS))['roots'].items()} if os.path.exists(UNREACH_ROOTS) else {}
    rule_p_ready()
    # (Rule E, proposed 2026-11-12 by agent "partial2", APPROVED by the user 2026-11-12: on. In the port a game function is entered only at its
    #  first instruction, as rule R says of the mod's copies: see unreach.RULE_E. So no other ways in.)
    why, dead = unreach.dead(f, () if unreach.RULE_E else _unreach_roots.get(name, ()), keep_saved=False)
    return set() if why else set(dead)


def rule_p_ready():
    """Rule P (proposed 2026-11-12 by agent "partial2", APPROVED by the user 2026-11-12: on): the functions proved to give s0-s7, fp and sp back,
    worked out once from every function's MIPS (unreach.preserving_functions). Part 1 only: the mod's copies (P_COPIES) and
    the native functions (P_NATIVE) are separate proposals."""
    if unreach.RULE_P and not unreach.PRESERVING:
        unreach.PRESERVING, unreach.P_SUMMARY, unreach.P_ENTRIES = unreach.preserving_functions(to_asm.read_functions(), patched_by_mod())


# ---- Narrow cache keys (2026-11-14, fastnight) ----
# Until 0.84 every key shared a hash of fuzz/'s sources and of every header in four folders, used or not, so any change there --
# a comment, a header no test includes, or code only the mod's native functions run (0.83's) -- tested all ~3,400 functions again
# ("0 from the cache"). Now the shared part is the tester exactly as the compiler sees it for the build that runs the function:
# fuzz_main.cpp and the generated file's fixed includes, each PREPROCESSED with that build's flags (clang -E -P -dD: every header
# it really includes, every macro definition kept, comments and #if-ed out code gone). Two builds (fuzz.h FUZZ_MOD_NATIVE): 1, the
# whole tester, for a function with a private table, native rules or the mod's image (fuzz_variant); 0 for every other, with the
# native-only code left out. So a change only to code under "#if FUZZ_MOD_NATIVE" tests again only the native functions, a header
# no test includes nothing, and a change that reaches the code a function's test compiles still tests it again. A function whose
# own text #includes a header (the native ones: nm_native.h) has that header, preprocessed in place, in its own part of the key.
# LIFT_FUZZ_SPLIT=0: every function in the whole tester (1), as before 0.84 (the off-switch, and the A/B check: fastnight's tests).
FUZZ_SPLIT = os.environ.get('LIFT_FUZZ_SPLIT', '1') != '0'


def fuzz_variant(name, v):
    """1 (the whole tester) for a function whose test uses the native-only code -- a private table, native rules, the mod's image
    (lift.py writes those only for candidates with 'private' or 'native') -- or with LIFT_FUZZ_SPLIT=0; 0 for every other."""
    return 1 if (not FUZZ_SPLIT or v.get('native') or v.get('private')) else 0


def fuzz_is_control(name, v):
    """A negative or positive control (prove_gen.py negatives: a deliberately broken copy, which the tester must find DIFFERENT):
    run fresh every time, never taken from the cache or kept in it -- its point is to check the tester as it is now."""
    return bool(v.get('native', {}).get('negative')) or name.startswith(('modrep_natneg', 'modrep_natpos'))


def fuzz_include(build):
    """The include folders of the tester's build (run_fuzz's compile_tester), in their order."""
    return [os.path.join(PORT, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), os.path.join(PORT, 'include'), GAME, FUZZ, build]


_preprocessed = {}


def fuzz_preprocessed(source, level, variant, build, text=None, flags=None):
    """The source as the compiler sees it in the tester's build: clang -E -P -dD with the build's flags, its -O level (which sets
    __OPTIMIZE__), FUZZ_MOD_NATIVE and include folders. `text`: written to `source` first (a file in the build folder, so its
    quoted includes are found as the generated file's are). The bytes, or SystemExit -- no key is made from a tester that can't be
    read (it couldn't be built either)."""
    if text is not None:
        if not os.path.exists(source) or open(source, encoding='utf8', newline='').read() != text:
            open(source, 'w', newline='\n').write(text)
    st = os.stat(source)     # (a file changed since: read again. Only one process's calls are remembered.)
    memo = (source, st.st_mtime_ns, st.st_size, level, variant, build, text, tuple(flags or ()))
    if memo in _preprocessed:
        return _preprocessed[memo]
    flags = [level if f == '-O1' else f for f in (flags or FUZZ_COMPILE_FLAGS) if f != '-static'] + ['-DFUZZ_MOD_NATIVE=%d' % variant]
    command = [CLANG] + flags + ['-E', '-P', '-dD', source]
    for folder in fuzz_include(build):
        command += ['-I', folder]
    p = subprocess.run(command, capture_output=True)
    if p.returncode != 0 or not p.stdout:
        print(p.stderr.decode('utf8', 'replace')[:3000])
        raise SystemExit('the tester could not be preprocessed for its cache key: %s' % os.path.basename(source))
    _preprocessed[memo] = p.stdout
    return p.stdout


def fuzz_toolchain_identity():
    """The compiler and what -static links in: clang's path, size, date and version, and the C++ and unwind libraries' size and
    date (a toolchain update changes how the same text compiles and runs)."""
    st = os.stat(CLANG)
    out = '\0clang\0%s\0%d\0%d\0' % (os.path.relpath(CLANG, ROOT).replace('\\', '/'), st.st_size, int(st.st_mtime))
    out += subprocess.run([CLANG, '--version'], capture_output=True, text=True).stdout.split('\n')[0]
    for lib in ('libc++.a', 'libunwind.a'):
        path = subprocess.run([CLANG, '-print-file-name=%s' % lib], capture_output=True, text=True).stdout.strip()
        if path and os.path.exists(path):
            st = os.stat(path)
            out += '\0%s\0%d\0%d' % (lib, st.st_size, int(st.st_mtime))
        else:
            out += '\0%s\0missing' % lib
    return out


def fuzz_tester_hash(tests, variant=1, build=None, game_shards=None):
    """The part of a function's cache key that every function of its build (FUZZ_MOD_NATIVE = variant) shares: the tester as the
    compiler sees it -- fuzz_main.cpp and the generated file's fixed includes, preprocessed (fuzz_preprocessed) -- the compiler and
    its libraries, the flags, the fixed text written into every tester, the tests asked for, and every FUZZ_ setting in the
    environment. (game_shards: the LIFT_GAME_SHARDS count the key is for -- GAME_SHARDS when not given, 0 for none.)"""
    if game_shards is None:
        game_shards = GAME_SHARDS
    build = build or os.path.join(FUZZ, 'build')
    h = hashlib.sha256(FUZZ_CACHE_VERSION.encode())
    h.update(('\0variant\0%d\0' % variant).encode())
    h.update(b'\0harness\0' + fuzz_preprocessed(os.path.join(FUZZ, 'fuzz_main.cpp'), FUZZ_HARNESS_LEVEL, variant, build))
    prologue = '\n'.join(FUZZ_GEN_TEXT['top'] + FUZZ_GEN_TEXT['cstring']) + '\n'
    h.update(b'\0generated\0' + fuzz_preprocessed(os.path.join(build, 'fuzz_key_prologue.cpp'), '-O1', variant, build, prologue))
    h.update(fuzz_toolchain_identity().encode())
    h.update(('\0flags\0%r\0' % (FUZZ_COMPILE_FLAGS + [FUZZ_HARNESS_LEVEL],)).encode())
    h.update(('\0tests\0%d\0' % tests).encode())
    if int(os.environ.get('LIFT_SHARDS', '1')) > 1:
        # (fuzz_shards, proposed 2026-11-12: a split run's lines are other tests than one run's, so they get keys of their own.
        #  Without LIFT_SHARDS every key is what it was.)
        only = os.environ.get('LIFT_SHARD_ONLY')
        h.update(('\0shards\0%s\0%s\0' % (os.environ['LIFT_SHARDS'], open(only).read() if only else '')).encode())
        if os.environ.get('FUZZ_SEED'):
            # (2026-11-14, modnative7, with collect084's P2 above, approved 00:40: a FUZZ_SEED run's shards are its own seeds now
            #  (fuzz_one), so their cached lines -- made with 9000 + i -- must not be read back as theirs)
            h.update(b'\0seed family: s * 1000 + i\0')
    if game_shards:
        # (LIFT_GAME_SHARDS, shardgame 2026-11-17: K shards, the seed family, and the combining rule of game_shard_line -- so a
        #  K-shard line has a key no unsharded line, no LIFT_SHARDS line and no other K has. Without it every key is as before.)
        h.update(('\0game shards\0%d\0seeds: default, 9000 + i; FUZZ_SEED=s: s * 1000 + i\0plain: each shard up to tests/K, '
                  'any shard short -> short\0' % game_shards).encode())
    # (The unreachable-line checker and its ways in are NOT here since 2026-11-10: what they claim for a function is in that
    #  function's own part of the key -- its never_runs line, run_fuzz's own[name] -- so a change to them now tests again only the
    #  functions whose claimed instructions changed, not every function.)
    for k in sorted(os.environ):
        if k.startswith('FUZZ_') and k not in FUZZ_TIME_SETTINGS:      # (the time is kept beside the line: run_fuzz)
            h.update(('\0env\0%s=%s\0' % (k, os.environ[k])).encode())
    # (The fixed texts that go into every build.)
    h.update(('\0helpers\0' + '\n'.join(OVERLAY_HELPER) + '\0' + '\n'.join(TAIL_HELPER) + '\0' + abi.CPP_HELPERS + '\0' +
              '\n'.join(UNALIGNED_HELPERS) + '\0' + '\n'.join(UNALIGNED_ORIGINALS) + '\0' + json.dumps(FUZZ_GEN_TEXT, sort_keys=True)).encode())
    return h.hexdigest()


INCLUDE_IN_TEXT = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"][^>"\n]+[>"]', re.M)


def fuzz_text_includes(texts, variant, build):
    """The headers a function's own text #includes, as its build compiles them (preprocessed after the fixed includes, as they
    come in the generated file), for its key: their names and a hash of that text; '' for a text with none (every game function)."""
    found = INCLUDE_IN_TEXT.findall('\n'.join(texts))
    if not found:
        return ''
    text = '\n'.join(FUZZ_GEN_TEXT['top'] + FUZZ_GEN_TEXT['cstring'] + sorted(set(f.strip() for f in found))) + '\n'
    tag = hashlib.sha1(text.encode()).hexdigest()[:12]
    seen = fuzz_preprocessed(os.path.join(build, 'fuzz_key_includes_%s.cpp' % tag), '-O1', variant, build, text)
    return 'includes %s: %s' % (' '.join(sorted(set(f.strip() for f in found))), hashlib.sha256(seen).hexdigest())


# A run-time lookup of a function by address, as the tester's texts write it (run_fuzz's key: the overlay stand-ins).
LOOKUP_IN_TEXT = re.compile(r'\bLOOKUP_FUNC\b|\blift_lookup\b|\blift_pointer\w*|\bget_function\b')


def fuzz_cache_key(tester, parts):
    """A function's cache key: the shared part (fuzz_tester_hash) and every text its test is made from (run_fuzz's `parts`)."""
    h = hashlib.sha256(tester.encode())
    for p in parts:
        h.update(b'\0part\0' + p.encode())
    return h.hexdigest()


def fuzz_line_meets_bar(line):
    """Whether a report line may be kept in the cache and used again: DIFFERENT (but not a crash, which may be the tester's), or
    proved -- no difference, every instruction reached, at least FULL_TESTS compared. Anything else, or a line not in the
    report's form, doesn't."""
    fields = line.split()
    # (8 fields since 0.71.0: the 8th is part 1's own compared count. Requiring exactly 7 meant nothing was ever kept, 2026-11-09.)
    if len(fields) not in (7, 8):
        return False
    name, calls, checked, different, reached, total, first = fields[:7]
    try:
        if int(different):
            return not first.startswith('crashed')
        # (Plain random tests only, 2026-11-09, the user's decision: steered and boundary tests still run, but don't count. A line
        #  with no 8th column has no such count, so it can't be kept as proved.)
        random_tests = int(fields[7]) if len(fields) == 8 else 0
        return bin(int(reached, 16)).count('1') >= int(total) and random_tests >= FULL_TESTS
    except ValueError:
        return False


# Settled lines (2026-11-12, the speed agent). A line short of the bar was run again every night, though its test hadn't changed:
# the tests are the same sequence on every run (fuzz_main.cpp starts from a fixed state; only FUZZ_SEED, the reach pass's, changes
# it) and each part stops at a CPU-time limit, so a fresh run with no more time repeats the same tests and stops at about the same
# place. 0.79.0's 20-minute pass spent 2 h 20 on 24 functions, 20 of which came out short again (8 under a quarter of the count). So
# such a line is kept too, with the time it was made with, and used again by the same rules as a proof's (the same key, at least
# the time asked for now) -- but only as what it is: it never counts as proved (score() reads it as it reads a fresh one: too few
# tests), and a function whose test changes runs again. Kept: a line with no difference and every instruction reached, whose plain
# random count is under SETTLED_SHORT_BELOW, a quarter of the count. The same CPU time gets through more or fewer tests with the
# core it runs on and what shares it (this PC has fast and slow cores; measured 2026-11-12: func_80010170 got 333 and 159 in two
# 40-second runs, at other times of the day), so a line that far short would need four times the speed to make it; a nearer
# miss still runs again every time. Not a line with an instruction unreached: a run that gets through more tests in its time, on a
# faster core, can reach more, and fresh runs did (func_80102120: 111 of 116 in 0.79.0, then 116 and 109 in two runs here).
# Never a crash, a difference (those are kept already, as DIFFERENT) or a run that timed out.
SETTLED_SHORT_BELOW = FULL_TESTS // 4


def fuzz_line_settled(line):
    """Whether a line short of the bar may be kept as settled (see above): 'short', or None."""
    fields = line.split()
    if len(fields) != 8:
        return None
    try:
        checked, different, reached, total, random_tests = int(fields[2]), int(fields[3]), int(fields[4], 16), int(fields[5]), int(fields[7])
    except ValueError:
        return None
    if different or fields[6] != '-' or checked < MIN_CHECKED or bin(reached).count('1') < total:
        return None
    return 'short' if random_tests < SETTLED_SHORT_BELOW else None


def private_table_lines(cands, run, mod_places):
    """The mod's private data in PC memory, for the tester (2026-11-12, scratch/agents/modglue; its exact form CONFIRMED by the
    user 2026-11-12 -- see "The mod's private data in PC memory" in fuzz/fuzz_main.cpp). A candidate with 'private' (the names
    of the mod's private symbols its converted side keeps as host objects) gets a table: each symbol's N64 place (mod.map, moved
    as the tester moves the mod), size, layout and host object. The host objects are defined here and nowhere else, so the
    converted side has no private data the tester doesn't know of. The layout is private_data.py's 'widths' (each field's
    width, from how the mod's code reaches it); a symbol it can't tell isn't private there, and is refused here. Nothing at
    all for a build with no such candidate."""
    users = [n for n in run if cands[n].get('private')]
    if not users:
        return []
    rows = {r['name']: r for r in json.load(open(os.path.join(HERE, 'private_data.json')))}
    shift = (mod_places[0] - modrep.sections()[0][0]) if mod_places and modrep.sections() else 0
    out = ["// The mod's private data in PC memory (lift.py private_table_lines; the rule the user confirmed 2026-11-12)"]
    defined = set()     # (each host object once, however many of the functions tested use it)
    for n in users:
        entries = []
        for s in cands[n]['private']:
            r = rows.get(s)
            if r is None or not r['private']:
                raise SystemExit('%s: %s is not private data of the mod (private_data.json)' % (n, s))
            if not r.get('widths') or sum(r['widths']) != r['size']:
                raise SystemExit('%s: %s has no layout (private_data.json)' % (n, s))
            c = re.sub(r'\W', '_', s)
            if c not in defined:
                defined.add(c)
                out += ['extern "C" { alignas(8) u8 lift_mod_private_%s[%d]; }   // %s, at %s as linked' % (c, r['size'], s, r['address'])]
            out += ['static const u8 private_widths_%s_%s[] = {%s};' % (n, c, ', '.join(str(w) for w in r['widths']))]
            # (2026-11-13, modnative5: the user's rule (2) -- a byte the mod's compiler shrank a variable to holds 0 or 1 only:
            #  the tester's zero_or_one. Every other entry is written as before.)
            shrunk = s in cands[n].get('native', {}).get('shrunk', []) and r['size'] == 1
            # (2026-11-14, modnative7, rule NM_RULE_WRITES -- approved by the user 2026-11-14, OFF until an independent review: a private scalar's values are
            #  only those the mod's own writes can give it, prove_gen.py's 'values' from private_values.py: a list, or a range
            #  lowest..highest. Without the switch every entry is written as before.)
            values = cands[n].get('native', {}).get('values', {}).get(s) if os.environ.get('NM_RULE_WRITES') == '1' else None
            tail = ', 1u' if shrunk else ''
            if values and len(r['widths']) == 1:
                listed = values.get('list') or values['range']
                out += ['static const int64_t private_values_%s_%s[] = {%s};' % (n, c, ', '.join('%dll' % x for x in listed))]
                tail = ', %du, private_values_%s_%s, %du, %du' % (1 if shrunk else 0, n, c, len(listed), 1 if 'list' in values else 2)
            entries.append('{0x%08Xu, %du, private_widths_%s_%s, %du, lift_mod_private_%s%s}' % (
                int(r['address'], 16) + shift, r['size'], n, c, len(r['widths']), c, tail))
        out += ['static const FuzzPrivate private_%s[] = {%s};' % (n, ', '.join(entries))]
    out += ['const FuzzPrivate* fuzz_private_table(const char* function, int* count) {']
    for n in users:
        out += ['    if (std::strcmp(function, "%s") == 0) { *count = %d; return private_%s; }' % (n, len(cands[n]['private']), n)]
    out += ['    *count = 0;', '    return nullptr;', '}', '']
    return out


NARROW_CODES = {'zb': 'b', 'sb': 'B', 'zh': 'h', 'sh': 'H', 'z1': '1'}


def native_where(field):
    """A native candidate's parameter place as the tester numbers it (2026-11-13, modnative5): 'r4' -> 4 (a GPR), 'f2' -> 32 + 2
    (a float register), 'stack16' -> 0x100 + 16 (a word of the caller's frame, from sp)."""
    if field.startswith('stack'):
        return 0x100 + int(field[5:])
    return int(field[1:]) + (32 if field.startswith('f') else 0)


def native_rules_lines(cands, run):
    """The user's rules of 2026-11-13 for the mod's own functions made native (scratch/agents/modnative5): for each native
    candidate tested, the tester's FuzzNativeRules (fuzz/fuzz.h) -- (1) its narrow parameters, each extended as its C type
    says; (3) its own parameters' registers and kinds when the mod's compiler gave it its fast convention, and the loop
    budget; and with them (4), the set-aside for an index past an array's end (fuzz_main.cpp, "Outside an object"). Only native
    candidates get one: every other function's test is as before. Nothing at all for a build with none."""
    users = [n for n in run if cands[n].get('native')]
    if not users:
        return []
    out = ["// The mod's own functions made native: the user's rules of 2026-11-13 (lift.py native_rules_lines)"]
    for n in users:
        nat = cands[n]['native']
        narrow = nat.get('narrow', [])
        fast = nat.get('fast_params', [])
        out += ['static const FuzzNarrow nm_narrow_%s[] = {%s{-1, 0}};' % (n, ''.join("{0x%X, '%s'}, " % (native_where(w), NARROW_CODES[k]) for w, k in narrow)),
                'static const FuzzNarrow nm_fast_%s[] = {%s{-1, 0}};' % (n, ''.join("{0x%X, '%s'}, " % (native_where(w), k) for w, k in fast)),
                'static const FuzzNativeRules nm_rules_%s = {nm_narrow_%s, %d, nm_fast_%s, %d, %d};' % (
                    n, n, len(narrow), n, len(fast), 1 if nat.get('loop_budget') else 0)]
    out += ['const FuzzNativeRules* fuzz_native_rules(const char* function) {']
    out += ['    if (std::strcmp(function, "%s") == 0) return &nm_rules_%s;' % (n, n) for n in users]
    out += ['    return nullptr;', '}', '']
    return out


def native_fast_callees(cands, run):
    """(2026-11-13, modnative5: the user's rule (3)) The mod's own functions with the fast convention that native candidates
    call with a float or more than four parameters: stand-in name -> the registers they are given, as the candidates' stubs
    fill them (prove_gen.py fast_stub). The same for every caller: it is the callee's convention."""
    out = {}
    for n in run:
        for c, regs in cands[n].get('native', {}).get('fast_callees', {}).items():
            out[c] = regs
    return out


def loop_ticks_original(text):
    """(2026-11-13, modnative5: the user's rule (3), the register-only-loop budget) A native candidate's original with each
    backward jump -- a goto to a label above it: every loop's -- counting one pass (lift_fuzz_loop_tick, fuzz_main.cpp), as the
    native code's loop conditions count theirs (nm_native.h NM_TICK). Nothing else in it changes."""
    seen = set()
    out = []
    for line in text.split('\n'):
        m = re.match(r'^(L_[0-9A-F]{8}):', line)
        if m:
            seen.add(m.group(1))
        line = re.sub(r'\bgoto (L_[0-9A-F]{8});', lambda g: '{ lift_fuzz_loop_tick(); goto %s; }' % g.group(1) if g.group(1) in seen else g.group(0), line)
        out.append(line)
    return '\n'.join(out)


# ---- The step limit (2026-11-14, scratch/agents/rules084: the user's approval of 2026-11-14 for 0.84; TESTER CHANGE, nothing
#      counts from it until an independent review approves it -- REVIEW_STEPLIMIT.txt) ----
# For the tester only (run_fuzz), never the game: each loop pass of both versions calls lift_fuzz_step (fuzz/fuzz.h), and a run
# past the step limit has run long, as one past the access limit has (fuzz_main.cpp "The step limit"). Every cycle a run can
# go round passes one of the marks: in the original (the translator's C, in address order) every loop has a backward goto;
# in the converted C++ (m2c's) every loop's body is entered once a pass, and a loop made of gotos has a backward one. A
# function with no loop comes out exactly as before. The mod's native candidates keep their own loop budget (rule (3)).
# (merge084b, 2026-11-15: APPROVED in its strict form by reviewr084 -- a mixed test is always a difference; the 0.84 night run
#  sets LIFT_STEP_LIMIT=1.)
# OFF unless LIFT_STEP_LIMIT=1 (until the independent review approves it): without it every tester text is as before. With it,
# the marks go in and FUZZ_STEP_LIMIT=1 is set for the testers (a FUZZ_ setting, so it is in every cache key: fuzz_tester_hash).
STEP_TICKS = os.environ.get('LIFT_STEP_LIMIT') == '1'
if STEP_TICKS:
    os.environ['FUZZ_STEP_LIMIT'] = '1'


def step_ticks_original(text):
    """A translated original (or several, a function and its pieces) with a step counted at each backward jump: a goto to an
    L_ label above it in the same function. The translator's other gotos (after_N, skip_N) only ever go forward. And at the
    start of each piece (original_<entry>__<piece>): the translator splits some functions where another symbol starts, and a
    loop between two pieces is then a jump from one piece's C to the other's (a tail call, func_800E284C's angle wrap goes
    round func_800E285C and func_800E2864 so), which no goto shows."""
    seen = set()
    out = []
    for line in text.split('\n'):
        if line.startswith('static void original_') or line.startswith('RECOMP_FUNC '):
            seen = set()        # (labels are each function's own)
            if re.match(r'^static void original_\w+__\w+\(.*\) \{$', line):
                line += ' lift_fuzz_step();'
        m = re.match(r'^(L_[0-9A-F]{8}):', line)
        if m:
            seen.add(m.group(1))
        line = re.sub(r'\bgoto (L_[0-9A-F]{8});',
                      lambda g: '{ lift_fuzz_step(); goto %s; }' % g.group(1) if g.group(1) in seen else g.group(0), line)
        out.append(line)
    return '\n'.join(out)


STEP_LOOP_HEAD = re.compile(r'^(?:(?:for|while)\s*\(.*\)\s*\{|do\s*\{)$')
STEP_LABEL = re.compile(r'^([A-Za-z_]\w*):\s*;?$')


def step_ticks_converted(cpp):
    """A converted function's C++ with a step counted where each loop's body starts (`for (...) {`, `while (...) {`, `do {`:
    m2c writes every loop head alone on its line) and at each goto to a label above it in the same function. None if a loop
    keyword is left anywhere it cannot mark: tester_cpp then leaves that function's text as it is (its loops uncounted, as
    before -- never a test set aside that should not be)."""
    seen = set()
    out = []
    for line in cpp.split('\n'):
        s = line.strip()
        if line and not line[0].isspace() and s.endswith('{') and '(' in s:
            seen = set()        # (a function's first line: labels are each function's own)
        m = STEP_LABEL.match(s)
        if m and m.group(1) not in ('default', 'public', 'private', 'protected'):
            seen.add(m.group(1))
        if STEP_LOOP_HEAD.match(s):
            line += ' lift_fuzz_step();'
        elif re.search(r'\b(?:for|while)\s*\(|\bdo\s*\{', s) and not re.match(r'^\}\s*while\s*\(.*\);$', s) and not s.startswith('//'):
            return None
        line = re.sub(r'\bgoto (\w+);', lambda g: '{ lift_fuzz_step(); goto %s; }' % g.group(1) if g.group(1) in seen else g.group(0), line)
        out.append(line)
    return '\n'.join(out)


def tester_cpp(v):
    """The converted function's C++ as the tester compiles it: with the step limit's marks (STEP_TICKS), else as it is. A native
    candidate's own C++ (the mod's code made native) is left as it is: its loops count against rule (3)'s budget (NM_TICK)."""
    if not STEP_TICKS or v.get('native'):
        return v['cpp']
    marked = step_ticks_converted(v['cpp'])
    return v['cpp'] if marked is None else marked


# ---- The write cut (2026-11-15, scratch/agents/rules084: a PROPOSAL for the user's "find another way" -- nothing counts from
#      it until an independent review: REVIEW_WRITECUT.txt; TESTER CHANGE) ----
# The functions whose tests that both versions run past the access limit are compared up to a cut instead of set aside
# (fuzz_main.cpp "The write cut"). Each with why. OFF unless LIFT_WRITE_CUT=1: then the tester gets fuzz_write_cut for them,
# and only their keys change. (merge084b, 2026-11-15: APPROVED by reviewr084; the 0.84 night run sets LIFT_WRITE_CUT=1.)
#   func_80008D6C: with bit 1 of its stack argument 0x3C(sp) set it draws mirrored -- a2 and a1 count DOWN -- and its row
#   loop's `slti $a1, 0x14` then stays true for 2**31 passes of two reads and a write each: every such test runs past the
#   access limit in both versions, so the three instructions of that way (0x80008E48-0x80008E50), which run on every pass
#   from the first, were never in a compared test. Its writes (one byte a pass) are what the two must agree on.
WRITE_CUT_NAMES = frozenset(['func_80008D6C']) if os.environ.get('LIFT_WRITE_CUT') == '1' else frozenset()


def write_cut_lines(run):
    """The tester's list of write-cut functions among those run (fuzz_main.cpp fuzz_write_cut); nothing when none is."""
    names = sorted(n for n in run if n in WRITE_CUT_NAMES)
    if not names:
        return []
    return (['bool fuzz_write_cut(const char* function) {  // (the write cut: lift.py WRITE_CUT_NAMES)'] +
            ['    if (std::strcmp(function, "%s") == 0) return true;' % n for n in names] + ['    return false;', '}', ''])


LOOP_TICK_NAMES = set()     # (run_fuzz: the native candidates whose originals count their loops' passes)
THRU_GROUPS = {}            # (run_fuzz: a native candidate -> the internal functions run through it, the user's rule (5))


def native_mod_image(build, shift):
    """(path, bytes, hash) of the mod's read-only image as the tester puts it in memory for a native mod function (2026-11-12,
    scratch/agents/modnative1): mod_binary.bin from the mod's start (0x81000000, as linked) up to its first code -- .rodata,
    the compiler's constant pools and strings, the jump tables -- each word in the PC's order (game memory's), and each word
    the linker relocated (mod.elf's R_MIPS_32 there) moved as the tester moves the mod, when it points into the mod."""
    build_dir = modrep.MOD_BUILD
    binary = open(os.path.join(build_dir, 'mod_binary.bin'), 'rb').read()
    start, size = modrep.sections()[0]
    code = None
    for line in open(os.path.join(build_dir, 'mod.map'), encoding='utf8', errors='replace'):
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +[0-9a-f]+ +\d+ (\.text\S*)\s*$', line)
        if m:
            code = int(m.group(1), 16) if code is None else min(code, int(m.group(1), 16))
    end = (code - start) & ~3
    words = [int.from_bytes(binary[k:k + 4], 'big') for k in range(0, end, 4)]
    readelf = os.path.join(ROOT, 'toolchain', 'llvm-mingw', 'bin', 'llvm-readelf.exe')
    p = subprocess.run([readelf, '-r', os.path.join(build_dir, 'mod.elf')], capture_output=True, text=True)
    moved = 0
    for m in re.finditer(r'^([0-9a-f]{8})\s+[0-9a-f]+\s+R_MIPS_32\b', p.stdout, re.M):
        at = int(m.group(1), 16) - start
        if 0 <= at < end and at % 4 == 0:
            w = words[at // 4]
            if start <= w < start + size:
                words[at // 4] = (w + shift) & 0xFFFFFFFF
                moved += 1
    data = b''.join(w.to_bytes(4, 'little') for w in words)
    path = os.path.join(build, 'mod_image.bin')
    if not os.path.exists(path) or open(path, 'rb').read() != data:
        open(path, 'wb').write(data)
    return os.path.abspath(path), len(data), 'sha1 %s, %d words moved' % (hashlib.sha1(data).hexdigest()[:16], moved)


def original_mod_places(text, mod_places):
    """The places in the mod's sections a translated original makes (2026-11-13, scratch/agents/modnative2): each
    RELOC_LO16(section, offset) -- the low half the loader completes an address with, so the offset is the whole address in its
    section -- where that section is in the tester (mod_places). A native mod function's test inputs: see run_fuzz."""
    return {mod_places[int(k)] + int(o, 0) for k, o in re.findall(r'\bRELOC_LO16\((\d+), (0X[0-9A-F]+|\d+)\)', text)
            if int(k) < len(mod_places)}


_ARGS_PROOF = {}


def args_proof():
    """(FUZZ_ARGS_STRICT) The strict read-set checker's results for the argument compare (readcheck.py): each function's raw
    reads (no independence file), the stack-parameter words it may read (None: not known), its outgoing words, and the
    runtime functions' reads. Worked out once."""
    if not _ARGS_PROOF:
        import readcheck
        # (A call the checker can't resolve reads all eight argument registers here, f13/f15 too -- only here, not in
        #  safeguard 1's findings: reviewac G3.)
        ck = readcheck.Checker(HERE, unknown_callee=readcheck.ARG_REGS)
        raw, _ = ck.run(explained=False)
        _ARGS_PROOF.update({'raw': raw, 'stack': ck.stack_reads(), 'outgoing': {n: ck.outgoing_words(n) for n in ck.F},
                            'system': ck.system, 'regs': readcheck.ARG_REGS})
    return _ARGS_PROOF


def args_masks(callees):
    """(FUZZ_ARGS_STRICT) (skip, stack) for a stand-in standing for any of `callees`: skip, bit i, the argument register
    readcheck.ARG_REGS[i] none of them is proved to read; stack, bit j, the stack word $sp + 0x10 + 4j one of them may read --
    0xFFFFFFFF where that isn't known (a callee with no code the checker reads, or one whose stack use it can't bound)."""
    pr = args_proof()
    reads, stack = set(), 0
    if not callees:
        return 0, 0xFFFFFFFF
    for c in callees:
        if c in pr['raw']:
            reads |= pr['raw'][c]
            k = pr['stack'][c]
            stack = 0xFFFFFFFF if k is None or stack == 0xFFFFFFFF else stack | sum(1 << w for w in k if w < 16)
        elif c in pr['system'] and c in SYSTEM:
            reads |= set(pr['system'][c])
            stack = stack if stack == 0xFFFFFFFF else stack | ((1 << min(16, SYSTEM[c]['stack'])) - 1)
        else:
            return 0, 0xFFFFFFFF          # (nothing proved about it)
    skip = sum(1 << i for i, r in enumerate(pr['regs']) if r not in reads)
    return skip, stack


ARGS_STAND_IN_CALL = re.compile(r'\bfuzz_(?:stand_in|system_stand_in|stand_in_abi|mod_stand_in|mod_stand_in_fast)\(')


def args_wrap(line, callees):
    """(FUZZ_ARGS_STRICT) A stand-in line with fuzz_args_next(skip, stack) just before its stand-in call."""
    m = ARGS_STAND_IN_CALL.search(line)
    skip, stack = args_masks(callees)
    return line[:m.start()] + 'fuzz_args_next(0x%Xu, 0x%Xu); ' % (skip, stack) + line[m.start():]


def args_outgoing_lines(names):
    """(FUZZ_ARGS_STRICT) fuzz_args_outgoing: each tested function's outgoing stack words (readcheck.py outgoing_words; 16 where
    the checker can't say)."""
    out = args_proof()['outgoing']
    lines = ['u32 fuzz_args_outgoing(const char* function) {']
    for n in names:
        k = out.get(n)
        lines += ['    if (std::strcmp(function, "%s") == 0) return %du;' % (n, 16 if k is None else k)]
    return lines + ['    return 16u;', '}', '']


def run_fuzz(tests):
    cands = json.load(open(CANDIDATES))
    names = sorted(n for n, v in cands.items() if 'cpp' in v)
    only = subset_names()
    if only is not None:
        names = [n for n in names if n in only]
    # (LIFT_FUZZ_TAG, 2026-11-10: a quick run -- `lift.py prove` -- of a few functions beside a full run: its own build folder and
    #  its own cache file, so the two never write the same files. See quick_cache below.)
    tag = os.environ.get('LIFT_FUZZ_TAG', '')
    build = os.path.join(FUZZ, 'build_' + tag if tag else 'build')
    os.makedirs(build, exist_ok=True)
    functions = to_asm.read_functions()
    # (The port's calls in its additions count as instructions that must run: originals_with_notes.)
    port_calls = {n: sorted(cands[n]['port']['externs']) for n in names if cands[n].get('port', {}).get('externs')}
    heading = '// The translated originals, for the random-input tester. WRITTEN BY tools/lift/lift.py.'
    chunks = {}
    use_hooked_originals(cands)     # (a function converted with its hook calls: its original with them too. hooks.py)
    LOOP_TICK_NAMES.clear()
    THRU_GROUPS.clear()
    THRU_GROUPS.update({n: cands[n]['native']['thru'] for n in names if cands[n].get('native', {}).get('thru')})
    LOOP_TICK_NAMES.update(n for n in names if cands[n].get('native', {}).get('loop_budget'))
    fast_callees = native_fast_callees(cands, names)     # (the user's rule (3): see stand_in below. 2026-11-13, modnative5)
    # (2026-11-13 modnative5: the narrow results of the callees native candidates call -- the stand-in's v0 extended as the
    #  callee's IR says, as rule (1) extends narrow parameters. Approved by the user 2026-11-14 and on from then (modnative6):
    #  LIFT_NM_NARROW_RESULTS=0 turns it off, every stand-in line as before.)
    narrow_results = {c: k for n in names for c, k in cands[n].get('native', {}).get('narrow_results', {}).items()}         if os.environ.get('LIFT_NM_NARROW_RESULTS', '1') == '1' else {}
    # (2026-11-14, modnative7: the narrow-argument rule's lists -- prove_gen.py narrow_args, strict: callees proved to ignore the
    #  high bits -- approved by the user 2026-11-14; NM_NO_LOWBITS=1 turns it off here too; see stand_in)
    narrow_args = {c: regs for n in names for c, regs in cands[n].get('native', {}).get('narrow_args', {}).items()} \
        if os.environ.get('NM_NO_LOWBITS') != '1' else {}
    # (2026-11-14, modnative7: narrow results made STRICT, approved by the user 2026-11-14 -- a callee's result is extended only
    #  when every candidate of the run that calls it is proved to ignore its high bits: if one isn't (prove_gen.py's
    #  narrow_results_unproved), that callee's stand-in gives all 32 bits, as for any callee)
    unproved_results = {c for n in names for c in cands[n].get('native', {}).get('narrow_results_unproved', [])}
    # (2026-11-14, NM_DECLARED_PARAMS -- APPROVED by the user for 0.84, and by the independent review (scratch/agents/reviewnm084,
    #  2026-11-15; REVIEW_declared_params.txt): it counts. The 0.84 night run sets NM_DECLARED_PARAMS=1.) A callee whose parameters are known for certain -- one of the mod's own functions by its
    #  definition, a host import by the port's C++ (prove_gen.py declared_masks) -- has its stand-in note only those registers.
    #  Every candidate of the run that calls it adds its mask (the union: more registers, never fewer). Off, every line as before.
    declared = {}
    if os.environ.get('NM_DECLARED_PARAMS') == '1':
        for n in names:
            for c, m in cands[n].get('native', {}).get('declared_masks', {}).items():
                declared[c] = declared.get(c, 0) | m
    narrow_results = {c: k for c, k in narrow_results.items() if c not in unproved_results}
    _, sizes = originals_with_notes(names, heading, functions, port_calls, chunks)
    # Each function's original as the tester runs it (fuzz_original_text), on its own: originals.inc is these one after another.
    originals = {n: fuzz_original_text('\n'.join(chunks[n])) for n in names}
    # (The step limit, 2026-11-14, rules084: both versions' loop passes counted, for the tester only. See STEP_TICKS.)
    if STEP_TICKS:
        originals = {n: originals[n] if cands[n].get('native') else step_ticks_original(originals[n]) for n in names}
    # (With the system functions handled, for their stand-ins: with_system, stand_in_line, 2026-11-07.)
    analysis = with_system(json.load(open(os.path.join(HERE, 'analysis.json'))), functions)
    called = sorted({c for n in names for c in cands[n].get('callees', [])} |
                    {c for n in names for c in functions[n]['calls'].values()})
    # The number each stand-in notes in the list of calls: the function's place among all the game's translated functions (or, for
    # a name that isn't one, a number from its name, apart from those), so a function's test is the same whichever others are
    # tested beside it -- until 2026-11-08 it was its place among the callees of the functions tested, which moved whenever one
    # came or went. (Only a number both versions' calls must agree on; it changes nothing else.)
    # (The game's functions only: nothing calls a modrep_ copy by name, and with them in the list every later name's number
    # would move, and with it every cache key. modrep.py)
    every = sorted(n for n in functions if not modrep.is_modrep(n))
    place = {n: k for k, n in enumerate(every)}
    stand_in_number = lambda c: place[c] if c in place else 0x20000000 | (zlib.crc32(c.encode()) & 0x0FFFFFFF)
    any_abi = any(cands[n].get('abi') for n in names)
    writes = surely = None
    if any_abi:
        writes, surely = abi.may_write(functions, analysis), abi.must_write(functions)
    def stand_in(c, abi_form):
        line = stand_in_plain(c, abi_form)
        if c in narrow_results:
            ext = {'zb': '(u32)(u8)', 'sb': '(s32)(s8)', 'zh': '(u32)(u16)', 'sh': '(s32)(s16)', 'z1': '1u & (u32)'}[narrow_results[c]]
            line = line[:-1].rstrip() + ' ctx->r2 = (gpr)(int64_t)(s32)(%s(u32)ctx->r2); }' % ext
        if c in narrow_args:
            # (2026-11-14, modnative7, approved by the user 2026-11-14 in its strict form; NM_NO_LOWBITS=1 off: a callee proved from
            #  its own code to ignore the bits of a narrow argument above 8 / 16 (prove_gen.py narrow_args, narrow_proof.py) has
            #  that argument noted by those low bits only, the same on both sides. Every other stand-in line is as before.)
            cut = ' '.join('ctx->r%d = (gpr)(int64_t)(s32)(u32)(%s)ctx->r%d;' % (g, 'u16' if k in ('zh', 'sh') else 'u8', g)
                           for g, k in narrow_args[c])
            at = line.index('{') + 1
            line = line[:at] + ' ' + cut + line[at:]
        if ARGS_STRICT:
            line = args_wrap(line, [c])      # (the argument compare: what the checker proves about this callee)
        return line

    def stand_in_plain(c, abi_form):
        if c in SYSTEM:
            # (A system function's stand-in does what the runtime's version does to the registers -- its results, nothing
            #  else -- which is also abi.py's faithful way: stand_in_line, 2026-11-07.)
            return stand_in_line(c, stand_in_number(c), analysis, functions)
        use = set(analysis.get(c, {}).get('use', []))
        mask = sum(1 << i for i, r in enumerate(('a0', 'a1', 'a2', 'a3', 'f12', 'f14')) if r in use) | stand_in_double_bits(use)
        if c in declared:
            mask = declared[c]      # (NM_DECLARED_PARAMS: its declared parameters' registers only)
        # (callee_function: one of the mod's own functions too, a copy's callee, 2026-11-12)
        slots = stack_parameters(callee_function(c, functions)) if callee_function(c, functions) else 0
        if abi_form:
            # (Behaves exactly as fuzz_stand_in unless the function under test is one of abi.py's.)
            # (2026-11-11, trial 5: a port helper that hands back a saved register may change it -- its bits in the may-write
            #  mask, which fuzz_stand_in_abi acts on. Every other function's line is as it was.)
            handed = abi.mask(helpers_writing_saved_registers().get(c, ()))
            return 'extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in_abi(%d, 0x%X, %d, 0x%016Xull, 0x%016Xull, 0x%016Xull, ctx); }' % (
                c, stand_in_number(c), mask, slots, abi.extra_reads(c, analysis), writes(c) | handed, surely(c))
        if c in fast_callees:
            # (2026-11-13, modnative5: the user's rule (3) -- one of the mod's functions with its compiler's fast convention, which a
            #  native candidate calls with a float or more than four parameters: its stand-in notes the registers they come in,
            #  a0-a3 as any stand-in does and the others -- t0-t8, v1, f0-f18 -- after them. fuzz_mod_stand_in_fast.)
            regs = [native_where(x) for x in fast_callees[c]]
            low = sum(1 << (r - 4) for r in regs if 4 <= r <= 7)
            extra = sum(1 << r for r in regs if not 4 <= r <= 7)
            return 'extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { fuzz_mod_stand_in_fast(%d, 0x%X, 0x%016Xull, ctx); }' % (
                c, stand_in_number(c), low, extra)
        if modrep.is_mod_own(c):
            # (One of the mod's own functions, which a copy of the mod's calls -- a C patch's glue (modglue) or an edited copy's callee
            #  (modcopies): fuzz_stand_in exactly, plus the mod's private data at the call where the function under test has it in
            #  PC memory. fuzz_mod_stand_in, 2026-11-12.)
            return 'extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { fuzz_mod_stand_in(%d, 0x%X, %d, ctx); }' % (c, stand_in_number(c), mask, slots)
        return 'extern "C" void %s(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in(%d, 0x%X, %d, ctx); }' % (c, stand_in_number(c), mask, slots)
    # The overlay calls (see "Calls into overlays"): get_function (fuzz_main.cpp) asks fuzz_overlay_lookup for the address, on both
    # sides -- the original's LOOKUP_FUNC and the converted one's lift_lookup -- and gets this address's recording stand-in. It
    # notes the address in its number (0x40000000 and the address's low 24 bits, apart from the named callees' numbers and the
    # port's calls'), and the parameters any function there reads, as a named callee's does. (Every candidate's addresses, not
    # only those tested: the same lookup whichever are.)
    at_address = functions_at(functions)
    looked = sorted({int(a, 16) for n, v in cands.items() if 'cpp' in v for a in v.get('overlay', [])})
    overlay_lines = []
    # (2026-11-11, trial 5: an address one of abi.py's functions looks up gets its stand-in in abi.py's form, as a named callee
    #  does there: it notes the registers any function at the address reads beyond its parameters, and changes only what one of
    #  them may change -- always what every one of them surely writes. Without that a converted abi.py function could leave a
    #  register an overlay callee reads out of step and no test would see it. In a test of any other function the form behaves
    #  exactly as fuzz_stand_in (fuzz_main.cpp fuzz_stand_in_abi), and every other address's line is as it was.)
    abi_looked = {int(a, 16) for n, v in cands.items() if 'cpp' in v and v.get('abi') for a in v.get('overlay', [])}
    if abi_looked and writes is None:
        writes, surely = abi.may_write(functions, analysis), abi.must_write(functions)
    for a in looked:
        there = at_address.get(a, [])
        use = set().union(*[set(analysis.get(n, {}).get('use', [])) for n in there]) if there else set()
        mask = sum(1 << i for i, r in enumerate(('a0', 'a1', 'a2', 'a3', 'f12', 'f14')) if r in use)
        slots = max([stack_parameters(functions[n]) for n in there] or [0])
        if a in abi_looked and there:
            extra = may = 0
            must = ~0
            for n in there:
                extra |= abi.extra_reads(n, analysis)
                may |= writes(n)
                must &= surely(n)
            overlay_lines += ['static void fuzz_overlay_%08X(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in_abi(0x%08Xu, 0x%X, %d, 0x%016Xull, 0x%016Xull, 0x%016Xull, ctx); }' % (
                a, 0x40000000 | (a & 0xFFFFFF), mask, slots, extra, may, must & 0xFFFFFFFFFFFFFFFF)]
            if ARGS_STRICT:
                overlay_lines[-1] = args_wrap(overlay_lines[-1], there)
            continue
        overlay_lines += ['static void fuzz_overlay_%08X(uint8_t* rdram, recomp_context* ctx) { fuzz_stand_in(0x%08Xu, 0x%X, %d, ctx); }' % (
            a, 0x40000000 | (a & 0xFFFFFF), mask, slots)]
        if ARGS_STRICT:
            overlay_lines[-1] = args_wrap(overlay_lines[-1], there)
    overlay_lines += ['recomp_func_t* fuzz_overlay_lookup(int32_t vram) {', '    switch ((uint32_t)vram) {']
    overlay_lines += ['    case 0x%08Xu: return fuzz_overlay_%08X;' % (a, a) for a in looked]
    overlay_lines += ['    }', '    return fuzz_overlay_unknown_address(vram);', '}', '']
    # The port functions the additions call: recording stand-ins (fuzz_port_call), numbered among every candidate's (the same
    # numbers whichever are tested), and the wrappers, after the memory macros so what an addition reads and writes is noted as
    # the originals' is.
    port_externs = {}
    for n, v in cands.items():
        if 'cpp' in v and v.get('port'):
            port_externs.update(v['port']['externs'])
    port_stubs = port_additions.stub_definitions(port_externs)
    # The mod's copies (modrep.py): their originals find the mod's sections through section_addresses (the translator's
    # RELOC_HI16 / RELOC_LO16, recomp.h), the converted ones through lift_mod_section_address (lift_mod_address). Both are the
    # same places here, and on purpose not where the mod was linked (modrep.TESTER_SHIFT further on): a converted copy that kept
    # a place in the mod as linked reads and writes somewhere else than its original, a difference.
    mod_places = []
    mod_text = []
    if any(modrep.is_modrep(n) for n in names):
        mod_places = [a + modrep.TESTER_SHIFT for a, _ in modrep.sections()] or [modrep.TESTER_SHIFT]
        mod_text = modrep.CPP_HELPER + ['extern "C" { int32_t lift_mod_section_address[%d] = {%s}; }' % (
            len(mod_places), ', '.join('(int32_t)0x%08Xu' % a for a in mod_places)), 'int32_t* section_addresses = lift_mod_section_address;', '']
        # (The mod's whole section gets random contents in a test of a copy: fuzz_main.cpp, fuzz_mod_region.)
        mod_text += ['void fuzz_mod_region(u32* start, u32* bytes) { *start = 0x%08Xu; *bytes = 0x%Xu; }' % (mod_places[0], modrep.sections()[0][1] if modrep.sections() else 0), '']
    # (2026-11-12, scratch/agents/modnative1) The mod's own functions made native (candidates with 'native': prove_gen.py): their
    #  tests have the mod's read-only image in memory, its built bytes, not random ones -- the tester addition the user approved
    #  (fuzz_main.cpp fuzz_mod_image). Only theirs: the line names them, and only their keys hold the image (by its hash).
    image_lines = []
    image_range = (0, 0)
    natives = sorted(n for n in names if cands[n].get('native'))
    if natives and mod_places:
        image_path, image_bytes, image_hash = native_mod_image(build, mod_places[0] - modrep.sections()[0][0])
        image_range = (mod_places[0], mod_places[0] + image_bytes)
        image_lines = ["// The mod's read-only image (lift.py native_mod_image: mod_binary.bin, relocated; %s)" % image_hash,
                       'const char* fuzz_mod_image(const char* function, u32* start, u32* bytes) {',
                       '    static const char* const natives[] = {%s};' % ', '.join('"%s"' % n for n in natives),
                       '    for (const char* n : natives) if (std::strcmp(function, n) == 0) { *start = 0x%08Xu; *bytes = 0x%Xu; return "%s"; }' % (
                           mod_places[0], image_bytes, image_path.replace(chr(92), '/')),
                       '    *start = 0; *bytes = 0; return nullptr;', '}', '']
    # Each function's own part of the generated file: its entry in the table and what goes with it.
    own = {}
    for name in names:
        v = cands[name]
        port = v.get('port', {})
        # (The addresses: from the original's own instructions since 2026-11-12, original_addresses -- and the port's additions'
        #  text, which is the port's, not the converted function's. FUZZ_ADDR_FROM_TEXT=1: every 0x80xxxxxx in the converted text, as before.)
        if os.environ.get('FUZZ_ADDR_FROM_TEXT') == '1':
            addresses = sorted({int(a, 16) for a in re.findall(r'0x80[0-9A-Fa-f]{6}\b', v['cpp'] + port.get('entry', '') + ''.join(port.get('wrappers', [])))})
        else:
            addresses = sorted(original_addresses(functions[name]) | {int(a, 16) for a in re.findall(r'0x80[0-9A-Fa-f]{6}\b', port.get('entry', '') + ''.join(port.get('wrappers', [])))})
        # (A copy of the mod's: the places in the mod it names, where they are here, get random contents too.)
        if v.get('native'):
            # (2026-11-13, scratch/agents/modnative2) One of the mod's own functions made native: its places in the mod come from
            #  its ORIGINAL -- each address the translation's RELOC_LO16(section, offset) completes, as the loader relocates it --
            #  never from the native C++, whose text (its views' lift_mod_address) must not choose its own test's inputs (the rule
            #  of original_addresses above). The mod's read-only image is left out: in these tests it is the built mod's constant
            #  bytes (native_mod_image), which nothing makes fresh.
            addresses = sorted(set(addresses) | {a for a in original_mod_places(originals[name], mod_places)
                                                 if not image_range[0] <= a < image_range[1]})
        else:
            addresses = sorted(set(addresses) | {mod_places[int(k)] + int(o, 16) for k, o in re.findall(r'\blift_mod_address\((\d+), 0x([0-9A-F]+)\)', v['cpp'])})
        # (A per-function hint, FUZZ_HINTS: fixed places the function reaches only by indexing from one it names -- the later
        #  records of a table -- so the tester gives them fresh random contents in every start too, as it does the named one.)
        addresses = sorted(set(addresses) | set(FUZZ_HINTS.get(name, {}).get('addresses', ())))
        result = 0 if v['ret'] == 'void' else 2 if v['ret'] == 'f32' else 3 if v['ret'] == 'f64' else 1     # (3: 2026-11-12, "refused2")
        constants = fuzz_constants(functions[name])
        compares = fuzz_compare_constants(functions[name])     # (for the tester's part 3: see fuzz_main.cpp compare_word)
        # (Per-function steering hints, FUZZ_HINTS above: put first, so the cut to `most` never drops them.)
        hint = FUZZ_HINTS.get(name, {})
        constants = [k for k in hint.get('constants', ()) if k not in constants] + constants
        constants = list(hint.get('repeat', ())) + constants        # (2026-11-14, collect084: weights -- every copy kept, own or not)
        compares = [k for k in hint.get('compares', ()) if k not in compares] + compares
        # (The port's additions compare with numbers of their own -- func_80001E08's with the camera matrix's address,
        # 0x80062BA8 -- which go in the dictionary too, so the paths they guard are reached.)
        for k in re.findall(r'\b0[xX]([0-9A-Fa-f]{1,8})[uU]?\b', port.get('entry', '') + ''.join(port.get('wrappers', []))):
            if int(k, 16) not in constants and int(k, 16) != 0:
                constants.append(int(k, 16))
        # The compiler's checks after a division that division_checks.py proves can never run (the divisor can't be 0, or -1,
        # there): counted as reached from the start, since no test can run them. Every other instruction still has to run.
        # And the instructions unreach.py proves can never run from the function's start or any other way into it
        # (unreach_roots.json), strict mode: after a call nothing is known, saved registers included (2026-11-09, the user's approval;
        # independently reviewed). Like the division checks, they only count as reached: every other instruction still has to
        # run in a compared test, and a difference anywhere is still a difference.
        never = sorted(SLOTS[name][a] for a in division_checks.dead_checks(functions[name]) | unreach_dead(name, functions[name])
                       if a in SLOTS[name])
        lines = ['static void converted_%s(uint8_t* rdram, recomp_context* ctx) {' % name, '    lift_rdram = rdram;',
                 port.get('entry', '') + '    ' + hook_wrapped(name, v, adapter_body(name, v)), '}',
                 'static u8 reached_%s[%d];' % (name, sizes[name])]
        if never:
            lines += ['static const bool never_runs_%s = (%s, true);   // (instructions that never run: division_checks.py, unreach.py)' % (
                name, ', '.join('reached_%s[%d] = 1' % (name, k) for k in never))]
        lines += ['static const u32 addresses_%s[] = {%s};' % (name, ', '.join('0x%08Xu' % a for a in addresses + [0])),
                  'static const u32 constants_%s[] = {%s};' % (name, ', '.join('0x%08Xu' % k for k in constants + [0])),
                  'static const u32 compares_%s[] = {%s};' % (name, ', '.join('0x%08Xu' % k for k in compares + [0])), '']
        # (abi.py's: the registers compared at the end, and its stand-ins' way of working -- FuzzFunction's abi_out, abi. Given
        #  for every function now, 0 where they don't apply, since the compare constants come after them.)
        extra = ', %d, 0x%016Xull, %d' % (1 if port else 0, abi.mask(v['abi']['out']) if v.get('abi') else 0, 1 if v.get('abi') else 0)
        # (and the registers besides a0-a3 it reads, for part 4: fuzz_input_registers)
        # (and whether it has no return instruction -- a thread's body, which loops for ever: each of its tests then ends at its
        #  K-th call, fuzz_main.cpp cut_call; the rule the user approved 2026-11-12, scratch/agents/neverran)
        row = '    {"%s", original_%s, converted_%s, "%s", %d, reached_%s, %d, addresses_%s, constants_%s, %d%s, compares_%s, %d, 0x%08Xu, %d, %d},' % (
            name, name, name, param_kinds(v), result, name, sizes[name], name, name, len(constants), extra, name, len(compares),
            fuzz_input_registers(v), calls_system(functions[name]), never_returns(functions[name]))
        # (FuzzFunction: input_registers, system_calls, then never_returns, the last field)
        own[name] = (lines, row)
    # The result cache: each function's key, from everything its test is made from. Its callees are the names in its code (the
    # original's, the converted one's, the port's) that are game functions -- any of them, not only those called by the functions
    # tested beside it, so the key doesn't depend on which those are; each one's stand-in goes in, in the form this function's
    # test uses (abi.py's form, with what the callee reads and changes, for one of abi.py's functions).
    called_set = set(called) | set(functions)
    # (2026-11-14, fastnight: each function's build -- fuzz_variant -- and that build's shared part, the tester as preprocessed for it)
    variant = {n: fuzz_variant(n, cands[n]) for n in names}
    testers = {k: fuzz_tester_hash(tests, k, build) for k in sorted(set(variant.values()))}
    controls = {n for n in names if fuzz_is_control(n, cands[n])}
    keys = {}
    # (LIFT_GAME_SHARDS_READ=K: each function's key as a K-shard pass makes it too, to read those lines. Nothing when unset.)
    read_testers = {k: fuzz_tester_hash(tests, k, build, GAME_SHARDS_READ) for k in sorted(set(variant.values()))} if GAME_SHARDS_READ else {}
    read_keys = {}
    # The one-time carry-over (2026-11-11), for the keys' change just below: a function with no lookup had the whole overlay table
    # in its key until now, so its key changes once although its test doesn't. fuzz/cache/legacy_overlay_lines.txt holds the table's
    # text as the last run before the change wrote it (scratch/legacy_overlay.py makes it, from that run's candidates); a function
    # with no lookup also gets its key as that run would have made it, and a cached line under that old key is used again and
    # saved under the new one. An old key only matches when every other part of the test is the same as when the line was made,
    # and the table can't change such a test, so nothing is taken on trust; a wrong file only matches nothing (everything runs).
    # Delete the file after one run.
    legacy_path = os.path.join(FUZZ, 'cache', 'legacy_overlay_lines.txt')
    legacy_overlay = open(legacy_path, encoding='utf8', newline='').read() if os.path.exists(legacy_path) else None
    legacy_keys = {}
    for name in names:
        v = cands[name]
        port = v.get('port', {})
        texts = [originals[name], tester_cpp(v), port.get('entry', ''), '\n'.join(port.get('wrappers', [])),
                 '\n'.join(port.get('declarations', []))]
        callees = sorted(called_set & set(re.findall(r'\b\w+\b', '\n'.join(texts))))
        tester = testers[variant[name]]
        head = [name] + texts + ['\n'.join(own[name][0]), own[name][1]]
        included = fuzz_text_includes(texts, variant[name], build)     # (a header its own text includes: fastnight, 2026-11-14)
        if included:
            head += [included]
        if v.get('hooks'):
            head += ['\n'.join(HOOK_TESTER + hook_places(name, v))]     # (its hook places: hooks.py, 2026-11-12)
        head += [stand_in(c, bool(v.get('abi'))) for c in callees]
        if v.get('private'):
            # (The mod's private data in PC memory, the user's rule of 2026-11-12: its table is part of the test. Nothing for any other.)
            head += ['\n'.join(private_table_lines({name: v}, [name], mod_places))]
        if v.get('native'):
            head += ['\n'.join(native_rules_lines({name: v}, [name]))]     # (the user's rules of 2026-11-13: native candidates only)
        if name in WRITE_CUT_NAMES:
            head += ['\n'.join(write_cut_lines([name]))]      # (the write cut, 2026-11-15: in its key only)
        if ARGS_STRICT:
            head += ['\n'.join(args_outgoing_lines([name]))]     # (the argument compare: its outgoing words, in its key)
        tail = ['\n'.join(port_stubs)]
        tail += ['\n'.join(unaligned_helpers([v['cpp']])), '\n'.join(unaligned_originals(originals[name]))]
        if modrep.is_modrep(name):
            tail += ['\n'.join(mod_text)]         # (where the mod's sections are in the tester)
        if v.get('native'):
            # (the mod's read-only image, by its hash -- not the list of names, which changes with what else is tested -- and
            #  the native mod's header, which every native candidate's C++ includes)
            tail += [image_lines[0] if image_lines else '', open(os.path.join(FUZZ, 'nm_native.h'), encoding='utf8').read()]
        # (The overlay lookup's stand-ins, fuzz_overlay_lookup, only for a function whose test can reach that lookup: a run-time
        #  lookup in its own text, the original's LOOKUP_FUNC or the converted one's lift_lookup / lift_pointer, or get_function
        #  itself. Nothing else in a test calls it -- its callees are stand-ins, which never look up -- so for any other function
        #  the table can't change what its test does. Until 2026-11-11 every key held the whole table, and a new overlay address
        #  anywhere re-tested all ~3,000 functions; the user: "If it's all the same for verifying ... go the fastest route".
        #  With it, a function calling through a pointer has the pointer helper in its key: POINTER_HELPER goes into its test and
        #  was in no key, 2026-11-11.)
        if LOOKUP_IN_TEXT.search('\n'.join(texts)):
            middle = ['\n'.join(overlay_lines)] + (['\n'.join(POINTER_HELPER)] if 'lift_pointer<' in v['cpp'] else [])
        else:
            middle = []
            if legacy_overlay is not None:
                legacy_keys[name] = fuzz_cache_key(tester, head + [legacy_overlay] + tail)
        keys[name] = fuzz_cache_key(tester, head + middle + tail)
        if GAME_SHARDS_READ:
            read_keys[name] = fuzz_cache_key(read_testers[variant[name]], head + middle + tail)
    def read_cache(path):
        try:
            c = json.load(open(path)) if os.path.exists(path) else {}
        except ValueError:
            c = {}
        return c if isinstance(c, dict) else {}
    # (FUZZ_SEED, the reach pass's runs: another sequence of tests. Since 2026-11-10 they have a cache of their own, by the same key
    #  and the seed: a function whose test hasn't changed gets the same seed's line back instead of an hour of the same runs. Any line
    #  but a crash is kept: these are meant to be partial.)
    seeded = 'FUZZ_SEED' in os.environ
    seed_tag = (':seed=%s:ms=%s/%s/%s' % (os.environ.get('FUZZ_SEED'), os.environ.get('FUZZ_MS'), os.environ.get('FUZZ_STEER_MS'),
                                          os.environ.get('FUZZ_PART4_MS'))) if seeded else ''
    # The cache this run writes: the main one, the reach pass's, or a quick run's own (LIFT_FUZZ_TAG). The others are only read, so a
    # quick run beside a full run never writes the full run's file, and each sees what the other has proved.
    cache_dir = os.path.join(FUZZ, 'cache')
    cache_path = os.path.join(cache_dir, 'reach.json' if seeded else ('quick_%s.json' % tag if tag else 'results.json'))
    cache = read_cache(cache_path)
    others = [] if seeded else [read_cache(p) for p in sorted(glob.glob(os.path.join(cache_dir, 'quick_*.json')) +
                                                              [os.path.join(cache_dir, 'results.json')]) if p != cache_path]
    use_cache = os.environ.get('LIFT_NO_CACHE') != '1'
    # (The time asked for now, against the time a line was made with: see "Policy on time" above.)
    given = {k: int(os.environ.get(k, d)) for k, d in FUZZ_TIME_SETTINGS.items()}
    def usable(e, n):
        if not (isinstance(e, dict) and e.get('key') in (keys[n] + seed_tag, legacy_keys.get(n, '\0') + seed_tag)):
            return False
        if seeded:
            return bool(e.get('line')) and ' crashed:' not in e['line']
        # (or a settled line, by the same rules: see "Settled lines" above)
        return (fuzz_line_meets_bar(e.get('line', '')) or fuzz_line_settled(e.get('line', '')) is not None) and \
            all(e.get('time', {}).get(k, 0) >= v for k, v in given.items())
    # Where a function's line is kept in its cache file: under its name -- and in the reach pass's, under its name and the seed's
    # tag (2026-11-12, the speed agent). Until then the reach pass kept one line per function, under its name alone: each seed's
    # line took the place of the one before, so the next night's seed 1 found seed 6's line, whose key (it ends in the seed) never
    # matched, and every seed ran again -- the reach.json 0.79.0 started from had 97 of its 112 lines from seed 6, 11 from seed 1.
    # The key is unchanged; only where the line is filed. (A line filed the old way, under the name alone, is still found: its key
    # says which seed it is, and only the same seed's key matches.)
    slot = (lambda n: n + seed_tag) if seeded else (lambda n: n)
    # (LIFT_GAME_SHARDS, shardgame 2026-11-17: a K-shard pass's lines are filed under the name and ':game-shards=K', so the
    #  unsharded first pass of the next night -- which runs a function its own key doesn't find, and then files or drops ITS line
    #  under the name -- never takes their place. Until now a LIFT_SHARDS line, filed under the name, was replaced so every night,
    #  and the longer passes ran in full again. Off: as before.)
    shard_tag = ':game-shards=%d' % GAME_SHARDS if GAME_SHARDS else ''
    if shard_tag:
        slot = (lambda n: n + seed_tag + shard_tag)
    def entry(c, n):
        e = c.get(slot(n))
        return e if e is not None or not seeded else c.get(n)
    def reusable(n):
        # (a control never: fuzz_is_control. 2026-11-14, fastnight)
        return use_cache and n not in controls and any(usable(entry(c, n), n) for c in [cache] + others)
    reused = {n: next(entry(c, n)['line'] for c in [cache] + others if usable(entry(c, n), n)) for n in names if reusable(n)}
    # (A line found under its old key -- the carry-over above -- is saved under the new one, so the next run finds it without the file.)
    carried = 0
    for n in reused:
        e = entry(cache, n)
        if n in legacy_keys and isinstance(e, dict) and e.get('key') == legacy_keys[n] + seed_tag:
            cache[slot(n)] = dict(e, key=keys[n] + seed_tag)
            carried += 1
    if legacy_overlay is not None:
        print('the carry-over: %d lines found under their old keys, saved under the new ones' % carried)
    if GAME_SHARDS_READ and not seeded and use_cache:
        # (LIFT_GAME_SHARDS_READ=K, shardgame 2026-11-17: a function this pass's own key doesn't find, whose K-shard line -- its
        #  own place and key, made by a LIFT_GAME_SHARDS=K pass with at least the time asked for now -- met the bar: proved or
        #  DIFFERENT, never a settled short line. Read only: this pass files its own lines under the name, as always.)
        read_tag = ':game-shards=%d' % GAME_SHARDS_READ
        from_shards = 0
        for n in names:
            if n in reused or n in controls:
                continue
            want = read_keys[n]
            for c in [cache] + others:
                e = c.get(n + read_tag)
                if isinstance(e, dict) and e.get('key') == want and fuzz_line_meets_bar(e.get('line', '')) and \
                        all(e.get('time', {}).get(k, 0) >= v for k, v in given.items()):
                    reused[n] = e['line']
                    from_shards += 1
                    break
        print('from the %d-shard lines (LIFT_GAME_SHARDS_READ): %d functions' % (GAME_SHARDS_READ, from_shards))
    run = [n for n in names if n not in reused]
    if os.environ.get('LIFT_CACHE_SHOW'):     # (which ones run: for checking the cache)
        print('to run: %s' % ' '.join(run))
    if os.environ.get('LIFT_CACHE_KEYS'):
        # (2026-11-14, fastnight: every function's key, its build, whether it's a control and whether the cache has its line, written
        #  to that file -- and nothing built or run. For checking which changes test which functions again: fastnight's
        #  key_mutation.py.)
        json.dump({n: {'key': keys[n], 'variant': variant[n], 'control': n in controls, 'cached': n in reused} for n in names},
                  open(os.environ['LIFT_CACHE_KEYS'], 'w'), indent=0, sort_keys=True)
        print('keys of %d functions -> %s' % (len(names), os.environ['LIFT_CACHE_KEYS']))
        return
    def save_cache():
        # (Written whole to one side and put in place, so a run stopped part-way leaves the last whole file.)
        os.makedirs(os.path.dirname(cache_path), exist_ok=True)
        with open(cache_path + '.tmp', 'w') as f:
            json.dump(cache, f, indent=0, sort_keys=True)
        os.replace(cache_path + '.tmp', cache_path)
    lines = []
    troubles = {}
    all_run = run
    # (2026-11-14, fastnight: one tester for each build the functions to run need -- FUZZ_MOD_NATIVE 0 for most, 1 for the native
    #  ones: fuzz_variant -- each built and run as the one tester was. A build tests only its own functions.)
    for build_variant in sorted({variant[n] for n in all_run}):
        run = [n for n in all_run if variant[n] == build_variant]
        # The tester, with the functions to run (the rest are the cache's).
        text = fuzz_original_text(heading + '\n') + '\n' + '\n'.join(originals[n] for n in run)
        open(os.path.join(build, 'originals.inc'), 'w', newline='\n').write(text)
        run_called = sorted({c for n in run for c in cands[n].get('callees', [])} |
                            {c for n in run for c in functions[n]['calls'].values()})
        out = ['// fuzz_gen.cpp: the candidates to test and their originals, for the random-input tester. WRITTEN BY tools/lift/lift.py.'] + FUZZ_GEN_TEXT['top'] + OVERLAY_HELPER + (
            POINTER_HELPER if any('lift_pointer<' in cands[n]['cpp'] for n in run) else []) + (
            TAIL_HELPER if any(TAIL_USE in cands[n]['cpp'] for n in run) else []) + callee_declarations(run_called) + ['']
        if any(cands[n].get('hooks') for n in run):
            # (The hook places of the functions converted with their hook calls, for both versions: hooks.py, 2026-11-12.)
            out += HOOK_TESTER + [line for n in run for line in hook_places(n, cands[n])] + ['']
        if any(modrep.is_modrep(n) for n in run):
            out += mod_text
        if any(cands[n].get('native') for n in run):
            out += image_lines
        # The port's additions (port_additions.py): the port functions they call, declared as the translated files do, and the
        # wrappers converted functions call in place of a call the port added code around. Both versions run the same additions
        # and call the same recording stand-ins (written below).
        declared = set()
        for n, v in cands.items():
            if 'cpp' in v and v.get('port'):
                for d in v['port']['declarations']:
                    if d not in declared:
                        declared.add(d)
                        out.append(d)
        out += FUZZ_GEN_TEXT['cstring'] + unaligned_helpers([cands[n]['cpp'] for n in run])
        # (The mod's private data's host objects, declared before the converted functions that use them: private_table_lines.)
        out += sorted({'extern "C" u8 lift_mod_private_%s[];' % re.sub(r'\W', '_', s) for n in run for s in cands[n].get('private', [])})
        out += FUZZ_GEN_TEXT['game_open']
        # (abi.py's functions: their helpers, and for the stand-ins below, what each callee reads beyond its parameters and which
        #  scratch registers it may change -- see fuzz_stand_in_abi in fuzz_main.cpp. Without any of them the file is as before.)
        run_abi = any(cands[n].get('abi') for n in run)
        if run_abi:
            out[3:3] = [abi.CPP_HELPERS]
        for name in run:
            out += [tester_cpp(cands[name]), '']
        out += FUZZ_GEN_TEXT['game_close']
        out += FUZZ_GEN_TEXT['memory']
        # (And the originals' lwl / lwr / swl / swr through that MEM_W, so what they write is noted too: UNALIGNED_ORIGINALS.)
        out += unaligned_originals(text) + FUZZ_GEN_TEXT['originals']
        # What's called is a stand-in here, recording what it was given and handing back random results. See fuzz_stand_in.
        out += [stand_in(c, run_abi) for c in run_called] + ['']
        out += overlay_lines
        out += port_stubs + ['']
        for name in run:
            out += cands[name].get('port', {}).get('wrappers', [])
        out += ['']
        for name in run:
            out += own[name][0]
        out += private_table_lines(cands, run, mod_places)
        out += native_rules_lines(cands, run)
        out += write_cut_lines(run)         # (the write cut, 2026-11-15: rules084)
        if ARGS_STRICT:
            out += args_outgoing_lines(run)     # (the argument compare: each function's outgoing stack words)
        out += FUZZ_GEN_TEXT['table_start'] + [own[n][1] for n in run] + FUZZ_GEN_TEXT['table_end']
        gen = os.path.join(build, 'fuzz_gen.cpp')
        open(gen, 'w', newline='\n').write('\n'.join(out))
        exe = os.path.join(build, 'fuzz.exe')
        include = fuzz_include(build)     # (the folders fuzz_preprocessed reads the keys' text with, in the same order)
        # (Two objects, so fuzz_main.cpp can have its own -O level: FUZZ_HARNESS_LEVEL.)
        def compile_tester(source, level):
            obj = os.path.join(build, os.path.basename(source) + '.o')
            flags = [level if f == '-O1' else f for f in FUZZ_COMPILE_FLAGS] + ['-DFUZZ_MOD_NATIVE=%d' % build_variant]
            command = [CLANG] + flags + ['-c', '-o', obj, source]
            for folder in include:
                command += ['-I', folder]
            return obj, subprocess.run(command, capture_output=True, text=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            built = list(pool.map(compile_tester, [gen, os.path.join(FUZZ, 'fuzz_main.cpp')], ['-O1', FUZZ_HARNESS_LEVEL]))
        for _, p in built:
            if p.returncode != 0:
                print(p.stderr[:3000])
                raise SystemExit('the tester did not compile')
        p = subprocess.run([CLANG, '-static', '-o', exe] + [obj for obj, _ in built], capture_output=True, text=True)
        if p.returncode != 0:
            print(p.stderr[:3000])
            raise SystemExit('the tester did not link')
        # (LIFT_GAME_SHARDS: each function's shards in one pool of TEST_WORKERS, game_shard_results; its own pool, so this one is idle)
        with concurrent.futures.ThreadPoolExecutor(max_workers=1 if GAME_SHARDS else TEST_WORKERS) as pool:
            if GAME_SHARDS:
                results = game_shard_results(exe, run, tests, sizes)
            else:
                results = pool.map(fuzz_one, [(exe, n, tests, sizes[n]) for n in run])
            for done, (name, line, trouble) in enumerate(results, 1):
                if line:
                    lines.append(line)
                # (Kept for a later run only once it meets the bar; a timed-out one has no line, and whatever an older run left goes.)
                if name in controls:
                    cache.pop(slot(name), None)     # (a control is never kept: fuzz_is_control)
                    cache.pop(name, None)
                elif seeded:
                    # (the reach pass's own cache: see seed_tag)
                    if line and ' crashed:' not in line:
                        cache[slot(name)] = {'key': keys[name] + seed_tag, 'line': line}
                        if slot(name) != name and isinstance(cache.get(name), dict) and cache[name].get('key') == keys[name] + seed_tag:
                            del cache[name]     # (the same line filed the old way)
                elif line and (fuzz_line_meets_bar(line) or fuzz_line_settled(line) is not None):
                    # (slot(name): the name, or with LIFT_GAME_SHARDS the name and ':game-shards=K' -- see shard_tag)
                    cache[slot(name)] = {'key': keys[name], 'time': given, 'line': line}     # (a settled line's kind is read from it)
                else:
                    cache.pop(slot(name), None)
                if trouble:
                    troubles[name] = trouble
                if done % 256 == 0:
                    save_cache()       # (a run stopped part-way keeps what it has found)
        save_cache()
    run = all_run
    if not run and carried:
        save_cache()        # (nothing to run, but lines moved to their new keys)
    lines += [reused[n] for n in names if n in reused]
    if only is not None and os.path.exists(FUZZ_REPORT):   # (a subset: its lines replace theirs, the rest kept)
        kept = [l for l in open(FUZZ_REPORT).readlines() if l.split() and l.split()[0] not in only]
        lines = kept + lines
    open(FUZZ_REPORT, 'w').write(''.join(lines))
    settled = 0 if seeded else sum(1 for n in reused if fuzz_line_settled(reused[n]) is not None)
    print('tested %d functions (%d run, %d from the cache: unchanged since a run that proved them or found them different, '
          '%d of those settled short of the bar), %d random inputs each. Trouble: %s' % (
              len(names), len(run), len(reused), settled, tests, troubles if troubles else 'none'))
    score([FUZZ_REPORT], False)


def read_reports(paths):
    """Adds up the reports: name -> calls, checked, different, instructions reached in any report, total, first difference."""
    seen = {}
    for path in paths:
        for line in open(path):
            fields = line.split()
            name, calls, checked, different, reached, total, first = fields[:7]      # (an 8th: part 1's own compared count)
            s = seen.setdefault(name, {'calls': 0, 'checked': 0, 'different': 0, 'reached': 0, 'total': int(total), 'first': '-', 'bits': 0, 'plain_random': 0})
            s['calls'] += int(calls)
            s['checked'] += int(checked)
            s['plain_random'] += int(fields[7]) if len(fields) > 7 else 0
            s['different'] += int(different)
            s['bits'] |= int(reached, 16)
            if first != '-' and s['first'] == '-':
                s['first'] = first
    for s in seen.values():
        s['reached'] = bin(s['bits']).count('1')
    return seen


def counted_once(proved_names, analysis, stubs_out=None):
    """(2026-11-12, middles) The proved count with each of the game's instructions counted once.

    score's own count adds up whole translated functions: proved, the sizes of the proved ones (an entry merge.py put
    together counts its pieces in its size); of the game, every translated function's own count (analysis 'n'). For the
    pieces that comes out right: the entry's own count and each piece's add up to the whole. But the translator also has
    many functions twice: a middle of a function (merge.holders) is a second copy of instructions another function already
    holds, from a symbol at an address inside it (another overlay's function starts there, or one added by hand). The game's
    count has those instructions twice, the proved count at most once: on 0.80's candidates, 99 middles of 17,288
    instructions are held whole by proved functions and still count as not converted ("is the middle of a function"), and
    the game's count is 24,465 instructions more than it has places. Here each instruction is one place -- its section
    (overlays share addresses) and its address -- counted once, proved when a proved function, as converted (put together
    where it is), holds it. Nothing about proving changes: only how what is proved is counted.
    Returns (proved places, all places, the functions not proved every instruction of which a proved one holds: [names])."""
    import merge
    separate = to_asm.read_functions(merge=False)
    whole = merge.apply(separate)
    table = merge.sections()
    place = lambda n, a: (merge.section_of(n, table), a)
    game = [n for n in analysis if not modrep.is_modrep(n) and n in separate]
    every = {place(n, a) for n in game for a in separate[n]['ins']}
    held = {place(n, a) for n in proved_names if n in whole for a in whole[n]['ins']} & every
    inside = sorted(n for n in game if n not in proved_names and separate[n]['ins'] and
                    all(place(n, a) in held for a in separate[n]['ins']))
    if stubs_out is not None:
        stubs_out.update(port_stubs(separate, proved_names, place, held, every))
    return len(held), len(every), inside


def port_stubs(separate, proved_names, place, held, every):
    """(2026-11-14, merge083: the user's counting decision of 2026-11-14) The six-man port's stubs: a game function whose first
    instruction the port made `j rw_X` -- its own MIPS for the function, rw_X, translated and converted on its own (rest100's
    MAP.md group G). As the port runs it, the function is that jump: rw_X does its work. So, as a function the mod replaces counts
    through its proved copy, a stub counts as converted -- each of its places once -- when its rw_X is proved. Only counting:
    nothing about proving changes. Returns {'stubs': {stub: rw_X} (all found), 'counted': [stubs whose rw_X is proved],
    'places': the places they add (not in the headline already)}."""
    import merge
    stubs = {}
    for n, f in separate.items():
        if n.startswith('rw_') or modrep.is_modrep(n) or not f['ins']:
            continue
        first = min(f['ins'])
        mn, ops = f['ins'][first]
        if mn != 'j':
            continue
        js = merge.jumps(f)
        target = js.get(first + 4) or js.get(first)
        if not target or not target.startswith('rw_') or not separate.get(target, {}).get('ins'):
            continue
        try:
            if min(separate[target]['ins']) != int(ops.strip().lstrip('L_'), 16):
                continue        # (a jump into rw_X's middle is not "rw_X does its work")
        except ValueError:
            continue
        stubs[n] = target
    counted = sorted(n for n, t in stubs.items() if t in proved_names and n not in proved_names)
    # (each stub's places not in the headline already: a place counts once however many functions hold it)
    places = {n: {place(n, a) for a in separate[n]['ins']} & every - held for n in counted}
    return {'stubs': stubs, 'counted': counted, 'places': places}


# (2026-11-18, twinnotes: the in-play twin check's results in the notes -- the independent re-check reviewct2 APPROVED the caller
#  twin v11 results on 0.84 for the notes, with this exact wording and caveats: its review, section 5.) The evidence is data in
#  the repository, tools/lift/inplay/*.json, made by inplay_from_twin.py from the twin's run reports: per function the completed
#  checks (N), the scenarios they ran in, the caveat counts, and the function's body hash at the build compared. A note is
#  written only while the function is converted exactly as it was then (inplay_body_hash of its candidates.json entry); any
#  later change of the function drops it -- a note never outlives the code it was about. Comments only: nothing here is code.
INPLAY = os.path.join(HERE, 'inplay')

# The header's caller-twin note, as approved (the review's section 5): once in lifted.cpp, referred to by each caller's note.
CALLER_TWIN_V11_NOTE = (
    'Caller twin v11 (headless lockstep, sampled: the first 300 calls, then 1 in 256): the original ran for real; the converted '
    'body ran on the side with each call matched in order and the callee\'s recorded effects replayed -- callees were not run '
    'against it. Compared at each call: the callee, every register of its explained read set, $sp, the stack words it reads, '
    'and the bytes its call tree read. Compared at the end: memory written outside the function\'s own frame, the result, the '
    'saved registers and $sp. NOT compared: its own frame; registers only in the raw read set (counted); bytes read by native '
    'C++ through raw pointers (arcade_bones, arcade_view, arcade_stories, main exports, the mod\'s native private bytes) and by '
    'the runtime\'s queue / thread code; callee reads in the 64 KB below the call\'s stack pointer (on the game thread this band '
    'holds globals); unjournaled writes on a 4 KB page that also holds a journaled write, or above the first 8 MB. Only the '
    'paths reached. Checks given up (caps, 50 ms, untracked writer) or yielded are not counted.')
CALLER_TWIN_REF = "see the header's caller-twin note"


def inplay_body_hash(entry):
    """A converted function's body hash for the in-play notes: its whole candidates.json entry -- the converted C++ (before names
    and fields, which check-names shows change no machine code) and how it is called (its parameters, result, ABI, hooks, port
    additions) -- as sorted JSON, SHA-256, the first 16 hex digits."""
    text = json.dumps(entry, sort_keys=True, separators=(',', ':'), ensure_ascii=True)
    return hashlib.sha256(text.encode('ascii')).hexdigest()[:16]


def inplay_sets(folder=None):
    """The in-play evidence files (tools/lift/inplay/*.json, in name order): [their contents]."""
    folder = folder or INPLAY
    if not os.path.isdir(folder):
        return []
    return [json.load(open(os.path.join(folder, f), encoding='utf8')) for f in sorted(os.listdir(folder)) if f.endswith('.json')]


def inplay_text(entry, build, set_name):
    """One function's in-play note, in the approved words (the review's section 5)."""
    scenarios = ', '.join(entry['scenarios'])
    if entry['twin'] == 'leaf':
        return 'In play: %d comparisons, no difference (leaf twin, %s, %s)' % (entry['n'], build, scenarios)
    if set_name != 'caller_twin_v11':
        raise SystemExit('lift.py: in-play evidence set %r has no approved wording for the caller twin' % set_name)
    text = 'In play: %d comparisons, no difference (caller twin v11, %s, %s; %s)' % (entry['n'], build, scenarios, CALLER_TWIN_REF)
    if entry.get('untracked'):
        text += '; %d checks given up for an untracked writer' % entry['untracked']
    if entry.get('raw_only'):
        text += '; %d raw-only argument mismatches (registers the independence proofs show unread)' % entry['raw_only']
    if entry.get('waited'):
        text += '; %d checks waited for another thread, its effects replayed' % entry['waited']
    return text


def inplay_notes(cands, names, folder=None):
    """{name: its in-play note} for the functions given, from the evidence files: only where the function's body hash now is the
    one it had when compared (otherwise dropped, and said so). Prints what it found."""
    notes, dropped = {}, []
    for ev in inplay_sets(folder):
        for name, entry in ev['functions'].items():
            if name not in names or 'cpp' not in cands.get(name, {}):
                continue
            if inplay_body_hash(cands[name]) != entry['body']:
                dropped.append(name)
                continue
            text = inplay_text(entry, ev['build'], ev['set'])
            notes[name] = notes[name] + '. ' + text if name in notes else text
    if notes or dropped:
        print('in-play notes: %d functions (caller twin %d, leaf twin %d); %d dropped: converted differently since they were compared%s' % (
            len(notes), sum(1 for t in notes.values() if CALLER_TWIN_REF in t), sum(1 for t in notes.values() if '(leaf twin,' in t),
            len(dropped), (' (%s%s)' % (', '.join(sorted(dropped)[:8]), ' ...' if len(dropped) > 8 else '')) if dropped else ''))
    return notes


def evidence_note(in_play, plain_random):
    """A converted function's note in lifted.cpp: the evidence it has, said as it is (2026-11-16, honesty: the user's approval of
    the outside review's item 3). The random-input tester's count always; the in-play twin check's (lift_twin.cpp) only when it
    has compared the function at least once -- a note never names a check that did not run. "Random-tested", not "Proved": the
    tester's evidence is strong, not a proof (lifted.cpp's header says what it rests on)."""
    note = 'Random-tested: %d plain random inputs, all instructions reached, no difference' % plain_random
    if in_play > 0:
        note += '; in play (lift_twin.cpp): %d comparisons, no difference' % in_play
    return note


def counted_figure(lines):
    """The counted figure in score's printed lines, as night.ps1 reads it for the landmarks: headline plus copies plus stubs,
    else headline plus copies, else the headline alone. A string such as '92.08%', or None when score couldn't work it out."""
    for pattern in (r'headline plus copies plus stubs: ([0-9.]+)%', r'headline plus copies: ([0-9.]+)%',
                    r'^converted, each instruction counted once: \d+ of \d+ instructions, ([0-9.]+)%'):
        found = [m.group(1) for m in (re.search(pattern, line) for line in lines) if m]
        if found:
            return found[-1] + '%'
    return None


def shipped_copies():
    """The shipped mod's copies: (name -> hash, None), or (None, why not). The shipped mod is modrep.MOD_BUILD (mod/build/mod_six),
    translated again to read each copy's hash (modrep.current_hashes). (pipefix, 2026-11-17)"""
    try:
        return modrep.current_hashes(), None
    except BaseException as e:      # (SystemExit from modrep: no build, the tool didn't build or run)
        return None, "the shipped mod's copies could not be read (%s)" % str(e).strip().split('\n')[0][:160]


def stale_copies(cands, copies, shipped):
    """The converted copies of the mod's (modrep_func_X, proved) that are not the shipped mod's copies now: name -> why. shipped:
    shipped_copies(). The mod loader's hash code must also be modrep's (modrep.check), or it uses none. (pipefix, 2026-11-17:
    score counts none of these.)"""
    if not copies:
        return {}
    loader = os.path.join(modrep.PORT, 'lib', 'N64ModernRuntime', 'librecomp', 'src', 'lift_modrep_hash.h')
    mine = os.path.join(modrep.TOOL_SOURCE, 'lift_modrep_hash.h')
    if os.path.exists(loader) and open(loader, 'rb').read().replace(b'\r', b'') != open(mine, 'rb').read().replace(b'\r', b''):
        return {n: "the mod loader's lift_modrep_hash.h differs from tools/lift/modrep's: it uses no converted copy" for n in copies}
    now, why = shipped
    if now is None:
        return {n: why for n in copies}
    out = {}
    for n in copies:
        # (2026-11-18, negfix, as reviewed: a copy with no record of the mod's copy it was converted from can't be shown to be
        #  the shipped one -- not counted)
        h = ((cands.get(n) or {}).get('modrep') or {}).get('hash')
        if not h:
            out[n] = "no record of the mod's copy it was converted from (no hash): can't be shown to be the shipped mod's"
        elif n not in now:
            out[n] = 'the shipped mod no longer replaces %s' % modrep.replaced(n)
        elif now[n] != h:
            out[n] = "stale: converted from the mod's copy %s, the shipped mod's is %s" % (h, now[n])
    return out


def score(paths, write):
    # (2026-11-16, reviewac G1: the argument compare's results count only with the strict read-set checker on too -- a register
    #  passed through unchanged from entry is not compared, and safeguard 1 is what fails a caller whose read set is short
    #  there. So no count is made from FUZZ_ARGS_STRICT=1 alone.)
    if ARGS_STRICT and os.environ.get('LIFT_CHECK_READS') != '1':
        raise SystemExit('lift.py: FUZZ_ARGS_STRICT=1 counts only with LIFT_CHECK_READS=1 too (reviewac G1): not scoring')
    cands = json.load(open(CANDIDATES))
    index_total = sum(v['size'] for v in cands.values())
    # (Whatever the random-input tester last found always counts.)
    seen = read_reports(list(paths) + ([FUZZ_REPORT] if os.path.exists(FUZZ_REPORT) and FUZZ_REPORT not in paths else []))
    in_play = read_reports([p for p in paths if p != FUZZ_REPORT])
    random_only = read_reports([FUZZ_REPORT]) if os.path.exists(FUZZ_REPORT) else {}
    # (Not the mod's copies' own entries: each stands in for a game function counted here at its own size. modrep.py)
    analysis = json.load(open(os.path.join(HERE, 'analysis.json')))
    all_total = sum(v.get('n', 0) for n, v in analysis.items() if not modrep.is_modrep(n))
    proved, differing, partial, never, too_few = {}, [], [], [], []
    for name, s in seen.items():
        if 'cpp' not in cands.get(name, {}):
            continue        # (A later check refused it since this report was made.)
        if s['different']:
            differing.append((name, s))
        elif s['checked'] < MIN_CHECKED:
            never.append(name)
        elif s['reached'] < s['total']:
            partial.append(name)
        elif random_only.get(name, {}).get('plain_random', 0) < FULL_TESTS:
            too_few.append(name)        # (every instruction reached, no difference -- but fewer plain random tests than the standard)
        else:
            proved[name] = evidence_note(in_play.get(name, {}).get('checked', 0), random_only.get(name, {}).get('plain_random', 0))
    ins = lambda names: sum(cands[n]['size'] for n in names)
    # (2026-11-16, agent argcheck: the strict read-set checker, LIFT_CHECK_READS=1 -- approved by the user 2026-11-16, nothing counts
    #  from it until its independent review: scratch/agents/argcheck/REVIEW_ARGCHECK.txt. readcheck.py works out again, with code
    #  of its own and the strict rule for a saved register's store to the stack, which registers each function reads; a proved
    #  function whose analysis.json `use` (or analysis_mod.json's) leaves out one of them that the independence files don't
    #  explain is not counted: "analysis disagrees", until it is explained; and, rule B below, neither is a proved caller of
    #  such a function. Off: the count as before.)
    disagrees = {}
    if os.environ.get('LIFT_CHECK_READS') == '1':
        import readcheck
        ck = readcheck.Checker(HERE)
        reads, _ = ck.run(explained=True)
        both = dict(analysis)
        if os.path.exists(os.path.join(HERE, 'analysis_mod.json')):
            both.update(json.load(open(os.path.join(HERE, 'analysis_mod.json'))))
        found = readcheck.findings(reads, both)
        disagrees = {n: found[n] for n in proved if n in found}
        for n in disagrees:
            del proved[n]
        print('strict read-set checker (LIFT_CHECK_READS): analysis disagrees for %d functions, %d of them proved -- not counted:' % (
            len(found), len(disagrees)))
        for n in sorted(disagrees):
            print('  analysis disagrees: %s reads %s' % (n, ','.join(disagrees[n])))
        others = sorted(set(found) - set(disagrees))
        if others:
            print('  (not proved, so nothing to take off: %s)' % ', '.join('%s +%s' % (n, ','.join(found[n])) for n in others))
        # (Rule B -- APPROVED by the user 2026-11-16, the independent review's (reviewac) recommendation: a proved CALLER of a
        #  function with an unexplained finding is not counted either, until the finding is explained. Its stand-in for that
        #  callee was made from the short read set, so its proof may have passed a register the callee reads -- an argument the
        #  argument compare skips (passed through from entry), or any other register kind (s/fp/gp/t/v/f20+, fcc). A call, a
        #  run-time lookup, a plat_game_call case, or a jump into the function: readcheck.callers_of. Score only.)
        rule_b = {n: cs for n, cs in ck.callers_of(set(found)).items() if n in proved}
        for n in rule_b:
            del proved[n]
        print('rule B (LIFT_CHECK_READS): %d proved functions call one the analysis disagrees with -- not counted:' % len(rule_b))
        for n in sorted(rule_b):
            print('  calls a function analysis disagrees with: %s -> %s' % (n, ', '.join(rule_b[n])))
    # (2026-11-17, pipefix) A converted copy of the mod's counts only while it is the shipped mod's copy: the hash it was converted
    #  from must be the one the mod as built now (mod/build/mod_six, what ships) has -- the very test the game's mod loader makes
    #  before it uses one, and write_release's before it releases one. The 0.84 night counted 133 copies made from the mod as
    #  it was before the run rebuilt it: the build did not use them (stale at "build exe"), the score said 93.21% for 86.66%.
    #  Nothing to compare against (no build, or modrep's tool can't run): none counts.
    shipped = shipped_copies()
    # (2026-11-18, negfix, as reviewed: the guard checks what each entry IS -- the game's converted copies of the mod's functions
    #  (modrep_func_X). The native mod's own entries (modrep_nat_, natneg, natpos, natthru: prove_gen.py, marked 'native') are
    #  not copies -- they have no copy hash, stand for none of the game's instructions here, and are judged by m5_analyse.py --
    #  and proving one made this crash (KeyError 'modrep'). Anything else under modrep_ is checked as a copy.)
    stale = stale_copies(cands, [n for n in proved if modrep.is_modrep(n) and not (cands.get(n) or {}).get('native')], shipped)
    if stale:
        print("the mod's copies proved but not the shipped mod's -- not counted: %d" % len(stale))
        for n in sorted(stale):
            del proved[n]
            print('  not counted: %s -- %s' % (n, stale[n]))
    # (2026-11-18, twinnotes) The in-play twin results where there are some, after the random-input note: inplay_notes.
    for n, text in inplay_notes(cands, set(proved)).items():
        proved[n] += '. ' + text
    game_proved = [n for n in proved if not modrep.is_modrep(n)]
    old = 'proved: %d functions, %d instructions, %.2f%% of the game' % (len(game_proved), ins(game_proved), 100 * ins(game_proved) / all_total)
    # (2026-11-12, middles; the HEADLINE since the user's decision of 2026-11-12, final081: each of the game's instructions counted
    #  once -- a piece or a middle with what holds it, counted_once. The old count stays beside it, labelled: it adds up whole
    #  translated functions, and the game's total has the middles' instructions twice. If the new one can't be worked out it says
    #  why and the old line is the headline, so a release never stops for it.)
    once_total = None      # (the game's total counted once, for the copies' share below; rest100 2026-11-14)
    figure = None          # (the counted figure, as night.ps1 reads it from these lines, for lifted.cpp's header: honesty 2026-11-16)
    stubs = {}             # (the port's stubs, counted through their proved rw_ replacements: port_stubs, merge083 2026-11-14)
    try:
        held, every, inside = counted_once(set(game_proved), analysis, stubs)
        once_total = every
        figure = 100 * held / every
        print('converted, each instruction counted once: %d of %d instructions, %.2f%% of the game (%d functions proved)' % (
            held, every, 100 * held / every, len(game_proved)))
        print('  of the functions not proved, %d (%d instructions) are held whole by proved ones: pieces and middles of them' % (
            len(inside), sum(analysis[n].get('n', 0) for n in inside)))
        print('  old count (each translated function at its own size; the game total counts the middles twice): ' + old)
    except Exception as e:
        print('converted, each instruction counted once: not worked out (%s)' % e)
        print(old)
    copies = [n for n in proved if modrep.is_modrep(n)]
    if copies:
        # (Apart from the line above: a copy counts only where the mod loader chooses it, and at the size of the game function
        #  it stands in for, which is what the percentage counts.)
        stands_for = sum(analysis.get(modrep.replaced(n), {}).get('n', 0) for n in copies)
        print("the mod's copies proved, not in the line above: %d functions, %d instructions of theirs, standing in for %d instructions, %.2f%% of the game more if the loader uses them all" % (
            len(copies), ins(copies), stands_for, 100 * stands_for / all_total))
        if once_total:
            # (rest100, 2026-11-14) The share above is of the OLD total, which counts the middles twice (383,750 in 0.82), while
            # the headline's is counted once (359,285): added to the headline it understated the whole by 0.50%. Checked on 0.82
            # (scratch/agents/rest100/map.py): the 27,793 instructions the copies stand in for are exactly the counted-once places
            # of their replaced functions, none of them in the headline -- so over the counted-once total this is their share.
            print("  the same counted once: %.2f%% of the game (headline plus copies: %.2f%%)" % (
                100 * stands_for / once_total, 100 * (held + stands_for) / once_total))
            figure = 100 * (held + stands_for) / once_total
    if once_total and stubs.get('stubs'):
        # (2026-11-14, merge083: the user's decision -- a port stub counts through its proved rw_ replacement, as a mod-replaced
        #  function counts through its proved copy. A stub the mod also replaces with a proved copy is in the copies' line
        #  already: not again here.)
        # (pipefix, 2026-11-17: and a stub the mod replaces at all is the mod's copy as the game runs it, not the jump to rw_X --
        #  modrep_func_800EDCF4 is nine instructions of the mod's own -- so it counts through a proved copy or not at all. Until
        #  then only stubs with a proved copy were left out here; with 0.84's stale copies not counted, func_800DBA88 and
        #  func_800EDCF4 would have come in through their rw_. The shipped mod's list, else modrep/out's.)
        replaced = {modrep.replaced(n) for n in copies} | {modrep.replaced(n) for n in (shipped[0] if shipped[0] is not None else modrep.index())}
        counted = [n for n in stubs['counted'] if n not in replaced]
        add = set().union(*(stubs['places'][n] for n in counted)) if counted else set()
        n_counted = len(counted)
        copies_n = stands_for if copies else 0
        print("port stubs counted through proved rw_ replacements: %d of the port's %d stubs (their rw_ proved), %d instructions counted once, %.2f%% of the game (headline plus copies plus stubs: %.2f%%)" % (
            n_counted, len(stubs['stubs']), len(add), 100 * len(add) / once_total, 100 * (held + copies_n + len(add)) / once_total))
        figure = 100 * (held + copies_n + len(add)) / once_total
    print('compared, no difference, but not every instruction reached: %d functions, %d instructions' % (len(partial), ins(partial)))
    print('never ran: %d functions, %d instructions' % (len(never), ins(never)))
    print('too few tests (no difference, all reached, under %d random tests: wait for a longer run): %d functions, %d instructions' % (
        FULL_TESTS, len(too_few), ins(too_few)))
    if write:
        open(os.path.join(FUZZ, 'too_few.txt'), 'w', newline='\n').write(''.join(n + '\n' for n in sorted(too_few)))
    print('DIFFERENT: %d functions, %d instructions' % (len(differing), ins([n for n, _ in differing])))
    for n, s in differing[:40]:
        print('  %s %d of %d: %s' % (n, s['different'], s['checked'], s['first']))
    if write:
        only = os.environ.get('LIFT_ONLY')
        if only:
            # To find which function breaks a build: only the ones named in this file (one per line), of those proved.
            keep = set(open(only).read().split())
            proved = {n: v for n, v in proved.items() if n in keep}
            print('LIFT_ONLY: %d of them' % len(proved))
        write_release(cands, proved, '%.2f%%' % figure if figure is not None else None)


def partial_names():
    """The functions fuzz/report.txt shows with no difference but not every instruction reached by a compared test: the reach pass's list."""
    seen = read_reports([FUZZ_REPORT])
    return sorted(n for n, s in seen.items() if not s['different'] and s['checked'] >= MIN_CHECKED and s['reached'] < s['total'])


def reach_pass(seeds, tests):
    """The reach pass (2026-11-09). Which instructions steering gets to depends on where its tests start: of the 145 functions the 0.72.0
    run left with 1 to 181 instructions unreached, 11 reached them all on a run with another sequence of tests. So each function still
    partial gets more runs of the tester (FUZZ_SEED), each compared exactly as every other test is: an instruction one of them reaches
    counts as reached by a compared test, and a difference in any of them is a difference, as it would be in the main pass. Only that
    is taken from them: the tests and compared columns, and the plain-random count the proof needs, are still the main pass's own."""
    global FUZZ_REPORT
    main = FUZZ_REPORT
    saved = {k: os.environ.get(k) for k in ('LIFT_SUBSET', 'FUZZ_SEED', 'FUZZ_MS', 'FUZZ_STEER_MS', 'FUZZ_PART4_MS')}
    side = main + '.reach'
    listing = main + '.reach_list'
    try:
        for seed in range(1, seeds + 1):
            names = partial_names()
            print('reach pass, seed %d: %d functions still partial' % (seed, len(names)))
            if not names:
                break
            open(listing, 'w', newline='\n').write(''.join(n + '\n' for n in names))
            os.environ.update({'LIFT_SUBSET': listing, 'FUZZ_SEED': str(seed), 'FUZZ_MS': '15000', 'FUZZ_STEER_MS': '15000', 'FUZZ_PART4_MS': '20000'})
            if os.path.exists(side):
                os.remove(side)
            FUZZ_REPORT = side
            run_fuzz(tests)
            FUZZ_REPORT = main
            more = {}
            for line in open(side):
                f = line.split()
                if len(f) in (7, 8):
                    more[f[0]] = f
            out = []
            for line in open(main):
                f = line.split()
                if f and f[0] in more and len(f) in (7, 8):
                    g = more[f[0]]
                    f[4] = format(int(f[4], 16) | int(g[4], 16), 'x')
                    f[3] = str(int(f[3]) + int(g[3]))
                    if f[6] == '-':
                        f[6] = g[6]
                    line = ' '.join(f) + '\n'
                out.append(line)
            open(main, 'w').write(''.join(out))
    finally:
        FUZZ_REPORT = main
        for k, v in saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        for path in (side, listing):
            if os.path.exists(path):
                os.remove(path)
    print('reach pass done: %d functions still partial' % len(partial_names()))


def too_few_names():
    """The functions fuzz/report.txt shows with no difference and every instruction reached, but fewer than FULL_TESTS random
    tests (the tester's time limit stopped them): the long-run pass's list (night.ps1, FUZZ_MS). 2026-11-08"""
    seen = read_reports([FUZZ_REPORT])
    # (2026-11-14, rest100) And the partly reached ones short of the count. The reach pass comes after the long passes and only
    # adds the instructions its runs reach, never plain tests, so a function both partial and short -- 12 in 0.82 (3,164
    # instructions), func_80035040 with 4,619 plain tests -- stayed short even when its reach was made whole: it never got
    # the long pass's time. A longer run also reaches what more tests reach. LIFT_LONG_PARTIAL=0: as before.
    partial_too = os.environ.get('LIFT_LONG_PARTIAL', '1') != '0'
    # (2026-11-14, rest100) LIFT_TOO_FEW_AT_LEAST=N: only those with at least N plain tests already -- night.ps1's longest pass,
    #  for the ones near enough to the count that more time gets them there.
    at_least = int(os.environ.get('LIFT_TOO_FEW_AT_LEAST', '0'))
    return sorted(n for n, s in seen.items()
                  if not s['different'] and s['checked'] >= MIN_CHECKED and (s['reached'] >= s['total'] or partial_too)
                  and at_least <= s['plain_random'] < FULL_TESTS)


def main():
    global FUZZ_REPORT      # (prove sets its own report)
    what = sys.argv[1]
    if what == 'reach':
        reach_pass(int(sys.argv[2]) if len(sys.argv) > 2 else 6, int(sys.argv[3]) if len(sys.argv) > 3 else 40000)
        return
    if what == 'too-few':
        names = too_few_names()
        open(sys.argv[2], 'w', newline='\n').write(''.join(n + '\n' for n in names))
        print('too few tests: %d functions -> %s' % (len(names), sys.argv[2]))
        return
    if what == 'candidates':
        candidates(sys.argv[2], sys.argv[3])
    elif what == 'twin':
        write_twin(json.load(open(CANDIDATES)))
    elif what == 'release':
        score(sys.argv[2:], True)
    elif what == 'fuzz':
        run_fuzz(int(sys.argv[2]) if len(sys.argv) > 2 else 200000)
    elif what == 'check-names':
        check_names()
    elif what == 'check-modrep':
        # The converted copies of the mod's in lifted.cpp that the mod as built now no longer has as they were (modrep.py):
        # listed, and exit 1. (build_mod.ps1 and install can call this: such a copy waits to be converted again, and the game's
        # mod loader runs the mod's own copy meanwhile.)
        bad = modrep.check(sys.argv[2] if len(sys.argv) > 2 else os.path.join(GAME, 'lifted.cpp'))
        for name, why in bad:
            print('STALE %s: %s' % (name, why))
        print("%d stale converted copies of the mod's" % len(bad) if bad else "the converted copies of the mod's in lifted.cpp are the mod's copies as built now")
        sys.exit(1 if bad else 0)
    elif what == 'independence':
        run_independence(int(sys.argv[2]) if len(sys.argv) > 2 else 100000)
    elif what == 'score':
        score(sys.argv[2:], False)
    elif what == 'prove':
        # (2026-11-10) A quick run of a few functions, to try a fix without waiting for a full run, and beside one if need be:
        #   python tools/lift/lift.py prove <tag> <function> [<function> ...]
        # Its own build folder (fuzz/build_<tag>), report (fuzz/report_<tag>.txt) and cache (fuzz/cache/quick_<tag>.json); the
        # full run reads that cache, so what it proves is used there when the function's test is the same. Exactly the same test
        # and bar as the full run. Not during a full run's candidates stage, which rewrites candidates.json.
        tag, wanted = sys.argv[2], sys.argv[3:]
        if not re.fullmatch(r'[a-z0-9_]+', tag) or not wanted:
            raise SystemExit('lift.py prove <tag: lowercase letters, digits, _> <function> [<function> ...]')
        os.makedirs(os.path.join(FUZZ, 'cache'), exist_ok=True)
        listing = os.path.join(FUZZ, 'cache', 'prove_%s.txt' % tag)
        open(listing, 'w', newline='\n').write(''.join(n + '\n' for n in wanted))
        os.environ.update({'LIFT_SUBSET': listing, 'LIFT_FUZZ_TAG': tag})
        FUZZ_REPORT = os.path.join(FUZZ, 'report_%s.txt' % tag)
        run_fuzz(int(os.environ.get('LIFT_TESTS', '200000')))
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
