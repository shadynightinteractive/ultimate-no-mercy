#!/usr/bin/env python3
"""The game functions the mods hook without replacing them, converted with the hook calls in (2026-11-12, scratch/agents/modhooks).

A mod's RECOMP_HOOK / RECOMP_HOOK_RETURN does not change the game's function: the loader translates the function again with a
call of the hook place's slot at its entry and/or at each of its returns (regenerate_with_hooks; Stage 1 links the same thing,
built ahead of time: tools/aot, src/game/mod_aot/mod_aot_hooked.c). So the function's own code converts like any other, and the
hook calls go into the converted function's outer wrapper, the extern "C" way in (lift.py release_text): the entry hook before
the converted body makes its frame, the return hook after the body has released it and the result is in the context -- the state
mod_aot_hooked.c has at its calls (its entry hook is the first statement; its return hook is after the delay slot, sp released,
ra and the saved registers back, v0 set). The wrapper, not m2c's body, because m2c's returns are not the MIPS jr $ra's.

What is compared (the user's approval of 2026-11-12, mod-functions-decisions: the "hook" stand-in). The original in the tester is
the function as the loader runs it: mod_aot_hooked.c's, which this checks is the translated original (RecompiledFuncs) line for
line with only its calls spelled LOOKUP_FUNC(address) and the hook calls added -- so the tester runs the translated original with
lift_run_hook lines exactly where mod_aot_hooked.c has nm_aot_run_hook. Both sides call the same stand-in at each hook place
(fuzz_main.cpp fuzz_hook_stand_in): it records the registers the hook functions there read, may change memory, changes no
register (a hook can't: run_hook puts the context back after it).

When the game may use the converted copy: only when the hooks waiting on the function are exactly the hook places compiled into
it (the loader: librecomp mods.cpp, lift_link_hooked_functions). Otherwise its slots stay empty and the loader's own copy (the
linked one or the live recompiler's) takes its entry, as before.
"""
import os, re, json, struct, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
# Stage 1's generated translations (tools/aot/aot.py writes them; build_exe.ps1 runs it). LIFT_AOT_DIR: another folder with the
# same files (a work copy's own aot.py run).
AOT = os.environ.get('LIFT_AOT_DIR') or os.path.join(PORT, 'src', 'game', 'mod_aot')
MODREP_HOOKS = os.path.join(HERE, 'modrep', 'out', 'modrep_hooks.json')     # (our mod's hook places: modrep.py)
# (what each of our mod's functions reads: analyse.py's analysis_mod.json, beside analysis.json -- one file for the mod's own
#  functions, shared with lift.py's copies and glue: final081 2026-11-12, in place of modhooks' modfn_use.json, the same reads)
MODFN_USE = os.path.join(HERE, 'analysis_mod.json')

_hooked = None


def place_name(name, at_return):
    """The name both sides use for a hook place: lift_hook_<address>_entry / _return. (A converted copy of the mod's,
    modrep_func_X, is named for the game function it replaces, func_X: 2026-11-12, kindd. No function is both: one a mod
    replaces is not one the mods only hook.)"""
    if name.startswith('modrep_'):
        name = name[len('modrep_'):]
    return 'lift_hook_%s_%s' % (name[len('func_'):], 'return' if at_return else 'entry')


def hooked():
    """name -> {'section': its section's ROM address, 'vram', 'entry': bool, 'return': bool, 'text': mod_aot_hooked.c's function},
    for each game function in Stage 1's table of hooked functions. {} when there is no table (a build without mod_aot)."""
    global _hooked
    if _hooked is not None:
        return _hooked
    _hooked = {}
    table = os.path.join(AOT, 'mod_aot_table.c')
    source = os.path.join(AOT, 'mod_aot_hooked.c')
    if not (os.path.exists(table) and os.path.exists(source)):
        return _hooked
    text = open(source, encoding='utf8', errors='replace').read()
    bodies = {}
    for m in re.finditer(r'^RECOMP_FUNC void (nm_aot_hooked_[0-9A-F]{8}_[0-9A-F]{8})\(uint8_t\* rdram, recomp_context\* ctx\)', text, re.M):
        bodies[m.group(1)] = text[m.start():text.index('\n;}', m.start()) + 3]
    for m in re.finditer(r'\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, (\d+), 0x[0-9A-F]{16}ull, (nm_aot_hooked_\w+), (-?\d+), (-?\d+)\},\s*// (func_\w+)',
                         open(table, encoding='utf8').read()):
        name = m.group(7)
        _hooked[name] = {'section': int(m.group(1), 16), 'vram': int(m.group(2), 16), 'entry': int(m.group(5)) >= 0,
                         'return': int(m.group(6)) >= 0, 'text': bodies.get(m.group(4))}
    return _hooked


