"""The game's functions the arcade changes a few instructions of (gen/asmpatch.py builds them)."""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import re

# "ordinal < 0x41: a Superstar (not a Create-a-Wrestler slot)" -> also the new Superstars (102 and up):
# a Create-a-Wrestler slot or edit buffer is 0x41..0x65
def superstar(dst, src):
    return ["addiu $at, $%s, -0x41" % src, "sltiu $at, $at, 0x25", "xori $%s, $at, 1" % dst]

PATCHES = [
    dict(func="func_801144EC", why="the Superstar Select's name panel: a Superstar's style, not an edit slot's",
         edits={0x80114584: superstar("v0", "v0"), 0x80114674: superstar("a1", "v0")}),
    dict(func="func_8011DCC4", why="the VS screen's name panel: the same",
         edits={0x8011E08C: superstar("v0", "v0")}),
]

# Photos 100-127 (files.c): the game's code that names a photo's files, from the photo p (its Superstar
# Select "TEX" 0x45B3 + p) or 2p (its picture 0x4AAE + 2p, palette 0x4AAD + 2p), names the new files for
# them. "addiu rd, rs, base" -> the game's file, moved to the new ones when the photo is 100 or more.
PHOTO_EXT0 = 100
TEX_NEW, TLUT_NEW, CI4_NEW = 0x4C03, 0x4C1F, 0x4C20
# Photos 128-255 (2026-11-01, ports4: the tenth Revenge port's photo is 129): a second block of the arcade's files
# (files.c FILE_PHOTO2_*): TEX 0x7C00 + (p - 128), palette / picture 0x7C80 / 0x7C81 + 2 (p - 128). The photo is read
# unsigned (func_8000AB9C's lbu and its callers' srl, below), so p (or 2p) here is 0..255 (0..510).
PHOTO_EXT2 = 128
TEX_NEW2, TLUT_NEW2, CI4_NEW2 = 0x7C00, 0x7C80, 0x7C81
def photo_file(rd, rs, base, two_p, label):
    new = {0x45B3: TEX_NEW, 0x4AAD: TLUT_NEW, 0x4AAE: CI4_NEW}[base]
    new2 = {0x45B3: TEX_NEW2, 0x4AAD: TLUT_NEW2, 0x4AAE: CI4_NEW2}[base]
    k = 2 if two_p else 1
    lim, lim2 = k * PHOTO_EXT0, k * PHOTO_EXT2
    off, off2 = (new - k * PHOTO_EXT0) - base, (new2 - k * PHOTO_EXT2) - base
    # (rd = rs + base in the first branch's delay slot; past 100: + off in the second's; past 128 + (off2 - off) more)
    return ["sltiu $at, $%s, %d" % (rs, lim), "bnez $at, %s" % label, "addiu $%s, $%s, 0x%X" % (rd, rs, base),
            "sltiu $at, $%s, 0x%X" % (rd, base + lim2), "bnez $at, %s" % label, "addiu $%s, $%s, %d" % (rd, rd, off),
            "addiu $%s, $%s, %d" % (rd, rd, off2 - off), "%s:" % label]
def ph(addr, rd, rs, base, two_p=False):
    return {addr: photo_file(rd, rs, base, two_p, "L_%08X_new" % addr)}
def merge(*ds):
    out = {}
    for d in ds: out.update(d)
    return out
PATCHES[1]["edits"].update(ph(0x8011DD4C, "s6", "v0", 0x45B3))       # (the VS screen's: one copy of it)
PATCHES += [
    dict(func="func_80114CBC", why="photo files", edits=ph(0x80114DC4, "v0", "v0", 0x45B3)),
    dict(func="func_80114FBC", why="photo files", edits=merge(ph(0x8011518C, "v0", "v0", 0x45B3), ph(0x801153E0, "v0", "v0", 0x45B3))),
    dict(func="func_8011A250", why="photo files", edits=ph(0x8011A3F0, "v0", "v0", 0x45B3)),
    dict(func="func_80123588", why="photo files", edits=ph(0x80123AF0, "s7", "v0", 0x45B3)),
    dict(func="func_8012BBB0", why="photo files", edits=ph(0x8012C2F0, "a1", "v0", 0x45B3)),
    dict(func="func_80109954", why="photo files", edits=merge(ph(0x8010A8C8, "v0", "v1", 0x4AAE, True), ph(0x8010A8D4, "v1", "v1", 0x4AAD, True))),
    dict(func="func_80110B30", why="photo files", edits=merge(ph(0x801111BC, "v1", "v0", 0x4AAE, True), ph(0x801111C8, "v0", "v0", 0x4AAD, True))),
    dict(func="func_80119A60", why="photo files", edits=merge(ph(0x80119B1C, "v0", "v1", 0x4AAE, True), ph(0x80119B28, "v1", "v1", 0x4AAD, True))),
    dict(func="func_80119CB8", why="photo files", edits=merge(ph(0x80119EAC, "v0", "v1", 0x4AAE, True), ph(0x80119EC0, "v1", "v1", 0x4AAD, True))),
    dict(func="func_80122660", why="photo files", edits=merge(ph(0x8012286C, "v0", "v1", 0x4AAE, True), ph(0x80122878, "v1", "v1", 0x4AAD, True))),
    dict(func="func_80122C40", why="photo files", edits=merge(ph(0x80122FA8, "v0", "v1", 0x4AAE, True), ph(0x80122FB4, "v1", "v1", 0x4AAD, True))),
]

