"""Scenario match types without menus (the user, 2026-11-06: "choose a match needed, rules, wrestlers"): a QA match
takes the attract demo set of its format -- singles 1, tag 6, triple threat 7, handicap 8 (the game's demo routine
func_800EF478 gives each set its set-up flags: 0x1, 0x2, 0x4, 0x8) -- with up to four Superstars, costumes and
controllers. Applied by hand after 0.59.0 (python qa\\patch_match_types.py), then tried a run each.

The mod (logic.c): qa_w / qa_c / qa_ctl hold four; qa_demo_set (1: singles, as before) picks the set; the title's
demo table records of that set get the Superstars; the attract counter asks for that set; qa_demo_setup gives each
of the format's slots its controller. mod.toml: qa_w3 / qa_w4, qa_c3 / qa_c4, qa_ctl3 / qa_ctl4, qa_demo_set.
qa_tool.py: "match": "tag" / "triple" / "handicap" (and "singles", "rumble", "arcade" as before), "wrestlers",
"costumes", "control" up to four."""
import os
import re

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


logic = os.path.join(D, 'mod', 'src', 'logic.c')
edit(logic, [
    ('static int qa_on, qa_w[2], qa_c[2], qa_arena, qa_ctl[2], qa_special, qa_matches;',
     'static int qa_on, qa_w[4], qa_c[4], qa_arena, qa_ctl[4], qa_special, qa_matches;\n'
     'static int qa_demo_set = 1;      // QA: the attract demo set a match is made from -- its format (see qa_set_men)'),
    ('    qa_w[0] = (int)plat_config("qa_w1", 0); qa_w[1] = (int)plat_config("qa_w2", 0);',
     '    qa_w[0] = (int)plat_config("qa_w1", 0); qa_w[1] = (int)plat_config("qa_w2", 0);\n'
     '    qa_w[2] = (int)plat_config("qa_w3", 0); qa_w[3] = (int)plat_config("qa_w4", 0);\n'
     '    qa_demo_set = (int)plat_config("qa_demo_set", 1); if (qa_demo_set < 0 || qa_demo_set > 8 || qa_demo_set == 5) qa_demo_set = 1;'),
    ('    qa_c[0] = (int)plat_config("qa_c1", 0) & 3; qa_c[1] = (int)plat_config("qa_c2", 0) & 3;',
     '    qa_c[0] = (int)plat_config("qa_c1", 0) & 3; qa_c[1] = (int)plat_config("qa_c2", 0) & 3;\n'
     '    qa_c[2] = (int)plat_config("qa_c3", 0) & 3; qa_c[3] = (int)plat_config("qa_c4", 0) & 3;'),
    ('    qa_ctl[0] = (int)plat_config("qa_ctl1", 0); qa_ctl[1] = (int)plat_config("qa_ctl2", 0);',
     '    qa_ctl[0] = (int)plat_config("qa_ctl1", 0); qa_ctl[1] = (int)plat_config("qa_ctl2", 0);\n'
     '    qa_ctl[2] = (int)plat_config("qa_ctl3", 0); qa_ctl[3] = (int)plat_config("qa_ctl4", 0);'),
    ('static u16 qa_a, qa_b;           // this match\'s Superstars',
     'static u16 qa_a, qa_b;           // this match\'s Superstars (the first two)\n'
     '// The demo sets (func_800EF478, by NM_ATTRACT_COUNTER / 2): their records in the demo table (0x80102AE4) and the\n'
     '// format\'s men -- 0 tag (0x42), 1 singles, 2 singles 0x11, 3 singles, 4 handicap 0x48, 5 the Rumble (not one of\n'
     '// these), 6 tag (0x2), 7 triple threat (0x4), 8 handicap (0x8)\n'
     'static const u8 qa_set_first[9] = { 0, 3, 11, 16, 17, 18, 19, 27, 30 }, qa_set_end[9] = { 3, 11, 16, 17, 18, 19, 27, 30, 33 };\n'
     'static const u8 qa_set_men[9] = { 4, 2, 2, 2, 3, 0, 4, 3, 3 };\n'
     'static u16 qa_men[4];            // this match\'s Superstars, every one of the format\'s'),
])
# the title: the set's records get this match's men, and the attract counter asks for that set
edit(logic, [
    ('        if (qa_stage == QA_TITLE) { qa_a = qa_pick(0, 0); qa_b = qa_pick(1, qa_a); qa_stage = QA_DEMO; }\n'
     '        patch_demo_table((u16)(qa_a | qa_c[0] << 12), (u16)(qa_b | qa_c[1] << 12));\n'
     '        RD8(NM_ATTRACT_COUNTER) = (u8)(NM_DEMO_SET_SINGLES * 2);',
     '        const int men = qa_set_men[qa_demo_set];\n'
     '        if (qa_stage == QA_TITLE) {\n'
     '            for (int k = 0; k < 4; k++) {   // (each a different Superstar: one the scenario names, or anyone left)\n'
     '                u16 id = 0;\n'
     '                if (k < men) for (int tries = 0; tries < 50; tries++) {\n'
     '                    id = qa_pick(k, 0); int dup = 0;\n'
     '                    for (int j = 0; j < k; j++) if (qa_men[j] == id) dup = 1;\n'
     '                    if (!dup || (qa_w[k] > 0 && qa_w[k] < 0x1000)) break;\n'
     '                }\n'
     '                qa_men[k] = id;\n'
     '            }\n'
     '            qa_a = qa_men[0]; qa_b = qa_men[1]; qa_stage = QA_DEMO;\n'
     '        }\n'
     '        for (int r = qa_set_first[qa_demo_set]; r < qa_set_end[qa_demo_set]; r++) {\n'
     '            u32 a = NM_DEMO_TABLE + 10u * (u32)r;\n'
     '            for (int k = 0; k < 4; k++) RD16(a + 2u * (u32)k) = k < men ? (u16)(qa_men[k] | qa_c[k] << 12) : 0;\n'
     '        }\n'
     '        RD8(NM_ATTRACT_COUNTER) = (u8)(qa_demo_set * 2);'),
    ('    for (int s = 0; s < 2; s++) {\n'
     '        int c = qa_ctl[s] == 1 || qa_ctl[s] == 2 ? qa_ctl[s] : 0;',
     '    for (int s = 0; s < qa_set_men[qa_demo_set]; s++) {   // (the format\'s slots: 2, 3 or 4)\n'
     '        int c = qa_ctl[s] == 1 || qa_ctl[s] == 2 ? qa_ctl[s] : 0;'),
    ('    active[0] = qa_ctl[0] == 1 || qa_ctl[1] == 1; active[1] = qa_ctl[0] == 2 || qa_ctl[1] == 2;',
     '    active[0] = qa_ctl[0] == 1 || qa_ctl[1] == 1 || qa_ctl[2] == 1 || qa_ctl[3] == 1;\n'
     '    active[1] = qa_ctl[0] == 2 || qa_ctl[1] == 2 || qa_ctl[2] == 2 || qa_ctl[3] == 2;'),
])