CALL_LINE = re.compile(r'^(\s*)LOOKUP_FUNC\(0x([0-9A-F]{8})\)\(rdram, ctx\);$')
HOOK_LINE = re.compile(r'^(\s*)(\{ )?nm_aot_run_hook\(rdram, ctx, nm_aot_hook_slot\[(\d+)\]\);( return;)?$')


_port_table = None


def port_function_at(address):
    """(2026-11-14, rest100) The function the port's own table (RecompiledFuncs/recomp_overlays.inl) has at `address`, where that is
    sure to be what the loader's lookup finds there: only in a section no other section shares addresses with (the main code at
    0x80000450 and the port's own MIPS, rwblob, at 0x80780000 -- never an overlay's, which depend on what is loaded), and only
    where exactly one function starts there. None otherwise. The table names functions as the translated originals call them:
    the runtime's own versions of libultra's (osRecvMesg_recomp at 0x80036CD0) and the port's rw_* among them."""
    global _port_table
    if _port_table is None:
        text = open(os.path.join(PORT, 'RecompiledFuncs', 'recomp_overlays.inl'), encoding='utf8', errors='replace').read()
        funcs = {}
        for m in re.finditer(r'static FuncEntry (section_\d+_\w+_funcs)\[\] = \{(.*?)\n\};', text, re.S):
            funcs[m.group(1)] = re.findall(r'\.func = (\w+), \.offset = 0x([0-9A-F]+)', m.group(2))
        sections = [(int(a, 16), int(n, 16), f) for a, n, f in
                    re.findall(r'\.ram_addr = 0x([0-9A-F]+), \.size = 0x([0-9A-F]+), \.funcs = (\w+),', text)]
        _port_table = {}
        for i, (start, size, f) in enumerate(sections):
            shared = any(j != i and start < s2 + n2 and s2 < start + size for j, (s2, n2, _) in enumerate(sections))
            if shared:
                continue
            for fname, offset in funcs.get(f, []):
                _port_table.setdefault(start + int(offset, 16), []).append(fname)
    names = _port_table.get(address, [])
    return names[0] if len(names) == 1 else None


def with_hooks(name, translated, replaced=None, odd=()):
    """(the translated original with its hook calls, None), or (None, why not). The hook calls go exactly where mod_aot_hooked.c
    has them, as lift_run_hook(rdram, ctx, <place>); and every other line must be the translated original's, but for the calls,
    which mod_aot_hooked.c spells LOOKUP_FUNC(address) (the loader's lookup of the same function by its address): such a line must
    be the translated original's direct call of the function at that address."""
    h = hooked().get(name)
    if h is None or not h['text']:
        return None, "the mod hooks it, and Stage 1's table (src/game/mod_aot) has no hooked copy of it"
    if translated is None:
        return None, 'the mod hooks it, and it has no translated original'
    def same_call(line, theirs):
        # (The same call: the hooked copy looks the function up by its address, the translated original names it. That the
        #  name is the function at that address is the translator's own doing for both; the tester stands in either way.)
        call = CALL_LINE.match(line)
        if call and re.fullmatch(re.escape(call.group(1)) + r'func_%s(?:_ovl_\w+)?\(rdram, ctx\);' % call.group(2), theirs):
            return True
        # (2026-11-14, rest100) Or the function the port's table has at that address under another name -- the runtime's own
        #  osRecvMesg_recomp, the port's rw_dlbase_s2 -- which the translated original calls by that name: the lookup finds the
        #  same function, unless a mod replaces what is at that address (replaced: lift.py's REPLACED_BY_MOD; with none given,
        #  never). func_800012B0, func_80002BB4, func_80002DE4 were refused on exactly these lines.
        if not call or replaced is None or 'func_' + call.group(2) in replaced:
            return False
        port = port_function_at(int(call.group(2), 16))
        # (Not a port helper that hands a result back in a saved register (odd: lift.py's helpers_writing_saved_registers):
        #  func_80002BB4, through rw_dlbase_s2, converted so and came out DIFFERENT -- m2c read the s2 the helper handed back
        #  only where it was used, after the function had moved s2 on. Those stay refused, as before.)
        # (A mod's replacement is listed by the name the port's table gives -- lift.py mod_targets -- so that name too.)
        return port is not None and port not in odd and port not in replaced and theirs == call.group(1) + port + '(rdram, ctx);'
    return merge_hooks(name, h['text'], translated, same_call, h['entry'], h['return'],
                       "the mod hooks it, and its hooked copy is not the translated original with hook calls")


