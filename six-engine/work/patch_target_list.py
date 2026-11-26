"""The target list for any count of wrestlers -- the 32-slot work, 2026-11-06.

func_80120D98 (with a list pointer in a3) fills its caller's list: every other wrestler in the match (all the slots,
not only those in the ring), then props while the list has 10 entries or fewer (rw_prop_append), then -1. Its two
callers that keep the list keep it on their own stack at sp+0x10, room for 12 entries, their other locals from
sp+0x40 on: func_80126384 (frame 0xB8) and func_8012069C (frame 0x110). 12 entries hold 11 other wrestlers; the
16-slot probe had 15 -- the list ran over sp+0x40, func_80126384 stored positions there, then read one as a wrestler's
index (lhu at 0x80180000 + garbage - 0x8E): a crash at match frame 8754 with 12 in the ring.
Now (build_rom.step_frames) both frames grow for more than six slots: by spec.TARGET_LIST_GROW, room for
max(n - 1, 10) + 1 entries (32 for 32 slots: 0x50 more); the list stays at sp+0x10, and every other stack offset
(sp+0x40 on, the saves, the caller's argument slots above the frame) moves up with the frame. Each function's stack
accesses were checked by hand: base sp, offsets 0x10 (the list: addiu / lw, and `addu v0, sp, index; lw 0x10(v0)`) and
0x40 on -- nothing else. The six-slot build is unchanged.
Then the second run crashed in the same place: func_80120D98 sets only the list's first 11 entries to -1 and writes no
end after the wrestlers -- past 11 others the list had none. rw/stubs.S rw_tl_call (by hand, after this script) sets
entries 11.. up to the match's count to -1 before it, at its two list callers (build_rom SEQS)."""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


edit(os.path.join(W, 'spec.py'), [
    ("POOL_RECORDS = max(900, 300 + 96 * NSLOTS)\n",
     "POOL_RECORDS = max(900, 300 + 96 * NSLOTS)\n\n"
     "# func_80120D98's target list on its callers' stacks (func_80126384, func_8012069C: sp+0x10, room for 12 entries):\n"
     "# max(n - 1, 10) + 1 entries -- the frames grow by what's missing, 8-aligned (patch_target_list.py; 0 for six)\n"
     "TARGET_LIST_GROW = 0 if NSLOTS == 6 else max(0, (4 * (max(NSLOTS - 1, 10) + 1) - 0x30 + 7) & ~7)\n"),
])
edit(os.path.join(W, 'build_rom.py'), [
    ("STEPS = [step_arrays, step_main] if ORACLE else [step_arrays, step_main, step_bss, step_heap]",
     '''# ---- stack frames grown (a list on a function's stack with room for fewer than the slots; spec.py) -------------------
# (section, function, the first offset that moves (below it: the list, kept), how much): every instruction with base sp
# in the function -- loads, stores, addiu -- at that offset or more moves up; the frame's own `addiu sp, sp, -/+frame`
# by the same. (Each function's accesses checked by hand first: patch_target_list.py.)
FRAME_GROWS = [('ovl_e', 0x80126384, 0x40, spec.TARGET_LIST_GROW), ('ovl_e', 0x8012069C, 0x40, spec.TARGET_LIST_GROW)]
_SP_OPS = {0x09, 0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B, 0x31, 0x35, 0x39, 0x3D}   # addiu lb lh lw lbu lhu sb sh sw lwc1 ldc1 swc1 sdc1
def step_frames(rom):
    secs_, fn2sec_, insns_ = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
    n = 0
    for sec, fa, keep_below, grow in FRAME_GROWS:
        if not grow: continue
        sec_, va, size = fn2sec_['func_%08X' % fa]; assert va == fa and sec_ == sec
        frames = 0
        for a in range(fa, fa + size, 4):
            w = rom.r32(a, sec)
            op, rs, rt, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            if op not in _SP_OPS or rs != 29: continue
            simm = imm - 0x10000 if imm >= 0x8000 else imm
            if op == 0x09 and rt == 29:                     # addiu sp, sp, -frame / +frame
                new = simm - grow if simm < 0 else simm + grow; frames += 1
            elif simm >= keep_below:
                new = simm + grow
            else:
                continue
            assert -0x8000 <= new < 0x8000 and (op not in (0x35, 0x3D) or new % 8 == 0), (hex(a), new)
            rom.w32(a, sec, (w & 0xFFFF0000) | (new & 0xFFFF), w, 'func_%08X frame +0x%X' % (fa, grow))
            n += 1
        assert frames == 2, ('func_%08X' % fa, frames)    # (one prologue, one epilogue: checked)
    return n

STEPS = [step_arrays, step_main] if ORACLE else [step_arrays, step_main, step_bss, step_heap]'''),
    ("if os.environ.get('SIX_LOOPS') == 'gp': STEPS += [step_loops_gp, step_stubs, step_masks, step_rewrites, step_hud]",
     "if os.environ.get('SIX_LOOPS') == 'gp': STEPS += [step_loops_gp, step_stubs, step_masks, step_rewrites, step_hud, step_frames]"),
])
print('applied')
