#!/usr/bin/env python3
"""Which instructions of a function can never run, proved from its own MIPS alone. A prototype: nothing uses it.

One rule covers every class (switch default leaf, division check, round-toward-zero adjust, padding): walk the function from its
entry, tracking what each register can hold, and take a branch's way out only if that way is possible. What the walk never arrives
at can never run. Everything else is reachable as far as this knows.

What it assumes, all listed so they can be reviewed:
  - Registers are 32-bit values sign-extended to 64 (a function with any 64-bit instruction is refused).
  - The way in is the function's first instruction, plus every other way in that `entries` found (other functions' jumps, branches,
    calls and jump tables, code that builds the address, and any word in a memory snapshot equal to the address).
  - A jump table has exactly the entries the translation lists: checked against the bounds test before it.

What it does NOT assume: anything about a caller (registers start unknown), about memory (a load is only as narrow as its width), or
about a called function (after a call every register is unknown, the saved ones too: a port helper can hand a result back in one).

  analyse(f, roots) -> Result(refused=why or None, reached=set of addresses, blocked={address: why the way there is impossible})
"""
import os
import re
from collections import namedtuple
from mips import uses_defs, BRANCH

LOW, HIGH = -0x80000000, 0x7FFFFFFF
TOP = (LOW, HIGH, frozenset())
WIDEN_AFTER = 12
MAX_EXCLUDED = 64
SIXTY_FOUR = {'ld', 'lwu', 'lld', 'dadd', 'daddu', 'daddi', 'daddiu', 'dsub', 'dsubu', 'dsll', 'dsrl', 'dsra', 'dsll32', 'dsrl32', 'dsra32',
              'dsllv', 'dsrlv', 'dsrav', 'dmult', 'dmultu', 'ddiv', 'ddivu', 'dmfc1', 'dmtc1'}
CALLS = {'jal', 'jalr', 'bgezal', 'bltzal'}
SAVED = {'s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp', 'sp'}
REG = re.compile(r'\$(\w+)')
Result = namedtuple('Result', 'refused reached blocked states')


def sext16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def s32(v):
    v &= 0xFFFFFFFF
    return v - 0x100000000 if v & 0x80000000 else v


def tidy(lo, hi, ex):
    """None if nothing is left."""
    while lo <= hi and lo in ex:
        lo += 1
    while lo <= hi and hi in ex:
        hi -= 1
    if lo > hi:
        return None
    ex = frozenset(x for x in ex if lo < x < hi)
    return (lo, hi, ex if len(ex) <= MAX_EXCLUDED else frozenset())


def get(st, r):
    return (0, 0, frozenset()) if r == 'zero' else st.get(r, TOP)


def bound(v):
    return (1 << v.bit_length()) - 1 if v > 0 else 0


def ranged(lo, hi):
    return (lo, hi, frozenset()) if LOW <= lo <= hi <= HIGH else None


# ---- Rule A (proposed 2026-11-10; reviewed, approved by the user 2026-11-10): multiples of a power of two ----
# For a register r, st['#' + r] = k says: r's value is a multiple of 2**k (its low k bits are 0). No entry means k = 0, which
# says nothing. It is kept beside the range, and used in one place only: when a branch narrows r's range, the ends are moved
# inward to the nearest multiples, and a range with no multiple left in it is impossible. A compiled `switch (x & 0xFF00)` is
# what this is for: between the cases 0x1100 and 0x1200 there is no multiple of 0x100, so the "none of these" jump never runs.
# Every proposed rule is OFF until it has been reviewed: with RULE_A, RULE_B, RULE_D and RULE_M all False (as here) this is
# exactly the checker as reviewed (checked on every function of the tester: the same dead instructions, 2026-11-10).
RULE_A = True        # (approved by the user 2026-11-10, after review)
RULE_B = False       # (a word read back from where it was just stored: see `remember_store`)
RULE_D = True        # (approved by the user 2026-11-10, after review)  # (slt / sltu with the constant on the left: see transfer)
RULE_M = True        # (approved by the user 2026-11-10, after review)  # (a product that fits in 32 bits: see transfer)
# ---- Rule S (proposed 2026-11-12 by agent "partial"; approved by the user 2026-11-12 once the two thread stacks below were
#      checked; on): the stack pointer is a multiple of 8 ----
# At the function's start and at every other way in, and again after every call, $sp is a multiple of 8 (st['#sp'] = 3, rule A's
# form; nothing about its range). Why it holds: the MIPS calling rules (o32) keep $sp 8-aligned at every call, and every
# instruction in the game and the mod's copies that writes $sp is `addiu $sp, $sp, K` with K a multiple of 8, but the entry
# point's `lui $sp, 0x800A` (+ -0x220): scratch/agents/partial/sp_writes.py; and the mod's built code (mod.elf) has only such
# `addiu`s too (sp_writes_mips.py). A thread starts on the stack top it is given: five of the game's seven osCreateThread calls
# give a fixed multiple of 16, and the other two were checked in the MIPS (agent "final080", 2026-11-12):
#   - 0x80000C88 (in func_80000A90) gives $fp, which func_80000A90 sets only once, to its second parameter (`addu $fp, $a1,
#     $zero` at 0x80000AB0); its only caller, func_800004D0, passes 0x80059410 (`lui $a1, 0x8006; addiu $a1, $a1, -0x6BF0`
#     at 0x8000054C, 0x80000550): a multiple of 16;
#   - 0x8002B590 (in func_8002B480) gives func_8002B78C(0x2000) + 0x2000. func_8002B78C is libultra's alHeapDBAlloc
#     (func_80033B90) on the heap at 0x8008D4F0 for 1 x 0x2000 bytes: it gives back the heap's current place, or 0 when it is
#     full; that place is set only by alHeapInit (func_80033B50: the base rounded up to 16) and moved only by alHeapDBAlloc
#     (by (count x size + 15) & ~15). Nothing else in the game writes it (0x8008D4F4: one read, in func_8002B7BC), none of
#     these is patched or hooked by a mod, and no mod code calls osCreateThread (0x80037520). So 0 or a multiple of 16,
#     + 0x2000: a multiple of 8 either way.
# After a call only this fact is kept, nothing else: a callee that came
# back with $sp moved would have broken its caller's every frame access, which the tester's own check (the original must give
# the stack pointer back) would show. Used through rule A only, so it needs RULE_A.
RULE_S = True
# ---- Rule C (proposed 2026-11-12 by agent "partial"; NOT approved, held by the user 2026-11-12: off): a callee known to give the saved registers back ----
# After `jal F`, what was known of s0-s7 and fp (their ranges, rule A's low zero bits, links between them) is kept when F is in
# TRUSTED_CALLEES: a game function the mod doesn't patch or hook, with no call through a register, all of whose callees are in
# TRUSTED_CALLEES too (a greatest fixed point over the call graph: trusted_callees below). Every other call still forgets
# everything. What it takes on trust: that such a function keeps the calling rules for s0-s7 and fp -- the game's compiler's
# code does (each saves what it uses and puts it back), and every test of every game function checks its original does so
# (fuzz_main.cpp: the original must leave the registers it has to keep as they were). The port's helpers, which may not, are
# reached only through the mod's patched functions, which are never trusted, nor is any function that can reach one.
RULE_C = False
TRUSTED_CALLEES = frozenset()


def trusted_callees(fs, patched):
    """The functions rule C trusts, by name: see above. fs: to_asm.read_functions(); patched: lift.patched_by_mod()."""
    ok = set()
    for n, f in fs.items():
        if n in patched or n.startswith('modrep_'):
            continue
        if any(mn == 'jalr' or (mn == 'jr' and ops.strip() != '$ra' and a not in f['tables']) for a, (mn, ops) in f['ins'].items()):
            continue
        jals = [a for a, (mn, ops) in f['ins'].items() if mn == 'jal']
        if any(a + 4 not in f['calls'] for a in jals):      # (to_asm keys a call by its delay slot's address)
            continue
        ok.add(n)
    changed = True
    while changed:
        changed = False
        for n in list(ok):
            if any(c not in ok for c in fs[n]['calls'].values()):
                ok.discard(n)
                changed = True
    return frozenset(ok)

# ---- Rules F, N and H (proposed 2026-11-12 by agent "tail"; APPROVED by the user 2026-11-12 ("Do your recommendation"): on) ----
# Rule F: a square is never negative, so the root of a sum of squares is never NaN. st['F' + f] = 'nonneg' says float register f
#   (single precision) holds +0, a positive number or +infinity -- never a NaN, never negative. Made by `mul.s $d, $a, $a` (one
#   register times itself), by `add.s $d, $a, $b` of two such, by `sqrt.s $d, $a` of one, and copied by `mov.s`. What it rests
#   on: an arithmetic instruction with a NaN operand stops the original (the translated code's NAN_CHECK before every one, as on
#   the N64, where a NaN operand raises an exception) -- so where mul.s goes on, its operand was a number or an infinity, and
#   x * x of those is +0, positive or +inf (rounding cannot change a sign; -0 * -0 is +0); the sum of two of those is one too
#   (no -inf to make inf - inf); and the root of one is one too. Then `c.eq.s $x, $x` of such an x is true (only a NaN is unequal
#   to itself): st['Fcc'] = True, which `bc1t` / `bc1f` read. (A square may be written `x * y` with y a copy of x's bits made
#   through an integer register, `mfc1 $s1, $f14; mtc1 $s1, $f18`: st['G' + s1] = 'f14' after the mfc1, st['E' + f18] = 'f14'
#   after the mtc1, each dropped when either register is written. func_80035040.) Every other write to the register drops
#   what was known (forget), any other c.cond or ctc1 drops Fcc, and a call drops everything (after_call). This is the sqrtf idiom
#   `r = sqrt.s(x); if (r != r) r = sqrtf(x)` -- the libm fallback that sets errno -- in func_80034B50, func_80034D1C,
#   func_80035040 and func_800353C0, where x is always a sum of squares.
RULE_F = False       # (approved 2026-11-12; OFF for 0.81: the independent review (scratch/agents/review) found NAN_CHECK is compiled out in Release, so a NaN can reach the fallback F calls dead -- for the user to decide)
# Rule N: `andi $d, $a, K` of a register known to be one value is that value & K; of one known not negative it is at most both
#   a's largest value and K. (Plain arithmetic of the 32-bit values: x & K <= x when x >= 0, x & K <= K.) Before, the checker
#   said 0..K whatever a held (func_8013655C: a flag only ever set to 1, tested with andi 0xFF).
RULE_N = True        # (approved by the user 2026-11-12; on since 0.81.0, final081)
# Rule H: what a division leaves in HI and LO. `div $zero, $x, $y` / `divu` with what is known of x and y then gives, in
#   st['%hi'] / st['%lo'] = (y, range): the remainder's and the quotient's ranges for y != 0. They are used by `mfhi` / `mflo`
#   only while y still holds the same value (forget drops them when y or HI/LO is written) AND y is then known not to be 0
#   there (the compiler's `bne $y, $zero; break 7` check after every division narrows it: a path that went on had y != 0, and
#   y has not changed since the division) -- so the y == 0 case, whose HI and LO the N64 sets otherwise, is never used. Signed:
#   |remainder| < |y| and its sign is x's, |quotient| <= |x| (x = 0x80000000 with y = -1 leaves 0x80000000: covered, as a
#   range that does not fit is not given). Unsigned: remainder < y, quotient <= x / y. (The remainder is not narrowed by x:
#   a loop counter's range grows each turn and the fact would be widened away.)
#   And a link: after `mfhi $r` of a signed division by y known > 0, st['<' + r] = y says -y < r < y, so `slt $d, $r, $y` is 1
#   and `subu $d, $y, $r` is 1 .. 2y - 1 (that one only for y < 2**30: no overflow). Dropped when r or y is written.
#   (func_80009B30: `x % t6` with t6 14 or 24, then t6 - rem - 1 >= 0; func_80134C74: `rand() % n < n`.)
RULE_H = True        # (approved by the user 2026-11-12; on since 0.81.0, final081)


RULE_R = True        # (rule R, proposed 2026-11-12, approved by the user 2026-11-12: the mod's copies checked too, with no other ways in -- lift.py unreach_dead)

# ---- Rule E (proposed 2026-11-12 by agent "partial2"; APPROVED by the user 2026-11-12: on): no other ways into a game function ----
# Rule R's reason holds for the game's own functions too: in the port, code reaches a translated function's middle only from
# that function's own C. lift.py unreach_dead then runs the checker with no other ways in (unreach_roots.json unused):
#   - a call or jump through a register is LOOKUP_FUNC(address) -> get_function (librecomp overlays.cpp), an exact look-up of
#     a function's first address in func_map; any other address stops the game ("Failed to find function", exit);
#   - `jr $ra` is a C `return;` (the address in $ra is never jumped to: no LOOKUP_FUNC(ctx->r31) in RecompiledFuncs), so a
#     return address in memory or a register is no way in either;
#   - a jump table's `switch` has a case for each entry the translator listed, and switch_error for anything else;
#   - every branch, jump, call and jump-table entry of the game that unreach_roots counts as a way into another function lands
#     on one of the source's own instructions too -- the source's own label, a goto in its own C (overlays sharing addresses,
#     a function and its pieces) -- never only in the target's C: 4,484 branches and jumps and 3,351 table entries, none
#     outside (scratch/agents/partial2/e_roots.py, 2026-11-12). No call lands inside another function.
# So a snapshot word, a built address or another function's branch is never a way into the middle of a function's C: the
# lifted C++ that replaces it is entered at its start, as the original is.
RULE_E = True        # (approved by the user 2026-11-12; on since 0.81.0, final081)

# ---- Rule W (proposed 2026-11-12 by agent "loopbounds"; NOT approved, left out by the user 2026-11-12 (no gain): off): an index into a local, bounded by the checker ----
# In rule P's walk (below), an address made by adding a register to a followed address -- `addu $d, $p, $i` (or `subu`) with
# p = ('X', r, k), r's entry value plus k exactly -- is r's entry value plus k plus i. If the checker's own ranges (analyse, run
# on this function from its first instruction with the rules that are on) say i is in [lo, hi] at that instruction on every
# way there, d is r's entry value plus an offset in [k + lo, k + hi]: a window, PRange. Then
#   - a store through it writes only bytes in that window (+ the width): WRITES[r] gets just those, and each of F's frame words
#     there (r = sp) may now hold what was stored or still what it held (both kept: a weak update);
#   - a load through it reads only the words in the window (+ 8 bytes): what any of them may hold;
#   - `addiu` moves the window; a join of two windows (or exact offsets) on the same register is their hull, kept while it is at
#     most P_RANGE_JOIN bytes wide (so a pointer stepped round a loop soon gives up, as before);
#   - everything else sees a PRange as the set of its sources (r, and what the index came from) -- exactly the mixed value
#     rule P had there before -- so no part of rule P is weakened: a window is only ever more precise where it is used.
# Why the ranges may be used: analyse's state at an instruction holds for every run of the function from its first instruction
# (the reviewed checker, with the approved rules); rule P's walk starts there too (rule E); and a window is a fact about the
# address's value, not about where it came from -- a write through it lands in it. The ranges are worked out with rule P keeping
# the saved registers over calls to the functions that are members in that round of the fixed point (p_states: recomputed
# whenever one of the function's callees drops out), so they rest on the same induction on call depth as rule P itself. Offsets
# are kept within +-P_RANGE_MAX so 32-bit wrap-around can't alias a frame word. A delay slot's ranges are its branch's (joined
# with its own state if something also jumps there), and $ra is never read in a call's delay slot.
RULE_W = False
P_RANGE_MAX = 0x100000      # (offsets further than this from the entry value: not a window, the mixed set as before)
P_RANGE_JOIN = 0x40         # (the widest hull a join keeps: wider loses the window, so a pointer stepped in a loop gives up)
P_RANGES = {}               # {(id of the function, which of its calls are to members): analyse states} -- what rule W reads
P_RANGES_FS = None          # (the functions those ids are of, kept alive while the cache is)

