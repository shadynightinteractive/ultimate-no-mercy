from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import json, re
n = json.load(open(BASE, encoding='utf8'))
ch = json.load(open(CH, encoding='utf8'))
new = set()
ren = {f: (n[f]['name'], v['name']) for f, v in ch.items() if f in n and n[f]['name'] != v['name']}
report = {}
for f, (old, nw) in sorted(ren.items()):
    pat = re.compile(r'(?<![\w`])%s(?![\w])' % re.escape(old))
    pat2 = re.compile(r'`?\b%s\b`?' % re.escape(old))
    hits = []
    for g in sorted(set(n) | set(ch)):
        if g == f: continue
        e = ch.get(g) or n[g]
        if re.search(r'\b%s\b' % re.escape(old), e['about']):
            hits.append(g)
            if g in new:
                continue
            # keep the history notes that say what it was renamed from
            text = e['about']
            text = re.sub(r'\b%s\b' % re.escape(old), nw, text)
            base = dict(e); base['about'] = text
            ch[g] = {'about': base['about'], 'guess': bool(base.get('guess')), 'name': base['name'], 'vars': dict(sorted(base.get('vars', {}).items()))}
    report[f] = {'old': old, 'new': nw, 'mentioned_by': hits, 'not_updated_new_in_079': [h for h in hits if h in new]}
json.dump(dict(sorted(ch.items())), open(CH, 'w', encoding='utf8', newline='\n'), indent=1, ensure_ascii=False)
json.dump(report, open(T + 'renames.json', 'w'), indent=1)
for f, r in report.items(): print(f, r['old'], '->', r['new'], r['mentioned_by'], r['not_updated_new_in_079'])
