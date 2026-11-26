# From the review: the per-slot / per-model arrays as blocks to move (true base, stride, 4 entries).
import aggregate as G, collections
items = []
for (b, s), a in G.A.items():
    if s is None or s == 0: continue
    cls = set(k for k in a['cls'] if k in ('SLOT', 'MODEL'))
    if not cls: continue
    items.append((b, s, cls, a))
by = collections.defaultdict(list)
for b, s, cls, a in items: by[s].append((b, cls, a))
blocks = []
for s, lst in by.items():
    lst.sort(key=lambda x: x[0])
    cur = None
    for b, cls, a in lst:
        if cur and s >= 8 and b - cur['base'] < s:
            cur['members'].append(b); cur['fn'] |= a['fn']; cur['cls'] |= cls
        else:
            cur = {'base': b, 'stride': s, 'members': [b], 'fn': set(a['fn']), 'cls': set(cls)}
            blocks.append(cur)
blocks.sort(key=lambda x: x['base'])
if __name__ == '__main__':
    prev = None
    for k in blocks:
        end = k['base'] + 4 * k['stride']
        ov = ''
        if prev and k['base'] < prev['base'] + 4 * prev['stride']: ov = '  OVERLAPS %08x' % prev['base']
        print('%08x-%08x x%-5x %-12s members=%s%s' % (k['base'], end, k['stride'], '/'.join(sorted(k['cls'])), ' '.join('%x' % m for m in k['members'][:6]), ov))
        prev = k if not prev or end > prev['base'] + 4 * prev['stride'] else prev
    print(len(blocks), 'blocks', hex(sum(6 * k['stride'] for k in blocks)), 'bytes at 6')
