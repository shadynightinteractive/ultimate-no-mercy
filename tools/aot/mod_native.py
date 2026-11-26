#!/usr/bin/env python3
"""The native mod: Ultimate No Mercy's own functions proved native, linked into the game in place of Stage 1's translation of them
(the release step of the native mod, as `lift.py release` is the game functions'. modnative7, 2026-11-14, from modnative5/6's
step E prototype, scratch/agents/modnative6/stepE_glue.py).

    python tools/aot/mod_native.py release     src/game/mod_native.cpp and src/game/mod_native.json from the proved candidates:
                                               tools/lift/candidates.json, each modrep_nat_<A> whose line in the tester's report
                                               meets the bar (lift.py fuzz_line_meets_bar), and src/game/nm_native.h, the header
                                               they were proved with (tools/lift/fuzz/nm_native.h)
    python tools/aot/mod_native.py check       the shipped mod_native.cpp is exactly what `release` makes from the proved texts,
                                               each proved text is the one the tester's report line was made from (its hash in
                                               mod_native.json), and nm_native.h is the proof's. Exit 1 if not. (As check-names.)
    python tools/aot/mod_native.py apply       (tools/aot/aot.py runs it after each translation) if src/game/mod_aot is the mod
                                               build the native functions were proved against (its hash = mod_native.json's), each
                                               native function's Stage 1 definition is made <name>_translated under
                                               ARCADE_MOD_NATIVE, and src/game/mod_aot/mod_native_on.txt says CMake may link
                                               mod_native.cpp; otherwise neither, and it says the native mod is off for that build.
    Options: --port <exe source folder> --lift <tools/lift folder> --report <report.txt> --mod <mod folder>
             --also <report> (any number): a function DIFFERENT in any of these runs is not proved
             --both <report> (any number): a function must also meet the bar in each of these (the second seed's run)
             --cands <candidates.json>: the proved tier's candidates (default: <lift>/candidates.json)
             --whole <candidates.json> (2026-11-14, modwhole; the user's "B now, C later"): release also makes the GAMEPLAY TIER,
                     src/game/mod_native_whole.cpp -- every other candidate there that compiles and links in the game, NOT
                     proved, listed in mod_native.json's "gameplay" (check verifies it as it verifies the proved tier; apply
                     renames their Stage 1 definitions only under ARCADE_MOD_NATIVE_WHOLE, which CMake sets when it links the
                     file). The two tiers are reported apart, never mixed: a gameplay function counts only once the gameplay
                     A/B runs verify it (tools/modnative/whole_ab.py).
             --verdicts <report> (any number): runs whose lines are written beside each gameplay function
             --whole-only <file>: the gameplay tier only the functions named there (the gameplay-verified list: one no scenario
                     has run, or one the A/B found changing a scenario, stays translated)

How a native function goes in. Stage 1 (tools/aot) translates the whole mod into C (src/game/mod_aot), one function per mod
function, nm_aot_nomercy_arcade_<A>; the translated code calls its own functions directly by those names, and the loader
(librecomp aot_mod.h) uses the table of them only for the exact .nrm it was made from (its hash). A native function takes that
name -- so every caller, translated or the loader's table, reaches it -- and Stage 1's own definition becomes
nm_aot_nomercy_arcade_<A>_translated, still linked: the native-off switches fall back to it.
  - The native code is the candidate's C++ exactly as the tester proved it (tools/lift/candidates.json), with the test's bindings
    made the game's: lift_mod_address(section, offset) where the loader put the mod; a call to a game function through the
    loader's table (nm_aot_..._game_funcs[k], so a function a mod patches or hooks is the patched / hooked one, as Stage 1 calls
    it); an import through nm_aot_..._imports[k]; another of the mod's functions by its Stage 1 name (native or translated,
    whichever is linked under it); a private scalar at its N64 place (a word as the game's memory holds it, a half at address ^ 2,
    a byte at ^ 3 -- where the translated functions keep it, so both share it).
  - Its way in is lift.py's own adapter (registers -> parameters -> v0), the one the tester ran.
  - Not shipped (said in mod_native.json, "refused"): a function with private data of more than one field (an array or a
    structure: the proof kept it as a PC object with its own layout; the game would need a view of its N64 place).
Switches (the user's, 2026-11-14): the build's ARCADE_MOD_NATIVE (CMake, on) links none of it when off; at run time
NM_NATIVE_OFF=all (every native function falls back to Stage 1's translation) or NM_NATIVE_OFF=<file>,<file>,... (only those
mod source files' functions do: draw, logic, media, roster, ... as in mod/src). The log says "[native] ..." once per function
that runs native, and once at start how many are linked and which files are off.
"""
import hashlib, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TAG = 'nomercy_arcade'
LABEL = '// New feature (Ultimate No Mercy)'


def args():
    a = {'port': None, 'lift': None, 'report': None, 'mod': None, 'aot': None}
    argv = sys.argv[2:]
    for k in list(a):
        if '--' + k in argv:
            a[k] = argv[argv.index('--' + k) + 1]
    root = os.path.abspath(os.path.join(HERE, '..', '..'))
    a['port'] = a['port'] or os.path.join(root, 'exe', 'WWFNoMercyRecomp')
    a['lift'] = a['lift'] or os.path.join(root, 'tools', 'lift')
    a['report'] = a['report'] or os.path.join(a['lift'], 'fuzz', 'report.txt')
    a['mod'] = a['mod'] or os.path.join(root, 'mod')
    a['aot'] = a['aot'] or os.path.join(a['port'], 'src', 'game', 'mod_aot')
    a['game'] = os.path.join(a['port'], 'src', 'game')
    # (--also <report>, any number: other runs of the same candidates -- a function DIFFERENT in any of them is not proved, for
    #  a difference found by any compared test is a difference)
    a['also'] = [argv[k + 1] for k in range(len(argv) - 1) if argv[k] == '--also']
    # (--both <report>, any number: runs of the same candidates under another seed (FUZZ_SEED) -- a function must meet the bar in
    #  each of them too: the native step proves under two seeds as standard, 2026-11-14)
    a['both'] = [argv[k + 1] for k in range(len(argv) - 1) if argv[k] == '--both']
    # (2026-11-14, modwhole: the user's "B now, C later") --whole [<candidates.json>]: also every other candidate that compiles in the
    #  game -- the GAMEPLAY tier, src/game/mod_native_whole.cpp, kept apart from the proved one (default: tools/lift/candidates.json);
    #  --verdicts <report> (any number): runs whose lines are written beside each gameplay function (its random test's verdict)
    a['whole'] = None
    if '--whole' in argv:
        k = argv.index('--whole')
        a['whole'] = argv[k + 1] if k + 1 < len(argv) and not argv[k + 1].startswith('--') else os.path.join(a['lift'], 'candidates.json')
    a['verdicts'] = [argv[k + 1] for k in range(len(argv) - 1) if argv[k] == '--verdicts']
    # (--cands <candidates.json>: the proved tier's candidates from another file than tools/lift's -- the proof run's own, when the
    #  gameplay tier's come from another: modwhole, 2026-11-14)
    a['cands'] = argv[argv.index('--cands') + 1] if '--cands' in argv else os.path.join(a['lift'], 'candidates.json')
    # (--whole-only <file>: the gameplay tier only these -- names modrep_nat_<A> or addresses, e.g. whole_ab.py's gameplay-verified
    #  list: the user's rule (3), a function no scenario has run stays translated)
    a['whole_only'] = None
    if '--whole-only' in argv:
        a['whole_only'] = {x[len('modrep_nat_'):] if x.startswith('modrep_nat_') else x for x in open(argv[argv.index('--whole-only') + 1]).read().split()}
    return a


