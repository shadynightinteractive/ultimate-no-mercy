#!/usr/bin/env python3
"""The mods translated ahead of time into the game's source (Stage 1 step 1, 2026-11-11).

    python tools\aot\aot.py            build aot_tool.exe if needed, translate the mods' .nrm files into
                                       exe\WWFNoMercyRecomp\src\game\mod_aot (build_exe.ps1 runs it, then builds the exe)
    python tools\aot\aot.py --check    only say whether src\game\mod_aot was made from those .nrm files as they are now
                                       (exit 1 if any differs, or the list of mods does, or the tool, or a mod's masked
                                       ranges -- the extras holes -- are not the ones its hole list gives now)

The mods (MODS below; NM_AOT_MODS, ';'-separated .nrm paths, for another list): the arcade mod as built, and the three
AkiMods the game ships with, as they are in the game's assets\mods. They stay separate files from a separate library; the
exe's table has an entry for each, each with its own hash (librecomp/aot_mod.h). The two files the loader reads are taken
out of each .nrm (a zip) into tools\aot\build\nrm, so what is hashed here is what the loader will hash.

aot_tool.cpp says what it writes and why. This is tools/lift/modrep.py's way of building and running its tool, for this one:
the project's compiler and N64Recomp's libraries as a build of the game made them, both read only.

Order of building, from now on: the mod first (build_mod.ps1), then this, then the exe. An exe built before the mod it runs
with still works: its mod loader sees that the .nrm is another build (the hash) and translates it live, as it always did --
which is why --check exists, and why a test should fail on the loader's "the live recompiler translates it" line.
"""
import os, re, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# Two layouts: in the project (tools\aot, the exe's source in exe\WWFNoMercyRecomp), or a work folder beside it (tools_aot,
# exe_src\WWFNoMercyRecomp, with the project used read only for its toolchain).
IN_PROJECT = os.path.exists(os.path.join(HERE, '..', '..', 'exe', 'WWFNoMercyRecomp'))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..') if IN_PROJECT else os.path.join(HERE, '..'))
DEV = ROOT if IN_PROJECT else os.environ['NM_DEV']   # (a work folder beside the project names the project: NM_DEV)
PORT = os.path.join(ROOT, 'exe' if IN_PROJECT else 'exe_src', 'WWFNoMercyRecomp')
NMR = os.path.join(PORT, 'lib', 'N64ModernRuntime')
N64RECOMP = os.path.join(NMR, 'N64Recomp')
TOOL = os.path.join(HERE, 'build', 'aot_tool.exe')
# the AkiMods' .nrm files: the source tree's copies (exe/WWFNoMercyRecomp/assets/mods), the ones every game folder gets
#  (tools/layout/install_base.ps1, tools/release/make_package.ps1). (newuser 2026-11-17: they were read from the installed
#  game's assets/mods, so a build needed a game folder -- a fresh clone, or a game folder moved aside, could not build. Same files.)
MODS = [p for p in os.environ.get('NM_AOT_MODS', '').split(';') if p] or [
    os.path.join(ROOT, 'mod', 'build', 'mod_six', 'nomercy_arcade.nrm'),
    os.path.join(PORT, 'assets', 'mods', 'aki_camera.nrm'),
    os.path.join(PORT, 'assets', 'mods', 'aki_cheats.nrm'),
    os.path.join(PORT, 'assets', 'mods', 'aki_quickmatch.nrm'),
]
GAME_SYMBOLS = os.path.join(PORT, 'syms', 'dump.toml')
# The ROM the game holds when it runs: the six-man engine's (main.cpp six_engine_init makes the player's stock ROM into it).
# The loader checks each hooked function's bytes against what was translated here, so another ROM only loses the linked copy.
ROM = os.environ.get('NM_AOT_ROM') or os.path.join(DEV, 'six-engine', 'NoMercySix', 'nomercy.z64')
OUT = os.path.join(PORT, 'src', 'game', 'mod_aot')
CLANG = os.path.join(DEV, 'toolchain', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang++.exe')


def recompiler_sources():
    """The N64Recomp sources libN64Recomp.a is compiled from (its five .cpp files and the headers they include): when one is newer
    than the library, the library is not this tree's recompiler. (2026-11-15, merge084: N64Recomp's unordered float compares were
    fixed for 0.84 -- Stage 1 must be translated by the fixed recompiler, not by a tool linked with the library built before.)"""
    src = [os.path.join(N64RECOMP, 'src', f + '.cpp') for f in ('analysis', 'operations', 'cgenerator', 'recompilation', 'mod_symbols')]
    inc = os.path.join(N64RECOMP, 'include', 'recompiler')
    return src + [os.path.join(inc, f) for f in sorted(os.listdir(inc)) if f.endswith('.h')]


def libs_folder():
    """N64Recomp's libraries: this folder's build of the game if it has them, else the project's. (2026-11-15, merge084) A build of
    this tree's own -- the project's exe\\build, a work folder's build\\ or its exe source's build\\ -- is first brought up to date
    with this tree's N64Recomp sources (ninja's N64Recomp target: only what changed is compiled, nothing when nothing did). The
    project's build is only READ from a work folder: if its library is older than the work folder's recompiler sources, it is
    another recompiler, and this stops rather than translate with it."""
    own = [os.path.join(ROOT, 'exe', 'build')] if IN_PROJECT else [os.path.join(ROOT, 'build'), os.path.join(PORT, 'build')]
    newest = max(os.path.getmtime(s) for s in recompiler_sources())
    for build in own + ([] if IN_PROJECT else [os.path.join(DEV, 'exe', 'build')]):
        folder = os.path.join(build, 'lib', 'N64ModernRuntime', 'librecomp', 'N64Recomp')
        lib = os.path.join(folder, 'libN64Recomp.a')
        if not os.path.exists(lib):
            continue
        if os.path.getmtime(lib) < newest:
            if build in own and os.path.exists(os.path.join(build, 'build.ninja')):
                ninja = os.path.join(DEV, 'toolchain', 'ninja', 'ninja.exe')     # (the project's own: night.ps1 runs this before
                ninja = ninja if os.path.exists(ninja) else 'ninja'              #  any script has put the toolchain on PATH)
                p = subprocess.run([ninja, '-C', build, 'N64Recomp'], capture_output=True, text=True)
                if p.returncode != 0:
                    raise SystemExit('aot: N64Recomp\'s library did not build:\n' + (p.stdout + p.stderr)[-3000:])
            if os.path.getmtime(lib) < newest:
                raise SystemExit('aot: %s is older than the N64Recomp sources it must be made from: build the game first' % lib)
        return folder
    raise SystemExit('aot: N64Recomp libraries missing: build the game first')


def tool_inputs(folder):
    """What aot_tool.exe is made from, as one digest: its own sources, N64Recomp's config.cpp and the four libraries it links."""
    import hashlib
    h = hashlib.sha256(b'aot-tool-1')
    for p in [os.path.join(HERE, 'aot_tool.cpp'), os.path.join(NMR, 'librecomp', 'include', 'librecomp', 'aot_mod.h'),
              os.path.join(N64RECOMP, 'src', 'config.cpp')] + lib_files(folder):
        h.update(os.path.basename(p).encode() + b'\0' + open(p, 'rb').read() + b'\0')
    return h.hexdigest()


def lib_files(folder):
    return [os.path.join(folder, 'libN64Recomp.a'), os.path.join(folder, 'libSymbolLists.a'), os.path.join(folder, 'librabbitizer.a'),
            os.path.join(folder, 'lib', 'fmt', 'libfmt.a')]


def tool_digest():
    """The digest of aot_tool.exe as built: written beside a translation (mod_aot\\aot_tool.txt), so --check sees one made by
    another tool."""
    import hashlib
    return hashlib.sha256(open(TOOL, 'rb').read()).hexdigest()


def build_tool():
    """aot_tool.exe, built when missing or when what it is made from changed (with N64Recomp's config.cpp, which its libraries
    leave out). (2026-11-15, merge084: by content -- its sources AND N64Recomp's libraries, tool_inputs -- not by the dates of
    its two own sources only: a fixed recompiler library left the old tool in place.)"""
    sources = [os.path.join(HERE, 'aot_tool.cpp'), os.path.join(NMR, 'librecomp', 'include', 'librecomp', 'aot_mod.h')]
    folder = libs_folder()
    stamp = TOOL + '.inputs'
    want = tool_inputs(folder)
    if os.path.exists(TOOL) and os.path.exists(stamp) and open(stamp).read().strip() == want:
        return TOOL
    os.makedirs(os.path.dirname(TOOL), exist_ok=True)
    libs = [os.path.join(folder, 'libN64Recomp.a'), os.path.join(folder, 'libSymbolLists.a'), os.path.join(folder, 'librabbitizer.a'),
            os.path.join(folder, 'lib', 'fmt', 'libfmt.a')]
    include = [os.path.join(N64RECOMP, 'include'), os.path.join(N64RECOMP, 'lib', 'rabbitizer', 'include'),
               os.path.join(N64RECOMP, 'lib', 'rabbitizer', 'cplusplus', 'include'), os.path.join(N64RECOMP, 'lib', 'fmt', 'include'),
               os.path.join(N64RECOMP, 'lib', 'tomlplusplus', 'include'), os.path.join(NMR, 'librecomp', 'include'),
               os.path.join(N64RECOMP, 'src')]   # (startup 2026-11-19: analysis.h, the jump-table finder aot_tool's mask check uses)
    command = [CLANG, '-std=c++20', '-O2', '-static', '-w', '-include', 'cstdlib', '-o', TOOL, sources[0],
               os.path.join(N64RECOMP, 'src', 'config.cpp')] + libs
    # (newhome 2026-11-17, reproducible builds: no link time stamp, and the project's folder written as "." in the tool's assert
    #  texts -- the same tool, byte for byte, wherever it is built, so its digest in mod_aot\aot_tool.txt stays put)
    command[1:1] = ['-Wl,--no-insert-timestamp', '-ffile-prefix-map=%s=.' % ROOT.replace('\\', '/'), '-ffile-prefix-map=%s=.' % ROOT]
    for path in include:
        command[1:1] = ['-I', path]
    p = subprocess.run(command, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('aot: the tool did not build:\n' + p.stderr[:6000])
    open(stamp, 'w').write(want + '\n')
    return TOOL


def tag(nrm):
    return os.path.splitext(os.path.basename(nrm))[0]


def mod_files(nrm):
    """(mod_syms.bin, mod_binary.bin) as bytes, from inside a .nrm."""
    with zipfile.ZipFile(nrm) as z:
        return z.read('mod_syms.bin'), z.read('mod_binary.bin')


def mod_hash(nrm):
    """The hash the loader works out for this .nrm (librecomp/aot_mod.h nm_aot_hash: FNV-1a over syms then binary)."""
    h = 0xCBF29CE484222325
    for data in mod_files(nrm):
        for b in data:
            h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return '%016X' % h


def linked_hashes():
    """tag -> the hash src/game/mod_aot was made from, for each mod in it ({} if it isn't there)."""
    path = os.path.join(OUT, 'mod_aot_report.txt')
    if not os.path.exists(path):
        return {}
    return dict(re.findall(r'^(\w+): hash ([0-9A-F]{16});', open(path).read(), re.M))


def linked_masks():
    """(relcheck 2026-11-19) tag -> [(offset, size)]: the masks the exe's table holds (src/game/mod_aot/mod_aot_table.c's
    nm_aot_<tag>_masks arrays, as aot_tool wrote them); {} if there is no table"""
    path = os.path.join(OUT, 'mod_aot_table.c')
    if not os.path.exists(path):
        return {}
    text = open(path).read()
    out = {}
    for m in re.finditer(r'static const NmAotMask nm_aot_(\w+)_masks\[(\d+)\] = \{(.*?)\n\};', text, re.S):
        pairs = [(int(o, 16), int(n, 16)) for o, n in re.findall(r'\{0x([0-9A-Fa-f]+)u, 0x([0-9A-Fa-f]+)u\}', m.group(3))]
        if len(pairs) != int(m.group(2)):
            raise SystemExit('aot: %s: nm_aot_%s_masks has %d entries, says %s' % (path, m.group(1), len(pairs), m.group(2)))
        out[m.group(1)] = pairs
    return out


def masks_hash(masks):
    """a short SHA-256 of a mask list ('0x<offset> 0x<size>' lines, as aot.py writes them for aot_tool)"""
    import hashlib
    return hashlib.sha256(''.join('0x%X 0x%X\n' % r for r in masks).encode()).hexdigest()[:16]


def extras_masks(nrm):
    """(startup 2026-11-19, in place of firstrun's base_hashes) [(offset, size)] of the mod image's EXTRAS holes -- the data a
    player's first start fills from their ROMs (tools/firstrun/mkplan.py find_holes: the same list plan.bin carries) -- or []
    for a mod not built here (no mod.elf / mod.map beside the .nrm, or the .nrm isn't that build). aot_tool masks them out of
    the entry's second hash, after proving that no code and nothing its translation reads is in them (aot_tool.cpp)."""
    build = os.path.dirname(nrm)
    if not os.path.exists(os.path.join(build, 'mod.elf')) or not os.path.exists(os.path.join(build, 'mod.map')): return []
    if mod_files(nrm)[1] != open(os.path.join(build, 'mod_binary.bin'), 'rb').read(): return []   # (the .nrm isn't this build)
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'firstrun'))
    import mkplan
    image, holes, relocs = mkplan.find_holes(build)
    return [(s, e - s) for s, e, cls, label in holes if cls == 'extras']