def merge_hooks(name, mine, translated, same_call, want_entry, want_return, not_same, slots=None):
    """The line-by-line walk of with_hooks and copy_with_hooks (2026-11-12, kindd: with_hooks' own walk, taken out as it was so the
    copies are checked the same way; for with_hooks it does what it did). mine: the loader's copy, with its nm_aot_run_hook lines;
    translated: the function the tester runs as the original; same_call(line, translated line): whether a line of the loader's
    copy that differs is the same call, spelled its way; not_same: the reason when a line is neither. slots: {False: the table's
    hook place of the function's entry, True: of its return}; each hook line must call its own (None: not checked, as with_hooks
    never did)."""
    mine = mine.split('\n')
    theirs = translated.split('\n')
    out = [theirs[0]]       # (the first line, the name: the translated original's own)
    i, j = 1, 1             # (the next line of the translated original, of mod_aot_hooked.c's copy)
    entries = returns = 0
    while j < len(mine):
        line = mine[j]
        hook = HOOK_LINE.match(line)
        if hook and slots is not None and (hook.group(2) is None) == (hook.group(4) is None) and \
                int(hook.group(3)) != slots.get(hook.group(4) is not None):
            return None, "the mod hooks it, and its copy calls a hook place other than its own"
        if hook and hook.group(4) is not None and hook.group(2) is not None:
            # A return hook: `{ nm_aot_run_hook(...); return;` and, on the next line, `}`, where the translated original has
            # `return;` -- the call put just before that return, the stack released, the result set.
            if theirs[i:i + 1] != [hook.group(1) + 'return;'] or mine[j + 1:j + 2] != [hook.group(1) + '}']:
                return None, "the mod hooks it, and a return hook in its hooked copy is not at one of the translated original's returns"
            out += [hook.group(1) + '{ lift_run_hook(rdram, ctx, %s); return;' % place_name(name, True), hook.group(1) + '}']
            i, j, returns = i + 1, j + 2, returns + 1
            continue
        if hook and hook.group(2) is None and hook.group(4) is None:
            # The entry hook: a line of its own, before the function's first instruction.
            out.append('%slift_run_hook(rdram, ctx, %s);' % (hook.group(1), place_name(name, False)))
            j, entries = j + 1, entries + 1
            continue
        if hook:
            return None, "the mod hooks it, and its hooked copy calls a hook in a way this does not read"
        if i >= len(theirs):
            return None, "the mod hooks it, and its hooked copy is longer than the translated original"
        if line == theirs[i] or same_call(line, theirs[i]):
            out.append(theirs[i])
        else:
            return None, "%s (line %d: %r)" % (not_same, i + 1, line[:80])
        i, j = i + 1, j + 1
    if i != len(theirs):
        return None, "the mod hooks it, and its hooked copy is shorter than the translated original"
    if entries != (1 if want_entry else 0) or (returns > 0) != bool(want_return):
        return None, "the mod hooks it, and its hooked copy's hook calls are not the ones its table says"
    return '\n'.join(out), None


def places_by_mod(nrms):
    """For each .nrm, the set of (game function's place: section ROM address, address; at_return) its hooks name."""
    out = []
    for nrm in nrms:
        with zipfile.ZipFile(nrm) as z:
            data = z.read('mod_syms.bin')
        (sections, dependencies, imports, dependency_events, replacements, exports, callbacks, events, hooks,
         strings) = struct.unpack_from('<10I', data, 12)
        at = 12 + 40 + strings
        for _ in range(sections):
            nf, nr = struct.unpack_from('<2I', data, at + 20)
            at += 28 + 8 * nf + 16 * nr
        at += 12 * dependencies + 12 * imports + 12 * dependency_events + 16 * replacements + 12 * exports + 8 * callbacks + 8 * events
        found = set()
        for i in range(hooks):
            _, section, vram, flags = struct.unpack_from('<4I', data, at + 16 * i)
            found.add((section, vram, bool(flags & 1)))     # (N64Recomp HookFlags::AtReturn = 1)
        out.append(found)
    return out


