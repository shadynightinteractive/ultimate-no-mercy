#!/usr/bin/env python3
"""Each hooked game function's translation from the ROM, against the port's own (2026-11-12, hookbug).

    python tools\aot\check_hooked.py [<mod_aot folder> [<RecompiledFuncs folder>]]   (aot.py runs it after the tool)

The loader translates a hooked game function again from the ROM (mods.cpp regenerate_with_hooks, or this folder's linked
copy in mod_aot_hooked.c) and points the game's function at that. The port's RecompiledFuncs version is what the port means
to run: anything the post-recompile tools changed in it (RecompiledFuncs/repaired_functions.txt), and the ROM it was made
from. So each hooked copy, its hook calls taken out, must be the port's text line for line, except that every call is a
lookup by address (the copy's context looks every call up): each such lookup must give the function the port calls there.
get_function gives what the runtime's function map has at the address, from the loaded sections, so a lookup gives the
port's function when the port's function is (a) in the caller's own section (loaded while the caller runs) or (b) the only
function any section has at that address. Anything else is said, and the run fails: the hooked copy would not be the port's
function, and a person has to look at it.
"""
import glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
IN_PROJECT = os.path.exists(os.path.join(HERE, '..', '..', 'exe', 'WWFNoMercyRecomp'))
PORT = os.path.abspath(os.path.join(HERE, '..', '..', 'exe', 'WWFNoMercyRecomp') if IN_PROJECT else os.path.join(HERE, '..', 'exe_src', 'WWFNoMercyRecomp'))
FUNC = re.compile(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{\n(.*?)^;\}', re.S | re.M)


def bodies(text):
    return {m.group(1): m.group(2) for m in FUNC.finditer(text.replace('\r\n', '\n'))}


def main(OUT=os.path.join(PORT, 'src', 'game', 'mod_aot'), RF=os.path.join(PORT, 'RecompiledFuncs')):
    """0 if every hooked copy in OUT (a mod_aot folder) is the port's function in RF (a RecompiledFuncs folder), else 1."""
    # The port's sections (recomp_overlays.inl): each function's section and address.
    inl = open(os.path.join(RF, 'recomp_overlays.inl')).read()
    arrays = {m.group(1): re.findall(r'\.func = (\w+), \.offset = 0x([0-9A-F]+),', m.group(2))
              for m in re.finditer(r'static FuncEntry (\w+)\[\] = \{(.*?)\n\};', inl, re.S)}
    at_place, at_vram = {}, {}   # (section ROM, address) -> name; address -> [(section ROM, name)]
    for m in re.finditer(r'\{ \.rom_addr = 0x([0-9A-F]+), \.ram_addr = 0x([0-9A-F]+), \.size = 0x[0-9A-F]+, \.funcs = (\w+),', inl):
        rom, ram = int(m.group(1), 16), int(m.group(2), 16)
        for name, offset in arrays[m.group(3)]:
            at_place[(rom, ram + int(offset, 16))] = name
            at_vram.setdefault(ram + int(offset, 16), []).append((rom, name))
    port = {}
    for path in glob.glob(os.path.join(RF, 'funcs_*.c')):
        port.update(bodies(open(path).read()))

    hooked = bodies(open(os.path.join(OUT, 'mod_aot_hooked.c')).read())
    problems, lookups = [], 0
    for name, body in sorted(hooked.items()):
        m = re.match(r'nm_aot_hooked_([0-9A-F]{8})_([0-9A-F]{8})$', name)
        section = int(m.group(1), 16)
        port_name = at_place.get((section, int(m.group(2), 16)))
        if port_name not in port:
            problems.append('%s: the port has no translated function there (%s)' % (name, port_name))
            continue
        # The hook calls out (aot_tool.cpp emit_function_start / emit_return put them in).
        mine = re.sub(r'    nm_aot_run_hook\(rdram, ctx, nm_aot_hook_slot\[\d+\]\);\n', '', body)
        mine = re.sub(r'\{ nm_aot_run_hook\(rdram, ctx, nm_aot_hook_slot\[\d+\]\); (return;)\n    \}\n', r'\1\n', mine)
        a, b = mine.split('\n'), port[port_name].split('\n')
        if len(a) != len(b):
            problems.append('%s (%s): %d lines, the port\'s %d' % (name, port_name, len(a), len(b)))
            continue
        for line, (x, y) in enumerate(zip(a, b)):
            if x == y:
                continue
            lookup = re.match(r'^\s*LOOKUP_FUNC\(0x([0-9A-F]+)\)\(rdram, ctx\);$', x)
            named = re.match(r'^\s*(\w+)\(rdram, ctx\);$', y)
            if lookup and named:
                lookups += 1
                there = at_vram.get(int(lookup.group(1), 16), [])
                if (section, named.group(1)) in there or [n for _, n in there] == [named.group(1)]:
                    continue
                problems.append('%s (%s) line %d: looks up 0x%s, which gives one of %s; the port calls %s'
                                % (name, port_name, line, lookup.group(1), [n for _, n in there] or 'nothing', named.group(1)))
            else:
                problems.append('%s (%s) line %d:\n    from the ROM: %s\n    the port:     %s' % (name, port_name, line, x.strip(), y.strip()))
    for p in problems:
        print('check_hooked: ' + p)
    print('check_hooked: %d hooked game functions, %d of their calls looked up where the port names the function: %s'
          % (len(hooked), lookups, 'each copy is the port\'s function' if not problems else '%d difference(s)' % len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:3]))
