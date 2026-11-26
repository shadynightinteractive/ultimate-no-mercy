#!/usr/bin/env python3
"""The bypass ledger: every function or code path of the game that the port does not run as the original's converted code, or
that the evidence counts without testing it -- skipped, stubbed, replaced, hooked, counted as dead, or judged by hand. One row
each, with its N64 address, its name (tools/lift/names.json), its kind, why it is believed dead or inapplicable, and the evidence.
(2026-11-16, agent "honesty": the user's approval of the outside review's item 1.)

It only reads. Its sources, each named in the row it makes:
  - the converted set: the release's src/game/lifted.cpp (and lifted_changed.inc, where the release has one): each function's
    extern "C" way in;
  - dead lines: what the random-input tester counts as reached without a test -- tools/lift/division_checks.py's division checks
    and tools/lift/unreach.py's dead instructions, worked out exactly as lift.py's tester text does it (its unreach_dead, with the
    rule switches of the environment this runs in), for each converted function. The night does not write them to a file of
    their own: they are compiled into the tester (fuzz_gen.cpp's never_runs_<name> lines), so they are worked out again here;
  - nomercy.toml [patches]: stubs, ignored, and the instruction patches (N64Recomp's input);
  - the mod: the game functions it replaces (an edited copy of their MIPS in mod/build/patches/mod.S, or C with RECOMP_PATCH in
    mod/src or mod/recomp) and those it hooks (RECOMP_HOOK / RECOMP_HOOK_RETURN), plus the other mods the game loads (their
    built .nrm files, as lift.py patched_by_mod reads them); tools/lift/modrep (the converted copies) and hooks.py;
  - the six-man port's stubs: game functions whose first instruction the port made `j rw_X` (lift.py port_stubs);
  - the PC port's hand edits in RecompiledFuncs (exe/WWFNoMercyRecomp/tools/readd_hand_edits.py), carried into conversions by
    tools/lift/port_additions.py;
  - registers judged not to be inputs: tools/lift/independent_by_hand.json (by hand, reviewed) and independent.json (the
    automatic test);
  - tests cut short: functions with no return (a thread's loop), whose tests end at the K-th call (fuzz_main.cpp cut_call).

    python bypass_ledger.py [--tools tools/lift] [--release exe/WWFNoMercyRecomp/src/game] [--out DIR]
writes DIR/BYPASS_LEDGER.md (totals by kind first, then one table per kind) and DIR/BYPASS_LEDGER.json (the same rows, the dead
lines' every address). Default DIR: the folder this script is in. Below-normal priority.
"""
import argparse
import collections
import ctypes
import datetime
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root(start):
    """Development: the first folder up from here with exe/, mod/ and tools/ in it (so this runs from scratch/agents/honesty or
    from tools/lift alike)."""
    d = start
    while True:
        if all(os.path.isdir(os.path.join(d, x)) for x in ('exe', 'mod', 'tools')):
            return d
        up = os.path.dirname(d)
        if up == d:
            raise SystemExit('bypass_ledger.py: no Development folder above %s' % start)
        d = up


ROOT = find_root(HERE)
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
MOD = os.path.join(ROOT, 'mod')
ADAPTER = re.compile(r'^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{', re.M)

