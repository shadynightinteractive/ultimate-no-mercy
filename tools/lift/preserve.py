#!/usr/bin/env python3
"""A base-game preservation snapshot (2026-11-13, agent "preserve"; proposed for tools/lift/preserve.py, run after each release).

Writes Development/preservation/base_game_<version>/: the original game's functions -- WWF No Mercy as the stock cartridge has it,
without the Ultimate No Mercy mod, the PC port's own code, or the six-man engine's rewrites -- as readable C++ where they are
converted and random-tested, and as MIPS plus N64Recomp's mechanical C where they are not, sorted into files by area, with names.json,
fields.json, a README (provenance by the ROM's hashes only, the version, each function's test status) and an MD5 list, and a zip.
It reads only what the release wrote and the project's dumps:
  - src/game/lifted.cpp and lifted_changed.inc (the release's converted functions: their C++, comments and evidence lines;
    lifted_changed.inc has those the mod has a hand in -- a game function the mods only hook is still the game's code);
  - tools/lift/names.json, fields.json, fuzz/report.txt (each function's test numbers);
  - the stock ROM's translation (--stock-translated; by default made here, in a folder deleted after: N64Recomp of the stock
    ROM, read in place, with the port's fixers but not its hand edits -- make_stock_translation): the base game's
    functions, their MIPS (each instruction's comment) and mechanical C;
  - the game's translation (exe/WWFNoMercyRecomp/RecompiledFuncs: the six-man engine ROM's), only to see which functions the
    engine changed: those are in the release as the engine's versions, not the stock ones. Each such function's stock version is
    used where --stock-release (a lifted.cpp written by lift.py's release text from a test run on the stock translation) has it
    with a report line (--stock-report) at the full bar; otherwise it is listed as not converted, with why.
It never reads or copies the ROM beyond hashing it, and copies no graphics or sound.

    python tools/lift/preserve.py <version> [--out DIR] [--stock-translated DIR] [--stock-release DIR --stock-report FILE]
                                            [--release DIR] [--no-zip]
"""
import argparse, collections, datetime, hashlib, json, os, re, shutil, sys, zipfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
BAR = 200000                      # (plain random tests a converted function has: the fixed bar, RULES / lift.py FULL_TESTS)

# (2026-11-16, honesty: the user's approval of the outside review's wording item, and the user's naming rule) A new snapshot says
#  what the evidence is -- "random-tested" -- never "proved", and names the project by the first label of the rule, the one whose
#  gate is met: "non-matching decompilation in progress, N% converted", N the release's counted figure (converted and
#  random-tested), read from the release's lifted.cpp header (lift.py header_lines). The later labels wait for their gates (every
#  game function converted, RecompiledFuncs out of the build). Snapshots already written are left as they are. The folder name
#  stock_proof/ is kept, so earlier snapshots are still found by later ones.
LABEL = re.compile(r'non-matching decompilation in progress, ([0-9.]+%) converted')
OLD_NOTE = re.compile(r'^// Proved: (\d+) comparisons in play, (\d+) on plain random inputs: all instructions reached, no difference\.$')


def evidence_line(line):
    """A release's note line in today's words: an older release's '// Proved: ...' as lift.py evidence_note says it now (the same
    numbers); anything else as it is."""
    m = OLD_NOTE.match(line)
    if not m:
        return line
    in_play, plain = int(m.group(1)), int(m.group(2))
    return '// Random-tested: %d plain random inputs, all instructions reached, no difference%s.' % (
        plain, '; in play (lift_twin.cpp): %d comparisons, no difference' % in_play if in_play > 0 else '')


def release_label(paths):
    """The release's status label from its lifted.cpp header, or a plain one without the figure for a release written before it."""
    for path in paths:
        if path and os.path.exists(path):
            m = LABEL.search(open(path, encoding='utf8').read(20000))
            if m:
                return 'a non-matching decompilation in progress, %s converted' % m.group(1)
    return 'a non-matching decompilation in progress (the converted figure is not in this release\'s lifted.cpp: see tools/lift/lift.py score)'
FUNC = re.compile(r'^RECOMP_FUNC void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{$', re.M)
INS = re.compile(r'^\s*// 0x([0-9A-F]{8}): (.*?)\s*$', re.M)
CALL = re.compile(r'\b(\w+)\(rdram, ctx\);')
ADAPTER = re.compile(r'^extern "C" (?:__attribute__\(\(noinline\)\) )?void (\w+)\(uint8_t\* rdram, recomp_context\* ctx\) \{\n(.*?)^\}', re.M | re.S)
DEFINITION = re.compile(r'\b(\w+)\(.*\)\s*\{\s*$')

