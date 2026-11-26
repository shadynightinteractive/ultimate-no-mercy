"""The PC port's translated code, regenerated from a six-man (or wider) ROM: regen.sh's steps, for Windows.

    python six-engine/work/port/regen.py SIX_ROM WORK_DIR [--install]

1. the port ROM: the six-man ROM with the engine's blob in its free tail (mkport.py), and the blob's [[section]] in
   syms/dump.toml;
2. N64Recomp (libs/N64Recomp/build-native/N64Recomp.exe) and the post-regen pipeline (BUILDING.md section 4: the fix_*
   tools, the hand edits re-added, the audits), run in WORK_DIR: a copy of six-engine/NoMercySix without its output;
3. with --install: RecompiledFuncs into exe/WWFNoMercyRecomp (the old one kept as RecompiledFuncs.before_regen), and the
   blob into src/main/six_blob.h. Without it, WORK_DIR/RecompiledFuncs is left to compare.

The six-man tree, six-engine/NoMercySix, is never written to.
"""
import os, re, shutil, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.abspath(os.path.join(HERE, '..', '..', '..')))
from nmpaths import nmp

DEV = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
TREE = os.path.join(DEV, 'six-engine', 'NoMercySix')
N64RECOMP = os.path.join(DEV, 'libs', 'N64Recomp', 'build-native', 'N64Recomp.exe')
STEPS = ['fix_stumps', 'fix_switches', 'fix_jraddend', 'fix_backbranches', 'fix_selfentry', 'readd_hand_edits', 'audit_coresidency']


def run(cmd, cwd, log=None):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if log:
        open(log, 'w').write(p.stdout + p.stderr)
    return p


def main():
    six_rom, work = sys.argv[1], os.path.abspath(sys.argv[2])
    install = '--install' in sys.argv
    # A copy of the tree, without its output or anything built.
    if os.path.exists(work):
        shutil.rmtree(work)
    shutil.copytree(TREE, work, ignore=shutil.ignore_patterns('RecompiledFuncs', 'RecompiledFuncs.prev', 'build*', 'assets', '.git'))
    os.makedirs(os.path.join(work, 'RecompiledFuncs'))
    # 1. The port ROM and the blob's section.
    rom = os.path.join(work, 'nomercy.z64')
    toml = os.path.join(work, 'rwblob.toml')
    p = run([sys.executable, os.path.join(HERE, 'mkport.py'), six_rom, rom, toml], work)
    print('mkport:', (p.stdout + p.stderr).strip()[-300:])
    dump = os.path.join(work, 'syms', 'dump.toml')
    s = open(dump).read()
    new = open(toml).read().strip() + '\n'
    i = s.find('[[section]]\nname = "rwblob"')
    if i < 0:
        s = s.rstrip('\n') + '\n\n' + new
    else:
        j = s.find('[[section]]', i + 10)
        s = s[:i] + new + ('\n' + s[j:] if j >= 0 else '')
    open(dump, 'w').write(s)
    # 2. N64Recomp and the pipeline.
    p = run([N64RECOMP, 'nomercy.toml'], work, os.path.join(work, 'n64recomp.log'))
    if p.returncode != 0:
        print((p.stdout + p.stderr)[-2000:])
        raise SystemExit('N64Recomp failed')
    print('N64Recomp:', len(os.listdir(os.path.join(work, 'RecompiledFuncs'))), 'files')
    for step in STEPS:
        p = run([sys.executable, os.path.join('tools', step + '.py')], work)
        print('==', step, ':', ' | '.join((p.stdout + p.stderr).strip().splitlines()[-3:])[:300])
    p = run([sys.executable, os.path.join('tools', 'romhack', 'list_repaired_functions.py'), '--recompiler', N64RECOMP], work)
    print('== list_repaired_functions :', ' | '.join((p.stdout + p.stderr).strip().splitlines()[-2:])[:300])
    if install:
        dest = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp')
        kept = os.path.join(dest, 'RecompiledFuncs.before_regen')
        if os.path.exists(kept):
            shutil.rmtree(kept)
        shutil.move(os.path.join(dest, 'RecompiledFuncs'), kept)
        shutil.copytree(os.path.join(work, 'RecompiledFuncs'), os.path.join(dest, 'RecompiledFuncs'), copy_function=shutil.copy)
        print('installed; the old translation is in RecompiledFuncs.before_regen')


if __name__ == '__main__':
    main()
