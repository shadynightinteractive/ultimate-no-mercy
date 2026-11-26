"""QA: a Royal Rumble's ring size (qa_rr_ring, 4..32; 0: the engine's own, 6): written to the engine's rw_rumble_ring
(SIX_RUMBLE_RING) while a QA Rumble is set up -- rw_rumble_fill takes as many as that (and the entrants and the slots
allow). For the 32-slot work's probes: what breaks with 8, 10 in the ring (2026-11-06)."""
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
     'static int qa_rr_ring;           // QA: a Royal Rumble\'s ring size (rw_rumble_ring; 0: the engine\'s own)'),
    ('    qa_rr_out = qa_on == 2 ? (int)plat_config("qa_rr_out", 0) : 0;',
     '    qa_rr_out = qa_on == 2 ? (int)plat_config("qa_rr_out", 0) : 0;\n'
     '    qa_rr_ring = qa_on == 2 ? (int)plat_config("qa_rr_ring", 0) : 0;'),
    ('static void qa_rumble(void) {\n    free_play = 1;',
     'static void qa_rumble(void) {\n    free_play = 1;\n'
     '#ifdef NM_SIX\n'
     '    if (qa_rr_ring >= 4 && qa_rr_ring <= NM_SLOTS_CAP) RD8(SIX_RUMBLE_RING) = (u8)qa_rr_ring;   // (before the set-up reads it)\n'
     '#endif'),
])
toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "qa_rr_out"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i)
t = t[:j] + ('[[manifest.config_options]]\nid = "qa_rr_ring"\nname = "QA: Rumble ring size"\n'
             'description = "For testing only: how many a QA Royal Rumble has in the ring (0: the engine\'s own)."\n'
             'type = "Number"\nmin = 0\nmax = 32\nstep = 1\nprecision = 0\ndefault = 0\n\n') + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)
print('applied')