# The game's files end at 0x4C02; the arcade's go on from 0x4C03 (files.c) up to FILES_END. The
# game's checks of a file ID against its count take the arcade's too.
FILES_END = 0x7FFF
# QA (Development\qa.ps1): the moveset entry a wrestler goes into goes through logic.c's qa_move_entry first, so a
# scenario can force a move (the rig's test_ec698, in the real game); unchanged unless a QA scenario asks.
# func_800EC698(wrestler, owner's slot, entry): its first instruction (move a3, a0) after the call.
PATCHES += [
    dict(func="func_800EC698", why="QA: a forced move (logic.c qa_move_entry)",
         edits={0x800EC698: ["addiu $sp, $sp, -32", "sw $ra, 16($sp)", "sw $a0, 20($sp)", "sw $a1, 24($sp)",
                             "jal qa_move_entry", "nop", "or $a2, $v0, $zero",
                             "lw $ra, 16($sp)", "lw $a0, 20($sp)", "lw $a1, 24($sp)", "addiu $sp, $sp, 32",
                             "addu $a3, $a0, $zero"]}),
]
# QA: the frame's view x projection (func_80001580: func_80001E08(view, projection, out)) through announce.c's
# qa_camera_mul, so a scenario can put the camera where it wants (the rig's ann_test_camera); unchanged otherwise.
PATCHES += [
    dict(func="func_80001580", why="QA: a scenario's camera (announce.c qa_camera_mul)",
         edits={0x80001590: "jal qa_camera_mul"}),
]
# The face's and skin's palette groups (func_800199CC, func_80019B38, func_80019C54: a list of parts each; func_80019D88,
# the face: five) through skel.c's pal_group_guard: loaded only where the part's palette has room -- a ported model's
# are packed, its display lists right after them (the Royal Rumble's "never-ending display list", 2026-10-27).
GUARD = "jal pal_group_guard"
PATCHES += [
    dict(func="func_800199CC", why="a palette group only where it fits (skel.c pal_group_guard)", edits={0x80019AD8: GUARD}),
    dict(func="func_80019B38", why="a palette group only where it fits (skel.c pal_group_guard)", edits={0x80019C00: GUARD}),
    dict(func="func_80019C54", why="a palette group only where it fits (skel.c pal_group_guard)", edits={0x80019D38: GUARD}),
    dict(func="func_80019D88", why="a palette group only where it fits (skel.c pal_group_guard)",
         edits={a: GUARD for a in (0x80019E08, 0x8001A09C, 0x8001A0C0, 0x8001A118, 0x8001A160)}),
]
PATCHES += [
    dict(func="func_800DF2C8", why="the sprite cache's check of a file ID",
         edits={0x800DF2D8: "sltiu $v0, $s0, 0x%X" % FILES_END}),
    dict(func="func_80110820", why="ovl_b: a picture's file ID checked",
         edits={0x80110830: "slti $v0, $s0, 0x%X" % FILES_END}),
]

# ovl_d (the edit screens) keeps a photo of 100 or more out (as photo 0): photos go to 127 now.
def find(name): return next(p for p in PATCHES if p["func"] == name)
find("func_80109954")["edits"][0x8010A678] = "sltiu $v0, $v0, 0x80"
find("func_80122660")["edits"][0x80122844] = "sltiu $v0, $v1, 0x100"
find("func_80122C40")["edits"][0x80122F7C] = "sltiu $v0, $v1, 0x100"
PATCHES.append(dict(func="func_801118E0", why="a Superstar's photo (100-255 kept)", edits={0x801118F4: "srl $v1, $v1, 24", 0x801118F8: "sltiu $v0, $v1, 0x100"}))
# A Superstar's photo unsigned (0-255; ports4): func_8000AB9C reads the looks' byte 0x3C with lbu, and each of its
# callers keeps it so (its "sll 24 / sra 24" a "sll 24 / srl 24"; func_801118E0's above)
PATCHES.append(dict(func="func_8000AB9C", why="a Superstar's photo unsigned (128-255)", edits={0x8000ABB8: "lbu $v0, 0x4C($sp)"}))
for _f, _a, _i in (("func_80114CBC", 0x80114DC0, "srl $v0, $v0, 24"), ("func_80114FBC", 0x80115180, "srl $v0, $v0, 24"),
                   ("func_80114FBC", 0x801153D4, "srl $v0, $v0, 24"), ("func_8011A250", 0x8011A3EC, "srl $v0, $v0, 24"),
                   ("func_8011DCC4", 0x8011DD48, "srl $v0, $v0, 24"), ("func_80123588", 0x80123AEC, "srl $v0, $v0, 24"),
                   ("func_8012BBB0", 0x8012C2E8, "srl $v0, $v0, 24"), ("func_80122660", 0x80122840, "srl $v1, $v0, 24"),
                   ("func_80122C40", 0x80122F78, "srl $v1, $v0, 24")):
    find(_f)["edits"][_a] = _i

# Body parts, with more of them (parts.c, gen/mkparts.py): func_80018A3C (a costume's struct -> its
# model) reads the bigger tables of parts, and takes the new parts' numbers: lower bodies up to 0x83,
# upper bodies 0x80 (besides the game's 0-0x7C), faces 0x80-0x82 (besides 0-0x7E), beards and masks up to 33.
# A table's address "lui r, hi / ... / op r', lo(r)" is made the bigger table's.
def tab(lui_at, lui_reg, use_at, use, sym):
    """use: the using instruction with {} for the low half, e.g. 'lbu $2, {}($2)'"""
    return {lui_at: "lui $%d, %%hi(%s)" % (lui_reg, sym), use_at: use.format("%%lo(%s)" % sym)}
def part_ok(dst, src, game_n, new0, new_n):
    """dst = src is a part: the game's (below game_n) or a new one (new0..)"""
    return ["sltiu $%d, $%d, 0x%X" % (dst, src, game_n), "addiu $at, $%d, -0x%X" % (src, new0),
            "sltiu $at, $at, %d" % new_n, "or $%d, $%d, $at" % (dst, dst)]
def face_ok(dst, src, game_n): return part_ok(dst, src, game_n, 0x80, 3)   # faces: new 0x80-0x82
PATCHES.append(dict(func="func_80018A3C", why="a costume's model: the bigger tables of parts", edits=merge(
    {0x80018B08: "srl $20, $2, 0x18", 0x80018B0C: "sltiu $2, $20, 0x84",          # lower body: unsigned, < 0x84
     0x80018B1C: "srl $17, $3, 0x18", 0x80018B20: part_ok(2, 17, 0x7D, 0x80, 1)},  # upper body: < 0x7D, 0x80
    tab(0x80018B70, 2, 0x80018B78, "lbu $2, {}($2)", "parts_lower_fem"),
    {0x80018DFC: face_ok(2, 3, 0x7F), 0x80018E08: "srl $2, $2, 0x17", 0x80018E20: "lbu $2, 0xf($19)"},
    tab(0x80018E0C, 1, 0x80018E14, "lhu $2, {}($1)", "parts_face_tex"),
    tab(0x80018E28, 1, 0x80018E30, "lhu $2, {}($1)", "parts_face_tex"),
    tab(0x80018E40, 8, 0x80018E48, "lbu $8, {}($8)", "parts_lower_kind"),
    tab(0x80018E88, 2, 0x80018E8C, "addiu $2, $2, {}", "parts_lower_desc"),
    tab(0x80018ED0, 8, 0x80018ED8, "lbu $8, {}($8)", "parts_upper_kind"),
    tab(0x80018F10, 2, 0x80018F14, "addiu $2, $2, {}", "parts_upper_desc"),
    {0x800192E8: "sltiu $2, $2, 0x21", 0x80019364: "slti $2, $3, 0x22"},          # beard: 1..33
    tab(0x80019324, 1, 0x8001932C, "lhu $2, {}($1)", "parts_fh_tex"),
    tab(0x8001933C, 1, 0x80019344, "lhu $2, {}($1)", "parts_fh_tex"),
    {0x80019350: "lbu $4, 0xf($19)", 0x80019354: face_ok(2, 4, 0x76)},
    tab(0x80019370, 2, 0x80019378, "lbu $2, {}($2)", "parts_face_b1"),
    tab(0x8001937C, 1, 0x80019384, "lbu $3, {}($1)", "parts_fh_b"),
    {0x80019398: "sltiu $2, $2, 0x21", 0x800193DC: "slti $2, $3, 0x22"},          # mask: 1..33
    tab(0x800193AC, 6, 0x800193B4, "lw $6, {}($6)", "parts_mask_desc"),
    {0x800193C8: "lbu $4, 0xf($19)", 0x800193CC: face_ok(2, 4, 0x76)},
    tab(0x800193E8, 2, 0x800193F0, "lb $2, {}($2)", "parts_mask_b"),
    tab(0x800193FC, 2, 0x80019404, "lbu $2, {}($2)", "parts_face_b2"),
    {0x800194A4: "lbu $2, 0xf($19)"},
    tab(0x800194AC, 1, 0x800194B4, "lhu $2, {}($1)", "parts_face_tv"),
    # the made model, before returning: parts_model_post(the looks (its copy on the stack), the model)
    {0x80019520: ["move $4, $19", "jal parts_model_post", "move $5, $18", "lw $ra, 0xbc($sp)"]},
)))
# func_8001858C(model, colours, type, param): a part's colours by its param (1..0x47; the new 0x80)
PATCHES.append(dict(func="func_8001858C", why="a part's colours: the new params", edits=merge(
    {0x80018594: part_ok(2, 7, 0x48, 0x80, 1)},
    tab(0x800185A8, 4, 0x800185B0, "lbu $4, {}($4)", "parts_param_kind"),
    tab(0x800185F0, 11, 0x800185F8, "lw $11, {}($11)", "parts_param_files"),
)))

