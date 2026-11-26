#!/usr/bin/env python3
"""rule1_qualify.py (modnative7, 2026-11-14): F5 of rule (1)'s independent review (scratch/agents/reviewpv/VERDICT.md). A function
proved only with rule (1) on (NM_RULE_WRITES=1) counts on the rule only if ALL of these hold:
  (i)   the rule changes something it reads other than a shrunk byte (a bounded private that rule (2) did not already give 0/1);
  (ii)  the rule-OFF tester, with every bounded private held at fixed real values (REVIEW_FORCE, below), finds no difference --
        so every difference the test found without the rule disappears at values the mod really writes (the test sequence is
        unchanged: no draw is made for them);
  (iii) the rule-ON tester, with the same fixed real values, finds no difference (parts 4 and 5 run: no part ends early);
  (iv)  the rule-ON run under a second seed (FUZZ_SEED) meets the full bar (its report given here).
Only then is a function "proved by the rule". Nothing here changes the shipped tester: REVIEW_FORCE is a patch to a COPY of
fuzz_main.cpp (as the reviewer's patch_force.py), linked with each run's own generated test object.

    python rule1_qualify.py --off <tag> --on <tag> --seed-report <report> --cands <candidates.json> <name> [...]
        <tag>: a finished lift.py prove's build folder, tools/lift/fuzz/build_<tag> (fuzz_gen.cpp + .o)
    QUAL_TESTS (default 40000 a combination), QUAL_COMBOS (default 6: of each private's real values, in order)
"""
import itertools, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
LIFT = os.path.join(DEV, 'tools', 'lift')
sys.path.insert(0, LIFT)
import lift

TESTS = os.environ.get('QUAL_TESTS', '40000')
COMBOS = int(os.environ.get('QUAL_COMBOS', '6'))
FORCE = '''
    // (rule1_qualify.py, after the reviewer's patch_force.py: REVIEW_FORCE="<address hex>=<value hex>,..." sets those private
    //  scalars' N64 copies to fixed values at every test's start, with no draw -- the test sequence is unchanged)
    if (const char* force = std::getenv("REVIEW_FORCE")) {
        for (const char* c = force; *c;) {
            char* end = nullptr;
            const u32 a = (u32)std::strtoul(c, &end, 16);
            if (end == nullptr || *end != '=') break;
            const uint64_t v = std::strtoull(end + 1, &end, 16);
            for (int s = 0; s < private_count; s++) {
                const FuzzPrivate& p = private_table[s];
                if (p.address == a && p.width_count == 1) set_n64_field(p.address, p.widths[0], v, false);
            }
            c = (*end == ',') ? end + 1 : end;
        }
    }
'''


def arg(k, many=False):
    a = sys.argv
    return a[a.index(k) + 1] if k in a else None


def forced_tester(tag):
    """A copy of the run's tester with REVIEW_FORCE: the patched fuzz_main.cpp linked with the run's own generated object."""
    b = os.path.join(lift.FUZZ, 'build_%s' % tag)
    exe = os.path.join(b, 'fuzz_force.exe')
    if os.path.exists(exe):
        return exe
    src = open(os.path.join(lift.FUZZ, 'fuzz_main.cpp'), encoding='utf8').read()
    anchor = '            set_n64_field(p.address, p.widths[0], written_value(p), false);\n    }\n}\n'
    assert src.count(anchor) == 1, 'native_start changed: update the patch anchor'
    src = src.replace(anchor, anchor[:-2] + FORCE + '}\n')
    main = os.path.join(b, 'fuzz_main_force.cpp')
    open(main, 'w', encoding='utf8', newline='\n').write(src)
    inc = [os.path.join(lift.PORT, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), os.path.join(lift.PORT, 'include'), lift.GAME, lift.FUZZ, b]
    flags = [lift.FUZZ_HARNESS_LEVEL if f == '-O1' else f for f in lift.FUZZ_COMPILE_FLAGS]
    obj = os.path.join(b, 'fuzz_main_force.o')
    cmd = [lift.CLANG] + flags + ['-c', '-o', obj, main]
    for i in inc:
        cmd += ['-I', i]
    subprocess.run(cmd, check=True)
    subprocess.run([lift.CLANG, '-static', '-o', exe, os.path.join(b, 'fuzz_gen.cpp.o'), obj], check=True)
    return exe