# The kinds, in the order the ledger shows them, and what each means.
KINDS = collections.OrderedDict([
    ('hardware stub', "N64Recomp makes the function an empty one (nomercy.toml [patches] stubs): it has instructions with no meaning "
                      "on a PC (cache, coprocessor 0 / TLB). The game calls it; nothing runs."),
    ('ignored', "N64Recomp does not translate it (nomercy.toml [patches] ignored): calls go to the runtime's own function."),
    ('instruction patch', "One instruction changed before translation (nomercy.toml [[patches.instruction]])."),
    ('mod replacement (edited MIPS)', "The mod replaces the game function with an edited copy of its MIPS (mod/build/patches/mod.S). "
                                      "The game's own version never runs while the mod is loaded; it is never converted."),
    ('mod replacement (C)', "The mod replaces the game function with C of its own (RECOMP_PATCH). The game's version never runs while "
                            "the mod is loaded; it is never converted."),
    ('mod hook', "The mod runs code of its own at the function's entry and/or return (RECOMP_HOOK). The function's own code still "
                 "runs; in the tester the hook is a stand-in (hooks.py), so the hook's code is not tested with it."),
    ('other mod replacement or hook', "A mod the game ships besides ours replaces or hooks it (its built .nrm): never converted."),
    ('port stub (j rw_)', "The six-man port made the function's first instruction a jump to rw_X, its own MIPS for it: the "
                          "function's original body never runs; rw_X does its work. Counted through rw_X when rw_X is converted."),
    ('port hand edit', "The PC port's own code added to the translated function (readd_hand_edits.py): not the original's alone."),
    ('dead lines (unreach.py)', "Instructions the reviewed dead-code checker shows can never run: the tester counts them as reached "
                                "without a test. Every other instruction must run in a compared test."),
    ('dead lines (division checks)', "The compiler's division checks (break 6 / 7) division_checks.py shows can never run: counted "
                                     "as reached without a test."),
    ('input judged unused (by hand)', "A register the function reads on some path, judged by hand not to matter "
                                      "(independent_by_hand.json): the tester does not vary it as an input."),
    ('input found unused (automatic)', "A register the automatic independence test found not to change the result "
                                       "(independent.json): the tester does not vary it as an input."),
    ('test cut short (no return)', "The function has no return (a thread's loop): each test ends at its K-th call "
                                   "(fuzz_main.cpp cut_call), so what follows that is not tested."),
])


def below_normal():
    try:
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x4000)
    except Exception:
        pass


def address_of(name, functions):
    """'0x80012BE0' (with the overlay, if any) from a func_ name; for another name, its first instruction's address."""
    m = re.match(r'^(?:modrep_)?func_([0-9A-F]{8})(_ovl_\w+)?$', name)
    if m:
        return '0x%s%s' % (m.group(1), ' (%s)' % m.group(2)[1:] if m.group(2) else '')
    f = functions.get(name) if functions else None
    if f and f.get('ins'):
        return '0x%08X' % min(f['ins'])
    return '-'


def released(release):
    """The converted functions in the release: each one's extern "C" way in, in lifted.cpp and lifted_changed.inc."""
    out = set()
    for f in ('lifted.cpp', 'lifted_changed.inc'):
        path = os.path.join(release, f)
        if os.path.exists(path):
            out |= set(ADAPTER.findall(open(path, encoding='utf8').read()))
    return out


def toml_patches():
    """nomercy.toml's stubs, ignored, and instruction patches (with the comment above each block)."""
    text = open(os.path.join(PORT, 'nomercy.toml'), encoding='utf8').read()
    stubs = re.findall(r'"(func_\w+)"', re.search(r'^stubs = \[(.*?)^\]', text, re.M | re.S).group(1))
    ignored_block = re.search(r'^ignored = \[(.*?)^\]', text, re.M | re.S).group(1)
    ignored = re.findall(r'^\s*"(\w+)"', ignored_block, re.M)
    ignored_why = ' '.join(l.strip().lstrip('#').strip() for l in ignored_block.split('\n') if l.strip().startswith('#'))
    patches = []
    for m in re.finditer(r'((?:^#.*\n)*)\[\[patches\.instruction\]\]\nfunc = "(\w+)"\nvram = (0x[0-9A-Fa-f]+)\nvalue = (0x[0-9A-Fa-f]+)', text, re.M):
        why = ' '.join(l.lstrip('#').strip() for l in m.group(1).strip().split('\n'))
        patches.append((m.group(2), m.group(3), m.group(4), why))
    return stubs, ignored, ignored_why, patches


COP0_FUNCT = {0x01: 'tlbr', 0x02: 'tlbwi', 0x06: 'tlbwr', 0x08: 'tlbp', 0x18: 'eret'}


