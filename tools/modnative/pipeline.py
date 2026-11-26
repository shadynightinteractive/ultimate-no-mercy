#!/usr/bin/env python3
"""tools/modnative/pipeline.py (modnative7, 2026-11-14; from scratch/agents/modnative5-7's pipeline.sh): the native mod's proof, after
build_mod.ps1 has built the mod (mod/build/mod_six) -- Stage 1 of that build (w/mod_aot), modrep's translation, the private data,
the analysis, the mod's IR and debug information, the inventory, the census (native.py), the candidates (prove_gen.py put /
check), the negative controls -- then lift.py prove of them all (tools/lift; LIFT_WORKERS, low priority), and the verdicts
(m5_analyse.py). night.ps1 runs it, then tools/aot/mod_native.py release and check. The rules the user approved on 2026-11-13/14
are on (their switches: prove_gen.py's head); NM_RULE_WRITES=1 (private values) only after its independent review.

    python tools/modnative/pipeline.py [<tag>]        (default tag "native": tools/lift/fuzz/report_native.txt)
Each step's output goes to pipe_<step>.log here; a step that fails stops it (exit 1), saying which.
"""
import json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
LIFT = os.path.join(DEV, 'tools', 'lift')
TAG = sys.argv[1] if len(sys.argv) > 1 else 'native'
# (merge084b, 2026-11-15: the three all-shard seed families of the native-mod bar -- report_<tag>_<family>.txt; see main's end)
SEED_FAMILIES = [('fd', None), ('fs', '777'), ('ft', '4242')]


def step(name, args, cwd=HERE, env=None):
    print('== %s' % name, flush=True)
    with open(os.path.join(HERE, 'pipe_%s.log' % name), 'w') as log:
        p = subprocess.run(args, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, env=env)
    if p.returncode:
        raise SystemExit('native mod pipeline: %s failed (exit %d): see tools/modnative/pipe_%s.log' % (name, p.returncode, name))


