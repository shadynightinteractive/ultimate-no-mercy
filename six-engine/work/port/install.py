"""The engine at a slot count, from scratch into the game: every file that depends on the layout, rebuilt and put in place.

    python six-engine/work/port/install.py SLOTS WORK_DIR

SLOTS is the room the engine has (spec.py NSLOTS: 6 is the six-man layout, 32 the wider one); a match still has its
own count. WORK_DIR holds what's built on the way (the blob, the ROM, the translator's run). In order:

  1. the engine's rewrites as a blob (rw/build.py), its A_ symbols at this layout's places;
  2. the engine's ROM (build_rom.py, SIX_LOOPS=gp: the full build);
  3. the port ROM and the translated code (port/regen.py: N64Recomp and the post-regen pipeline);
  4. into the game: exe RecompiledFuncs (the old kept in RecompiledFuncs.before_regen), src/main/six_blob.h,
     src/main/six_rompatch.h, the port ROM in six-engine/NoMercySix (the mod's build makes its patched copies from
     it), and the address files the mod and the exe read (gen_arcade_six.py: mod/src/six_addrs.h,
     src/main/six_layout.h; mod/gen/mkannsave.py: mod/src/ann_slot.h).

At 6 this reproduces the files as they were (checked 2026-11-05: same translated code, ROM, blob and headers), so it is
also how to go back.
"""
import os, shutil, struct, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SIX = os.path.abspath(os.path.join(HERE, '..'))
DEV = os.path.abspath(os.path.join(SIX, '..', '..'))
sys.path.append(DEV)
from nmpaths import nmp

EXE = os.path.join(DEV, 'exe', 'WWFNoMercyRecomp')


def step(name, cmd, cwd, env):
    print('==', name, flush=True)
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    tail = [l for l in (p.stdout + p.stderr).strip().splitlines() if not l.lstrip().startswith("('func")][-6:]
    for l in tail:
        print('   ', l[:240])
    if p.returncode != 0:
        raise SystemExit(name + ' failed')


