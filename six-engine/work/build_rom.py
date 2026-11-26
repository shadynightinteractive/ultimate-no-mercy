import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
# Builds the six-man ROM from the stock one: each step is a function of a Rom; run all in order.
import pickle, re, sys, os
sys.path.insert(0, nmp('/home/devbox/work/six'))
from rompatch import Rom
from luifind import find_lui, parse

refs = pickle.load(open(nmp('/home/devbox/work/six/refs.pkl'), 'rb'))
MATCH = ('ovl_e', 'main_1050')

def hilo(addr):
    lo = addr & 0xFFFF
    if lo >= 0x8000: lo -= 0x10000
    hi = ((addr - lo) >> 16) & 0xFFFF
    return hi, lo

def base_reg(asm):
    m = re.search(r'\((\$\w+)\)', asm)
    if m: return m.group(1)
    op, args = parse(asm)
    return args[1]

def move_global(rom, old_lo_addr, old_hi_addr, new_base, why, kinds=('direct', 'indexed', 'addr')):
    """Every reference to [old_lo_addr, old_hi_addr) moves to new_base + (ea - old_lo_addr):
    the memory op's (or addiu's) offset, and its lui when the high half changes."""
    done_lui = {}
    n = 0
    seen = set()
    for r in refs:
        ea, sec, fn, pc, op, asm, kind = r
        if (sec, pc) in seen: continue
        if sec not in MATCH or not (old_lo_addr <= ea < old_hi_addr) or kind not in kinds: continue
        new = new_base + (ea - old_lo_addr)
        oh, ol = hilo(ea); nh, nl = hilo(new)
        rb = base_reg(asm)
        lu, chain = find_lui(sec, fn, pc, rb)
        if lu is None: raise ValueError('no lui for %s %08x %s' % (fn, pc, asm))
        cur = rom.r32(lu, sec)
        if (cur & 0xFFFF) not in (oh, nh): raise ValueError('lui mismatch %08x' % lu)
        if nh != oh:
            if lu in done_lui and done_lui[lu] != nh: raise ValueError('shared lui %08x' % lu)
            if lu not in done_lui:
                rom.set_imm16(lu, sec, nh, why + ' hi'); done_lui[lu] = nh
        ins = rom.r32(pc, sec)
        imm = ins & 0xFFFF; imm = imm - 0x10000 if imm >= 0x8000 else imm
        if imm != ol: raise ValueError('imm mismatch %s %08x %s (%x vs %x)' % (fn, pc, asm, imm, ol))
        rom.set_imm16(pc, sec, nl, why)
        seen.add((sec, pc))
        n += 1
    return n

import spec
# rw_opp's halves (rw/r_tables.c RW_OPP_HALF): mode & 0x4000 shifted to that offset -- 11 gives 8 (up to eight
# slots, the six-man build as it was), 10 gives 16, 9 gives 32
OPP_SHIFT = 11 if spec.NSLOTS <= 8 else 10 if spec.NSLOTS <= 16 else 9
from reloc import Moves

ORACLE = '--oracle' in sys.argv
# The oracle (transplant.py) runs the stock game's match state with this ROM's code: the moved arrays go where the
# stock state has nothing (expansion RAM), and the heap / BSS / framebuffer steps (which only matter from power-on)
# are left out.
OVLE_NEW, _OVLE_END = spec.layout(spec.OVLE, 0x80400000, 0x80480000) if ORACLE else (spec.OVLE_NEW, spec.OVLE_USED)
EXTRA = spec.blocks(_OVLE_END, 0x80480000 if ORACLE else spec.OVLE_BSS_END)    # (the engine's own blocks: X_<name>)
if os.environ.get('SIX_ONLY'):        # (bisecting: only these arrays, by index in spec.OVLE)
    keep = set(int(x) for x in os.environ['SIX_ONLY'].split(','))
    OVLE_NEW = [x for i, x in enumerate(OVLE_NEW) if i in keep]

# the main segment's per-slot / per-model arrays (main BSS: every overlay's references move with them)
MAIN_NEW, _MAIN_END = spec.layout(spec.MAIN, 0x80480000, 0x80500000) if ORACLE else spec.layout(spec.MAIN, spec.EXP, spec.FB0, spec.NSLOTS, 'main')
EXTRA.update(spec.blocks(0x80700000, 0x80780000, spec.EXTRA_MAIN) if ORACLE else spec.blocks(_MAIN_END, spec.FB0, spec.EXTRA_MAIN))
# (the frame display lists: in the main arrays' room for six, at spec.DLBUF past it -- patch_dlbuf.py; X_dlbuf2 the second)
if 'dlbuf' not in EXTRA: EXTRA['dlbuf'] = spec.DLBUF
EXTRA['dlbuf2'] = EXTRA['dlbuf'] + spec.DLBUF_SIZE
if os.environ.get('SIX_MAIN') is not None:     # (bisecting: only these, by index in spec.MAIN; '' = none)
    keep = set(int(x) for x in os.environ['SIX_MAIN'].split(',') if x != '')
    MAIN_NEW = [x for i, x in enumerate(MAIN_NEW) if i in keep]

def step_main(rom):
    m = Moves()
    for base, stride, new, note in MAIN_NEW:
        m.add(base, base + 4 * stride, new, note)
    r = m.apply(rom)            # all sections
    if m.skipped: print('skipped (misfiled):', len(m.skipped))
    return r

def moves():
    m = Moves()
    for base, stride, new, note in OVLE_NEW:
        m.add(base, base + 4 * stride, new, note)
    return m

def step_arrays(rom):
    # ovl_e's own code, and the main segment's few functions that run in a match (the other overlays share these
    # addresses for their own data)
    m = moves()
    r = m.apply(rom, sections=('ovl_e', 'main_1050'))
    if m.skipped: print('skipped (misfiled):', len(m.skipped))
    return r

