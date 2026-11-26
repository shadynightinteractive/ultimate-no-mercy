# The six-man engine's data: every per-wrestler (slot) and per-model array the engine keeps for 4, moved to
# room for 6 (the review: kb/review/*.out, kb/plan.py). base, element stride, where it goes.
import os as _os
# The wrestler slots the engine has room for: 6, the six-man layout (everything below), unless SIX_SLOTS asks for
# more (the 32-slot capacity: room for 32 while a match still has its own count -- $gp, rw_match_count). With more
# slots the room is worked out at the end of this file: ovl_e's BSS grows past 0x80176000 (the heap starts later)
# and the main arrays start below 0x80500000 (the heap ends sooner), both inside the N64's 8 MB, below FB0.
NSLOTS = int(_os.environ.get('SIX_SLOTS', '6'))
OVLE_BSS_END_OLD = 0x8016B9F0
OVLE_BSS_END = 0x80176000          # ovl_e's BSS grows (zeroed at each load); the heap starts after it
HEAP = OVLE_BSS_END
EXP = 0x80500000                   # main-segment arrays (expansion RAM; never touched by the game)
# (SIX_HEAPGROW=0: the heap moves but keeps the stock size -- the full layout's check against the stock game, where
#  a block's address then moves by the same amount whichever end of the heap it was taken from)
HEAP_END = EXP if _os.environ.get('SIX_HEAPGROW', '1') == '1' else HEAP + 0x221800
FB0 = 0x80600000
FB1 = FB0 + 0x38400