def generate():
    tool = build_tool()
    command = [tool, GAME_SYMBOLS, ROM, OUT]
    for nrm in MODS:
        if not os.path.exists(nrm):
            raise SystemExit('aot: %s is missing -- build the mod first' % nrm)
        folder = os.path.join(HERE, 'build', 'nrm', tag(nrm))
        os.makedirs(folder, exist_ok=True)
        for name, data in zip(('mod_syms.bin', 'mod_binary.bin'), mod_files(nrm)):
            open(os.path.join(folder, name), 'wb').write(data)
        command += [tag(nrm), os.path.join(folder, 'mod_syms.bin'), os.path.join(folder, 'mod_binary.bin')]
    env = dict(os.environ)
    env.pop('NM_AOT_ALSO', None)
    masks = []
    for nrm in MODS:
        ranges = extras_masks(nrm)
        if not ranges: continue
        path = os.path.join(HERE, 'build', 'nrm', tag(nrm), 'masks.txt')
        open(path, 'w').write(''.join('0x%X 0x%X\n' % r for r in ranges))
        masks.append('%s=%s' % (tag(nrm), path))
    if masks: env['NM_AOT_MASK'] = ';'.join(masks)
    else: env.pop('NM_AOT_MASK', None)
    p = subprocess.run(command, capture_output=True, text=True, env=env)
    # (the front end prints a line for each tail call it finds: not wanted here)
    sys.stdout.write(''.join(l + '\n' for l in p.stdout.splitlines() if l and not l.startswith('Tail call')))
    if p.returncode != 0:
        raise SystemExit('aot: the tool failed: ' + (p.stderr or p.stdout)[-3000:])
    if p.stderr.strip():
        sys.stdout.write(p.stderr)


