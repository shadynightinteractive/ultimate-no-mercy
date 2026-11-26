"""(2026-11-13, modnative5: the user's rule (6), "compound-literal padding: make the native code write padding exactly as the
original") Which of page_items' compound literals write MI's padding bytes on the N64, site by site, from the original's own
stores: page_sites.py gives each instruction's source line (the mod's debug build) and the instruction (Stage 1's original);
a site writes the padding when its stores cover MI's padding bytes (6-7, 18-19, 26-27 of each 28-byte element).

Measured on this build: every PG / ACT site and the two plain `(MI){ ..., MI_INFO_PAD, ... }` sites write all of it as zeros
(the compiler copies those literals whole: unaligned swl / swr $zero over the zero fields and the padding between them); no
V site writes any of it (field stores only). Written to compound_pads.json in the order of the compound literals in the
native text, which is checked against the source's order by each one's kind.
"""
import json, re, os, collections

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
d = json.load(open(os.path.join(HERE, 'dev', 'page_sites.json')))
PAD = {6, 7, 18, 19, 26, 27}


def without_comments(lines):
    """The lines with their comments blanked out (// to the line's end, /* ... */ across lines; not inside a string or a
    character constant), one for one, so line numbers stay the source's. (2026-11-17, pipefix: 0.84's logic.c has a comment
    in page_items -- `case PG_WRESTLERS: // (... see PG("ROYAL RUMBLE") above)`, revfix -- that the site pattern took for a
    63rd compound literal; the native text has 62, and the order check stopped the native step.)"""
    out, block = [], False
    for line in lines:
        code, i, quote = [], 0, None
        while i < len(line):
            c = line[i]
            if block:
                if line.startswith('*/', i):
                    block, i = False, i + 2
                else:
                    i += 1
                continue
            if quote:
                code.append(c)
                if c == '\\' and i + 1 < len(line):
                    code.append(line[i + 1])
                    i += 2
                    continue
                if c == quote:
                    quote = None
            elif c in '"\'':
                quote = c
                code.append(c)
            elif line.startswith('//', i):
                break
            elif line.startswith('/*', i):
                block, i = True, i + 2
                code.append(' ')
                continue
            else:
                code.append(c)
            i += 1
        out.append(''.join(code))
    return out


src = without_comments(open(os.path.join(MOD_DIR, 'src', 'logic.c')).read().split('\n'))
start = next(i for i, l in enumerate(src) if l.startswith('static int page_items('))
end = next(i for i in range(start, len(src)) if src[i].startswith('}'))
sites = [i + 1 for i in range(start, end) if not src[i].lstrip().startswith('#define') and (
    re.search(r'(?<![A-Za-z_])(?:V|PG|ACT)\(', src[i]) or '(MI){' in src[i])]

# (2026-11-17, pipefix) Code the compiler shares between sites has no source line (llvm-symbolizer gives it line 0): in 0.84's
#  page_items the last ACT of two cases -- ACT("QUIT GAME", ...) before `return n;`, ACT("BACK", ...) at the end -- stores only
#  the element's first word itself, then runs into one shared tail (a `j` from the `return n;` line, or falling through) that
#  writes the rest of the element, the padding with it (swl / swr / sh $zero over 4..0x1B). By their own lines they wrote none of
#  it. So a site's stores are its own line's and those of the line-0 code that runs right after it -- from the site's line or the
#  plain statements after it up to the next site (`return n;`), falling through or by an unconditional jump, up to the first
#  instruction of another line.
inv = json.load(open(os.path.join(HERE, 'inventory.json')))
base = int(next(k for k, r in inv.items() if r['name'] == 'page_items'), 16)
at = {}
for l, v in d.items():
    for k, mn, ops in v:
        at[k] = (int(l), mn, ops)