# (base, stride, note) -- ovl_e BSS (0x80151ED0..0x8016B9F0)
OVLE = [
    (0x80154638, 0xC,   'HUD entry per wrestler'),
    (0x80154668, 0xDC,  'HUD panel sprite A'),
    (0x801549D8, 0x8,   'HUD texture/palette'),
    (0x80154AA8, 0x34,  'HUD sprite params'),
    (0x80154B78, 0x1,   'HUD color index'),
    (0x80154B80, 0xDC,  'HUD sprite D'),
    (0x80154F08, 0x34,  'HUD sprite D params 1'),
    (0x80154FD8, 0x34,  'HUD sprite D params 2'),
    (0x801550A8, 0x34,  'HUD sprite D params 3'),
    (0x80155178, 0x8,   'HUD image refs'),
    (0x80155198, 0xDC,  'HUD sprite C'),
    (0x80155508, 0x34,  'HUD sprite C params'),
    (0x801556F0, 0xDC,  'HUD panel sprite'),
    (0x80155A60, 0x34,  'HUD sprite E params'),
    (0x80155B30, 0x8,   'HUD texture pair'),
    (0x80155B50, 0xDC,  'HUD panel sprite'),
    (0x80155EC0, 0x34,  'HUD sprite B params'),
    # (0x80156880 x 0x410 'HUD texture buffer': not per slot only -- the area is also generic object/matrix data)
    (0x80157F60, 0x294, 'HUD digits'),
    (0x801589B0, 0x3,   'HUD digit chars'),
    (0x80158EC0, 0x14,  'camera position snapshot'),
    (0x801590E0, 0x3C,  'input history per model (30 byte pairs; wrestler +0x344 points at its own)'),
    (0x801591D0, 0x4,   'action counter'),
    (0x80159218, 0x4,   'team score part'),
    (0x80159270, 0x4,   'per-model timer (func_80122AB0, model +0x9E)'),
    (0x80159290, 0x18,  'run/rope record'),
    (0x801593A0, 0x2,   'per-model s16 (func_8012F848: +0xA6)'),
    (0x801593A8, 0xC,   'head-look angles'),
    (0x801593D8, 0x4,   'head-look value'),
    (0x80159420, 0x4,   'press counter'),
    (0x80159430, 0x4,   'press count'),
    (0x80159440, 0x4,   'press delay'),
    (0x80159450, 0x2,   'per-model flags (func_80132FFC)'),
    (0x80159458, 0x2,   'per-model flags 2 (func_80132FFC)'),
    (0x801594C0, 0x4,   'hold counter'),
    (0x801594E0, 0xC,   'CPU move-list counts (func_800FB038)'),
    (0x80159518, 0x38,  'input per slot'),
    (0x80159608, 0x4,   'sequence slot list'),
    (0x8015961C, 0x2,   'costume per slot'),
    (0x80159630, 0x10,  'CPU move list C (func_800FB038)'),
    (0x80159680, 0x10,  'CPU move list F (func_800FB038)'),
    (0x801596C0, 0xB0,  'CPU AI per slot'),
    (0x80159990, 0x358, 'wrestlers'),
    (0x8016B300, 0x10,  'CPU move list E (func_800FB038)'),
    (0x8016B350, 0x88,  'per-slot 17 records'),
    (0x8016B570, 0x2,   'slot in use'),
    (0x8016B580, 0x4,   'pending action'),
    (0x8016B5A0, 0x20,  'state history'),
    (0x8016B620, 0xC,   'CPU move list D (func_800FB038)'),
    (0x8016B658, 0x30,  'CPU move list A (func_800FB038)'),
    (0x8016B718, 0x1,   'per-wrestler char'),
    (0x8016B728, 0x70,  'hit radius'),
    (0x8016B8F0, 0xC,   'per-slot state'),
    (0x8016B920, 0x2,   'roster id per slot'),
    (0x8016B940, 0x20,  'CPU move list B (func_800FB038)'),
    (0x8016B9D0, 0x4,   'slot controller port'),
    # (ovl_e .data, zero in the ROM: here zeroed at each load the same) the computer's last 10 actions per model
    # (func_800F0DA4 shifts them in, func_800F0E0C looks), packed against the difficulty tables at 0x8014526C
    (0x8014521C, 0x14,  'CPU recent actions per model'),
    # (found by exp6 EXP_OLD: slots 4 and 5 reached past these into the old places above them)
    # ovl_e .data, zero in the ROM like the actions above: func_800F0D04's per-slot 3-byte timers (frames, seconds,
    # minutes since the slot's last ...; func_800F6154 reads them)
    (0x80145210, 0x3,   'CPU timers per slot (func_800F0D04)'),
    # the word per slot that func_8010EFB0 reads and func_8010FF78 writes through a pointer (indexed by +0's slot)
    (0x801590D0, 0x4,   'per-slot word (func_8010EFB0 / func_8010FF78)'),
    # (found by dbg/slotscan_cmp.py: 4-man vs 6-man access steps) ovl_e .data, zero in the ROM: func_800F03F0's
    # per-model last action and how many frames it has stayed (at 300: func_800FCF74) -- slots 4 and 5 ran into
    # each other and into the computer's level table at 0x801452D4 (func_800F1740)
    (0x801452C4, 0x2,   'CPU last action per model (func_800F03F0)'),
    (0x801452CC, 0x2,   'CPU same-action frames per model (func_800F03F0)'),
    # (2026-11-12, crash32: found by the write-watch on the 32-slot Rumble's crash -- the replay recorder's pointer
    #  0x801593E8 written over by state 89 for slot 10 -- and then by a scan of the engine's code for indexed accesses
    #  to ovl_e's stock BSS: per-slot arrays the review missed, each indexed by the step's slot (+0 of the step record).
    #  Left where they were (room for 4), slot 4 on ran into the words after them: the match's random numbers
    #  0x80159350.. (slots 4 and 5 of 0x801592F0: a six-man match too), the replay's 0x801593E8.. (slots 10 to 13 --
    #  the crash), the result-up / camera-shot words 0x801594D0 / 0x801594D2 (slot 4 of 0x80159460), the D-pad copies
    #  0x80159250.. (slots 4 to 7 of 0x80159240).)
    (0x801590C0, 0x4,   'per-slot word (state 27 taunt, func_800E67F0)'),
    (0x80159240, 0x4,   'per-slot float (state 127 hold from a grapple, func_80119E00)'),
    (0x80159250, 0x2,   'D-pad copy per slot (states 17 / 19 rope walk, func_8011BAC4)'),
    (0x80159280, 0x4,   'target slot per slot (state 129 grapple reversed, func_80125190)'),
    (0x801592F0, 0x18,  'move place record (state 89 move in progress, func_80129128: +8 distance, +0x10 angle)'),
    (0x80159460, 0x18,  'move place record 2 (state 70 ground grapple set-up, func_80136240)'),
]