# The save stays the game's (save.c): func_80004EE8, the one way the save image is ever written,
# has it without the new Superstars while it writes (save_sanitize) and as it was after (save_restore).
PATCHES.append(dict(func="func_80004EE8", why="the save written without the new Superstars", edits={
    0x80004F08: ["jal save_sanitize", "nop", "lui $4, 0x8006"],          # (there is an image)
    0x80004FB4: ["jal func_8000423C", "nop", "jal save_restore"],      # (the nop after: its delay slot)
}))

# The Superstar Select's Random (ovl_c func_8010A9E8) draws an ordinal, rand() % 65 (then redraws one
# that isn't on the pages, or can't be picked): roster.c's draws the new Superstars' too.
PATCHES.append(dict(func="func_8010A9E8", why="the Superstar Select's Random: the new Superstars too", edits=merge(
    {0x8010AA18: ["jal roster_random_ord", "move $4, $2", "jal func_8000AE38", "move $4, $2"]},
    {a: [] for a in range(0x8010AA1C, 0x8010AA3C, 4)},
)))

# More music slots (media.c, gen/mkcontent.py): func_80017C64 (a song played) takes songs up to SONGS_N and
# reads media.c's song table (the game's 80, then the new slots' songs) in place of the game's 0x800480D8.
SONGS_N = int(re.search(r"#define SONGS_N (\d+)", open(nmp("/home/devbox/work/arcade/src/content_index.h")).read()).group(1))
PATCHES.append(dict(func="func_80017C64", why="a song played: more songs (media.c media_songs)", edits=merge(
    {0x80017CC4: "sltiu $2, $2, %d" % (SONGS_N + 64)},     # (64: themes from wrestler folders, FOLDER_THEMES in arcade.h)
    {0x80017D0C: "lui $2, %hi(media_songs)", 0x80017D10: "addiu $2, $2, %lo(media_songs)"},
    {0x800180C4: "lui $3, %hi(media_songs)", 0x800180C8: "addiu $3, $3, %lo(media_songs)"})))

# The Superstar Select's pages, as many as roster.c has (roster_pages_tab, roster_pages_n of them: the game's
# twelve and WCW's, or select_pages.json's, up to 32 -- the user, 2026-10-30). The game's table (0x800A9990,
# 12 x 0x38: 9 x {ID, ordinal}, the count at +0x24, the name at +0x26) has the game's data right after it, so the
# table is moved: every reach into it from the select screens (ovl_c) and the page editor (ovl_d) is pointed at
# roster_pages_tab, and the select screens' twelves read roster_pages_n (L / R, the next shown, the page list, Random,
# the Superstars' list). (The page editor's own loops and its save copy stay at the game's twelve: its pages.)
# The page list (func_801378B8) keeps a page in five bits (+0x60 & 0x1F) and lists them at 0x8015B8A0 (two
# bytes each; 128 there, used four players x 16 elsewhere): 32 pages at most.
PG = "roster_pages_tab"
PGN = "roster_pages_n"
def pg_n(r):
    """$r = roster_pages_n"""
    return ["lui $%s, %%hi(%s)" % (r, PGN), "lw $%s, %%lo(%s)($%s)" % (r, PGN, r)]
def pg_lt(dst, src):
    """slti $dst, $src, 12 -> $dst = $src < roster_pages_n ($at)"""
    return pg_n("at") + ["slt $%s, $%s, $at" % (dst, src)]
def pg_base(r, field=0):
    """addiu $r, $r, -0x6670 (+field) after lui $r, 0x800B: the table (its field) itself"""
    return ["lui $%s, %%hi(%s+%d)" % (r, PG, field), "addiu $%s, $%s, %%lo(%s+%d)" % (r, r, PG, field)]
def pg_count(dst, base):
    """lhu $dst, -0x664C($base), $base = 0x800B0000 + page * 0x38: that page's count"""
    s = dst if base == "at" else "at"          # (0x800B0000 in: the load's own register when $at is the base)
    return ["lui $%s, 0x800B" % s, "subu $at, $%s, $%s" % (base, s),          # (the page's place in the table)
            "lui $%s, %%hi(%s+0x24)" % (dst, PG), "addu $%s, $%s, $at" % (dst, dst),
            "lhu $%s, %%lo(%s+0x24)($%s)" % (dst, PG, dst)]