# ---- Areas ----
# A function's area from its name (names.json), then its about, then the call graph (the area most of its callers and callees
# have), then where it is. The first rule that matches wins, so the more particular come first.
AREAS = [
    ('libultra', 'libultra: the N64 SDK library (OS, audio library, graphics utilities, controller pak, libc)', None),
    ('ai', 'computer-controlled wrestlers (AI)', r'^ai_|_ai_|_ai$|\bplan|opponent|cpu_'),
    ('audio', 'sound, music and voices', r'sound|audio|music|song|sfx|voice|crowd_noise|channel|sequence|volume|sample'),
    ('menus', 'menus and pages (Superstar Select, editors, options, the title)', r'page|menu|widget|row|choice|select|editor|edit_|cursor|'
                                                                               r'scroll|title|card|preview|option|league|bracket|story|caw|'
                                                                               r'superstar|roster|text|font|name_'),
    ('input', 'controllers and input', r'controller|button|stick|input|rumble_pak|pad_'),
    ('saves', 'saves, the controller pak and files on the cartridge', r'save|pak|pfs|eeprom|sram|flash_ram|file|rom_'),
    ('memory', 'memory, heaps and pools', r'heap|alloc|pool|free_|_free|memory|buffer|copy_bytes|memcpy|memset|bzero'),
    ('wrestler', 'wrestler states, moves and animation', r'state|move|grapple|strike|dive|lying|running|attack|animation|anim_|bone|'
                                                         r'skeleton|reversal|pin_|submission|throw|hit_|damage|spirit|stamina|wrestler|'
                                                         r'man_|actor|pose|taunt'),
    ('match', 'match flow (rules, the ring, entrances, referees, the camera, the HUD)', r'match|ring|cage|ladder|entrance|entrant|rumble|'
                                                                                       r'referee|count|rope|apron|corner|tag_|team|belt|'
                                                                                       r'weapon|camera|hud|crowd|arena|backstage|intro'),
    ('graphics', 'graphics (display lists, models, textures, matrices, effects)', r'draw|texture|picture|quad|matrix|mtx|model|render|'
                                                                                  r'colour|color|palette|sprite|vertex|light|effect|fx|'
                                                                                  r'scene|display|screen|frame|fade|flash|part'),
    ('system', 'start-up, threads, the scheduler and timing', r'thread|schedul|task|queue|message|boot|start_up|interrupt|timer|clock|'
                                                             r'vi_|retrace'),
]
LIBULTRA_PREFIXES = ('al_', 'gu_', 'pfs_', 'cont_', 'si_', 'epi_', 'os_', 'vi_', 'pi_', 'sp_', 'osContStartReadData', '__')
MAIN_END = 0x80040380             # (the main segment's end: main_1050 in syms/dump.toml)


def area_of_text(text):
    text = text.lower()
    for key, _, pattern in AREAS:
        if pattern and re.search(pattern, text):
            return key
    return None


def libultra_start(names):
    """The lowest address of a function in main named as an SDK library function: libultra sits from there to main's end."""
    found = [int(n[5:13], 16) for n, v in names.items() if n.startswith('func_') and v.get('name', '').startswith(LIBULTRA_PREFIXES)
             and 0x80020000 <= int(n[5:13], 16) < MAIN_END]
    return min(found) if found else MAIN_END


def address_of(name):
    m = re.search(r'([0-9A-F]{8})', name)
    return int(m.group(1), 16) if m else None


def classify(functions, names, graph):
    """name -> (area, how it was decided)."""
    start = libultra_start(names)
    areas = {}
    for n in functions:
        a, info = address_of(n), names.get(n, {})
        shown = info.get('name', '')
        if (a is not None and start <= a < MAIN_END and not n.endswith(tuple('_ovl_' + c for c in 'abcde'))) or \
                shown.startswith(LIBULTRA_PREFIXES) or n.endswith('_recomp') or n.startswith('os'):
            areas[n] = ('libultra', 'its place: the SDK library in the main segment')
            continue
        by_name = area_of_text(shown) if shown and not shown.startswith('func_') else None
        if by_name:
            areas[n] = (by_name, 'its name')
            continue
        by_about = area_of_text(info.get('about', ''))
        if by_about:
            areas[n] = (by_about, 'its explanation')
    # (The rest by the call graph: the area most of the functions it calls and that call it have, repeated while it decides more.)
    callers = collections.defaultdict(set)
    for n, callees in graph.items():
        for c in callees:
            callers[c].add(n)
    for _ in range(6):
        decided = {}
        for n in functions:
            if n in areas:
                continue
            votes = collections.Counter(areas[m][0] for m in (graph.get(n, set()) | callers.get(n, set())) if m in areas and areas[m][0] != 'libultra')
            if votes:
                decided[n] = (votes.most_common(1)[0][0], 'the functions it calls and that call it')
        if not decided:
            break
        areas.update(decided)
    for n in functions:
        if n not in areas:
            a = address_of(n)
            areas[n] = ('system' if a is not None and a < 0x800D9960 else 'match', 'its place (nothing else to go by)')
    return areas


# ---- Reading what the release wrote and the dumps ----