def private_addresses(tag, name):
    """{private: its tester address} from the run's generated file (the FuzzPrivate table of that candidate)."""
    t = open(os.path.join(lift.FUZZ, 'build_%s' % tag, 'fuzz_gen.cpp'), encoding='utf8', errors='replace').read()
    m = re.search(r'static const FuzzPrivate private_%s\[\] = \{(.*?)\};\n' % re.escape(name), t)
    if not m:
        return {}
    return {c: int(a, 16) for a, c in re.findall(r'\{0x([0-9A-F]{8})u, \d+u, private_widths_%s_(\w+?), \d+u, lift_mod_private_' % re.escape(name), m.group(1))}


def run(exe, name, env):
    p = subprocess.run([exe, name, TESTS], capture_output=True, text=True, cwd=LIFT, env=dict(os.environ, **env), timeout=3600)
    line = [l for l in p.stdout.split('\n') if l.startswith(name + ' ')]
    return line[-1].split() if line else ['%s' % name, 'no', 'line', 'rc=%d' % p.returncode]


def main():
    off, on, seed_report, cands_path = arg('--off'), arg('--on'), arg('--seed-report'), arg('--cands')
    names = [a for a in sys.argv[1:] if a.startswith('modrep_nat')]
    cands = json.load(open(cands_path))
    seed = {}
    for l in open(seed_report):
        f = l.split()
        if f:
            seed[f[0]] = f
    ex_off, ex_on = forced_tester(off), forced_tester(on)
    out = []
    for n in names:
        nat = cands[n]['native']
        values = nat.get('values', {})
        shrunk = set(nat.get('shrunk', []))
        reads = sorted(v for v in values if v not in shrunk)
        verdict = {'name': n, 'function': nat['name'], 'i_rule_changes_a_read': bool(reads)}
        addr_on, addr_off = private_addresses(on, n), private_addresses(off, n)
        choices = []
        for v in sorted(values):
            b = values[v]
            vals = b['list'] if 'list' in b else sorted({b['range'][0], b['range'][1], (b['range'][0] + b['range'][1]) // 2})
            choices.append([(v, x) for x in vals])
        combos = list(itertools.islice(itertools.product(*choices), COMBOS)) if choices else []
        diffs_off, diffs_on = [], []
        for combo in combos:
            def force(addrs):
                w = {}
                for v, x in combo:
                    c = re.sub(r'\W', '_', v)
                    if c in addrs:
                        w[addrs[c]] = x & 0xFFFFFFFF
                return ','.join('%08X=%X' % kv for kv in w.items())
            f_off = run(ex_off, n, {'REVIEW_FORCE': force(addr_off)})
            f_on = run(ex_on, n, {'REVIEW_FORCE': force(addr_on)})
            diffs_off.append((dict(combo), f_off[3] if len(f_off) > 3 else '?', f_off[6] if len(f_off) > 6 else ''))
            diffs_on.append((dict(combo), f_on[3] if len(f_on) > 3 else '?', f_on[6] if len(f_on) > 6 else ''))
        verdict['ii_off_forced_no_difference'] = bool(combos) and all(d == '0' for _, d, _ in diffs_off)
        verdict['iii_on_forced_no_difference'] = bool(combos) and all(d == '0' for _, d, _ in diffs_on)
        s = seed.get(n)
        verdict['iv_second_seed_meets_bar'] = bool(s) and len(s) == 8 and s[3] == '0' and bin(int(s[4], 16)).count('1') >= int(s[5]) and int(s[7]) >= 200000
        verdict['QUALIFIES'] = all(verdict[k] for k in ('i_rule_changes_a_read', 'ii_off_forced_no_difference', 'iii_on_forced_no_difference', 'iv_second_seed_meets_bar'))
        verdict['forced_off'] = diffs_off
        verdict['forced_on'] = diffs_on
        verdict['second_seed_line'] = ' '.join(s) if s else None
        out.append(verdict)
        print('%-34s %-22s i=%s ii=%s iii=%s iv=%s -> %s' % (n, nat['name'], verdict['i_rule_changes_a_read'], verdict['ii_off_forced_no_difference'],
                                                          verdict['iii_on_forced_no_difference'], verdict['iv_second_seed_meets_bar'],
                                                          'COUNTS' if verdict['QUALIFIES'] else 'does not count'), flush=True)
    json.dump(out, open(os.path.join(HERE, 'rule1_qualify.json'), 'w'), indent=1, default=str)


if __name__ == '__main__':
    main()
