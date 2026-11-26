#!/usr/bin/env python3
"""The four numbers, said apart (2026-11-16, agent "honesty": the user's approval of the outside review's item 2):

  (a) converted and random-tested -- the counted figure: the game's instructions, each counted once, that converted functions
      hold (with the mod's copies and the port's stubs as lift.py score counts them), every one of those functions random-tested
      to the standard. Named so, never "proved", until the in-play twin check (lift_twin.cpp) has run and its results are in.
  (b) deliberately changed -- the converted functions the mod has a hand in: "Changed for new features" (a game function the mod
      replaces with an edited copy of its MIPS, or hooks) and "New feature" (the mod's own C, RECOMP_PATCH, converted), with the
      mod's copies apart; their count and their N64 instructions. Their behaviour is the mod's, on purpose, not the original's.
  (c) bypassed -- the bypass ledger's totals by kind (bypass_ledger.py's BYPASS_LEDGER.json).
  (d) readability debt -- in the released C++: raw N64 address literals in code, gotos, functions still named by their address,
      call sites that name their callee by its address, and the "to be tidied by hand" notes.
  (in play) its own number beside them (2026-11-18, twinnotes; the independent re-check reviewct2's headline): of the converted
      functions of a build, how many the in-play twin check compared, by which twin, with how many differences, and the caller
      twin's checks and scenarios -- read from the evidence files lift.py writes the notes from (tools/lift/inplay/*.json). Not
      "verified", not "proved", and no change to the label.

And the status label by the user's naming rule: "non-matching decompilation in progress, N% converted", N = (a). The later labels
("non-matching decompilation", "decompilation") need every game function converted and RecompiledFuncs out of the build: not met.

    python four_numbers.py [--tools tools/lift] [--release exe/WWFNoMercyRecomp/src/game] [--ledger BYPASS_LEDGER.json]
                           [--score-file FILE (lift.py score's output, saved; otherwise score is run here)] [--json OUT]
Prints the four (night.ps1 can print them in its summary); --json also writes them. Only reads.
"""
import argparse
import collections
import ctypes
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root(start):
    """Development: the first folder up from here with exe/, mod/ and tools/ in it."""
    d = start
    while True:
        if all(os.path.isdir(os.path.join(d, x)) for x in ('exe', 'mod', 'tools')):
            return d
        up = os.path.dirname(d)
        if up == d:
            raise SystemExit('four_numbers.py: no Development folder above %s' % start)
        d = up


ROOT = find_root(HERE)
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
NOTE = re.compile(r'^// (?:Random-tested|Proved): ')
ADDRESS_NAME = re.compile(r'^(?:modrep_)?func_[0-9A-F]{8}(?:_ovl_\w+)?$')
LITERAL = re.compile(r'\b0[xX]8[0-9A-Fa-f]{7}[uU]?\b')       # (an address in the N64's main memory, KSEG0: 0x80000000 up)
CALL_BY_ADDRESS = re.compile(r'lift_call<[^>]*(?:<[^>]*>)?[^>]*>\(ctx, ::((?:func|modfn|rw)_[0-9A-Fa-f]{8}\w*)\)')


def below_normal():
    try:
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    except Exception:
        pass


def score_lines(args):
    if args.score_file:
        return open(args.score_file, encoding='utf8', errors='replace').read().split('\n')
    p = subprocess.run([sys.executable, '-B', os.path.join(args.tools, 'lift.py'), 'score'], cwd=ROOT, capture_output=True, text=True)
    return (p.stdout + p.stderr).split('\n')


def number_a(lines):
    """(a) from score's lines: the counted figure and its parts."""
    find = lambda pattern: next((m for m in (re.search(pattern, l) for l in lines) if m), None)
    head = find(r'^converted, each instruction counted once: (\d+) of (\d+) instructions, ([0-9.]+)% of the game \((\d+) functions')
    copies = find(r"^the mod's copies proved, not in the line above: (\d+) functions")
    copies_once = find(r'the same counted once: ([0-9.]+)% of the game \(headline plus copies: ([0-9.]+)%\)')
    stubs = find(r'^port stubs counted through proved rw_ replacements: (\d+) of .*?, ([0-9.]+)% of the game \(headline plus copies plus stubs: ([0-9.]+)%\)')
    figure = stubs.group(3) if stubs else copies_once.group(2) if copies_once else head.group(3) if head else None
    return {'figure': float(figure) if figure else None,
            'game_own': float(head.group(3)) if head else None, 'game_functions': int(head.group(4)) if head else None,
            'held': int(head.group(1)) if head else None, 'total': int(head.group(2)) if head else None,
            'copies_functions': int(copies.group(1)) if copies else 0, 'copies_share': float(copies_once.group(1)) if copies_once else 0.0,
            'stubs': int(stubs.group(1)) if stubs else 0, 'stubs_share': float(stubs.group(2)) if stubs else 0.0,
            'different': next((l.strip() for l in lines if l.startswith('DIFFERENT:')), '')}