# ---- Rule I (proposed 2026-11-12 by agent "loopbounds"; APPROVED by the user 2026-11-12: on): float ranges ----
# st['I' + f] = (kind, lo, hi) says float register f holds, as kind 's' (a single), 'd' (a double, f and f+1) or 'w' (a 32-bit
# integer's bits, from mtc1), a value in [lo, hi] -- never a NaN or an infinity. Made by
#   `mtc1 $r, $f` with r's range (as the word's integer value; a word known exactly is also that single's value, and two known
#   words in a pair are that double's);
#   `cvt.s.w` / `cvt.d.w` / `cvt.s.d` / `cvt.d.s`, `mov`, `neg`, `add`, `sub`, `mul`, `div` (by a range without 0) and
#   `sqrt` (of a range >= 0), .s and .d, on known operands: the interval's corners, worked out in Python's doubles and then
#   widened outward by 2**-20 of each end (singles; 2**-50 for doubles) plus 2**-126 (2**-1000): more than any rounding mode's
#   error (one unit in the last place: 2**-23, 2**-52) and a flushed or denormal result. A result past the largest finite
#   value of its kind gives nothing (it might be an infinity);
#   `trunc.w` / `cvt.w` (`floor`, `ceil`, `round`) of a known single or double wholly inside (-2**31, 2**31): the integer range
#   (truncated or rounded either way), as a 'w'; `mfc1 $r, $f` of a 'w' gives r that range.
# And the condition bit: `c.le`, `c.lt`, `c.eq` (.s, .d) of two known ranges sets st['Fcc'] to True or False when every pair of
# values gives the same answer (rule F's key; False is new: a bc1t can't then be taken, a bc1f can't fall through).
# What is dropped: any other write to f (or to its pair partner, for a double), every call, every other way in; joins keep the hull of
# the same kind. It takes nothing on trust: no NaN ever enters (a range is only made from known numbers by operations on finite
# ranges that stay finite), so it does not rest on NAN_CHECK. (func_800DD5F4: 255 - k * 9.0 with k in 0..26, and 255 - k * q
# with q = 255 / x, both far below 2**31, so the "too big for a signed int" conversions never run.)
RULE_I = True        # (approved by the user 2026-11-12; on since 0.81.0, final081)
# ---- Rule I's words (proposed 2026-11-14, part 1 of the reach job's proposals; re-derived and put
#      behind a switch by rules084. The user approved the extension 2026-11-14; OFF unless LIFT_RULE_I_WORDS=1, and nothing
#      counts from it until an independent review approves it: REVIEW_I_WORDS.txt. APPROVED by reviewr084 2026-11-15;
#      the 0.84 night run sets LIFT_RULE_I_WORDS=1) ----
# A float register holding a word known only as a range wholly inside 0 .. 0x7F7FFFFF (kind 'w': a join of several `mtc1`
# constants, say -- func_801079B4's f10 is 0.0, 128.0 or 255.0) holds, as a single, a value between the singles of the range's
# two ends: those words are +0 .. FLT_MAX, and among them a larger word is a larger single (IEEE 754: sign 0, then the exponent,
# then the fraction, most significant first). No NaN and no infinity is among them (they start at 0x7F800000). A low end that is
# a denormal (a word under 0x00800000) is taken as 0, so a CPU that flushes denormal operands to zero is covered too.
RULE_I_WORDS = os.environ.get('LIFT_RULE_I_WORDS') == '1'
# ---- Rule "break ends the way" (proposed 2026-11-14, part 1 of the reach job; re-derived by rules084. The user approved it
#      2026-11-14; OFF unless LIFT_RULE_BREAK=1, nothing counts from it until an independent review: REVIEW_BREAK.txt. APPROVED by
#      reviewr084 2026-11-15; the 0.84 night run sets LIFT_RULE_BREAK=1) ----
# No run goes on from a `break` to the instruction after it: the translated original calls do_break there, which in the port
# ends the game and in the tester stops the test (fuzz_main.cpp do_break: Stopped, or TooLong outside a run). So the walk does
# not carry the state past a break that is not in a delay slot (a delay slot's break goes through its branch, as before).
# What it gives: after the compiler's `bne $y, $zero; break 7`, y is not 0 on every way on, so a later check of the same y can
# never break.
RULE_BREAK = os.environ.get('LIFT_RULE_BREAK') == '1'
# ---- Rules K and O (2026-11-13, scratch/agents/modnative5: the user's item (7) of 2026-11-13, "andi/sltiu ranges and the mod's
# read-only image as known memory -- implement, list for an independent review; don't count lines from them until reviewed").
# PROPOSED, NOT REVIEWED: both False here, so this is exactly the checker as reviewed. (Not "repeated loads": rule B stays off.)
#   K: slti / sltiu whose operand's range settles the comparison -- every value of it below the constant, or none -- gives 1 or
#      0 rather than "0 or 1". (andi's range, [0, K] and rule N's narrowing, is already here; K is what lets a compare after an
#      andi know its answer: `andi $t, $x, 7` then `sltiu $at, $t, 8` is always 1.) Signed for slti, unsigned for sltiu: a
#      range with negative values is only settled for sltiu when it is all negative (all >= 2^31 unsigned).
#      And andi: a mask of low ones (0xFF, 0xFFFF, ...) over a range already inside it keeps the range, its low end too (rule N
#      kept only its top: `andi $a0, $v0, 0xFFFF` after a load of 2 to 9 is 2 to 9, not 0 to 9).
#   O: a load whose every possible address (its base register's range plus its offset, at most 0x4000 bytes) is inside the mod's
#      read-only image -- its constants, as built (MOD_IMAGE, given by lift.py: start, the bytes, and the offsets of the words
#      the loader relocates, which stay unknown) -- reads one of the values there: their lowest to highest. The image is what
#      the game has there (the tester loads the same bytes: fuzz_mod_image); nothing writes it (the mod's .rodata).
RULE_K = False
RULE_O = False
MOD_IMAGE = None     # (rule O: (start address as linked, bytes, set of byte offsets of relocated words))

