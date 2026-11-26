"""Which parameter registers each runtime (system) function reads, from its source in the port's runtime.

The translated game calls libultra through functions like osRecvMesg_recomp(rdram, ctx), written in C++ in N64ModernRuntime (and a few
in the port's src). Their bodies are the whole truth of what they read: ctx->r4..r7 (a0-a3), _arg<N, T>(rdram, ctx) (parameter N:
a0-a3 for N < 4, the stack after), ctx->f12 / ctx->f14. A body that hands ctx to anything else, or reads a register some other way,
is counted as unknown and keeps the old assumption (all of a0-a3, f12, f14)."""
import glob, json, os, re, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PORT = os.path.join(ROOT, 'exe', 'WWFNoMercyRecomp')
ARGS = ['a0', 'a1', 'a2', 'a3']
# (Either way the runtime writes it: `uint8_t* rdram, recomp_context* ctx`, or `RDRAM_ARG recomp_context* ctx`, RDRAM_ARG being
#  `uint8_t *rdram,` -- osEPiStartDma_recomp is the second, and was missed until 2026-11-07.)
DEF = re.compile(r'(?:extern "C" )?void (\w+)\((?:uint8_t\s*\*\s*rdram,|RDRAM_ARG)\s*recomp_context\s*\*\s*ctx\)\s*\{')

def bodies():
    files = (glob.glob(os.path.join(PORT, 'lib', 'N64ModernRuntime', '**', '*.cpp'), recursive=True) +
             glob.glob(os.path.join(PORT, 'lib', 'N64ModernRuntime', '**', '*.h'), recursive=True) +
             glob.glob(os.path.join(PORT, 'src', '**', '*.cpp'), recursive=True))
    # (Not the converted code lift.py writes, src/game/lifted.cpp and mod_converted.cpp: 2026-11-13, final082. Since 0.81's
    #  release it has a forwarding stub for each port export a converted copy of the mod's calls -- `void
    #  recomp_arcade_bones(uint8_t* rdram, recomp_context* ctx) { ... lift_import("recomp_arcade_bones"); f(rdram, ctx); }` --
    #  so the export had two definitions, table() could no longer read it through its registered function, and the next
    #  candidates run refused the 9 copies that call one ("calls a system function": modrep_func_80012BE0 and 8 more) that 0.81
    #  converted. Nothing a system function is lives there: lift.py's own output is never a source of what the port reads.)
    # (and lifted_changed.inc, 2026-11-16, honesty: the release's second file, included by lifted.cpp -- the mod's copies and the
    #  forwarders above are there now. The globs above don't take .inc files; named here so they never do.)
    generated = {os.path.normcase(os.path.join(PORT, 'src', 'game', n)) for n in ('lifted.cpp', 'lifted_changed.inc', 'mod_converted.cpp')}
    files = [p for p in files if os.path.normcase(os.path.abspath(p)) not in generated]
    found = {}
    for path in files:
        t = open(path, encoding='utf8', errors='replace').read()
        for m in DEF.finditer(t):
            i, d = m.end(), 1
            while d:
                d += {'{': 1, '}': -1}.get(t[i], 0)
                i += 1
            found.setdefault(m.group(1), []).append((path, t[m.end():i - 1]))
    return found

def reads(body):
    """The registers the body reads, or None if it can't be told."""
    b = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    b = re.sub(r'"(?:\\.|[^"\\])*"', '""', b)
    out = set()
    for m in re.finditer(r'\b_arg<\s*(\d+)\s*,', b):
        n = int(m.group(1))
        if n >= 4:
            return None          # (a stack parameter: reads sp and memory; not modelled here)
        out.add(ARGS[n])
    # Every other use of ctx: a field read, a field written, or ctx handed on.
    for m in re.finditer(r'\bctx\b', b):
        rest = b[m.end():m.end() + 40]
        before = b[max(0, m.start() - 40):m.start()]
        if re.match(r'\s*\)\s*', rest) and re.search(r'_(?:arg<[^>]*>|return<[^>]*>)\s*\(\s*rdram\s*,\s*$', before):
            continue             # (_arg / _return: counted above; _return only writes v0 / f0)
        if re.search(r'\b_return<[^>]*>\s*\(\s*$', before):
            continue             # (_return<T>(ctx, value))
        f = re.match(r'\s*->\s*(\w+)(?:\.\w+)?\s*(=(?!=)|[-+*/|&^]=)?', rest)
        if not f:
            return None          # (ctx handed to something else)
        field, assign = f.group(1), f.group(2)
        if assign == '=' and field in ('r2', 'r3', 'f0'):
            continue             # (writing the result)
        m2 = re.fullmatch(r'r(\d+)', field)
        if m2 and 4 <= int(m2.group(1)) <= 7 and not assign:
            out.add(ARGS[int(m2.group(1)) - 4])
            continue
        if field in ('f12', 'f14') and not assign:
            out.add(field)
            continue
        return None
    return out