def release_blocks(release):
    """[(file, shown name, comment lines, code lines)] for each converted function in the release files."""
    out = []
    for f in ('lifted.cpp', 'lifted_changed.inc'):
        path = os.path.join(release, f)
        if not os.path.exists(path):
            continue
        lines = open(path, encoding='utf8').read().split('\n')
        first, last = lines.index('namespace game {'), lines.index('}  // namespace game')
        k = first + 1
        while k < last:
            if NOTE.match(lines[k]) or lines[k].startswith('// Under comparison'):
                start = k
                while lines[start - 1].startswith('//'):
                    start -= 1
                d = k + 1
                while not lines[d].strip():
                    d += 1
                end = d
                while lines[end] != '}':
                    end += 1
                m = re.match(r'[^(]*?\b(\w+)\(', lines[d])
                out.append((f, m.group(1) if m else '?', lines[start:k + 1], lines[d:end + 1]))
                k = end + 1
                continue
            k += 1
    return out


def size_of(comments):
    for c in comments:
        m = re.search(r'\((\d+) (?:N64 )?instructions\)', c)
        if m:
            return int(m.group(1))
    return 0


def number_b(blocks):
    """(b) the functions the mod has a hand in, by their first comment line (lift.py mod_mark)."""
    out = collections.OrderedDict()
    for mark, key in (('// Changed for new features (Ultimate No Mercy)', 'changed'), ('// New feature (Ultimate No Mercy)', 'new')):
        these = [b for b in blocks if b[2] and b[2][0] == mark]
        copies = [b for b in these if any("The mod's copy of the game function" in c for c in b[2])]
        out[key] = {'functions': len(these), 'instructions': sum(size_of(b[2]) for b in these),
                    'mod_copies': len(copies), 'mod_copies_instructions': sum(size_of(b[2]) for b in copies),
                    'hooked_game_functions': len(these) - len(copies), 'hooked_instructions': sum(size_of(b[2]) for b in these if b not in copies)}
    return out


def number_d(blocks, names):
    """(d) readability debt in the released C++ (code only: // comments taken out)."""
    literals, functions_with, gotos, goto_functions, calls, callees, tidy = 0, 0, 0, 0, 0, set(), 0
    by_address = [b[1] for b in blocks if ADDRESS_NAME.match(b[1])]
    distinct = set()
    for f, shown, comments, code in blocks:
        text = '\n'.join(l.split('//', 1)[0] for l in code)
        found = LITERAL.findall(text)
        literals += len(found)
        distinct |= {x.rstrip('uU').upper() for x in found}
        functions_with += bool(found)
        g = len(re.findall(r'\bgoto\s+\w+\s*;', text))
        gotos += g
        goto_functions += bool(g)
        for m in CALL_BY_ADDRESS.finditer(text):
            calls += 1
            callees.add(m.group(1))
        tidy += any('to be tidied by hand' in c for c in comments)
    named_callees = {c for c in callees if isinstance(names.get(c), dict) and names[c].get('name')}
    return {'address_literals': literals, 'address_literals_distinct': len(distinct), 'functions_with_literals': functions_with,
            'gotos': gotos, 'functions_with_gotos': goto_functions, 'functions_named_by_address': len(by_address),
            'call_sites_by_address': calls, 'callees_by_address': len(callees), 'callees_by_address_with_a_name': len(named_callees),
            'tidy_by_hand_notes': tidy, 'functions': len(blocks)}


