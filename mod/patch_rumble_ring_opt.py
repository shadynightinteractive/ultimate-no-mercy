"""The Royal Rumble's ring size as an operator setting (the 32-slot work, 2026-11-07): IN THE RING AT ONCE on the Royal
Rumble page (and mod.toml rumble_ring), 4 up to the engine's room (NM_SLOTS_CAP); 6, the engine's own, by default --
so nothing changes until it's set. rr_configure (the arcade's Royal Rumble set-up) writes it to the engine's
rw_rumble_ring (SIX_RUMBLE_RING), which rw_rumble_fill takes as far as the entrants and the slots go; a QA scenario's
qa_rr_ring still wins in its own runs."""
import os

D = os.path.dirname(os.path.abspath(__file__))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


edit(os.path.join(D, 'src', 'logic.c'), [
    ('static int rumble_interval = 30; // ... seconds between entrants (10..120)',
     'static int rumble_interval = 30; // ... seconds between entrants (10..120)\n'
     'static int rumble_ring = 6;      // ... in the ring at once (4..NM_SLOTS_CAP; 6, the engine\'s own)'),
    ('    if (rumble_interval < 10 || rumble_interval > 120) rumble_interval = 30;\n',
     '    if (rumble_interval < 10 || rumble_interval > 120) rumble_interval = 30;\n'
     '    rumble_ring = (int)plat_config("rumble_ring", 6);\n'
     '    if (rumble_ring < 4 || rumble_ring > NM_SLOTS_CAP) rumble_ring = NM_SLOTS_CAP < 6 ? NM_SLOTS_CAP : 6;\n'),
    ('    plat_config_set("rumble_interval", (u32)rumble_interval);\n',
     '    plat_config_set("rumble_interval", (u32)rumble_interval);\n'
     '    plat_config_set("rumble_ring", (u32)rumble_ring);\n'),
    ('    rumble_size = 15; rumble_interval = 30; arcade_speed = 1;',
     '    rumble_size = 15; rumble_interval = 30; rumble_ring = NM_SLOTS_CAP < 6 ? NM_SLOTS_CAP : 6; arcade_speed = 1;'),
    ('    RDS16(NM_SEL_COUNT) = (s16)(rumble_size + 2);\n',
     '    RDS16(NM_SEL_COUNT) = (s16)(rumble_size + 2);\n'
     '#ifdef NM_SIX\n'
     '    // IN THE RING AT ONCE: the engine\'s rw_rumble_ring, read by the set-up (rw_rumble_fill); a QA scenario\'s own wins\n'
     '    RD8(SIX_RUMBLE_RING) = (u8)((qa_rr_ring >= 4 && qa_rr_ring <= NM_SLOTS_CAP) ? qa_rr_ring : rumble_ring);\n'
     '#endif\n'),
    ('        V("TIME BETWEEN ENTRANTS", &rumble_interval, 10, 120, 5, 0, VF_SEC);\n',
     '        V("TIME BETWEEN ENTRANTS", &rumble_interval, 10, 120, 5, 0, VF_SEC);\n'
     '        V("IN THE RING AT ONCE", &rumble_ring, NM_SLOTS_CAP < 4 ? NM_SLOTS_CAP : 4, NM_SLOTS_CAP, 1, 0, VF_PLAIN);\n'),
])
toml = os.path.join(D, 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "rumble_interval"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i)
t = t[:j] + ('[[manifest.config_options]]\nid = "rumble_ring"\nname = "Royal Rumble: in the ring at once"\n'
             'description = "How many Superstars a Royal Rumble has in the ring at once (4 to 32; 6 is the game\'s own)."\n'
             'type = "Number"\nmin = 4\nmax = 32\nstep = 1\nprecision = 0\ndefault = 6\n\n') + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)
print('applied')
