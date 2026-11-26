"""QA: the write-watch's words chosen by the scenario (qa_watch1 / qa_watch2: N64 addresses, as numbers; 0: none) --
they take the first of the exe's four debug registers, the palette words the rest. For the 10-man Rumble's crash
(2026-11-06): frame block 1's scheduler task, its reply queue (block + 0x2F30) written over mid-match."""
import os

D = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def edit(path, pairs):
    s = open(path, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (path, a[:70])
        s = s.replace(a, b)
    open(path, 'w', encoding='utf-8', newline='').write(s)


edit(os.path.join(D, 'mod', 'src', 'logic.c'), [
    ('static int qa_rr_ring;',
     'static int qa_rr_ring;\n'
     'static u32 qa_watch[2];           // QA: words for the exe\'s write-watch, chosen by the scenario (0: none)'),
    ('    qa_rr_ring = qa_on == 2 ? (int)plat_config("qa_rr_ring", 0) : 0;',
     '    qa_rr_ring = qa_on == 2 ? (int)plat_config("qa_rr_ring", 0) : 0;\n'
     '    qa_watch[0] = qa_on == 2 ? (u32)plat_config("qa_watch1", 0) : 0;\n'
     '    qa_watch[1] = qa_on == 2 ? (u32)plat_config("qa_watch2", 0) : 0;'),
    ('        u32 at[4] = { 0, 0, 0, 0 }, k = 0;\n',
     '        u32 at[4] = { 0, 0, 0, 0 }, k = 0;\n'
     '        for (int q = 0; q < 2; q++) if (qa_watch[q] >= 0x80000000u && qa_watch[q] < 0x80800000u) at[k++] = qa_watch[q] & ~3u;   // (the scenario\'s own first)\n'),
])
toml = os.path.join(D, 'mod', 'recomp', 'mod.toml')
t = open(toml, encoding='utf-8').read()
a = 'id = "qa_rr_ring"'
assert t.count(a) == 1
i = t.index(a)
j = t.index('[[manifest.config_options]]', i)
opt = ''.join('[[manifest.config_options]]\nid = "qa_watch%d"\nname = "QA: watched word %d"\n'
              'description = "For testing only: an address whose writes the QA write-watch logs (0: none)."\n'
              'type = "Number"\nmin = 0\nmax = 4294967295\nstep = 1\nprecision = 0\ndefault = 0\n\n' % (n, n) for n in (1, 2))
t = t[:j] + opt + t[j:]
open(toml, 'w', encoding='utf-8', newline='').write(t)
print('applied')
