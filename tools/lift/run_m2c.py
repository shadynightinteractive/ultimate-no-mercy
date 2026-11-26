#!/usr/bin/env python3
"""Step 4: runs m2c over the functions that call nothing else, and measures what comes out.

    python tools/lift/run_m2c.py <asm folder from to_asm.py> <out folder>

Nothing here goes into the game. A function only counts as converted later, once it has been compared against the
original during real play and has replaced it. This measures the first gate: how much m2c can turn into C at all.

Writes <out folder>/<function>.c and <out folder>/summary.json. A function's grade:
  clean   m2c finished, with no goto and no warning or error comment in the C
  goto    m2c finished, but the C still has a goto, so its structure needs a person or a later pass
  warned  m2c finished, but left a warning or error comment
  failed  m2c stopped, or timed out
"""
import concurrent.futures, json, os, re, subprocess, sys

M2C = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'third_party', 'm2c')
WARNING = re.compile(r'M2C_ERROR|/\*\s*(?:Warning|Error|Unable|unable)|\bWarning:|\bERROR\b')


def convert(job):
    name, asm, out = job
    try:
        p = subprocess.run([sys.executable, 'm2c.py', '--target', 'mips-gcc-c', asm], cwd=M2C, capture_output=True,
                           text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return name, 'failed', 'timed out'
    if p.returncode != 0 or not p.stdout.strip():
        lines = p.stderr.strip().splitlines()
        return name, 'failed', lines[-1][:160] if lines else 'no output'
    open(os.path.join(out, name + '.c'), 'w').write(p.stdout)
    if WARNING.search(p.stdout):
        return name, 'warned', ''
    if re.search(r'\bgoto\b', p.stdout):
        return name, 'goto', ''
    return name, 'clean', ''


def main():
    asm, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    index = json.load(open(os.path.join(asm, 'index.json')))
    leaves = [k for k, v in index.items() if not v['calls'] and not v['indirect_call'] and not v['jump_table']]
    jobs = [(k, os.path.abspath(os.path.join(asm, k + '.s')), os.path.abspath(out)) for k in leaves]
    grades = {}
    reasons = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        for name, grade, why in pool.map(convert, jobs):
            grades[name] = grade
            if why:
                reasons[name] = why
    total = sum(v['size'] for v in index.values())
    summary = {}
    for grade in ('clean', 'goto', 'warned', 'failed'):
        names = [k for k, g in grades.items() if g == grade]
        ins = sum(index[k]['size'] for k in names)
        summary[grade] = {'functions': len(names), 'instructions': ins, 'share_of_game': round(100 * ins / total, 2)}
        print(grade, summary[grade])
    json.dump({'summary': summary, 'grades': grades, 'reasons': reasons}, open(os.path.join(out, 'summary.json'), 'w'),
              indent=0)


if __name__ == '__main__':
    main()
