"""The scene-record pool for any count of wrestlers -- the 32-slot work, 2026-11-06.

func_8000176C hands out 0xB8-byte records (the scene's bones and objects: func_800016E4 / func_80001738 /
func_80001684 chain them onto a model) from one heap block, *0x80060B90, counting at 0x800619B0 -- no check of its
end. func_80001340 makes the block once at boot: 0x286E0 bytes, 900 records, allocated right below the two 0x2F58
frame blocks (the heap gives from its top). What a match uses: 504 with 4 wrestlers, about 645 with 6, about 968 with
10 (QA runs' RAM, 2026-11-06) -- about 72 a wrestler. The 10-man Rumble ran 68 past the end: frame block 1's heap
header and scheduler task written over (the write-watch: func_8000D284 -> func_800016E4 -> func_8000176C writing
0x804AA070, block 1's header), its reply queue garbage, the graphics thread's osSendMesg crashed.
Now the block is spec.POOL_RECORDS records: 300 + 96 a slot, never fewer than the stock 900 (so the six-slot build is
unchanged): 3372 for 32 (606 KB of the 3 MB heap at boot; the arcade's heap regions add 264 MB besides).
The frame's draw list 0x80060BA0 (900 u32, count 0x800619B2; about 47 a wrestler: 633 with 10) is the next limit,
about 19 wrestlers: not moved here (the mod's announce.c and tron.c read it at its stock address)."""
import os

W = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='\n').write(s)


edit(os.path.join(W, 'spec.py'), [
    ("# ---- more than six slots (SIX_SLOTS) ----",
     "# The scene-record pool (func_8000176C's 0xB8-byte records, one heap block made at boot by func_80001340; stock 900):\n"
     "# about 72 a wrestler (504 with 4, ~645 with 6, ~968 with 10) -- 300 + 96 a slot, never fewer than the stock 900\n"
     "# (patch_record_pool.py has the story)\n"
     "POOL_RECORDS = max(900, 300 + 96 * NSLOTS)\n\n"
     "# ---- more than six slots (SIX_SLOTS) ----"),
])
edit(os.path.join(W, 'build_rom.py'), [
    ("    lui_addiu(rom, sec, 0x800033DC, 0x800033E0, spec.FB0, 'fb0 clear')\n    return size, n",
     "    lui_addiu(rom, sec, 0x800033DC, 0x800033E0, spec.FB0, 'fb0 clear')\n"
     "    # the scene-record pool (func_80001340's heap block for func_8000176C's 0xB8-byte records): 900 -> spec.POOL_RECORDS\n"
     "    # (`lui a0, 2; ... ori a0, a0, 0x86E0` its size for the allocation, `lui a1, 2; ori a1, a1, 0x86E0` for the clear)\n"
     "    assert rom.r32(0x80001520, sec) == 0x3C040002 and rom.r32(0x80001528, sec) == 0x348486E0\n"
     "    assert rom.r32(0x80001538, sec) == 0x3C050002 and rom.r32(0x8000153C, sec) == 0x34A586E0\n"
     "    pool = spec.POOL_RECORDS * 0xB8\n"
     "    rom.set_imm16(0x80001520, sec, pool >> 16, 'record pool size hi'); rom.set_imm16(0x80001528, sec, pool & 0xFFFF, 'record pool size lo')\n"
     "    rom.set_imm16(0x80001538, sec, pool >> 16, 'record pool clear hi'); rom.set_imm16(0x8000153C, sec, pool & 0xFFFF, 'record pool clear lo')\n"
     "    return size, n"),
])
print('applied')
