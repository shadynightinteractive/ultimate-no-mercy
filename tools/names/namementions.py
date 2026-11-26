# (fablenames, 2026-11-14) Abouts that still call a function by its N64 name when it now has a confirmed name (PROMPT part 3:
# "abouts that name a function by its N64 name (func_8001xxxx) when it now has a name"). fixstale.py only reaches the mentions
# marked "(not converted)". Here, in the part of an about BEFORE its first dated note -- the description; the dated notes are
# history and stay as written -- each mention of a func_X that has an entry with guess false, and is not the function itself,
# becomes:      func_X(args)  ->  name(args)            (the given abouts write calls so: load_hand_meshes(1))
#               func_X        ->  name (func_X)         (the given style: run_editor_frame (func_80107200))
# Left alone: a mention inside a quoted source line ('...', the evidence as the source has it), one already beside its name
# ("name (func_X" / "func_X (name" / "func_X, name"), modrep_func_X, the lists callernames.py already rewrote, and any about
# where a replacement would make a line end in a backslash or add a carriage return (none do).
# Writes namementions_edits.json for put.py ({func: {"about": ...}}).
from paths import D, L, B, T, BASE, CANDS, CH
import json, re
n = json.load(open(BASE, encoding='utf8')); n.update(json.load(open(CH, encoding='utf8')))
c = json.load(open(CANDS))
FN = re.compile(r'(?<![\w.])func_[0-9A-F]{8}(?:_ovl_\w)?\b')
def quoted(text):
    """the positions inside '...' quotes (an apostrophe inside a word is not a quote mark)"""
    inside = [False] * len(text); q = False
    for i, ch_ in enumerate(text):
        if ch_ == "'":
            before = text[i - 1] if i else ' '
            after = text[i + 1] if i + 1 < len(text) else ' '
            if not q and (before in ' (:[,;' ) and after != ' ':
                q = True
            elif q and not after.isalnum():
                q = False
            elif not q and before.isalnum() and after.isalnum():
                pass
        inside[i] = q
    return inside
edits = {}; count = 0
for f, e in n.items():
    if f not in c or 'cpp' not in c[f]:
        continue
    about = e['about']
    m = re.search(r'\(\d{4}-\d\d-\d\d', about)
    head, tail = (about[:m.start()], about[m.start():]) if m else (about, '')
    inside = quoted(head)
    out = []; last = 0
    for m in FN.finditer(head):
        g = m.group(0)
        if g == f or g not in n or n[g].get('guess') or n[g]['name'].startswith('func_') or inside[m.start()]:
            continue
        if head[max(0, m.start() - 7):m.start()] == 'modrep_':
            continue
        name = n[g]['name']
        before = head[max(0, m.start() - len(name) - 3):m.start()]
        after = head[m.end():m.end() + len(name) + 4]
        if name in before or name in after or re.search(r'\b%s\b' % re.escape(name), head[max(0, m.start() - 80):m.end() + 80]):
            continue          # already beside its name
        if head[m.end():m.end() + 1] == '(':
            rep = name
        elif head[m.start() - 1:m.start()] == '(' and head[m.end():m.end() + 1] == ')':
            rep = '%s, %s' % (name, g)        # "(func_X)" -> "(name, func_X)": no bracket inside a bracket
        else:
            rep = '%s (%s)' % (name, g)
        out.append(head[last:m.start()] + rep); last = m.end(); count += 1
    if out:
        new = ''.join(out) + head[last:] + tail
        if '\r' in new or re.search(r'\\$', new, re.M):
            continue
        edits[f] = {'about': new}
json.dump(edits, open(T + 'namementions_edits.json', 'w'), indent=0, ensure_ascii=False)
print(len(edits), 'abouts', count, 'mentions')
import random
random.seed(2)
for f in random.sample(sorted(edits), 7):
    a, b = n[f]['about'], edits[f]['about']
    i = next(k for k in range(min(len(a), len(b))) if a[k] != b[k])
    print(f, '|', a[max(0, i - 60):i + 110].replace('\n', ' '), '\n   ->', b[max(0, i - 60):i + 150].replace('\n', ' '))