# ---- ovl_e's BSS grows (the overlay table's bss end), the heap starts after it ----------------------------------
def step_bss(rom):
    sec = 'main_1050'
    rom.w32(0x80052E2C, sec, spec.OVLE_BSS_END, spec.OVLE_BSS_END_OLD, 'ovl_e bss end')
    return 1

def lui_addiu(rom, sec, lui_pc, addiu_pc, value, why):
    hi, lo = hilo(value)
    rom.set_imm16(lui_pc, sec, hi, why + ' hi'); rom.set_imm16(addiu_pc, sec, lo, why + ' lo')

# ---- a bigger heap (models of two more wrestlers), the framebuffers to the expansion RAM's top --------------------
def step_heap(rom):
    sec = 'main_1050'
    size = spec.HEAP_END - spec.HEAP
    assert rom.r32(0x800007F8, sec) == 0x3C050022 and rom.r32(0x800007FC, sec) == 0x34A51800
    assert rom.r32(0x80000810, sec) == 0x3C020002 and rom.r32(0x80000814, sec) == 0x34422180
    # (0x800007F8/FC is only the boot's clear of the heap: left at the stock size, so the boot takes the time it did --
    #  the rest of the heap, in expansion RAM, is zero from power-on)
    u = size >> 4
    rom.set_imm16(0x80000810, sec, u >> 16, 'heap units hi'); rom.set_imm16(0x80000814, sec, u & 0xFFFF, 'heap units lo')
    # the heap's start: func_800007D4's references to 0x8016E000..0x8016E00C
    m = Moves(); m.add(0x8016E000, 0x8016E010, spec.HEAP, 'heap start')
    n = m.apply(rom, sections=('main_1050',))
    lui_addiu(rom, sec, 0x800031B8, 0x800031BC, spec.FB1, 'fb1')
    lui_addiu(rom, sec, 0x800031C8, 0x800031CC, spec.FB0, 'fb0')
    lui_addiu(rom, sec, 0x800033B8, 0x800033BC, spec.FB1, 'fb1 clear')
    lui_addiu(rom, sec, 0x800033DC, 0x800033E0, spec.FB0, 'fb0 clear')
    # the scene-record pool (func_80001340's heap block for func_8000176C's 0xB8-byte records): 900 -> spec.POOL_RECORDS
    # (`lui a0, 2; ... ori a0, a0, 0x86E0` its size for the allocation, `lui a1, 2; ori a1, a1, 0x86E0` for the clear)
    assert rom.r32(0x80001520, sec) == 0x3C040002 and rom.r32(0x80001528, sec) == 0x348486E0
    assert rom.r32(0x80001538, sec) == 0x3C050002 and rom.r32(0x80001540, sec) == 0x34A586E0
    pool = spec.POOL_RECORDS * 0xB8
    rom.set_imm16(0x80001520, sec, pool >> 16, 'record pool size hi'); rom.set_imm16(0x80001528, sec, pool & 0xFFFF, 'record pool size lo')
    rom.set_imm16(0x80001538, sec, pool >> 16, 'record pool clear hi'); rom.set_imm16(0x80001540, sec, pool & 0xFFFF, 'record pool clear lo')
    return size, n

# ---- the loops over slots / models: 4 -> 6 (kb/review/loops*.out PATCH lines, checked by loops.py) -----------------
def step_loops(rom):
    import loops
    n, problems = loops.apply(rom)
    if problems: raise ValueError('loop patches: %s' % problems[:3])
    return n

GP_SITES = [
    ('ovl_e', 0x800D9AE0, 0x24020004, 0x03801021, 'func_800D9AB0: 0x6000 match -> $gp wrestlers (addu $v0, $gp, $zero)'),
]
# ---- or the same loops bounded by $gp (the match's wrestler count; loops.apply_gp) -----------------------------------
def step_loops_gp(rom):
    import loops
    n, left = loops.apply_gp(rom)
    # $gp: set in the main thread (the only one that runs slot loops) at each overlay's entry: CALLS, rw_run_overlay
    # and the counts set from $gp: the match's wrestler count for a four-wrestler type (func_800D9AB0, 0x6000 -> 4)
    for sec_, site_, old_, new_, why_ in GP_SITES:
        rom.w32(site_, sec_, new_, old_, why_)
    print('loops by $gp:', n, '; left for rewrites:', len(left))
    for x in left: print('   ', x)
    return n