# ---- Rule P (proposed 2026-11-12 by agent "partial2"; APPROVED by the user 2026-11-12, part 1: on -- parts 2 and 3, P_COPIES and P_NATIVE, stay off): a callee PROVED to give s0-s7, fp and sp back ----
# Rule C's place, with a proof from the callee's own MIPS in place of tests. After `jal F`, what was known of s0-s7, fp and sp
# (ranges, rule A's low zero bits, links between them) is kept when every function that call can run is in PRESERVING.
#
# preserves(F) walks F's MIPS from its first instruction along every way through it (both ways of every branch: nothing is
# pruned), keeping for each register and for each word of F's stack frame where its value may have come from:
#   ('X', r, k)  exactly what register r held when F was entered, plus k ($sp's entry value + k is a frame address);
#   a set of registers: something worked out from what those registers held at entry ('*' in it: from anything at all);
#   None: nothing a register held at entry (a constant, or a word loaded through a pointer that is not a frame address).
# A frame word nothing in this run of F has written yet is '*' (stale: it may hold anything an earlier call left there).
# F passes when, on every way out of it:
#   - `jr $ra`: $ra is ('X', ra, 0) (read before the delay slot, as the CPU does); after the delay slot $sp is ('X', sp, 0)
#     and each of s0-s7 and fp is ('X', itself, 0) -- untouched, or loaded back from a frame word still holding its entry value.
#     So a save is a store of the entry value to a frame word, a restore a load from that word;
#   - a jump or branch out of F (a tail call, or falling into the next function): the same, $ra included, and the function(s)
#     jumped to are in PRESERVING;
#   - every `jal`: $sp is known, and every function the call can run is in PRESERVING (the translation's named callee, or, for a
#     call it looks up by address, every function of any overlay that starts there); after it s0-s7, fp and $sp are as before;
#   - nothing F can't be proved for: no `jalr`, `bgezal`/`bltzal`, `jr` with no table, `$sp` written by anything but
#     `addiu $sp, $sp, k`, `$sp` above its entry value.
# The aliasing question -- what may write a frame word between the save and the restore -- is answered by following every
# frame address, as the values above do, and never assuming where an unknown pointer points:
#   - F's own stores: through ('X', sp, k) the word is known exactly (any width, swl/swr too); through any other ('X', r, k)
#     the bytes written are noted in F's summary, WRITES[r] (offsets from r's entry value: "F writes there through r");
#     through a set of registers, each of them gets WRITES[r] = everything ('any'), and if $sp or '*' is among them every frame
#     word is dropped and the frame counts as written anywhere -- F fails;
#   - a store of a value worked out from a register's entry value to memory that is not F's own frame lets that address out
#     where nothing follows it: that register's WRITES become 'any' (for $sp: F fails). Into F's own frame (below its entry
#     $sp) it is followed as the word's value -- unless F lends its frame (below), when it counts as let out too -- and when
#     the word is given up ($sp moves up past it: F's frame released) it is let out then: the next frame there may read it
#     as a local nobody set. Into its caller's frame (above its entry $sp: a home word) it is let out at once;
#   - a callee's: it runs below F's $sp, so F's words below $sp are dropped at every call (and whenever $sp moves up); what it
#     writes through each register it was given (its WRITES, with $sp's from its entry $sp, offsets >= 0) is mapped onto what
#     F gave it: through ('X', r, k), F's WRITES[r] at those offsets plus k, F's frame words there (for r = sp) set to None (a
#     callee stores no followed value outside its frame: that would have made its WRITES 'any'); through a set, 'any'.
#     What it hands back in other registers (RETURNS: which of its entry registers each may come from) is mapped the same way;
#   - a word above F's entry $sp that F did not store (a stack argument) is ('X', 'm<offset>', 0), followed like a register:
#     a caller maps it through its own frame word there;
#   - F lends its frame when a frame address is in any register but $sp, or in a frame word at or above $sp (a callee can
#     read those as stack arguments), at a call or a way out. Then every followed value F stores in its frame counts as let
#     out (a callee holding the frame's address can read it back through it, which nothing follows).
# So a frame word holding a saved register can only change by a store this follows. What is taken on trust (to be
# confirmed by whoever reviews this):
#   - no store goes through an address that none of these functions made from a register (a wild, dangling or
#     out-of-bounds pointer into a live stack frame), and no other thread or DMA writes a thread's stack. (The translated code
#     keeps frames in N64 memory as the original does; its `jr $ra` is a C return, so only s0-s7 and fp depend on this, not
#     where a call comes back to: rule E.) The tester shows how such a write looks: fuzz_main.cpp's "frame was overwritten"
#     notes (a random loop count over a local array) -- inputs no game state gives;
#   - a word loaded through a pointer that is not a frame address is None: no address these functions made from a register
#     is ever in such memory, nor left behind in a released frame, because every such store is followed (above). The caller
#     that uses rule P is not one of them, but its frame does not matter: its own s0-s7 and fp are in registers over the call;
#   - in the port, the function a call runs is the MIPS this reads (the named callee; for a looked-up call, the function
#     starting at that address). Never in PRESERVING, nor anything that can reach one: the mod's patched and hooked functions
#     (lift.patched_by_mod) but for part 2, the port's rw_ helpers (some hand back a saved register on purpose), functions with
#     no MIPS (the runtime's osRecvMesg_recomp and the like, the port's recomp_* exports) but for part 3. A converted function
#     the port runs in an original's place keeps s0-s7, fp and sp too: the tester checks it (fuzz_main.cpp kept_registers_same
#     on the converted run), and it keeps the original's values in C++ variables, never in the context;
#   - recursion: PRESERVING is the largest set whose members all pass given the others, with WRITES and RETURNS the smallest
#     that hold together; sound for every call that comes back, by induction on how deep its calls go.
# A test cannot show anything this proves false: the tester's stand-ins for callees never change a kept register (fuzz_main.cpp
# CALL_REG), so every line this calls dead was already out of every test's reach.
# (merge084b, 2026-11-15: rules084's fixed form of rule P -- LIFT_RULE_P, P_USER_ENTRY_STRICT, P_LENT_STALE, p_compared,
#  decides and the 11-13 fixes -- was REJECTED by the independent review (scratch/agents/reviewr084) and dropped by the user
#  for good 2026-11-15: not merged. Rule P stays off, as in 0.83.)
RULE_P = False       # (approved 2026-11-12; OFF for 0.81: the independent review (scratch/agents/review, cex_new.py) found 4 counter-examples -- swl/swr partial words, a frame address through a float register, a tail call, an address below sp -- fix and re-review first)
P_LEND = True               # (False: the strict form -- a frame address may never leave $sp at all; proves fewer)
# (Measurement only, NOT proposed as sound: P_INBOUNDS takes every store through a frame address it can't place to stay inside
#  the local it was made for, so a save slot -- the compiler's own word, no local's -- is written only by its owner's exact
#  stores. That is the assumption "the game never overruns a stack buffer", which the MIPS alone can't show.)
P_INBOUNDS = False
PRESERVING = frozenset()
P_SUMMARY = {}              # {member: (WRITES, RETURNS)}: see above
P_ENTRIES = {}              # {address: [the functions (any overlay) whose first instruction is there]}
# (Part 2 of the proposal, separable: the mod's copies. A game function one mod -- and only one -- replaces, and no mod hooks,
#  runs as that mod's copy, modrep_<name>: the very MIPS rule R checks (lift.py unreach_dead). With P_COPIES = {game name:
#  copy}, such a call is proved through the copy, and the copies and the mod's other functions (modfn_<address>, read from
#  modrep.analysis_sources) are checked as any function is. Empty: patched functions are never proved, as in part 1.)
P_COPIES = {}
# (Part 3, separable: functions with no MIPS -- the runtime's os*_recomp, the port's recomp_* exports. P_NATIVE = {name:
#  reads stack arguments?}, each checked in its C++ (p_native.py): it writes no register of the context but v0, v1 and float
#  ones, and hands the context to nothing that could. Its summary is the worst it could do otherwise: anything written
#  through a0-a3 (and through $sp's caller frame if it reads stack arguments), v0 and v1 worked out from a0-a3.)
P_NATIVE = {}
P_KEPT = ('s0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 'fp')
P_GPR = ('at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7', 't8', 't9', 's0', 's1', 's2',
         's3', 's4', 's5', 's6', 's7', 'k0', 'k1', 'gp', 'sp', 'fp', 'ra', 'hi', 'lo')
P_STORE_WIDTH = {'sb': 1, 'sh': 2, 'sw': 4, 'swl': 4, 'swr': 4, 'swc1': 4, 'sc': 4, 'sd': 8, 'sdl': 8, 'sdr': 8, 'sdc1': 8, 'scd': 8}
P_LOADS = {'lb', 'lbu', 'lh', 'lhu', 'lw', 'lwl', 'lwr', 'll', 'ld', 'ldl', 'ldr', 'lwc1', 'ldc1'}
P_STALE = frozenset(['*'])
P_ANY = 'any'
P_MAX_WRITES = 512          # (more bytes than this through one register: counted as 'any')


def p_entries(fs):
    """{address: names of the functions (of any overlay) that start there}; the mod's copies are not among them."""
    out = {}
    for n, f in fs.items():
        if f['ins'] and not n.startswith('modrep_'):
            out.setdefault(min(f['ins']), []).append(n)
    return out


def call_targets(f, pc, entries):
    """The functions the `jal` at pc can run, or None if they can't all be named. (to_asm keys a call by its delay slot.)"""
    name = f['calls'].get(pc + 4) or f['calls'].get(pc)
    if name:
        return [P_COPIES.get(name, name)]
    t = target(f['ins'][pc][1])
    return [P_COPIES.get(x, x) for x in entries.get(t, [])] or None


def jump_targets(t, entries):
    """The functions a jump out of a function to t enters, or None: it must be some function's first instruction."""
    return [P_COPIES.get(x, x) for x in entries.get(t, [])] or None


class PRange:
    """Rule W: register `base`'s entry value plus an offset in [lo, hi] (bytes), worked out also from `extra` (the index's
    sources). Anything that doesn't know this form sees the set of its sources -- iterating it, `in`, p_sources: the mixed value
    rule P had before -- so it can only ever be as precise as rule P was, or more."""
    __slots__ = ('base', 'lo', 'hi', 'extra')

    def __init__(self, base, lo, hi, extra=frozenset()):
        self.base, self.lo, self.hi, self.extra = base, lo, hi, frozenset(extra)

    def sources(self):
        return frozenset([self.base]) | self.extra

    def __iter__(self):
        return iter(self.sources())

    def __contains__(self, x):
        return x in self.sources()

    def __len__(self):
        return len(self.sources())

    def __eq__(self, o):
        return isinstance(o, PRange) and (self.base, self.lo, self.hi, self.extra) == (o.base, o.lo, o.hi, o.extra)

    def __hash__(self):
        return hash((self.base, self.lo, self.hi, self.extra))

    def __repr__(self):
        return 'PRange(%s%+d..%+d%s)' % (self.base, self.lo, self.hi, (' +' + ','.join(sorted(self.extra))) if self.extra else '')


def p_window(base, lo, hi, extra=frozenset()):
    """Rule W: a followed address -- ('X', base, lo) when exact, a PRange when a window -- or None when the window is too far
    out (then the caller falls back to the mixed set)."""
    if not (-P_RANGE_MAX <= lo <= hi <= P_RANGE_MAX):
        return None
    if lo == hi and not extra:
        return ('X', base, lo)
    return PRange(base, lo, hi, extra)


def p_offsets(v):
    """(base, lo, hi, extra) of an exact followed address or a window; None for anything else."""
    if isinstance(v, tuple):
        return v[1], v[2], v[2], frozenset()
    if isinstance(v, PRange):
        return v.base, v.lo, v.hi, v.extra
    return None


def p_sources(v):
    """The entry registers a value may come from."""
    if v is None:
        return frozenset()
    if isinstance(v, PRange):
        return v.sources()
    return frozenset([v[1]]) if isinstance(v, tuple) else v


def p_mixed(srcs):
    srcs = frozenset(srcs)
    return P_STALE if '*' in srcs else (srcs or None)


def p_join_value(a, b):
    if a == b:
        return a
    if RULE_W:
        # (rule W: two followed addresses on the same register join to the window holding both, while it is narrow)
        x, y = p_offsets(a), p_offsets(b)
        if x and y and x[0] == y[0] and max(x[2], y[2]) - min(x[1], y[1]) <= P_RANGE_JOIN:
            w = p_window(x[0], min(x[1], y[1]), max(x[2], y[2]), x[3] | y[3])
            if w is not None:
                return w
    return p_mixed(p_sources(a) | p_sources(b))


def p_word(slots, o):
    """What F's frame word at offset o (from F's entry $sp) holds: what F stored there; above its entry $sp, if F stored
    nothing there, what the caller left there as F came in (a stack argument): ('X', 'm<o>', 0), followed like a register;
    below it, if F stored nothing there, '*' (stale)."""
    if o in slots:
        return slots[o]
    return ('X', 'm%d' % o, 0) if o >= 0 else P_STALE


def p_clear_frame(slots):
    """A store went somewhere in the frame not known: every word is dropped -- but for P_INBOUNDS (measurement only), where a
    word holding a saved register's entry value (a save slot, its own object) is kept."""
    for o in list(slots):
        v = slots[o]
        if not (P_INBOUNDS and isinstance(v, tuple) and v[2] == 0 and v[1] in P_KEPT + ("ra",)):
            del slots[o]


def p_native_summary(reads_stack):
    writes = {r: P_ANY for r in ('a0', 'a1', 'a2', 'a3')}
    if reads_stack:
        writes['sp'] = P_ANY
    returns = {y: frozenset([y]) for y in P_GPR if y not in P_KEPT and y not in ('sp', 'ra')}
    returns['v0'] = returns['v1'] = frozenset(['a0', 'a1', 'a2', 'a3'] + (['*'] if reads_stack else []))
    return writes, returns


def p_states(f, members, entries):
    """Rule W: analyse's states for f run from its first instruction, with rule P keeping the saved registers over calls to
    `members` (the fixed point's current set: the ranges depend only on which of f's calls are to members, so that is the key).
    None if the checker refuses f."""
    flags = tuple(bool(g) and all(x in members for x in g) for g in
                  (call_targets(f, pc, entries) for pc in sorted(f['ins']) if f['ins'][pc][0] == 'jal'))
    key = (id(f), flags)
    if key not in P_RANGES:
        global RULE_P, PRESERVING, P_ENTRIES
        saved = RULE_P, PRESERVING, P_ENTRIES
        RULE_P, PRESERVING, P_ENTRIES = True, members, entries
        try:
            res = analyse(f, ())
        finally:
            RULE_P, PRESERVING, P_ENTRIES = saved
        P_RANGES[key] = None if res.refused else res.states
    return P_RANGES[key]


class _PRun:
    """One walk of one function for rule P: see the comment above."""

    def __init__(self, f, members, entries, lend):
        self.f, self.members, self.entries, self.lend = f, members, entries, lend
        self.writes, self.returns, self.problems, self.lent = {}, {}, [], False
        self.at, self.regs_now = None, {}

    def wrote(self, r, offsets):
        """r's entry value + each offset was written (offsets: a set, or P_ANY)."""
        if r == '*':
            for x in P_GPR:
                self.wrote(x, P_ANY)
            return
        old = self.writes.get(r, frozenset())
        if old == P_ANY:
            return
        if offsets == P_ANY:
            self.writes[r] = P_ANY
            return
        if r == 'sp':
            offsets = {o for o in offsets if o >= 0}        # (below its entry $sp: its own frame, nobody else's)
        new = old | frozenset(offsets)
        self.writes[r] = P_ANY if len(new) > P_MAX_WRITES else new

    def let_out(self, v):
        for r in p_sources(v):
            self.wrote(r, P_ANY)

    def kill(self, slots, lo, hi, to):
        """The frame words with a byte in lo..hi-1 now hold `to` (None) or are dropped (to = P_STALE: below $sp, given up --
        a followed value left there is let out: whoever's frame is there next may read it, as a local nobody set)."""
        for o in [o for o in slots if o < hi and o + 4 > lo]:
            if to == P_STALE:
                self.let_out(slots[o])
                del slots[o]
            else:
                slots[o] = to

    def transfer(self, mn, ops, regs, slots):
        r = REG.findall(ops)
        uses, defs = uses_defs(mn, ops)
        regs, slots = dict(regs), dict(slots)
        val = lambda x: None if x == 'zero' else regs.get(x)
        m = PLACE.match(ops.strip())
        if mn in P_STORE_WIDTH and m:
            vr = m.group(1)
            v = val(vr) if vr in P_GPR else None
            base = val(m.group(3))
            off = sext16(int(m.group(2), 0))
            w = P_STORE_WIDTH[mn]
            win = p_offsets(base) if RULE_W and isinstance(base, PRange) else None
            if win:
                # (rule W: the bytes this can write are those of the window, from its lowest address to its highest + width)
                lo, hi = base.lo + off, base.hi + off
                lo, hi = (lo & ~3, (hi & ~3) + 4) if mn in ('swl', 'swr') else (lo, hi + w)
                if hi - lo > P_MAX_WRITES:
                    win = None
            if win:
                self.wrote(base.base, set(range(lo, hi)))
                if base.base == 'sp':
                    for o in range(lo & ~3, hi, 4):
                        # (each word in reach may now hold what was stored, or still what it held: a float is no followed value)
                        slots[o] = p_mixed(p_sources(p_word(slots, o)) | p_sources(v if mn in ('sw', 'swl', 'swr', 'sb', 'sh', 'sc') else None))
                        if slots[o] == P_STALE:
                            del slots[o]
                    if (self.lend or hi > 0) and p_sources(v):
                        self.let_out(v)
                else:
                    self.let_out(v)
            elif isinstance(base, tuple):
                a = base[2] + off
                lo, hi = (a & ~3, (a & ~3) + 4) if mn in ('swl', 'swr') else (a, a + w)
                self.wrote(base[1], set(range(lo, hi)))
                if base[1] == 'sp':
                    for o in range(lo & ~3, hi, 4):
                        if lo <= o and o + 4 <= hi:         # (the whole word: what was stored; a float is no followed value)
                            slots[o] = v if mn == 'sw' else p_mixed(p_sources(v))
                        elif o in slots or o >= 0:          # (a part of it: what was there, mixed with what was stored)
                            slots[o] = p_mixed(p_sources(p_word(slots, o)) | p_sources(v))
                        if slots.get(o, 0) == P_STALE:
                            del slots[o]
                    if (self.lend or hi > 0) and p_sources(v):
                        self.let_out(v)             # (lent frame, or the caller's frame above F's: see above)
                else:
                    self.let_out(v)
            else:
                for s in p_sources(base):
                    self.wrote(s, P_ANY)
                if base is not None and ('sp' in base or '*' in base):
                    p_clear_frame(slots)
                self.let_out(v)
            return regs, slots
        if mn in ('mtc1', 'dmtc1', 'ctc1'):
            self.let_out(val(r[0]) if r else None)        # (a value in a float register is not followed)
            return regs, slots
        if mn in P_LOADS and m:
            d = m.group(1)
            if d not in P_GPR:
                return regs, slots
            base = val(m.group(3))
            off = sext16(int(m.group(2), 0))
            win = RULE_W and isinstance(base, PRange) and (base.hi - base.lo) + 8 <= P_MAX_WRITES
            if win and base.base == 'sp':
                # (rule W: what any word the window reaches may hold)
                v = p_mixed(set().union(*[p_sources(p_word(slots, o)) for o in range((base.lo + off) & ~3, base.hi + off + 8, 4)]))
            elif win and not ('sp' in base.extra or '*' in base.extra):
                v = None                                    # (through a window on another register: not F's frame, as for ('X', r, k))
            elif isinstance(base, tuple) and base[1] == 'sp':
                a = base[2] + off
                if mn == 'lw' and a % 4 == 0:
                    v = p_word(slots, a)
                else:
                    v = p_mixed(set().union(*[p_sources(p_word(slots, o)) for o in range(a & ~3, a + 4, 4)]))
            elif base is not None and not isinstance(base, tuple) and ('sp' in base or '*' in base):
                v = P_STALE                                 # (a frame, somewhere: anything)
            else:
                v = None
            if mn in ('lwl', 'lwr', 'ldl', 'ldr'):
                v = p_mixed(p_sources(v) | p_sources(val(d)))
            regs[d] = v
            return regs, slots
        if 'sp' in defs:
            sp = regs.get('sp')
            k = re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ops)
            if mn in ('addiu', 'addi') and r[:2] == ['sp', 'sp'] and isinstance(sp, tuple) and sp[1] == 'sp' and k:
                c = sp[2] + sext16(int(k.group(1), 0))
                if c > 0:
                    self.problems.append('$sp above its entry value (%s %s)' % (mn, ops))
                regs['sp'] = ('X', 'sp', c)
                self.kill(slots, -0x80000000, c, P_STALE)     # (below $sp: anyone's)
            else:
                self.problems.append('$sp written by %s %s' % (mn, ops))
                regs['sp'] = P_STALE
            return regs, slots
        int_defs = [d for d in defs if d in P_GPR]
        if not int_defs:
            return regs, slots
        k = re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ops)
        if mn in ('addiu', 'addi') and len(r) == 2 and k:
            x = val(r[1])
            v = ('X', x[1], x[2] + sext16(int(k.group(1), 0))) if isinstance(x, tuple) else x
            if isinstance(x, PRange):       # (rule W: the window moves with it)
                n = sext16(int(k.group(1), 0))
                v = p_window(x.base, x.lo + n, x.hi + n, x.extra) or p_mixed(x.sources())
        elif RULE_W and mn in ('addu', 'add', 'subu', 'sub') and len(r) == 3 and 'zero' not in r[1:] and \
                (self.window(r[1], r[2], mn in ('subu', 'sub')) or (mn in ('addu', 'add') and self.window(r[2], r[1], False))):
            v = self.window(r[1], r[2], mn in ('subu', 'sub')) or self.window(r[2], r[1], False)
        elif mn in ('addu', 'or', 'daddu', 'add') and len(r) == 3 and 'zero' in r[1:]:
            v = val(r[2] if r[1] == 'zero' else r[1])
        elif mn == 'move' and len(r) == 2:
            v = val(r[1])
        else:
            v = p_mixed(set().union(*[p_sources(val(u)) for u in uses if u in P_GPR] or [frozenset()]))
        for d in int_defs:
            regs[d] = v
        return regs, slots

    def window(self, p, i, minus):
        """Rule W: p + i (or p - i) as a followed address with a window, when p holds a followed address and the checker's
        ranges bound i here; None otherwise."""
        x = p_offsets(self.regs_now.get(p)) if p != 'zero' else None
        if x is None:
            return None
        rng = self.index_range(i)
        if rng is None:
            return None
        lo, hi = (-rng[1], -rng[0]) if minus else rng
        return p_window(x[0], x[1] + lo, x[2] + hi, x[3] | p_sources(self.regs_now.get(i)))

    def index_range(self, reg):
        """Rule W: [lo, hi] that register reg surely holds as the instruction at self.at starts, on every run of f from its
        first instruction (analyse's states; a delay slot's are its branch's, joined with its own if something jumps to it);
        None if not known."""
        if reg == 'zero':
            return (0, 0)
        states = p_states(self.f, self.members, self.entries)
        if not states:
            return None
        a, ins = self.at, self.f['ins']
        prev = ins.get(a - 4, ('', ''))[0]
        sts = []
        if prev in BRANCH or prev in ('j', 'b', 'jr', 'jal', 'jalr'):
            if reg == 'ra' and prev in ('jal', 'jalr', 'bgezal', 'bltzal'):
                return None
            if a - 4 in states:
                sts.append(states[a - 4])
        if a in states:
            sts.append(states[a])
        if not sts:
            return None
        vs = [get(st, reg) for st in sts]
        lo, hi = min(v[0] for v in vs), max(v[1] for v in vs)
        if (lo, hi) == (LOW, HIGH) or hi - lo > P_RANGE_MAX:
            return None
        return lo, hi

    def transfer_at(self, a, mn, ops, regs, slots):
        self.at, self.regs_now = a, regs
        return self.transfer(mn, ops, regs, slots)

    def check_lending(self, regs, slots=None):
        """A frame address where a callee or caller can get it: in a register but $sp, or in a frame word at or above $sp
        (a callee can read those as stack arguments)."""
        if any('sp' in p_sources(v) for k, v in regs.items() if k != 'sp'):
            self.lent = True
        sp = regs.get('sp')
        if slots and isinstance(sp, tuple) and any('sp' in p_sources(v) for o, v in slots.items() if o >= sp[2]):
            self.lent = True

    def apply_callees(self, gs, regs, slots):
        """What callees gs do to F's frame and summary, called with F's registers `regs`; returns what each register then may
        come from (all but s0-s7, fp, sp, ra)."""
        c = regs['sp'][2]
        before = dict(slots)
        val = lambda x: p_word(before, c + int(x[1:])) if x[0] == 'm' else regs.get(x)
        out = {}
        for g in gs:
            writes, returns = P_SUMMARY.get(g, ({}, {}))
            for x, offs in writes.items():
                v = val(x)
                if isinstance(v, tuple):
                    shifted = P_ANY if offs == P_ANY else {v[2] + o for o in offs}
                    self.wrote(v[1], shifted)
                    if v[1] == 'sp':
                        if shifted == P_ANY:
                            p_clear_frame(slots)
                        else:
                            for b in shifted:
                                self.kill(slots, b, b + 1, None)
                elif v is not None:
                    for s in v:
                        self.wrote(s, P_ANY)
                    if 'sp' in v or '*' in v:
                        p_clear_frame(slots)
            for y, srcs in returns.items():
                got = set()
                for x in srcs:
                    got |= {'*'} if x == '*' else p_sources(val(x))
                out[y] = p_mixed(got | p_sources(out.get(y)))
        return out

    def members_ok(self, gs, how):
        if not gs:
            self.problems.append('%s an address no function starts at' % how)
            return False
        bad = [g for g in gs if g not in self.members]
        if bad:
            self.problems.append('%s %s' % (how, ','.join(bad)))
            return False
        return True

    def as_entered(self, regs, with_ra):
        if regs.get('sp') != ('X', 'sp', 0):
            return 'sp'
        for x in P_KEPT + (('ra',) if with_ra else ()):
            if regs.get(x) != ('X', x, 0):
                return x
        return None

    def note_return(self, regs):
        self.check_lending(regs)
        for y in P_GPR:
            if y not in P_KEPT and y not in ('sp', 'ra'):
                self.returns[y] = self.returns.get(y, frozenset()) | p_sources(regs.get(y))

    def leave(self, t, st, how):
        """A way out of f to t that is not a return: a tail call into a member."""
        regs, slots = st
        bad = self.as_entered(regs, True)
        if bad:
            self.problems.append('%s to %08X with %s not as it came in' % (how, t, bad))
            return
        gs = jump_targets(t, self.entries)
        if not self.members_ok(gs, '%s to %08X: not proved:' % (how, t)):
            return
        self.check_lending(regs, slots)
        back = self.apply_callees(gs, regs, dict(slots))
        for y, v in back.items():
            self.returns[y] = self.returns.get(y, frozenset()) | p_sources(v)

    def run(self):
        f, ins = self.f, self.f['ins']
        if not ins:
            return 'no instructions'
        entry = {x: ('X', x, 0) for x in P_GPR}
        start = min(ins)
        state = {start: (entry, {})}
        work = [start]

        def arrive(pc, st):
            old = state.get(pc)
            if old is None:
                state[pc] = st
                work.append(pc)
                return
            regs = {k: p_join_value(old[0].get(k), st[0].get(k)) for k in set(old[0]) | set(st[0])}
            regs = {k: v for k, v in regs.items() if v is not None}
            slots = {k: p_join_value(old[1][k], st[1][k]) for k in set(old[1]) & set(st[1])}
            slots = {k: v for k, v in slots.items() if v != P_STALE}
            if (regs, slots) != (old[0], old[1]):
                state[pc] = (regs, slots)
                work.append(pc)

        def go(pc, st):
            if pc in ins:
                arrive(pc, st)
            else:
                self.leave(pc, st, 'falls')

        while work and not self.problems:
            pc = work.pop()
            regs, slots = state[pc]
            mn, ops = ins[pc]
            ds = pc + 4
            jumps = mn in BRANCH or mn in ('j', 'b', 'jr', 'jal', 'jalr')
            if jumps and ds not in ins:
                return 'a jump with no instruction after it (%08X)' % pc
            dmn, dops = ins[ds] if jumps else (None, None)
            if mn in ('jalr', 'bgezal', 'bltzal', 'bal', 'syscall', 'eret'):
                self.problems.append('%s at %08X' % (mn, pc))
                break
            if mn in BRANCH or mn in ('j', 'b'):
                after = self.transfer_at(ds, dmn, dops, regs, slots)
                t = target(ops)
                if t is None:
                    self.problems.append('a jump with no target (%08X)' % pc)
                    break
                if t in ins:
                    arrive(t, after)
                else:
                    self.leave(t, after, 'a jump')
                if mn in BRANCH:
                    go(ds + 4, (regs, slots) if mn.endswith('l') else after)
            elif mn == 'jr':
                if ops.strip() == '$ra':
                    if regs.get('ra') != ('X', 'ra', 0):
                        self.problems.append('returns at %08X through a $ra not as it came in' % pc)
                        break
                    after = self.transfer_at(ds, dmn, dops, regs, slots)
                    bad = self.as_entered(after[0], False)
                    if bad:
                        self.problems.append('returns at %08X with %s not as it came in' % (pc, bad))
                    self.note_return(after[0])
                elif pc in f['tables']:
                    after = self.transfer_at(ds, dmn, dops, regs, slots)
                    for t in sorted(set(f['tables'][pc].values())):
                        if t in ins:
                            arrive(t, after)
                        else:
                            self.problems.append('a jump table entry out of the function (%08X)' % t)
                else:
                    self.problems.append('a jump with no table (%08X)' % pc)
            elif mn == 'jal':
                regs1 = dict(regs)
                regs1['ra'] = None                          # (jal writes $ra before its delay slot runs)
                regs1, slots1 = self.transfer_at(ds, dmn, dops, regs1, slots)
                gs = call_targets(f, pc, self.entries)
                if not self.members_ok(gs, 'calls'):
                    self.problems[-1] += ' at %08X' % pc
                    break
                sp = regs1.get('sp')
                if not (isinstance(sp, tuple) and sp[1] == 'sp'):
                    self.problems.append('a call with $sp unknown (%08X)' % pc)
                    break
                self.check_lending(regs1, slots1)
                slots1 = dict(slots1)
                self.kill(slots1, -0x80000000, sp[2], P_STALE)     # (the callee's frame is below $sp)
                back = self.apply_callees(gs, regs1, slots1)
                kept = {x: regs1[x] for x in P_KEPT + ('sp',) if regs1.get(x) is not None}
                kept.update({y: v for y, v in back.items() if v is not None})
                go(ds + 4, (kept, slots1))
            else:
                go(pc + 4, self.transfer_at(pc, mn, ops, regs, slots))
        if not self.problems and self.writes.get('sp') == P_ANY and not P_INBOUNDS:
            self.problems.append('its frame may be written through an address not followed')
        if not self.problems and self.lent and not P_LEND:
            self.problems.append('lends its frame (strict form)')
        return self.problems[0] if self.problems else None


