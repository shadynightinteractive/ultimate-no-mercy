"""QA: qa_rr_out's eliminations from a match time on (qa_rr_out_after, game seconds; 0: from the start). For the end of a
10-slot Rumble (2026-11-06): the entrants (one every 10 s at the least) all in first, then a man out every few seconds
down to a winner -- from the start, the eliminations outran the entrants and the field never grew."""
import os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


edit(os.path.join(D, 'mod', 'src', 'logic.c'), [
    ('static int qa_rr_ports, qa_rr_out, qa_p2, qa_p2_after_op;',
     'static int qa_rr_ports, qa_rr_out, qa_p2, qa_p2_after_op;\n'
     'static int qa_rr_out_after;      // QA: qa_rr_out\'s eliminations from this many game seconds into the match on'),
    ('    qa_rr_out = qa_on == 2 ? (int)plat_config("qa_rr_out", 0) : 0;',
     '    qa_rr_out = qa_on == 2 ? (int)plat_config("qa_rr_out", 0) : 0;\n'
     '    qa_rr_out_after = qa_on == 2 ? (int)plat_config("qa_rr_out_after", 0) : 0;'),
    ('            if (qa_rr_out > 0 && ++qa_out_wait >= qa_rr_out * 60) {',
     '            if (qa_rr_out > 0 && RD16(NM_MATCH_FRAMES) != 0xFFFFu && RD16(NM_MATCH_FRAMES) >= (u32)qa_rr_out_after * 60u\n'
     '                && ++qa_out_wait >= qa_rr_out * 60) {'),
])
toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "qa_rr_out"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i)
t = t[:j] + ('[[manifest.config_options]]\nid = "qa_rr_out_after"\nname = "QA: Rumble eliminations from"\n'
             'description = "For testing only: the game seconds into a QA Royal Rumble before its men are put out (0: from the start)."\n'
             'type = "Number"\nmin = 0\nmax = 1000\nstep = 1\nprecision = 0\ndefault = 0\n\n') + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)
print('applied')
