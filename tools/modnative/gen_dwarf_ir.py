#!/usr/bin/env python3
"""The mod's debug information and LLVM IR, file by file, for prove_gen.py (modnative5, 2026-11-13; the way modnative3 made
modnative4's dwarf/ and ir/, written down as a script so a changed mod gets them fresh):

  dwarf/<file>.o, .elf   the file compiled with build_mod.ps1's flags and -g, linked alone (frame_variables: llvm-symbolizer FRAME)
  ir/<file>.ll           the same flags, -S -emit-llvm -fno-discard-value-names (compiled_interface: each function's final define)
  ir/<file>.before_dae.ll  -mllvm -print-before=deadargelim (the calls as they were before dead-argument elimination)
  ir/<file>.front.ll     -S -emit-llvm -Xclang -disable-llvm-passes: the front end's IR, before any optimisation (shrunk.py:
                         the values the C stores in a variable the optimiser later shrank to a boolean)

From this folder's mod copy (w/mod) unless a folder is given.   python gen_dwarf_ir.py [<mod folder>]
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.environ.get('NM_MOD_DIR') or os.path.abspath(os.path.join(HERE, '..', '..', 'mod'))   # (tools/modnative: the project's mod)
DEV = os.path.abspath(os.path.join(HERE, '..', '..'))
MOD = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] != 'irg' else MOD_DIR
ZIG = os.environ.get('NM_ZIG') or os.path.join(DEV, 'toolchain', 'zig', 'zig.exe')
AKI = os.path.join(DEV, 'libs', 'AkiMods', 'common')
# (build_mod.ps1's flags, the six-man build that ships: -DNM_SIX -DARCADE_STORY -DARCADE_MOVEX)
FLAGS = ['--target=mips-unknown-none-elf', '-mcpu=mips2', '-mabi=32', '-O2', '-G0', '-mno-abicalls', '-fno-pic',
         '-mno-odd-spreg', '-Xclang', '-target-feature', '-Xclang', '+nooddspreg', '-mno-check-zero-division',
         '-fomit-frame-pointer', '-fno-builtin', '-fno-jump-tables', '-nostdinc', '-ffreestanding', '-ffunction-sections',
         '-Wno-everything', '-Isrc', '-I', os.path.join(AKI, 'include'), '-DNM_SIX', '-DARCADE_STORY', '-DARCADE_MOVEX']
# (modnative6, 2026-11-14: build_mod_copy.ps1's -fwrapv -fno-strict-aliasing too -- the IR must be the build's. approved by the user
#  2026-11-14; NM_NO_WRAP=1 as build_mod_copy.ps1 -NoWrap)
if os.environ.get('NM_NO_WRAP') != '1':
    FLAGS += ['-fwrapv', '-fno-strict-aliasing']
SOURCES = ['src/draw.c', 'src/logic.c', 'src/gameplay.c', 'src/bigshow.c', 'src/media.c', 'src/commentary.c', 'src/roster.c',
           'src/variants.c', 'src/files.c', 'src/parts.c', 'src/skel.c', 'src/hd.c', 'src/hands.c', 'src/movex.c', 'src/announce.c',
           'src/arcctl.c', 'src/story.c', 'src/save.c', 'src/util.c', 'src/tron.c', 'src/arena.c', 'src/arena_ringstage.c',
           'src/arena_entrance.c', 'src/arena_ring.c', 'recomp/hooks_recomp.c']


def run(cmd, **kw):
    p = subprocess.run(cmd, cwd=MOD, capture_output=True, text=True, **kw)
    if p.returncode:
        raise SystemExit('failed: %s\n%s' % (' '.join(cmd[:6]), p.stderr[-2000:]))
    return p


def main():
    for d in ('dwarf', 'ir'):
        os.makedirs(os.path.join(HERE, d), exist_ok=True)
    for src in SOURCES:
        base = os.path.splitext(os.path.basename(src))[0]
        o = os.path.join(HERE, 'dwarf', base + '.o')
        run([ZIG, 'clang'] + FLAGS + ['-g', '-c', src, '-o', o])
        run([ZIG, 'ld.lld', o, '-o', os.path.join(HERE, 'dwarf', base + '.elf'), '--unresolved-symbols=ignore-all', '-e', '0'])
        run([ZIG, 'clang'] + FLAGS + ['-S', '-emit-llvm', '-fno-discard-value-names', src, '-o', os.path.join(HERE, 'ir', base + '.ll')])
        run([ZIG, 'clang'] + FLAGS + ['-S', '-emit-llvm', '-fno-discard-value-names', '-Xclang', '-disable-llvm-passes', src, '-o',
                                      os.path.join(HERE, 'ir', base + '.front.ll')])
        p = run([ZIG, 'clang'] + FLAGS + ['-fno-discard-value-names', '-mllvm', '-print-before=deadargelim', '-c', src, '-o', os.devnull])
        open(os.path.join(HERE, 'ir', base + '.before_dae.ll'), 'w', newline='\n').write(p.stderr)
        print(base)


def main_irg():
    """irg/<file>.ll and irg/<file>.i (part 1 of the Linux work, 2026-11-14; inline_sites.py reads them): the file compiled with
    the same flags and -g -- so each instruction left has its source line, column and the chain of calls it was inlined through
    -- and the file preprocessed as native.py does but with its line markers. Paths in both are relative to the mod's folder
    (-fdebug-compilation-dir=. and a relative -I), so neither names where it was made.
        NM_CLANG=clang python gen_dwarf_ir.py irg"""
    clang = [os.environ['NM_CLANG']] if os.environ.get('NM_CLANG') else [ZIG, 'clang']
    out = os.path.join(HERE, 'irg')
    os.makedirs(out, exist_ok=True)
    aki = os.path.relpath(os.path.join(AKI, 'include'), MOD_DIR).replace(os.sep, '/')
    flags = [f if f != os.path.join(AKI, 'include') else aki for f in FLAGS]
    for src in SOURCES:
        base = os.path.splitext(os.path.basename(src))[0]
        p = subprocess.run(clang + flags + ['-g', '-fdebug-compilation-dir=.', '-S', '-emit-llvm', '-fno-discard-value-names', src, '-o',
                                            os.path.join(out, base + '.ll')], cwd=MOD_DIR, capture_output=True, text=True)
        if p.returncode:
            raise SystemExit('failed: %s\n%s' % (src, p.stderr[-2000:]))
        p = subprocess.run(clang + ['--target=mips-unknown-none-elf', '-E', '-nostdinc', '-ffreestanding', '-Isrc', '-I', aki, '-DNM_SIX',
                                    '-DARCADE_STORY', '-DARCADE_MOVEX', src, '-o', os.path.join(out, base + '.i')],
                           cwd=MOD_DIR, capture_output=True, text=True)
        if p.returncode:
            raise SystemExit('failed: %s\n%s' % (src, p.stderr[-2000:]))
        print(base)


if __name__ == '__main__':
    main_irg() if sys.argv[1:2] == ['irg'] else main()