# ---- register stubs (rw/stubs.S): a site's two instructions -> `jal rw_X` with the first one in the delay slot ------
# (sec, site, its two instructions, stub). The function must have saved $ra (it is a call).
STUBS = [
    # the camera's per-slot sets (2026-11-06, patch_cam_wide.py): func_800E38A4's `andi s5, v0, 0xFFFF`, and
    # func_800E28C0's / func_800E30D8's framing set (`andi a3, a0, 0xFFFF` / `andi a0, a0, 0xFFFF`)
    ('ovl_e', 0x800E3D10, (0x00008821, 0x3055FFFF), 'rw_cam_view_s5'),
    ('ovl_e', 0x800E2984, (0x00002821, 0x3087FFFF), 'rw_cam_sel_a3'),
    ('ovl_e', 0x800E31A0, (0x250898D0, 0x3084FFFF), 'rw_cam_sel_a0'),
    # func_80117F28, the match's per-frame round-robin over the wrestlers: (s1 + 1) & 3 -> wrap at the count
    ('ovl_e', 0x80118084, (0x26220001, 0x30510003), 'rw_wrap_s1_v0'),
    ('ovl_e', 0x801180D4, (0x26220001, 0x30510003), 'rw_wrap_s1_v0'),
    ('ovl_e', 0x80118124, (0x26220001, 0x30510003), 'rw_wrap_s1_v0'),
    ('ovl_e', 0x8011819C, (0x26220001, 0x30510003), 'rw_wrap_s1_v0'),
    ('ovl_e', 0x801181AC, (0x26220001, 0x30420003), 'rw_wrap_v0_v0'),
    #   its state-history loop's end (0x8016B5A0 + 4 * 0x20)
    ('ovl_e', 0x801182C4, (0x00A03821, 0x24A90080), 'rw_t1_a1_n20'),
    # func_800DDB10 (HUD setup): the per-slot name textures' base, 0x801578C0 - 4 * 0x410, -> the engine's block
    ('ovl_e', 0x800DE4B4, (0x00009021, 0x2631EFC0), 'rw_namebuf_s1'),
    #   its texture-pair clear: byte offset < 4 * 8
    ('ovl_e', 0x800DDFA0, (0x24630008, 0x2C620020), 'rw_sltu_v0_v1_k8'),
    # the frame display list (func_80002BB4 starts it, func_80002DE4 ends it into the scheduler task): in a six-man
    # match it goes to the engine's dlbuf, not the first 12000 bytes of the 0x2F58 frame block (too few for six)
    ('main_1050', 0x80002BEC, (0x02429021, 0x8E52D8E0), 'rw_dlbase_s2'),
    ('main_1050', 0x80002EF8, (0x00621821, 0x8C620000), 'rw_dlbase_v0_v1'),
    ('main_1050', 0x80002F00, (0xAC820030, 0x8C620000), 'rw_dlbase_v0_v1'),
    # func_80115000: the rumble records loop, wrestler byte offset < 4 * 0x358
    ('ovl_e', 0x80115E14, (0xAC620018, 0x28820D60), 'rw_slt_v0_a0_k358'),
]
# sites whose two instructions both go: `jal stub; nop` (the stub does their work, conditionally)
STUBS2 = [
    # func_80120D98's target list (callers' 12-int stack lists, 11 cleared): up to 3 other wrestlers + 7 props +
    # the -1; with 5 others it would pass its end -- props are appended only while the list has 10 entries or fewer
    ('ovl_e', 0x80121160, (0xAE420000, 0x26520004), 'rw_prop_append'),
    # func_80138D10 (ovl_c, the results table's values): rw_colrec (the 4 records the table shows) is filled first
    ('ovl_c', 0x80138D6C, (0x00009021, 0x3C048015), 'rw_colrec_fill_s'),
    # func_8011D848, the sequence lock for any count (2026-11-06; rw/stubs.S rw_seq_take / rw_seq_release): its take,
    # `xori v0, a0, 0x300F; and v0, v1, v0`, and its give-back, `nor v1, zero, v1; and v0, v0, v1`
    ('ovl_e', 0x8011D8B8, (0x3882300F, 0x00621024), 'rw_seq_take'),
    ('ovl_e', 0x8011E0EC, (0x00031827, 0x00431024), 'rw_seq_release'),
    # the camera's per-slot sets (2026-11-06, patch_cam_wide.py): func_800E2344's `addu a1, v0, zero; addu s0, zero, zero`
    ('ovl_e', 0x800E23F4, (0x00402821, 0x00008021), 'rw_cam_view_a1'),
]
# whole sequences rewritten: (section, address, the stock word or None (an address step_main moved), new word or
# ('jal', stub))
SEQS = [
    # func_80126384 / func_8012069C: their func_80120D98 calls (a3 = the list on their stack) through rw_tl_call, which
    # ends the list for more than 11 others (patch_target_list.py) -- `jal 0x80120D98`
    ('ovl_e', 0x801263E8, 0x0C048366, ('jal', 'rw_tl_call')),
    ('ovl_e', 0x8012071C, 0x0C048366, ('jal', 'rw_tl_call')),
    # func_800E2344 / func_800E38A4 / func_800E28C0 / func_800E30D8: the camera's per-slot sets for any count
    # (2026-11-06, patch_cam_wide.py): the in-view set's copies made whole (`andi x, a1, 0xFFFF` -> `addu x, a1, zero`)
    ('ovl_e', 0x800E23FC, 0x30A6FFFF, 0x00A03021),     # addu a2, a1, zero
    ('ovl_e', 0x800E2504, 0x30B3FFFF, 0x00A09821),     # addu s3, a1, zero
    # func_800E2344's frame 0x48 -> 0x88: its in-view list (sp+0x10) had room for 8 before the saves
    ('ovl_e', 0x800E234C, 0x27BDFFB8, 0x27BDFF78),     # addiu sp, sp, -0x88
    ('ovl_e', 0x800E2350, 0xAFB50034, 0xAFB50074),     # sw s5
    ('ovl_e', 0x800E2358, 0xAFB20028, 0xAFB20068),     # sw s2
    ('ovl_e', 0x800E2360, 0xAFBF0038, 0xAFBF0078),     # sw ra
    ('ovl_e', 0x800E2364, 0xAFB40030, 0xAFB40070),     # sw s4
    ('ovl_e', 0x800E2368, 0xAFB3002C, 0xAFB3006C),     # sw s3
    ('ovl_e', 0x800E236C, 0xAFB10024, 0xAFB10064),     # sw s1
    ('ovl_e', 0x800E2370, 0xAFB00020, 0xAFB00060),     # sw s0
    ('ovl_e', 0x800E2374, 0xF7B40040, 0xF7B40080),     # sdc1 f20
    ('ovl_e', 0x800E2798, 0x8FBF0038, 0x8FBF0078),     # lw ra
    ('ovl_e', 0x800E279C, 0x8FB50034, 0x8FB50074),     # lw s5
    ('ovl_e', 0x800E27A0, 0x8FB40030, 0x8FB40070),     # lw s4
    ('ovl_e', 0x800E27A4, 0x8FB3002C, 0x8FB3006C),     # lw s3
    ('ovl_e', 0x800E27A8, 0x8FB20028, 0x8FB20068),     # lw s2
    ('ovl_e', 0x800E27AC, 0x8FB10024, 0x8FB10064),     # lw s1
    ('ovl_e', 0x800E27B0, 0x8FB00020, 0x8FB00060),     # lw s0
    ('ovl_e', 0x800E27B4, 0xD7B40040, 0xD7B40080),     # ldc1 f20
    ('ovl_e', 0x800E27BC, 0x27BD0048, 0x27BD0088),     # addiu sp, sp, 0x88
    # func_800E2344: the camera event word's (0x801594D2) wrestler, slot + 1 in bits 8..15 (func_800E4B60 reads all 8):
    # its "has a wrestler" test was `andi v0, v1, 0x700` -- slot 7 (8 << 8 = 0x800), 15, 23, 31 read as none, and their
    # events were dropped (2026-11-06, the 32-slot work) -> 0xFF00
    ('ovl_e', 0x800E26AC, 0x30620700, 0x3062FF00),
    # func_800E2344: the unrolled `pad[1] == 1 || pad[2] == 1 || pad[3] == 1` (controller 1 on another slot;
    # 0x800A6028 per-slot pads) -> rw_p1_other_v0 for slots 1..n-1, then the same two ways out
    ('ovl_e', 0x800E2464, None, ('jal', 'rw_p1_other_v0')),
    ('ovl_e', 0x800E2468, None, 0),
    ('ovl_e', 0x800E246C, 0x24030001, 0x1440000A),     # bne v0, zero, 0x800E2498
    ('ovl_e', 0x800E2470, 0x10430009, 0),
    ('ovl_e', 0x800E2474, 0x00000000, 0x0803893F),     # j 0x800E24FC
    ('ovl_e', 0x800E2478, None, 0x3242FFFF),           # (delay) andi v0, s2, 0xFFFF -- the old way out's delay slot
    # func_800E0A0C: mode 0x4000's number panels, one per wrestler: 4 -> n
    ('ovl_e', 0x800E0AB4, 0x240C0004, 0x03806021),     # addu t4, gp, zero
    # func_800F2BBC: "all the others have +0x82 & 4" compares the count with 3 -> n - 1
    ('ovl_e', 0x800F2C9C, 0x24020003, 0x2782FFFF),     # addiu v0, gp, -1
    # func_800DDB10 (HUD setup): modes 0x6000 show a panel per wrestler: 4 -> n
    ('ovl_e', 0x800DDB70, 0x24020004, 0x03801021),     # (delay) addu v0, gp, zero
    # func_80115000: Royal Rumble without 0x4000000 starts with all the wrestlers entered (0x80159214): 4 -> n
    ('ovl_e', 0x80115B88, 0x24020004, 0x03801021),     # addu v0, gp, zero
    # func_80115000, modes 0xC000: the first target, (mode 0x4000 ? slot ^ 3 : 0x80146470[slot]) ->
    # rw_opp[(mode & 0x4000 ? 8 : 0) + slot] (rw/r_tables.c); v0 = mode & 0x4000 here
    ('ovl_e', 0x80115730, 0x14400004, 0x00021002 | (OPP_SHIFT << 6)),   # srl v0, v0, OPP_SHIFT (11: 8)
    ('ovl_e', 0x80115734, 0x3A220003, 0x00511021),     # addu v0, v0, s1
    ('ovl_e', 0x80115738, 0x3C028014, ('hi', 'rw_opp', 0x3C010000)),   # lui at, %hi(rw_opp)
    ('ovl_e', 0x8011573C, 0x00511021, 0x00411021),     # addu v0, v0, at
    ('ovl_e', 0x80115740, 0x80426470, ('lo', 'rw_opp', 0x80420000)),   # lb v0, %lo(rw_opp)(v0)
    # func_8013486C (ovl_c, the Royal Rumble's setup): entrants 4 and 5 into slots 4 and 5 and the match's count
    # (rw/r_menu.c rw_rumble_fill), on the way out: its `jr ra` -> `j rw_rumble_fill` (a tail call: the delay slot's
    # stack pop stays, and rw_rumble_fill returns to the caller)
    ('ovl_c', 0x80134C6C, 0x03E00008, ('j', 'rw_rumble_fill')),
    # func_80113E30: the computer levels 0x8009ED10[n-1..0] = 3, counted down from n - 1 (loops: `addiu s0, gp, -1`)
    # with the pointer from element 3 down -- the pointer now goes up from element 0 (the same stores, ascending)
    ('ovl_e', 0x80113F54, None, ('hi_arr', 0x8009ED10, 0x3C020000)),    # lui v0, %hi(levels)
    ('ovl_e', 0x80113F58, None, ('lo_arr', 0x8009ED10, 0x24420000)),    # addiu v0, v0, %lo(levels)
    ('ovl_e', 0x80113F68, 0x2442FFFE, 0x24420002),                     # (delay) addiu v0, v0, 2
    # func_800EE69C (ovl_a, a match's set-up): the run-in entry conditions 0x800AA078[slot - 2] for slots 2, 3 go
    # through a pointer that starts at element -2 (0x800AA070, slot 0) -- the moved array's base - 8
    ('ovl_a', 0x800EE824, 0x3C12800B, ('hi_arr', 0x800AA078, 0x3C120000, -8)),   # lui s2, %hi(conds - 8)
    ('ovl_a', 0x800EE828, 0x2652A070, ('lo_arr', 0x800AA078, 0x26520000, -8)),   # addiu s2, s2, %lo(conds - 8)
    # func_8010A19C (ovl_c, the title change after a Royal Rumble): the winner's roster id 0x8016B920[slot] is ovl_e's,
    # read after the match from what ovl_e left (past ovl_c's BSS) -- its moved place (also past it: 0x8016DA30)
    ('ovl_c', 0x8010A214, 0x3C058017, ('hi_arr', 0x8016B920, 0x3C050000)),   # lui a1, %hi(roster ids)
    ('ovl_c', 0x8010A21C, 0x94A5B920, ('lo_arr', 0x8016B920, 0x94A50000)),   # lhu a1, %lo(roster ids)(a1)
    # The results table (ovl_c, scr 28) shows 4 columns from the per-slot records 0x800A3DC0 (0x1C each): the column
    # headers func_8011FD1C (+0x18 the entrant, +0/+1), the values func_80138D10 (copies all of them) and the panels
    # func_801401BC (+0/+1) -- they read rw_colrec instead, the 4 records the table shows (rw/r_results.c: after a
    # six-man Rumble the winner and then the three who lasted longest; else records 0..3 as they are), and the
    # winner/loser stamp by column is rw_col_result (func_8010AED8 for the slot the column shows)
    ('ovl_c', 0x8011FD70, None, ('hi', 'rw_colrec', 0x3C150000, 0x18)),  # lui s5, %hi(colrec + 0x18)
    ('ovl_c', 0x8011FD78, None, ('lo', 'rw_colrec', 0x8EB50000, 0x18)),  # lw s5, %lo(colrec + 0x18)(s5)
    ('ovl_c', 0x801200F4, None, ('hi', 'rw_colrec', 0x3C020000, 0)),     # lui v0, %hi(colrec)
    ('ovl_c', 0x801200FC, None, ('lo', 'rw_colrec', 0x90420000, 0)),     # lbu v0, %lo(colrec)(v0)
    ('ovl_c', 0x80120110, None, ('hi', 'rw_colrec', 0x3C020000, 1)),
    ('ovl_c', 0x80120118, None, ('lo', 'rw_colrec', 0x90420000, 1)),     # lbu v0, %lo(colrec + 1)(v0)
    ('ovl_c', 0x80138D78, None, ('hi', 'rw_colrec', 0x3C030000, 0)),     # lui v1, %hi(colrec)
    ('ovl_c', 0x80138D7C, None, ('lo', 'rw_colrec', 0x24630000, 0)),     # addiu v1, v1, %lo(colrec)
    ('ovl_c', 0x80138F14, None, ('hi', 'rw_colrec', 0x3C020000, 0)),
    ('ovl_c', 0x80138F1C, None, ('lo', 'rw_colrec', 0x90420000, 0)),
    ('ovl_c', 0x80138F28, None, ('hi', 'rw_colrec', 0x3C020000, 1)),
    ('ovl_c', 0x80138F30, None, ('lo', 'rw_colrec', 0x90420000, 1)),
    ('ovl_c', 0x801402B0, None, ('hi', 'rw_colrec', 0x3C020000, 0)),
    ('ovl_c', 0x801402B8, None, ('lo', 'rw_colrec', 0x90420000, 0)),
    ('ovl_c', 0x801402F4, None, ('hi', 'rw_colrec', 0x3C020000, 1)),
    ('ovl_c', 0x801402FC, None, ('lo', 'rw_colrec', 0x90420000, 1)),
    ('ovl_c', 0x8011FED4, 0x0C042BB6, ('jal', 'rw_col_result')),
    ('ovl_c', 0x80138EFC, 0x0C042BB6, ('jal', 'rw_col_result')),
    # func_8011723C (an elimination: is the Rumble over?), mode 0x4000000: "at least n - 1 slots are their team's
    # last" was `count < 3`: the count starts at 1 and `count < n`
    ('ovl_e', 0x801172EC, 0x00002821, 0x24050001),     # (delay) addiu a1, zero, 1  (the other way resets a1)
    ('ovl_e', 0x80117320, 0x28A20003, 0x00BC102A),     # slt v0, a1, gp
    # func_800E4B60 (camera): the event word's focus slot ((w >> 8 & 0xFF) - 1) is a wrestler below 4 -> below n
    ('ovl_e', 0x800E4B88, 0x2E220004, 0x023C102B),     # sltu v0, s1, gp
    # 0x8016B650's per-model lock bits are 0xF and 0x3000 (rw_tab_65C8): func_8011D848 "someone else holds it",
    # func_80115000's release of all
    ('ovl_e', 0x8011D8B8, 0x3882000F, 0x3882300F),     # xori v0, a0, 0x300F
    ('ovl_e', 0x80115350, 0x3042FFF0, 0x3042CFF0),     # andi v0, v0, 0xCFF0
    # func_8013140C (playback), the end: `(w & 0xFFEFFFFF) | 0xF8` sets every slot's snap bit -> with 4 and 5's
    ('ovl_e', 0x80131B58, 0x00431024, ('jal', 'rw_snap_v0')),
    ('ovl_e', 0x80131B5C, 0x344200F8, 0x00431024),     # (delay) and v0, v0, v1
    # func_800EBD2C (contact list, callers' 10 entries + the -1 at the 11th): the props' loop appends only while
    # the list has fewer than 10 (3 others + 7 props fit for four; 5 others can't all have 7 props): its distance
    # test `c.lt.d f0, f2` is false once the list is full: rw_contact_cap makes f0 = f2 then (the compare stays in
    # the function -- the PC port keeps the FP condition per function)
    ('ovl_e', 0x800EC034, 0x46000021, ('jal', 'rw_contact_cap')),
    ('ovl_e', 0x800EC038, 0x4622003C, 0x46000021),     # (delay) cvt.d.s f0, f0
    ('ovl_e', 0x800EC03C, 0x00000000, 0x4622003C),     # c.lt.d f0, f2
]
# The HUD's per-corner tables (rw/hud.c: 6 corners) and the corner masks that index them (`andi ..., 3` -> 7):
# (stock table, its element size); the code's references move to rw_hud_<addr>
HUD_TABLES = [(0x80144B78, 2), (0x80144B80, 2), (0x80144BD0, 2), (0x80144BD8, 2), (0x80144BE0, 2), (0x80144BE8, 2),
              (0x80144BF0, 2), (0x80144BF8, 2), (0x80144C00, 4), (0x80144C10, 2),
              (0x80144B90, 2)]     # (func_800E0A0C, mode 0x4000: panel i at B80[i], B90[i])