def table_hash(aot):
    t = open(os.path.join(aot, 'mod_aot_report.txt')).read()
    return re.search(r'^%s: hash ([0-9A-F]{16});' % TAG, t, re.M).group(1)


def aot_indexes(aot):
    """name -> ('game_funcs' | 'imports', k): every call Stage 1's translation makes through the loader's tables."""
    out = {}
    for f in sorted(os.listdir(aot)):
        if f.startswith('mod_aot_%s_' % TAG) and f.endswith('.c'):
            for kind, k, name in re.findall(r'nm_aot_%s_(game_funcs|imports)\[(\d+)\]\(rdram, ctx\);\s*// (\w+)' % TAG,
                                            open(os.path.join(aot, f)).read()):
                out[name] = (kind, int(k))
    return out


def proved(line, full_tests):
    """A report line at the full bar: no difference, every instruction reached (the never-run ones proved so by unreach.py are
    in the mask), at least full_tests plain random tests. (Not lift.py's fuzz_line_meets_bar: that is the cache's test, which
    keeps DIFFERENT lines too.)"""
    f = line.split()
    if len(f) != 8:
        return False
    try:
        diffs, mask, n, plain = int(f[3]), int(f[4], 16), int(f[5]), int(f[7])
    except ValueError:
        return False
    return diffs == 0 and bin(mask).count('1') >= n and plain >= full_tests


def proved_names(a, cands):
    """The modrep_nat_<A> whose report line meets the bar (no difference, every instruction reached, enough plain tests)."""
    sys.path.insert(0, a['lift'])
    import lift
    out = {}
    for line in open(a['report']):
        f = line.split()
        if f and re.fullmatch(r'modrep_nat_[0-9A-F]{8}', f[0]) and f[0] in cands and proved(line, lift.FULL_TESTS):
            out[f[0]] = line.strip()
    for other in a.get('both', []):
        met = set()
        for line in open(other):
            f = line.split()
            if f and f[0] in out and proved(line, lift.FULL_TESTS):
                met.add(f[0])
        for n in [n for n in out if n not in met]:
            del out[n]          # (not proved under the other seed)
    for other in a.get('also', []):
        for line in open(other):
            f = line.split()
            if len(f) > 3 and f[0] in out and f[3] != '0':
                del out[f[0]]       # (DIFFERENT in another run)
    return out


def source_comment(mod, rel, name):
    """The comment lines right above the function's definition in the mod's C (mod/src/<file>.c), without their // -- the mod
    author's own description of it -- or [] if it has none."""
    rel = rel[len('mod/'):] if rel.startswith('mod/') else rel      # (candidates name it from the project: mod/src/<file>.c)
    path = os.path.join(mod, rel.replace('/', os.sep)) if not os.path.isabs(rel) else rel
    if not os.path.exists(path):
        return []
    lines = open(path, encoding='utf8', errors='replace').read().split('\n')
    for k, l in enumerate(lines):
        if re.match(r'^(?:static\s+)?(?:inline\s+)?[\w\s\*]+?\b%s\s*\([^;]*$' % re.escape(name), l) and not l.lstrip().startswith(('//', '*', 'return')):
            out = []
            j = k - 1
            while j >= 0 and lines[j].lstrip().startswith('//'):
                out.insert(0, lines[j].strip()[2:].strip())
                j -= 1
            return out
    return []


def file_of(v):
    """The mod source file a native function is from: 'variants' for mod/src/variants.c."""
    return os.path.splitext(os.path.basename(v['native']['file']))[0]


def braces(line):
    """(2026-11-14, modwhole) '{' less '}' on a line of C++, not counting those in string or character literals or comments."""
    s = re.sub(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'', '', line)
    s = re.sub(r'/\*.*?\*/', '', s)
    s = s.split('//', 1)[0]
    return s.count('{') - s.count('}')


SCALARS = {'u8', 's8', 'u16', 's16', 'u32', 's32', 'u64', 's64', 'f32', 'f64', 'char', 'short', 'int', 'long', 'float', 'double',
           'bool', '_Bool', 'unsigned', 'signed', 'unsigned char', 'signed char', 'unsigned short', 'unsigned int', 'unsigned long',
           'long long', 'unsigned long long'}