def blob_header(blob_bin, out):
    """src/main/six_blob.h: the blob's words, as regen.sh wrote them"""
    b = open(blob_bin, 'rb').read()
    b += b'\0' * (-len(b) % 4)
    w = struct.unpack('>%dI' % (len(b) // 4), b)
    L = ['// Generated from the six-man engine blob (rw/out/full/blob.bin): code + data at 0x80780000.', '#pragma once',
         '#include <cstdint>', 'static const uint32_t six_blob_vram = 0x80780000u;',
         'static const uint32_t six_blob_words[%d] = {' % len(w)]
    for i in range(0, len(w), 8):
        L.append('    ' + ', '.join('0x%08X' % x for x in w[i:i + 8]) + ',')
    L.append('};')
    open(out, 'w', newline='\n').write('\n'.join(L) + '\n')


def check_lifted(env):
    """Stops the install when it would move an array the converted code points into. lifted.cpp (tools/lift) has the
    places of the moved arrays written in, as m2c saw them in the translated code: a layout that moves one leaves that
    code reading the old place -- the draw list's first stride shifted every main array 64 KB, and the title crashed in
    a converted func_80025EA4 at once (2026-11-06). Then: convert again (tools/lift/night.ps1) or keep the arrays put."""
    import json, re, subprocess
    # (against the layout the converted code was made with: src/game/lifted_layout.h, the six_layout.h of that time --
    #  tools/lift/night.ps1 writes it when it releases lifted.cpp)
    lifted = os.path.join(EXE, 'src', 'game', 'lifted.cpp')
    layout_h = os.path.join(EXE, 'src', 'game', 'lifted_layout.h')
    if not os.path.exists(lifted):
        return
    if not os.path.exists(layout_h):
        print("note: no src/game/lifted_layout.h -- the layout of the converted code is not known, not checked")
        return
    out = subprocess.run([sys.executable, '-c', 'import json, build_rom as b; print(json.dumps([(x[0], x[1], x[2]) for x in '
                          'b.OVLE_NEW + b.MAIN_NEW] + [b.spec.NSLOTS]))'], cwd=SIX, env=env, capture_output=True, text=True)
    rows = json.loads(out.stdout.strip().splitlines()[-1])
    n = rows.pop()
    old = {int(m.group(1), 16): int(m.group(2), 16)
           for m in re.finditer(r'SIX_([0-9A-F]{8}) = (0x[0-9A-Fa-f]+)u', open(layout_h).read())}
    # (and the mod's converted functions, in src/game/mod_converted.cpp since 2026-11-13, agent "preserve": they read the game's
    #  arrays too. Two fixes found on the way, 2026-11-13: the pattern ended in a backspace character where \b, a word's end,
    #  was meant -- no constant is followed by one, so this found nothing and passed every layout; and the files are read as
    #  UTF-8, as lift.py writes them -- the names' abouts quote the game's Japanese text, which the default code page stops on.)
    texts = [open(p, encoding='utf8', errors='replace').read() for p in (lifted, os.path.join(EXE, 'src', 'game', 'mod_converted.cpp'))
             if os.path.exists(p)]
    consts = {int(c, 16) for t in texts for c in re.findall(r'0x(8[0-9A-Fa-f]{7})\b', t)}
    bad = []
    for stock, stride, new in rows:
        was = old.get(stock)
        if was is None or was == new:
            continue
        hit = sorted(c for c in consts if was <= c < was + stride * n)
        if hit:
            bad.append('%08X (was %08X, now %08X): %s' % (stock, was, new, ' '.join('%08X' % c for c in hit[:4])))
    if bad:
        print('install stopped: this layout moves arrays the converted code (lifted.cpp) points into --')
        for x in bad[:12]:
            print('   ', x)
        print('convert again against the new layout (tools/lift/night.ps1), or keep those arrays where they are')
        sys.exit(1)


def main():
    slots, work = int(sys.argv[1]), os.path.abspath(sys.argv[2])
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, SIX_SLOTS=str(slots), SIX_LOOPS='gp')
    check_lifted(env)
    blob = os.path.join(SIX, 'rw', 'out', 'full') if slots == 6 else os.path.join(work, 'blob%d' % slots)
    env['SIX_BLOB'] = blob
    rom = os.path.join(work, 'six%d.z64' % slots)
    env['SIX_OUT'] = rom
    regen = os.path.join(work, 'regen%d' % slots)
    step('blob', [sys.executable, os.path.join(SIX, 'rw', 'build.py'), 'full', blob], SIX, env)
    step('ROM', [sys.executable, 'build_rom.py'], SIX, env)
    step('translate', [sys.executable, os.path.join(HERE, 'regen.py'), rom, regen], DEV, env)
    # into the game
    old = os.path.join(EXE, 'RecompiledFuncs.before_regen')
    if os.path.exists(old):
        shutil.rmtree(old)
    shutil.move(os.path.join(EXE, 'RecompiledFuncs'), old)
    # (copied with today's dates, not the files' own: the build goes by dates, and an older date would keep old objects)
    shutil.copytree(os.path.join(regen, 'RecompiledFuncs'), os.path.join(EXE, 'RecompiledFuncs'), copy_function=shutil.copy)
    shutil.copy(os.path.join(regen, 'nomercy.z64'), os.path.join(DEV, 'six-engine', 'NoMercySix', 'nomercy.z64'))
    # (and its symbols' blob section: the mod's gen/asmpatch.py names the engine's functions from it -- a changed blob
    #  moves them, and a stale list makes it stop: "no function at ...")
    shutil.copy(os.path.join(regen, 'syms', 'dump.toml'), os.path.join(DEV, 'six-engine', 'NoMercySix', 'syms', 'dump.toml'))
    # (and the exe's copy, the one the mod's build links against -- RecompModTool's func_reference_syms_file: a mod call
    #  into a moved engine function is "Reference symbol does not exist" at load otherwise, 2026-11-06)
    shutil.copy(os.path.join(regen, 'syms', 'dump.toml'), os.path.join(EXE, 'syms', 'dump.toml'))
    blob_header(os.path.join(blob, 'blob.bin'), os.path.join(EXE, 'src', 'main', 'six_blob.h'))
    step('ROM patch', [sys.executable, os.path.join(HERE, 'gen_rompatch.py'), nmp('/home/devbox/work/nomercy.z64'),
                       os.path.join(regen, 'nomercy.z64'), os.path.join(EXE, 'src', 'main', 'six_rompatch.h')], DEV, env)
    step('addresses', [sys.executable, os.path.join(HERE, 'gen_arcade_six.py')], DEV, env)
    step('announcer slot', [sys.executable, os.path.join(DEV, 'mod', 'gen', 'mkannsave.py')], DEV, env)
    print('installed: %d slots' % slots)


if __name__ == '__main__':
    main()
