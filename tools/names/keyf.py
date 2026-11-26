# (names4, 2026-11-12) The key facts of a state / plan handler, decoded: plans set, buttons held, situations tested,
# animation files started, states changed to, control bits tested, phases.
from paths import D, L, B, T, BASE, CANDS, CH   # (namesprep 2026-11-14: the folder's own paths, paths.py)
import re, sys, os, collections
import stfacts as S
BTN = [(0x8000, 'A'), (0x4000, 'B'), (0x2000, 'Z'), (0x1000, 'Start'), (0x800, 'up'), (0x400, 'down'), (0x200, 'left'), (0x100, 'right'),
       (0x20, 'L'), (0x10, 'R'), (8, 'C-up'), (4, 'C-down'), (2, 'C-left'), (1, 'C-right')]
def bn(v):
    try: v = int(v.rstrip('U'), 0)
    except ValueError: return v
    return '+'.join(n for b, n in BTN if v & b) or str(v)
def dec(v):
    v = v.strip().rstrip('U')
    try: return str(int(v, 0))
    except ValueError: return v
STATEC = {'change_state', 'set_wrestler_state', 'set_state_letting_go', 'change_state_letting_go', 'set_pair_states', 'change_pair_states',
          'put_pair_in_states_linked', 'set_corner_tag_states'}
ANIMC = {'start_animation', 'restart_animation'}
def key(f):
    t, cc, flds, masks, cmps, sets = S.facts(f)
    body = t.split('{', 1)[1]
    out = collections.OrderedDict()
    plans, btns, anims, states, other = set(), set(), [], [], []
    for c in cc:
        m = re.match(r'(\w+)\??\((.*)\)$', c)
        if not m: continue
        fn, args = m.group(1), [a.strip() for a in re.split(r',\s*(?![^()]*\))', m.group(2))]
        if fn == 'ai_set_plan' and args:
            for a in args[0].split('|'): plans.add(dec(a))
        elif fn in ANIMC and len(args) > 1: anims.append(args[1])
        elif fn in STATEC: states.append('%s(%s)' % (fn, ', '.join(dec(a) for a in args[1:]) if fn in ('change_state', 'set_wrestler_state', 'set_state_letting_go', 'change_state_letting_go') else ', '.join(dec(a) for a in args)))
        elif not fn.startswith(('push_out_of_walls', 'step_timed_slide', 'snap_position')): other.append(c)
    for m in re.finditer(r'\b(?:(buttons_held|built_buttons|buttons_pressed)(?:, \w+)?|P<[us]16> , (0xC|0xE|0x10))\) ([|]?)= ([^;\n]+);', body):
        rhs = m.group(4); fld = m.group(1) or {'0xC': 'buttons_held', '0xE': 'buttons_pressed', '0x10': 'built_buttons'}[m.group(2)]
        tag = {'buttons_held': 'H', 'buttons_pressed': 'P', 'built_buttons': 'W'}[fld] + ('|' if m.group(3) else '')
        if re.fullmatch(r'\w+', rhs.strip()):
            for x in re.findall(r'\b%s = ([^;\n]+);' % re.escape(rhs.strip()), body): rhs += ' ' + x
        for c in re.findall(r'\b(0x[0-9A-F]+|\d+)U?\b', rhs):
            if c != '0' and int(c, 0) < 0x10000: btns.add(tag + ':' + bn(c))
        if re.search(r'built_buttons|0x10\)', rhs) and fld != 'built_buttons': btns.add(tag + ':built')
        if re.search(r'ai_\w+\(|temp_ret', rhs): btns.add(tag + ':call')
    sits = {}
    for k, v in cmps.items():
        if 'situation' in k or k.startswith(('state', 'plan', 'phase', 'follow_up')):
            sits.setdefault(re.sub(r'_\d+$', '', k), set()).update(re.sub(r'(-?0x[0-9A-F]+)', lambda m: str(int(m.group(1), 0)), x) for x in v)
    ctl = set()
    cv = set(re.findall(r'(\w+) = STEP(?:_AS)?\(step, (controls_\w+)', body))
    for var, fld in cv:
        for m in re.finditer(r'\b%s & (0x[0-9A-F]+|\d+)' % var, body): ctl.add('%s&%s' % (fld.split('_')[1], bn(m.group(1))))
        for m in re.finditer(r'\b(\w+) = %s & (0x[0-9A-F]+|\d+)' % var, body): ctl.add('%s&%s' % (fld.split('_')[1], bn(m.group(2))))
    for m in re.finditer(r'STEP(?:_AS)?\(step, (controls_\w+)(?:, \w+)?\) & (0x[0-9A-F]+|\d+)', body): ctl.add('%s&%s' % (m.group(1).split('_')[1], bn(m.group(2))))
    phs = sorted(set(k.split('=', 1)[1] for k in sets if k.startswith('phase=')))
    r = []
    if plans: r.append('PLANS ' + ','.join(sorted(plans, key=lambda x: (len(x), x))))
    if btns: r.append('BTN ' + ','.join(sorted(btns)))
    if anims: r.append('ANIM ' + '; '.join(anims))
    if states: r.append('STATE ' + '; '.join(states))
    if ctl: r.append('CTL ' + ','.join(sorted(ctl)))
    if sits: r.append('TEST ' + '; '.join('%s %s' % (k, ','.join(sorted(v))) for k, v in sits.items()))
    if phs: r.append('PHASESET ' + ','.join(phs))
    r.append('CALLS ' + '; '.join(other))
    return r
if __name__ == '__main__':
    BV = __import__('bv') if False else None
    for f in sys.argv[1:]:
        e = S.names[f]; a = e['about']
        import re as _r
        for b in [r' ?Called through the plan table 0x80145A10 by the CPU step func_800F03F0 while the AI record \(\*0x8015A6F0\) \+0x00 is the plan; \+0x24 is his own situation code, \+0x48 the target\'s \(structs.md\)\.',
                  r' ?The state machine \(func_800E5890\) calls it once a step through the table at 0x80144CF0 while the wrestler\'s record \+0x14 is \d+, with the step context 0x80159080 \(\+0x00 his slot, \+0x2C his record, \+0x30 his target\'s record\); phases are his record \+0x18\.',
                  r' ?The meaning is a reading of the evidence named, not certain\.', r' ?Handed out by: [^.]*\.']:
            a = _r.sub(b, '', a)
        print('== %s %s (%s)' % (f, e['name'], S.cands[f].get('size')))
        print('  ABOUT: ' + a)
        for l in key(f): print('  ' + l)
