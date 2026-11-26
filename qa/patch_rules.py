"""A QA match's rules by name (the user, 2026-11-06: "choose a match needed, rules, wrestlers"): the scenario's "rules"
(gen/storyc.py RULES' names per match type: "no dq", "hardcore", "tornado", ...) is the game's own preset e; logic.c
copies preset e (0x800F5E78 + 12e, the title overlay's data -- loaded while the demo is made) into the RULES page's
rows (0x800AA044) on the title, before the demo match is set up. mod.toml qa_rules (0: the demo's own). Whether the
demo match takes them is what the first runs show (the match flags)."""
import os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


logic = os.path.join(D, 'mod', 'src', 'logic.c')
edit(logic, [
    ('static int qa_demo_set = 1;      // QA: the attract demo set a match is made from -- its format (see qa_set_men)',
     'static int qa_demo_set = 1;      // QA: the attract demo set a match is made from -- its format (see qa_set_men)\n'
     'static int qa_rules;             // QA: the game\'s rule preset for it (0x800F5E78 + 12 e; 0: the demo\'s own)'),
    ('    qa_demo_set = (int)plat_config("qa_demo_set", 1);',
     '    qa_rules = (int)plat_config("qa_rules", 0) & 0x3F;\n'
     '    qa_demo_set = (int)plat_config("qa_demo_set", 1);'),
    ('        RD8(NM_ATTRACT_COUNTER) = (u8)(qa_demo_set * 2);',
     '        RD8(NM_ATTRACT_COUNTER) = (u8)(qa_demo_set * 2);\n'
     '        // (the scenario\'s rules: the game\'s preset into the RULES page\'s rows -- the preset table is the title\n'
     '        //  overlay\'s data, there while the demo is made)\n'
     '        if (qa_rules > 0)\n'
     '            for (u32 i = 0; i < 12; i++) RD8(NM_RULE_ROWS + i) = RD8(0x800F5E78u + 12u * (u32)qa_rules + i);'),
])

toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "qa_demo_set"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i) if '[[manifest.config_options]]' in t[i:] else len(t)
block = ('[[manifest.config_options]]\nid = "qa_rules"\nname = "QA: rules"\n'
         'description = "For testing only: the game\'s rule preset for a QA match (0: the demo\'s own)."\n'
         'type = "Number"\nmin = 0\nmax = 63\nstep = 1\nprecision = 0\ndefault = 0\n\n')
t = t[:j] + block + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)

tool = os.path.join(D, 'qa', 'qa_tool.py')
edit(tool, [
    ('    demo_set = {"singles": 1, "tag": 6, "triple": 7, "handicap": 8}.get(fmt, 1)',
     '    demo_set = {"singles": 1, "tag": 6, "triple": 7, "handicap": 8}.get(fmt, 1)\n'
     '    # (the rules by the story compiler\'s names for the type: the game\'s preset; a number is a preset as it is)\n'
     '    rules = s.get("rules")\n'
     '    if isinstance(rules, str):\n'
     '        import importlib.util\n'
     '        spec = importlib.util.spec_from_file_location("storyc", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mod", "gen", "storyc.py"))\n'
     '        storyc = importlib.util.module_from_spec(spec); spec.loader.exec_module(storyc)\n'
     '        kind = {"singles": "single"}.get(fmt, fmt)\n'
     '        rules = dict(storyc.RULES[kind])[rules]\n'
     '    rules = int(rules or 0)'),
    ('"qa_demo_set": demo_set,', '"qa_demo_set": demo_set, "qa_rules": rules,'),
])
print('applied')