def exports():
    """The port's functions the mod calls by an export name (the mod's imports, recomp_arcade_*): export name -> the C++
    functions registered under it, recomp::overlays::register_base_export("recomp_arcade_bones", bones_run) (2026-11-08)."""
    out = {}
    for path in glob.glob(os.path.join(PORT, 'src', '**', '*.cpp'), recursive=True):
        t = open(path, encoding='utf8', errors='replace').read()
        for m in re.finditer(r'register_base_export\(\s*"(\w+)"\s*,\s*(\w+)\s*\)', t):
            out.setdefault(m.group(1), set()).add(m.group(2))
    return out


# Read by hand, where the body can't be read the way above (2026-11-08, scratch/lift_modrep):
#   recomp_arcade_drive (drive_run, src/main/arcade_bones.cpp): takes _arg<0> (a0, a pointer to its arguments in memory),
#     then hands ctx on only to call game functions with ctx->r4 / ctx->r5 it sets itself (func_80012A8C, func_80013EB0,
#     func_80013860, func_80012B24, func_80002268, which read a0, a1 at most).
BY_HAND = {'recomp_arcade_drive': {'a0'}}


def table(names):
    """{name: set of registers read} for each of names whose reads can be told: exactly one definition, and a body read() follows.
    The rest are left out, and keep the old assumption.

    A name that is an export the mod imports (only the mod's copies call those, tools/lift/modrep.py) is read through the one
    function registered under it; in such a body `(void)ctx;` reads nothing (cont_ask). (The game's own system functions are
    read exactly as before.)"""
    found = bodies()
    aliases = None
    out = {}
    for n in names:
        defs = found.get(n, [])
        body = defs[0][1] if len(defs) == 1 else None
        if not defs:
            aliases = exports() if aliases is None else aliases
            if len(aliases.get(n, ())) == 1:
                defs = found.get(next(iter(aliases[n])), [])
                body = re.sub(r'\(void\)\s*ctx\s*;', '', defs[0][1]) if len(defs) == 1 else None
        r = reads(body) if body is not None else None
        if r is None:
            r = BY_HAND.get(n)
        if r is not None:
            out[n] = r
    return out


# ---- What a converted function needs to call a system function, and the tester to stand in for it (2026-11-07, scratch/lift_sys) ----
#
# Until 2026-11-07 lift.py refused every function that calls one ("calls a system function": 149 functions, 27,367 instructions, 77
# of them only for osVirtualToPhysical): the analysis has no entry for a system function, so nothing said which registers to pass it,
# what it hands back, or how the random-input tester could stand in for it. model() says all three, for each system function whose
# source has been read by hand (MEMORY below); every other one stays refused.
#
# A converted function calls the same thing the translated original calls -- osRecvMesg_recomp(rdram, ctx), the runtime's own
# version -- with the same registers set (lift_call), so in the game the two are the same call. What the tester has to prove is that
# the converted caller makes the same calls, with the same parameters and memory, in the same order, and uses what comes back the
# same way. Its stand-in for a system function (fuzz_system_stand_in in fuzz/fuzz_main.cpp) does what the runtime's version does to
# the registers -- writes exactly the result registers its body writes, with random values, and leaves every other register alone,
# as C++ code does -- notes the memory outside the caller's frame as it is at the call (what the function could read), and writes
# random bytes over the memory it writes through its parameters, plus the random write every stand-in may make.