PATCHES += [
    dict(func="func_8010958C", why="pages: L / R through them all", edits={0x801095B8: pg_n("s3") + ["addiu $s3, $s3, -1"], 0x80109600: pg_lt("v0", "v0"), 0x80109638: pg_base("v1")}),
    dict(func="func_8010A458", why="pages: the table", edits={0x8010A5BC: pg_base("v1")}),
    dict(func="func_8010B66C", why="pages: the table", edits={0x8010BBA4: pg_base("v1")}),
    dict(func="func_8010BF54", why="pages: the next of them all", edits={0x8010C584: pg_lt("v0", "s0")}),
    dict(func="func_8010C650", why="pages: counts", edits={0x8010C684: pg_count("v0", "v0"), 0x8010C6E4: pg_count("v0", "v0")}),
    dict(func="func_80110FD0_ovl_c", why="pages: the next shown of them all", edits={0x80111018: pg_lt("v0", "v0"), 0x80111088: pg_base("v0")}),
    dict(func="func_80111158", why="pages: the table", edits={0x80111918: pg_base("a0")}),
    dict(func="func_8011F4AC", why="pages: names", edits={0x8011F540: pg_base("v0", 0x26)}),
    dict(func="func_80120D98_ovl_c", why="pages: names", edits={0x80120FE4: pg_base("a1", 0x26)}),
    dict(func="func_8012D2EC", why="pages: the table", edits={0x8012D380: pg_base("v1")}),
    # func_8013486C, the Royal Rumble's field from the pages (one from each page in turn, till there are as many as
    # the Rumble's NUMBER): every page (not the game's twelve: s5 % 12), and a round of them all that adds nobody
    # ends it -- the field is who there is (NUMBER made that many): with fewer Superstars on the pages than that
    # (select_pages.json) it went round for ever. roster_rr_fill_mark: the field's size at the last round's end.
    dict(func="func_8013486C", why="pages: counts; the Rumble's field from them all, and no more than there are",
         edits={0x80134A50: pg_count("v0", "v0"), 0x80134ACC: pg_count("v0", "v0"),
                0x80134A30: ["addiu $v0, $zero, -1", "lui $at, %hi(roster_rr_fill_mark)", "sw $v0, %lo(roster_rr_fill_mark)($at)", "lui $s7, 0x800C"],
                0x80134C04: ["addiu $s5, $s5, 1"] + pg_n("at") + ["slt $v0, $s5, $at", "bne $v0, $zero, 1f", "nop",
                             "addu $s5, $zero, $zero",                       # (round them all again: anyone added?)
                             "lui $at, %hi(roster_rr_fill_mark)", "lw $v0, %lo(roster_rr_fill_mark)($at)",
                             "bne $v0, $s3, 2f", "nop",
                             "lui $at, 0x800B", "j L_ovl_c_80134C44", "sh $s3, -0x6A74($at)",   # (nobody: NUMBER = the field)
                             "2:", "sw $s3, %lo(roster_rr_fill_mark)($at)",
                             "1:", "lui $v1, 0x800B", "lh $v1, -0x6A74($v1)", "slt $v1, $s3, $v1",
                             "bne $v1, $zero, L_ovl_c_80134A3C", "nop"],
                **{a: [] for a in range(0x80134C08, 0x80134C44, 4)}}),
    dict(func="func_801371EC", why="pages: counts", edits={0x8013721C: pg_count("v0", "v0"), 0x80137274: pg_count("v0", "v0"), 0x801372B0: pg_count("v0", "at")}),
    dict(func="func_80137318", why="pages: the table", edits={0x8013733C: pg_base("v1")}),
    dict(func="func_801378B8", why="pages: the table; the page list, them all", edits={0x80137958: pg_base("v0"), 0x80137A78: pg_lt("v0", "s0")}),
    dict(func="func_8013C5B0", why="pages: counts; the Superstars' list from them all", edits={0x8013C604: pg_count("v0", "v0"), 0x8013C654: pg_count("v0", "v0"), 0x8013C66C: pg_lt("v0", "s1")}),
    dict(func="func_8011929C", why="pages: names (the page editor)", edits={0x80119378: pg_base("t0", 0x26), 0x801193C4: pg_base("t0", 0x26)}),
    dict(func="func_80119658", why="pages: names (the page editor)", edits={0x801199D8: pg_base("v0", 0x26)}),
    dict(func="func_80120A74", why="pages: the table (the page editor)", edits={a: pg_base(r) for a, r in ((0x80120AA4, "t2"), (0x80120B28, "t2"), (0x80120BD0, "v1"), (0x80120C1C, "a0"), (0x80120C78, "v0"), (0x80120CC8, "a0"))}),
    dict(func="func_801213A0", why="pages: names (the page editor)", edits={0x80121408: pg_base("v0", 0x26)}),
    dict(func="func_801214C8", why="pages: the table (the page editor)", edits={0x801215BC: pg_count("v0", "v0"), 0x801215C4: pg_base("v1"), 0x80121620: pg_base("t2")}),
    dict(func="func_801221C4", why="pages: names (the page editor)", edits={0x80122268: pg_base("v0", 0x26)}),
    dict(func="func_80136244", why="pages: the table (the page editor)", edits={0x8013626C: pg_base("a2")}),
]
find("func_8010A9E8")["edits"][0x8010AA54] = pg_lt("v0", "s1")   # (Random: a Superstar on any of the pages)

# Costume variants (src/variants.c; the user, 2026-10-31: Undertaker 1999 in the Undertaker's box, reached with the Type
# arrows). The Type arrows (C-left / C-right, func_80111158 0x80111974): func_8010A960(id, costume, step) -> the costume;
# variants.c's sel_type_step(id, costume, step, row) in its place goes on into the box's variants (the row's Superstar made
# his). Random (func_8010A9E8): the Superstar it drew and his page to variants.c (one of his family at random).
find("func_80111158")["edits"][0x801119C4] = ["jal sel_type_step", "move $a3, $s2"]   # (a3: the box's row)
find("func_80111158")["edits"][0x801119C8] = []                                      # (its delay slot, a nop: in the line above)
find("func_8010A9E8")["edits"][0x8010AB84] = ["move $s3, $v0", "move $a0, $s0", "jal variant_random_pick", "move $a1, $s1",
                                              "move $v0, $s3", "lw $ra, 0x20($sp)"]   # (s3: restored right after)

# ---- Each arena its own (src/arena.c; mod/NOTES.md "Arenas restructured", 2026-11-01) ------------------------------------
# The arena builders read their lists, per-venue entries, close scene and rope colours through `lui rX, 0x8005` + an
# offset (the game's arena data, 0x80050040..0x80052890). Each such lui becomes the arena's own base: arena_lists (the
# ring, crowd, truss, aprons, ropes) or arena_close (the entrances' close scene) -- 0x80050000 for the game's arenas (their
# own data, read in place), the arcade arena's own block for one of ours -- the offsets after it as they were. (Found
# with xref.py / luis.py over the six-man engine's code: every one of these luis addresses only arena data; the table /
# steps lists, 0x80050098 / 0x80050338.. -- the shared props -- and 0x80052850 are left the game's.)
def base_lui(reg, sym):
    return ["lui $%s, %%hi(%s)" % (reg, sym), "lw $%s, %%lo(%s)($%s)" % (reg, sym, reg)]