HUD_CORNER_MASKS = [0x800DEC08, 0x800DEC20, 0x800DEC9C, 0x800DECD8, 0x800DED38,       # func_800DEAB0 (layout)
                    0x800DF7A0, 0x800DF7BC, 0x800DFAFC, 0x800DFB18, 0x800DFB3C, 0x800DFB70]   # func_800DEDE0 (frame)
# Per-slot tables of 4 the code indexes by slot (rw/r_tables.c, 6 entries): (stock table, element size)
SLOT_TABLES = [(0x80146440, 4), (0x8014636C, 4), (0x80146450, 4),     # func_80115000: match flags & tab[slot]
               (0x80146558, 4),                                       # func_80115E54: start (x, z), modes 0xC000
               (0x8014DB60, 4), (0x8014DB70, 2), (0x8014DB78, 4),     # the instant replay's slot bits
               (0x801465C8, 2),                                       # 0x8016B650's lock bit by model
               (0x80146350, 4, 0xC)]     # camera event slot field (only refs below +0xC: func_801163C4's
                                         # table of shorts at 0x8014635E starts inside its last word)
def step_hud(rom):
    syms = stub_syms(); m = Moves()
    for t, sz in HUD_TABLES: m.add(t, t + 4 * sz, syms['rw_hud_%X' % (t & 0xFFF)], 'HUD corner table %08x' % t)
    for t, sz, *span in SLOT_TABLES: m.add(t, t + (span[0] if span else 4 * sz), syms['rw_tab_%X' % (t & 0xFFFF)], 'per-slot table %08x' % t)
    r = m.apply(rom, sections=('ovl_e',))
    if m.skipped: raise ValueError('HUD table refs skipped: %s' % m.skipped[:4])
    for a in HUD_CORNER_MASKS:
        w = rom.r32(a, 'ovl_e'); assert (w >> 26) == 0x0C and w & 0xFFFF == 3, (hex(a), hex(w))
        rom.w32(a, 'ovl_e', (w & 0xFFFF0000) | 7, w, 'HUD corner & 7')
    return r, len(HUD_CORNER_MASKS)