# A stack parameter read by the runtime's version: MEM_W(0x10, ctx->r29) is the first (osCreateThread's sp and priority).
STACK_WORD = re.compile(r'\bMEM_W\(\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*ctx\s*->\s*r29\s*\)')


def stack_words(body):
    """How many stack parameter words the body reads with MEM_W(offset, ctx->r29), and the body with those reads taken out (so
    reads() can tell the rest), or None if one isn't a whole word of the parameter area (0x10 and up, a multiple of 4)."""
    words = 0
    for m in STACK_WORD.finditer(body):
        offset = int(m.group(1), 0)
        if offset < 0x10 or offset % 4:
            return None
        words = max(words, (offset - 0x10) // 4 + 1)
    return words, STACK_WORD.sub('0', body)


def results(body):
    """The result registers the body writes: v0 (ctx->r2, or _return<an integer type>), v1 (ctx->r3), f0 (ctx->f0, or
    _return<float>). None if it returns some other way (_return without a type, a 64-bit one)."""
    b = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
    out = set()
    for m in re.finditer(r'\b_return\s*(<\s*(\w+)\s*>)?\s*\(\s*ctx\b', b):
        t = m.group(2)
        if t in ('float', 'f32'):
            out.add('f0')
        elif t in ('s32', 'u32', 'int32_t', 'uint32_t', 'int', 's16', 'u16', 's8', 'u8'):
            out.add('v0')
        else:
            return None
    for m in re.finditer(r'\bctx\s*->\s*(r2|r3|f0)\b(?:\.\w+)?\s*=(?!=)', b):
        out.add({'r2': 'v0', 'r3': 'v1', 'f0': 'f0'}[m.group(1)])
    return out


# The system functions whose runtime source (N64ModernRuntime's librecomp / ultramodern, the port's src) has been read by hand,
# 2026-11-07: the result registers it always writes (the source's own are checked against these, so a change there refuses it
# again), and the memory it writes through its parameters, as (parameter number, bytes from where it points). Parameters 0-3 are
# a0-a3, 4 and up the stack words. Memory a function writes elsewhere -- through a pointer it reads from memory, or at a place of
# its own -- is left to the random write every stand-in makes now and then, and the note says what it is.
MEMORY = {
    '__osDisableInt_recomp': ((), (), 'does nothing: v0, which the N64 version set to the old interrupt mask, is left as it was'),
    '__osRestoreInt_recomp': ((), (), 'does nothing'),
    'osSetIntMask_recomp': ((), (), 'does nothing: v0 left as it was'),
    '__osSiDeviceBusy_recomp': (('v0',), (), 'returns 0'),
    '__osSiRawStartDma_recomp': (('v0',), ((1, 64),), 'reads (a0 = 1) or writes (a0 = 0) the 64-byte PIF block at a1; posts an SI message'),
    'osAiSetFrequency_recomp': (('v0',), (), 'host audio rate'),
    'osAiSetNextBuffer_recomp': (('v0',), (), 'host audio queue: reads the buffer at a0'),
    'osAiGetStatus_recomp': (('v0',), (), 'host audio state'),
    'osCartRomInit_recomp': (('v0',), (), 'fills the cart handle at a place of its own, and returns its address'),
    'osContInit_recomp': (('v0',), ((1, 1), (2, 16)), 'the controller bit pattern at a1, four controllers\' status at a2'),
    'osCreateMesgQueue_recomp': ((), ((0, 0x18),), 'the OSMesgQueue at a0'),
    'osCreatePiManager_recomp': ((), (), 'does nothing'),
    'osCreateViManager_recomp': ((), (), 'does nothing'),
    'osEPiStartDma_recomp': (('v0',), (), 'reads the OSIoMesg at a1 and the handle at a0; copies ROM to the message\'s dramAddr (not modelled: '
                             'a pointer read from memory) and posts to its queue'),
    'osFlashAllErase_recomp': (('v0',), (), 'the save file, on the host'),
    'osFlashSectorErase_recomp': (('v0',), (), 'the save file, on the host'),
    'osFlashInit_recomp': (('v0',), (), 'returns the flash handle'),
    'osFlashWriteArray_recomp': (('v0',), (), 'the save file, on the host'),
    'osFlashWriteBuffer_recomp': (('v0',), (), 'reads 128 bytes at a2 into the host\'s buffer; posts to the queue at a3'),
    'osFlashReadArray_recomp': (('v0',), ((3, 0x80),), 'reads stack words 0-1 (pages, queue); writes pages * 128 bytes at a3 (the first '
                                'page modelled)'),
    'osGetCount_recomp': (('v0',), (), 'the host clock'),
    'osGetTime_recomp': (('v0', 'v1'), (), 'the host clock, 64 bits in v0 and v1'),
    'osInitialize_recomp': ((), (), 'host start-up'),
    'osRecvMesg_recomp': (('v0',), ((1, 4), (0, 0x18)), 'the message at a1, the queue at a0; may wait for another thread'),
    'osSendMesg_recomp': (('v0',), ((0, 0x18),), 'the queue at a0, and a slot of its message array (not modelled: a pointer read from '
                          'memory); may wait, or wake another thread'),
    'osSetEventMesg_recomp': ((), (), 'host event table'),
    'osViSetEvent_recomp': ((), (), 'host VI state'),
    'osCreateThread_recomp': ((), ((0, 0x30),), 'reads stack words 0-1 (sp, priority); the OSThread at a0; starts a host thread'),
    'osStartThread_recomp': ((), ((0, 0x18),), 'the OSThread at a0, and the run queue\'s threads; may switch threads'),
    'osSetThreadPri_recomp': ((), ((0, 0x18),), 'the OSThread at a0, and the run queue\'s threads; may switch threads'),
    'osSpTaskLoad_recomp': ((), (), 'does nothing'),
    'osSpTaskStartGo_recomp': ((), (), 'reads the OSTask at a0; the RSP task it starts writes memory later, on its own'),
    'osSpTaskYield_recomp': ((), (), 'does nothing'),
    'osSpTaskYielded_recomp': (('v0',), (), 'returns 0'),
    'osViBlack_recomp': ((), (), 'host VI state'),
    'osViSetMode_recomp': ((), (), 'host VI state: keeps the OSViMode pointer a0'),
    'osViSetSpecialFeatures_recomp': ((), (), 'host VI state'),
    'osViSetYScale_recomp': ((), (), 'host VI state (f12)'),
    'osViSwapBuffer_recomp': ((), (), 'host VI state'),
    'osViGetCurrentFramebuffer_recomp': (('v0',), (), 'host VI state'),
    'osViGetNextFramebuffer_recomp': (('v0',), (), 'host VI state'),
    'osVirtualToPhysical_recomp': (('v0',), (), 'pure: the physical address of a0'),
}


def model(names):
    """For each of names: (handled, refused). handled is {name: {'use': registers read, 'stack': stack words read, 'results':
    result registers written, 'writes': [(parameter, bytes)], 'note': text}}; refused is {name: why}.

    Handled only when all of these hold: exactly one definition in the source; its body reads registers only as reads() can tell,
    plus whole stack words; it returns only through v0, v1, f0, and the registers it writes are the ones MEMORY says, read by hand."""
    found = bodies()
    handled, refused = {}, {}
    for n in names:
        defs = found.get(n, [])
        if len(defs) != 1:
            refused[n] = '%d definitions in the source' % len(defs)
            continue
        stack = stack_words(defs[0][1])
        r = reads(stack[1]) if stack else None
        if r is None:
            refused[n] = "its source reads registers in a way that can't be told (ctx handed on, or an odd stack read)"
            continue
        if n not in MEMORY:
            refused[n] = 'not read by hand (system_reads.MEMORY)'
            continue
        out = results(defs[0][1])
        want, writes, note = MEMORY[n]
        if out is None or out != set(want):
            refused[n] = 'its source writes %s, where the hand-read note says %s' % (sorted(out) if out is not None else '?', list(want))
            continue
        handled[n] = {'use': sorted(r), 'stack': stack[0], 'results': sorted(out), 'writes': [list(w) for w in writes], 'note': note}
    return handled, refused


if __name__ == '__main__':
    found = bodies()
    for n in sys.argv[1:]:
        defs = found.get(n, [])
        r = reads(defs[0][1]) if len(defs) == 1 else None
        print('%-40s %s' % (n, sorted(r) if r is not None else 'unknown (%d definitions)' % len(defs)))