def number_inplay(tools, blocks):
    """(in play) per evidence file (tools/lift/inplay/*.json, inplay_from_twin.py): the build, the converted functions it had,
    those compared (caller twin, leaf twin), the differing ones, the caller twin's checks and the scenarios; and how many functions
    of the release carry an in-play note now (lift.py drops a note when the function is converted differently since)."""
    folder = os.path.join(tools, 'inplay')
    out = []
    for f in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        if not f.endswith('.json'):
            continue
        ev = json.load(open(os.path.join(folder, f), encoding='utf8'))
        fs = list(ev['functions'].values())
        callers = [e for e in fs if e['twin'] == 'caller']
        out.append({'file': f, 'set': ev['set'], 'build': ev['build'], 'converted_functions': ev['converted_functions'],
                    'compared': len(fs), 'caller_twin': len(callers), 'leaf_twin': len(fs) - len(callers),
                    'differences': len(ev['differing_functions']), 'caller_checks': sum(e['n'] for e in callers),
                    'scenarios': len(ev['scenarios'])})
    noted = sum(1 for b in blocks if any(' In play: ' in c for c in b[2]))
    return out, noted


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tools', default=os.path.join(ROOT, 'tools', 'lift'))
    ap.add_argument('--release', default=os.path.join(PORT, 'src', 'game'))
    ap.add_argument('--ledger', default=os.path.join(HERE, 'BYPASS_LEDGER.json'))
    ap.add_argument('--score-file', default=None)
    ap.add_argument('--json', default=None)
    args = ap.parse_args()
    below_normal()
    a = number_a(score_lines(args))
    blocks = release_blocks(args.release)
    b = number_b(blocks)
    names = json.load(open(os.path.join(args.tools, 'names.json'), encoding='utf8'))
    d = number_d(blocks, names)
    inplay, inplay_noted = number_inplay(args.tools, blocks)
    c = json.load(open(args.ledger)) if os.path.exists(args.ledger) else None

    fig = '%.2f%%' % a['figure'] if a['figure'] is not None else 'N% (score could not be read)'
    out = ['== the four numbers (tools/release/four_numbers.py)',
           'Status: non-matching decompilation in progress, %s converted.' % fig,
           '(a) converted and random-tested: %s of the game\'s instructions, each counted once%s.' % (
               fig, ' -- the game\'s own functions %.2f%% (%d functions, %d of %d instructions), the mod\'s copies %.2f%% (%d functions), '
                    'the port\'s stubs through their rw_ %.2f%% (%d)' % (a['game_own'], a['game_functions'], a['held'], a['total'], a['copies_share'],
                                                                          a['copies_functions'], a['stubs_share'], a['stubs']) if a['game_own'] is not None else ''),
           '    Evidence: 200,000 plain random inputs each, no difference, every instruction reached or shown dead by the reviewed '
           'checker; the whole-game QA runs. ' + ('Not "proved": the in-play comparisons are their own number, below.' if inplay else
                                                  'Not "proved": the in-play twin check has not run (0 in-play comparisons in the notes).')]
    if a['different']:
        out.append('    (score also says: %s -- not released)' % a['different'])
    ch, nw = b['changed'], b['new']
    out += ['(b) deliberately changed: %d functions, %d N64 instructions -- %d "Changed for new features" (%d of the mod\'s edited copies of '
            'game functions, %d instructions; %d game functions the mod hooks, %d instructions) and %d "New feature" (the mod\'s own C '
            'converted, %d instructions). In (a) as converted, but their behaviour is the mod\'s on purpose; all in lifted_changed.inc '
            'where the release is split.' % (
                ch['functions'] + nw['functions'], ch['instructions'] + nw['instructions'], ch['functions'], ch['mod_copies'],
                ch['mod_copies_instructions'], ch['hooked_game_functions'], ch['hooked_instructions'], nw['functions'], nw['instructions'])]
    if c:
        t = c['totals']
        dead = c.get('dead_instructions', {})
        out += ['(c) bypassed: %d ledger rows -- %s; dead lines: %d instructions (unreach.py) + %d (division checks) counted as reached '
                'without a test. (BYPASS_LEDGER.md, %s)' % (
                    sum(t.values()), ', '.join('%s %d' % (k, v) for k, v in t.items() if v), dead.get('unreach', 0), dead.get('division', 0),
                    c.get('written', '?'))]
    else:
        out += ['(c) bypassed: no ledger at %s (run bypass_ledger.py first)' % args.ledger]
    out += ['(d) readability debt, in %d converted functions: %d raw N64 address literals in code (%d distinct, in %d functions); %d gotos '
            '(in %d functions); %d functions still named by their address; %d call sites name their callee by address (%d callees, %d of '
            'them have a name in names.json); %d "to be tidied by hand" notes.' % (
                d['functions'], d['address_literals'], d['address_literals_distinct'], d['functions_with_literals'], d['gotos'],
                d['functions_with_gotos'], d['functions_named_by_address'], d['call_sites_by_address'], d['callees_by_address'],
                d['callees_by_address_with_a_name'], d['tidy_by_hand_notes'])]
    for e in inplay:
        out += ['(in play) {:,} of {:,} converted functions compared in play on {} (caller twin {:,}, leaf twin {:,}), {:,} differences; '
                '{:,} caller checks in {} scenarios.'.format(e['compared'], e['converted_functions'], e['build'], e['caller_twin'],
                                                             e['leaf_twin'], e['differences'], e['caller_checks'], e['scenarios'])]
    if inplay:
        out += ['    (tools/lift/inplay; %d functions of this release carry an in-play note: one stays only while the function is converted '
                'as it was when compared.)' % inplay_noted]
    print('\n'.join(out))
    if args.json:
        json.dump({'label': 'non-matching decompilation in progress, %s converted' % fig, 'a_converted_and_random_tested': a,
                   'b_deliberately_changed': b, 'c_bypassed': c['totals'] if c else None,
                   'c_dead_instructions': c.get('dead_instructions') if c else None, 'd_readability_debt': d,
                   'in_play': inplay, 'in_play_noted_in_release': inplay_noted},
                  open(args.json, 'w', newline='\n'), indent=1)


if __name__ == '__main__':
    main()