def oracle_elems():
    """(name, stock address, six-man address, bytes) for data the six-man game keeps in another shape: the oracle
    compares these one by one (the hit-stat matrices: rw/six.h STAT)"""
    out = []
    if 'statmat' in EXTRA:
        for t in range(3):
            for a in range(4):
                for c in range(4):
                    out.append(('hit stats %d [%d][%d]' % (t, a, c), 0x8009FDF8 + t * 0x20 + a * 8 + c * 2, EXTRA['statmat'] + (t * spec.NSLOTS * spec.NSLOTS + a * spec.NSLOTS + c) * 2, 2))
    return out
def blob_dir(lay):
    """the engine's blob for this layout: rw/out/<layout>, or SIX_BLOB (a wider build's own, rw/build.py full DIR)"""
    return os.environ.get('SIX_BLOB') or nmp('/home/devbox/work/six/rw/out/%s' % lay)
def stub_syms():
    lay = 'oracle' if ORACLE else 'full'
    return {nm: int(a, 16) for a, t, nm in (l.split() for l in open(os.path.join(blob_dir(lay), 'blob.syms')))}
# calls that go to a stub instead (the delay slot stays): (section, site, the old target, stub)
CALLS = [('main_1050', 0x800007C0, 0x800D9960, 'rw_run_overlay')]   # func_800007AC: every overlay's entry
def step_stubs(rom):
    syms = stub_syms()
    for sec, site, old_, stub in CALLS:
        rom.w32(site, sec, 0x0C000000 | ((syms[stub] >> 2) & 0x3FFFFFF), 0x0C000000 | ((old_ >> 2) & 0x3FFFFFF), 'jal ' + stub)
    for sec, a, old_, new_ in SEQS:
        if not isinstance(new_, tuple): w = new_
        elif new_[0] == 'jal': w = 0x0C000000 | ((syms[new_[1]] >> 2) & 0x3FFFFFF)
        elif new_[0] == 'j': w = 0x08000000 | ((syms[new_[1]] >> 2) & 0x3FFFFFF)
        elif new_[0] == 'hi': w = new_[2] | hilo(syms[new_[1]] + (new_[3] if len(new_) > 3 else 0))[0]
        elif new_[0] == 'lo': w = new_[2] | (hilo(syms[new_[1]] + (new_[3] if len(new_) > 3 else 0))[1] & 0xFFFF)
        elif new_[0] == 'hi_arr': w = new_[2] | hilo(MOVED[new_[1]] + (new_[3] if len(new_) > 3 else 0))[0]
        elif new_[0] == 'lo_arr': w = new_[2] | (hilo(MOVED[new_[1]] + (new_[3] if len(new_) > 3 else 0))[1] & 0xFFFF)
        # (hi_arr/lo_arr: a moved array's new base, plus a bias for a base the code keeps below the array)
        assert old_ is None or rom.r32(a, sec) == old_, (hex(a), hex(rom.r32(a, sec)))
        rom.w32(a, sec, w, rom.r32(a, sec), 'sequence rewrite')
    for sec, site, (i0, i1), stub in STUBS2:
        assert rom.r32(site, sec) == i0 and rom.r32(site + 4, sec) == i1, (hex(site), hex(rom.r32(site, sec)), hex(rom.r32(site + 4, sec)))
        rom.w32(site, sec, 0x0C000000 | ((syms[stub] >> 2) & 0x3FFFFFF), i0, 'jal ' + stub)
        rom.w32(site + 4, sec, 0, i1, 'stub delay slot')
    for sec, site, (i0, i1), stub in STUBS:
        assert rom.r32(site, sec) == i0 and rom.r32(site + 4, sec) == i1, (hex(site), hex(rom.r32(site, sec)), hex(rom.r32(site + 4, sec)))
        rom.w32(site, sec, 0x0C000000 | ((syms[stub] >> 2) & 0x3FFFFFF), i0, 'jal ' + stub)
        rom.w32(site + 4, sec, i0, i1, 'stub delay slot')
    return len(STUBS) + len(STUBS2) + len(CALLS) + len(SEQS)