ARENA_LUIS = [
    ("func_8001B548", "arena_lists", [(0x8001B5F0, "a1"), (0x8001B5F8, "a2"), (0x8001B618, "a2"), (0x8001B624, "a1"), (0x8001B644, "a1"), (0x8001B64C, "a2"), (0x8001B6C0, "a1"), (0x8001B6C8, "a2"), (0x8001B6E8, "a2"), (0x8001B6F4, "a1"), (0x8001B728, "a1"), (0x8001B730, "a2"), (0x8001B870, "s5"), (0x8001B878, "s4"), (0x8001B950, "fp"), (0x8001B958, "s7"), (0x8001B968, "s6")]),
    ("func_8001C62C", "arena_lists", [(0x8001C6F0, "a1"), (0x8001C704, "a1"), (0x8001C70C, "a2"), (0x8001C78C, "a2"), (0x8001C798, "a1"), (0x8001C7B0, "a2"), (0x8001C7BC, "a1"), (0x8001C830, "a1"), (0x8001C83C, "a1"), (0x8001C844, "a2"), (0x8001C878, "s5"), (0x8001C880, "s4"), (0x8001C924, "fp"), (0x8001C92C, "s7"), (0x8001C934, "s6")]),
    ("func_8001CD34", "arena_lists", [(0x8001CD54, "a1"), (0x8001CD8C, "a2"), (0x8001CDAC, "a1"), (0x8001CDB4, "a2"), (0x8001CDE8, "a1"), (0x8001CDF4, "a2"), (0x8001CE00, "a3"), (0x8001CE30, "a1"), (0x8001CE3C, "a2"), (0x8001CE64, "a1"), (0x8001CE6C, "a2"), (0x8001CE9C, "a2"), (0x8001CEA8, "a1"), (0x8001CED0, "a1"), (0x8001CED8, "a2")]),
    ("func_8001D354", "arena_lists", [(0x8001D3E0, "a2")]),
    ("func_8001D4D4", "arena_lists", [(0x8001D520, "s7"), (0x8001D53C, "a1"), (0x8001D544, "a2"), (0x8001D598, "a2"), (0x8001D5C0, "a1"), (0x8001D5CC, "a2"), (0x8001D5D8, "a3"), (0x8001D644, "a2"), (0x8001D650, "a1"), (0x8001D6A8, "a1"), (0x8001D6F4, "s2"), (0x8001D73C, "a1"), (0x8001D788, "s2"), (0x8001D7D0, "a1"), (0x8001D81C, "s2"), (0x8001D864, "a1")]),
    ("func_80021980", "arena_lists", [(0x800219B8, "v1"), (0x800219C8, "v0"), (0x80021A04, "v0"), (0x80021A10, "a2")]),
    ("func_8001F088", "arena_close", [(0x8001F0BC, "s4"), (0x8001F114, "a1"), (0x8001F120, "a2"), (0x8001F12C, "a3")]),
    ("func_8001F2D8", "arena_close", [(0x8001F3BC, "a1"), (0x8001F3D8, "a1"), (0x8001F3E4, "a2"), (0x8001F3F4, "a3"), (0x8001F4A8, "a1"), (0x8001F4BC, "a2"), (0x8001F4C8, "a3"), (0x8001F4F8, "a1"), (0x8001F504, "a2"), (0x8001F510, "a3"), (0x8001F5BC, "at"), (0x8001F5D0, "v1"), (0x8001F5F0, "s7"), (0x8001F5F8, "s5"), (0x8001F704, "at"), (0x8001F768, "at"), (0x8001F77C, "v1"), (0x8001F79C, "s7"), (0x8001F7A4, "s5"), (0x8001F8B0, "at"), (0x8001F8F0, "s7"), (0x8001F8F8, "fp"), (0x8001F96C, "t0"), (0x8001FA98, "at"), (0x8001FAD4, "t0"), (0x8001FB84, "t0"), (0x8001FBD4, "t0"), (0x8001FC2C, "at"), (0x8001FC40, "at"), (0x8001FC58, "at"), (0x8001FC8C, "t0"), (0x8001FCE0, "t0"), (0x8001FD90, "t0"), (0x8001FDE4, "t0"), (0x8001FE64, "t0"), (0x8001FEC4, "at"), (0x8001FEF0, "at"), (0x8001FF1C, "at"), (0x8001FF54, "at")]),
    ("func_8001FFA8", "arena_close", [(0x8002001C, "a0"), (0x80020030, "at"), (0x800200A0, "a0"), (0x800200B4, "at"), (0x80020524, "s3"), (0x800205CC, "at"), (0x800205FC, "at"), (0x80020638, "at"), (0x800206DC, "at"), (0x8002070C, "at"), (0x80020748, "at"), (0x8002086C, "at"), (0x80020898, "at"), (0x800208C4, "at"), (0x800208FC, "at")]),
    ("func_80020940", "arena_close", [(0x8002096C, "a2"), (0x80020990, "at"), (0x800209B4, "a2"), (0x80020A48, "v1"), (0x80020A54, "v0"), (0x80020AC4, "v1"), (0x80020AD0, "v0")]),
    ("func_80020B58", "arena_close", [(0x80020C08, "a2"), (0x80020C2C, "s0"), (0x80020C44, "a1"), (0x80020C54, "a1"), (0x80020C60, "a2"), (0x80020CEC, "at"), (0x80020D00, "s0"), (0x80020D18, "a2"), (0x80020D50, "a2"), (0x80020D5C, "a3"), (0x80020D80, "a1"), (0x80020D8C, "a2"), (0x80020D98, "a3"), (0x80020E10, "v1"), (0x80020E1C, "v0"), (0x80020E8C, "at"), (0x80020EA0, "v1"), (0x80020EB4, "s7"), (0x80020EDC, "t0"), (0x80020F7C, "at"), (0x80020FDC, "v1"), (0x80020FE8, "v0"), (0x80021058, "at"), (0x8002106C, "v1"), (0x80021080, "s7"), (0x800210A8, "t0"), (0x80021148, "at"), (0x800211C4, "at"), (0x800211DC, "v0"), (0x8002123C, "at"), (0x80021248, "v1"), (0x80021268, "t0"), (0x800212C8, "t0"), (0x8002130C, "t0"), (0x80021344, "v1"), (0x80021358, "v0"), (0x800213B8, "v0"), (0x800213D4, "at"), (0x80021410, "v0"), (0x8002142C, "v0"), (0x80021514, "at"), (0x80021524, "at"), (0x8002153C, "at"), (0x80021650, "at"), (0x80021670, "v0"), (0x80021690, "v0"), (0x800216BC, "v0")]),
    ("func_8002173C", "arena_close", [(0x80021768, "v0"), (0x8002179C, "at"), (0x800217D0, "at"), (0x8002180C, "at"), (0x80021818, "a0"), (0x80021870, "at"), (0x8002187C, "a0")]),
    ("func_8001B3E4", "arena_close", [(0x8001B428, "at"), (0x8001B438, "at"), (0x8001B448, "at"), (0x8001B458, "at")]),
    ("func_8001B220", "arena_close", [(0x8001B298, "at"), (0x8001B2AC, "at"), (0x8001B2BC, "at"), (0x8001B2CC, "at")]),
]
for _fn, _sym, _sites in ARENA_LUIS:
    PATCHES.append(dict(func=_fn, why="the arena's own data (%s, arena.c)" % _sym, edits={a: base_lui(r, _sym) for a, r in _sites}))