def stub_instructions(stubs):
    """For each stub, the instructions that made it one (gen_stubs.py's test: COP0 or CACHE), read from the ROM's code by
    dump.toml's sections. Only reads the ROM file; copies nothing from it."""
    rom_path = os.path.join(PORT, 'nomercy.z64')
    if not os.path.exists(rom_path):
        return {}
    rom = open(rom_path, 'rb').read()
    out, sec_rom, sec_vram = {}, 0, 0
    for line in open(os.path.join(PORT, 'syms', 'dump.toml'), encoding='utf8'):
        m = re.match(r'rom = (0x[0-9A-Fa-f]+)', line)
        if m:
            sec_rom = int(m.group(1), 16)
        m = re.match(r'vram = (0x[0-9A-Fa-f]+)', line)
        if m:
            sec_vram = int(m.group(1), 16)
        m = re.search(r'\{ name = "([^"]+)", vram = (0x[0-9A-Fa-f]+), size = (0x[0-9A-Fa-f]+)', line)
        if not m or m.group(1) not in stubs:
            continue
        vram, size = int(m.group(2), 16), int(m.group(3), 16)
        start = sec_rom + vram - sec_vram
        found = []
        for k in range(0, size, 4):
            w = struct.unpack_from('>I', rom, start + k)[0]
            op = w >> 26
            if op == 0x2F:
                found.append('cache 0x%X at 0x%08X' % ((w >> 16) & 0x1F, vram + k))
            elif op == 0x10:
                rs = (w >> 21) & 0x1F
                what = {0: 'mfc0', 4: 'mtc0'}.get(rs) or (COP0_FUNCT.get(w & 0x3F, 'cop0 0x%X' % (w & 0x3F)) if rs == 0x10 else 'cop0 rs %d' % rs)
                if what in ('mfc0', 'mtc0'):
                    what += ' $%d' % ((w >> 11) & 0x1F)
                found.append('%s at 0x%08X' % (what, vram + k))
        out[m.group(1)] = found
    return out


def mod_sources():
    """{game function: [(kind, file:line, the comment above it)]} for RECOMP_PATCH / RECOMP_HOOK in the mod's C, and
    {game function: its one-line comment} from mod/build/patches/mod.S."""
    found = collections.defaultdict(list)
    for base in (os.path.join(MOD, 'src'), os.path.join(MOD, 'recomp')):
        for dirpath, _, files in os.walk(base):
            for fn in sorted(files):
                if not fn.endswith(('.c', '.h')):
                    continue
                path = os.path.join(dirpath, fn)
                lines = open(path, encoding='utf8', errors='replace').read().split('\n')
                for i, line in enumerate(lines):
                    m = re.search(r'\bRECOMP_PATCH\s+[A-Za-z_][\w \t*]*?\b(func_[0-9A-F]{8}(?:_ovl_\w+)?)\s*\(', line)
                    kind = 'patch'
                    if not m:
                        m = re.search(r'\bRECOMP_HOOK(_RETURN)?\s*\(\s*"(func_[0-9A-F]{8}(?:_ovl_\w+)?)"\s*\)\s*\w+\s+(\w+)', line)
                        if not m:
                            continue
                        kind = 'hook return' if m.group(1) else 'hook entry'
                        target, hook_fn = m.group(2), m.group(3)
                    else:
                        target, hook_fn = m.group(1), None
                    above = []
                    k = i - 1
                    while k >= 0 and lines[k].strip().startswith(('//', '/*', '*')) and len(above) < 3:
                        above.insert(0, lines[k].strip().lstrip('/*').strip())
                        k -= 1
                    rel = os.path.relpath(path, ROOT).replace('\\', '/')
                    found[target].append((kind, '%s:%d' % (rel, i + 1), hook_fn, ' '.join(above)))
    asm = {}
    mod_s = os.path.join(MOD, 'build', 'patches', 'mod.S')
    if os.path.exists(mod_s):
        for m in re.finditer(r'^# (func_[0-9A-F]{8}(?:_ovl_\w+)?)\b[^\n]*?\)?\s*(.*)\n\s*\.section \.recomp_patch[^\n]*\n\s*\.globl (func_\w+)',
                             open(mod_s, encoding='utf8', errors='replace').read(), re.M):
            asm[m.group(3)] = re.sub(r'^\(\w+\)\s*', '', m.group(2)).strip()
    return found, asm