def bind(a_, v, rows):
    """The candidate's proved C++ with its test bindings made the game's (see the module's text), and its first-call bindings
    (nm_bind's lines) -- or (None, why) when it can't be shipped."""
    cpp = v['cpp']
    for p in v.get('private', []):
        r = rows[p]
        if len(r['widths'] or []) != 1 or int(r['address'], 16) % r['size']:
            return None, 'private data of more than one field (%s: an array or a structure) -- a view of its N64 place, not yet' % p
    binds = []
    priv = {re.sub(r'\W', '_', p): rows[p] for p in v.get('private', [])}

    def off_of(c):
        return int(priv[c]['address'], 16) - 0x81000000
    out, depth = [], None
    for line in cpp.split('\n'):
        if depth is None:
            out.append(line)
            if line.strip() == 'namespace nm_%s {' % a_:
                depth = 1
            continue
        # (2026-11-14, modwhole: at namespace level -- where the match is on the line, not where the line starts: a function's
        #  body may begin on its first line, `static void host_send(void) { static s32& a = ...`)
        at_top = lambda m: depth + braces(m.string[:m.start()]) == 1
        if re.match(r'\s*extern "C" u8 lift_mod_private_\w+\[\];\s*$', line):
            depth += braces(line)
            continue

        def plain(m):
            t, x, c = m.group(1).strip(), m.group(2), m.group(3)
            if at_top(m):
                binds.append('%s.a = lift_mod_address(0, 0x%X);' % (x, off_of(c)))
                return 'static NVar<%s> %s(0); /* (private: its N64 place, bound by nm_bind) */' % (t, x)
            return 'static NVar<%s> %s(lift_mod_address(0, 0x%X)); /* (private: its N64 place) */' % (t, x, off_of(c))
        line = re.sub(r'static ([^&;=()]+?)\s*& (\w+) = \*reinterpret_cast<[^;]*?\*>\(lift_mod_private_(\w+)\); static_assert\([^;]*\);', plain, line)

        def shrunk(m):
            if at_top(m):
                binds.append('%s.byte = nm_private_at(0x%X, 1);' % (m.group(2), off_of(m.group(3))))
                return 'static %s %s(nullptr);' % (m.group(1), m.group(2))
            return 'static %s %s(nm_private_at(0x%X, 1));' % (m.group(1), m.group(2), off_of(m.group(3)))
        line = re.sub(r'static (NShrunk<[^>]*>) (\w+)\(lift_mod_private_(\w+)\);', shrunk, line)

        def split(m):
            els = [e.strip() for e in m.group(3).split(',')]
            inits = []
            top = at_top(m)
            for i, e in enumerate(els):
                if e.startswith('lift_mod_private_'):
                    r = priv[e[len('lift_mod_private_'):]]
                    at = 'nm_private_at(0x%X, %d)' % (int(r['address'], 16) - 0x81000000, r['size'])
                    if top:
                        binds.append('%s.e[%d] = %s;' % (m.group(2), i, at))
                    inits.append(at)
                else:
                    inits.append(e)
            if top:
                return 'static %s %s = {};' % (m.group(1), m.group(2))
            return 'static %s %s = {{%s}};' % (m.group(1), m.group(2), ', '.join(inits))
        line = re.sub(r'static (NSplit<[^>]*>) (\w+) = \{\{([^}]*)\}\};', split, line)
        out.append(line)
        depth += braces(line)
    cpp = '\n'.join(out)
    if 'lift_mod_private_' in cpp.split('namespace nm_%s {' % a_, 1)[1]:
        return None, 'a private binding this release step does not know'
    # (the views at file scope of its namespace are made at start-up, before the loader has put the mod in place: bound again
    #  on the first call)
    depth = None
    for line in cpp.split('\n'):
        if depth is None:
            if line.strip() == 'namespace nm_%s {' % a_:
                depth = 1
            continue
        m = re.match(r'static (N(?:Var|Arr))<(.*)> (\w+)\(lift_mod_address\(0, (0x[0-9A-F]+)\)', line.strip())
        if m and depth == 1:
            ty = m.group(2).replace('const ', '').strip()
            if m.group(1) == 'NVar' and ty not in SCALARS and not ty.startswith('NPtr') and '[' not in ty:
                # (a structure's view: its fields are views made with it -- made again in place, at the mod's place)
                # (__builtin_addressof: a view's own & gives its N64 pointer, as C's & does)
                binds.append('::new ((void*)__builtin_addressof(%s)) NVar<%s>(lift_mod_address(0, %s));' % (m.group(3), m.group(2), m.group(4)))
            else:
                binds.append('%s.a = lift_mod_address(0, %s);' % (m.group(3), m.group(4)))
        # (2026-11-15, modwhole) A PC table of N64 pointers at namespace level, its elements the mod's places -- `static NPtr<char>
        #  np_caption[3] = { NPtr<char>(lift_mod_address(0, 0x9284B)), ... }` (native.py string_views in a table's initializer) --
        #  is filled at start-up too, before the loader has put the mod in place: its elements set again on the first call. (The
        #  A/B's first crash on the final build: number_page_frame handed plat_game_text a caption at the mod's offset alone.)
        t = re.match(r'static (?:const )?NPtr<[^>]*> (\w+)\[\w*\] = \{(.*)\};\s*$', line.strip())
        if t and depth == 1 and 'lift_mod_address(' in t.group(2):
            els, cur, d = [], '', 0
            for ch in t.group(2):
                if ch == ',' and d == 0:
                    els.append(cur.strip())
                    cur = ''
                    continue
                d += (ch in '([{') - (ch in ')]}')
                cur += ch
            els.append(cur.strip())
            binds += ['%s[%d] = %s;' % (t.group(1), i, e) for i, e in enumerate(els) if 'lift_mod_address(' in e]
        depth += braces(line)
    return (cpp, binds), None