# The stage set: its parts in arena.c's arena_set_parts (ARENA_SET_MAX -- the game's array 0x8007B8A0 has room for 15),
# the arena's lists and count (arena_set_mesh / pal / n), the count freed the count built (arena_set_built), the set's
# node at the arena's z (arena_set_z: the game's 1500.0, Revenge's sets 0).
SET_BUILT = ["lui $at, %hi(arena_set_built)", "sw $a3, %lo(arena_set_built)($at)"]
PATCHES += [
    dict(func="func_8001D8EC", why="the stage set: the arena's (arena.c)", edits={
        0x8001D8FC: "lui $s1, %hi(arena_set_parts)", 0x8001D900: "addiu $s1, $s1, %lo(arena_set_parts)",
        0x8001D914: "lui $a1, %hi(arena_set_mesh)", 0x8001D918: [], 0x8001D91C: "lw $a1, %lo(arena_set_mesh)($a1)",
        0x8001D920: "lui $a2, %hi(arena_set_pal)", 0x8001D924: [], 0x8001D928: "lw $a2, %lo(arena_set_pal)($a2)",
        0x8001D930: "lui $a3, %hi(arena_set_n)", 0x8001D934: [], 0x8001D938: ["lw $a3, %lo(arena_set_n)($a3)"] + SET_BUILT,
        0x8001D974: "lui $at, %hi(arena_set_z)", 0x8001D978: "lw $at, %lo(arena_set_z)($at)"}),
    dict(func="func_8001DA24", why="the stage set freed: the count built (arena.c)", edits={
        0x8001DA40: "lui $a2, %hi(arena_set_built)", 0x8001DA44: [], 0x8001DA48: "lw $a2, %lo(arena_set_built)($a2)",
        0x8001DA4C: "lui $a0, %hi(arena_set_parts)", 0x8001DA54: "addiu $a0, $a0, %lo(arena_set_parts)"}),
    dict(func="func_8001DA6C", why="the stage set made again: the arena's (arena.c)", edits={
        0x8001DA94: "lui $a2, %hi(arena_set_built)", 0x8001DA98: [], 0x8001DA9C: "lw $a2, %lo(arena_set_built)($a2)",
        0x8001DAA0: "lui $s1, %hi(arena_set_parts)", 0x8001DAA4: "addiu $s1, $s1, %lo(arena_set_parts)",
        0x8001DABC: "lui $a1, %hi(arena_set_mesh)", 0x8001DAC0: [], 0x8001DAC4: "lw $a1, %lo(arena_set_mesh)($a1)",
        0x8001DAC8: "lui $a2, %hi(arena_set_pal)", 0x8001DACC: [], 0x8001DAD0: "lw $a2, %lo(arena_set_pal)($a2)",
        0x8001DAD8: "lui $a3, %hi(arena_set_n)", 0x8001DADC: [], 0x8001DAE0: ["lw $a3, %lo(arena_set_n)($a3)"] + SET_BUILT}),
]
# The announce table and the steps: the game leaves them out when the match's rules have 0x100000 or it's the cage
# (func_8001DB4C makes them, 8001E488 hides them, 8001DF38 runs them each frame): an arena without them
# (arena_no_table) takes the same way. 8001B0B8 (puts the table back after the entrances) and 8001B08C (the table
# breaks) do nothing then.
NO_TABLE = ["and $v0, $v0, $v1", "lui $at, %hi(arena_no_table)", "lw $at, %lo(arena_no_table)($at)", "or $v0, $v0, $at"]
RET_NO_TABLE = ["lui $at, %hi(arena_no_table)", "lw $at, %lo(arena_no_table)($at)", "beq $at, $zero, 1f", "nop", "jr $ra", "nop", "1:"]
PATCHES += [
    dict(func="func_8001DB4C", why="no table in an arena without one (arena.c)", edits={0x8001DB98: NO_TABLE}),
    dict(func="func_8001E488", why="no table in an arena without one (arena.c)", edits={0x8001E4D4: NO_TABLE}),
    dict(func="func_8001DF38", why="no table in an arena without one (arena.c)", edits={0x8001DF64: NO_TABLE}),
    dict(func="func_8001B0B8", why="no table in an arena without one (arena.c)", edits={0x8001B0B8: RET_NO_TABLE + ["addiu $sp, $sp, -0x28"]}),
    dict(func="func_8001B08C", why="no table in an arena without one (arena.c)", edits={0x8001B08C: RET_NO_TABLE + ["addiu $v0, $zero, 0x1"]}),
]
# The collision (the match overlay ovl_e): an arena's own walls. The ring's row (0) of the zone table 0x8014D290
# (func_8012CB8C, 8012D58C: [quadrant] -> its zone list) -> arena_zone_q; row 0 of the floor zones 0x8014DA6C
# (8012DDD8) -> arena_floor_zones; func_80140FD4's eight zone lists (0x80147BB4..0x80147C24, compared and switched
# by pointer) -> arena_zone_fix[8]. 0: the game's (its own arenas, and an arena with its table).
def ZQ(row):
    return ["addiu $a0, $a0, -0x2D70", "bne $%s, $zero, 1f" % row, "lui $at, %hi(arena_zone_q)",
            "lw $at, %lo(arena_zone_q)($at)", "beq $at, $zero, 1f", "nop", "move $a0, $at", "1:"]
_ZF = [(0x80141168, 0), (0x80141178, 4), (0x80141188, 1), (0x80141198, 5), (0x801411A8, 2), (0x801411B8, 6), (0x801411C8, 3), (0x801411D8, 7)]
_zf_edits = {}
for _a, _k in _ZF:
    _zf_edits[_a] = "lui $v0, %%hi(arena_zone_fix+%d)" % (4 * _k)
    _zf_edits[_a + 4] = "lw $v0, %%lo(arena_zone_fix+%d)($v0)" % (4 * _k)
PATCHES += [
    dict(func="func_8012CB8C", why="the ring's walls: the arena's own (arena.c)", edits={0x8012CFE8: ZQ("s0")}),
    dict(func="func_8012D58C", why="the ring's walls: the arena's own (arena.c)", edits={0x8012D70C: ZQ("s0")}),
    dict(func="func_8012DDD8", why="the floor's zones: the arena's own (arena.c)", edits={0x8012DE5C: [
        "bne $v0, $zero, 1f", "lw $v0, -0x2594($at)", "lui $at, %hi(arena_floor_zones)", "lw $at, %lo(arena_floor_zones)($at)",
        "beq $at, $zero, 1f", "nop", "move $v0, $at", "1:"]}),
    dict(func="func_80140FD4", why="the floor's zone lists: the arena's own (arena.c)", edits=_zf_edits),
]