if __name__ == '__main__':
    if '--check' in sys.argv:
        linked, stale = linked_hashes(), 0
        for nrm in MODS:
            now = mod_hash(nrm) if os.path.exists(nrm) else None
            was = linked.pop(tag(nrm), None)
            if now != was:
                stale += 1
                print('STALE %s: src/game/mod_aot was made from %s, the .nrm now is %s' % (tag(nrm), was, now))
        for name in linked:
            stale += 1
            print('STALE %s: in src/game/mod_aot, not in the list of mods' % name)
        # (2026-11-15, merge084) and made by the tool as it is now: a translation by another aot_tool -- one linked with another
        #  N64Recomp library, e.g. before 0.84's unordered-compare fix -- is made again, though the mods are the same
        made_by = os.path.join(OUT, 'aot_tool.txt')
        was_tool = open(made_by).read().strip() if os.path.exists(made_by) else None
        try:
            build_tool()
            now_tool = tool_digest()
        except SystemExit as e:
            now_tool = "the tool can't be built here (%s)" % str(e).splitlines()[0]
        if was_tool != now_tool:
            stale += 1
            print('STALE: src/game/mod_aot was made by aot_tool %s, the tool now is %s' % (was_tool, now_tool))
        # (relcheck 2026-11-19, approved by the user on 2026-11-19: "I looked at your list you needed from me and I concur with
        #  your recommendations" -- REVIEW_STARTUP_MASK.txt's optional item (a); review: notes REVIEW_RELCHECK.txt) and with the
        #  masks as they are now: the exe's table carries each mod's masked ranges (the extras' holes, aot_tool's masked hash);
        #  a hole list changed in mkplan.py (its BASE list, EXTRAS_IN_SOURCE, the headers' names) with the mod's bytes unchanged
        #  would leave the table's masks stale -- the loader would then quietly miss a player's extras-filled mod. The table's
        #  masks must be exactly the extras holes extras_masks() finds now (their hashes are printed).
        table = linked_masks()
        for nrm in MODS:
            if not os.path.exists(nrm): continue
            now_m, was_m = extras_masks(nrm), table.pop(tag(nrm), [])
            if now_m != was_m:
                stale += 1
                print('STALE %s: the masks in src/game/mod_aot (%d ranges, hash %s) are not the extras holes now (%d ranges, hash %s)' % (
                    tag(nrm), len(was_m), masks_hash(was_m), len(now_m), masks_hash(now_m)))
            elif now_m:
                print('masks %s: %d ranges, hash %s = the extras holes now' % (tag(nrm), len(now_m), masks_hash(now_m)))
        for name in table:
            stale += 1
            print('STALE %s: masks in src/game/mod_aot for a mod not in the list' % name)
        print('%d of the linked translations are not the mods (or the tool) as they are now: run build_exe.ps1 (tools/aot/aot.py, then the exe)' % stale if stale else
              'src/game/mod_aot is the %d mods as they are now' % len(MODS))
        sys.exit(1 if stale else 0)
    generate()
    open(os.path.join(OUT, 'aot_tool.txt'), 'w', newline='\n').write(tool_digest() + '\n')   # (2026-11-15, merge084: for --check)
    # (2026-11-12) Each hooked game function's copy must be the port's own function (check_hooked.py says why): a post-recompile
    # repair of a function the mods hook, or another ROM, would otherwise make the game run other code than the port's.
    import check_hooked
    if check_hooked.main(OUT, os.path.join(PORT, 'RecompiledFuncs')):
        raise SystemExit("aot: a hooked game function is not the port's own function (above): see tools/aot/check_hooked.py")
    # (2026-11-14, modnative7) The native mod: the mod's functions proved native (src/game/mod_native.cpp, released by
    #  tools/aot/mod_native.py from the tester's proofs) take their Stage 1 names -- only if this translation is the very mod
    #  build they were proved against (its hash); otherwise the game links Stage 1 alone and this says so.
    import mod_native
    mod_native.apply({'aot': OUT, 'game': os.path.dirname(OUT)})
    print('-> %s' % OUT)