# The camera's slot mask (func_80116130, rewritten: rw/r_80116130.c) has the six-slot layout -- slots 0x3F, out of
# play 0xFC0, flags 0x1000/0x2000 -- for any count; its consumers' masks: (section, addr, old imm, new imm, why)
MASK_IMMS = [
    ('ovl_e', 0x800DEE38, 0xF, 0x3F, 'func_800DEDE0: camera mask & slots'),
    ('ovl_e', 0x800E2958, 0xF, 0x3F, 'func_800E28C0: mask & slots'),
    ('ovl_e', 0x800E2964, 0x3C0, 0xFC0, 'func_800E28C0: ^ out-of-play'),
    ('ovl_e', 0x800E2970, 0xF, 0x3F, 'func_800E28C0: in play & slots'),
    ('ovl_e', 0x800E2978, 0xF, 0x3F, 'func_800E28C0: none -> all slots'),
    ('ovl_e', 0x800E315C, 0x3C0, 0xFC0, 'func_800E30D8: ^ out-of-play'),
    ('ovl_e', 0x800E3174, 0xF, 0x3F, 'func_800E30D8: mask & slots'),
    ('ovl_e', 0x800E3178, 0x3C0, 0xFC0, 'func_800E30D8: ^ out-of-play'),
    ('ovl_e', 0x800E3184, 0xF, 0x3F, 'func_800E30D8: in play & slots'),
    ('ovl_e', 0x800E318C, 0xF, 0x3F, 'func_800E30D8: none -> all slots'),
    # func_800EE130's two masks (rewritten: rw/r_800EE130.c) at bits 6-11 and 0-5
    ('ovl_e', 0x800DFCCC, 0x11, 0x41, 'func_800DEDE0: func_800EE130 mask, 0x41 << slot'),
]
# Contact ids (func_800EBD2C's list, func_8012BF50's result): a wrestler's slot, or 100 + a prop's index; "is a
# wrestler" was `id < 4` -- now `id < 100`, the same for four and right for six: (section, addr, why)
WRESTLER_ID_TESTS = [
    ('ovl_e', 0x800E758C, 'func_800E74B8: func_8012BF50 hit a wrestler'),
    ('ovl_e', 0x800EAE40, 'func_800EAD70: func_8012BF50 hit a wrestler'),
    ('ovl_e', 0x800EAFB8, 'func_800EAEDC: func_8012BF50 hit a wrestler'),
    ('ovl_e', 0x80123EF0, 'func_80123BD4: func_8012BF50 hit a wrestler'),
    ('ovl_e', 0x8011C538, 'func_8011C510: func_8012BF50 hit a wrestler'),
    ('ovl_e', 0x8012C544, 'func_8012C474: contact is a wrestler'),
    ('ovl_e', 0x8012C6F8, 'func_8012C474: contact is a wrestler'),
    ('ovl_e', 0x801445FC, 'func_801445F8: the sound at the hitter (a wrestler) else model 0'),
]
def step_masks(rom):
    for sec, a, why in WRESTLER_ID_TESTS:
        w = rom.r32(a, sec); assert (w >> 26) in (0x0A, 0x0B) and w & 0xFFFF == 4, (hex(a), hex(w))   # slti/sltiu
        rom.w32(a, sec, (w & 0xFFFF0000) | 100, w, why)
    for sec, a, old_, new_, why in MASK_IMMS:
        w = rom.r32(a, sec); assert w & 0xFFFF == old_ and (w >> 26) in (0x09, 0x0C, 0x0D, 0x0E), (hex(a), hex(w))   # addiu/andi/ori/xori
        rom.w32(a, sec, (w & 0xFFFF0000) | new_, w, why)
    return len(MASK_IMMS) + len(WRESTLER_ID_TESTS)
