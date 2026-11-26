#!/usr/bin/env python3
"""The mod's own functions, one row each, for making them native (scratch/agents/modnative1, 2026-11-12).

Reads (all read only):
  scratch/agents/final081/aot/mod_aot/mod_aot_nomercy_arcade_*.c   Stage 1's translation of the built mod (nm_aot_nomercy_arcade_<address>):
                                                                    the ORIGINAL each native function is proved against
  tools/lift/modrep/out/modrep_modfuncs.c               the same functions as modrep translates them (modfn_<address>): which
                                                                    addresses are the mod's own functions (not copies of the game's)
  mod/build/mod_six/mod.map                                         names, objects, data symbols
  tools/lift/private_data.json                          modglue's private-data classification (the confirmed rule)
  mod/src/*.c, mod/recomp/hooks_recomp.c                            the C each function is compiled from

Writes inventory.json beside it: address -> {name, object, instructions, calls_mod, calls_game, calls_import, data (symbols
reached, private or not), floats, source (the function's C text, or null), why_not_leaf}.
"""
import json, os, re, glob

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
AOT = os.path.join(HERE, 'w', 'mod_aot')   # (modnative5: this folder's Stage 1 translation of its own mod build, aot_copy.py)
LIFT = os.path.join(DEV, 'tools', 'lift')
MAP = os.path.join(MOD_DIR, 'build', 'mod_six', 'mod.map')   # (modnative5: this folder's mod build)


def aot_functions():
    """address -> list of lines of nm_aot_nomercy_arcade_<address>'s body (the header line excluded)."""
    out, cur = {}, None
    for path in sorted(glob.glob(os.path.join(AOT, 'mod_aot_nomercy_arcade_[0-9]*.c'))):
        for line in open(path, encoding='utf8', errors='replace'):
            m = re.match(r'RECOMP_FUNC void nm_aot_nomercy_arcade_([0-9A-F]{8})\(', line)
            if m:
                cur = out.setdefault(m.group(1), [])
                continue
            if cur is not None:
                cur.append(line.rstrip('\n'))
                if line.startswith(';}'):
                    cur = None
    return out


def own_addresses():
    text = open(os.path.join(LIFT, 'modrep', 'out', 'modrep_modfuncs.c'), encoding='utf8', errors='replace').read()
    return set(re.findall(r'RECOMP_FUNC void modfn_([0-9A-F]{8})\(', text))


def map_symbols():
    """(text: address -> (name, object), data: sorted [(address, size, name)])"""
    text, data = {}, []
    section = obj = None
    for line in open(MAP, encoding='utf8', errors='replace'):
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) (\S.*)$', line)
        if m:
            section = m.group(4).strip()
            continue
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) {9}(\S.*)$', line)
        if m:
            obj = m.group(4).strip()
            continue
        m = re.match(r'^([0-9a-f]{8}) [0-9a-f]{8} +([0-9a-f]+) +(\d+) {17}(\S+)\s*$', line)
        if not m:
            continue
        a, size, name = int(m.group(1), 16), int(m.group(2), 16), m.group(4)
        if section.startswith('.text') or section.startswith('.recomp_'):
            text.setdefault('%08X' % a, (name, re.sub(r'^.*[\\/]|\.o:.*$', '', obj or '')))
        elif section.startswith('.rodata') or section in ('.data', '.bss', '.sdata', '.sbss'):
            data.append((a, size, name))
    data.sort()
    return text, data


