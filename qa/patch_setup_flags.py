"""QA: a match's set-up flags given outright (qa_setup_flags; 0: the demo set's) -- for the kinds no demo set has
(0x80, 0x100: the cage is one, by the menu). The demo routine func_800EF478 writes its set's flags to 0x800A957C; a
hook on its return puts the scenario's there instead, before the match is made from them."""
import os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


edit(os.path.join(D, 'mod', 'src', 'logic.c'), [
    ('static int qa_rules;             // QA: the game\'s rule preset for it (0x800F5E78 + 12 e; 0: the demo\'s own)',
     'static int qa_rules;             // QA: the game\'s rule preset for it (0x800F5E78 + 12 e; 0: the demo\'s own)\n'
     'static u32 qa_setup_flags;       // QA: its set-up flags outright (0x800A957C; 0: the demo set\'s)'),
    ('    qa_rules = (int)plat_config("qa_rules", 0) & 0x3F;',
     '    qa_rules = (int)plat_config("qa_rules", 0) & 0x3F;\n'
     '    qa_setup_flags = (u32)plat_config("qa_setup_flags", 0);'),
    ('#endif\nstatic void qa_demo_setup(void) {',
     '// (and its set-up flags outright, when a scenario gives them: after the demo routine has written its set\'s)\n'
     'RECOMP_HOOK_RETURN("func_800EF478") void qa_hook_demo_flags(void) {\n'
     '    if (qa_on == 1 && qa_setup_flags) RD32(0x800A957Cu) = qa_setup_flags;\n'
     '}\n'
     '#endif\nstatic void qa_demo_setup(void) {'),
])
toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "qa_rules"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i)
t = t[:j] + ('[[manifest.config_options]]\nid = "qa_setup_flags"\nname = "QA: set-up flags"\n'
             'description = "For testing only: a QA match\'s set-up flags outright (0: its demo set\'s)."\n'
             'type = "Number"\nmin = 0\nmax = 1048575\nstep = 1\nprecision = 0\ndefault = 0\n\n') + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)
edit(os.path.join(D, 'qa', 'qa_tool.py'), [
    ('"qa_demo_set": demo_set, "qa_rules": rules,',
     '"qa_demo_set": demo_set, "qa_rules": rules, "qa_setup_flags": int(s.get("setup_flags", 0)),'),
])
print('applied')