def build_text(a):
    """(mod_native.cpp's text, the manifest) from the proved candidates. Deterministic: check() makes it again and compares."""
    cands = json.load(open(a['cands']))
    rows = {r['name']: r for r in json.load(open(os.path.join(a['lift'], 'private_data.json')))}
    copies = {e['name']: e['mod_vram'] for e in json.load(open(os.path.join(a['lift'], 'modrep', 'out', 'modrep_index.json')))}
    proved = proved_names(a, cands)
    idx = aot_indexes(a['aot'])
    sys.path.insert(0, a['lift'])
    import lift
    chosen, refused = [], {}
    for n in sorted(proved):
        A = n[len('modrep_nat_'):]
        res, why = bind(A, cands[n], rows)
        if res is None:
            refused[A] = why
        else:
            chosen.append((A, res))
    files = sorted({file_of(cands['modrep_nat_' + A]) for A, _ in chosen})
    forwarders, blocks, entries, final = {}, [], [], {}
    for A, (cpp, binds) in chosen:
        v = cands['modrep_nat_' + A]
        for c in v.get('callees', []):
            if c.startswith('modfn_') or c in copies:
                at = c[len('modfn_'):] if c.startswith('modfn_') else '%08X' % copies[c]
                target = 'nm_aot_%s_%s(rdram, ctx);' % (TAG, at)
                decl = 'extern "C" void nm_aot_%s_%s(uint8_t*, recomp_context*);' % (TAG, at)
            else:
                base = re.sub(r'_recomp$', '', c)
                if base not in idx and c not in idx:
                    raise SystemExit('%s: %s is not in Stage 1\'s tables' % (A, c))
                kind, k = idx.get(base) or idx[c]
                target = 'nm_aot_%s_%s[%d](rdram, ctx);' % (TAG, kind, k)
                decl = ''
            forwarders[c] = (decl, 'extern "C" void nm_native_call_%s(uint8_t* rdram, recomp_context* ctx) { %s }' % (c, target))
            cpp = re.sub(r'::%s\b' % re.escape(c), '::nm_native_call_%s' % c, cpp)
        name, rel = v['native']['name'], v['native']['file']
        about = source_comment(a['mod'], rel, name)
        head = ['', '// ' + '-' * 125,
                '%s: %s (%s), the mod\'s function at 0x%s.' % (LABEL, name, rel, A)]
        head += ['//   ' + l for l in about]
        head += ['// Native C++ in place of Stage 1\'s translation of it, exactly as the random tester proved it against that translation',
                 '// (%s random tests, no difference, every instruction reached), with the game\'s bindings (tools/aot/mod_native.py).' %
                 '{:,}'.format(int(proved['modrep_nat_' + A].split()[7]) if len(proved['modrep_nat_' + A].split()) > 7 else 0)]
        blocks.append('\n'.join(head) + '\n' + cpp + '\nnamespace nm_%s { void nm_bind() { %s } }' % (A, ' '.join(binds)))
        fk = files.index(file_of(v))
        entries.append('%s: %s -- Stage 1\'s name, so every caller reaches the native code; NM_NATIVE_OFF falls back to the translation\n'
                       'extern "C" void nm_aot_%s_%s_translated(uint8_t* rdram, recomp_context* ctx);\n'
                       'extern "C" void nm_aot_%s_%s(uint8_t* rdram, recomp_context* ctx) {\n'
                       '    if (!nm_native_on(%d)) { nm_aot_%s_%s_translated(rdram, ctx); return; }   // (%s.c)\n'
                       '    lift_rdram = rdram;\n'
                       '    static const bool bound = (game::nm_%s::nm_bind(), nm_native_note("%s", "%s"), true);   // (the mod is in place by its first call)\n'
                       '    (void)bound;\n    const u32 nm_frame_was = nm_frame_base;   // (a native caller\'s frame: put back for its views made after this call)\n    %s\n    nm_frame_base = nm_frame_was;\n}' % (LABEL, name, TAG, A, TAG, A, fk, TAG, A, files[fk], A, name, A,
                                                     lift.adapter_body('modrep_nat_' + A, v)))
        final[A] = {'name': name, 'file': rel, 'proved_sha1': hashlib.sha1(v['cpp'].encode()).hexdigest()[:16],
                    'report': proved['modrep_nat_' + A]}
    head = ['// mod_native.cpp: Ultimate No Mercy\'s own functions in native C++ -- each one proved by the random tester against Stage 1\'s',
            '// translation of the mod build it ships with, and linked in place of that translation. WRITTEN BY tools/aot/mod_native.py',
            '// release from tools/lift/candidates.json and the tester\'s report; `mod_native.py check` proves this file is what that',
            '// makes. Do not edit it: the mod\'s C (mod/src) is the source, and every change there is proved again before it ships.',
            '// %d functions from %d of the mod\'s files; mod_native.json lists them, the mod build (its hash) and any left out.' % (len(final), len(files)),
            '#ifndef NM_NATIVE_BUILT_WITH_FWRAPV',
            '#error "mod_native.cpp must be compiled with -fwrapv -fno-strict-aliasing -ffp-contract=off -fsigned-char (and -DNM_NATIVE_BUILT_WITH_FWRAPV): the tester\'s semantics"',
            '#endif',
            '#include <cstdint>', '#include <cstdio>', '#include <cstdlib>', '#include <cstring>', '#include "recomp.h"', '#include "lift_mem.h"',
            'extern "C" int32_t nm_aot_%s_section_addresses[];' % TAG,
            'extern "C" recomp_func_t* nm_aot_%s_game_funcs[];' % TAG,
            'extern "C" recomp_func_t* nm_aot_%s_imports[];' % TAG,
            '// (where the loader put the mod: lift_mod_address in the proved text)',
            'inline u32 lift_mod_address(int section, u32 offset) { return (u32)nm_aot_%s_section_addresses[section] + offset; }' % TAG,
            "// (one of the mod's private scalars at its N64 place: a word as the game's memory holds it, a half at address ^ 2, a",
            "//  byte at address ^ 3 -- the translated code's own places for them)",
            'inline u8* nm_private_at(u32 offset, u32 width) {',
            '    const u32 a = (u32)nm_aot_%s_section_addresses[0] + offset;' % TAG,
            '    return lift_rdram + ((width == 1 ? a ^ 3 : width == 2 ? a ^ 2 : a) - 0x80000000u);',
            '}',
            '#include "nm_native.h"',
            "// (game only: a private pointer at its N64 place -- the proof's PC object was an NPtr itself -- moved as C moves a pointer,",
            "//  in elements: the same as the NPtr's own += / -= / ++ / -- on the word that holds it)",
            'template <class U> struct NVar<NPtr<U>> : NRef<NPtr<U>> {',
            '    explicit NVar(u32 address) : NRef<NPtr<U>>(address) {}',
            '    using NRef<NPtr<U>>::operator=;',
            '    const NVar& operator+=(s32 n) const { lift_write32(this->a, lift_read32(this->a) + (u32)n * NSize<U>::value); return *this; }',
            '    const NVar& operator-=(s32 n) const { lift_write32(this->a, lift_read32(this->a) - (u32)n * NSize<U>::value); return *this; }',
            '    const NVar& operator++() const { return *this += 1; }',
            '    const NVar& operator--() const { return *this -= 1; }',
            '    NPtr<U> operator++(int) const { NPtr<U> o = *this; *this += 1; return o; }',
            '    NPtr<U> operator--(int) const { NPtr<U> o = *this; *this -= 1; return o; }',
            '};',
            '// The mod source files with native functions, in the order nm_native_on numbers them.',
            'static const char* const nm_native_files[] = {%s};' % ', '.join('"%s"' % f for f in files),
            '// The native-off switches (the user\'s, 2026-11-14): NM_NATIVE_OFF=all, or a list of those files (any separator): their',
            '// functions run Stage 1\'s translation instead. Read once, at the first call of any native function.',
            'static bool nm_native_on(int file) {',
            '    static const unsigned long long off = [] {',
            '        unsigned long long bits = 0;',
            '        const char* e = std::getenv("NM_NATIVE_OFF");',
            '        if (e != nullptr) {',
            '            for (int k = 0; k < (int)(sizeof(nm_native_files) / sizeof(nm_native_files[0])); k++) {',
            '                const size_t n = std::strlen(nm_native_files[k]);',
            '                for (const char* p = std::strstr(e, nm_native_files[k]); p != nullptr; p = std::strstr(p + 1, nm_native_files[k]))',
            '                    if ((p == e || !(std::isalnum((unsigned char)p[-1]) || p[-1] == \'_\')) && !(std::isalnum((unsigned char)p[n]) || p[n] == \'_\')) bits |= 1ull << k;',
            '            }',
            '            if (std::strcmp(e, "all") == 0) bits = ~0ull;',
            '        }',
            '        std::fprintf(stderr, "[native] %d of the mod\'s functions linked native (mod build %s); off: %%s\\n", e != nullptr && *e ? e : "none");' % (len(final), table_hash(a['aot'])),
            '        return bits;',
            '    }();',
            '    return !((off >> file) & 1);',
            '}',
            '// (said once per function, the first time it runs native: the QA runs count them)',
            'static void nm_native_note(const char* name, const char* address) { std::fprintf(stderr, "[native] %s (%s) in use\\n", name, address); }']
    head.insert(head.index('#include <cstring>') + 1, '#include <cctype>')
    if any('::new (' in ' '.join(b) for _, (_, b) in chosen):
        head.insert(head.index('#include <cctype>') + 1, '#include <new>')   # (a structure's view made again in place: bind)
    for c, (decl, line) in sorted(forwarders.items()):
        if decl:
            head.append(decl)
        head.append(line)
    text = ('\n'.join(head) + '\nnamespace game {\n' + '\n'.join(blocks) + '\n}  // namespace game\n\n' + '\n\n'.join(entries) + '\n')
    manifest = {'nrm_hash': table_hash(a['aot']), 'files': files, 'functions': final, 'refused': refused,
                'header_sha1': hashlib.sha1(open(os.path.join(a['lift'], 'fuzz', 'nm_native.h'), 'rb').read()).hexdigest()[:16],
                'text_sha1': hashlib.sha1(text.encode()).hexdigest()[:16]}
    return text, manifest