def reads(name, at_return, nrms):
    """The registers the hooks at this place read (analyse.py's reads of each hook function, its callees' included), by abi.py's
    names; sp always (a hook's frame is below it). A place another mod hooks too -- its hook functions are not analysed here --
    reads every register, which only makes the comparison stricter."""
    h = hooked()[name]
    return reads_at(name, h['section'], h['vram'], at_return, nrms)


def reads_at(name, section, vram, at_return, nrms):
    """reads(), for the game function `name` at (section, vram): one only hooked, or one a copy of the mod's replaces
    (copy_reads). (2026-11-12, kindd: reads' own body, given the place.)"""
    key = (section, vram, at_return)
    mods = places_by_mod(nrms)
    ours = json.load(open(MODREP_HOOKS))
    use = {n: e['use'] for n, e in json.load(open(MODFN_USE)).items()} if os.path.exists(MODFN_USE) else {}
    if any(key in m for m in mods[1:]) or not os.path.exists(MODFN_USE):
        return None
    regs = {'sp'}
    for e in ours:
        if e['hooks'] == name and bool(e['at_return']) == at_return:
            if e['function'] not in use:
                return None
            regs |= set(use[e['function']])
    return sorted(regs)


# ---- The mod's copies that something also hooks (kind D: scratch/agents/modnative PLAN.md; 2026-11-12, kindd) ----
#
# A game function the mod replaces with its copy AND something hooks: the loader does not translate the game's function again
# then; it puts the hook calls into the mod's copy itself as it translates the mod (load_mod_code: entry_func_hooks /
# return_func_hooks). Stage 1 links the same thing (mod_aot_nomercy_arcade_<n>.c, mod_aot_table.c's copy_hooks naming each
# call). modrep's translation of the copy (modrep_func_X: what lift converts, and what the tester runs as the original) has no
# hook calls. So here, as with_hooks does for a function only hooked: Stage 1's copy must be modrep's line for line, but for its
# hook calls and the way it spells three kinds of call, and the hook calls go into modrep's text exactly where Stage 1's are.

GAME_CALL = re.compile(r'^(\s*)nm_aot_nomercy_arcade_game_funcs\[(\d+)\]\(rdram, ctx\);   // (\w+)$')
MOD_CALL = re.compile(r'^(\s*)nm_aot_nomercy_arcade_([0-9A-F]{8})\(rdram, ctx\);$')
_copies = None


def copies():
    """func_X -> {'func': Stage 1's function for our mod's copy of it, 'mod_vram': the copy's address as linked, 'entry' /
    'return': the table's hook place its call there names (an index in the table's hooks; None: no call there), 'text': the copy
    as Stage 1 translated it}, for each copy with hook calls in Stage 1's table; and '' -> {'game_funcs': [(vram, name)] by the
    mod's game-function number, 'places': [(section, vram, at_return)] by hook place}. {} without the table."""
    global _copies
    if _copies is not None:
        return _copies
    _copies = {}
    path = os.path.join(AOT, 'mod_aot_table.c')
    if not os.path.exists(path):
        return _copies
    table = open(path, encoding='utf8').read()
    def block(start):
        at = table.index(start)
        return table[at:table.index('};', at)]
    functions = re.findall(r'^\s+(nm_aot_nomercy_arcade_[0-9A-F]{8}),', block('nm_aot_nomercy_arcade_functions['), re.M)
    vrams = [int(v, 16) for v in re.findall(r'^\s+0x([0-9A-F]{8})u,', block('nm_aot_nomercy_arcade_function_vrams['), re.M)]
    game = [(int(v, 16), n) for v, n in re.findall(r'\{0x[0-9A-F]{8}u, 0x([0-9A-F]{8})u\},\s*// (\w+)',
                                                   block('nm_aot_nomercy_arcade_game_func_targets['))]
    places = [(int(s, 16), int(v, 16), r == '1') for s, v, r in
              re.findall(r'\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, ([01])\},', block('static const NmAotHook hooks['))]
    if len(functions) != len(vrams):
        return _copies
    text = ''.join(open(os.path.join(AOT, f), encoding='utf8', errors='replace').read() for f in sorted(os.listdir(AOT))
                   if re.fullmatch(r'mod_aot_nomercy_arcade_\d+\.c', f))
    for index, at_return, hook, name in re.findall(r"\{(\d+), ([01]), (\d+)\},\s*// in the mod's copy of (func_\w+)",
                                                   block('nm_aot_nomercy_arcade_copy_hooks[')):
        fn = functions[int(index)]
        e = _copies.setdefault(name, {'func': fn, 'mod_vram': vrams[int(index)], 'entry': None, 'return': None, 'text': ''})
        if e['func'] != fn:
            e['text'] = None        # (two of the mod's functions say they are its copy: neither is taken)
        if e['text'] is None:
            continue
        e['return' if at_return == '1' else 'entry'] = int(hook)
        m = re.search(r'^RECOMP_FUNC void %s\(uint8_t\* rdram, recomp_context\* ctx\) \{' % fn, text, re.M)
        e['text'] = text[m.start():text.index('\n;}', m.start()) + 3] if m else None
    _copies[''] = {'game_funcs': game, 'places': places}
    return _copies


