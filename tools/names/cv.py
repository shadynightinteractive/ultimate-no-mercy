# (names4) compact structured view: declarations and ABI noise dropped, empty branches folded, records shortened.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import re, sys, os
import stfacts as S
def compact(f):
    t = S.text(f)
    L = []
    for l in t.split('\n'):
        s = l.strip()
        if not s or re.match(r'(?:const )?(?:s8|s16|s32|u8|u16|u32|f32|f64|s64|u64|P<.*>|Ref<.*>|M2C_UNK|LiftAbiOut\w*) \w+(?:[;{]| = )', s) and ('LiftAbiOut' in s or s.endswith(';') and ' = ' not in s) \
           or 'LiftFrame' in s or 'frame.keep' in s or re.match(r'lift_out_\w+ = [^;]*;$', s) or re.match(r'(?:LiftAbiOut\w*) ', s): continue
        L.append(l)
    t = '\n'.join(L)
    for _ in range(4):
        t = re.sub(r'if \((.*)\) \{\n\s*\} else \{', lambda m: 'if (!(%s)) {' % m.group(1), t)
        t = re.sub(r'\} else \{\n\s*\}', '}', t)
    t = re.sub(r'AI_RECORD(?:_AS)?\((?:ai_record\w*|\*\(P<P<void>> \)0x8015A6F0|ai\w*|temp_\w+), (\w+)(?:, \w+)?\)', r'ai.\1', t)
    t = re.sub(r'STEP_AS\(step, (\w+), \w+\)', r'step.\1', t)
    t = re.sub(r'WRESTLER(?:_AS)?\((\w+), (\w+)(?:, \w+)?\)', r'\1.\2', t)
    t = re.sub(r'\*\(P<P<void>> \)0x8015A6F0', 'AI', t)
    t = re.sub(r'\((?:f64|s32|u16|s16|u32|f32)\) ', '', t)
    t = re.sub(r'(?m)^((?:    )+)', lambda m: '  ' * (len(m.group(1)) // 4), t)
    return t
def inline(t):
    # fold single-assignment, call-free short locals into their uses (reading aid only)
    for _ in range(3):
        lines = t.split('\n')
        for i, l in enumerate(lines):
            m = re.match(r'\s*(\w+) = ([^;=()]*(?:\([^()]*\)[^;=()]*)*);$', l)
            if not m: continue
            v, rhs = m.group(1), m.group(2)
            if re.search(r'\w+\(', rhs) and not re.match(r'(ai|step|\w+)\.\w+$', rhs) and 'M2C_FIELD' not in rhs and '*(P<' not in rhs: continue
            if len(rhs) > 70 or v in ('ai', 'his_record'): continue
            rest = '\n'.join(lines[:i] + lines[i + 1:])
            if len(re.findall(r'\b%s\s*(?:[-+*/&|^]|<<|>>)?=(?!=)' % re.escape(v), rest)): continue
            uses = len(re.findall(r'(?<![.\w])%s\b' % re.escape(v), rest))
            if uses == 0 or uses > 2: continue
            rhs2 = rhs if re.fullmatch(r'[\w.>]+|\*\(P<[^)]*\)\s*0x\w+', rhs) else '(' + rhs + ')'
            # only inline into lines after the assignment
            after = '\n'.join(lines[i + 1:])
            if len(re.findall(r'(?<![.\w])%s\b' % re.escape(v), after)) != uses: continue
            after = re.sub(r'(?<![.\w])%s\b' % re.escape(v), lambda _m: rhs2, after)
            t = '\n'.join(lines[:i]) + '\n' + after
            break
        else:
            break
    return t

for f in sys.argv[1:]:
    e = S.names[f]
    print('=' * 30, f, e['name'], S.cands[f].get('size'))
    if os.environ.get('AB'): print('ABOUT:', e['about'][:int(os.environ['AB'])])
    b = compact(f).split('{', 1)[1].rstrip('}\n')
    for _ in range(int(os.environ.get('INL', '40'))):
        b2 = inline(b)
        if b2 == b: break
        b = b2
    for _ in range(3):
        b = re.sub(r'if \(!\(([^()&|]*?) != ([^()&|]*?)\)\) \{', r'if (\1 == \2) {', b)
        b = re.sub(r'if \(!\(([^()&|]*?) == ([^()&|]*?)\)\) \{', r'if (\1 != \2) {', b)
        b = re.sub(r'\(([\w.]+(?: \+ 1)?)\)', r'\1', b) if os.environ.get('PAREN') else b
    for _ in range(80):
        b2 = re.sub(r'(\n\s*)case ([\d, ]+):\n\s*goto (block_\d+);\n\s*case (\d+):\n\s*goto \3;', r'\1case \2, \4:\n goto \3;', b)
        if b2 == b: break
        b = b2
    b = re.sub(r'(?m)^\s*s32 lift_\w+ = 0.*$', '', b)
    b = re.sub(r'0x([0-9A-F]+)\b', lambda m: str(int(m.group(1), 16)) if int(m.group(1), 16) < 256 else m.group(0), b)
    print('\n'.join(l for l in b.split('\n') if l.strip() and not re.fullmatch(r'\s*ai_record\w* = AI;', l)))
