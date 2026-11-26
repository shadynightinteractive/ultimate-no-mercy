"""The engine's rewrites as a blob at 0x80780000, for Windows: rw/build.sh's steps, with the Development folder's zig clang.

    python six-engine/work/rw/build.py full|oracle [OUT_DIR]

OUT_DIR defaults to rw/out/<layout>: blob.elf, blob.bin and blob.syms (the rw_ symbols build_rom.py and mkport.py read).
"""
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.abspath(os.path.join(HERE, '..', '..', '..')))
import nmpaths   # (puts the Development folder's toolchain first on PATH)

FLAGS = ['--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-O2', '-fomit-frame-pointer', '-G0', '-mno-abicalls', '-fno-pic',
         '-mno-check-zero-division', '-fno-builtin', '-nostdinc', '-ffreestanding', '-ffunction-sections', '-fno-jump-tables', '-Wall',
         '-Wextra', '-Wno-unused-parameter']


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd), (p.stdout + p.stderr)[-3000:]))
    return p.stdout


def main():
    layout = sys.argv[1]
    out = os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(HERE, 'out', layout)
    os.makedirs(out, exist_ok=True)
    sources = sorted(f for f in os.listdir(HERE) if f.startswith('r_') and f.endswith('.c'))
    # the engine's slot room (spec.py NSLOTS, SIX_SLOTS) for its own arrays: rw/six.h SIX_NSLOTS
    sys.path.insert(0, os.path.abspath(os.path.join(HERE, '..')))
    import spec
    flags = FLAGS + ['-DSIX_NSLOTS=%d' % spec.NSLOTS]
    run([sys.executable, 'gen_syms.py', layout, os.path.join(out, 'syms.ld'), 'six.h', 'stubs.S'] + sources, HERE)
    for f in sources:
        run(['zig', 'clang'] + flags + ['-c', f, '-o', os.path.join(out, f[:-2] + '.o')], HERE)
    run(['zig', 'clang', '--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-mno-abicalls', '-fno-pic', '-c', 'stubs.S', '-o',
         os.path.join(out, 'stubs.o')], HERE)
    objs = sorted(f for f in os.listdir(out) if f.endswith('.o'))
    run(['zig', 'ld.lld', '-m', 'elf32btsmip', '-T', os.path.join(HERE, 'blob.ld')] + objs + ['-o', 'blob.elf'], out)
    run(['llvm-objcopy', '-O', 'binary', 'blob.elf', 'blob.bin'], out)
    nm = run(['llvm-nm', '-n', 'blob.elf'], out)
    syms = [l for l in nm.splitlines() if len(l.split()) == 3 and l.split()[1] in 'TDdRrBb' and l.split()[2].startswith('rw_')]
    open(os.path.join(out, 'blob.syms'), 'w', newline='\n').write('\n'.join(syms) + '\n')
    print(os.path.getsize(os.path.join(out, 'blob.bin')), 'bytes;', len(syms), 'symbols ->', out)


if __name__ == '__main__':
    main()