def main():
    env = dict(os.environ)
    env.setdefault('NM_MOD_DIR', os.path.join(DEV, 'mod'))
    env['LIFT_MOD_BUILD'] = os.path.join(env['NM_MOD_DIR'], 'build', 'mod_six')
    env['LIFT_AOT_DIR'] = os.path.join(HERE, 'w', 'mod_aot')
    env['LIFT_PRIORITY'] = 'low'
    env.setdefault('NM_UNNAMED_OK', '1')     # (a place no symbol names: the tester decides -- prove_gen.py why_not)
    py = [sys.executable, os.path.join(HERE, 'lowpri.py')]
    for d in ('w', os.path.join('w', 'mod_aot'), 'ir', 'dwarf'):
        os.makedirs(os.path.join(HERE, d), exist_ok=True)
    os.environ.update(env)
    # (merge084b, 2026-11-15: this run's second-seed and seed-family reports are made afresh below; one left from an earlier
    #  night must never stand in for them -- gone first, so a run that stops before them leaves none, and night.ps1 then
    #  releases nothing new)
    for suffix in ['_s2'] + ['_' + f for f, _ in SEED_FAMILIES]:
        r = os.path.join(LIFT, 'fuzz', 'report_%s%s.txt' % (TAG, suffix))
        if os.path.exists(r):
            os.remove(r)
    step('aot', py + ['aot_copy.py', os.path.join(env['LIFT_MOD_BUILD'], 'nomercy_arcade.nrm')], env=env)
    step('modrep', py + ['modrep.py'], cwd=LIFT, env=env)
    step('private', py + ['private_data.py'], cwd=LIFT, env=env)
    step('analyse', py + [os.path.join(LIFT, 'analyse.py')], cwd=DEV, env=env)
    step('dwarf', py + ['gen_dwarf_ir.py', env['NM_MOD_DIR']], env=env)
    step('shrunk', [sys.executable, 'shrunk.py'], env=env)
    step('inventory', py + ['inventory.py'], env=env)
    step('census', py + ['native.py', 'census'], env=env)
    step('page_sites', [sys.executable, os.path.join('dev', 'page_sites.py')], env=env)
    step('page_pads', [sys.executable, os.path.join('dev', 'page_pads.py')], env=env)
    step('list', py + ['prove_gen.py', 'list'], env=env)
    ok = json.load(open(os.path.join(HERE, 'provable_now.json')))['ok']
    env['NM_CANDIDATES_BASE'] = os.path.join(LIFT, 'candidates.json')   # (the game's candidates stay; native ones made again)
    step('put', py + ['prove_gen.py', 'put'] + ok, env=env)
    put = sorted(k for k in json.load(open(os.path.join(LIFT, 'candidates.json'))) if k.startswith('modrep_nat_'))
    step('check', py + ['prove_gen.py', 'check'] + put, env=env)
    failed = json.load(open(os.path.join(HERE, 'check_failed.json')))
    checked = [n for n in put if n not in failed]
    step('neg', py + ['prove_gen.py', 'neg'], env=env)
    negs = open(os.path.join(HERE, 'pipe_neg.log')).read().split()
    # (2026-11-14, modwhole: the gameplay tier, when night.ps1 asks for it -- NM_NATIVE_WHOLE=1. prove_gen.py NM_WHOLE=1 makes an
    #  entry also for each function refused only for the tester's sake, into whole/candidates.json APART from the candidates
    #  proved here, which stay exactly as without it; mod_native.py release --whole whole/candidates.json takes from it every
    #  function not proved. Nothing here is proved.)
    if os.environ.get('NM_NATIVE_WHOLE') == '1':
        envw = dict(env, NM_WHOLE='1', NM_STAGE=os.path.join(HERE, 'whole'))
        os.makedirs(envw['NM_STAGE'], exist_ok=True)
        step('list_whole', py + ['prove_gen.py', 'list'], env=envw)
        okw = json.load(open(os.path.join(HERE, 'provable_now.json')))['ok']
        step('put_whole', py + ['prove_gen.py', 'put'] + okw, env=envw)
        putw = sorted(k for k in json.load(open(os.path.join(envw['NM_STAGE'], 'candidates.json'))) if k.startswith('modrep_nat_'))
        step('check_whole', py + ['prove_gen.py', 'check'] + putw, env=envw)
        print('gameplay tier: %d entries made, %d compile alone' % (len(putw), len(putw) - len(json.load(open(os.path.join(HERE, 'check_failed.json'))))),
              flush=True)
    print('candidates: %d, negative controls: %d' % (len(checked), len(negs)), flush=True)
    env.update({'FUZZ_MS': env.get('FUZZ_MS', '300000'), 'LIFT_WORKERS': env.get('LIFT_WORKERS', '3'),
                'LIFT_EXTRA_ORIGINALS': os.path.join(LIFT, 'modrep', 'out', 'nat_originals.c')})
    step('prove', [sys.executable, 'lift.py', 'prove', TAG] + checked + negs, cwd=LIFT, env=env)
    with open(os.path.join(HERE, 'analyse_%s.txt' % TAG), 'w') as out:
        subprocess.run([sys.executable, 'm5_analyse.py', os.path.join(LIFT, 'candidates.json'), TAG], cwd=HERE, stdout=out,
                       stderr=subprocess.STDOUT, env=env)
    lines = open(os.path.join(HERE, 'analyse_%s.txt' % TAG)).read().split('\n')
    print('\n'.join(l for l in lines if l.startswith(('run ', 'negative controls'))))
    import re
    neg = next((l for l in lines if l.startswith('negative controls')), '')
    m = re.match(r'negative controls: (\d+), DIFFERENT: (\d+)', neg)
    if not m or m.group(1) != m.group(2):
        raise SystemExit('native mod pipeline: a negative control was not DIFFERENT (%s) -- the tester is not to be trusted: stop' % neg)
    # (2026-11-14, the coordinator's order after rule (1)'s review: the tester can write a private object's N64 copy, which no real
    #  code can, so a result may depend on the test sequence. Every function proved here is proved again under a second seed --
    #  FUZZ_SEED, another sequence of tests altogether, at the full bar -- with the negative controls; the release takes only what
    #  meets the bar in both (mod_native.py release --both report_<tag>_s2.txt). Extra testing, not a rule change.)
    proved = open(os.path.join(HERE, 'proved_%s.txt' % TAG)).read().split()
    env2 = dict(env, FUZZ_SEED=os.environ.get('NM_SECOND_SEED', '777'))
    step('prove_s2', [sys.executable, 'lift.py', 'prove', TAG + '_s2'] + proved + negs, cwd=LIFT, env=env2)
    with open(os.path.join(HERE, 'analyse_%s_s2.txt' % TAG), 'w') as out:
        subprocess.run([sys.executable, 'm5_analyse.py', os.path.join(LIFT, 'candidates.json'), TAG + '_s2'], cwd=HERE, stdout=out,
                       stderr=subprocess.STDOUT, env=env)
    lines2 = open(os.path.join(HERE, 'analyse_%s_s2.txt' % TAG)).read().split('\n')
    print('second seed: ' + '\n'.join(l for l in lines2 if l.startswith(('run ', 'negative controls'))))
    m = re.match(r'negative controls: (\d+), DIFFERENT: (\d+)', next((l for l in lines2 if l.startswith('negative controls')), ''))
    if not m or m.group(1) != m.group(2):
        raise SystemExit('native mod pipeline: a negative control was not DIFFERENT under the second seed -- stop')
    both = sorted(set(proved) & set(open(os.path.join(HERE, 'proved_%s_s2.txt' % TAG)).read().split()))
    open(os.path.join(HERE, 'proved_%s_both.txt' % TAG), 'w', newline='\n').write(' '.join(both))
    print('proved under both seeds: %d of %d' % (len(both), len(proved)))
    # (merge084b, 2026-11-15, the coordinator's order: the native-mod bar is the two seeds above AND the all-shard three-family
    #  check modnative7 measured 0.84 with -- scratch/agents/modnative7/run_084b.sh, eval_084.py: LIFT_SHARDS=4 under each
    #  family, shard 0 the family's seed and shard i its i-th (lift.py fuzz_one: none = the default, shards 9001-9003; 777,
    #  777001-777003; 4242, 4242001-4242003), the tests split over the four and added up. The same `lift.py prove` as above,
    #  repeated, on what passed both seeds and the negative controls (each control must be DIFFERENT in each family); a
    #  function counts / ships only if it meets the bar in every run: night.ps1 gives mod_native.py release every report as
    #  --both. A testing-run change only: no rule changes, nothing counted that the runs above did not already require.)
    passed = list(both)
    for fam, seed in SEED_FAMILIES:
        envf = dict(env, LIFT_SHARDS='4', FUZZ_MS=os.environ.get('NM_FAMILY_MS', '900000'))
        envf.pop('LIFT_SHARD_ONLY', None)
        if seed is None:
            envf.pop('FUZZ_SEED', None)
        else:
            envf['FUZZ_SEED'] = seed
        tagf = '%s_%s' % (TAG, fam)
        step('prove_' + fam, [sys.executable, 'lift.py', 'prove', tagf] + both + negs, cwd=LIFT, env=envf)
        with open(os.path.join(HERE, 'analyse_%s.txt' % tagf), 'w') as out:
            subprocess.run([sys.executable, 'm5_analyse.py', os.path.join(LIFT, 'candidates.json'), tagf], cwd=HERE, stdout=out,
                           stderr=subprocess.STDOUT, env=env)
        linesf = open(os.path.join(HERE, 'analyse_%s.txt' % tagf)).read().split('\n')
        print('seed family %s: ' % fam + '\n'.join(l for l in linesf if l.startswith(('run ', 'negative controls'))))
        m = re.match(r'negative controls: (\d+), DIFFERENT: (\d+)', next((l for l in linesf if l.startswith('negative controls')), ''))
        if not m or m.group(1) != m.group(2):
            raise SystemExit('native mod pipeline: a negative control was not DIFFERENT in seed family %s -- stop' % fam)
        passed = sorted(set(passed) & set(open(os.path.join(HERE, 'proved_%s.txt' % tagf)).read().split()))
    open(os.path.join(HERE, 'proved_%s_all.txt' % TAG), 'w', newline='\n').write(' '.join(passed))
    print('proved under both seeds and all three seed families: %d of %d' % (len(passed), len(proved)))


if __name__ == '__main__':
    main()
