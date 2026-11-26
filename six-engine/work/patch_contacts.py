"""The contact list for any count of wrestlers -- the 32-slot work, 2026-11-06.

func_800EBD2C fills its one caller's list (func_8012C474: on its stack at sp+0x30): entries of 0x28 bytes (+0 the
contact's id -- a wrestler's slot, or 100 + a prop -- +4.. what it found), the other wrestlers that pass its tests
(no limit), then props while fewer than 10 (rw_contact_cap); the -1s are only the first 11 entries' (it clears them
first), and entry 10 doubles as the owner's own record (+0xC..+0x28: list + 0x19C..+0x1B8). func_8012C474 walks
entries 0..9 (to list + 0x190). Past 10 other wrestlers the list ran over its owner record and the caller's stack
(its saves at sp+0x1F0..: the 16-slot probe's third crash, func_80131E08's s0 come back garbage).
Now, for more than six slots: CAP = spec.CONTACT_CAP = max(n - 1, 10) entries (31 for 32) and the owner entry after
them, in the engine's rw_contacts (rw/r_tables.c), not the stack -- 0x348 more bytes on a frame of 0x238 deep in
the match thread, whose stack's room couldn't be measured; func_8012C474 never calls itself (its callees' calls
followed: no way back), so one buffer is enough:
  - func_800EBD2C: its clear 11 -> CAP + 1 entries; the owner record's offsets (0x19C..0x1B4) + (CAP - 10) * 0x28;
  - func_8012C474: its list base (`addiu s0, sp, 0x30`) rw_contacts (rw_contacts_s0); its walk's end
    (`addiu v0, s6, 0x190`) CAP * 0x28; its two owner pointers (`addiu a0, sp, 0x1CC`, `addiu a1, sp, 0x1D8`) from
    s6, the list's base, at the owner's new place.
The six-slot build is unchanged."""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


edit(os.path.join(W, 'spec.py'), [
    ("TARGET_LIST_GROW = 0 if NSLOTS == 6",
     "# func_800EBD2C's contact list (patch_contacts.py): room for max(n - 1, 10) entries and the owner's after them\n"
     "CONTACT_CAP = max(NSLOTS - 1, 10)\n"
     "TARGET_LIST_GROW = 0 if NSLOTS == 6"),
])
edit(os.path.join(W, 'rw', 'r_tables.c'), [
    ("const u16 rw_tab_65C8[SIX_NSLOTS]",
     "/* func_800EBD2C's contact list, off func_8012C474's stack (patch_contacts.py): max(n - 1, 10) entries of 0x28 and\n"
     " * the owner's after them (8-aligned: it holds floats) */\n"
     "#define RW_CONTACT_CAP (SIX_NSLOTS - 1 > 10 ? SIX_NSLOTS - 1 : 10)\n"
     "u8 rw_contacts[(RW_CONTACT_CAP + 1) * 0x28] __attribute__((aligned(8)));\n\n"
     "const u16 rw_tab_65C8[SIX_NSLOTS]"),
])
edit(os.path.join(W, 'rw', 'stubs.S'), [
    ('''    .data
    .globl rw_seq_lock''', '''# func_8012C474: was `addiu s0, sp, 0x30` -- its contact list, the engine's rw_contacts (patch_contacts.py)
    .globl rw_contacts_s0
rw_contacts_s0:
    lui   $s0, %hi(rw_contacts)
    jr    $ra
    addiu $s0, $s0, %lo(rw_contacts)

    .data
    .globl rw_seq_lock'''),
])
edit(os.path.join(W, 'build_rom.py'), [
    ("FRAME_GROWS = [",
     '''# func_800EBD2C's contact list (patch_contacts.py): (address, stock word) -- the immediates set from spec.CONTACT_CAP
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

FRAME_GROWS = ['''),
    ("STEPS += [step_loops_gp, step_stubs, step_masks, step_rewrites, step_hud, step_frames]",
     "STEPS += [step_loops_gp, step_stubs, step_masks, step_rewrites, step_hud, step_frames, step_contacts]"),
])
print('applied')