def translation(folder):
    """name -> its translated text (N64Recomp's mechanical C, each instruction's MIPS in a comment), from a RecompiledFuncs folder."""
    out = {}
    for f in sorted(os.listdir(folder)):
        if not f.endswith('.c'):
            continue
        text = open(os.path.join(folder, f), encoding='utf8', errors='replace').read()
        starts = list(FUNC.finditer(text))
        for k, m in enumerate(starts):
            end = starts[k + 1].start() if k + 1 < len(starts) else len(text)
            body = text[m.start():end].rstrip()
            out[m.group(1)] = body
    return out


def mips(text):
    """The function's MIPS, in address order, once each (the translation writes a delay slot's beside its branch)."""
    seen = {}
    for a, ins in INS.findall(text):
        seen.setdefault(int(a, 16), re.sub(r'\s+', ' ', ins))
    return [(a, seen[a]) for a in sorted(seen)]


def converted(path):
    """What a release's lifted.cpp has: {N64 name: {'shown', 'block' (its comments and C++), 'proof', 'port'}}, and its preamble
    (everything ahead of the first function: helpers, the named fields). With the release's lifted_changed.inc beside it
    (lift.py, 2026-11-16), that file's functions too: lifted.cpp's preamble is the one returned."""
    if not path or not os.path.exists(path):
        return {}, ''
    beside = os.path.join(os.path.dirname(path), 'lifted_changed.inc')
    out, preamble = converted_one(path)
    if os.path.basename(path) == 'lifted.cpp' and os.path.exists(beside):
        more, _ = converted_one(beside)
        out.update(more)
    return out, preamble


def converted_one(path):
    text = open(path, encoding='utf8').read()
    shown_to_n64, port = {}, set()
    for m in ADAPTER.finditer(text):
        g = re.search(r'game::(\w+)\(', m.group(2))
        if g:
            shown_to_n64[g.group(1)] = m.group(1)
            if '[port addition]' in m.group(2):
                port.add(m.group(1))
    lines = text.split('\n')
    try:
        first = lines.index('namespace game {')
        last = lines.index('}  // namespace game')
    except ValueError:
        return {}, ''
    out, preamble_end = {}, None
    k = first + 1
    while k < last:
        if lines[k].startswith(('// Random-tested: ', '// Proved: ', '// Under comparison')):
            lines[k] = evidence_line(lines[k])
            start = k
            while start > first + 1 and lines[start - 1].startswith('//'):
                start -= 1
            end = k + 1
            while end < last and lines[end] != '}':
                end += 1
            d = DEFINITION.search(lines[k + 1]) if k + 1 < last else None
            shown = d.group(1) if d else None
            n64 = shown_to_n64.get(shown)
            if n64:
                block = '\n'.join(lines[start:end + 1])
                out[n64] = {'shown': shown, 'block': block, 'proof': lines[k][3:].rstrip('.'),
                            'port': n64 in port or 'lift_port_' in block}
                if preamble_end is None:
                    preamble_end = start
            k = end + 1
            continue
        k += 1
    preamble = '\n'.join(lines[:first] + lines[first:preamble_end or first + 1] + ['}  // namespace game (the functions are in the area files)'])
    return out, preamble


def report(path):
    """name -> its tester numbers, from a fuzz report (lift.py read_reports' columns)."""
    out = {}
    if not path or not os.path.exists(path):
        return out
    for line in open(path):
        f = line.split()
        if len(f) < 7:
            continue
        name, calls, checked, different, reached, total = f[:6]
        out[name] = {'tests': int(calls), 'compared': int(checked), 'different': int(different),
                     'reached': bin(int(reached, 16)).count('1'), 'instructions': int(total),
                     'plain': int(f[7]) if len(f) > 7 else 0}
    return out


def meets_bar(r):
    return bool(r) and r['different'] == 0 and r['reached'] >= r['instructions'] and r['plain'] >= BAR


N64RECOMP = os.path.join(ROOT, 'libs', 'N64Recomp', 'build-native', 'N64Recomp.exe')
# The port's steps after N64Recomp (six-engine/work/port/regen.py STEPS), less readd_hand_edits: the port's own edits (the 60 Hz
# matrix hooks, the widescreen cull, the netplay guard, the dcc4 airbag) are not the game's.
FIXERS = ['fix_stumps', 'fix_switches', 'fix_jraddend', 'fix_backbranches', 'fix_selfentry', 'audit_coresidency']


