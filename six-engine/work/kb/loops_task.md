# Task: exact instruction patches that make the match engine's wrestler loops run over 6 slots

Read /home/devbox/work/six/kb/context.md first. Wrestler slots become 0..5 (6), and wrestler MODEL instances
become 0..5 too (model index == slot). Slots 4 and 5 are EMPTY (+0xA0 == -1) in every 4-man match, so a loop
over 6 slots must still give exactly the same results for 4 wrestlers. All per-slot / per-model arrays keep their
element STRIDE (they are being moved elsewhere with room for 6; addresses in the code are rewritten separately, so
do not touch lui/address immediates). Your job is only the COUNT side of loops.

For each function in your list, the loops to fix are in /home/devbox/work/six/kb/loops_todo.txt (lines
`CLASS | func | bound | class | description`). Read the raw MIPS with
`python3 /home/devbox/work/six/mdis.py <func>` and the C at /home/devbox/work/ghidra/out/<func>.c.
`python3 /home/devbox/work/six/kb/loopsites.py <func>` lists candidate instructions (compares with 3/4/5,
constants 3/4, and constants that are 3x/4x a known stride). For each SLOT or MODEL loop, find the exact
instruction(s) holding the count and give the replacement, changing ONLY the 16-bit immediate field (MIPS I-type:
slti/sltiu/addiu/ori/andi; never insert or remove instructions). Typical cases:
- `slti $v0, $s1, 0x4` -> `slti $v0, $s1, 0x6`
- loop over byte offsets: `slti $v0, $a0, 0xD60` (4*0x358) -> `0x1410` (6*0x358)
- counting down from 3: `addiu $s1, $zero, 0x3` -> `0x5`, and a start byte offset 3*stride -> 5*stride
- `bne $x, $y` against a register loaded with 4 by `addiu $y, $zero, 0x4` -> change that addiu to 6
- a "none found" test `== 4` after a search loop -> `== 6`
If the count can't be changed in place (immediate wouldn't fit 16 bits, the count is shared with a non-slot use
of the same register/constant, the loop is unrolled 4 times, a 4-entry LOCAL (stack) array would overflow, a
bitmask with one bit per slot, etc.), report it as NEEDS_REWRITE with the reason instead of a patch. UNSURE loops:
decide if they are slot/model loops; if not, say NOT_SLOT.

Write one line per finding to your output file, fields separated by ' | ':
PATCH | <func> | <instruction address hex> | <old instruction as mdis prints it> | <new immediate hex> | <which loop / why>
NEEDS_REWRITE | <func> | <instruction address(es)> | <reason>
NOT_SLOT | <func> | <loop> | <why>
CHECK | <func> | <address> | <anything else that must change for 6, e.g. a 4-entry stack buffer size, a mask>
Be exact: the address must be the instruction holding the immediate, and the old instruction text must match
mdis output. Every loop line in loops_todo.txt for your functions must be accounted for by at least one line.