# ---- The gameplay tier (2026-11-14, modwhole; the user's decision "B now, C later") ----
# Every other candidate of the mod that compiles natively -- those the random tester has not proved (DIFFERENT, short, partly
# reached, or made only for this tier) -- goes in src/game/mod_native_whole.cpp, apart from the proved mod_native.cpp, and is
# always reported apart from it: mod_native.json's "gameplay" (each with its random test's lines, if it had any). Such a function
# counts as GAMEPLAY-VERIFIED only by the user's rule: it compiles under the same flags, the QA runs log it running native, and
# every recorded scenario gives the same results native and translated (tools/modnative/whole_ab.py). Linked only with CMake's
# ARCADE_MOD_NATIVE_WHOLE (off by default) and only for the mod build it was made from (the hash, as the proved tier).
# Switches at run time, besides NM_NATIVE_OFF=all / <files>: NM_NATIVE_OFF=gameplay (this tier off, the proved one on) and
# NM_NATIVE_OFF=<function name> (that one function off: to find which one changes a scenario).
WHOLE_LABEL = '// New feature (Ultimate No Mercy), gameplay tier'


def clangxx():
    """The C++ compiler the game is built with (the project's toolchain; NM_CLANGXX overrides)."""
    return os.environ.get('NM_CLANGXX') or os.path.join(HERE, '..', '..', 'toolchain', 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-clang++.exe')


def forwarder(c, idx, copies, prefix):
    """(its declaration or '', the forwarder's definition) for a native function's call c -- to another of the mod's functions or
    of its copies by Stage 1's name, else through the loader's tables -- or None if Stage 1 has no such call. (As build_text's.)"""
    if c.startswith('modfn_') or c in copies:
        at = c[len('modfn_'):] if c.startswith('modfn_') else '%08X' % copies[c]
        target = 'nm_aot_%s_%s(rdram, ctx);' % (TAG, at)
        decl = 'extern "C" void nm_aot_%s_%s(uint8_t*, recomp_context*);' % (TAG, at)
    else:
        base = re.sub(r'_recomp$', '', c)
        if base not in idx and c not in idx:
            m = re.fullmatch(r'func_([0-9A-F]{8})(?:_recomp)?', c)
            if not m:
                return None
            # (a game function no translated function of the mod calls: by its address, through the runtime's table)
            return '', 'extern "C" void %s%s(uint8_t* rdram, recomp_context* ctx) { LOOKUP_FUNC(0x%sU)(rdram, ctx); }' % (prefix, c, m.group(1))
        kind, k = idx.get(base) or idx[c]
        target = 'nm_aot_%s_%s[%d](rdram, ctx);' % (TAG, kind, k)
        decl = ''
    return decl, 'extern "C" void %s%s(uint8_t* rdram, recomp_context* ctx) { %s }' % (prefix, c, target)


def verdict_lines(a):
    """{candidate: ['<report>: <its line>', ...]} from the --report, --both, --also and --verdicts runs, in that order."""
    out = {}
    for r in [a['report']] + a.get('both', []) + a.get('also', []) + a.get('verdicts', []):
        if r and os.path.exists(r):
            for line in open(r):
                f = line.split()
                if f and f[0].startswith('modrep_nat_'):
                    out.setdefault(f[0], []).append('%s: %s' % (os.path.basename(r), line.strip()))
    return out


def verdict_comment(s):
    """A report line, short: its counts and its first difference (not the reached mask)."""
    where, line = s.split(': ', 1)
    f = line.split()
    if len(f) != 8:
        return '// Random test (%s): %s' % (where, line[:120])
    return '// Random test (%s): %s tests, %s compared, %s differences, first %s, %s plain' % (where, f[1], f[2], f[3], f[6], f[7])


def build_whole_text(a, proved, skip):
    """(mod_native_whole.cpp's text, {A: entry}, {A: why left out}, {A: [(first line, last line)]}) from the --whole candidates
    that are not in the proved tier. skip: {A: why} left out already (the release's compile check: kept in the manifest, so
    check makes the same text). Deterministic, as build_text."""
    cands = json.load(open(a['whole']))
    rows = {r['name']: r for r in json.load(open(os.path.join(a['lift'], 'private_data.json')))}
    copies = {e['name']: e['mod_vram'] for e in json.load(open(os.path.join(a['lift'], 'modrep', 'out', 'modrep_index.json')))}
    idx = aot_indexes(a['aot'])
    lines = verdict_lines(a)
    sys.path.insert(0, a['lift'])
    import lift
    proved_A = {n[len('modrep_nat_'):] for n in proved}
    chosen, refused = [], dict(skip)
    for n in sorted(k for k in cands if re.fullmatch(r'modrep_nat_[0-9A-F]{8}', k)):
        A = n[len('modrep_nat_'):]
        if A in proved_A or A in skip or (a.get('whole_only') is not None and A not in a['whole_only']):
            continue
        res, why = bind(A, cands[n], rows)
        if res is None:
            refused[A] = why
            continue
        missing = [c for c in cands[n].get('callees', []) if forwarder(c, idx, copies, '') is None]
        if missing:
            refused[A] = "a call Stage 1's tables do not have (%s)" % ', '.join(missing)
            continue
        chosen.append((A, res))
    files = sorted({file_of(cands['modrep_nat_' + A]) for A, _ in chosen})
    forwarders, blocks, entries, final = {}, [], [], {}
    for A, (cpp, binds) in chosen:
        v = cands['modrep_nat_' + A]
        for c in v.get('callees', []):
            forwarders[c] = forwarder(c, idx, copies, 'nm_whole_call_')
            cpp = re.sub(r'::%s\b' % re.escape(c), '::nm_whole_call_%s' % c, cpp)
        name, rel = v['native']['name'], v['native']['file']
        seen = lines.get('modrep_nat_' + A, [])
        head = ['', '// ' + '-' * 125, '%s: %s (%s), the mod\'s function at 0x%s.' % (WHOLE_LABEL, name, rel, A)]
        head += ['//   ' + l for l in source_comment(a['mod'], rel, name)]
        head += ['// Native C++ in place of Stage 1\'s translation of it, compiled from the mod\'s C as the tester would test it, with the',
                 '// game\'s bindings (tools/aot/mod_native.py). NOT proved by the random tester: it counts only once the gameplay A/B verifies it.']
        head += [verdict_comment(s) for s in seen] or ['// Random test: none (%s)' % (v['native'].get('whole_only') or 'not run')]
        blocks.append((A, '\n'.join(head) + '\n' + cpp + '\nnamespace nm_%s { void nm_bind() { %s } }' % (A, ' '.join(binds))))
        fk = files.index(file_of(v))
        entries.append((A, '%s: %s\n'
                        'extern "C" void nm_aot_%s_%s_translated(uint8_t* rdram, recomp_context* ctx);\n'
                        'extern "C" void nm_aot_%s_%s(uint8_t* rdram, recomp_context* ctx) {\n'
                        '    static const bool on = nm_whole_on(%d, "%s");   // (%s.c)\n'
                        '    if (!on) { nm_aot_%s_%s_translated(rdram, ctx); return; }\n'
                        '    lift_rdram = rdram;\n'
                        '    static const bool bound = (game::nm_%s::nm_bind(), nm_whole_note("%s", "%s"), true);   // (the mod is in place by its first call)\n'
                        '    (void)bound;\n    const u32 nm_frame_was = nm_frame_base;   // (a native caller\'s frame: put back for its views made after this call)\n    %s\n    nm_frame_base = nm_frame_was;\n}' % (WHOLE_LABEL, name, TAG, A, TAG, A, fk, name, files[fk], TAG, A, A, name, A,
                                                      lift.adapter_body('modrep_nat_' + A, v))))
        final[A] = {'name': name, 'file': rel, 'text_sha1': hashlib.sha1(v['cpp'].encode()).hexdigest()[:16], 'size': v.get('size', 0),
                    'random_test': seen, 'tier': 'gameplay'}
        if v['native'].get('whole_only'):
            final[A]['whole_only'] = v['native']['whole_only']
    head = ['// mod_native_whole.cpp: the GAMEPLAY TIER of Ultimate No Mercy\'s native functions -- each compiled natively from the mod\'s C',
            '// like the proved ones in mod_native.cpp, but NOT proved by the random tester. WRITTEN BY tools/aot/mod_native.py release --whole;',
            '// `mod_native.py check --whole` proves this file is what that makes. Linked only with ARCADE_MOD_NATIVE_WHOLE (CMake, off by',
            '// default) and for the mod build in mod_native.json. A function here counts as gameplay-verified only when the A/B runs (native',
            '// and translated, every recorded scenario) give the same results and log it running native: tools/modnative/whole_ab.py.',
            '// %d functions from %d of the mod\'s files; mod_native.json\'s "gameplay" lists them, "gameplay_refused" those left out.' % (len(final), len(files)),
            '#ifndef NM_NATIVE_BUILT_WITH_FWRAPV',
            '#error "mod_native_whole.cpp must be compiled with -fwrapv -fno-strict-aliasing -ffp-contract=off -fsigned-char (and -DNM_NATIVE_BUILT_WITH_FWRAPV)"',
            '#endif',
            '#include <cstdint>', '#include <cstdio>', '#include <cstdlib>', '#include <cstring>', '#include <cctype>', '#include <new>', '#include "recomp.h"', '#include "lift_mem.h"',
            'extern "C" int32_t nm_aot_%s_section_addresses[];' % TAG,
            'extern "C" recomp_func_t* nm_aot_%s_game_funcs[];' % TAG,
            'extern "C" recomp_func_t* nm_aot_%s_imports[];' % TAG,
            "// (mod_native.cpp's helpers, word for word: inline, so the two files share them)",
            'inline u32 lift_mod_address(int section, u32 offset) { return (u32)nm_aot_%s_section_addresses[section] + offset; }' % TAG,
            'inline u8* nm_private_at(u32 offset, u32 width) {',
            '    const u32 a = (u32)nm_aot_%s_section_addresses[0] + offset;' % TAG,
            '    return lift_rdram + ((width == 1 ? a ^ 3 : width == 2 ? a ^ 2 : a) - 0x80000000u);',
            '}',
            '#include "nm_native.h"',
            'template <class U> struct NVar<NPtr<U>> : NRef<NPtr<U>> {',
            '    explicit NVar(u32 address) : NRef<NPtr<U>>(address) {}',
            '    using NRef<NPtr<U>>::operator=;',
            '    const NVar& operator+=(s32 n) const { lift_write32(this->a, lift_read32(this->a) + (u32)n * NSize<U>::value); return *this; }',
            '    const NVar& operator-=(s32 n) const { lift_write32(this->a, lift_read32(this->a) - (u32)n * NSize<U>::value); return *this; }',
            '    const NVar& operator++() const { return *this += 1; }',
            '    const NVar& operator--() const { return *this -= 1; }',
            '    NPtr<U> operator++(int) const { NPtr<U> o = *this; *this += 1; return o; }',
            '    NPtr<U> operator--(int) const { NPtr<U> o = *this; *this -= 1; return o; }',
            '};',
            '// The mod source files with gameplay-tier functions, in the order nm_whole_on numbers them.',
            'static const char* const nm_whole_files[] = {%s};' % ', '.join('"%s"' % f for f in files),
            '// (a name in NM_NATIVE_OFF as a word of its own, not part of a longer name)',
            'static bool nm_whole_named(const char* e, const char* w) {',
            '    const size_t n = std::strlen(w);',
            '    for (const char* p = std::strstr(e, w); p != nullptr; p = std::strstr(p + 1, w))',
            "        if ((p == e || !(std::isalnum((unsigned char)p[-1]) || p[-1] == '_')) && !(std::isalnum((unsigned char)p[n]) || p[n] == '_')) return true;",
            '    return false;',
            '}',
            '// The native-off switches: NM_NATIVE_OFF=all, =gameplay (this whole tier), a list of the mod\'s files, or of functions\' names.',
            '// Read once per function, at its first call.',
            'static bool nm_whole_on(int file, const char* name) {',
            '    static const char* const e = [] {',
            '        const char* v = std::getenv("NM_NATIVE_OFF");',
            '        std::fprintf(stderr, "[native] %d of the mod\'s functions linked native, GAMEPLAY TIER (not proved; mod build %s); off: %%s\\n", v != nullptr && *v ? v : "none");' % (len(final), table_hash(a['aot'])),
            '        return v != nullptr ? v : "";',
            '    }();',
            '    return !(std::strcmp(e, "all") == 0 || nm_whole_named(e, "gameplay") || nm_whole_named(e, nm_whole_files[file]) || nm_whole_named(e, name));',
            '}',
            '// (said once per function, the first time it runs native: the A/B runs count them; "gameplay tier" tells them apart)',
            'static void nm_whole_note(const char* name, const char* address) { std::fprintf(stderr, "[native] %s (%s) in use (gameplay tier)\\n", name, address); }']
    for c, (decl, line) in sorted(forwarders.items()):
        if decl:
            head.append(decl)
        head.append(line)
    # (each function's lines, for the compile check: its block and its entry)
    parts = [('\n'.join(head) + '\nnamespace game {\n', None)] + [(b + '\n', A) for A, b in blocks] + [('}  // namespace game\n\n', None)]
    parts += [(e + ('\n\n' if k + 1 < len(entries) else '\n'), A) for k, (A, e) in enumerate(entries)]
    text, at, ranges = '', 1, {}
    for s, A in parts:
        if A:
            ranges.setdefault(A, []).append((at, at + s.count('\n')))
        text += s
        at += s.count('\n')
    return text, final, refused, ranges


def whole_compile(a, text):
    """The compiler's errors for mod_native_whole.cpp's text, as the game's build compiles it (syntax and types): [(line, message)]
    (line 0: not in the file)."""
    import shutil, subprocess, tempfile
    port = a['port']
    d = tempfile.mkdtemp(prefix='nm_whole_')
    try:
        path = os.path.join(d, 'mod_native_whole.cpp')
        open(path, 'w', newline='\n', encoding='utf8').write(text)
        shutil.copyfile(os.path.join(a['lift'], 'fuzz', 'nm_native.h'), os.path.join(d, 'nm_native.h'))
        cmd = [clangxx(), '-std=c++20', '-fsyntax-only', '-ferror-limit=0', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-fsigned-char',
               '-DNM_NATIVE_BUILT_WITH_FWRAPV', '-Wno-everything', '-fno-caret-diagnostics', '-I', d,
               '-I', os.path.join(port, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), '-I', os.path.join(port, 'include'),
               '-I', os.path.join(port, 'src', 'game'), path]
        p = subprocess.run(cmd, capture_output=True, text=True)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    errs = []
    for l in p.stderr.splitlines():
        m = re.match(r'^(.*?):(\d+):\d+: (?:fatal )?error: (.*)$', l)
        if m:
            errs.append((int(m.group(2)) if os.path.basename(m.group(1)) == 'mod_native_whole.cpp' else 0, m.group(3)))
    if p.returncode and not errs:
        errs.append((0, (p.stderr.strip().splitlines() or ['the compiler failed'])[-1]))
    return errs


def whole_undefined(a, text):
    """{A: the functions its native text uses but nothing defines}: the file compiled to an object as the game compiles it, and its
    undefined symbols (llvm-nm) of the native functions' own namespaces -- the linker would refuse them."""
    import shutil, subprocess, tempfile
    port = a['port']
    d = tempfile.mkdtemp(prefix='nm_whole_')
    try:
        path = os.path.join(d, 'mod_native_whole.cpp')
        open(path, 'w', newline='\n', encoding='utf8').write(text)
        shutil.copyfile(os.path.join(a['lift'], 'fuzz', 'nm_native.h'), os.path.join(d, 'nm_native.h'))
        obj = os.path.join(d, 'whole.o')
        p = subprocess.run([clangxx(), '-std=c++20', '-c', '-O0', '-fwrapv', '-fno-strict-aliasing', '-ffp-contract=off', '-fsigned-char',
                            '-DNM_NATIVE_BUILT_WITH_FWRAPV', '-Wno-everything', '-I', d,
                            '-I', os.path.join(port, 'lib', 'N64ModernRuntime', 'N64Recomp', 'include'), '-I', os.path.join(port, 'include'),
                            '-I', os.path.join(port, 'src', 'game'), path, '-o', obj], capture_output=True, text=True)
        if p.returncode:
            raise SystemExit('mod_native --whole: the object compile failed: %s' % p.stderr[-500:])
        nm = os.environ.get('NM_LLVM_NM') or os.path.join(os.path.dirname(clangxx()), 'llvm-nm.exe')
        q = subprocess.run([nm, '-C', '--undefined-only', obj], capture_output=True, text=True)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    out = {}
    for line in q.stdout.splitlines():
        m = re.search(r'game::nm_([0-9A-F]{8})::(\w+)', line)
        if m:
            out.setdefault(m.group(1), []).append(m.group(2))
    return out


def whole_release(a, proved):
    """The gameplay tier, compiled as the game compiles it; each function with an error is left out (said in the manifest) and the
    rest made again, until it compiles."""
    skip = {}
    for _ in range(12):
        text, final, refused, ranges = build_whole_text(a, proved, skip)
        errs = whole_compile(a, text)
        if not errs:
            undefined = whole_undefined(a, text)
            if not undefined:
                return text, final, refused, skip
            for A, names in sorted(undefined.items()):
                skip[A] = 'does not link in the game: it calls functions its text declares but never defines (%s)' % ', '.join(sorted(set(names))[:6])
            print('mod_native --whole: %d left out: calls nothing defines (%s)' % (len(undefined), ', '.join(sorted(undefined)[:6])))
            continue
        bad = {}
        for line, msg in errs:
            owner = next((A for A, rs in ranges.items() if any(f <= line < l for f, l in rs)), None)
            if owner is None:
                raise SystemExit('mod_native --whole: an error outside any function (line %d): %s' % (line, msg))
            bad.setdefault(owner, 'does not compile in the game: ' + msg[:200])
        skip.update(bad)
        print('mod_native --whole: %d left out for compile errors (%s)' % (len(bad), ', '.join(sorted(bad)[:6])))
    raise SystemExit('mod_native --whole: still not compiling after 12 passes')


def whole_manifest(a, text, final, refused, skip):
    """mod_native.json's gameplay-tier part."""
    return {'gameplay': final, 'gameplay_refused': refused, 'gameplay_compile_refused': skip,
            'gameplay_only': sorted(a['whole_only']) if a.get('whole_only') is not None else None,
            'gameplay_candidates': os.path.relpath(os.path.abspath(a['whole']), os.path.abspath(os.path.join(HERE, '..', '..'))).replace(os.sep, '/'),
            'whole_text_sha1': hashlib.sha1(text.encode()).hexdigest()[:16]}


def release(a):
    text, manifest = build_text(a)
    open(os.path.join(a['game'], 'mod_native.cpp'), 'w', newline='\n', encoding='utf8').write(text)
    json.dump(manifest, open(os.path.join(a['game'], 'mod_native.json'), 'w', newline='\n'), indent=1, sort_keys=True)
    open(os.path.join(a['game'], 'nm_native.h'), 'wb').write(open(os.path.join(a['lift'], 'fuzz', 'nm_native.h'), 'rb').read())
    print('mod_native: %d native functions from %d files, %d left out; mod build %s -> %s' % (
        len(manifest['functions']), len(manifest['files']), len(manifest['refused']), manifest['nrm_hash'], a['game']))
    whole_path = os.path.join(a['game'], 'mod_native_whole.cpp')
    if a['whole']:
        cands = json.load(open(a['cands']))
        wtext, wfinal, wrefused, skip = whole_release(a, proved_names(a, cands))
        open(whole_path, 'w', newline='\n', encoding='utf8').write(wtext)
        manifest.update(whole_manifest(a, wtext, wfinal, wrefused, skip))
        json.dump(manifest, open(os.path.join(a['game'], 'mod_native.json'), 'w', newline='\n'), indent=1, sort_keys=True)
        print('mod_native --whole: %d gameplay-tier functions (%d instructions, NOT proved) from %d files, %d left out -> %s' % (
            len(wfinal), sum(f['size'] for f in wfinal.values()), len({f['file'] for f in wfinal.values()}), len(wrefused), whole_path))
    elif os.path.exists(whole_path):
        os.remove(whole_path)       # (a release without the gameplay tier: none left from an earlier one)


def check(a):
    text, manifest = build_text(a)
    bad = []
    shipped = open(os.path.join(a['game'], 'mod_native.cpp'), encoding='utf8').read()
    if shipped != text:
        bad.append('mod_native.cpp is not what the proved candidates make (%d bytes shipped, %d made)' % (len(shipped), len(text)))
    old = json.load(open(os.path.join(a['game'], 'mod_native.json')))
    for A, f in old['functions'].items():
        now = manifest['functions'].get(A)
        if now is None:
            bad.append('%s (%s): shipped, but not proved now' % (A, f['name']))
        elif now['proved_sha1'] != f['proved_sha1']:
            bad.append('%s (%s): shipped text %s, proved text %s' % (A, f['name'], f['proved_sha1'], now['proved_sha1']))
    if open(os.path.join(a['game'], 'nm_native.h'), 'rb').read() != open(os.path.join(a['lift'], 'fuzz', 'nm_native.h'), 'rb').read():
        bad.append('src/game/nm_native.h is not the header the functions were proved with (tools/lift/fuzz/nm_native.h)')
    # (2026-11-14, modwhole) The gameplay tier, when this release has one: mod_native_whole.cpp is what the --whole candidates make
    #  (with the same functions left out for compile errors as the release found), each function's text is the one listed, and no
    #  function is in both tiers. A tier the build doesn't link (no "gameplay" in the manifest) is not looked at.
    if 'gameplay' in old:
        if not a['whole']:
            a['whole'] = os.path.join(os.path.abspath(os.path.join(HERE, '..', '..')), old['gameplay_candidates'].replace('/', os.sep))
        if a.get('whole_only') is None and old.get('gameplay_only') is not None:
            a['whole_only'] = set(old['gameplay_only'])     # (the release's --whole-only, as the manifest has it)
        cands = json.load(open(a['cands']))
        wtext, wfinal, _, ranges = build_whole_text(a, proved_names(a, cands), old.get('gameplay_compile_refused', {}))
        wpath = os.path.join(a['game'], 'mod_native_whole.cpp')
        wshipped = open(wpath, encoding='utf8').read() if os.path.exists(wpath) else ''
        if wshipped != wtext:
            bad.append('mod_native_whole.cpp is not what the --whole candidates make (%d bytes shipped, %d made)' % (len(wshipped), len(wtext)))
        for A, f in old['gameplay'].items():
            now = wfinal.get(A)
            if now is None:
                bad.append('%s (%s): in the gameplay tier shipped, not made now' % (A, f['name']))
            elif now['text_sha1'] != f['text_sha1']:
                bad.append('%s (%s): gameplay text %s shipped, %s made' % (A, f['name'], f['text_sha1'], now['text_sha1']))
        both = sorted(set(old['gameplay']) & set(manifest['functions']))
        if both:
            bad.append('in both tiers: %s' % ', '.join(both))
        print('mod_native check --whole: the gameplay tier, %d functions, %d bytes (not proved: counted apart)' % (len(old['gameplay']), len(wshipped)))
    for b in bad:
        print('DIFFERENT ' + b)
    print('mod_native check: %s (%d functions, %d bytes)' % ('DIFFERENT' if bad else 'IDENTICAL', len(old['functions']), len(shipped)))
    return 1 if bad else 0


def apply(a):
    """After Stage 1's translation (aot.py): the native functions take their Stage 1 names, if this is the build they were proved
    against. Writes only src/game/mod_aot (made fresh by each translation)."""
    man_path = os.path.join(a['game'], 'mod_native.json')
    marker = os.path.join(a['aot'], 'mod_native_on.txt')
    for m in (marker, os.path.join(a['aot'], 'mod_native_whole_on.txt')):
        if os.path.exists(m):
            os.remove(m)
    if not os.path.exists(man_path) or not os.path.exists(os.path.join(a['game'], 'mod_native.cpp')):
        print('mod_native: no native functions released (src/game/mod_native.json): Stage 1 only')
        return 0
    man = json.load(open(man_path))
    now = table_hash(a['aot'])
    if man['nrm_hash'] != now:
        print('mod_native: OFF for this build -- the mod (%s) is not the build its native functions were proved against (%s): '
              'Stage 1 only, until the native mod is proved again (night.ps1)' % (now, man['nrm_hash']))
        return 0
    want = set(man['functions'])
    # (2026-11-14, modwhole: the gameplay tier, when released and its file is there -- its functions take their names only in a
    #  build that links mod_native_whole.cpp, ARCADE_MOD_NATIVE_WHOLE)
    whole = set(man.get('gameplay', {})) if os.path.exists(os.path.join(a['game'], 'mod_native_whole.cpp')) else set()
    want |= whole
    done = set()
    for f in sorted(os.listdir(a['aot'])):
        if not re.match(r'mod_aot_%s_\d+\.c$' % TAG, f):
            continue
        path = os.path.join(a['aot'], f)
        text = open(path).read()
        changed = False
        for A in sorted(want - done):
            head = 'RECOMP_FUNC void nm_aot_%s_%s(uint8_t* rdram, recomp_context* ctx) {' % (TAG, A)
            if text.count(head) == 1:
                # (the native function takes this name when the build links it: CMake's ARCADE_MOD_NATIVE)
                when = 'defined(ARCADE_MOD_NATIVE) && defined(ARCADE_MOD_NATIVE_WHOLE)' if A in whole else 'defined(ARCADE_MOD_NATIVE)'
                text = text.replace(head, '#if %s\nRECOMP_FUNC void nm_aot_%s_%s_translated(uint8_t* rdram, recomp_context* ctx) {\n'
                                          '#else\n%s\n#endif' % (when, TAG, A, head)) if A in whole else text.replace(
                    head, '#ifdef ARCADE_MOD_NATIVE\nRECOMP_FUNC void nm_aot_%s_%s_translated(uint8_t* rdram, recomp_context* ctx) {\n'
                          '#else\n%s\n#endif' % (TAG, A, head))
                done.add(A)
                changed = True
        if changed:
            open(path, 'w', newline='\n').write(text)
    if done != want:
        raise SystemExit('mod_native: Stage 1 has no single definition of %s' % sorted(want - done))
    open(marker, 'w', newline='\n').write('on %s %d\n' % (now, len(done - whole)))
    wmarker = os.path.join(a['aot'], 'mod_native_whole_on.txt')
    if whole:
        open(wmarker, 'w', newline='\n').write('gameplay tier %s %d\n' % (now, len(whole)))
    print('mod_native: %d native functions take their Stage 1 names (mod build %s)%s' % (
        len(done - whole), now, '; and %d of the gameplay tier, in a build with ARCADE_MOD_NATIVE_WHOLE' % len(whole) if whole else ''))
    return 0


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd not in ('release', 'check', 'apply'):
        raise SystemExit(__doc__)
    a = args()
    sys.exit({'release': release, 'check': check, 'apply': apply}[cmd](a) or 0)
