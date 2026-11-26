"""QA rules, second way: the demo applies its own preset to the RULES rows (func_800E2BC0 -> func_800E2B44(e, dst):
preset e's 12 bytes from 0x800F5E78 + 12e to dst), after the title -- so copying a preset there on the title was
written over (2026-11-06: hardcore's run ended with the normal rows). Now a hook on func_800E2B44: its start notes the
destination, its return -- when that was the RULES rows (0x800AA044) and a scenario asked for rules -- puts the
scenario's preset there instead, as the game's own step has just done with the demo's."""
import os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = os.path.join(D, 'mod', 'src', 'logic.c')
s = open(p, encoding='utf-8').read()


def sub(a, b):
    global s
    assert s.count(a) == 1, a[:70]
    s = s.replace(a, b)


sub('''        // (the scenario's rules: the game's preset into the RULES page's rows -- the preset table is the title
        //  overlay's data, there while the demo is made)
        if (qa_rules > 0)
            for (u32 i = 0; i < 12; i++) RD8(NM_RULE_ROWS + i) = RD8(0x800F5E78u + 12u * (u32)qa_rules + i);
''', '')
sub('static void qa_demo_setup(void) {', '''// A QA match's rules (qa_rules, the scenario's "rules"): the demo puts a preset of its own into the RULES page's rows
// with the game's func_800E2B44(e, dst) -- e's 12 bytes from 0x800F5E78 + 12e (the title overlay's data) to dst. The
// hook's start notes where they go; its return, when that was the rows and a scenario asked for rules, puts the
// scenario's preset there in their place (the same table, the same step). Nothing happens outside a QA match.
static u32 qa_preset_dst;
RECOMP_HOOK("func_800E2B44") void qa_hook_preset(s32 e, u32 dst) { qa_preset_dst = dst; }
RECOMP_HOOK_RETURN("func_800E2B44") void qa_hook_preset_done(void) {
    if (qa_on != 1 || qa_rules <= 0 || (qa_preset_dst | 0x80000000u) != NM_RULE_ROWS) return;
    for (u32 i = 0; i < 12; i++) RD8(NM_RULE_ROWS + i) = RD8(0x800F5E78u + 12u * (u32)qa_rules + i);
}
static void qa_demo_setup(void) {''')
open(p, 'w', encoding='utf-8', newline='').write(s)
print('applied')
