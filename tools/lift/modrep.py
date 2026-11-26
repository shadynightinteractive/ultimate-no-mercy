#!/usr/bin/env python3
"""The mod's replacement functions, as originals tools/lift can convert ("modrep", 2026-11-08, scratch/lift_modrep).

The mod replaces some of the game's functions with its own copies: mod/gen/asmpatch.py's edited copies of the game's MIPS
(mod/build/patches/mod.S, in the mod's .recomp_patch section under the game function's name), and the C functions that say
RECOMP_PATCH. In the game the mod loader translates the mod as it loads it (N64Recomp's live recompiler) and points each replaced
function's entry at its copy (librecomp mods.cpp, patch_func). So what runs for func_X is the mod's copy, and lift.py never converts
the game's func_X (patched_by_mod): a converted copy of the game's version would skip the mod's change.

This makes the mod's copy itself convertible:
  1. modrep/modrep_tool.cpp translates each replacement from the built mod (mod/build/mod_six: mod_syms.bin, mod_binary.bin,
     what RecompModTool made and what the loader reads) with N64Recomp's own front end -- the one the live recompiler uses -- into
     the same register-level C as RecompiledFuncs, each instruction's MIPS in a comment. Each comes out as a NEW function,
     modrep_func_X, beside the game's func_X (which stays as it is, and stays refused).
  2. to_asm.py, analyse.py and lift.py read modrep/out/modrep_originals.c with RecompiledFuncs (sources()), so modrep_func_X goes
     through the whole pipeline like any function, with the mod's translated copy as the original the tester compares against.
  3. A proved modrep_func_X goes into lifted.cpp under that name, with a table (release_table) of {the game function it
     replaces, the hash of the mod's copy it was made from}. The game's mod loader (exe.diff) points func_X's entry at the
     native modrep_func_X in place of its live-recompiled copy only when the mod it is loading has a copy with that same hash and
     nothing hooks the function; otherwise the mod's own copy runs, as it always did. Converted callers never call modrep_func_X
     directly: they call func_X through its entry (lift_call), so they run whatever the loader chose.
  4. The hash (modrep/lift_modrep_hash.h, the same code in the tool and in the loader) is of the copy as the built mod has it.
     lift.py refuses to release a conversion whose hash is not the current mod build's, and `lift.py check-modrep` lists the
     conversions in lifted.cpp that a later mod build has made stale (exit 1), so they are converted again.

What a copy may refer to, to be converted: the game's code and addresses; another replacement (called through that game
function's entry); its own jump tables, in the mod's data (m2c's switch never reads them); and the mod's own data, whose
addresses the converted code takes from where the loader put the mod's section (lift_mod_address). Not: the mod's other
functions, or the port's imports (why_not: converted code can't reach the loader's translation of them), nor a function the mod
also hooks (the live-recompiled copy calls the hook).

And for the game's own functions (analyse.py, lift.py result_registers, abi.py): a replaced func_X is the mod's copy when the mod
runs it and the game's own when it doesn't, so its callers are checked against both -- what either reads, what both always
leave. analyse.py reads the mod's other functions too (modrep_modfuncs.c, as modfn_<address>; never converted, never in
analysis.json) for what a copy reads through them, and system_reads.py the port's imports through the functions registered
under their names.

    python tools/lift/modrep.py           translate the mod's replacements from its build into modrep/out (night.ps1)
    python tools/lift/lift.py check-modrep  the converted copies in lifted.cpp a later mod build has made stale (exit 1)
"""
import json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
N64RECOMP = os.path.join(PORT, 'lib', 'N64ModernRuntime', 'N64Recomp')
TOOL_SOURCE = os.path.join(HERE, 'modrep')
TOOL = os.path.join(TOOL_SOURCE, 'build', 'modrep_tool.exe')
OUT = os.environ.get('LIFT_MODREP_OUT') or os.path.join(TOOL_SOURCE, 'out')
# The built mod the loader reads (build_mod.ps1's six-man build: what ships).
MOD_BUILD = os.environ.get('LIFT_MOD_BUILD') or os.path.join(ROOT, 'mod', 'build', 'mod_six')
GAME_SYMBOLS = os.path.join(PORT, 'syms', 'dump.toml')
# N64Recomp's libraries as the game's build made them (build_exe.ps1); LIFT_N64RECOMP_LIBS for another folder.
LIBS = os.environ.get('LIFT_N64RECOMP_LIBS') or os.path.join(ROOT, 'exe', 'build', 'lib', 'N64ModernRuntime', 'librecomp', 'N64Recomp')
CLANG = os.path.join(ROOT, 'toolchain', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang++.exe')
ORIGINALS = 'modrep_originals.c'
PREFIX = 'modrep_'


def build_tool():
    """modrep_tool.exe, built when missing or older than its source (with N64Recomp's config.cpp, which its libraries leave out)."""
    sources = [os.path.join(TOOL_SOURCE, 'modrep_tool.cpp'), os.path.join(TOOL_SOURCE, 'lift_modrep_hash.h')]
    if os.path.exists(TOOL) and all(os.path.getmtime(TOOL) >= os.path.getmtime(s) for s in sources):
        return TOOL
    os.makedirs(os.path.dirname(TOOL), exist_ok=True)
    libs = [os.path.join(LIBS, 'libN64Recomp.a'), os.path.join(LIBS, 'libSymbolLists.a'), os.path.join(LIBS, 'librabbitizer.a'),
            os.path.join(LIBS, 'lib', 'fmt', 'libfmt.a')]
    missing = [l for l in libs if not os.path.exists(l)]
    if missing:
        raise SystemExit('modrep: N64Recomp libraries missing (%s): build the game first, or set LIFT_N64RECOMP_LIBS' % missing[0])
    include = [os.path.join(N64RECOMP, 'include'), os.path.join(N64RECOMP, 'lib', 'rabbitizer', 'include'),
               os.path.join(N64RECOMP, 'lib', 'rabbitizer', 'cplusplus', 'include'), os.path.join(N64RECOMP, 'lib', 'fmt', 'include'),
               os.path.join(N64RECOMP, 'lib', 'tomlplusplus', 'include'), TOOL_SOURCE]
    command = [CLANG, '-std=c++20', '-O2', '-static', '-w', '-include', 'cstdlib', '-o', TOOL, sources[0],
               os.path.join(N64RECOMP, 'src', 'config.cpp')] + libs
    for folder in include:
        command[1:1] = ['-I', folder]
    p = subprocess.run(command, capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('modrep: the tool did not build:\n' + p.stderr[:3000])
    return TOOL


def generate(out=OUT, mod_build=MOD_BUILD):
    """Translates the mod's replacements from the built mod into out (modrep_originals.c, modrep_index.json, ...)."""
    tool = build_tool()
    for f in ('mod_syms.bin', 'mod_binary.bin'):
        if not os.path.exists(os.path.join(mod_build, f)):
            raise SystemExit('modrep: %s is missing -- build the mod first' % os.path.join(mod_build, f))
    os.makedirs(out, exist_ok=True)
    p = subprocess.run([tool, os.path.join(mod_build, 'mod_syms.bin'), os.path.join(mod_build, 'mod_binary.bin'), GAME_SYMBOLS, out],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise SystemExit('modrep: the tool failed: ' + (p.stderr or p.stdout)[:2000])
    return index(out)


def sources():
    """The translated originals' files to read beside RecompiledFuncs' (none until generate() has run, or with LIFT_MODREP=0)."""
    if os.environ.get('LIFT_MODREP') == '0':
        return []
    path = os.path.join(OUT, ORIGINALS)
    # (LIFT_EXTRA_ORIGINALS, 2026-11-12 modglue: more translated originals, ';'-separated, read after the mod's copies -- for a
    #  tester's negative control in a tools copy, e.g. one of the mod's own functions under a modrep_ name. Never set by the
    #  pipeline.)
    extra = [p for p in os.environ.get('LIFT_EXTRA_ORIGINALS', '').split(';') if p]
    return ([path] if os.path.exists(path) else []) + extra


def analysis_sources():
    """The mod's other functions (modfn_<address>), for analyse.py only: what a copy reads through one it calls."""
    if os.environ.get('LIFT_MODREP') == '0':
        return []
    path = os.path.join(OUT, 'modrep_modfuncs.c')
    return [path] if os.path.exists(path) else []


CALL_NAME = re.compile(r'\b(\w+)(?=\(rdram, ctx\);)')


def game_names(text):
    """The translated copies' text with each call to a game function the runtime replaces under its name in the game's own
    translation: `osVirtualToPhysical(rdram, ctx)` becomes `osVirtualToPhysical_recomp(rdram, ctx)`.

    (2026-11-12, modcopies.) modrep_tool names a copy's call to a game function by the game's symbol at that address
    (dump.toml), but N64Recomp's translation of the game calls the libultra functions the runtime provides under that name with
    _recomp after it, and that is what the game's function table holds at the address (RecompiledFuncs/recomp_overlays.inl:
    `{ .func = osVirtualToPhysical_recomp, .offset = 0x00034690 }`, 0x80034AE0). The mod loader resolves the copy's call by the
    address, through that table, so in the game the copy calls osVirtualToPhysical_recomp. Under the symbol's own name the
    pipeline took it for an unknown system function and refused the three copies calling it (modrep_func_8011DCC4,
    _80123588, _8012BBB0: "calls a system function"; mod/build/patches/gamesyms.ld links the name to 0x80034AE0). Only names
    that system_reads.py has read by hand with _recomp after them are changed: a name it doesn't know stays as it is, and refused."""
    import system_reads
    known = system_reads.MEMORY
    return CALL_NAME.sub(lambda m: m.group(1) + '_recomp' if m.group(1) + '_recomp' in known else m.group(1), text)


def is_source(path):
    """Whether a file is one of the translations made here (modrep_originals.c, modrep_modfuncs.c): its calls need game_names."""
    return os.path.dirname(os.path.abspath(path)) == os.path.abspath(OUT)


def index(out=OUT):
    """name (modrep_func_X) -> its record: replaces, section_vrom, vram, mod_vram, words, force, hooked_entry, hooked_return, hash."""
    path = os.path.join(out, 'modrep_index.json')
    if not os.path.exists(path):
        return {}
    return {e['name']: e for e in json.load(open(path))}


def sections(out=OUT):
    """The mod's sections' addresses as linked (where RELOC_HI16 / RELOC_LO16's section numbers start), and each one's size."""
    path = os.path.join(out, 'modrep_sections.json')
    return json.load(open(path)) if os.path.exists(path) else []


def is_modrep(name):
    return name.startswith(PREFIX)


def is_mod_own(name):
    """One of the mod's own functions that replaces nothing (modfn_<address>): never converted, never called by converted code."""
    return name.startswith('modfn_')


def replaced(name):
    """modrep_func_X -> func_X."""
    return name[len(PREFIX):]


def record(rec):
    """What candidates.json keeps of a converted copy: the game function it stands in for, and the hash of the mod's copy."""
    return {'replaces': rec['replaces'], 'section_vrom': rec['section_vrom'], 'vram': rec['vram'], 'hash': rec['hash']}


# ---- Checks on a copy before m2c, and its addresses after (lift.py candidates) ----

INSTRUCTION = re.compile(r'^\s*// 0x([0-9A-F]{8}): (\S+)\s*(.*)$')
RELOC = re.compile(r'\bRELOC_(HI|LO)16\((\d+), (0X[0-9A-F]+|\d+)\)')


def relocations(text):
    """The copy's instructions that the translator writes with RELOC_HI16 / RELOC_LO16 (an address in the mod's own sections,
    which the loader decides): {instruction address: [(HI or LO, section, offset)]}."""
    found, last = {}, None
    for line in (text or '').split('\n'):
        m = INSTRUCTION.match(line)
        if m:
            last = int(m.group(1), 16)
            continue
        for r in RELOC.finditer(line):
            if last is not None:
                found.setdefault(last, []).append((r.group(1), int(r.group(2)), int(r.group(3), 0)))
    return found


def mod_ranges(out=OUT):
    """[(section, start, end)]: each of the mod's sections as linked, with 64 KB more at the end (the upper half of an address
    near the end can round up past it: %hi)."""
    return [(i, a, a + size + 0x10000) for i, (a, size) in enumerate(sections(out))]


MAP = os.path.join(MOD_BUILD, 'mod.map')
_C_PATCH_RANGES = None


def c_patch_ranges(path=MAP):
    """[(start, end)] of the mod's .recomp_patch input sections that come from its C (every object but patches.o, which is
    mod/build/patches/mod.S: the asm copies). From the linker's map of the build that is translated (2026-11-12, modglue)."""
    global _C_PATCH_RANGES
    if _C_PATCH_RANGES is None:
        _C_PATCH_RANGES = []
        if os.path.exists(path):
            for m in re.finditer(r'^\s*([0-9a-f]{8})\s+[0-9a-f]{8}\s+([0-9a-f]+)\s+\d+\s+\S*?([\w.]+)\.o:\(\.recomp_patch\)\s*$',
                                 open(path, encoding='utf8', errors='replace').read(), re.M):
                if m.group(3) != 'patches':
                    _C_PATCH_RANGES.append((int(m.group(1), 16), int(m.group(1), 16) + int(m.group(2), 16)))
    return _C_PATCH_RANGES


def c_patch(rec):
    """Whether a copy (its modrep_index record) is one of the mod's C patches, RECOMP_PATCH, rather than an edited copy of the
    game's own MIPS (2026-11-12, modglue)."""
    return any(lo <= rec.get('mod_vram', 0) < hi for lo, hi in c_patch_ranges())


def why_not(name, records, f, text, mod_own=(), hooked=None):
    """Why a copy can't be converted (None if it can, so far): see the module's notes. mod_own: the mod's own functions a copy
    may call (lift.py with_system: those analyse.py has read, 2026-11-12). hooked: for the copies something also hooks, name ->
    None where it converts with its hook calls (lift.py hooked_copies), or why not (2026-11-12, kindd)."""
    rec = records.get(name)
    if rec is None:
        return 'the mod has no such copy now'
    if rec['hooked_entry'] or rec['hooked_return'] or name in (hooked or {}):
        # (The loader's copy of it calls the hook; the converted one wouldn't, so it was refused. Since 2026-11-12 (kindd) one
        #  converts with the hook calls in its wrapper when Stage 1's copy with them is modrep's line for line
        #  (hooks.copy_with_hooks), and the loader takes the converted one only when the hooks on the function are exactly those.)
        why = (hooked or {}).get(name, 'the mod also hooks the function it replaces')
        if why:
            return why
    if c_patch(rec):
        # (2026-11-12, scratch/agents/modglue) One of the mod's C patches (RECOMP_PATCH: kind B of scratch/agents/modnative's
        #  plan). Its whole job is to call the mod's own code or the port's (57 of the 62 are one such call), so those calls are
        #  what it is made of: the converted copy is its glue, which makes the same calls by the same names. In the tester they
        #  are stand-ins on both sides, as every callee is (the user's decision of 2026-11-12: such a function counts once this
        #  glue is proved against the mod's translated copy). In the game the names are bound to Stage 1's linked translation of
        #  the mod (nm_aot_nomercy_arcade_<address>, at the same address as modfn_<address>) and to the port's registered
        #  exports -- release's business, not yet written. The check for a number in the mod's range below still applies.
        imports = set()
    else:
        imports = set(rec.get('imports', []))
    # (in the callees' order by name, 2026-11-12: a set's order changes from run to run, and with it which reason a copy that
    #  calls both kinds was given -- the speed agent's unchanged checks saw modrep_func_80034618's reason change so)
    for callee in sorted(set(f['calls'].values())) if not c_patch(rec) else []:     # (a C patch: none of these, modglue above)
        # (Until 2026-11-12 any call to one of the mod's own functions was refused here. Since then (modcopies) a copy may call
        #  one analyse.py has read: the tester stands in for it on both sides, as for a game function (the user's approval:
        #  "calls between mod functions rewritten to stand-ins in the test build"), and in the game the converted copy calls it
        #  through the loader's own translation of it (release_mod_calls).)
        if is_mod_own(callee) and callee not in mod_own:
            return "calls the mod's own code"
        if callee in imports:
            return 'calls a function the port provides to the mod'
    # A number in the mod's range made by an instruction the translator doesn't relocate would be where the mod was linked, not
    # where it is: refused. (Only a lui can make one.)
    relocated = relocations(text)
    ranges = mod_ranges()
    for a, (mn, ops) in f['ins'].items():
        m = re.match(r'\$\w+,\s*((?:0[xX])?[0-9A-Fa-f]+)$', ops)
        if mn == 'lui' and m and a not in relocated:
            value = (int(m.group(1), 0) << 16) & 0xFFFFFFFF
            if any(lo <= value < hi for _, lo, hi in ranges):
                return "makes a number in the mod's range that isn't relocated"
    return None


LITERAL = re.compile(r'(?<![\w.])(-\s*)?(0[xX][0-9A-Fa-f]+|\d+)([uUlL]*)(?![\w.])')
CPP_HELPER = [
    "// Where the mod loader put the mod's sections: it fills these in before it lets a converted copy of the mod's code run",
    "// (tools/lift/modrep.py). A place in the mod, as it was linked, is now its section's place plus the same offset.",
    'extern "C" int32_t lift_mod_section_address[];',
    'inline u32 lift_mod_address(int section, u32 offset) { return (u32)lift_mod_section_address[section] + offset; }',
    '']


def relocate(cpp, text):
    """The converted copy with each number that is a place in the mod (as linked: m2c folds the relocated lui and its lower half
    into one) as lift_mod_address(section, offset). Returns (cpp, None), or (None, why not).

    Exact either way: the translator's RELOC_HI16 / RELOC_LO16 come to the section's place plus the offset too, and m2c only
    adds and subtracts the instructions' own numbers to them. The tester puts the mod's sections elsewhere than where they were
    linked, so a place left as it was linked is a difference there (run_fuzz)."""
    ranges = mod_ranges()
    data = any(kind == 'HI' for sites in relocations(text).values() for kind, _, _ in sites)
    out, at, count = [], 0, 0
    for m in LITERAL.finditer(cpp):
        v = int(m.group(2), 0)
        if m.group(1):
            if any(lo <= ((-v) & 0xFFFFFFFF) < hi for _, lo, hi in ranges):
                return None, 'm2c wrote a place in the mod as a negative number'
            continue
        hit = [(s, lo) for s, lo, hi in ranges if lo <= v < hi]
        if not hit:
            continue
        section, start = hit[0]
        out.append(cpp[at:m.start()] + 'lift_mod_address(%d, 0x%X)' % (section, v - start))
        at = m.end()
        count += 1
    if count and not data:
        return None, "a number in the mod's range with no relocation for it"
    return ''.join(out) + cpp[at:], None


# ---- In lifted.cpp, and the checks that what is there is still the mod's copy ----

def release_table(entries):
    """lifted.cpp's table of converted copies for the game's mod loader: entries = [(modrep_func_X, its candidates record)]."""
    count = len(sections()) or 1
    out = ["// ---- The mod's copies, converted (tools/lift/modrep.py) ----",
           "// The game's mod loader (librecomp mods.cpp) reads this table through lift_mod_replacement_table. For each replacement in",
           "// the mod it loads, it looks here for the game function replaced (by its section's ROM address and its address). If this",
           "// has a converted copy made from a mod copy with the same hash (lift_modrep_hash.h), and nothing hooks the function, the",
           "// loader points the function's entry at the converted copy, in place of its own translation of the mod's copy; and it",
           "// puts where it loaded the mod's sections in lift_mod_section_address. Otherwise the entry is not used, and the mod's own",
           "// copy runs. Converted callers call the game function's entry, never these by name, so they run what the loader chose.",
           'struct LiftModReplacement {',
           '    uint32_t section_vrom;     // The replaced game function: its section\'s ROM address,',
           '    uint32_t vram;             // and its address.',
           '    const char* name;',
           '    void (*native)(uint8_t* rdram, recomp_context* ctx);',
           '    uint64_t hash;             // The mod\'s copy this was converted from (lift_modrep_hash.h).',
           '};',
           'static const LiftModReplacement lift_mod_replacements[%d] = {' % max(1, len(entries))]
    for name, rec in entries:
        out.append('    {0x%08Xu, 0x%08Xu, "%s", %s, 0x%sull},' % (rec['section_vrom'], rec['vram'], name, name, rec['hash']))
    out += ['};',
            'extern "C" {',
            'int32_t lift_mod_section_address[%d] = {0};' % count,
            '// The table, how many entries it has, and where the loader puts its sections\' places.',
            'const LiftModReplacement* lift_mod_replacement_table(int* count, int32_t** section_address, int* section_count) {',
            '    *count = %d;' % len(entries),
            '    *section_address = lift_mod_section_address;',
            '    *section_count = %d;' % count,
            '    return lift_mod_replacements;',
            '}',
            '}', '']
    return out


MOD_CALL = re.compile(r'::(modfn_[0-9A-F]{8})\b')


def release_mod_calls(cpps):
    """lifted.cpp's definitions of the mod's own functions its converted copies call (2026-11-12, modcopies), and their table for
    the game's mod loader. cpps: the converted copies' C++.

    A converted copy calls one of the mod's own functions as lift_call(ctx, ::modfn_<address as linked>). There is no such
    function in the game: what runs for it is the loader's translation of the mod (live, or the linked one, Stage 1). So each
    modfn_ here is a forwarder to the function the loader fills in, by the mod function's section and its offset in it -- the very
    terms in which the copy's hash holds the call (lift_modrep_hash.h, 'D'). So whenever a converted copy is used (its hash is the
    loaded mod's copy's), the function at that place is the one the copy's own translation would call. The loader fills the
    table when it takes the converted copies (librecomp mods.cpp, lift_mod_call_table), and takes none if one of the places has no
    function in the mod it loads. Unfilled, a forwarder is never reached: no converted copy is in use then."""
    names = sorted({n for c in cpps for n in MOD_CALL.findall(c)})
    places = []
    for n in names:
        at = int(n[len('modfn_'):], 16)
        hit = [(i, at - a) for i, (a, size) in enumerate(sections()) if a <= at < a + size]
        if not hit:
            raise SystemExit("modrep: %s is in none of the mod's sections" % n)
        places.append(hit[0])
    out = ["// ---- The mod's own functions the converted copies call (tools/lift/modrep.py release_mod_calls) ----",
           "// Each is a forwarder to the function the mod loader runs for it (its own translation of the mod), by where the function",
           "// is in the mod: its section and its offset there, the same terms the copies' hashes hold their calls in. The loader fills",
           "// the targets in through lift_mod_call_table when it takes the converted copies.",
           'struct LiftModCall {',
           '    uint32_t section;          // The mod\'s section,',
           '    uint32_t offset;           // and the function\'s offset in it.',
           '};',
           'static const LiftModCall lift_mod_calls[%d] = {' % max(1, len(names))]
    out += ['    {%du, 0x%Xu},   // %s' % (s, o, n) for n, (s, o) in zip(names, places)] or ['    {0u, 0u},']
    out += ['};',
            'static recomp_func_t* lift_mod_call_target[%d] = {nullptr};' % max(1, len(names)),
            'extern "C" {']
    out += ['void %s(uint8_t* rdram, recomp_context* ctx) { lift_mod_call_target[%d](rdram, ctx); }' % (n, k) for k, n in enumerate(names)]
    out += ['// The places, how many there are, and the targets for the loader to fill in.',
            'const LiftModCall* lift_mod_call_table(int* count, recomp_func_t*** targets) {',
            '    *count = %d;' % len(names),
            '    *targets = lift_mod_call_target;',
            '    return lift_mod_calls;',
            '}',
            '}', '']
    return out


IMPORT_CALL = re.compile(r'::(recomp_\w+)\b')


def release_imports(cpps):
    """lifted.cpp's definitions of the port's exports its converted copies call (2026-11-12, final081: modglue's "release side
    still to write", the part for the imports). cpps: the converted copies' C++.

    A C patch's glue calls one of the port's exports as lift_call(ctx, ::recomp_arcade_<x>): the name the mod imports it by
    (recomp_arcade_drive, ...). The port has no C function of that name: it registers its function under the name
    (recomp::overlays::register_base_export("recomp_arcade_drive", drive_run)), and the mod loader resolves the mod's import by
    the name through get_base_export (librecomp mods.cpp). So each recomp_<x> here is a forwarder that does the same, once, and
    calls what is registered: the function the mod's own copy of the patch calls. (A converted copy is used only when its hash
    is the loaded mod's copy's, and a mod whose import has no export is not loaded at all, so the look-up always finds it; if
    it ever did not, the forwarder says so and stops, rather than run nothing in the function's place.)"""
    names = sorted({n for c in cpps for n in IMPORT_CALL.findall(c)})
    out = ["// ---- The port's exports the converted copies call (tools/lift/modrep.py release_imports) ----",
           "// Each is the export the mod imports under this name, looked up as the mod loader looks it up (get_base_export).",
           '#include <cstdio>',
           '#include <cstdlib>',
           '#include <string>',
           'namespace recomp { namespace overlays { recomp_func_t* get_base_export(const std::string& export_name); } }',
           'static recomp_func_t* lift_import(const char* name) {',
           '    recomp_func_t* f = recomp::overlays::get_base_export(name);',
           '    if (f == nullptr) { std::fprintf(stderr, "[lift] the port has no export %s, which a converted copy calls\\n", name); std::abort(); }',
           '    return f;',
           '}',
           'extern "C" {']
    out += ['void %s(uint8_t* rdram, recomp_context* ctx) { static recomp_func_t* const f = lift_import("%s"); f(rdram, ctx); }' % (n, n)
            for n in names]
    out += ['}', '']
    return out if names else []


ROW = re.compile(r'^\s*\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, "(modrep_\w+)", (\w+), 0x([0-9A-F]{16})ull\},$', re.M)


def current_hashes(mod_build=MOD_BUILD):
    """name -> hash, for the mod as it is built now (translated again into a folder of its own)."""
    import tempfile
    out = tempfile.mkdtemp(prefix='modrep_check_')
    try:
        return {n: e['hash'] for n, e in generate(out, mod_build).items()}
    finally:
        import shutil
        shutil.rmtree(out, ignore_errors=True)


def check(lifted, mod_build=MOD_BUILD):
    """The converted copies in a lifted.cpp that the mod as built now no longer has as they were: [(name, why)]. Also that the mod
    loader's copy of the hash's code, where there is one, is the same as this one's (they must agree for any to be used)."""
    text = open(lifted, encoding='utf8').read()
    # (2026-11-16, honesty: the table is in lifted_changed.inc, which lifted.cpp includes, since the release has the functions the
    #  mod has a hand in apart: read with it when it is beside the lifted.cpp given)
    beside = os.path.join(os.path.dirname(os.path.abspath(lifted)), 'lifted_changed.inc')
    if os.path.basename(lifted) == 'lifted.cpp' and os.path.exists(beside):
        text += '\n' + open(beside, encoding='utf8').read()
    rows = ROW.findall(text)
    bad = []
    loader = os.path.join(PORT, 'lib', 'N64ModernRuntime', 'librecomp', 'src', 'lift_modrep_hash.h')
    if rows and os.path.exists(loader) and open(loader, 'rb').read().replace(b'\r', b'') != \
            open(os.path.join(TOOL_SOURCE, 'lift_modrep_hash.h'), 'rb').read().replace(b'\r', b''):
        bad.append(('(all)', "the mod loader's lift_modrep_hash.h differs from tools/lift/modrep's"))
    if not rows:
        return bad
    now = current_hashes(mod_build)
    for vrom, vram, name, native, h in rows:
        if name not in now:
            bad.append((name, 'the mod no longer replaces %s' % replaced(name)))
        elif now[name] != h:
            bad.append((name, "the mod's copy changed (hash %s, converted from %s)" % (now[name], h)))
    return bad


if __name__ == '__main__':
    # python tools/lift/modrep.py            translate the mod's replacements from its build into modrep/out
    # python tools/lift/modrep.py check [lifted.cpp]   list converted copies the mod as built now has made stale (exit 1)
    if len(sys.argv) > 1 and sys.argv[1] == 'check':
        lifted = sys.argv[2] if len(sys.argv) > 2 else os.path.join(PORT, 'src', 'game', 'lifted.cpp')
        bad = check(lifted)
        for name, why in bad:
            print('STALE %s: %s' % (name, why))
        print('%d stale converted copies of the mod\'s' % len(bad) if bad else "the converted copies of the mod's in lifted.cpp are the mod's copies as built now")
        sys.exit(1 if bad else 0)
    found = generate()
    print('%d replacements in the mod -> %s' % (len(found), OUT))


# Where the tester puts the mod's sections: a little over 4 MB past where they were linked (lift.py run_fuzz). Not a multiple
# of 64 KB, on purpose: the loader only promises 16 bytes. Where m2c has kept the upper half of a place in the mod on its own (a
# lui's value, left in a register a caller reads), it is right only for a section at a multiple of 64 KB, and differs here.
TESTER_SHIFT = 0x412340