def make_stock_translation(work, rom):
    """The stock ROM translated as the port translates it, without the port's hand edits, into work/RecompiledFuncs: the exe's
    nomercy.toml (its ROM path pointed at the stock ROM where it is: the ROM is read in place, never copied), its symbols less the
    six-man engine's section (rwblob), and its fixers. (Checked 2026-11-13: with the hand edits too, this is RecompiledFuncs.stock
    function for function; without them, 6 functions differ, the 6 the hand edits change.)"""
    import subprocess
    os.makedirs(os.path.join(work, 'RecompiledFuncs'))
    shutil.copytree(os.path.join(PORT, 'syms'), os.path.join(work, 'syms'))
    shutil.copytree(os.path.join(PORT, 'tools'), os.path.join(work, 'tools'), ignore=shutil.ignore_patterns('ffmpeg_build', '__pycache__'))
    dump = os.path.join(work, 'syms', 'dump.toml')
    s = open(dump, encoding='utf8').read()
    i = s.find('[[section]]\nname = "rwblob"')
    if i >= 0:
        j = s.find('[[section]]', i + 10)
        s = s[:i].rstrip('\n') + '\n' + ('\n' + s[j:] if j >= 0 else '')
        open(dump, 'w', newline='\n', encoding='utf8').write(s)
    toml = open(os.path.join(PORT, 'nomercy.toml'), encoding='utf8').read()
    toml = re.sub(r'(?m)^rom_file_path = ".*"$', 'rom_file_path = "%s"' % os.path.abspath(rom).replace('\\', '/'), toml)
    open(os.path.join(work, 'nomercy.toml'), 'w', newline='\n', encoding='utf8').write(toml)
    # (fix_switches.py reads the ROM beside its tools folder: these copies of the fixers read it where it is instead)
    for step in FIXERS:
        path = os.path.join(work, 'tools', step + '.py')
        text = open(path, encoding='utf8').read()
        open(path, 'w', newline='\n', encoding='utf8').write(text.replace("os.path.join(ROOT, 'nomercy.z64')", repr(os.path.abspath(rom))))
    p = subprocess.run([N64RECOMP, 'nomercy.toml'], cwd=work, capture_output=True, text=True)
    if p.returncode:
        raise SystemExit('N64Recomp failed on the stock ROM:\n' + (p.stdout + p.stderr)[-2000:])
    for step in FIXERS:
        p = subprocess.run([sys.executable, os.path.join('tools', step + '.py')], cwd=work, capture_output=True, text=True)
        if p.returncode:
            raise SystemExit('%s failed on the stock translation:\n%s' % (step, (p.stdout + p.stderr)[-2000:]))
    return os.path.join(work, 'RecompiledFuncs')


HAND_MARKS = ('[wcw fix]', '[nomercy][', 'HAND-EDIT')     # (the hand edits' marks; the fixers' own are '[nomercy fix_...]')


def strip_hand_edits(stock):
    """(convert2, 2026-11-22) A stock translation as the exe's build keeps it, exe/WWFNoMercyRecomp/RecompiledFuncs.stock, is
    N64Recomp of the stock ROM with the port's fixers AND its hand edits (tools/readd_hand_edits.py: the 60 Hz matrix hooks,
    the widescreen cull, the netplay guard, the dcc4 airbag, the ray-tracing camera hand-off) -- checked 2026-11-22: of the
    0.84 snapshot's 469 not-converted functions (made by make_stock_translation, no hand edits) 464 are the same text and the 5
    others are exactly hand-edited ones. Those edits are taken out here, with the edit registry's own texts (the inserted lines
    removed, the replaced line put back), so what the snapshot shows is the game's code alone. Returns {name: text} and the
    names still carrying a hand-edit mark afterwards (none expected: the README says so if any)."""
    import importlib.util
    spec = importlib.util.spec_from_file_location('readd_hand_edits', os.path.join(PORT, 'tools', 'readd_hand_edits.py'))
    reg = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reg)
    strip = lambda block: [l.rstrip('\n') for l in block]
    inserted = [strip(reg.WCW_MATRIX_HOOK), strip(reg.WCW_F2L_HOOK), strip(reg.DCC4_PROBE)]
    replaced = [(strip(reg.WCW_RESYNC_NULL_GUARD), ['    func_80021B10(rdram, ctx);']), (strip(reg.WCW_CULL_EDIT), [reg.WCW_CULL_COMPARE])]
    out, left = {}, []
    for name, text in stock.items():
        if not any(mark in text for mark in HAND_MARKS):
            out[name] = text
            continue
        lines = text.replace('\r\n', '\n').split('\n')
        def cut(block, put):
            for i in range(len(lines) - len(block) + 1):
                if lines[i:i + len(block)] == block:
                    lines[i:i + len(block)] = put
                    return True
            return False
        for block in inserted:
            cut(block, [])
        for block, put in replaced:
            cut(block, put)
        # (the ray-tracing hand-offs, three lines each, written inside rt_hand_edits: `    { /* [nomercy][rt] ...` up to the line
        #  that closes it)
        i = 0
        while i < len(lines):
            if lines[i].startswith('    { /* [nomercy][rt]'):
                j = i
                while j < len(lines) and not lines[j].rstrip().endswith('}'):
                    j += 1
                del lines[i:j + 1]
                continue
            i += 1
        t = '\n'.join(lines)
        if any(mark in t for mark in HAND_MARKS):
            left.append(name)
        out[name] = t
    return out, left