def copy_with_hooks(name, translated, records):
    """For a copy of the mod's that something also hooks (modrep_func_X): (modrep's translation of it with the hook calls, None),
    or (None, why not). records: modrep.index().

    Stage 1's copy must be modrep's line for line but for its hook calls (each calling the table's place for this function's
    entry, or for its return) and three spellings of the same call:
      - a game function: Stage 1 calls the loader's entry for it from its table (nm_aot_nomercy_arcade_game_funcs[N], the
        function's name in a comment), modrep names it: the same entry if the table's N-th function is at that name's address
        and the comment is that name (a name modrep.game_names gave _recomp after it is the same function, by its address);
      - a game function the mod replaces too: Stage 1 calls the mod's copy of it itself (nm_aot_nomercy_arcade_<address as
        linked>), modrep calls the game function's entry, which the loader points at that copy (or a converted one made from
        exactly it): the same if modrep's index says the copy at that address replaces that function;
      - one of the mod's own functions: the same address, nm_aot_nomercy_arcade_<address> or modfn_<address>.
    The tester stands in for a call either way, so a call is the same call if it reaches the same function."""
    rec = records.get(name)
    if rec is None:
        return None, 'the mod has no such copy now'
    c = copies().get(name[len('modrep_'):])
    if c is None or not c['text']:
        return None, "the mod also hooks the function it replaces, and Stage 1's table (src/game/mod_aot) has no copy of it with the hook calls"
    if translated is None:
        return None, 'the mod also hooks the function it replaces, and it has no translated copy'
    common = copies()['']
    if c['mod_vram'] != rec['mod_vram']:
        return None, "the mod also hooks the function it replaces, and Stage 1's copy of it is another of the mod's functions"
    for at in ('entry', 'return'):
        if rec['hooked_' + at] and c[at] is None:
            # (modrep reads our mod's hooks only, Stage 1's table every loaded mod's: a call only the table has is another mod's
            #  hook, which the walk below takes as any; one modrep has and the table hasn't: the two are of different builds.)
            return None, "the mod also hooks the function it replaces, and Stage 1's copy of it has not got the mod's hook"
        if c[at] is not None and common['places'][c[at]] != (rec['section_vrom'], rec['vram'], at == 'return'):
            return None, "the mod also hooks the function it replaces, and Stage 1's table gives its copy another function's hook"
    by_mod_vram = {r['mod_vram']: r['replaces'] for r in records.values()}
    def same_call(line, theirs):
        m = GAME_CALL.match(line)
        if m:
            n = int(m.group(2))
            if n >= len(common['game_funcs']):
                return False
            vram, comment = common['game_funcs'][n]
            target = re.fullmatch(re.escape(m.group(1)) + r'((?:func_([0-9A-F]{8})(?:_ovl_\w+)?)|\w+)\(rdram, ctx\);', theirs)
            if not target or comment != m.group(3) or target.group(1) not in (comment, comment + '_recomp'):
                return False
            return target.group(2) is None or int(target.group(2), 16) == vram
        m = MOD_CALL.match(line)
        if m:
            if theirs == '%smodfn_%s(rdram, ctx);' % (m.group(1), m.group(2)):
                return True
            at = int(m.group(2), 16)
            return at in by_mod_vram and theirs == '%s%s(rdram, ctx);' % (m.group(1), by_mod_vram[at])
        return False
    return merge_hooks(name, c['text'], translated, same_call, c['entry'] is not None, c['return'] is not None,
                       "the mod also hooks the function it replaces, and Stage 1's copy of it is not modrep's with hook calls",
                       slots={False: c['entry'], True: c['return']})


def copy_reads(name, at_return, records, nrms):
    """reads(), for a copy of the mod's (modrep_func_X): the registers the hooks at func_X's entry or return read."""
    rec = records[name]
    return reads_at(name[len('modrep_'):], rec['section_vrom'], rec['vram'], at_return, nrms)