# (bisecting a wider layout: SIX_WIDE="ovle", "main", "ovle:0-27", "main:10-20,ovle:5" -- only those arrays get NSLOTS
#  slots' room, the rest six; unset, all of them. For finding which array's move a problem follows, 2026-11-06.)
_WIDE = _os.environ.get('SIX_WIDE')
def _wide(tag, i):
    if not _WIDE or tag is None: return True
    for part in _WIDE.split(','):
        t, _, rng = part.partition(':')
        if t != tag: continue
        if not rng: return True
        lo_, _, hi_ = rng.partition('-')
        if int(lo_) <= i <= int(hi_ or lo_): return True
    return False
def layout(arrs, lo, hi, n=NSLOTS, tag=None):
    """new base for each array, packed from lo (16-aligned)"""
    out = []; a = lo
    for i, (base, stride, note) in enumerate(arrs):
        a = (a + 15) & ~15
        out.append((base, stride, a, note))
        a += (n if _wide(tag, i) else 6) * stride
    assert a <= hi, 'room: %x > %x' % (a, hi)
    return out, a

if NSLOTS == 6:      # (more slots: at the end of the file, once the room is known)
    OVLE_NEW, OVLE_USED = layout(OVLE, OVLE_BSS_END_OLD, OVLE_BSS_END)

# blocks of the six-man engine's own in ovl_e's BSS, after the moved arrays (zeroed at each load): name -> bytes
#  - namebuf: the HUD's per-slot name textures (file 0x395, 0x410 each), stock 4 at 0x80156880 packed against the
#    clean copy at 0x801578C0 -- that area is not moved (the clean copy and the HUD's other data stay); func_800DDB10
#    loads the per-slot copies here instead (rw_namebuf_s1) and every reader goes by the pointers it stores
#  - statmat: the three per-wrestler hit-stat matrices (stock 0x8009FDF8/0x8009FE18/0x8009FE38: 4 x 4 u16, rows of
#    8 bytes -- a row can't take 6 columns), here 6 x 6, rows of 12 bytes (rw/six.h STAT); only ovl_e uses them
#  - result6: slots 4 and 5's match result (rw/six.h RES6: winners bits 0-1, losers bits 4-5) -- the result mask
#    0x800A10EC has no free bits (ovl_c func_8013843C sets 0x1000000 / 0x2000000); only ovl_e reads them
EXTRA_OVLE = [('namebuf', NSLOTS * 0x410), ('statmat', 3 * NSLOTS * NSLOTS * 2), ('result6', 8)]   # (result6: the winners' word and the losers', slots 4 on: rw/six.h RES6)
# ... and for the whole run (expansion RAM after the main arrays; every overlay):
#  - dlbuf: the frame display lists of a six-man match, 2 x 0x8000 (the stock ones are the first 12000 bytes of
#    each 0x2F58 frame block, the scheduler task after them -- six wrestlers need more: rw_dlbase_*)
EXTRA_MAIN = [('dlbuf', 2 * 0x8000)]
DLBUF_SIZE = 0x8000   # (each of the two; past six slots they're elsewhere and bigger: below, patch_dlbuf.py)
def blocks(lo, hi, extra=None):
    out = {}; a = lo
    for name, size in (EXTRA_OVLE if extra is None else extra):
        a = (a + 15) & ~15; out[name] = a; a += size
    assert a <= hi, 'room: %x > %x' % (a, hi)
    return out