# The rewritten functions (rw/r_XXXXXXXX.c, in the blob): each game function's first two instructions become
# `j rw_XXXXXXXX; nop`
def step_rewrites(rom):
    n = 0
    for nm, a in sorted(stub_syms().items()):
        if not re.fullmatch(r'rw_[0-9A-F]{8}', nm): continue
        fa = int(nm[3:], 16); sec = fn_sec(fa)
        rom.w32(fa, sec, 0x08000000 | ((a >> 2) & 0x3FFFFFF), rom.r32(fa, sec), 'j ' + nm)
        rom.w32(fa + 4, sec, 0, rom.r32(fa + 4, sec), 'rewrite hook delay slot')
        n += 1
    return n
def fn_sec(fa):
    secs_, fn2sec_, insns_ = pickle.load(open(nmp('/home/devbox/work/six/db.pkl'), 'rb'))
    sec, va, size = fn2sec_['func_%08X' % fa]; assert va == fa
    return sec
# ---- stack frames grown (a list on a function's stack with room for fewer than the slots; spec.py) -------------------
# (section, function, the first offset that moves (below it: the list, kept), how much): every instruction with base sp
# in the function -- loads, stores, addiu -- at that offset or more moves up; the frame's own `addiu sp, sp, -/+frame`
# by the same. (Each function's accesses checked by hand first: patch_target_list.py.)
# func_800EBD2C's contact list (patch_contacts.py): (address, stock word) -- the immediates set from spec.CONTACT_CAP
# past six slots: the clear's count and start, the owner record's offsets, func_8012C474's walk end and owner pointers
def _contact_words():
    cap = spec.CONTACT_CAP; d = (cap - 10) * 0x28
    out = [(0x800EBD48, 0x2412000A, cap), (0x800EBD4C, 0x26A20190, cap * 0x28)]
    for a, w in ((0x800EBDAC, 0x26A5019C), (0x800EBDC4, 0xAEA001A0), (0x800EBDC8, 0xAEA201B4), (0x800EBDCC, 0xE6A0019C),
                 (0x800EBDD0, 0xE6A001A4), (0x800EBDE0, 0xAEA201A8), (0x800EBDE4, 0xAEA301AC), (0x800EBDE8, 0xAEA401B0),
                 (0x800EBF04, 0x26A401A8), (0x800EBF54, 0x26A401A8), (0x800EC074, 0x26A401A8)):
        out.append((a, w, (w & 0xFFFF) + d))
    out.append((0x8012C7AC, 0x26C20190, cap * 0x28))                       # addiu v0, s6, 0x190: the walk's end
    out.append((0x8012C51C, 0x27A401CC, 0x26C40000 | (0x19C + d)))         # addiu a0, sp, 0x1CC -> a0, s6, owner
    out.append((0x8012C534, 0x27A501D8, 0x26C50000 | (0x1A8 + d)))         # addiu a1, sp, 0x1D8 -> a1, s6, owner + 0xC
    return out
