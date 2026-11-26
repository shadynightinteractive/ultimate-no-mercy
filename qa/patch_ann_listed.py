"""QA: how many of the frame's draw-list entries are the announcers' (state column ann_listed, QA_STATE + 0x23C; 0 with
none built). announcers_front passed with JR and King not drawn (2026-11-06: the draw list moved, and the announcer's
slot save put it back over their entries) -- ann_draw stayed 0 in good runs and bad. Now the announcer scenarios can
check that their items are in the list."""
import json, os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


edit(os.path.join(D, 'mod', 'src', 'announce.c'), [
    ('static Announcer ann[2];',
     'static Announcer ann[2];\n\n'
     '// (QA: the frame\'s draw-list entries that are the announcers\' -- an entry\'s +4 is its bone, in an announcer\'s own block)\n'
     'u32 ann_listed(void) {\n'
     '    u32 n = 0; s16 c = RDS16(DRAW_COUNT);\n'
     '    for (s16 i = 0; i < c && i < 8192; i++) {\n'
     '        u32 d = RD32(DRAW_LIST + (u32)i * 4);\n'
     '        if (!GAME_PTR(d)) continue;\n'
     '        u32 bone = RD32(d + 4);\n'
     '        for (int k = 0; k < 2; k++)\n'
     '            if (ann[k].bones && bone >= ann[k].bones && bone < ann[k].bones + BONES_SZ) n++;\n'
     '    }\n'
     '    return n;\n'
     '}'),
])
edit(os.path.join(D, 'mod', 'src', 'logic.c'), [
    ('        RD32(QA_STATE + 0x238) = present | in_ring << 8;\n    }',
     '        RD32(QA_STATE + 0x238) = present | in_ring << 8;\n    }\n'
     '    { extern u32 ann_listed(void); RD32(QA_STATE + 0x23C) = ann_listed(); }   // (QA: the announcers\' draw-list entries)'),
])
cr = os.path.join(D, 'exe', 'WWFNoMercyRecomp', 'src', 'main', 'crash_report.cpp')
edit(cr, [
    ('    fprintf(f, ",present,in_ring");', '    fprintf(f, ",present,in_ring,ann_listed");'),
    ('        fprintf(f, ",%u,%u", rd(Q + 0x238) & 0xFF, (rd(Q + 0x238) >> 8) & 0xFF);',
     '        fprintf(f, ",%u,%u,%u", rd(Q + 0x238) & 0xFF, (rd(Q + 0x238) >> 8) & 0xFF, rd(Q + 0x23C));   // (+0x23C: the announcers\' draw-list entries)'),
])
sc = os.path.join(D, 'qa', 'scenarios', 'announcers_front.json')
s = json.load(open(sc))
if not any('ann_listed' in e.get('check', '') for e in s['expect']):
    s['expect'].append({"name": "the announcers are in the frame's draw list (their bones' entries)", "when": "match",
                        "check": "ann_listed > 0"})
json.dump(s, open(sc, 'w'), indent=2)
print('applied')