def hand_edits():
    """[(function, tag, the comment)] from readd_hand_edits.py: each function its edits name."""
    path = os.path.join(PORT, 'tools', 'readd_hand_edits.py')
    if not os.path.exists(path):
        return []
    lines = open(path, encoding='utf8', errors='replace').read().split('\n')
    out, tag = [], ''
    for i, line in enumerate(lines):
        t = line.strip()
        if t.startswith('# [') or t.startswith('# The latch'):
            tag = t.lstrip('# ').strip()
            tag = tag.split('. ')[0].rstrip('.') + '.'        # (its first sentence: the comment goes on over several lines)
        for m in re.finditer(r"(?:body_range\(\w+, |_insert_after_all\('[\w.]+', )'(func_[0-9A-F]{8})'", line):
            what = tag if 'body_range' in line else '[nomercy][rt] ray-tracing camera hand-off (rt_hand_edits)'
            out.append((m.group(1), what, 'readd_hand_edits.py:%d' % (i + 1)))
    seen, uniq = set(), []
    for row in out:
        if (row[0], row[1]) not in seen:
            seen.add((row[0], row[1]))
            uniq.append(row)
    return uniq


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tools', default=os.path.join(ROOT, 'tools', 'lift'))
    ap.add_argument('--release', default=os.path.join(PORT, 'src', 'game'))
    ap.add_argument('--out', default=HERE)
    args = ap.parse_args()
    below_normal()
    sys.dont_write_bytecode = True
    sys.path.insert(0, os.path.abspath(args.tools))
    import lift            # noqa: E402  (the tools copy given: its rules, switches and data)
    import to_asm          # noqa: E402
    import division_checks  # noqa: E402
    import modrep          # noqa: E402
    import unreach         # noqa: E402

    names = json.load(open(os.path.join(args.tools, 'names.json'), encoding='utf8'))
    shown = lambda n: names.get(n, {}).get('name', '') if isinstance(names.get(n), dict) else ''
    functions = to_asm.read_functions()
    done = released(args.release)
    cands = json.load(open(lift.CANDIDATES))
    rows = []

    def row(kind, name, reason, evidence, **extra):
        r = {'kind': kind, 'name': name, 'address': address_of(name, functions), 'shown': shown(name) or shown(name.replace('modrep_', '')),
             'reason': reason, 'evidence': evidence}
        r.update(extra)
        rows.append(r)

    # 1. nomercy.toml [patches]
    stubs, ignored, ignored_why, patches = toml_patches()
    instr = stub_instructions(set(stubs))
    callers = lift.callers_of(functions)
    for n in stubs:
        found = instr.get(n, [])
        conv_callers = sorted(c for c in callers.get(n, []) if c in done)
        listed = ', '.join(found[:4]) + (' and %d more' % (len(found) - 4) if len(found) > 4 else '')
        row('hardware stub', n,
            ('has %s: no meaning on the PC (memory is coherent, there is no TLB or N64 coprocessor 0); the runtime does the '
             'equivalent where one is needed' % listed)
            if found else 'listed as a stub (no cache / coprocessor 0 instruction found in the ROM read here)',
            "nomercy.toml [patches] stubs, made by exe/WWFNoMercyRecomp/tools/gen_stubs.py (every func_ with a COP0 or CACHE "
            "instruction); checked against WM2000's stub set, verified there as no-ops (nomercy.toml's comment). Converted callers: %s"
            % (', '.join(conv_callers[:6]) + (' and %d more' % (len(conv_callers) - 6) if len(conv_callers) > 6 else '') if conv_callers else 'none'),
            instructions=found)
    for n in ignored:
        row('ignored', n, ignored_why or 'routed to the runtime', 'nomercy.toml [patches] ignored')
    for func, vram, value, why in patches:
        row('instruction patch', func, why, 'nomercy.toml [[patches.instruction]]: %s at %s' % (value, vram))

    # 2. the mod: replacements and hooks (lift.py patched_by_mod: our mod's sources, mod.S, and every built mod's .nrm)
    patched = lift.patched_by_mod()
    replaced = set(lift.REPLACED_BY_MOD)
    src, asm = mod_sources()
    index = modrep.index()
    copy_of = {rec['replaces']: n for n, rec in index.items()}
    ours = set(asm) | set(src)
    for n in sorted(replaced):
        copy = copy_of.get(n)
        conv = copy in done if copy else False
        state = ('its converted copy %s is in the release (used only when the loaded mod has the same copy, by hash)' % copy if conv
                 else 'the mod\'s copy runs as the loader translates it (not converted)')
        if n in asm:
            row('mod replacement (edited MIPS)', n, asm[n] or "the mod's change to it (mod/gen)", 'mod/build/patches/mod.S; %s' % state,
                converted_copy=copy if conv else None)
        elif any(k == 'patch' for k, *_ in src.get(n, [])):
            k, where, _, above = next(x for x in src[n] if x[0] == 'patch')
            row('mod replacement (C)', n, above or 'the mod\'s C in its place', '%s (RECOMP_PATCH); %s' % (where, state),
                converted_copy=copy if conv else None)
        elif n not in ours:
            row('other mod replacement or hook', n, 'replaced by another mod the game loads', "a built .nrm's replacements (lift.py mod_targets)")
        else:
            row('mod replacement (C)', n, 'replaced by the mod', 'lift.py REPLACED_BY_MOD; %s' % state, converted_copy=copy if conv else None)
    for n in sorted(patched - replaced):
        hooks_here = [x for x in src.get(n, []) if x[0].startswith('hook')]
        if hooks_here:
            conv = n in done
            row('mod hook', n, '; '.join('%s: %s%s' % (k, fn, (' -- ' + above) if above else '') for k, where, fn, above in hooks_here[:3]),
                '%s; %s' % (', '.join(where for _, where, _, _ in hooks_here[:3]),
                            'converted with its hook calls in (hooks.py): in the release' if conv else 'not converted: the loader\'s hooked translation runs'))
        else:
            row('other mod replacement or hook', n, 'hooked by another mod the game loads', "a built .nrm's hooks (lift.py mod_targets)")

    # 3. the six-man port's stubs (lift.py port_stubs, as score counts them)
    analysis = json.load(open(os.path.join(args.tools, 'analysis.json')))
    stubs_out = {}
    game_done = {n for n in done if not modrep.is_modrep(n)}
    try:
        lift.counted_once(game_done, analysis, stubs_out)
    except Exception as e:      # (said, not fatal: the ledger's other kinds don't need it)
        print('port stubs not worked out: %s' % e)
    for n, target in sorted(stubs_out.get('stubs', {}).items()):
        counted = n in stubs_out.get('counted', [])
        row('port stub (j rw_)', n, "the six-man port's own MIPS for it, %s, does its work" % target,
            'its first instruction is `j %s` in the six-man ROM (lift.py port_stubs); %s' % (
                target, 'rw_ converted, so counted in the figure' if counted else 'rw_ not converted: not counted'), replaced_by=target, counted=counted)

    # 4. the PC port's hand edits
    for n, tag, where in hand_edits():
        carried = bool(cands.get(n, {}).get('port'))
        row('port hand edit', n, tag, '%s; %s' % (where, 'converted with the edit carried in (port_additions.py)' if n in done and carried
                                                  else 'in the release' if n in done else 'not converted: the edited translation runs'))

    # 5. dead lines, for each converted function, as the tester text has them (lift.py unreach_dead and division_checks)
    rules = {k: getattr(unreach, k) for k in sorted(dir(unreach)) if re.match(r'^RULE_[A-Z_]+$', k)}
    lift.rule_p_ready()
    roots = None
    dead_total = collections.Counter()
    for n in sorted(done):
        f = functions.get(n)
        if not f or 'cpp' not in cands.get(n, {}):
            continue
        div = sorted(division_checks.dead_checks(f))
        if modrep.is_modrep(n):
            res = unreach.dead(f, (), keep_saved=False) if unreach.RULE_R else ('rule R off', {})
        else:
            if roots is None:
                roots = {k: set(v) for k, v in json.load(open(lift.UNREACH_ROOTS))['roots'].items()} if os.path.exists(lift.UNREACH_ROOTS) else {}
            res = unreach.dead(f, () if unreach.RULE_E else roots.get(n, ()), keep_saved=False)
        why, dead = res
        un = {} if why else dead
        un = {a: r for a, r in un.items() if a not in div}
        if un:
            reasons = collections.Counter(un.values())
            row('dead lines (unreach.py)', n, '%d instruction(s): %s' % (len(un), '; '.join('%s (%d)' % (r, c) for r, c in reasons.most_common(3))),
                'unreach.py, strict mode (after a call nothing is known), rules on: %s; approved by the user 2026-11-09/10 after an '
                'independent review' % ', '.join(k[5:] for k, v in rules.items() if v is True), lines=['0x%08X' % a for a in sorted(un)])
            dead_total['unreach'] += len(un)
        if div:
            row('dead lines (division checks)', n, '%d check instruction(s): the divisor can be neither 0 nor -1 there' % len(div),
                'division_checks.py (value ranges from the function\'s own MIPS)', lines=['0x%08X' % a for a in div])
            dead_total['division'] += len(div)

    # 6. inputs judged unused
    by_hand = json.load(open(os.path.join(args.tools, 'independent_by_hand.json'), encoding='utf8'))
    for n, v in sorted(by_hand.items()):
        if not isinstance(v, dict):
            continue
        row('input judged unused (by hand)', n, '%s: %s' % (', '.join(v.get('registers', [])), v.get('why', '')),
            'independent_by_hand.json%s%s' % (' (reviewed: an analyst and three skeptics, 2026-11-05)' if v.get('reviewed') else '',
                                              '; matters only if: ' + v['condition'] if v.get('condition') else ''))
    auto = json.load(open(os.path.join(args.tools, 'independent.json'), encoding='utf8'))
    for n, regs in sorted(auto.items()):
        row('input found unused (automatic)', n, ', '.join(regs), 'independent.json (lift.py independence: 100,000 tests varying it, '
                                                                  'no change in the result)')

    # 7. tests cut short
    for n in sorted(done):
        f = functions.get(n)
        if f and f.get('ins') and lift.never_returns(f):
            row('test cut short (no return)', n, 'no `jr $ra`: a loop that never ends by itself', 'lift.py never_returns; fuzz_main.cpp '
                'cut_call (the rule the user approved 2026-11-12)')

    write(args, rows, rules, dead_total, len(done))