# ---- Step 4 (mod/NOTES.md "Arenas restructured"): what the game reads by the venue byte outside the arena data, and the
# announce table -- each arena's own (arena.c arena_font_tab / arena_close_tab / arena_table, gen/arenas.py write_tables)
PATCHES += [
    dict(func="func_8000A470", why="the nameplate font: the arena's own (arena.c arena_font_tab)", edits={
        0x8000A4B4: base_lui("a1", "arena_font_tab"), 0x8000A4BC: "lhu $a1, 0x0($a1)"}),
    dict(func="func_80109F88", why="ovl_b's close-shot byte: the arena's own (arena.c arena_close_tab)", edits={
        0x8010A6B8: base_lui("at", "arena_close_tab"), 0x8010A6C0: "lbu $v0, 0x0($at)"}),
]
# (2026-11-18, valetfix) The same function's test of the video step (func_8010C998 with the entrant's video): with FULL
# SCREEN TITANTRONS off the game goes past the step itself and sets the stage step up with its own code -- the walk
# paths with them: a valet on her own path, beside and behind her man (logic.c entrance_video_step). The delay slot,
# a0 = the video, stays.
find("func_80109F88")["edits"][0x8010A070] = "jal entrance_video_step"
_TABLE_LUIS = [("func_8001DB4C", [(0x8001DCC8, "s5"), (0x8001DCD0, "s4"), (0x8001DCDC, "at"), (0x8001DCE8, "at"), (0x8001DCFC, "at"), (0x8001DDDC, "t0"), (0x8001DDE4, "fp")]),
               ("func_8001DF38", [(0x8001E094, "a1"), (0x8001E0A0, "a2"), (0x8001E0AC, "a3"), (0x8001E104, "a1"), (0x8001E10C, "a2"), (0x8001E114, "a3"), (0x8001E168, "a1"), (0x8001E170, "a2"), (0x8001E178, "a3"), (0x8001E19C, "at"), (0x8001E1CC, "a1"), (0x8001E1D4, "a3")]),
               ("func_8001E488", [(0x8001E5B0, "s5"), (0x8001E5B8, "s4"), (0x8001E66C, "t1"), (0x8001E674, "fp")]),
               ("func_8001B0B8", [(0x8001B0E4, "a1"), (0x8001B0F0, "a2"), (0x8001B0FC, "a3")])]
for _fn, _sites in _TABLE_LUIS:
    find(_fn)["edits"].update({a: base_lui(r, "arena_table") for a, r in _sites})

# ---- Step 4b: each arena's own walls (gen/arenas_walls.py, arena.c) -- the zone lists the code names by address: the
# table's top (0x80147E20: 8012CB8C / 8012D58C / 8012D82C), the table's planes (0x80147E28 / 0x80147E30: 8011B5B0,
# 8011E748, 8011EBC0, 8011FDE8), the aisle's (0x80147408: 800EB0DC), the far plane's (0x80147B7C: 800EB334, 8011FDE8),
# the table top's record (0x80147E38: 80139D80); the table broken (8012DCC0 case 2: the arena's own relink)
def gpair(lui_at, reg, sym):
    return {lui_at: "lui $%s, %%hi(%s)" % (reg, sym), lui_at + 4: "lw $%s, %%lo(%s)($%s)" % (reg, sym, reg)}
_ZONE_SITES = [("func_8012CB8C", 0x8012D010, "s2", "arena_zone_top"), ("func_8012D58C", 0x8012D730, "s2", "arena_zone_top"),
               ("func_8012D82C", 0x8012D9C4, "s2", "arena_zone_top"),
               ("func_8011B5B0", 0x8011B7B8, "v0", "arena_zone_t28"), ("func_8011B5B0", 0x8011B7D8, "v0", "arena_zone_t30"),
               ("func_8011E748", 0x8011E954, "v0", "arena_zone_t28"), ("func_8011E748", 0x8011E970, "v0", "arena_zone_t30"),
               ("func_8011EBC0", 0x8011EEA0, "v0", "arena_zone_t28"), ("func_8011EBC0", 0x8011EEBC, "v0", "arena_zone_t30"),
               ("func_8011FDE8", 0x8011FFE4, "v0", "arena_zone_t28"), ("func_8011FDE8", 0x80120000, "v0", "arena_zone_t30"),
               ("func_8011FDE8", 0x80120074, "v0", "arena_zone_plane"), ("func_800EB334", 0x800EB4D0, "v0", "arena_zone_plane"),
               ("func_800EB0DC", 0x800EB1AC, "v0", "arena_zone_aisle"), ("func_80139D80", 0x80139DEC, "a2", "arena_rec_top")]
for _fn, _a, _r, _sym in _ZONE_SITES:
    _d = next((p for p in PATCHES if p["func"] == _fn), None)
    if _d is None: _d = dict(func=_fn, why="the arena's own walls (arena.c)", edits={}); PATCHES.append(_d)
    if _a == 0x8012D730 or _a == 0x8012D9C4:          # (its addiu the delay slot of the j after the lui)
        _d["edits"].update({_a: "lui $%s, %%hi(%s)" % (_r, _sym), _a + 8: "lw $%s, %%lo(%s)($%s)" % (_r, _sym, _r)})
    else: _d["edits"].update(gpair(_a, _r, _sym))
PATCHES.append(dict(func="func_8012DCC0", why="the table broken: the arena's own walls relinked (arena.c arena_brk_*)", edits={
    0x8012DD30: "lui $v0, %hi(arena_brk_val)", 0x8012DD34: "lw $v0, %lo(arena_brk_val)($v0)",
    0x8012DD38: "lui $at, %hi(arena_brk_at)", 0x8012DD3C: ["lw $at, %lo(arena_brk_at)($at)", "sw $v0, 0x0($at)"],
    0x8012DD40: ["lui $at, %hi(arena_brk_zone)", "lw $at, %lo(arena_brk_zone)($at)"], 0x8012DD48: "sw $zero, 0x0($at)"}))

# ---- Step 5: ovl_b's entrance effect sets and ovl_c's VS-screen pieces, the arena's own (arena.c arena_fx_tab / arena_vs_*)
PATCHES += [
    dict(func="func_8011D2F8", why="the entrance effect sets: the arena's own (arena.c arena_fx_tab)", edits={
        0x8011D404: "lui $s4, %hi(arena_fx_tab)", 0x8011D408: "lw $s4, %lo(arena_fx_tab)($s4)"}),
    dict(func="func_80116194", why="the VS screen's pieces: the arena's own (arena.c arena_vs_ptrs)", edits={
        0x801161F8: base_lui("s2", "arena_vs_ptrs"), 0x80116200: "lw $s2, 0x0($s2)"}),
    dict(func="func_8011649C", why="the VS screen's values: the arena's own (arena.c arena_vs_a / _b)", edits={
        0x801165F0: "lui $a0, %hi(arena_vs_b)", 0x801165F4: "lw $a0, %lo(arena_vs_b)($a0)",
        0x80116608: "lui $a0, %hi(arena_vs_a)", 0x8011660C: "lw $a0, %lo(arena_vs_a)($a0)"}),
]