def preserves(f, members, entries, lend=False):
    """Rule P's check of one function: (why not or None, WRITES, RETURNS, lends its frame?), given that the functions in
    `members` pass, with their P_SUMMARY."""
    run = _PRun(f, members, entries, lend)
    why = run.run()
    return why, run.writes, run.returns, run.lent


def preserving_functions(fs, patched, why=None):
    """Rule P's PRESERVING, P_SUMMARY and P_ENTRIES: the largest set of functions that each pass given the rest, with the
    smallest summaries that hold together. fs: to_asm.read_functions(); patched: lift.patched_by_mod(). why, a dict, gets
    each refused function's first reason. Sets P_SUMMARY as it goes."""
    global P_SUMMARY, P_RANGES, P_RANGES_FS
    P_RANGES, P_RANGES_FS = {}, fs         # (rule W's ranges are keyed by the function's id: fs is kept alive with them)
    entries = p_entries(fs)
    members = {n for n, f in fs.items() if f['ins'] and n not in patched and not n.startswith(('modrep_', 'rw_', 'modfn_'))}
    if P_COPIES:        # (part 2: the copies that run in place of patched functions, and the mod's other functions)
        members |= {n for n, f in fs.items() if f['ins'] and (n in set(P_COPIES.values()) or n.startswith('modfn_'))}
    natives = {n for n in P_NATIVE if n not in fs}
    P_SUMMARY = {n: p_native_summary(P_NATIVE[n]) for n in natives}
    P_SUMMARY.update({n: ({}, {}) for n in members})
    lend = {n: False for n in members}
    while True:
        failed, changed = {}, False
        everyone = members | natives
        new = {}
        for n in sorted(members):
            bad, writes, returns, lent = preserves(fs[n], everyone, entries, lend[n])
            if bad:
                failed[n] = bad
                continue
            if lent and not lend[n]:
                lend[n], changed = True, True
            old_w, old_r = P_SUMMARY[n]
            w = {r: (P_ANY if P_ANY in (old_w.get(r), writes.get(r)) else old_w.get(r, frozenset()) | writes.get(r, frozenset()))
                 for r in set(old_w) | set(writes)}
            rt = {y: old_r.get(y, frozenset()) | returns.get(y, frozenset()) for y in set(old_r) | set(returns)}
            if (w, rt) != (old_w, old_r):
                new[n] = (w, rt)
                changed = True
        if why is not None:
            why.update(failed)
        members -= set(failed)
        P_SUMMARY.update(new)
        for n in failed:
            P_SUMMARY.pop(n, None)
        if not failed and not changed:
            break
    return frozenset(members | natives), P_SUMMARY, entries


def ctz(v):
    """How many low bits of the 32-bit value v are 0 (32 for 0)."""
    v &= 0xFFFFFFFF
    return 32 if v == 0 else (v & -v).bit_length() - 1


def low_zeros(st, r):
    """k such that r is surely a multiple of 2**k: what was noted, or what its one possible value shows."""
    if r == 'zero':
        return 32
    a = st.get(r, TOP)
    k = st.get('#' + r, 0)
    return max(k, ctz(a[0])) if a[0] == a[1] else k