# (base, stride, note) -- the main segment's BSS (0x80056670..0x800BD960): global, all overlays' references move
MAIN = [
    (0x80069F20, 0xC0,   'model anim stream state'),
    (0x8006A228, 0x8,    'model anim bytes'),
    (0x8006A248, 0x8,    'model anim bytes 2'),
    (0x8006A268, 0x1,    'model body class'),
    (0x80070D02, 0x2,    'model s16 (func_80013F48)'),
    (0x80070D0A, 0x2,    'model short (0x80048A48 id)'),
    (0x80070D12, 0x2,    'model variant/costume'),
    (0x80070D20, 0x10,   'model 16-byte record'),
    (0x80070D60, 0x410,  'model buffer'),
    (0x80071DA0, 0x1130, 'model buffer 2'),
    (0x80076260, 0x1,    'model loaded flag'),
    (0x80076264, 0x2,    'model loaded id'),
    (0x8007626C, 0x1,    'model loaded variant'),
    (0x80076270, 0x2,    'model requested id'),
    (0x80076278, 0x1,    'model load state'),
    (0x80076588, 0x1,    'model attachment state'),
    (0x80076590, 0x4,    'model attachment parts'),
    (0x800765A0, 0x90,   'model face parts set A'),
    (0x800767E0, 0x50,   'model face parts set B'),
    (0x80076BC0, 0x40,   'model joint copies'),
    (0x80076CC0, 0x4,    'model blend'),
    (0x80076CD0, 0x4,    'model fade rate'),
    (0x80076CE0, 0x2,    'model timer'),
    (0x80076CE8, 0x2,    'model timer copy'),
    (0x80076CF0, 0x1,    'model byte'),
    (0x80076CF8, 0x8,    'model handles'),
    (0x80077120, 0x2A,   'model texture/part state'),
    (0x8008826F, 0x2,    'model attached (model, node) byte pairs'),
    (0x80088278, 0xDC,   'model attached scene object'),
    (0x800885E8, 0x24,   'model DL object'),
    (0x80088678, 0x10,   'model DL object 2'),
    (0x800886B8, 0x14,   'model DL object 3'),
    (0x80088708, 0x10,   'model DL object 4'),
    (0x80088748, 0xDC,   'model shadow object'),
    (0x80088F10, 0x2,    'model shadow state'),
    (0x8008FA70, 0x448,  'per-model anim table A'),
    (0x80091CC0, 0x1810, 'model render struct'),
    (0x80097FB8, 0x8,    'match results'),
    (0x8009ED10, 0x2,    'CPU difficulty'),
    (0x8009ED20, 0x1,    'slot human flag'),
    (0x8009ED24, 0x1,    'slot team/queue size'),
    # controller pads by port (+0 status pointer, +4 held, +6 pressed, +8 released, +A/+B stick); func_80005818 fills
    # ports 0..3. A slot's port (0x8016B9D0) is 4 or 5 for the computer's slots past 4 in modes 0xC000, and
    # func_80118368 reads pad[port] for every slot (its button toggle hands the slot to a player): ports 4, 5 = none
    (0x8009EDA0, 0xC,    'controller pads per port'),
    (0x8009FDF4, 0x1,    'result byte'),
    (0x8009FE58, 0x4,    'elimination time'),
    (0x8009FE78, 0x40,   'per-slot block'),
    (0x8009FF90, 0x448,  'per-model anim table B'),
    (0x800A10CC, 0x1,    'model byte'),
    (0x800A10D9, 0x1,    'model byte 2'),
    (0x800A1130, 0xB24,  'model bones'),
    (0x800A3DC0, 0x1C,   'rumble per-slot record'),
    (0x800A4038, 0x4,    'slot wrestler id'),
    (0x800A4050, 0x5B0,  'model parts'),
    (0x800A5720, 0x224,  'special move map'),
    (0x800A6028, 0x4,    'slot pad'),
    (0x800A84F0, 0x1,    'slot byte'),
    (0x800A8958, 0x4,    'slot costume'),
    (0x800A8980, 0x270,  'model descriptor'),
    (0x800A9458, 0x1,    'model variant'),
    (0x800A9946, 0x2,    'model costume'),
    (0x800AA078, 0x4,    'team int'),
    (0x800AA090, 0x4D10, 'AI per model'),
]

# The frame's draw list (u32[] at 0x80060BA0, the count at 0x800619B2 right after it: func_8000161C appends, func_800022D8
# / func_80002400 sort, func_800025B0 / func_80002730 draw; the mod's announce.c and tron.c read and write it): stock 900,
# about 47 a wrestler (356 with 4, ~450 with 6, ~633 with 10) -- about 19 wrestlers fill it. For more than six slots it
# moves with the main arrays, 0x258 bytes (150 entries) a slot, 4800 for 32 (the references moved, to the stock
# list's first 4 x 0x258 bytes, are all its own: only its users' bases point into it). 150, not more: it then fits the
# room the main arrays left below FB0 (0x5660 bytes), so nothing else moves -- the converted code (lifted.cpp) has the
# other arrays' places written in, and a 0x384 stride's 64 KB shift crashed the title (2026-11-06). The six-slot
# build keeps it where it was.
if NSLOTS != 6:
    MAIN.append((0x80060BA0, 0x258, 'scene draw list (u32, 150 a slot)'))
    # (and its sorted groups, the same size: 0x80061D40, u32[900] up to the words at 0x80062B50, count 0x800619B4 --
    #  func_80002400 fills it, func_800025B0 / func_800028B4 / func_80002FA4 and the exe's arcade_scene.cpp and the
    #  mod's tron.c read it; 1224 with 24 wrestlers ran it over the lights at 0x80062B68: the first 24-man probe's
    #  crash at match frame 129, 2026-11-07)
    MAIN.append((0x80061D40, 0x258, 'scene sorted groups (u32, 150 a slot)'))