# ---- RAW IS WAR: the ring and the stage one area (data/ringstage/RINGSTAGE.md, src/arena_ringstage.c). With ringstage_on off
# (every other arena) each of these does what the game's did.
def _rs_lh(lui_at, lh_at, off):
    return {lui_at: ["lui $v0, %hi(arena_arrive)", "lw $v0, %lo(arena_arrive)($v0)"], lh_at: "lh $v0, 0x%X($v0)" % off}
RINGSTAGE_PATCHES = [
    dict(func="func_8012F9CC", why="a man's pose: lifted by the ground under him (y stays the game's 0 / 90)", edits={
        0x8012F9E4: ["sw $a1, 0x3C($sp)", "sw $a2, 0x40($sp)", "addu $a0, $s0, $zero", "jal ringstage_lift", "nop",
                     "lui $at, %hi(ringstage_h)", "swc1 $f0, %lo(ringstage_h)($at)", "lwc1 $f2, 0x100($s0)", "add.s $f0, $f0, $f2",
                     "lw $a1, 0x3C($sp)", "lw $a2, 0x40($sp)"],
        0x8012FA58: ["lwc1 $f0, 0x24($sp)", "lui $at, %hi(ringstage_h)", "lwc1 $f2, %lo(ringstage_h)($at)", "sub.s $f0, $f0, $f2"]}),
    dict(func="func_8013140C", why="the replay's pose: the same lift", edits={
        0x80131638: ["addiu $a0, $sp, 0x20", "jal ringstage_replay_lift", "nop", "lhu $v0, 0xA($s1)"]}),
    dict(func="func_801166B8", why="the count-out: none past the old door line (as in the stage area)", edits={
        0x801167FC: ["lui $at, %hi(arena_count_z)", "lwc1 $f2, %lo(arena_count_z)($at)", "lui $at, %hi(arena_man_z0)",
                     "lw $at, %lo(arena_man_z0)($at)", "addu $at, $at, $v1", "lwc1 $f0, 0x0($at)", "c.lt.s $f0, $f2", "nop",
                     "bc1f L_ovl_e_80116804", "nop", "bnel $a0, $zero, L_ovl_e_80116810"]}),
    # (Ultimate No Mercy, the stages agent 2026-11-20) In the six-man build the game's func_800EDCF4 is a tail call to the
    # engine's own copy, rw_800EDCF4 (0x80780E90): the edit this patch had, at 0x800EDE04 (the door code 0x2000's "+ 10"),
    # was in the dead rest of the function and never ran -- backstage's way back went to the start venue's old stage area.
    # Now the tail call goes through arena_ringstage.c ringstage_area_change, which calls rw_800EDCF4 and puts it right.
    dict(func="func_800EDCF4", why="the way back from backstage: the ring's world where the stage is one area with it", edits={
        0x800EDCF4: ["addiu $sp, $sp, -0x18", "sw $ra, 0x14($sp)", "jal ringstage_area_change", "nop", "lw $ra, 0x14($sp)",
                     "jr $ra", "addiu $sp, $sp, 0x18"]}),
    dict(func="func_80115000", why="the arrival spots: the arena's (RAW's index 0: the door at the back of its stage)",
         edits=merge(_rs_lh(0x80115140, 0x80115148, 0), _rs_lh(0x8011515C, 0x80115164, 2), _rs_lh(0x8011517C, 0x80115184, 4))),
    dict(func="func_80014494", why="a man's shadow on RAW's ramp and deck: the ground he's lifted by", edits={
        0x800144F4: ["jal ringstage_shadow_call"]}),
    dict(func="func_80116CDC", why="a story script's count-out: who's out, at the count", edits={
        0x80117018: ["jal story_co_call"]}),
    dict(func="func_800E1E60", why="a man's shadow on RAW's ramp and deck: the ground he's lifted by", edits={
        0x800E1EA0: ["jal ringstage_cam_call"], 0x800E1F8C: ["nop"], 0x800E1F94: ["jal ringstage_shadow"]}),
    dict(func="func_80110D7C", why="a door wall: RAW's opens only with someone else on the stage", edits={
        0x80110E80: ["jal ringstage_door_open", "nop", "beq $v0, $zero, L_ovl_e_80110F1C", "addu $a0, $s0, $zero", "jal func_800EDCF4"]}),
    dict(func="func_800F03F0", why="the CPU's way between the ring's side, the aisle and the deck", edits={
        0x800F07C8: ["jal ringstage_ai", "nop", "lui $t0, 0x8017", "lw $t0, -0x4718($t0)", "lw $v1, 0x2E4($t0)"],
        0x800F0CF0: ["jal ringstage_ai_post", "nop", "lw $ra, 0x38($sp)"]}),
    dict(func="func_80142B28", why="a weapon in reach: its real height against the man's as he's drawn", edits={
        0x80142C40: ["lwc1 $f0, 0x100($a0)", "lh $t9, 0x9E($a0)", "sll $t9, $t9, 2", "lui $at, %hi(h_now)", "addu $at, $at, $t9",
                     "lwc1 $f18, %lo(h_now)($at)", "add.s $f0, $f0, $f18"]}),
    dict(func="func_80141508", why="a weapon picked up: its real height against the man's as he's drawn", edits={
        0x80141618: ["lwc1 $f2, 0x100($s1)", "lh $t9, 0x9E($s1)", "sll $t9, $t9, 2", "lui $at, %hi(h_now)", "addu $at, $at, $t9",
                     "lwc1 $f18, %lo(h_now)($at)", "add.s $f2, $f2, $f18"]}),
    dict(func="func_800F6154", why="the CPU's back-to-the-ring: not with his man up the aisle", edits={
        0x800F6174: "lui $v0, %hi(arena_ai_area)", 0x800F6178: "lw $v0, %lo(arena_ai_area)($v0)"}),
    # (Ultimate No Mercy, the stages agent 2026-11-20) the CPU's look ahead outside the ring (0x8014DA2C[row], row 0): the
    # arena's own walls (arena.c arena_zone_look), not the game's with its record 10 across the aisle at z 800. At 0x8012D9A8
    # v0 is the row: row 0 and an arena list -> s2 = it, on to 0x8012DA20 as the game's own load goes; else the game's code.
    dict(func="func_8012D82C", why="the CPU's look ahead outside the ring: the arena's own walls (arena.c arena_zone_look)", edits={
        0x8012D9A8: ["bne $v0, $zero, 1f", "lui $at, %hi(arena_zone_look)", "lw $at, %lo(arena_zone_look)($at)",
                     "beq $at, $zero, 1f", "nop", "j L_ovl_e_8012DA20", "move $s2, $at", "1:", "sll $v0, $v0, 2"]}),
]
for _p in RINGSTAGE_PATCHES:
    _d = next((p for p in PATCHES if p["func"] == _p["func"]), None)
    if _d is None: PATCHES.append(_p)
    else: _d["edits"].update(_p["edits"])