def align(v, k):
    """The range v with its ends moved inward to multiples of 2**k and excluded ends stepped over; None if no multiple is left.
    (A 32-bit value is a multiple of 2**k, k <= 32, exactly when it is as a signed number: 2**32 is itself a multiple.)"""
    if v is None or k <= 0:
        return v
    m = 1 << min(k, 32)
    lo, hi, ex = v
    lo = -((-lo) // m) * m          # (up to a multiple)
    hi = (hi // m) * m              # (down to a multiple)
    while lo <= hi and lo in ex:
        lo += m
    while lo <= hi and hi in ex:
        hi -= m
    if lo > hi:
        return None
    return (lo, hi, frozenset(x for x in ex if lo < x < hi))


def float_partner(r):
    """The other half of float register r's even/odd pair (f24 <-> f25)."""
    return 'f%d' % (int(r[1:]) ^ 1)


FLT_MAX, DBL_MAX = 3.4028234663852886e38, 1.7976931348623157e308


def i_widen(kind, lo, hi):
    """Rule I: [lo, hi] widened outward past any rounding of its kind; None if it may not be finite."""
    import math
    if kind == 'w':
        return (kind, lo, hi)
    rel, tiny, top = (2.0 ** -20, 2.0 ** -126, FLT_MAX) if kind == 's' else (2.0 ** -50, 2.0 ** -1000, DBL_MAX)
    if not (math.isfinite(lo) and math.isfinite(hi)) or lo > hi:
        return None
    lo, hi = lo - abs(lo) * rel - tiny, hi + abs(hi) * rel + tiny
    if lo < -top or hi > top:
        return None
    return (kind, lo, hi)


def i_value(st, f, kind):
    """Rule I: the range [lo, hi] float register f holds as a single ('s') or a double ('d'), or None."""
    import math, struct
    v = st.get('I' + f)
    if v and v[0] == kind:
        return v[1], v[2]
    if v and v[0] == 'w' and v[1] == v[2] and kind == 's':
        x = struct.unpack('>f', struct.pack('>I', v[1] & 0xFFFFFFFF))[0]
        return (x, x) if math.isfinite(x) else None
    if RULE_I_WORDS and v and v[0] == 'w' and kind == 's' and 0 <= v[1] <= v[2] <= 0x7F7FFFFF:
        # (rule I's words: see RULE_I_WORDS)
        lo = 0.0 if v[1] < 0x00800000 else struct.unpack('>f', struct.pack('>I', v[1]))[0]
        hi = struct.unpack('>f', struct.pack('>I', v[2]))[0]
        return (lo, hi)
    if kind == 'd' and v and v[0] == 'w' and v[1] == v[2] and int(f[1:]) % 2 == 0:
        h = st.get('I' + float_partner(f))
        if h and h[0] == 'w' and h[1] == h[2]:
            x = struct.unpack('>d', struct.pack('>II', h[1] & 0xFFFFFFFF, v[1] & 0xFFFFFFFF))[0]
            return (x, x) if math.isfinite(x) else None
    return None


INF = float('inf')


def n_value(st, f, kind):
    """Path ranges: what float register f can hold as `kind` ('s' or 'd'): (lo, hi, nan) -- a number in [lo, hi] (either end may
    be infinite; lo > hi: no number at all), or a NaN if nan. Rule I's range (never a NaN) and a compare's narrowing (N), both
    when both are known; everything when neither is."""
    lo, hi, nan = -INF, INF, True
    v = i_value(st, f, kind)
    if v is not None:
        lo, hi, nan = v[0], v[1], False
    n = st.get('N' + f)
    if n is not None and n[0] == kind:
        lo, hi, nan = max(lo, n[1]), min(hi, n[2]), nan and n[3]
    # (A denormal end is taken as reaching 0: a CPU that flushes denormal operands to zero compares them as 0 -- the margin
    #  rule I's words keep, rules084's NC-I3. Rule I's i_value takes an exact word as its exact single, denormal or not.)
    tiny = 2.0 ** -126 if kind == 's' else 2.0 ** -1022
    if -tiny < lo < tiny:
        lo = min(lo, 0.0)
    if -tiny < hi < tiny:
        hi = max(hi, 0.0)
    return lo, hi, nan


def n_verdict(op, a, b):
    """Path ranges: True or False if `c.<op>` of sides that can hold a and b (n_value's) always gives that; else None. Ordered
    compares (le, lt, eq): false whenever a side is a NaN."""
    (alo, ahi, anan), (blo, bhi, bnan) = a, b
    numbers = alo <= ahi and blo <= bhi
    if op == 'le':
        can_true, can_false = numbers and alo <= bhi, anan or bnan or (numbers and ahi > blo)
    elif op == 'lt':
        can_true, can_false = numbers and alo < bhi, anan or bnan or (numbers and ahi >= blo)
    else:
        can_true = numbers and max(alo, blo) <= min(ahi, bhi)
        can_false = anan or bnan or (numbers and not (alo == ahi == blo == bhi))
    if not can_true and not can_false:
        return None         # (no value at all: the way here is impossible, which the branch's narrowing will find)
    return None if can_true and can_false else can_true


def n_narrow(st, holds):
    """Path ranges: st on the way out of bc1t / bc1f where the last compare (st['C']) came out `holds`; None if impossible."""
    op, kind, ra, rb = st['C']
    (alo, ahi, anan), (blo, bhi, bnan) = n_value(st, ra, kind), n_value(st, rb, kind)
    if holds:
        # (true: both numbers, and the order -- closed ends throughout, which only ever says less)
        if op == 'eq':
            na = nb = (max(alo, blo), min(ahi, bhi), False)
        else:
            na, nb = (alo, min(ahi, bhi), False), (max(blo, alo), bhi, False)
    else:
        # (false: a NaN on either side, or the order the other way: a > b for le, a >= b for lt; nothing for eq)
        na, nb = (alo, ahi, anan), (blo, bhi, bnan)
        if op in ('le', 'lt'):
            if not bnan:
                na = (max(alo, blo), ahi, anan)
            if not anan:
                nb = (blo, min(bhi, ahi), bnan)
    out = dict(st)
    for r, (lo, hi, nan) in ((ra, na), (rb, nb)):
        if lo > hi and not nan:
            return None
        out['N' + r] = (kind, lo, hi, nan)
    return out


def float_ranges(mn, ops, st, new):
    """Rule I (see RULE_I): what `mn ops` makes known of float registers, from the state before it (st), into new (the state
    after it, with what it wrote already forgotten)."""
    import math
    r = REG.findall(ops)
    parts = mn.split('.')
    if mn == 'mtc1' and len(r) == 2:
        a = get(st, r[0])
        if (a[0], a[1]) != (LOW, HIGH):
            new['I' + r[1]] = ('w', a[0], a[1])
        return new
    if mn == 'mfc1' and len(r) == 2 and r[0] != 'zero':
        v = st.get('I' + r[1])
        if v and v[0] == 'w':
            new[r[0]] = (v[1], v[2], frozenset())
        return new
    if mn.startswith('c.') or mn == 'ctc1':
        if not RULE_F:
            new.pop('Fcc', None)        # (the condition bit is rewritten; with rule F on, its own step has dropped it already)
        new.pop('C', None)              # (path ranges: the condition bit is no longer the last compare's)
    if _IN_PATHS and parts[0] == 'c' and len(parts) == 3 and parts[1] in ('le', 'lt', 'eq') and parts[2] in ('s', 'd') and len(r) == 2 \
            and all(int(x[1:]) % 2 == 0 for x in r):      # (reviewranges A1: only even registers -- see the rule's header)
        # (Path ranges: which compare set the condition bit, for bc1t / bc1f to narrow its sides by; and the verdict from what is
        #  known of each side, NaN included -- at least what rule I's verdict below gives.)
        new['C'] = (parts[1], parts[2], r[0], r[1])
        v = n_verdict(parts[1], n_value(st, r[0], parts[2]), n_value(st, r[1], parts[2]))
        if v is not None:
            new['Fcc'] = v
        return new          # (not rule I's verdict below: n_value's ends are denormal-safe, i_value's exact words are not)
    if parts[0] == 'c' and len(parts) == 3 and parts[1] in ('le', 'lt', 'eq') and len(r) == 2:
        a, b = i_value(st, r[0], parts[2]), i_value(st, r[1], parts[2])
        if a and b:
            if parts[1] == 'le':
                verdict = True if a[1] <= b[0] else False if a[0] > b[1] else None
            elif parts[1] == 'lt':
                verdict = True if a[1] < b[0] else False if a[0] >= b[1] else None
            else:
                verdict = True if a[0] == a[1] == b[0] == b[1] else False if a[1] < b[0] or b[1] < a[0] else None
            if verdict is not None:
                new['Fcc'] = verdict
        return new
    if len(parts) == 3 and parts[0] in ('cvt', 'trunc', 'round', 'floor', 'ceil') and len(r) == 2:
        to, frm = parts[1], parts[2]
        if frm == 'w':
            v = st.get('I' + r[1])
            src = (v[1], v[2]) if v and v[0] == 'w' else None
        else:
            src = i_value(st, r[1], frm)
        if src is None or to not in ('s', 'd', 'w'):
            return new
        lo, hi = src
        if to == 'w':
            if not (-2.0 ** 31 < lo and hi < 2.0 ** 31):
                return new
            lo, hi = (math.trunc(lo), math.trunc(hi)) if parts[0] == 'trunc' else (math.floor(lo), math.ceil(hi))
            if -2 ** 31 <= lo <= hi < 2 ** 31:
                new['I' + r[0]] = ('w', lo, hi)
            return new
        w = (to, float(lo), float(hi)) if (to == 'd' and frm in ('w', 's')) else i_widen(to, float(lo), float(hi))
        if w:
            new['I' + r[0]] = w
        return new
    if len(parts) == 2 and parts[1] in ('s', 'd') and parts[0] in ('mov', 'neg', 'add', 'sub', 'mul', 'div', 'sqrt'):
        kind, op = parts[1], parts[0]
        xs = [i_value(st, x, kind) for x in r[1:]]
        if not r or any(x is None for x in xs) or len(xs) != (2 if op in ('add', 'sub', 'mul', 'div') else 1):
            return new
        if op == 'mov':
            out = (kind,) + xs[0]
        elif op == 'neg':
            out = (kind, -xs[0][1], -xs[0][0])
        elif op == 'sqrt':
            out = i_widen(kind, math.sqrt(xs[0][0]), math.sqrt(xs[0][1])) if xs[0][0] >= 0 else None
        else:
            (a0, a1), (b0, b1) = xs
            if op == 'add':
                c = [a0 + b0, a1 + b1]
            elif op == 'sub':
                c = [a0 - b1, a1 - b0]
            elif op == 'mul':
                c = [x * y for x in (a0, a1) for y in (b0, b1)]
            else:
                c = [x / y for x in (a0, a1) for y in (b0, b1)] if (b0 > 0 or b1 < 0) else None
            out = i_widen(kind, min(c), max(c)) if c else None
        if out:
            new['I' + r[0]] = out
        return new
    return new


# ==== PROPOSED 2026-11-14 by agent "reach2" (part 2) -- NOT approved, NOT reviewed: all four OFF. For independent review. ====
# (merge084b, 2026-11-15: all four REVIEWED and APPROVED by reviewr084 -- T with its fix in table_index. Still OFF by default;
#  the 0.84 night run (run_084.ps1) sets LIFT_RULE_T/Q/L/U=1.)
# With RULE_T, RULE_Q, RULE_L and RULE_U all False (as here) this file is exactly the checker it was (the same dead instructions on
# every function of fuzz/report.txt: results/checker/compare_off.txt).
# (Rebased onto 0.83's unreach.py by collect084, 2026-11-14: reach2's rule O renamed Q, as 0.83 has modnative5's rule O; reach2's
#  rule J left out, as rules084's RULE_I_WORDS is the same rule, and its rule K, as 0.83's rule K (modnative5's) gives the same
#  dead lines on every function. Checked again on 0.83: scratch/agents/collect084/INTEGRATION.md.)
#
# ---- Rule T: a jump table's entries, no further than the index can go ----
# The checker takes every entry of a jump table as a way on from its `jr` (analyse). With rule T it takes only the entries
# whose number the index register can hold there. The table jump must be exactly the compiler's five instructions, ending
# at the jr:
#       sll  $t, $y, 2          (y is not t)
#       lui  $A, hi
#       addu $A, $A, $t         (either order of the two)
#       lw   $j, lo($A)
#       jr   $j
# with y written by none of the last four, and none of the last four a place any branch, jump or table entry of the function
# goes to -- so every run that arrives at the jr came through the sll with y as it still is at the jr, and the word it loaded
# is the table's entry number y (the translation's `switch (jr_addend >> 2)`: the entries it lists, numbered from 0, which
# table_checked already checks against the bounds test). The entries taken are those i with y's range at the jr holding i
# (inside lo..hi and not one of the excluded values); the others' targets are not arrived at from this jr. What it rests on:
# the range the reviewed rules give y at the jr, nothing else. (modrep_func_80111158: `andi $v0, $v0, 0xF; addiu $v1, $v0,
# -0x2; sltiu $v0, $v1, 0x16` -- the index is 0..13, and entry 21 is the only way to 0x81101130.)
RULE_T = os.environ.get('LIFT_RULE_T') == '1'      # (reach2's rule, rebased onto 0.83 by collect084; OFF unless LIFT_RULE_T=1 -- nothing counts from it until an independent review: rules084's REVIEW_REACH2.txt. rules084, 2026-11-15. APPROVED by reviewr084 2026-11-15; the 0.84 night run sets LIFT_RULE_T=1. With reviewr084's fix (2026-11-15): table_index refuses when the instruction before the sll (jr-20) is a likely branch or a call.)
# ---- Rule Q (reach2's "O", renamed by collect084: 0.83 has a rule O): the range test of a moved value, `addiu $t, $x, n; sltiu $t, $t, N` ----
# st['+' + t] = (x, n) says t holds x + n (32 bits, wrapping), made by `addiu $t, $x, n` with t not x, dropped when t or x is
# written (forget), at a call and at a join unless both ways agree. `sltiu $d, $t, N` of such a t links d to x: when d is not
# 0 afterwards, 0 <= (x + n) mod 2**32 < N, so x is one of the N values -n .. N - 1 - n -- said only when that run of values
# does not pass the ends of the signed range (then it is exactly the values of x that pass). When d is 0 nothing is said.
# The checker has this already when the test is on x itself (`sltiu $d, $x, N`: the 'sltu' link); the compiler's switch and
# range tests subtract the lowest case first, and the link was lost there when t is also the result (a0 == d).
# (modrep_func_801144EC: `addiu $v0, $v1, -0x1; sltiu $v0, $v0, 0xF` -- v1 is 1..15, so 51 * (15 - v1) is at most 255.)
RULE_Q = os.environ.get('LIFT_RULE_Q') == '1'      # (reach2's rule, rebased onto 0.83 by collect084; OFF unless LIFT_RULE_Q=1 -- nothing counts from it until an independent review: rules084's REVIEW_REACH2.txt. rules084, 2026-11-15. APPROVED by reviewr084 2026-11-15; the 0.84 night run sets LIFT_RULE_Q=1.)
# ---- Rule L: a register that is another one times a constant ----
# st['*' + d] = (x, m) says d holds x * m (32 bits, wrapping; x a register that is not d, as it still is). Made by
# `sll $d, $x, k` (m = 2**k); kept through `sll $d, $d, k` (m << k), `addu $d, $d, $x` / `addu $d, $x, $d` (m + 1),
# `subu $d, $d, $x` (m - 1) and `addu $d, $a, $b` of two multiples of the same x (the sum); dropped when d or x is written
# otherwise (forget), at a call, and at a join unless both ways agree. Used in one place: when every x * m for x in x's range
# fits in a signed 32-bit number, d's range is that product's (met with the range interval arithmetic gives). The compiler
# writes `x * 7` as `sll $v0, $v1, 3; subu $v0, $v0, $v1`: interval arithmetic takes the two v1 as unrelated and gets
# -255 .. 2040 for a byte, where the product is 0 .. 1785. (func_8011FD1C: a byte times 224 is never negative.)
RULE_L = os.environ.get('LIFT_RULE_L') == '1'      # (reach2's rule, rebased onto 0.83 by collect084; OFF unless LIFT_RULE_L=1 -- nothing counts from it until an independent review: rules084's REVIEW_REACH2.txt. rules084, 2026-11-15. APPROVED by reviewr084 2026-11-15; the 0.84 night run sets LIFT_RULE_L=1.)
# ---- Rule U: a bit a branch has shown to be 1 ----
# st['1' + r] = m says: every bit of m is 1 in register r. Made in one way: `andi $d, $r, B` with B a single bit (d not r)
# links d to r, and on the way out of `bne $d, $zero` / `beq $d, $zero` where d is not 0, r has bit B (d is r & B: not 0
# means the bit is there). Used in one place: `andi $d, $r, K` then has the bits m & K, so it is at least m & K (a value
# with those bits set and none above bit 15 is no smaller than them), and d has them too. Dropped when r is written
# (forget), at a call; at a join only the bits both ways in have are kept. (func_800EC354: `andi $v0, $a1, 0x8000; bne
# $v0, $zero` and then `switch (a1 & 0x8F00)`: the value is 0x8000 or more, so below 0x8200 only 0x8000 and 0x8100 are left
# -- rule A's multiples of 0x100 -- and the "none of these" jump at 0x800EC3C0 never runs.)
RULE_U = os.environ.get('LIFT_RULE_U') == '1'      # (reach2's rule, rebased onto 0.83 by collect084; OFF unless LIFT_RULE_U=1 -- nothing counts from it until an independent review: rules084's REVIEW_REACH2.txt. rules084, 2026-11-15. APPROVED by reviewr084 2026-11-15; the 0.84 night run sets LIFT_RULE_U=1.)


# ---- Rule "path ranges" (2026-11-22, convert2: the user approved "Use your recommendations", 2026-11-22 -- the rule as stated
#      there: it reasons only from values the function computes in registers, never re-reading memory. OFF unless
#      LIFT_RULE_RANGES=1; nothing counts from it until an independent review approves it) ----
# analyse joins every way into an instruction into one state: a register that is 0..3 one way and 8..9 the other becomes
# 0..9 there, and what one way's compares said is lost where the ways meet. This keeps up to PATHS - 1 states apart at each
# instruction instead, each exactly as one way (or several ways that agree exactly) brought it; only when all those places are
# taken are the further ways joined into one more state, as analyse joins them all (with its widening). Each state is followed
# on its own through the instructions after, by the very same transfer / outcome / after_call as analyse: so a branch way is
# taken only if it is possible for SOME kept state, and an instruction is reached if SOME kept state arrives there.
# Why it is sound: every run of the function that arrives at an instruction arrived along some way, and each way's facts are
# kept, either in a state of its own or in the joined one -- the set of runs a state stands for is never smaller than
# analyse's would be for that way. Nothing new is assumed: no memory is read back (rule B stays off), after a call nothing is
# known (strict mode), the same rules decide every transfer. It only stops throwing away what the function's own register
# arithmetic and compares already showed on each way.
# Part 2, float compares (only in analyse_paths' walk): `c.le` / `c.lt` / `c.eq` (.s, .d: the ORDERED compares, false whenever
# a side is a NaN; c.ult and the like are left alone) is noted, st['C'] = (op, kind, a, b), until a side or the condition bit
# is written again (forget: a side, or half of a double side; any other c.* or ctc1; a call drops everything). On the ways out
# of the bc1t / bc1f that reads it, what the outcome says of the two sides is kept, st['N' + f] = (kind, lo, hi, nan): f holds
# a number in [lo, hi] (ends may be infinite; lo > hi: no number) or, if nan, a NaN. True: both are numbers and a <= b (closed
# ends for lt too, which only says less); eq: both in the overlap. False: a NaN on a side, or the order the other way -- so a's
# low end is raised to b's only when b can't be a NaN, b's high end lowered to a's only when a can't be one; eq false says
# nothing. A compare's verdict then comes from what is known of both sides, rule I's ranges and these, NaN included
# (n_verdict; for an exact denormal word it can say less than rule I's verdict, never more); a denormal end counts as reaching 0 (a CPU that flushes denormal operands compares them as 0: rules084's
# NC-I3 margin) -- so in this walk rule I's exact-word verdict is not used. Joins keep the hull of the same kind (NaN if
# either side may be one). (func_801079B4: f10 is 0.0 / 128.0 / 255.0, `c.lt.s $f10, $f2` false says f2 <= 255 or a NaN,
# so `2**31 <= f2` -- the float-to-unsigned path for big values -- can never hold.)
# Assumed (reviewranges, APPROVED 2026-11-22, notes\REVIEW_RANGES.txt): a single-precision compare reads even registers only
# (in the translation an odd register's single is kept elsewhere than the odd half mtc1 / lwc1 write): guarded -- a compare
# with an odd register is not noted. (None in the game today: all 1,056 compares use even registers.)
# What counts: dead() takes the instructions analyse never arrives at AND those analyse_paths never arrives at -- each walk
# is sound on its own, so the union is, and it proves at least what analyse alone proves. Its claims are in each function's
# never_runs line, part of that function's own cache key (lift.py), like every other rule's. Controls:
# scratch\convert2\cex\cex_ranges.py (19 cases, 12 sabotages caught), and every earlier counter-example set unchanged.
RULE_RANGES = os.environ.get('LIFT_RULE_RANGES') == '1'
_IN_PATHS = False           # (True only while analyse_paths walks: the float-compare part is its alone)
PATHS = int(os.environ.get('LIFT_RULE_RANGES_PATHS', '64'))

def table_index(f, jr, targets):
    """Rule T: the register holding the entry number at this table jump, or None if the jump is not exactly the idiom."""
    ins = f['ins']
    seq = [ins.get(jr - 4 * i) for i in range(5)]       # jr, lw, addu, lui, sll
    if any(x is None for x in seq):
        return None
    (jmn, jops), (lmn, lops), (amn, aops), (umn, uops), (smn, sops) = seq
    j = REG.findall(jops)
    lw = re.match(r'^\$(\w+),\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))\(\$(\w+)\)$', lops.strip())
    ad = REG.findall(aops)
    lu = REG.findall(uops)
    sl = re.match(r'^\$(\w+),\s*\$(\w+),\s*2$', sops.strip())
    if jmn != 'jr' or lmn != 'lw' or amn != 'addu' or umn != 'lui' or smn != 'sll' or not lw or not sl or len(ad) != 3 or len(lu) != 1:
        return None
    t, y = sl.group(1), sl.group(2)
    A = lu[0]
    if len(j) != 1 or lw.group(1) != j[0] or lw.group(3) != A or ad[0] != A or sorted(ad[1:]) != sorted([A, t]):
        return None
    if y in (t, A, j[0], 'zero') or t in (A,) or A == 'zero':
        return None
    if any((jr - 4 * i) in targets for i in range(4)):      # (a way into the middle: the index there need not be y)
        return None
    prev = ins.get(jr - 20, ('', ''))[0]
    if prev in ('beql', 'bnel', 'bgezl', 'bgtzl', 'blezl', 'bltzl', 'bc1tl', 'bc1fl', 'bgezall', 'bltzall', 'j', 'b', 'jr',
                'jal', 'jalr', 'bal', 'bgezal', 'bltzal'):
        return None     # (reviewr084 fix, 2026-11-15: the sll is the delay slot of a likely branch -- not taken, it is skipped -- or of a call -- t comes back as the callee left it)
    return y


def table_entry_possible(yv, i):
    """Rule T: can the index y, in the range yv = (lo, hi, excluded), select entry i? `sll $t, $y, 2` keeps only y's low 30
    bits, so entry i is selected by every y congruent to i modulo 2**30 -- not only y == i. (rules084, 2026-11-15: before, an
    index whose range lay above 2**30, or far below 0, made the entries it does select look impossible: table_checked only
    checks that SOME `sltiu` before the jump has the table's count, not that it tests y -- controls/cex_reach2.py NC-T1.)"""
    lo, hi, ex = yv
    step = 1 << 30
    j = -((i - lo) // step)             # (the smallest j with i + j * 2**30 >= lo)
    y = i + j * step
    while y <= hi:
        if y not in ex:
            return True
        y += step
    return False


def branch_targets(f):
    """Every place a branch, jump or table entry of the function goes to."""
    out = set()
    for a, (mn, ops) in f['ins'].items():
        if mn in BRANCH or mn in ('j', 'b', 'jal'):
            t = target(ops)
            if t is not None:
                out.add(t)
    for t in f['tables'].values():
        out.update(t.values())
    return out


def forget(st, regs):
    """These registers changed: what is known about them, and every link from or to them, goes."""
    out = {}
    for k, v in st.items():
        if k in regs:
            continue
        if k[0] == '@' and (k[1:] in regs or v[1] in regs):
            continue
        if k[0] == '#' and k[1:] in regs:        # (rule A: what was known of its low bits goes with it)
            continue
        if k[0] == 'M' and k[1] != '@' and k[1:].split('+')[0] in regs:      # (rule B: a place named by a register that changed)
            continue
        if k[0] == 'I' and (k[1:] in regs or (v[0] == 'd' and float_partner(k[1:]) in regs)):   # (rule I: that register changed, or half a double)
            continue
        if k[0] == 'F' and k[1:] in regs:        # (rule F: what was known of a float register)
            continue
        if k[0] in 'GE' and (k[1:] in regs or v in regs):     # (rule F: a copy of a float register's bits, either side changed)
            continue
        if k in ('%hi', '%lo') and (k[1:] in regs or v[0] in regs):       # (rule H: HI/LO rewritten, or the divisor changed)
            continue
        if k[0] == '<' and (k[1:] in regs or v in regs):                  # (rule H: the remainder's link to its divisor)
            continue
        if k[0] in '+*' and (k[1:] in regs or v[0] in regs):              # (rules Q and L, proposed: either side of the link changed)
            continue
        if k[0] == '1' and k[1:] in regs:                                 # (rule U, proposed: the register changed)
            continue
        if k[0] == 'N' and (k[1:] in regs or (v[0] == 'd' and float_partner(k[1:]) in regs)):  # (path ranges: that register changed, or half a double)
            continue
        if k == 'C' and (v[2] in regs or v[3] in regs or (v[1] == 'd' and (float_partner(v[2]) in regs or float_partner(v[3]) in regs))):
            continue                                                      # (path ranges: a side of the last float compare changed)
        out[k] = v
    return out


# ---- Rule B (proposed 2026-11-10; NOT approved, the user 2026-11-10: off): a word read back from where it was just stored ----
# st['M' + place] = (range, low zero bits) says: the 32-bit word at `place` holds a value in that range. A place is a base
# register and an offset, 'sp+16', or, when the base register holds one known value, the address itself, '@8008D294'.
# Made by `sw`; used by `lw` from the same place; and dropped
#   - by every other store of any width (it may be to the same memory through another register),
#   - when the base register is written (forget),
#   - at every call (after_call starts from nothing) and at every other way in (a root starts from nothing; a join keeps only
#     what both ways in agree on).
# So between the `sw` and the `lw` this function has run no store and no call on any path. What it takes on trust beyond the
# reviewed checker: nothing else writes that word in between -- no other thread and no interrupt handler. (To be confirmed for
# the port by whoever reviews this: game threads there switch only inside system calls, and a call drops everything.)
STORES = {'sb', 'sh', 'sw', 'swl', 'swr', 'sd', 'sdl', 'sdr', 'swc1', 'sdc1', 'sc', 'scd'}
PLACE = re.compile(r'^\$(\w+),\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))\(\$(\w+)\)$')


def place_of(st, base, offset):
    a = get(st, base)
    if a[0] == a[1]:
        return 'M@%08X' % ((a[0] + offset) & 0xFFFFFFFF)
    return 'M%s+%d' % (base, offset)


def image_load(mn, ops, st):
    """(rule O, proposed) The values a load can read when every address it can make -- its base register's range plus the
    offset, at most 0x4000 bytes of them -- is inside MOD_IMAGE and in no relocated word: (lowest, highest) of the image's values
    at each of them that the load's width allows (aligned); else None."""
    m = PLACE.match(ops.strip())
    if not m:
        return None
    base = get(st, m.group(3))
    if base[1] - base[0] > 0x4000:
        return None
    start, data, moved = MOD_IMAGE
    w = {'lw': 4, 'lhu': 2, 'lh': 2, 'lbu': 1, 'lb': 1}[mn]
    lo = hi = None
    for b in range(base[0], base[1] + 1):
        if b in base[2]:
            continue
        off = ((b + sext16(int(m.group(2), 0))) & 0xFFFFFFFF) - start
        if off % w:
            continue        # (a load the N64 can't make: an address error)
        if off < 0 or off + w > len(data) or (off & ~3) in moved:
            return None
        raw = int.from_bytes(data[off:off + w], 'big')
        if mn in ('lh', 'lb') and raw >> (8 * w - 1):
            raw -= 1 << (8 * w)
        raw = s32(raw) if w == 4 else raw
        lo = raw if lo is None else min(lo, raw)
        hi = raw if hi is None else max(hi, raw)
    return None if lo is None else (lo, hi)


def transfer(mn, ops, st):
    """The state after one instruction (calls: `clobber` does the rest)."""
    new = transfer_ranges(mn, ops, st)
    if RULE_I:
        new = float_ranges(mn, ops, st, new)
    return new


def transfer_ranges(mn, ops, st):
    """transfer, but for rule I's float ranges."""
    r = REG.findall(ops)
    _, defs = uses_defs(mn, ops)
    new = forget(st, defs)
    if RULE_F and (mn.startswith('c.') or mn == 'ctc1'):
        # (Rule F: the float condition is rewritten; true only for x == x of an x known not NaN.)
        new.pop('Fcc', None)
        if mn == 'c.eq.s' and len(r) == 2 and r[0] == r[1] and st.get('F' + r[0]) == 'nonneg':
            new['Fcc'] = True
        return new
    if RULE_F and mn == 'mfc1' and len(r) == 2 and r[0] != 'zero':
        new['G' + r[0]] = r[1]          # (rule F: integer register r0 holds float register r1's bits)
        return new
    if RULE_F and mn == 'mtc1' and len(r) == 2 and st.get('G' + r[0]) is not None and st['G' + r[0]] != r[1]:
        src = st['G' + r[0]]
        new['E' + r[1]] = src           # (rule F: r1 now holds the same bits as float register src)
        if st.get('F' + src) == 'nonneg':
            new['F' + r[1]] = 'nonneg'
        return new
    if RULE_F and mn in ('mul.s', 'add.s', 'sqrt.s', 'mov.s') and r and r[0] in defs:
        if mn == 'mul.s' and len(r) == 3 and (r[1] == r[2] or st.get('E' + r[2]) == r[1] or st.get('E' + r[1]) == r[2]):
            new['F' + r[0]] = 'nonneg'
        elif mn == 'add.s' and len(r) == 3 and st.get('F' + r[1]) == 'nonneg' and st.get('F' + r[2]) == 'nonneg':
            new['F' + r[0]] = 'nonneg'
        elif mn in ('sqrt.s', 'mov.s') and len(r) == 2 and st.get('F' + r[1]) == 'nonneg':
            new['F' + r[0]] = 'nonneg'
        return new
    if RULE_H and mn in ('div', 'divu') and len(r) == 3 and r[0] == 'zero':
        x, y = get(st, r[1]), get(st, r[2])
        if mn == 'div':
            m = max(abs(y[0]), abs(y[1]))
            big = max(abs(x[0]), abs(x[1]))
            rem = m - 1         # (not narrowed by x's size: a range that grew each turn of a loop would be widened away)
            hi = ranged(0 if x[0] >= 0 else -rem, 0 if x[1] <= 0 else rem)
            if x[0] >= 0 and y[0] >= 1:
                lo = ranged(x[0] // y[1], x[1] // y[0])
            else:
                lo = ranged(-big, big)
        else:
            # (unsigned: only when both are known not negative, so their ranges are their unsigned values)
            hi = lo = None
            if x[0] >= 0 and y[0] >= 0:
                hi = ranged(0, y[1] - 1) if y[1] >= 1 else None
                lo = ranged(x[0] // y[1] if y[1] >= 1 else 0, x[1] // max(1, y[0]))
        if hi is not None:
            new['%hi'] = (r[2], hi, mn == 'div' and y[0] >= 1)
        if lo is not None:
            new['%lo'] = (r[2], lo, False)
        return new
    if RULE_H and mn in ('mfhi', 'mflo') and len(r) == 1 and r[0] != 'zero':
        known = st.get('%' + mn[2:])
        if known is not None:
            y = get(st, known[0])
            if not (y[0] <= 0 <= y[1]) or 0 in y[2]:       # (y is not 0 here: the division's zero check was passed)
                new[r[0]] = known[1]
                if mn == 'mfhi' and known[2] and known[0] != r[0]:
                    new['<' + r[0]] = known[0]
            return new
        # (2026-11-12, agent "loopbounds": nothing from a division -- HI/LO came from mult/multu or mthi/mtlo: go on to rule
        #  M's mflo below, as without rule H. Before, this returned here, and with rule H on the checker lost what rule M proves:
        #  func_80114AB8's 80114C58.)
    if RULE_H and mn in ('slt', 'subu') and len(r) == 3 and st.get('<' + (r[1] if mn == 'slt' else r[2])) == (r[2] if mn == 'slt' else r[1]):
        # (Rule H's link: r < y, -y < r, with y > 0 and y < 2**30 as it was at the division -- y unchanged since.)
        y = get(st, r[2] if mn == 'slt' else r[1])
        if mn == 'slt' and y[0] >= 1:
            new[r[0]] = (1, 1, frozenset())
            return new
        if mn == 'subu' and y[0] >= 1 and y[1] < (1 << 30):
            new[r[0]] = (1, 2 * y[1] - 1, frozenset())
            return new
    if RULE_M and mn == 'mult' and len(r) == 2:
        # Rule M (proposed 2026-11-10; reviewed, approved by the user 2026-11-10): `mult $a, $b` multiplies two signed 32-bit values into 64 bits and leaves
        # the low 32 in LO. When every product of a value in a's range by one in b's fits in a signed 32-bit number, LO is that
        # product, so its range is the smallest and largest of the four corner products. `lo` is kept like a register: mips.py
        # lists it as written by mult, multu, div, divu and mtlo, so `forget` drops it at each of those, and a call drops
        # everything. `mflo $d` then gives d that range. Otherwise nothing is known of LO, as before.
        a, b = get(st, r[0]), get(st, r[1])
        corners = [x * y for x in (a[0], a[1]) for y in (b[0], b[1])]
        v = ranged(min(corners), max(corners))
        if v is not None:
            new['lo'] = v
        return new
    if RULE_M and mn == 'mflo' and len(r) == 1 and r[0] != 'zero' and 'lo' in st:
        new[r[0]] = st['lo']
        return new
    if mn in STORES:
        new = {k: v for k, v in new.items() if k[0] != 'M'}       # (any store: every remembered word may have changed)
        m = PLACE.match(ops.strip())
        if RULE_B and mn == 'sw' and m:
            new[place_of(st, m.group(3), sext16(int(m.group(2), 0)))] = (get(st, m.group(1)), low_zeros(st, m.group(1)))
        return new
    if RULE_B and mn == 'lw':
        m = PLACE.match(ops.strip())
        known = m and st.get(place_of(st, m.group(3), sext16(int(m.group(2), 0))))
        if known and m.group(1) != 'zero':
            new[m.group(1)] = known[0]
            if known[1] > 0:
                new['#' + m.group(1)] = known[1]
            elif ('#' + m.group(1)) in new:
                del new['#' + m.group(1)]
            return new
    if len(defs) != 1 or not r or r[0] == 'zero':
        return new
    d = next(iter(defs))
    if d != r[0]:
        return new
    imm = re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ops)
    k = int(imm.group(1), 0) if imm and '(' not in ops else None
    a, b = get(st, r[1]) if len(r) > 1 else TOP, get(st, r[2]) if len(r) > 2 else TOP
    a0, b0 = r[1] if len(r) > 1 else None, r[2] if len(r) > 2 else None
    v = None
    if mn in ('addiu', 'addi') and k is not None:
        n = sext16(k)
        v = ranged(a[0] + n, a[1] + n)
        if v:
            v = tidy(v[0], v[1], frozenset(x + n for x in a[2]))
        if n == 0 and a0 != d and ('@' + a0) in st and st['@' + a0][1] != d:     # (review 2026-11-09: not a link whose register this overwrites)
            new['@' + d] = st['@' + a0]
        if RULE_Q and a0 != d and a0 != 'zero':
            new['+' + d] = (a0, n)              # (rule Q, proposed: d is a0 + n)
    elif mn == 'lui' and k is not None:
        v = ranged(s32(k << 16), s32(k << 16))
    elif mn == 'ori' and k is not None:
        v = (ranged(a[0] | (k & 0xFFFF), a[0] | (k & 0xFFFF)) if a[0] == a[1] else ranged(0, bound(a[1] | (k & 0xFFFF))) if a[0] >= 0 else None)
    elif mn == 'andi' and k is not None:
        v = ranged(0, k & 0xFFFF)
        if RULE_K and a[0] >= 0 and a[1] <= (k & 0xFFFF) and (k & 0xFFFF) & ((k & 0xFFFF) + 1) == 0:
            v = tidy(a[0], a[1], a[2])      # (rule K: a mask of low ones over values already under it keeps them all)
        if RULE_N and a[0] == a[1]:
            v = ranged((a[0] & 0xFFFFFFFF) & (k & 0xFFFF), (a[0] & 0xFFFFFFFF) & (k & 0xFFFF))     # (rule N: one value)
        elif RULE_N and a[0] >= 0:
            v = ranged(0, min(a[1], k & 0xFFFF))          # (rule N: x & K <= x for x >= 0)
        if RULE_U and a0 not in (None, 'zero'):
            ones = st.get('1' + a0, 0) & k & 0xFFFF
            if ones and v is not None and v[0] < ones <= v[1]:
                v = ranged(ones, v[1])                    # (rule U, proposed: the result has these bits, so it is at least them)
            if ones:
                new['1' + d] = ones
            b1 = k & 0xFFFF
            if a0 != d and b1 and b1 & (b1 - 1) == 0:
                new['@' + d] = ('bit', a0, b1)            # (rule U, proposed: d is not 0 exactly when a0 has this bit)
    elif mn == 'xori' and k is not None and a[0] >= 0:
        v = ranged(0, bound(a[1] | (k & 0xFFFF)))
    elif mn == 'slti' and k is not None:
        v = ranged(0, 1)
        if RULE_K and a[1] < sext16(k):
            v = ranged(1, 1)            # (rule K: every value below)
        elif RULE_K and a[0] >= sext16(k):
            v = ranged(0, 0)            # (rule K: none below)
        if a0 != d:                 # (a link says what a register's value was compared with, so that register must still hold it)
            new['@' + d] = ('slt', a0, sext16(k))
    elif mn == 'sltiu' and k is not None:
        v = ranged(0, 1)
        if RULE_K:
            ku = sext16(k) & 0xFFFFFFFF
            if (a[0] >= 0 or a[1] < 0) and (a[0] & 0xFFFFFFFF) <= (a[1] & 0xFFFFFFFF):
                lo_u, hi_u = a[0] & 0xFFFFFFFF, a[1] & 0xFFFFFFFF     # (all of one sign: unsigned order is the same order)
                if hi_u < ku:
                    v = ranged(1, 1)
                elif lo_u >= ku:
                    v = ranged(0, 0)
        if a0 != d:
            new['@' + d] = ('sltu', a0, sext16(k) & 0xFFFFFFFF)
        elif RULE_Q and ('+' + a0) in st and st['+' + a0][0] != d:
            # (rule Q, proposed: the tested value is x + n, read from the state BEFORE this instruction, which overwrites it)
            new['@' + d] = ('sltuo', st['+' + a0][0], (st['+' + a0][1], sext16(k) & 0xFFFFFFFF))
    elif mn in ('slt', 'sltu') and len(r) == 3:
        v = ranged(0, 1)
        if b[0] == b[1] and a0 != d:
            new['@' + d] = ('slt' if mn == 'slt' else 'sltu', a0, b[0] if mn == 'slt' else b[0] & 0xFFFFFFFF)
        elif RULE_D and a[0] == a[1] and b0 != d and b0 != 'zero':
            # Rule D (proposed 2026-11-10; reviewed, approved by the user 2026-11-10): the constant is on the LEFT, `slt $d, $c, $x` with c one known value K
            # (read from the state BEFORE this instruction, so it doesn't matter that $d is often $c itself: `slt $v0, $v0,
            # $v1`). d is 1 exactly when K < x. The link is on x, which this instruction doesn't write.
            new['@' + d] = ('gt' if mn == 'slt' else 'gtu', b0, a[0] if mn == 'slt' else a[0] & 0xFFFFFFFF)
    elif RULE_O and MOD_IMAGE and mn in ('lw', 'lhu', 'lh', 'lbu', 'lb') and image_load(mn, ops, st) is not None:
        x = image_load(mn, ops, st)
        v = ranged(x[0], x[1])          # (rule O: addresses in the mod's read-only image: the values there)
    elif mn == 'lbu':
        v = ranged(0, 0xFF)
    elif mn == 'lhu':
        v = ranged(0, 0xFFFF)
    elif mn == 'lb':
        v = ranged(-0x80, 0x7F)
    elif mn == 'lh':
        v = ranged(-0x8000, 0x7FFF)
    elif mn in ('addu', 'add') and len(r) == 3:
        v = ranged(a[0] + b[0], a[1] + b[1])
        for x, y, src in ((a, b, b0), (b, a, a0)):
            if x[0] == 0 and x[1] == 0:
                v = y
                if ('@' + src) in st and src != d and st['@' + src][1] != d:
                    new['@' + d] = st['@' + src]
    elif mn == 'or' and len(r) == 3:
        for x, y, src in ((a, b, b0), (b, a, a0)):
            if x[0] == 0 and x[1] == 0:
                v = y
                if ('@' + src) in st and src != d and st['@' + src][1] != d:
                    new['@' + d] = st['@' + src]
        if v is None and a[0] >= 0 and b[0] >= 0:
            v = ranged(max(a[0], b[0]), bound(a[1] | b[1]))
    elif mn in ('subu', 'sub') and len(r) == 3:
        v = ranged(a[0] - b[1], a[1] - b[0])
    elif mn == 'negu' and len(r) == 2:
        v = ranged(-a[1], -a[0])
    elif mn == 'and' and len(r) == 3:
        if a[0] >= 0 or b[0] >= 0:
            v = ranged(0, min(x[1] for x in (a, b) if x[0] >= 0))
    elif mn == 'xor' and len(r) == 3 and a[0] >= 0 and b[0] >= 0:
        v = ranged(0, bound(a[1] | b[1]))
    elif mn == 'sra' and k is not None:
        v = ranged(a[0] >> (k & 31), a[1] >> (k & 31))
    elif mn == 'srl' and k is not None:
        s = k & 31
        v = ranged(a[0] >> s, a[1] >> s) if a[0] >= 0 else ranged(0, 0xFFFFFFFF >> s) if s > 0 else None
    elif mn == 'sll' and k is not None:
        s = k & 31
        v = ranged(a[0] << s, a[1] << s)
    if RULE_L:
        # (Rule L, proposed: d as a multiple of another register. Links are read from the state BEFORE this instruction.)
        link = None
        la, lb = st.get('*' + a0) if a0 else None, st.get('*' + b0) if b0 else None
        if mn == 'sll' and k is not None and a0 not in (None, 'zero'):
            if a0 != d:
                link = (a0, 1 << (k & 31))
                if la is not None and la[0] != d:
                    link = (la[0], la[1] << (k & 31))
            elif la is not None:
                link = (la[0], la[1] << (k & 31))
        elif mn in ('addu', 'add') and len(r) == 3:
            if la is not None and la[0] == b0:
                link = (b0, la[1] + 1)
            elif lb is not None and lb[0] == a0:
                link = (a0, lb[1] + 1)
            elif la is not None and lb is not None and la[0] == lb[0]:
                link = (la[0], la[1] + lb[1])
        elif mn in ('subu', 'sub') and len(r) == 3:
            if la is not None and la[0] == b0:
                link = (b0, la[1] - 1)
            elif la is not None and lb is not None and la[0] == lb[0]:
                link = (la[0], la[1] - lb[1])
        if link is not None and link[0] != d and link[0] != 'zero' and abs(link[1]) < (1 << 32):
            new['*' + d] = link
            x = get(st, link[0])
            w = ranged(min(x[0] * link[1], x[1] * link[1]), max(x[0] * link[1], x[1] * link[1]))
            if w is not None:
                v = w if v is None else (ranged(max(v[0], w[0]), min(v[1], w[1])) or w)
    if v is not None:
        new[d] = v
    if RULE_A:
        # Rule A: the low zero bits of the result, from the instruction's own arithmetic on 32-bit values. za, zb: the operands'.
        # Adding, subtracting, or-ing and xor-ing keep the low bits that are 0 in both; and-ing keeps those 0 in either; a left
        # shift adds its count; a right shift takes its count away. Anything else: nothing known (no entry).
        za, zb = low_zeros(st, a0) if a0 else 0, low_zeros(st, b0) if b0 else 0
        z = 0
        if mn in ('addiu', 'addi', 'ori', 'xori') and k is not None:
            z = min(za, ctz(sext16(k) if mn in ('addiu', 'addi') else k & 0xFFFF))
        elif mn == 'andi' and k is not None:
            z = max(za, ctz(k & 0xFFFF))
        elif mn == 'lui' and k is not None:
            z = ctz(k << 16)
        elif mn in ('addu', 'add', 'subu', 'sub', 'or', 'xor') and len(r) == 3:
            z = min(za, zb)
        elif mn == 'and' and len(r) == 3:
            z = max(za, zb)
        elif mn == 'sll' and k is not None:
            z = min(32, za + (k & 31))
        elif mn in ('sra', 'srl') and k is not None:
            z = max(0, za - (k & 31))
        if z > 0:
            new['#' + d] = z
    return new


def narrow(st, r, lo=LOW, hi=HIGH, not_value=None):
    """st with register r also within lo..hi and not not_value; None if that's impossible."""
    if r == 'zero':
        ok = lo <= 0 <= hi and not_value != 0
        return st if ok else None
    a = get(st, r)
    v = tidy(max(a[0], lo), min(a[1], hi), a[2] | ({not_value} if not_value is not None else set()))
    if RULE_A:
        v = align(v, st.get('#' + r, 0))         # (rule A: only multiples of 2**k are left to choose from)
    if v is None:
        return None
    out = dict(st)
    out[r] = v
    return out


def through_link(st, r, is_zero):
    """r holds a comparison's result (slti and so on): what r being 0, or not 0, says about the value compared. Given r's 0-or-1 only."""
    link = st.get('@' + r)
    if not link:
        return st
    kind, src, k = link
    if kind == 'bit':           # (rule U, proposed: r is src & k, k one bit)
        if is_zero:
            return st
        out = dict(st)
        out['1' + src] = out.get('1' + src, 0) | k
        return out
    if kind == 'sltuo':         # (rule Q, proposed: r is 1 exactly when 0 <= (src + n) mod 2**32 < N)
        n, N = k
        if is_zero or N < 1 or -n < LOW or N - 1 - n > HIGH:
            return st
        return narrow(st, src, lo=-n, hi=N - 1 - n)
    if kind == 'slt':
        return narrow(st, src, lo=k) if is_zero else narrow(st, src, hi=k - 1)
    if kind == 'gt':            # (rule D: r is 1 exactly when k < src, signed)
        return narrow(st, src, hi=k) if is_zero else narrow(st, src, lo=k + 1)
    if kind == 'gtu':           # (rule D, unsigned: only said when src is known not negative and k fits, as for sltu below)
        a = get(st, src)
        if a[0] < 0 or k > HIGH:
            return st
        return narrow(st, src, hi=k) if is_zero else narrow(st, src, lo=k + 1)
    a = get(st, src)        # (sltu: unsigned)
    if is_zero:
        return narrow(st, src, lo=k) if a[0] >= 0 and k <= HIGH else st
    return narrow(st, src, lo=0, hi=k - 1) if k <= HIGH + 1 else st


def outcome(mn, ops, st, taken):
    """st on this way out of a branch; None if this way can't happen."""
    base = mn[:-1] if mn.endswith('l') and mn not in ('bgezal', 'bltzal') else mn
    r = REG.findall(ops)
    if base in ('bc1t', 'bc1f'):
        if RULE_F and st.get('Fcc') is True and (base == 'bc1t') != taken:
            return None             # (rule F: x == x of an x known not NaN is true)
        if RULE_I and st.get('Fcc') is not None and (base == 'bc1t') != (st['Fcc'] == taken):
            return None             # (rule I: a float compare known true or false, and this way needs the other)
        if _IN_PATHS and st.get('C') is not None:
            return n_narrow(st, (base == 'bc1t') == taken)      # (path ranges: what the compare's outcome says of its sides)
        return st
    if base in ('beq', 'bne'):
        equal = (base == 'beq') == taken
        x, y = r[0], r[1]
        a, b = get(st, x), get(st, y)
        if equal:
            s = narrow(st, x, b[0], b[1])
            s = s and narrow(s, y, a[0], a[1])
            if s and y == 'zero':
                s = through_link(s, x, True)
            if s and x == 'zero':
                s = through_link(s, y, True)
            return s
        if x == y:
            return None
        s = st
        if b[0] == b[1]:
            s = narrow(s, x, not_value=b[0])
        if s and a[0] == a[1]:
            s = narrow(s, y, not_value=a[0])
        if s and y == 'zero':
            s = through_link(s, x, False)
        if s and x == 'zero':
            s = through_link(s, y, False)
        return s
    x = r[0]
    limits = {'blez': (LOW, 0), 'bgtz': (1, HIGH), 'bltz': (LOW, -1), 'bgez': (0, HIGH), 'bgezal': (0, HIGH), 'bltzal': (LOW, -1)}[base]
    if not taken:
        limits = {(LOW, 0): (1, HIGH), (1, HIGH): (LOW, 0), (LOW, -1): (0, HIGH), (0, HIGH): (LOW, -1)}[limits]
    return narrow(st, x, limits[0], limits[1])


def join(a, b):
    out = {}
    for k in set(a) & set(b):
        if k[0] == '@':
            if a[k] == b[k]:
                out[k] = a[k]
        elif k[0] == '#':
            out[k] = min(a[k], b[k])            # (rule A: a multiple of both is a multiple of the smaller power)
        elif k[0] == 'M':
            if a[k] == b[k]:                    # (rule B: only a word both ways in agree on, exactly)
                out[k] = a[k]
        elif k[0] == '%':
            if a[k][0] == b[k][0]:              # (rule H: the same divisor both ways: the two ranges' span)
                ra, rb = a[k][1], b[k][1]
                out[k] = (a[k][0], (min(ra[0], rb[0]), max(ra[1], rb[1]), frozenset()), a[k][2] and b[k][2])
        elif k[0] in 'F<GE+*':
            if a[k] == b[k]:                    # (rules F and H -- and Q and L, proposed: only what both ways in agree on, exactly)
                out[k] = a[k]
        elif k[0] == '1':
            if a[k] & b[k]:                     # (rule U, proposed: the bits both ways in have)
                out[k] = a[k] & b[k]
        elif k[0] == 'I':
            if a[k][0] == b[k][0]:              # (rule I: the hull of two ranges of the same kind)
                out[k] = (a[k][0], min(a[k][1], b[k][1]), max(a[k][2], b[k][2]))
        elif k[0] == 'N':
            if a[k][0] == b[k][0]:              # (path ranges: the hull, NaN if either way may hold one)
                out[k] = (a[k][0], min(a[k][1], b[k][1]), max(a[k][2], b[k][2]), a[k][3] or b[k][3])
        elif k == 'C':
            if a[k] == b[k]:
                out[k] = a[k]
        else:
            lo, hi = min(a[k][0], b[k][0]), max(a[k][1], b[k][1])
            has = lambda v, x: v[0] <= x <= v[1] and x not in v[2]
            # (A value neither side can hold stays out: the gap between 6 and 9 when a register is one or the other, an excluded 0 joined with a 1.)
            gaps = range(lo + 1, hi) if hi - lo <= 256 else a[k][2] | b[k][2]
            ex = frozenset(x for x in gaps if not has(a[k], x) and not has(b[k], x))
            if len(ex) > MAX_EXCLUDED:
                ex = frozenset()
            if (lo, hi) != (LOW, HIGH) or ex:
                out[k] = (lo, hi, ex)
    return out


def after_call(st, keep_saved, f=None, pc=None):
    # (review 2026-11-09: keep_saved assumes a callee gives saved registers back, which this checker cannot know: never for a claim that counts)
    assert not keep_saved, 'keep_saved is not sound: measurement only, never for claims'
    if keep_saved:
        return {k: v for k, v in st.items() if k in SAVED or (k[0] == '@' and k[1:] in SAVED and v[1] in SAVED)}
    if RULE_P and f is not None and f['ins'].get(pc, ('',))[0] == 'jal':
        # (rule P: every function this call can run is proved to give s0-s7, fp and sp back -- preserves() above)
        g = call_targets(f, pc, P_ENTRIES)
        if g and all(x in PRESERVING for x in g):
            return {k: v for k, v in st.items() if k in SAVED or (k[0] == '#' and k[1:] in SAVED) or (k[0] == '@' and k[1:] in SAVED and v[1] in SAVED)}
    if RULE_C and f is not None and f['calls'].get(pc + 4) in TRUSTED_CALLEES:
        # (rule C: a trusted callee gives s0-s7 and fp back; the stack pointer too)
        keep = {k: v for k, v in st.items() if k in SAVED or (k[0] == '#' and k[1:] in SAVED) or (k[0] == '@' and k[1:] in SAVED and v[1] in SAVED)}
        return keep
    if RULE_S and st.get('#sp', 0) >= 3:
        return {'#sp': 3}       # (rule S: the stack pointer is still a multiple of 8 after the call; nothing else is known)
    return {}


def table_checked(f, jr):
    """The table is whole: the `sltiu $x, $y, N` test before it is for exactly the N entries listed, numbered from 0."""
    t = f['tables'][jr]
    for back in range(1, 25):
        ins = f['ins'].get(jr - 4 * back)
        if ins is None:
            return False
        if ins[0] == 'sltiu':
            n = int(re.search(r',\s*(-?(?:0[xX][0-9A-Fa-f]+|\d+))$', ins[1]).group(1), 0)
            return n == len(t) and sorted(t) == list(range(n))
    return False


def target(ops):
    m = re.search(r'(?:L_|0[xX])([0-9A-Fa-f]{8})\s*$', ops)
    return int(m.group(1), 16) if m else None


def analyse(f, roots=(), keep_saved=False):
    ins = f['ins']
    if not ins:
        return Result('no instructions', set(), {}, {})
    for a, (mn, ops) in ins.items():
        if mn in SIXTY_FOUR:
            return Result('a 64-bit instruction (%s)' % mn, set(), {}, {})
        if mn == 'jr' and ops.strip() != '$ra':
            if a not in f['tables']:
                return Result('a jump with no table (%08X)' % a, set(), {}, {})
            if not table_checked(f, a):
                return Result('a jump table its bounds test does not confirm (%08X)' % a, set(), {}, {})
    state, visits, blocked = {}, {}, {}
    all_targets = (branch_targets(f) | set(roots)) if RULE_T else set()
    work = [min(ins)] + [r for r in roots if r in ins and r != min(ins)]
    for r in work:
        state[r] = {'#sp': 3} if RULE_S else {}
    reached = set()

    def arrive(pc, st):
        if pc not in ins:
            return                  # (out of the function: a tail call)
        old = state.get(pc)
        new = st if old is None else join(old, st)
        if old is not None:
            for k in [k for k, v in new.items() if old.get(k) != v]:
                visits[(pc, k)] = visits.get((pc, k), 0) + 1
                if visits[(pc, k)] > WIDEN_AFTER:
                    del new[k]
        if old is None or new != old:
            state[pc] = new
            work.append(pc)

    while work:
        pc = work.pop()
        st = state[pc]
        reached.add(pc)
        mn, ops = ins[pc]
        ds = pc + 4
        if mn in BRANCH or mn in ('j', 'b', 'jr') or mn in CALLS:
            if ds not in ins:
                return Result('a jump with no instruction after it (%08X)' % pc, set(), {}, {})
            dmn, dops = ins[ds]
        if mn in BRANCH:
            likely = mn.endswith('l') and mn not in ('bgezal', 'bltzal')
            t = target(ops)
            for taken in (True, False):
                s = outcome(mn, ops, st, taken)
                dest = t if taken else ds + 4
                if s is None:
                    blocked.setdefault(dest, '%s at %08X %s: the way %s is impossible' % (mn, pc, ops, 'taken' if taken else 'not taken'))
                    continue
                if likely and not taken:
                    arrive(dest, s)
                    continue
                reached.add(ds)
                s = transfer(dmn, dops, s)
                if mn in CALLS:
                    # (review 2026-11-09: bgezal / bltzal taken is a call: it comes back after the delay slot, and a target in this function is
                    #  entered too. Not taken it goes on as a branch would.)
                    if taken:
                        if t is not None and t in ins:
                            arrive(t, s)
                        arrive(ds + 4, after_call(s, keep_saved, f, pc))
                        continue
                    s = after_call(s, keep_saved, f, pc)
                if dest in ins:
                    arrive(dest, s)
        elif mn in ('j', 'b', 'jr'):
            reached.add(ds)
            s = transfer(dmn, dops, st)
            if mn == 'jr':
                if ops.strip() != '$ra':
                    tab = f['tables'][pc]
                    y = table_index(f, pc, all_targets) if RULE_T else None
                    if y is not None:
                        # (rule T, proposed: only the entries the index can be -- its range BEFORE the delay slot, at the jr)
                        yv = get(st, y)
                        can = set(t for i, t in tab.items() if table_entry_possible(yv, i))
                        for t in sorted(set(tab.values()) - can):
                            blocked.setdefault(t, 'jr at %08X: no entry of the table that the index (%s: %d..%d) can be leads here' % (pc, y, yv[0], yv[1]))
                    else:
                        can = set(tab.values())
                    for t in sorted(can):
                        arrive(t, s)
            else:
                t = target(ops)
                if t is not None:
                    arrive(t, s)
        elif mn in ('jal', 'jalr'):
            reached.add(ds)
            s = transfer(dmn, dops, st)
            # (review 2026-11-09: a jal into this function's own body enters it there: follow it. A jalr's target comes from a register; an
            #  address of this function kept in data is already an extra way in, run.py's roots.)
            t = target(ops) if mn == 'jal' else None
            if t is not None and t in ins:
                arrive(t, s)
            arrive(ds + 4, after_call(s, keep_saved, f, pc) if mn == 'jal' else after_call(s, keep_saved))
        elif RULE_BREAK and mn == 'break':
            pass        # (rule "break ends the way": see RULE_BREAK)
        else:
            arrive(pc + 4, transfer(mn, ops, st))
    return Result(None, reached, blocked, state)


def analyse_paths(f, roots=(), keep_saved=False):
    """Rule "path ranges": analyse, with up to PATHS states kept apart at each instruction, and float compares' outcomes
    narrowing their sides (see RULE_RANGES)."""
    global _IN_PATHS
    _IN_PATHS = True
    try:
        return _analyse_paths(f, roots, keep_saved)
    finally:
        _IN_PATHS = False


def _analyse_paths(f, roots=(), keep_saved=False):
    ins = f['ins']
    if not ins:
        return Result('no instructions', set(), {}, {})
    for a, (mn, ops) in ins.items():
        if mn in SIXTY_FOUR:
            return Result('a 64-bit instruction (%s)' % mn, set(), {}, {})
        if mn == 'jr' and ops.strip() != '$ra':
            if a not in f['tables']:
                return Result('a jump with no table (%08X)' % a, set(), {}, {})
            if not table_checked(f, a):
                return Result('a jump table its bounds test does not confirm (%08X)' % a, set(), {}, {})
    paths, visits, blocked = {}, {}, {}
    all_targets = (branch_targets(f) | set(roots)) if RULE_T else set()
    starts = [min(ins)] + [r for r in roots if r in ins and r != min(ins)]
    work = []
    for r in starts:
        paths[r] = [{'#sp': 3} if RULE_S else {}]
        work.append((r, 0))
    reached = set()

    def arrive(pc, st):
        if pc not in ins:
            return                  # (out of the function: a tail call)
        kept = paths.setdefault(pc, [])
        if st in kept[:PATHS - 1]:
            return                  # (this very state is already kept apart here: nothing new)
        if len(kept) < PATHS - 1:
            kept.append(st)         # (a state of its own, never joined, never changed: one more way here, kept apart)
            work.append((pc, len(kept) - 1))
            return
        # (Every place for a state of its own is taken: the rest are joined into the last, as analyse joins them all.)
        old = kept[PATHS - 1] if len(kept) == PATHS else None
        new = st if old is None else join(old, st)
        if old is not None:
            for k in [k for k, v in new.items() if old.get(k) != v]:
                visits[(pc, k)] = visits.get((pc, k), 0) + 1
                if visits[(pc, k)] > WIDEN_AFTER:
                    del new[k]
        if old is None or new != old:
            if old is None:
                kept.append(new)
            else:
                kept[PATHS - 1] = new
            work.append((pc, PATHS - 1))

    while work:
        pc, which = work.pop()
        st = paths[pc][which]
        reached.add(pc)
        mn, ops = ins[pc]
        ds = pc + 4
        if mn in BRANCH or mn in ('j', 'b', 'jr') or mn in CALLS:
            if ds not in ins:
                return Result('a jump with no instruction after it (%08X)' % pc, set(), {}, {})
            dmn, dops = ins[ds]
        if mn in BRANCH:
            likely = mn.endswith('l') and mn not in ('bgezal', 'bltzal')
            t = target(ops)
            for taken in (True, False):
                s = outcome(mn, ops, st, taken)
                dest = t if taken else ds + 4
                if s is None:
                    blocked.setdefault(dest, '%s at %08X %s: the way %s is impossible' % (mn, pc, ops, 'taken' if taken else 'not taken'))
                    continue
                if likely and not taken:
                    arrive(dest, s)
                    continue
                reached.add(ds)
                s = transfer(dmn, dops, s)
                if mn in CALLS:
                    # (review 2026-11-09: bgezal / bltzal taken is a call: it comes back after the delay slot, and a target in this function is
                    #  entered too. Not taken it goes on as a branch would.)
                    if taken:
                        if t is not None and t in ins:
                            arrive(t, s)
                        arrive(ds + 4, after_call(s, keep_saved, f, pc))
                        continue
                    s = after_call(s, keep_saved, f, pc)
                if dest in ins:
                    arrive(dest, s)
        elif mn in ('j', 'b', 'jr'):
            reached.add(ds)
            s = transfer(dmn, dops, st)
            if mn == 'jr':
                if ops.strip() != '$ra':
                    tab = f['tables'][pc]
                    y = table_index(f, pc, all_targets) if RULE_T else None
                    if y is not None:
                        # (rule T, proposed: only the entries the index can be -- its range BEFORE the delay slot, at the jr)
                        yv = get(st, y)
                        can = {t for i, t in tab.items() if table_entry_possible(yv, i)}
                        for t in sorted(set(tab.values()) - can):
                            blocked.setdefault(t, 'jr at %08X: no entry of the table that the index (%s: %d..%d) can be leads here' % (pc, y, yv[0], yv[1]))
                    else:
                        can = set(tab.values())
                    for t in sorted(can):
                        arrive(t, s)
            else:
                t = target(ops)
                if t is not None:
                    arrive(t, s)
        elif mn in ('jal', 'jalr'):
            reached.add(ds)
            s = transfer(dmn, dops, st)
            # (review 2026-11-09: a jal into this function's own body enters it there: follow it. A jalr's target comes from a register; an
            #  address of this function kept in data is already an extra way in, run.py's roots.)
            t = target(ops) if mn == 'jal' else None
            if t is not None and t in ins:
                arrive(t, s)
            arrive(ds + 4, after_call(s, keep_saved, f, pc) if mn == 'jal' else after_call(s, keep_saved))
        elif mn == 'break' and RULE_BREAK:
            continue    # (as analyse: rule "break ends the way", RULE_BREAK)
        else:
            arrive(pc + 4, transfer(mn, ops, st))
    state = {}
    for pc, kept in paths.items():      # (what the other users of Result.states read: every way here, joined)
        j = kept[0]
        for x in kept[1:]:
            j = join(j, x)
        state[pc] = j
    return Result(None, reached, blocked, state)


def dead(f, roots=(), keep_saved=False):
    """(why refused or None, {dead address: reason})"""
    res = analyse(f, roots, keep_saved)
    if res.refused:
        return res.refused, {}
    out = {}
    addrs = sorted(f['ins'])
    for a in addrs:
        if a in res.reached:
            continue
        b = a
        while b in f['ins'] and b not in res.reached and b not in res.blocked:
            b -= 4
        out[a] = res.blocked.get(b, 'nothing leads here')
    if RULE_RANGES:
        # (Path ranges: and what analyse_paths proves too. Each is sound on its own, so an instruction either never reaches
        #  can never run; the union proves at least what analyse alone does -- the validation rule -- by construction.)
        more = analyse_paths(f, roots, keep_saved)
        if more.refused is None:
            for a in addrs:
                if a in more.reached or a in out:
                    continue
                b = a
                while b in f['ins'] and b not in more.reached and b not in more.blocked:
                    b -= 4
                out[a] = 'path ranges: ' + more.blocked.get(b, 'nothing leads here')
    return None, out