def step_contacts(rom):
    if spec.NSLOTS == 6: return 0
    n = 0
    for a, w, new in _contact_words():
        new = new if new > 0xFFFF else (w & 0xFFFF0000) | new
        rom.w32(a, 'ovl_e', new, w, 'contact list (patch_contacts.py)'); n += 1
    sym = stub_syms()['rw_contacts_s0']      # func_8012C474: `addu a0, s1, zero; addiu s0, sp, 0x30` -> jal; addu
    rom.w32(0x8012C4D0, 'ovl_e', 0x0C000000 | ((sym >> 2) & 0x3FFFFFF), 0x02202021, 'jal rw_contacts_s0')
    rom.w32(0x8012C4D4, 'ovl_e', 0x02202021, 0x27B00030, 'its delay slot')
    return n + 2

# func_800E5890's `F |= DB60[slot]` (patch_replay_bits.py): `addu a0, a0, v0; lw a0, <rw_tab_DB60>(a0)` -> jal
# rw_rp_set_a0; nop -- past six slots, after the tables have moved (the load checked by its form: the table's place is
# the blob's)
def step_replay(rom):
    if spec.NSLOTS == 6: return 0
    assert rom.r32(0x800E5BD0, 'ovl_e') == 0x00822021, hex(rom.r32(0x800E5BD0, 'ovl_e'))
    w = rom.r32(0x800E5BD4, 'ovl_e'); assert w & 0xFFFF0000 == 0x8C840000, hex(w)
    sym = stub_syms()['rw_rp_set_a0']
    rom.w32(0x800E5BD0, 'ovl_e', 0x0C000000 | ((sym >> 2) & 0x3FFFFFF), 0x00822021, 'jal rw_rp_set_a0')
    rom.w32(0x800E5BD4, 'ovl_e', 0, w, 'its delay slot')
    # the replay's sample rings (func_80113E30's block, 0x8016B57C: one 0x1C20 ring of 600 samples a wrestler and the
    # props' 7 -- rw/r_801308FC.c; stock 0x2BF20, room for 18 wrestlers): (n + 7) rings past that. Only the rings use
    # the block (func_801144E4 frees it). `lui a0, 2` (a branch's delay slot) / `ori a0, a0, 0xBF20` (the call's).
    size = max(0x2BF20, (spec.NSLOTS + 7) * 0x1C20)
    assert rom.r32(0x80113E7C, 'ovl_e') == 0x3C040002 and rom.r32(0x80113E84, 'ovl_e') == 0x3484BF20
    rom.set_imm16(0x80113E7C, 'ovl_e', size >> 16, 'replay rings size hi')
    rom.set_imm16(0x80113E84, 'ovl_e', size & 0xFFFF, 'replay rings size lo')
    return 4

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

STEPS = [step_arrays, step_main] if ORACLE else [step_arrays, step_main, step_bss, step_heap]
if os.environ.get('SIX_LOOPS') == '1': STEPS.append(step_loops)
if os.environ.get('SIX_LOOPS') == 'gp': STEPS += [step_loops_gp, step_stubs, step_masks, step_rewrites, step_hud, step_frames, step_contacts, step_replay]
RELOC = [(b, b + 4 * s, n) for b, s, n, _ in OVLE_NEW + MAIN_NEW]
MOVED = {b: n for b, s, n, _ in OVLE_NEW + MAIN_NEW}

if __name__ == '__main__':
    rom = Rom(nmp('/home/devbox/work/nomercy.z64'))
    for st in STEPS: print(st.__name__, st(rom))
    out = os.environ.get('SIX_OUT') or nmp('/home/devbox/work/six/nomercy6%s.z64') % ('_oracle' if ORACLE else '')
    rom.save(out)
    # the blob this ROM's stub and rewrite calls go to, kept with it (ROM.blob.bin / .blob.syms)
    import shutil
    lay = 'oracle' if ORACLE else 'full'
    for ext in ('bin', 'syms'): shutil.copy(os.path.join(blob_dir(lay), 'blob.' + ext), out + '.blob.' + ext)
    print(len(rom.log), 'edits')