def esc(t):
    return str(t).replace('|', '\\|').replace('\n', ' ')


def write(args, rows, rules, dead_total, n_done):
    by_kind = collections.OrderedDict((k, [r for r in rows if r['kind'] == k]) for k in KINDS)
    now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M')
    tools_rel = os.path.relpath(os.path.abspath(args.tools), ROOT).replace('\\', '/')
    rel_rel = os.path.relpath(os.path.abspath(args.release), ROOT).replace('\\', '/')
    md = ['# Bypass ledger', '',
          'WRITTEN BY tools/release/bypass_ledger.py (%s), from `%s` and the release in `%s` (%d converted functions). '
          'Every function or code path the port does not run as the original\'s converted code, or that the evidence counts without '
          'testing it. It says what is bypassed and why; it does not say any of it is wrong.' % (now, tools_rel, rel_rel, n_done), '',
          '## Totals by kind', '', '| kind | rows | what it means |', '|---|---:|---|']
    for k, rs in by_kind.items():
        extra = ''
        if k == 'dead lines (unreach.py)':
            extra = ' (%d instructions)' % dead_total['unreach']
        if k == 'dead lines (division checks)':
            extra = ' (%d instructions)' % dead_total['division']
        md.append('| %s | %d%s | %s |' % (k, len(rs), extra, esc(KINDS[k])))
    md += ['| **all** | **%d** | |' % len(rows), '',
           'The rule switches of unreach.py as this ran: %s.' % ', '.join('%s %s' % (k[5:], 'on' if v is True else 'off' if v is False else v)
                                                                       for k, v in rules.items()), '']
    for k, rs in by_kind.items():
        md += ['## %s (%d)' % (k, len(rs)), '', esc(KINDS[k]), '']
        if not rs:
            md += ['None.', '']
            continue
        md += ['| N64 address | function | name | reason | evidence |', '|---|---|---|---|---|']
        for r in rs:
            md.append('| %s | %s | %s | %s | %s |' % (r['address'], r['name'], esc(r['shown'] or '-'), esc(r['reason']), esc(r['evidence'])))
        md.append('')
    os.makedirs(args.out, exist_ok=True)
    open(os.path.join(args.out, 'BYPASS_LEDGER.md'), 'w', newline='\n', encoding='utf8').write('\n'.join(md))
    json.dump({'written': now, 'tools': tools_rel, 'release': rel_rel, 'converted': n_done,
               'totals': {k: len(v) for k, v in by_kind.items()}, 'dead_instructions': dict(dead_total),
               'rules': {k: v for k, v in rules.items() if isinstance(v, (bool, int, str))}, 'rows': rows},
              open(os.path.join(args.out, 'BYPASS_LEDGER.json'), 'w', newline='\n'), indent=0)
    print('bypass ledger: %d rows (%s)' % (len(rows), ', '.join('%s %d' % (k, len(v)) for k, v in by_kind.items() if v)))


if __name__ == '__main__':
    main()