def c_sources():
    """name -> the function's C text (its definition, from the return type to the closing brace), from the mod's sources."""
    found = {}
    paths = sorted(glob.glob(os.path.join(MOD_DIR, 'src', '*.c'))) + [os.path.join(MOD_DIR, 'recomp', 'hooks_recomp.c')]   # (modnative5: this folder's mod copy)
    head = re.compile(r'^(?!\s)(?:static\s+|inline\s+|RECOMP_\w+(?:\([^)]*\))?\s*|__attribute__\(\([^)]*\)\)\s*)*'
                      r'(?:const\s+)?[A-Za-z_]\w*(?:\s*\*+\s*|\s+)(?:\*\s*)*([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*\{', re.M)
    for path in paths:
        text = open(path, encoding='utf8', errors='replace').read()
        for m in head.finditer(text):
            name = m.group(1)
            if name in ('if', 'while', 'for', 'switch', 'return'):
                continue
            depth, i = 0, m.end() - 1
            while i < len(text):
                if text[i] == '{':
                    depth += 1
                elif text[i] == '}':
                    depth -= 1
                    if depth == 0:
                        break
                elif text[i] == '"':
                    i += 1
                    while text[i] != '"':
                        i += 2 if text[i] == '\\' else 1
                elif text[i] == "'":
                    i += 1
                    while text[i] != "'":
                        i += 2 if text[i] == '\\' else 1
                elif text.startswith('//', i):
                    i = text.index('\n', i)
                elif text.startswith('/*', i):
                    i = text.index('*/', i) + 1
                i += 1
            found.setdefault(name, {'file': os.path.relpath(path, DEV), 'text': text[m.start():i + 1],
                                    'static': text[m.start():m.end()].lstrip().startswith('static')})
    return found


def main():
    aot = aot_functions()
    own = own_addresses()
    text_syms, data = map_symbols()
    starts = [d[0] for d in data]
    private = {r['name']: r for r in json.load(open(os.path.join(LIFT, 'private_data.json')))}
    sources = c_sources()
    import bisect
    rows = {}
    for a in sorted(own):
        body = aot.get(a)
        name, obj = text_syms.get(a, ('?', '?'))
        row = {'name': name, 'object': obj, 'in_aot': body is not None}
        if body is None:
            rows[a] = row
            continue
        ins = set(re.findall(r'^\s*// 0x([0-9A-F]{8}):', '\n'.join(body), re.M))
        row['instructions'] = len(ins)
        row['calls_mod'] = sorted(set(re.findall(r'nm_aot_nomercy_arcade_([0-9A-F]{8})\(rdram', '\n'.join(body))))
        row['calls_game'] = sorted(set(re.findall(r'game_funcs\[\d+\]\(rdram, ctx\);\s*// (\w+)', '\n'.join(body))))
        row['calls_import'] = sorted(set(re.findall(r'imports\[\d+\]\(rdram, ctx\);\s*// (\w+)', '\n'.join(body))))
        row['other_calls'] = sorted(set(l.strip() for l in body if '(rdram, ctx)' in l and 'nm_aot_nomercy_arcade_' not in l))[:5]
        row['jr_not_ra'] = sum(1 for l in body if re.search(r'// 0x[0-9A-F]{8}: jr +\$(?!ra)', l))
        row['floats'] = any(re.search(r'\bctx->f\d+', l) for l in body)
        refs = set()
        for sec, off in re.findall(r'RELOC_(?:HI|LO)16\((\d+), (0X[0-9A-F]+|\d+)\)', '\n'.join(body)):
            addr = 0x81000000 + int(off, 16)
            k = bisect.bisect_right(starts, addr) - 1
            if k >= 0 and addr < data[k][0] + max(data[k][1], 1):
                refs.add(data[k][2])
            else:
                refs.add('?%08X' % addr)
        row['data'] = sorted(refs)
        row['data_private'] = sorted(r for r in refs if private.get(r, {}).get('private'))
        row['data_shared'] = sorted(r for r in refs if not private.get(r, {}).get('private'))
        src = sources.get(name)
        row['source_file'] = src['file'] if src else None
        row['source'] = src['text'] if src else None
        rows[a] = row
    json.dump(rows, open(os.path.join(HERE, 'inventory.json'), 'w'), indent=1)
    have = [r for r in rows.values() if r.get('in_aot')]
    print('own functions: %d (in Stage 1: %d), instructions %d' % (len(rows), len(have), sum(r['instructions'] for r in have)))
    leaf = [r for r in have if not r['calls_mod'] and not r['calls_game'] and not r['calls_import'] and not r['other_calls']]
    print('leaves (no calls): %d, %d instructions' % (len(leaf), sum(r['instructions'] for r in leaf)))
    print('with C source found: %d' % sum(1 for r in have if r['source']))
    nodata = [r for r in leaf if not r['data_shared']]
    print('leaves touching no shared mod data: %d, %d instructions' % (len(nodata), sum(r['instructions'] for r in nodata)))


if __name__ == '__main__':
    main()