def merged_after(own):
    """The line-0 instructions run right after the instructions `own` (indexes): [(k, mn, ops)]."""
    found, todo, seen = [], [], set()

    def follow(k):
        # (a jump's delay slot runs, then its target; anything else runs on into the next instruction)
        l, mn, ops = at.get(k, (None, '?', ''))
        if mn == 'j':
            m = re.match(r'L_([0-9A-F]{8})$', ops)
            return ([k + 1] if at.get(k + 1, (None,))[0] == 0 else []) + ([(int(m.group(1), 16) - base) // 4] if m else [])
        if mn in ('jr', 'jal', 'jalr') or mn.startswith('b'):
            return []           # (a return, a call or a branch: not followed -- the shared tails are reached by j or falling in)
        return [k + 1]
    for k in own:
        todo += follow(k)
    while todo:
        k = todo.pop()
        if k in seen or at.get(k, (None,))[0] != 0:
            continue
        seen.add(k)
        found.append((k,) + at[k][1:])
        todo += follow(k)
    return found


out = {}
for i, line in enumerate(sites):
    cov = set()
    nxt = sites[i + 1] if i + 1 < len(sites) else end + 1
    after = []          # (the `return n;` / `break;` right after the site, if any: where its jump to a shared tail is)
    for l in range(line + 1, nxt):
        s = src[l - 1].strip()
        if not s:
            continue
        if not re.match(r'(return\b|break\s*;)', s):
            break
        after.append(l)
    own = [k for l in [line] + after for k, _, _ in d.get(str(l), [])]
    merged = merged_after(own)
    if merged and any(mn in ('sw', 'sh', 'sb', 'swl', 'swr') for _, mn, _ in merged):
        print('line %d: +%d instructions of shared code after it (from %#x)' % (line, len(merged), base + 4 * min(k for k, _, _ in merged)))
    for k, mn, ops in [tuple(x) for x in d.get(str(line), [])] + merged:
        m = re.match(r'\$\w+, (-?0x[0-9A-F]+)\(\$(\w+)\)', ops)
        if not m or mn not in ('sw', 'sh', 'sb', 'swl', 'swr') or m.group(2) == 'sp':
            continue
        off = int(m.group(1), 16)
        if mn == 'sw':
            r = range(off, off + 4)
        elif mn == 'sh':
            r = range(off, off + 2)
        elif mn == 'sb':
            r = range(off, off + 1)
        elif mn == 'swl':
            r = range(off, (off | 3) + 1)
        else:
            r = range(off & ~3, off + 1)
        cov |= {b % 28 for b in r}
    out[line] = sorted(cov & PAD)
print(len(sites), collections.Counter(tuple(v) for v in out.values()))
t = open(os.path.join(HERE, 'pp', 'logic.native.cpp')).read()
a_ = t.index('static int page_items(int page, NPtr<MI> a) {')
body = t[a_:t.index('\n}\n', a_)]
kinds = re.findall(r'\(MI\)\{ (?:NPtr<char>\(lift_mod_address\(0, 0x[0-9A-F]+\)\)|[^,]+), (MI_\w+)', body)
src_kinds = []
for l in sorted(out):
    m = re.search(r'(?<![A-Za-z_])(V|PG|ACT)\(', src[l - 1])
    src_kinds.append({'V': 'MI_VAL', 'PG': 'MI_PAGE', 'ACT': 'MI_ACT'}[m.group(1)] if m else re.search(r'\(MI\)\{ [^,]+, (MI_\w+)', src[l - 1]).group(1))
assert src_kinds == kinds, (len(src_kinds), len(kinds))
# (a site whose last stores the debug lines give to the next line -- the last ACT of a run -- writes all of it too: partial
#  coverage only ever comes from that, and is counted as writing it)
pads = [bool(out[l]) for l in sorted(out)]
json.dump({'page_items': {'struct': 'MI', 'writes_padding': pads, 'lines': sorted(out)}},
          open(os.path.join(HERE, 'compound_pads.json'), 'w'), indent=0)
print('compound_pads.json: %d sites, %d write the padding' % (len(pads), sum(pads)))