# (one list for the whole frame, not a model's: MAIN's other arrays are per model, and what treats them so -- the mod's
#  announcer, which saves and puts back a model slot's arrays round its load (gen/mkannsave.py, src/ann_slot.h) -- leaves
#  these out. Put back with the slot, the list lost the announcers' entries: JR and King not drawn, 2026-11-06.)
MAIN_SHARED = {0x80060BA0, 0x80061D40}

# The scene-record pool (func_8000176C's 0xB8-byte records, one heap block made at boot by func_80001340; stock 900):
# about 72 a wrestler (504 with 4, ~645 with 6, ~968 with 10) -- 300 + 96 a slot, never fewer than the stock 900
# (patch_record_pool.py has the story)
POOL_RECORDS = max(900, 300 + 96 * NSLOTS)

# func_80120D98's target list on its callers' stacks (func_80126384, func_8012069C: sp+0x10, room for 12 entries):
# max(n - 1, 10) + 1 entries -- the frames grow by what's missing, 8-aligned (patch_target_list.py; 0 for six)
# func_800EBD2C's contact list (patch_contacts.py): room for max(n - 1, 10) entries and the owner's after them
CONTACT_CAP = max(NSLOTS - 1, 10)
TARGET_LIST_GROW = 0 if NSLOTS == 6 else max(0, (4 * (max(NSLOTS - 1, 10) + 1) - 0x30 + 7) & ~7)

# ---- more than six slots (SIX_SLOTS) ----------------------------------------------------------------------------
# The six-man numbers above stay as they are for 6 (so that build still comes out byte for byte the same). For more,
# the room is what the arrays need: ovl_e's BSS ends after its arrays and the engine's own blocks (rounded up to 8 KB,
# as 0x80176000 is), and the main arrays and their blocks end at FB0 (their start rounded down to 64 KB). The heap
# keeps what is left between the two: 0x80194000..0x804A0000 for 32, about 3 MB (the stock game's was 2.1 MB; the
# arcade adds 7 MB above the 8 MB besides).
def _need(arrs, extra, n, tag=None):
    end = layout(arrs, 0, 1 << 32, n, tag)[1]
    for name, size in extra:
        end = ((end + 15) & ~15) + size
    return end
if NSLOTS != 6:
    OVLE_BSS_END = (OVLE_BSS_END_OLD + _need(OVLE, EXTRA_OVLE, NSLOTS, 'ovle') + 0x1FFF) & ~0x1FFF
    HEAP = OVLE_BSS_END
    # the frame display lists, past six slots: out of the main arrays' room, to the expansion RAM that's empty in a
    # match, 0x1000 + 0xA00 a slot each (about 0x900-0xA00 a wrestler is used), never under 0x8000 (patch_dlbuf.py)
    EXTRA_MAIN = []
    DLBUF = 0x80730000
    DLBUF_SIZE = max(0x8000, (0x1000 + 0xA00 * NSLOTS + 0xFFF) & ~0xFFF)
    assert DLBUF + 2 * DLBUF_SIZE <= 0x80780000, 'the frame lists would reach the engine'
    # the main arrays' start, kept where the converted code (lifted.cpp) has them: it may go down for more room (and
    # then the conversion must be run again -- port/install.py check_lifted stops the install), not up
    EXP_PIN = {32: 0x804B0000}
    EXP = (FB0 - _need(MAIN, EXTRA_MAIN, NSLOTS, 'main')) & ~0xFFFF
    EXP = min(EXP, EXP_PIN.get(NSLOTS, EXP))
    HEAP_END = EXP if _os.environ.get('SIX_HEAPGROW', '1') == '1' else HEAP + 0x221800
    assert HEAP_END - HEAP >= 0x221800, 'the heap would be smaller than the stock one'
    OVLE_NEW, OVLE_USED = layout(OVLE, OVLE_BSS_END_OLD, OVLE_BSS_END, NSLOTS, 'ovle')