def md5(path):
    h = hashlib.md5()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def rom_identity(path):
    """The cartridge by its hashes and header only (never copied)."""
    if not os.path.exists(path):
        return None
    data = open(path, 'rb').read()
    return {'md5': hashlib.md5(data).hexdigest(), 'sha1': hashlib.sha1(data).hexdigest(), 'crc32': '%08X' % (zlib.crc32(data) & 0xFFFFFFFF),
            'size': len(data), 'title': data[0x20:0x34].decode('ascii', 'replace').strip(), 'game_code': data[0x3B:0x3F].decode('ascii', 'replace'),
            'revision': data[0x3F]}


def comment_block(lines):
    return '\n'.join('// ' + l if l else '//' for l in lines)


def mips_block(listing, heading):
    return '\n'.join(['/* ' + heading] + ['   0x%08X: %s' % (a, ins) for a, ins in listing] + ['*/'])


# ---- The snapshot ----

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('version')
    ap.add_argument('--out', default=os.path.join(ROOT, 'preservation'))
    ap.add_argument('--release', default=os.path.join(PORT, 'src', 'game'))
    ap.add_argument('--tools', default=HERE, help='where names.json, fields.json and fuzz/report.txt are')
    ap.add_argument('--report', default=None)
    ap.add_argument('--game-translated', default=os.path.join(PORT, 'RecompiledFuncs'))
    ap.add_argument('--stock-translated', default=None, help='a translation of the stock ROM; made here when not given')
    ap.add_argument('--stock-release', default=None)
    ap.add_argument('--stock-report', default=None)
    ap.add_argument('--stock-note', default='')
    ap.add_argument('--rom', default=os.path.join(PORT, 'nomercy.z64'))
    ap.add_argument('--no-zip', action='store_true')
    args = ap.parse_args()
    if not args.stock_release:
        # (By default the newest earlier snapshot's stock proof: the stock ROM doesn't change, so a stock version once proved stays
        #  proved. --stock-release none: without one.)
        earlier = sorted((os.path.getmtime(os.path.join(args.out, d)), os.path.join(args.out, d, 'stock_proof'))
                         for d in (os.listdir(args.out) if os.path.isdir(args.out) else [])
                         if d.startswith('base_game_') and d != 'base_game_%s' % args.version and os.path.isdir(os.path.join(args.out, d, 'stock_proof')))
        if earlier:
            args.stock_release = earlier[-1][1]
            args.stock_report = args.stock_report or os.path.join(earlier[-1][1], 'report.txt')
            print('stock versions\' tests: %s' % args.stock_release)
    elif args.stock_release == 'none':
        args.stock_release = None
    out = os.path.join(args.out, 'base_game_%s' % args.version)
    if args.stock_release and os.path.abspath(args.stock_release).startswith(os.path.abspath(out) + os.sep):
        raise SystemExit("the stock proof is inside the snapshot this replaces: copy it elsewhere first")

    out = os.path.join(args.out, 'base_game_%s' % args.version)
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'converted'))
    os.makedirs(os.path.join(out, 'not_converted'))

    names = json.load(open(os.path.join(args.tools, 'names.json'), encoding='utf8'))
    made = None
    if not args.stock_translated:
        import tempfile
        made = tempfile.mkdtemp(prefix='preserve_stock_')
        args.stock_translated = make_stock_translation(made, args.rom)
        shown_translation = "made by preserve.py from that ROM (N64Recomp, the port's fixers, not its hand edits)"
    else:
        shown_translation = '`%s`' % os.path.relpath(args.stock_translated, ROOT).replace('\\', '/')
    stock = translation(args.stock_translated)
    raw_stock = stock
    stock, hand_left = strip_hand_edits(stock)
    stripped = sorted(n for n in raw_stock if raw_stock[n] != stock[n])
    if stripped:
        shown_translation += ", with the PC port's hand edits (tools/readd_hand_edits.py) taken out of %d functions (%s)" % (len(stripped), ', '.join(stripped))
    if made:
        shutil.rmtree(made, ignore_errors=True)
    game = translation(args.game_translated)
    done, preamble = converted(os.path.join(args.release, 'lifted.cpp'))
    stock_done, _ = converted(os.path.join(args.stock_release, 'lifted.cpp')) if args.stock_release else ({}, '')
    tests = report(args.report or os.path.join(args.tools, 'fuzz', 'report.txt'))
    stock_tests = report(args.stock_report)
    graph = {n: set(CALL.findall(t)) & set(stock) for n, t in stock.items()}
    areas = classify(sorted(stock), names, graph)

    # Each base-game function: which C++ (if any) stands for it, and why.
    rows, by_area = [], collections.defaultdict(lambda: {'converted': [], 'not': []})
    for n in sorted(stock, key=lambda n: (address_of(n) or 0, n)):
        listing = mips(stock[n])
        engine = n not in game or mips(game[n]) != listing
        entry, status, proof = None, '', ''
        if n in done and not engine and not done[n]['port']:
            entry, status, proof = done[n], 'converted', done[n]['proof']
        elif n in done and done[n]['port']:
            status = "not converted here: the release's conversion has the PC port's own additions in it (its original here has none)"
        elif engine and n in stock_done and meets_bar(stock_tests.get(n)):
            r = stock_tests[n]
            entry, status = stock_done[n], 'converted: the stock version (the release has the six-man engine\'s), random-tested on its own'
            proof = 'Random-tested: %d plain random tests, %d compared, no difference, all %d instructions reached (against the stock original)' % (
                r['plain'], r['compared'], r['instructions'])
        elif engine and n in done:
            r = stock_tests.get(n)
            status = "not converted here: the release has the six-man engine's version (%s); the stock version %s" % (
                'its instructions differ from the stock ROM\'s', 'is not random-tested to the standard yet' + (
                    ' (%d plain tests, %d differences, %d of %d instructions reached)' % (r['plain'], r['different'], r['reached'], r['instructions'])
                    if r else ''))
        elif engine:
            status = "not converted (the six-man engine changes it too)"
        else:
            status = 'not converted yet'
        info = names.get(n, {})
        row = {'n64_name': n, 'name': info.get('name', ''), 'area': areas[n][0], 'area_from': areas[n][1], 'instructions': len(listing),
               'status': status, 'evidence': proof, 'six_engine_changes_it': engine,
               'tests': tests.get(n) if not engine else stock_tests.get(n)}
        rows.append(row)
        heading = '==== %s%s: %d instructions ==== %s' % (n, ' (%s)' % info['name'] if info.get('name') else '', len(listing), status)
        if entry:
            # (Its evidence is the block's own last comment line, as the release has it, in today's words: evidence_line.)
            text = '\n'.join(['// ' + heading, entry['block'],
                              mips_block(listing, 'The original N64 code (MIPS, the stock ROM\'s), %s:' % n), ''])
            by_area[areas[n][0]]['converted'].append(text)
        else:
            about = ['%s: %s' % (info['name'], info['about'])] if info.get('about') else []
            text = '\n'.join(['// ' + heading] + [comment_block(about)] * bool(about) +
                             [mips_block(listing, 'The original N64 code (MIPS, the stock ROM\'s), %s:' % n),
                              "// N64Recomp's mechanical C of it (each instruction's MIPS in a comment):", stock[n], ''])
            by_area[areas[n][0]]['not'].append(text)

    titles = {k: t for k, t, _ in AREAS}
    label = release_label([os.path.join(args.release, 'lifted.cpp')])
    for area, parts in sorted(by_area.items()):
        for kind, folder, ext in (('converted', 'converted', '.cpp'), ('not', 'not_converted', '.c')):
            if not parts[kind]:
                continue
            head = ['// WWF No Mercy (N64), the base game: %s -- %s.' % (titles.get(area, area), 'readable C++, each function converted from its N64 code and '
                    'random-tested against it, its original MIPS beside it' if kind == 'converted' else 'not yet converted: the original MIPS and N64Recomp\'s mechanical C'),
                    '// A private preservation copy, not for distribution. Snapshot of release %s (%s); see README.md.' % (args.version, label), '']
            if kind == 'converted':
                head += ['#include "_preamble.h"   // (the helpers and named fields the C++ uses, as the release has them)', '']
            open(os.path.join(out, folder, area + ext), 'w', newline='\n', encoding='utf8').write('\n'.join(head + parts[kind]))
    open(os.path.join(out, 'converted', '_preamble.h'), 'w', newline='\n', encoding='utf8').write(
        '// The release\'s lifted.cpp ahead of its functions: the helpers and the named fields (tools/lift/lift.py writes it).\n' + preamble + '\n')
    # (The stock versions' proof, kept so a later snapshot can use it again: the stock ROM doesn't change, and a proved conversion
    #  of a function stays proved. --stock-release <this folder>/stock_proof --stock-report <this folder>/stock_proof/report.txt)
    if args.stock_release:
        os.makedirs(os.path.join(out, 'stock_proof'))
        shutil.copy(os.path.join(args.stock_release, 'lifted.cpp'), os.path.join(out, 'stock_proof', 'lifted.cpp'))
        if args.stock_report:
            keep = set(stock_done)
            open(os.path.join(out, 'stock_proof', 'report.txt'), 'w', newline='\n').write(
                ''.join(l for l in open(args.stock_report) if l.split()[:1] and l.split()[0] in keep))
    for f in ('names.json', 'fields.json'):
        if os.path.exists(os.path.join(args.tools, f)):
            shutil.copy(os.path.join(args.tools, f), os.path.join(out, f))
    json.dump(rows, open(os.path.join(out, 'functions.json'), 'w', newline='\n'), indent=1)
    with open(os.path.join(out, 'functions.tsv'), 'w', newline='\n', encoding='utf8') as f:
        f.write('n64_name\tname\tarea\tinstructions\tsix_engine_changes_it\tstatus\tevidence\n')
        for r in rows:
            f.write('\t'.join(str(r[k]) for k in ('n64_name', 'name', 'area', 'instructions', 'six_engine_changes_it', 'status', 'evidence')) + '\n')

    # README
    conv = [r for r in rows if r['status'].startswith('converted')]
    stock_conv = [r for r in conv if 'stock version' in r['status']]
    engine = [r for r in rows if r['six_engine_changes_it']]
    rom = rom_identity(args.rom)
    lifted_path = os.path.join(args.release, 'lifted.cpp')
    changed_path = os.path.join(args.release, 'lifted_changed.inc')
    lines = ['# WWF No Mercy (N64): base-game preservation snapshot, release %s' % args.version, '',
             '**A private preservation copy, not for distribution.** It holds code derived from the original game (its functions turned into '
             'C, and their MIPS); it holds no ROM, graphics, sound or other game data. Keep it with the project, not in public.', '',
             'Written %s by tools/lift/preserve.py from release %s.' % (datetime.datetime.now().strftime('%Y-%m-%d %H:%M'), args.version), '',
             'Status of the project: %s (the release\'s counted figure, converted and random-tested, for the whole game with the mod\'s '
             'copies and the port\'s stubs as lift.py score counts them). Non-matching: the C++ is meant to behave as the original does, '
             'not to build the original ROM again byte for byte. This snapshot\'s own counts, the base game alone, are below.' % label, '',
             '## What it is', '',
             "The original game's own functions -- WWF No Mercy as the stock cartridge has it -- and nothing else: not the Ultimate No Mercy mod "
             "(its copies of functions and its own code are in the release's lifted_changed.inc, marked there as changed or new features, not here; a game function the mods only hook is here, as its code is the game's), not the PC port's code (a function whose "
             "conversion carries the port's additions is given here as its original, not converted), and not the six-man engine's rewrites (see below).", '',
             '- `converted/<area>.cpp`: each function converted to readable C++ (names and explanations from names.json, fields from fields.json) and '
             'random-tested against its original, with its original MIPS beside it. `_preamble.h` is the release\'s own helpers and named fields.',
             "- `not_converted/<area>.c`: each function not converted yet: its MIPS and N64Recomp's mechanical C (the instruction-by-instruction "
             'translation, each instruction\'s MIPS in a comment).',
             '- `functions.tsv` / `functions.json`: every function, its name, area, size, status and the evidence for it.',
             '- `names.json`, `fields.json`: the naming track\'s names and record layouts as of this release.',
             '- `MD5SUMS.txt`: every file\'s MD5; the zip (`base_game_%s.zip`) holds all of them; its own MD5 is in `base_game_%s.zip.md5`.' % (args.version, args.version), '',
             '## Provenance', '']
    if rom:
        lines += ['The original: WWF No Mercy (USA), Rev %d -- identified by its hashes only, never copied:' % rom['revision'], '',
                  '| | |', '|---|---|',
                  '| MD5 | `%s` |' % rom['md5'], '| SHA-1 | `%s` |' % rom['sha1'], '| CRC32 | `%s` |' % rom['crc32'],
                  '| size | %d bytes (big-endian .z64) |' % rom['size'], '| header | "%s", game code %s, revision byte %d |' % (rom['title'], rom['game_code'], rom['revision']), '']
    lines += ['- The stock translation: %s.' % shown_translation,
              '- The release\'s converted code: `%s` (MD5 `%s`)%s; the fuzz report: `%s`.' % (
                  os.path.relpath(lifted_path, ROOT).replace('\\', '/'), md5(lifted_path),
                  ' and `%s` (MD5 `%s`)' % (os.path.relpath(changed_path, ROOT).replace('\\', '/'), md5(changed_path)) if os.path.exists(changed_path) else '',
                  os.path.relpath(args.report or os.path.join(args.tools, 'fuzz', 'report.txt'), ROOT).replace('\\', '/')),
              "- The game's translation the release was made from (only to see which functions the six-man engine changes): `%s`. It must be "
              "the release's own: run this right after the release, before the engine is installed again." % os.path.relpath(args.game_translated, ROOT).replace('\\', '/'),
              '- Each converted function was random-tested against its original by tools/lift/fuzz: at least %d plain random tests compared, no '
              'difference, every instruction reached (or shown never to run by the reviewed dead-code checker, tools/lift/unreach.py and '
              'division_checks.py, under the approved rules); the release\'s normal build then passed the whole-game QA runs. That is strong '
              'evidence, not a mathematical proof. An in-play twin check (lift_twin.cpp) exists, but its results are not in these notes yet.' % BAR, '',
              '## The six-man engine and the stock functions', '',
              "The game's converted code comes from the six-man engine's ROM (six-engine/), which moves the game's per-wrestler arrays and rewrites "
              "some code, so %d of the base game's functions (%d instructions) have different instructions there than on the stock cartridge "
              "(mostly array addresses and loop bounds; some rewritten). For the base game those must be the stock versions:" % (
                  len(engine), sum(r['instructions'] for r in engine)), '',
              '- converted from the stock originals and random-tested on their own (status "the stock version"): %d functions, %d instructions;' % (
                  len(stock_conv), sum(r['instructions'] for r in stock_conv)),
              '- the rest are under "not_converted", their stock MIPS and mechanical C given. %s' % args.stock_note, '',
              '## Counts', '',
              '| | functions | instructions |', '|---|---:|---:|',
              '| the base game | %d | %d |' % (len(rows), sum(r['instructions'] for r in rows)),
              '| converted and random-tested | %d | %d |' % (len(conv), sum(r['instructions'] for r in conv)),
              '| not converted | %d | %d |' % (len(rows) - len(conv), sum(r['instructions'] for r in rows if r not in conv)), '',
              '| area | converted | not converted |', '|---|---:|---:|']
    for k, title, _ in AREAS:
        c = len([r for r in rows if r['area'] == k and r in conv])
        nc = len([r for r in rows if r['area'] == k and r not in conv])
        lines.append('| %s (`%s`) | %d | %d |' % (title, k, c, nc))
    lines += ['', 'Areas come from each function\'s name, then its explanation, then the functions it calls and that call it (functions.tsv '
              'says which); they are a reading aid, not a claim about the original source files.', '',
              '## Status of each function', '',
              'functions.tsv has every one. A converted function\'s first comment lines are the release\'s own, then its C++, then its MIPS. '
              'The C++ is the release\'s text: it uses the release\'s helpers (`_preamble.h`, lift_call.h) and is not a separate program.', '',
              '| status | functions | instructions |', '|---|---:|---:|']
    for status, group in sorted(collections.Counter(re.sub(r' \(\d.*', '', r['status']) for r in rows).items(), key=lambda x: -x[1]):
        these = [r for r in rows if re.sub(r' \(\d.*', '', r['status']) == status]
        lines.append('| %s | %d | %d |' % (status, len(these), sum(r['instructions'] for r in these)))
    left = collections.Counter('the six-man engine\'s own code (rw_...)' if n.startswith('rw_') else
                               "the mod's copies (modrep_..., in lifted_changed.inc)" if n.startswith('modrep_') else
                               'not a function of the stock translation' for n in done if n not in stock)
    lines += ['', 'Left out of the release\'s converted functions, as not the base game\'s:', '']
    lines += ['- %s: %d' % (why, k) for why, k in sorted(left.items())] or ['- none']
    lines += ['- with the PC port\'s additions in their conversion (given here as their originals): %d' % len([n for n in done if done[n]['port'] and n in stock])]
    lines += ['', 'A function the converted code holds inside a bigger one (a piece or a middle of it, which the translation lists apart) is '
              'listed here as not converted: its instructions are in the bigger function\'s C++ and MIPS.', '',
              'names.json MD5 `%s`, fields.json MD5 `%s`.' % tuple(md5(os.path.join(args.tools, f)) if os.path.exists(os.path.join(args.tools, f)) else '-'
                                                                 for f in ('names.json', 'fields.json')), '']
    hand = hand_left      # (strip_hand_edits: a hand edit its registry doesn't name, left in)
    if hand:
        lines += ['Note: the stock translation read has the PC port\'s hand edits in %d functions (%s): their mechanical C is not the game\'s '
                  'alone. Use a translation made without tools/readd_hand_edits.py.' % (len(hand), ', '.join(sorted(hand))), '']
    lines += ['## Made again', '',
              'After a release (tools/lift/night.ps1 has written src/game/lifted.cpp):', '',
              '    python tools/lift/preserve.py <version> [--stock-release <a stock proof> --stock-report <its report>]', '',
              "The stock translation is made again each time (about a minute) unless --stock-translated names one. The stock versions' "
              'test results in stock_proof/ can be passed to the next snapshot as it is: --stock-release base_game_%s/stock_proof '
              '--stock-report base_game_%s/stock_proof/report.txt.' % (args.version, args.version), '',
              'Its arguments for this one: ' + ' '.join(a for a in sys.argv[1:]), '']
    open(os.path.join(out, 'README.md'), 'w', newline='\n', encoding='utf8').write('\n'.join(lines))

    # MD5 list and zip
    files = sorted(os.path.relpath(os.path.join(d, f), out).replace('\\', '/') for d, _, fs in os.walk(out) for f in fs)
    open(os.path.join(out, 'MD5SUMS.txt'), 'w', newline='\n').write(''.join('%s  %s\n' % (md5(os.path.join(out, f)), f) for f in files))
    files.append('MD5SUMS.txt')
    if not args.no_zip:
        zpath = os.path.join(out, 'base_game_%s.zip' % args.version)
        with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for f in files:
                z.write(os.path.join(out, f), 'base_game_%s/%s' % (args.version, f))
        open(zpath + '.md5', 'w', newline='\n').write('%s  %s\n' % (md5(zpath), os.path.basename(zpath)))
    print('%s: %d functions (%d instructions), %d converted (%d of them stock versions random-tested on their own), %d not; %d files' % (
        out, len(rows), sum(r['instructions'] for r in rows), len(conv), len(stock_conv), len(rows) - len(conv), len(files)))


if __name__ == '__main__':
    main()