# mod.toml: the new options, after qa_ctl1's block
toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
def option(i, name, desc, mx, default=0):
    return ('\n[[manifest.config_options]]\nid = "%s"\nname = "%s"\ndescription = "%s"\ntype = "Number"\nmin = 0\nmax = %d\n'
            'step = 1\nprecision = 0\ndefault = %d\n' % (i, name, desc, mx, default))
extra = (option('qa_w3', 'QA: Superstar 3', 'For testing only: slot 3\'s Superstar ID (0: anyone).', 4095)
         + option('qa_w4', 'QA: Superstar 4', 'For testing only: slot 4\'s Superstar ID (0: anyone).', 4095)
         + option('qa_c3', 'QA: costume 3', 'For testing only: slot 3\'s costume.', 3)
         + option('qa_c4', 'QA: costume 4', 'For testing only: slot 4\'s costume.', 3)
         + option('qa_ctl3', 'QA: controller 3', 'For testing only: slot 3 (0: the CPU, 1 / 2: a controller).', 2)
         + option('qa_ctl4', 'QA: controller 4', 'For testing only: slot 4 (0: the CPU, 1 / 2: a controller).', 2)
         + option('qa_demo_set', 'QA: match format', 'For testing only: the attract demo set a match is made from (1 singles, 6 tag, 7 triple threat, 8 handicap).', 8, 1))
m = re.search(r'\n\[\[manifest\.config_options\]\]\nid = "qa_ctl2"[^\[]*', t)
assert m, 'qa_ctl2 block'
t = t[:m.end()] + extra.lstrip('\n') + '\n' + t[m.end():]
open(toml, 'w', encoding='utf-8', newline='').write(t)

# qa_tool.py: the formats and four of each
tool = os.path.join(D, 'qa', 'qa_tool.py')
edit(tool, [
    ('    w = [wrestler_id(x, names) for x in (s.get("wrestlers") or [None, None]) + [None, None]][:2]\n'
     '    c = (s.get("costumes") or [0, 0]) + [0, 0]',
     '    w = [wrestler_id(x, names) for x in (s.get("wrestlers") or [None, None]) + [None, None, None, None]][:4]\n'
     '    c = ((s.get("costumes") or [0, 0]) + [0, 0, 0, 0])[:4]'),
    ('    ctls = [ctl[x] for x in ((s.get("control") or ["cpu", "cpu"]) + ["cpu", "cpu"])[:2]]',
     '    ctls = [ctl[x] for x in ((s.get("control") or ["cpu", "cpu"]) + ["cpu", "cpu", "cpu", "cpu"])[:4]]\n'
     '    # (a QA match\'s format: the attract demo set it\'s made from -- logic.c qa_demo_set)\n'
     '    fmt = s.get("match", "singles")\n'
     '    demo_set = {"singles": 1, "tag": 6, "triple": 7, "handicap": 8}.get(fmt, 1)'),
    ('    st.update({"qa_mode": {"singles": 1, "rumble": 2, "arcade": 0}[s.get("match", "singles")], "qa_w1": w[0], "qa_w2": w[1], "qa_c1": int(c[0]), "qa_c2": int(c[1]),',
     '    st.update({"qa_mode": {"singles": 1, "tag": 1, "triple": 1, "handicap": 1, "rumble": 2, "arcade": 0}[fmt], "qa_demo_set": demo_set,\n'
     '               "qa_w1": w[0], "qa_w2": w[1], "qa_w3": w[2], "qa_w4": w[3], "qa_c1": int(c[0]), "qa_c2": int(c[1]), "qa_c3": int(c[2]), "qa_c4": int(c[3]),'),
    ('               "qa_ctl1": ctls[0], "qa_ctl2": ctls[1],',
     '               "qa_ctl1": ctls[0], "qa_ctl2": ctls[1], "qa_ctl3": ctls[2], "qa_ctl4": ctls[3],'),
])
print('applied')
