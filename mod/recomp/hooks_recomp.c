// WWF No Mercy: Recompiled platform layer: runtime hooks, keyboard and mod options.
#include "modding.h"
#include "recomp_api.h"
#include "recomp_input.h"
#include "arcade.h"
#include "skel.h"

// recomp_set_config_u32 (this port's): a mod's option is set and saved as the Mods menu would.
RECOMP_IMPORT("*", void recomp_set_config_u32(const char* key, unsigned long value));

// The two modes (2026-11-07): until now every hook below asked the option "enabled" (read live, each call) and with it
// off the mod did almost nothing -- no settings, no F2, no commentary, Titantron or HD men. Now the mod always runs:
// the enhancements in both modes, the coin-op only in Arcade. Which parts are the arcade's is decided in logic.c and
// gameplay.c (nm_arcade(): the mode latched once at start-up, see logic.c "the game's two modes"); the hooks here
// just call through. "enabled" is now read only once, by the settings' migration (an old install's arcade on/off
// becomes its game_mode).

// Frame task submit (gfx thread): the mod's per-frame logic + its drawing. Its argument is the frame task. Both modes:
// arcade_frame runs the universal services and returns before the coin-op's part in Regular.
RECOMP_HOOK("func_800012B0") void arcade_on_frame(u32 task) {
    arcade_frame(task);
}

// The frame's display list (func_80002FA4): func_80002BB4 starts it, func_800028B4 draws the scene
// into it, func_80002DE4 ends it (full sync) and sends it. The arcade's drawing is part of the
// frame: called from the game's list where the scene ends (draw.c, draw_scene_end).
// func_80012BE0, the wrestlers' skeletons: the game's translated function no longer runs. Native (exe src/main/arcade_bones.cpp), proved beside the original first: every value, to the bit: QA 2026-11-04: 4,000 skeletons in a match with the full-detail men, 40,000 (19 million values) in a 6-slot Rumble, none different.
RECOMP_IMPORT("*", void recomp_arcade_bones(s32 slot));
RECOMP_PATCH void func_80012BE0(s32 slot) { recomp_arcade_bones((s16)slot); }
// The animation player's bones: func_80013234 (a key, straight on) and func_800132D8 (a key, a share of the way), with func_800135D0 inside them, which so no longer runs: native (arcade_bones.cpp). Proved beside the originals first, to the bit. QA 2026-11-04: 600,000 keys and 40,000 frames of animation in a 6-slot Rumble, 4,000 in a match with the full-detail men, none different.
extern void func_80012B24(s32 slot);
RECOMP_IMPORT("*", void recomp_arcade_anim_step(s32 slot));
RECOMP_IMPORT("*", void recomp_arcade_anim_blend(s32 slot, s32 n));
RECOMP_PATCH void func_80013234(s32 slot) { recomp_arcade_anim_step((s16)slot); }
RECOMP_PATCH void func_800132D8(s32 slot, s32 n) { func_80012B24((s16)slot); recomp_arcade_anim_blend((s16)slot, (s16)n); }
// func_80012F48, easing into a new move: native too (arcade_bones.cpp). Proved beside the original the same way: 4,000 in a 6-slot Rumble, none different.
RECOMP_IMPORT("*", void recomp_arcade_anim_settle(s32 slot, s32 n, s32 flags));
RECOMP_PATCH void func_80012F48(s32 slot, s32 n, s32 flags) { recomp_arcade_anim_settle((s16)slot, (s16)n, (s8)flags); }
// The animation driver, func_80012130: native (arcade_bones.cpp drive). What it calls stays what it was where the mod has its own (func_80011A54 the moves in the pack, func_80002268 the checked matrices) or where it reaches the disk (func_80012A8C, func_80013860, func_80012B24, func_80013EB0). Proved whole first, by the twin (the original kept in place, the rewrite on a copy of the 8 MB, every word compared): 8,700 runs in a match and a 6-slot Rumble, none different; 2,891 move starts among them. To prove it again: take this patch out and bring back the twin's hooks (see NOTES 0.39.7).
RECOMP_IMPORT("*", void recomp_arcade_drive(u32 args));
RECOMP_PATCH void func_80012130(s32 a0, s32 a1, s32 a2, u32 a3, u32 p5, s32 flags, s32 rx, s32 rz) { s32 a[8]; a[0] = a0; a[1] = a1; a[2] = a2; a[3] = (s32)a3; a[4] = (s32)p5; a[5] = flags; a[6] = rx; a[7] = rz; recomp_arcade_drive((u32)a); }
RECOMP_IMPORT("*", void recomp_arcade_view_hd_pack(int man, u32 addr));
RECOMP_IMPORT("*", void recomp_arcade_view_frame(u32 end));
RECOMP_IMPORT("*", void recomp_arcade_view_begin(void));
void plat_view_hd_pack(int man, u32 addr) { recomp_arcade_view_hd_pack(man, addr); }
RECOMP_HOOK("func_80002BB4") void arcade_on_frame_list(u32 picture, s32 clear) {
    if ((s16)clear) recomp_arcade_view_begin();     // (the native window: the frame before is whole)
    dl_big_begin();     // (the frame built in the big lists: draw.c)
    draw_frame_begin(picture); tron_frame_begin(picture);   // (the mod's drawing and the Titantron: both modes)
}
RECOMP_HOOK("func_80002DE4") void arcade_on_frame_list_end(u32 a0, u32 head, s32 a2, s32 a3) { recomp_arcade_view_frame(head); dl_big_end(); }    // (head: where the list's end goes: the native window reads it up to there)
RECOMP_HOOK_RETURN("func_80002DE4") void arcade_on_frame_list_sent(void) { dl_big_done(); }
// The graphics thread takes the frame (func_80002730: the scene's items made groups, their matrices fixed, for the
// slot): the Titantron's hard camera made for it too (tron.c).
RECOMP_IMPORT("*", void recomp_arcade_view_take(void));
RECOMP_IMPORT("*", void recomp_arcade_view_tron(u32 vp, u32 items));
void plat_view_tron(u32 vp, u32 items) { recomp_arcade_view_tron(vp, items); }
// (2026-11-11) The entrant's own theme / Titantron from common files: the exe answers which native parts play (arcade_media.cpp).
RECOMP_IMPORT("*", int recomp_arcade_entrance_media(u32 id, u32 variant, int on));
int plat_entrance_media(u32 id, u32 variant, int on) { return recomp_arcade_entrance_media(id, variant, on); }
RECOMP_HOOK("func_80002730") void arcade_on_frame_take(s32 slot) {
    extern void hd_frame(s32);
    recomp_arcade_view_take();     // (the native picture: the camera the items' matrices are made with)
    hd_frame(slot); tron_hardcam_build(slot);       // (the HD men, the Titantron's hard camera: both modes)
}
RECOMP_HOOK_RETURN("func_80002730") void arcade_on_frame_taken(void) {
    extern void hd_frame_done(void);
    hd_frame_done();
}
// func_800028B4, which turns the scene's draw items into graphics commands each frame: the game's own translated function no longer runs. Rewritten native (exe src/main/arcade_scene.cpp) and proved beside the original first:
// QA 2026-11-04, every word compared: 22,000 lists in a match with the full-detail men, 38,000 (22.7 M words) in a 6-slot Royal Rumble, none different. (#define SCENE_CHECK at the top of this file: the check again.)
#ifndef SCENE_CHECK
RECOMP_IMPORT("*", int recomp_arcade_scene_emit(u32 gfx, s32 slot, s32 first));
RECOMP_PATCH s32 func_800028B4(u32 gfxp, s32 a1, s32 a2) {
    tron_scene_begin(gfxp, a1, a2);                 // (the Titantron and the mod's drawing in the scene: both modes)
    s32 r = recomp_arcade_scene_emit(gfxp, a1, a2);
    tron_scene_end(gfxp); draw_scene_end(gfxp);
    return r;
}
#else   // (the check: the original runs, this beside it, every word compared: exe recomp_arcade_scene_check)
static u32 scene_gfxp, scene_start;
static s32 scene_a1, scene_a2;
RECOMP_IMPORT("*", void recomp_arcade_scene_check(u32 start, u32 end, s32 slot, s32 first));
RECOMP_HOOK("func_800028B4") void arcade_on_scene(u32 gfxp, s32 a1, s32 a2) {
    scene_gfxp = gfxp; scene_a1 = a1; scene_a2 = a2;
    tron_scene_begin(gfxp, a1, a2);
    scene_start = *(volatile u32*)gfxp;
}
RECOMP_HOOK_RETURN("func_800028B4") void arcade_on_scene_done(void) {
    recomp_arcade_scene_check(scene_start, *(volatile u32*)scene_gfxp, scene_a1, scene_a2);
    tron_scene_end(scene_gfxp); draw_scene_end(scene_gfxp);
}
#endif

// A crash in the entrances (func_80020B58 -> func_80010314, reading from address 0; twice in 2026-11-04/05's runs).
// func_8010DC70 asks for a scene to be built next frame: it looks the scene up in a table and always stores the answer at
// SCENE_WANTED, but only sets SCENE_PENDING if it's a real scene (below 25). 255 in that table means "no scene". So if a
// real request is still pending when a "no scene" one comes, the pending flag is left on with 255 as the scene, and
// func_8010DD38 hands that to func_8001B3E4, which reads past the end of its tables. The rest of func_8010DD38 already
// treats 25 and up as nothing to build, so the pending request is dropped the same way.
#define SCENE_WANTED  0x8015C8E0u    // s16
#define SCENE_PENDING 0x8015CCD4u    // s16
int plat_heap_test(void);
RECOMP_IMPORT("*", void recomp_arcade_scene_dropped(s32 scene));
RECOMP_HOOK("func_8010DD38") void arcade_on_scene_request(s32 a0) {
    (void)a0;
    if (RDS16(SCENE_PENDING) == 0) return;
    // (QA: WCW_HEAP_TEST=9 makes this happen on every request, to prove the guard: without it, this crashes at once.)
    if (plat_heap_test() == 9) RD16(SCENE_WANTED) = 255;
    if (RDS16(SCENE_WANTED) >= 25) {
        recomp_arcade_scene_dropped(RDS16(SCENE_WANTED));
        RD16(SCENE_PENDING) = 0;
    }
}

// The RULES page's preset for the match type (ovl_c func_80134140): a Royal Rumble's without pins,
// submissions or rope breaks -- in Arcade (logic.c decides: the user, 2026-11-07, a per-mode toggle, off in Regular).
RECOMP_HOOK_RETURN("func_80134140") void arcade_on_rules_preset(void) {
    arcade_rules_preset_done();
}

// The menus' action interpreter (ovl_c func_8010F0A8(action, record, row) -> the next screen): which action ran, for
// the mod's answer after it (W10: Multi Play's Royal Rumble takes its copy of F2's Rumble values -- logic.c).
static s32 menu_action_ran = -1;
RECOMP_HOOK("func_8010F0A8") void arcade_on_menu_action(s32 action, u32 rec, u32 row) { menu_action_ran = action; }
RECOMP_HOOK_RETURN("func_8010F0A8") void arcade_on_menu_action_done(void) {
    s32 a = menu_action_ran; menu_action_ran = -1;
    if (a >= 0) arcade_menu_action_done(a);
}
// The Royal Rumble's NUMBER page confirmed (ovl_c func_8012E9F0: NM_SEL_COUNT = its widget x 4): W10, the match's count.
RECOMP_HOOK_RETURN("func_8012E9F0") void arcade_on_number_confirm(void) { arcade_number_confirm_done(); }
// ... and its set-up (ovl_c func_8012E934), and the pages' Start skip-ahead (func_8010CE74): the match's count first.
RECOMP_HOOK("func_8012E934") void arcade_on_number_setup(void) { arcade_number_setup(); }
RECOMP_HOOK_RETURN("func_8012E934") void arcade_on_number_setup_done(void) { arcade_number_setup_done(); }   // (W10b: by one)
// A menu row's per-frame update (ovl_c func_801079B4(row, ...), each row from func_80108C54): W10b, the NUMBER page's
// widget rows' limits read as that update takes "none" while it runs (logic.c arcade_row_update).
RECOMP_HOOK("func_801079B4") void arcade_on_row_update(u32 row) { arcade_row_update(row); }
RECOMP_HOOK_RETURN("func_801079B4") void arcade_on_row_update_done(void) { arcade_row_update_done(); }
RECOMP_HOOK("func_8010CE74") void arcade_on_pages_skip(void) { arcade_number_setup(); }

// The computer player has decided a slot's buttons for the step (ovl_e func_800F03F0, called from
// func_80118368 for a computer slot): in a Royal Rumble, he goes for eliminations (gameplay.c).
static s32 cpu_slot = -1;
RECOMP_HOOK("func_800F03F0") void arcade_on_cpu(s32 slot) { cpu_slot = slot; }
RECOMP_HOOK_RETURN("func_800F03F0") void arcade_on_cpu_done(void) {
    arcade_rumble_ai(cpu_slot);                     // (any Rumble, both modes: its F2 option decides)
    { extern int ms_live; extern void match_slots_cpu(s32 s); if (ms_live) match_slots_cpu(cpu_slot); }   // (any32: extra slots, slots.c)
}

// osContGetReadData (fills the controller array at 0x80063350) returned. Big Show (bigshow.c), the new Superstars
// and NM+'s media are the roster's options in both modes (until 2026-11-07 the arcade off put Benoit back and
// took them out); then the mod's input handling (logic.c: the universal part, then the arcade's in Arcade).
RECOMP_IMPORT("*", void recomp_arcade_cont_ask(void));
RECOMP_IMPORT("*", void recomp_arcade_cont_read(u32 data));
RECOMP_IMPORT("*", void recomp_arcade_cont_status(u32 data));
RECOMP_IMPORT("*", void recomp_arcade_cont_motor(s32 channel, s32 on));
RECOMP_IMPORT("*", u32 recomp_arcade_pack_reserve(u32 bytes));
u32 plat_reserve(u32 bytes) { return recomp_arcade_pack_reserve(bytes); }
RECOMP_IMPORT("*", int recomp_arcade_wrestler_hd_count(void));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_hd_meta(int i, u32 dst));
int plat_wrestler_hd_count(void) { return recomp_arcade_wrestler_hd_count(); }
void plat_wrestler_hd_meta(int i, u32 dst) { recomp_arcade_wrestler_hd_meta(i, dst); }
RECOMP_IMPORT("*", int recomp_arcade_arena_count(void));
RECOMP_IMPORT("*", void recomp_arcade_arena_get(int i, u32 dst));
RECOMP_IMPORT("*", u32 recomp_arcade_arena_file(int i, u32 id, u32 size_at));
int plat_arena_folder_count(void) { return recomp_arcade_arena_count(); }
void plat_arena_folder_get(int i, u32 dst) { recomp_arcade_arena_get(i, dst); }
// (The packs have to be loaded before the exe can say where a file landed: asking for one does that.)
u32 plat_arena_folder_file(int i, u32 id, u32 size_at) { plat_pack(PACK_CONTENT); return recomp_arcade_arena_file(i, id, size_at); }
RECOMP_IMPORT("*", int recomp_arcade_wrestler_video_count(void));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_video_get(int i, u32 dst));
int plat_wrestler_video_count(void) { return recomp_arcade_wrestler_video_count(); }
void plat_wrestler_video_get(int i, u32 dst) { recomp_arcade_wrestler_video_get(i, dst); }
RECOMP_IMPORT("*", int recomp_arcade_wrestler_theme_count(void));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_theme_get(int i, u32 dst));
int plat_wrestler_theme_count(void) { return recomp_arcade_wrestler_theme_count(); }
void plat_wrestler_theme_get(int i, u32 dst) { recomp_arcade_wrestler_theme_get(i, dst); }
RECOMP_IMPORT("*", int recomp_arcade_wrestler_count(void));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_get(int i, u32 dst));
int plat_wrestler_count(void) { return recomp_arcade_wrestler_count(); }
// (2026-11-13, nmhandler) The stock Superstars' folders (exe arcade_wrestlers.cpp "stock folders", mod roster.c).
RECOMP_IMPORT("*", int recomp_arcade_stock_count(void));
RECOMP_IMPORT("*", void recomp_arcade_stock_get(int i, u32 dst));
RECOMP_IMPORT("*", int recomp_arcade_stock_photo(int i, int c, u32 dst));
RECOMP_IMPORT("*", void recomp_arcade_stock_note(int i, int what_costume, int a, u32 b));
int plat_stock_count(void) { return recomp_arcade_stock_count(); }
void plat_stock_get(int i, u32 dst) { recomp_arcade_stock_get(i, dst); }
int plat_stock_photo(int i, int c, u32 dst) { return recomp_arcade_stock_photo(i, c, dst); }
void plat_stock_note(int i, int what, int c, int a, u32 b) { recomp_arcade_stock_note(i, what << 8 | (c & 0xFF), a, b); }
// (2026-11-13, removal) The stock Superstars whose folders were deleted (exe arcade_wrestlers.cpp), and the mod's lines about
// what it did with whatever still names one (mod roster.c "removed").
RECOMP_IMPORT("*", int recomp_arcade_removed_count(void));
RECOMP_IMPORT("*", int recomp_arcade_removed_get(int i));
RECOMP_IMPORT("*", void recomp_arcade_removal_text(u32 text));
int plat_removed_count(void) { return recomp_arcade_removed_count(); }
int plat_removed_get(int i) { return recomp_arcade_removed_get(i); }
void plat_removal_text(const char* text) { recomp_arcade_removal_text((u32)text); }
// (2026-11-14, families) The families -- one select box, an ordered list of variants -- as the exe read them (exe
// arcade_wrestlers.cpp "families": families.json, or the default data), and the mod's lines about them (mod variants.c).
RECOMP_IMPORT("*", int recomp_arcade_family_of(u32 id));
RECOMP_IMPORT("*", int recomp_arcade_family_size(int f));
RECOMP_IMPORT("*", u32 recomp_arcade_family_variant(int f, int k));
RECOMP_IMPORT("*", void recomp_arcade_family_text(u32 text));
int plat_family_of(u32 id) { return recomp_arcade_family_of(id); }
int plat_family_size(int f) { return recomp_arcade_family_size(f); }
u32 plat_family_variant(int f, int k) { return recomp_arcade_family_variant(f, k); }
void plat_family_text(const char* text) { recomp_arcade_family_text((u32)text); }
RECOMP_IMPORT("*", u32 recomp_arcade_family_slot(int f));
RECOMP_IMPORT("*", u32 recomp_arcade_hidden(u32 id));
u32 plat_family_slot(int f) { return recomp_arcade_family_slot(f); }
u32 plat_hidden(u32 id) { return recomp_arcade_hidden(id); }
// (rostertags, 2026-11-21: a folder's "class" and the rivals.json pairs -- exe arcade_wrestlers.cpp; mod roster.c roster tags)
RECOMP_IMPORT("*", u32 recomp_arcade_wrestler_class(u32 id));
RECOMP_IMPORT("*", int recomp_arcade_rivals(u32 a, u32 b));
u32 plat_wrestler_class(u32 id) { return recomp_arcade_wrestler_class(id); }
int plat_rivals(u32 a, u32 b) { return recomp_arcade_rivals(a, b); }
RECOMP_IMPORT("*", int recomp_arcade_family_rest(u32 id));
int plat_family_rest(u32 id) { return recomp_arcade_family_rest(id); }
// (2026-11-12, handlers: a folder wrestler's own low-detail model -- exe arcade_wrestlers.cpp, mod parts.c / files.c)
RECOMP_IMPORT("*", u32 recomp_arcade_wrestler_file(u32 file, u32 dest));
RECOMP_IMPORT("*", int recomp_arcade_wrestler_model(int i, int costume, u32 model));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_model_note(int i, int costume, int bytes_differ, u32 files));
u32 plat_wrestler_file(u32 file, u32 dest) { return recomp_arcade_wrestler_file(file, dest); }
int plat_wrestler_model(int i, int costume, u32 model) { return recomp_arcade_wrestler_model(i, costume, model); }
void plat_wrestler_model_note(int i, int costume, int bytes_differ, u32 files) { recomp_arcade_wrestler_model_note(i, costume, bytes_differ, files); }
// (2026-11-13, handlers1: the model's game file numbers given on demand -- where the game keeps what holds them; the QA
//  check of a model with no built-in twin)
RECOMP_IMPORT("*", void recomp_arcade_wrestler_model_where(u32 slots, u32 slot_count, u32 shared_list));
RECOMP_IMPORT("*", u32 recomp_arcade_wrestler_model_check(int i, int costume, u32 model));
void plat_wrestler_model_where(u32 slots, u32 slot_count, u32 shared_list) { recomp_arcade_wrestler_model_where(slots, slot_count, shared_list); }
u32 plat_wrestler_model_check(int i, int costume, u32 model) { return recomp_arcade_wrestler_model_check(i, costume, model); }
// (2026-11-13, filepool) The numbers a folder model's files can have (files.c files_pool_send), and the end of a slot's
// load (func_80016464 below): the exe gives his files numbers only for the load.
RECOMP_IMPORT("*", void recomp_arcade_wrestler_file_pool(u32 ranges, u32 count));
RECOMP_IMPORT("*", void recomp_arcade_wrestler_model_loaded(u32 desc));
void plat_wrestler_file_pool(u32 ranges, u32 count) { recomp_arcade_wrestler_file_pool(ranges, count); }
void plat_wrestler_get(int i, u32 dst) { recomp_arcade_wrestler_get(i, dst); }
// (2026-11-13, movesets) Folder wrestler i's moveset as a list of slots (his moves.json), into dst (none if 0): 1 if he has
// one. And a QA note about a moveset unpacked, for the exe's log (roster.c moves_twin).
RECOMP_IMPORT("*", int recomp_arcade_wrestler_slots(int i, u32 dst));
RECOMP_IMPORT("*", void recomp_arcade_moves_note(u32 what, u32 id, u32 a, u32 b));
int plat_wrestler_slots(int i, u32 dst) { return recomp_arcade_wrestler_slots(i, dst); }
void plat_moves_note(u32 what, u32 id, u32 a, u32 b) { recomp_arcade_moves_note(what, id, a, b); }
// (2026-11-13, movelib) Moves of other games and of folders (movex.c): a library move or a folder's own by its ID (movelib2:
// the library's too, served by the exe), with its animations' game file numbers given for the match (3 u16 into out; 0: it can't be had, the exe's log says why); how
// many built-in library moves the exe has (the mod's move_lib.h must have as many); a folder animation's header and record
// (40 bytes into dst; 0: the number isn't one), its "ROM" place and its bytes there; and where the slots' animation tables
// and the game's animation players are (the exe gives a folder's numbers to another only once none of them names one).
RECOMP_IMPORT("*", int recomp_arcade_move_own(u32 id, u32 out));
RECOMP_IMPORT("*", u32 recomp_arcade_move_lib_n(void));
RECOMP_IMPORT("*", u32 recomp_arcade_move_lib_stamp(void));   // (2026-11-13, movelib2: the library's stamp, move_lib.h's)
RECOMP_IMPORT("*", int recomp_arcade_anim_meta(u32 file, u32 dst));
RECOMP_IMPORT("*", u32 recomp_arcade_anim_rom(u32 file));
RECOMP_IMPORT("*", void recomp_arcade_anim_read(u32 rom, u32 dst, u32 len));
RECOMP_IMPORT("*", void recomp_arcade_movex_where(u32 files, u32 slots_entries, u32 players, u32 players_stride));
int plat_move_own(u32 id, u32 out) { return recomp_arcade_move_own(id, out); }
u32 plat_move_lib_n(void) { return recomp_arcade_move_lib_n(); }
u32 plat_move_lib_stamp(void) { return recomp_arcade_move_lib_stamp(); }
// (2026-11-13, movelib2) A folder wrestler's moves.json edits ("records", "extra"): how many, and edit k (84 bytes) into dst.
RECOMP_IMPORT("*", int recomp_arcade_wrestler_edit(int i, int k, u32 dst));
int plat_wrestler_edit(int i, int k, u32 dst) { return recomp_arcade_wrestler_edit(i, k, dst); }
int plat_anim_meta(u32 file, u32 dst) { return recomp_arcade_anim_meta(file, dst); }
u32 plat_anim_rom(u32 file) { return recomp_arcade_anim_rom(file); }
void plat_anim_read(u32 rom, u32 dst, u32 len) { recomp_arcade_anim_read(rom, dst, len); }
void plat_movex_where(u32 files, u32 slots_entries, u32 players, u32 players_stride) { recomp_arcade_movex_where(files, slots_entries, players, players_stride); }
// (2026-11-13, seatsroot: staging seats, roster.c -- a folder wrestler found by his game number without fetching every
//  folder, his game number by his folder, and the seats' lines in the game's log)
RECOMP_IMPORT("*", int recomp_arcade_wrestler_find(u32 id));
RECOMP_IMPORT("*", u32 recomp_arcade_wrestler_id(int i));
RECOMP_IMPORT("*", void recomp_arcade_seat_note(int kind, int seat, int folder, int other));
int plat_wrestler_find(u32 id) { return recomp_arcade_wrestler_find(id); }
u32 plat_wrestler_id(int i) { return recomp_arcade_wrestler_id(i); }
void plat_seat_note(int kind, int seat, int folder, int other) { recomp_arcade_seat_note(kind, seat, folder, other); }
// (2026-11-13, seatsroot: the save sidecar, save.c -- the exe keeps it beside the save, by string ID)
RECOMP_IMPORT("*", void recomp_arcade_sidecar_put(u32 buf, int len));
RECOMP_IMPORT("*", int recomp_arcade_sidecar_get(u32 buf, int max));
void plat_sidecar_put(u32 buf, int len) { recomp_arcade_sidecar_put(buf, len); }
int plat_sidecar_get(u32 buf, int max) { return recomp_arcade_sidecar_get(buf, max); }
RECOMP_IMPORT("*", int recomp_arcade_heap_test(void));
RECOMP_IMPORT("*", void recomp_arcade_heap_note(u32 low, u32 high, u32 big));
int plat_heap_test(void) { return recomp_arcade_heap_test(); }
void plat_heap_note(u32 low, u32 high, u32 big) { recomp_arcade_heap_note(low, high, big); }
RECOMP_PATCH void func_80034618(u32 data) {
    recomp_arcade_cont_read(data);
    arcade_heap_service();
    arcade_roster_service(1);
    arcade_pad_hook();
}

// The rest of the game's controller library: straight to the PC's input (exe src/main/arcade_cont.cpp), not through
// the 64-byte blocks of the N64's controller chip.
RECOMP_PATCH s32 func_800344F0(u32 mq) { (void)mq; recomp_arcade_cont_ask(); return 0; }    // osContStartReadData
RECOMP_PATCH s32 func_80034590(u32 mq) { (void)mq; recomp_arcade_cont_ask(); return 0; }    // osContStartQuery
RECOMP_PATCH void func_80034570(u32 data) { recomp_arcade_cont_status(data); }              // osContGetQuery
// osMotorInit, osMotorStart / Stop: the OSPfs is the game's, it reads none of it but what's kept here
RECOMP_PATCH s32 func_800399D4(u32 mq, u32 pfs, s32 channel) {
    *(volatile u32*)(pfs + 0x4u) = mq;
    *(volatile s32*)(pfs + 0x8u) = channel;
    *(volatile u8*)(pfs + 0x65u) = 0xFF;
    *(volatile u32*)pfs = 8u;       // (PFS_MOTOR_INITIALIZED)
    return 0;
}
RECOMP_PATCH s32 func_80039890(u32 pfs, s32 on) {
    if (!(*(volatile u32*)pfs & 8u)) return 5;
    recomp_arcade_cont_motor(*(volatile s32*)(pfs + 0x8u), on);
    return 0;
}

// Title: the attract demo match was just picked (ovl_a). Both modes: any arena, ours too, for an attract demo (the
// plan's Q11: Regular's own attract gets the mod's arenas), and the QA bench's match.
RECOMP_HOOK_RETURN("func_800EF478") void arcade_on_demo_setup(void) {
    arcade_demo_setup_done();
}

// A story page waits for the player (ovl_a's func_800E8A14 reads the buttons pressed this frame):
// after a while, the page sees a confirm -- the arcade's abbreviated menus (arcade_story_page says no in Regular).
RECOMP_HOOK("func_800E8A14") void arcade_on_page_wait(s32 a0) {
    if (arcade_story_page()) RD16(NM_PRESSED) |= BTN_A;
}

// (QA, the title's PRESS START: the menus' sprite requests -- func_800DD5A4(id, x, y, style, flags) -- and each sprite's
//  draw -- func_80117714(gfx, request, alpha, flags) -- seen for sprite 15; logic.c qa_press_start)
RECOMP_HOOK("func_800DD5A4") void arcade_on_sprite_request(u32 id, s32 x, s32 y, u32 style) { arcade_qa_sprite_request(id); }
RECOMP_HOOK("func_80117714") void arcade_on_sprite_draw(u32 gfx, u32 req, s32 alpha, u32 flags) { arcade_qa_sprite_draw(req, alpha); }
// The menus are about to draw their sprite queue (ovl_c func_80117A84): the arcade's text joins it (Arcade only:
// arcade_menu_sprites does nothing in Regular, so the title keeps its own PRESS START).
RECOMP_HOOK("func_80117A84") void arcade_on_menu_sprites(u32 gfx, s32 fade) {
    arcade_menu_sprites();
}

// Camera build: func_80001580(gfx, proj, view) -> CAMERA_MATRIX = view x proj.
static float* cam_proj;
RECOMP_HOOK("func_80001580") void arcade_on_camera(u32 gfx, float* proj, float* view) {
    cam_proj = proj;
}
// the match loop (func_800D9AB0) has run a step of the match's logic
RECOMP_HOOK_RETURN("func_8000073C") void arcade_on_overlay(void) { arcade_overlay_loaded(); }
// (both modes: the made-over moves' chains after every step; the arcade's extra steps only in Arcade -- logic.c)
RECOMP_HOOK_RETURN("func_80114514") void arcade_on_match_step(void) {
    arcade_match_step();
}

// (the low stage-walk camera: arcade_entrance_cam decides -- Arcade only for now, the user's per-mode toggle to come)
RECOMP_HOOK_RETURN("func_80001580") void arcade_on_camera_built(void) {
    if (cam_proj) arcade_camera(cam_proj, (float*)NM_CAMERA_MATRIX);
}

int plat_key_down(int scancode) {
    return recomp_get_key_down((unsigned int)scancode) != 0;
}

// The full-detail models' per-frame work, native (exe: src/main/arcade_hd.cpp; src/hd.c)
RECOMP_IMPORT("*", int recomp_arcade_hd_blend(u32 bones, u32 table, u32 vtx, u32 mats));
RECOMP_IMPORT("*", void recomp_arcade_hd_hand(u32 args));
RECOMP_IMPORT("*", void recomp_arcade_hd_time(int what));
int plat_hd_blend(u32 bones, u32 table, u32 vtx, u32 mats) { return recomp_arcade_hd_blend(bones, table, vtx, mats); }
void plat_hd_hand(u32 args) { recomp_arcade_hd_hand(args); }
void plat_hd_time(int what) { recomp_arcade_hd_time(what); }

// The operator's settings are the mod's options (mod.toml: the same ids). Enum options come back as
// the option's index.
// (ctrlsave, 2026-11-25) continue_secs is told by its whole name. The test was the first three letters, "con", and
// "controls" (F2's CONTROL MODE) starts with them too: CONTROL MODE was read as the continue timer (10 s: not 0, so
// 6-BUTTON at every start) and saved as the timer's fallback index (1: 6-BUTTON) -- the player's N64 never kept.
static int is_continue_secs(const char* key) {
    const char* k = "continue_secs";
    while (*key && *key == *k) { key++; k++; }
    return *key == 0 && *k == 0;
}
u32 plat_config(const char* key, u32 fallback) {
    if (is_continue_secs(key)) {
        u32 i = (u32)recomp_get_config_u32("continue_secs");
        return i < 6 ? 5 * (i + 1) : fallback;            // ("5" .. "30")
    }
    return (u32)recomp_get_config_u32(key);
}

RECOMP_IMPORT("*", void recomp_arcade_picture(u32 v));
void plat_picture(u32 v) { recomp_arcade_picture(v); }
RECOMP_IMPORT("*", void recomp_arcade_host(s32 volume, s32 rumble, s32 deadzone));
void plat_host(s32 volume, s32 rumble, s32 deadzone) { recomp_arcade_host(volume, rumble, deadzone); }
RECOMP_IMPORT("*", void recomp_arcade_view_replace(u32 head, u32 end));
void plat_view_replace(u32 head, u32 end) { recomp_arcade_view_replace(head, end); }
void plat_config_set(const char* key, u32 value) {
    if (is_continue_secs(key)) value = value >= 5 && value <= 30 ? value / 5 - 1 : 1;   // continue_secs: the Enum's index
    recomp_set_config_u32(key, value);
}

// Controllers: the exe's (src/main/arcade_pads.cpp)
RECOMP_IMPORT("*", int recomp_arcade_pad_name(int player, char* buf, int len));
RECOMP_IMPORT("*", unsigned int recomp_arcade_pad_buttons(int player));
RECOMP_IMPORT("*", int recomp_arcade_pad_learn_start(int player));
RECOMP_IMPORT("*", int recomp_arcade_pad_learn_read(int player, char* buf, int len));
RECOMP_IMPORT("*", int recomp_arcade_pad_learn_apply(int player, char* elements));
RECOMP_IMPORT("*", void recomp_arcade_pad_swap(void));
RECOMP_IMPORT("*", void recomp_arcade_quit(void));
RECOMP_IMPORT("*", int recomp_arcade_bind_name(int dev, int row, char* buf, int len));
RECOMP_IMPORT("*", int recomp_arcade_bind_start(int dev, int row));
RECOMP_IMPORT("*", int recomp_arcade_bind_poll(void));
RECOMP_IMPORT("*", void recomp_arcade_bind_clear(int dev, int row));
RECOMP_IMPORT("*", void recomp_arcade_bind_reset(int dev));
RECOMP_IMPORT("*", int recomp_arcade_bind_stick(int dev, int which));
// Commentary: the exe's (src/main/commentary/arcade_commentary.cpp)
RECOMP_IMPORT("*", int recomp_arcade_comm_event(int ev, int a, int b, int arg));
RECOMP_IMPORT("*", int recomp_arcade_comm_tick(int in_match));
RECOMP_IMPORT("*", void recomp_arcade_comm_config(int on, int volume, int talk));
RECOMP_IMPORT("*", void recomp_arcade_comm_reset(void));
RECOMP_IMPORT("*", int recomp_arcade_comm_available(void));
void plat_comm_event(int ev, int a, int b, int arg) { recomp_arcade_comm_event(ev, a, b, arg); }
int plat_comm_tick(int in_match) { return recomp_arcade_comm_tick(in_match); }
void plat_comm_config(int on, int volume, int talk) { recomp_arcade_comm_config(on, volume, talk); }
void plat_comm_reset(void) { recomp_arcade_comm_reset(); }
int plat_comm_available(void) { return recomp_arcade_comm_available(); }
int plat_pad_name(int player, char* buf, int len) { return recomp_arcade_pad_name(player, buf, len); }
u32 plat_pad_buttons(int player) { return recomp_arcade_pad_buttons(player); }
int plat_pad_learn_start(int player) { return recomp_arcade_pad_learn_start(player); }
int plat_pad_learn_read(int player, char* buf, int len) { return recomp_arcade_pad_learn_read(player, buf, len); }
int plat_pad_learn_apply(int player, char* elements) { return recomp_arcade_pad_learn_apply(player, elements); }
void plat_pad_swap(void) { recomp_arcade_pad_swap(); }
void plat_quit(void) { recomp_arcade_quit(); }
int plat_bind_name(int dev, int row, char* buf, int len) { return recomp_arcade_bind_name(dev, row, buf, len); }
int plat_bind_start(int dev, int row) { return recomp_arcade_bind_start(dev, row); }
int plat_bind_poll(void) { return recomp_arcade_bind_poll(); }
void plat_bind_clear(int dev, int row) { recomp_arcade_bind_clear(dev, row); }
void plat_bind_reset(int dev) { recomp_arcade_bind_reset(dev); }
int plat_bind_stick(int dev, int which) { return recomp_arcade_bind_stick(dev, which); }
RECOMP_IMPORT("*", int recomp_arcade_story_scan(void));
RECOMP_IMPORT("*", int recomp_arcade_story_menu(int i, char* buf, int len));
RECOMP_IMPORT("*", int recomp_arcade_story_pack(int i, u8* dst, int maxlen));
int plat_story_scan(void) { return recomp_arcade_story_scan(); }
int plat_story_menu(int i, char* buf, int len) { return recomp_arcade_story_menu(i, buf, len); }
RECOMP_IMPORT("*", int recomp_arcade_story_error(int i, char* buf, int len));
int plat_story_error(int i, char* buf, int len) { return recomp_arcade_story_error(i, buf, len); }
int plat_story_pack(int i, u8* dst, int maxlen) { return recomp_arcade_story_pack(i, dst, maxlen); }
RECOMP_IMPORT("*", int recomp_arcade_pages(u8* dst, int maxlen));
int plat_pages_pack(u8* dst, int maxlen) { return recomp_arcade_pages(dst, maxlen); }

// Game functions (arcade.h's GF_*), by their recompiled names. The menus' ones are ovl_c's; the
// arcade only calls them while that overlay is loaded.
extern void func_8012D2EC(s32);
extern void func_8012D720(void);
extern void func_8012D614(void);
extern void func_8013DA18(void);
extern void func_80110FD0_ovl_c(s32, s32);
extern void func_80132EB4(void);
extern void func_80003714(s32);
extern void func_80133DF8(void);
extern void func_8013437C(void);
extern void func_8014227C(void);
extern void func_80143390(void);
extern void func_80135534(s32);
extern void func_80133CD8(void);
extern s32 func_8010F0A8(s32, u32, u32);
extern void func_80118A20(u32, u32, u32, u32);
extern void func_80134140(s32);
extern void func_80135728(s32);
extern s32 func_80136784(s32);
extern void func_8010D874(s32);
extern void func_80136CC8(s32);
extern s32 func_800EC484(u32, s32);
extern s32 func_8012EA9C(u32, u32);
extern void func_80114514(void);
extern void func_800159FC(s32, s32);
extern void func_8000B60C(u32, u32);
extern u32 func_8000AEC8(s32, s32);
extern s32 func_80134830(u32);
extern u32 func_8002B12C(u32, s32);
extern void func_800DF5DC(u32);
extern u32 func_800DF3BC(u32, u32);
extern s32 func_801202F4(u32);
extern void func_800EB6D4(u32, s32);
extern u32 func_800DD6F4(u32, s32);
extern u32 func_8010CC10(u32, s32);
extern s32 func_80138360(void);
extern s32 func_800ED958(u32, s32);
extern s32 func_800EDD84(u32);
extern s32 func_800EDE48(u32);
extern void func_800EE084(u32);
extern void func_80017C64(s32, s32, s32);
extern s32 func_8013E870(u32, u32, u32);

s32 plat_game_call(int fn, u32 a0, u32 a1, u32 a2) {
    switch (fn) {
    case GF_PAGE_INIT: func_8012D2EC((s32)a0); return 0;
    case GF_EXH_INIT: func_8012D720(); return 0;
    case GF_INIT_5: func_8012D614(); return 0;
    case GF_INIT_14: func_8013DA18(); return 0;
    case GF_SELECT_INIT: func_80110FD0_ovl_c((s32)a0, (s32)a1); return 0;
    case GF_INIT_17: func_80132EB4(); return 0;
    case GF_80003714: func_80003714((s32)a0); return 0;
    case GF_INIT_21: func_80133DF8(); return 0;
    case GF_INIT_20: func_8013437C(); return 0;
    case GF_INIT_33: func_8014227C(); return 0;
    case GF_INIT_34: func_80143390(); return 0;
    case GF_INIT_25: func_80135534((s32)a0); return 0;
    case GF_INIT_22: func_80133CD8(); return 0;
    case GF_ACTION: return func_8010F0A8((s32)a0, a1, a2);
    case GF_RULES_PRESET: func_80134140((s32)a0); return 0;
    case GF_SELECT_DONE: func_80135728((s32)a0); return 0;
    case GF_FINALIZE: return func_80136784((s32)a0);
    case GF_SETTINGS_SLOT: func_8010D874((s32)a0); return 0;
    case GF_BUILD_RECORDS: func_80136CC8((s32)a0); return 0;
    case GF_RR_ENTRANT: return arcade_rr_entrant((s32)a0);
    case GF_ELIMINATE: func_80118A20(a0, a1, a2, 0); return 0;
    case GF_RETARGET: return func_800EC484(a0, (s32)a1);
    case GF_FACING: return func_8012EA9C(a0, a1);
    case GF_MATCH_STEP: func_80114514(); return 0;
    case GF_PLAYER_SHOW: func_800159FC((s32)a0, (s32)a1); return 0;
    case GF_LOOK: func_8000B60C(a0, a1); return 0;
    case GF_ROPES_TEST: return func_801202F4(a0);
    case GF_SET_STATE: func_800EB6D4(a0, (s32)a1); return 0;
    case GF_TEXT_BANK: return (s32)func_800DD6F4(a0, (s32)a1);
    case GF_SCENE_TEXT_BANK: return (s32)func_8010CC10(a0, (s32)a1);
    case GF_RATING: return func_80138360();
    case GF_COND: return func_800ED958(a0, (s32)a1);
    case GF_COND_RANK: return func_800EDD84(a0);
    case GF_COND_STATS: return func_800EDE48(a0);
    case GF_HISTORY_RUMBLE: func_800EE084(a0); return 0;
    case GF_PLAY_SONG: func_80017C64((s32)a0, (s32)a1, (s32)a2); return 0;
    case GF_PAGE_MEMBER: return (s32)func_8000AEC8((s32)a0, (s32)a1);
    case GF_RR_ELIGIBLE: return func_80134830(a0);
    case GF_AUDIO_DMA: return (s32)func_8002B12C(a0, (s32)a1);
    case GF_SPRITE_RELEASE: func_800DF5DC(a0); return 0;
    case GF_MENU_TEXT: return (s32)func_800DF3BC(a0, a1);
    case GF_GROUND_GRAPPLE: return func_8013E870(a0, a1, a2);
    }
    return -1;
}

extern u32 func_800DF3BC(u32, u32);
extern void func_800DD5A4(u32, s32, s32, u32, u32);
u32 plat_text_sprite(const char* s, u32 style) { return func_800DF3BC((u32)s, style); }
void plat_sprite_request(u32 id, s32 x, s32 y, u32 style, u32 flags) { func_800DD5A4(id, x, y, style, flags); }
extern u32 func_80009F6C(u32, u32);
u32 plat_game_text(const char* s, u32 style) { return func_80009F6C((u32)s, style); }
extern void func_8000A1BC(u32);
void plat_game_text_free(u32 rec) { func_8000A1BC(rec); }
extern u32 func_800173C0(u32);
void plat_sound(u32 id) { func_800173C0(id); }

// ---- the single-player Royal Rumble (see logic.c) ----
// These always call through: each body does nothing unless the arcade's own Rumble is set up (rr_mode, which only
// the arcade's championship-list row -- rr_open -- and its QA bench set), so a Rumble from the game's own Multi Play
// menu is the game's in either mode until the plan's W10 brings the F2 Rumble settings to it.
// The menus set the match up from their choices (ovl_c func_80136784).
RECOMP_HOOK("func_80136784") void arcade_on_finalize(s32 a0) {
    arcade_rr_finalize_pre();
}
RECOMP_HOOK_RETURN("func_80136784") void arcade_on_finalize_done(void) {
    arcade_rr_finalize_post();
}
// The match's set-up (ovl_e func_80115000).
RECOMP_HOOK_RETURN("func_80115000") void arcade_on_match_setup(void) {
    arcade_rr_match_setup();
}
// Who goes to a Royal Rumble slot next (ovl_e func_801199F0): the game's function, with the arcade's
// rule (only when the clock has called an entrant) in it.
RECOMP_PATCH s32 func_801199F0(s32 slot) {
    return arcade_rr_entrant(slot);
}
// A knocked-down wrestler's lying animation (ovl_e func_8013FEF4): the game's function, with FAST GET UP in it
// (gameplay.c) -- the running mode's toggle (W9: on in Arcade, off in Regular by default; arcade_fast_getup).
RECOMP_PATCH s32 func_8013FEF4(u32 w) {
    return arcade_down_anim(w, 1);
}
// A slot is eliminated (ovl_e func_8011723C) -> the match is over: the game's function, with the
// arcade's Rumble holding the player's slot while his continue is to come (logic.c).
RECOMP_PATCH s32 func_8011723C(s32 slot) {
    return arcade_elim_check(slot);
}
// The Rumble's entrance manager, each frame of the match (ovl_e func_8011D400).
RECOMP_HOOK("func_8011D400") void arcade_on_rumble_frame(void) {
    ring_void_guard();                              // (every match: a man left at the mat's height past the apron, logic.c)
    arcade_rr_frame();                              // (nothing unless the arcade's Rumble is live: rr_live())
}
// The match HUD's per-frame update (ovl_e func_800DEDE0).
RECOMP_HOOK_RETURN("func_800DEDE0") void arcade_on_hud(void) {
    arcade_rr_hud();                                // (likewise)
}


// ---- Big Show (see bigshow.c) ----
// The save: the game packs a costume record (ovl_a func_800E299C(dest, record)) or a moveset
// (func_800E2AC0(dest, moveset)) for it. Not tied to Enabled: Benoit's own data always goes in.
RECOMP_HOOK("func_800E299C") void bigshow_on_pack_record(u32 dest, u32 rec) { bigshow_pack_pre(0, rec); }
RECOMP_HOOK_RETURN("func_800E299C") void bigshow_on_pack_record_done(void) { bigshow_pack_post(); }
RECOMP_HOOK("func_800E2AC0") void bigshow_on_pack_moves(u32 dest, u32 moves) { bigshow_pack_pre(1, moves); }
RECOMP_HOOK_RETURN("func_800E2AC0") void bigshow_on_pack_moves_done(void) { bigshow_pack_post(); }
// Every file the game reads from the ROM (func_80003C94(info, file, dest)): Big Show's photo.
// A moveset is unpacked (moves, slots out, abilities, abilities out): Big Show's are No Mercy Plus's.
// A moveset is unpacked: by the menus (func_80005EFC(moves, slots, abilities, out)) or for the match
// (ovl_e func_8013CBAC(slot, moves, ability bits)): the new Superstars' and Big Show's (roster.c).
// (2026-11-13, movesets: the slots and abilities out, and the ability bits, go in too -- a folder wrestler's list of slots
//  is put into them once the game's unpack is done: roster.c "movesets as slot lists")
RECOMP_HOOK("func_80005EFC") void roster_on_unpack(u32 moves, u32 slots, u32 abil, u32 out) { roster_moves_pre(moves, slots, out); }
RECOMP_HOOK_RETURN("func_80005EFC") void roster_on_unpack_done(void) { roster_moves_post(); }
RECOMP_HOOK("func_8013CBAC") void roster_on_match_moves(s32 slot, u32 moves, u32 bits) { roster_match_moves_pre(slot, moves, bits); }
RECOMP_HOOK_RETURN("func_8013CBAC") void roster_on_match_moves_done(void) { roster_match_moves_post(); }
// func_80002268(node): the scene's world matrices -- with each checked (gameplay.c)
RECOMP_PATCH void func_80002268(u32 n) { scene_world(n); }
#ifdef ARCADE_MOVEX
// ovl_e func_80130878(wrestler, 0 / 1): the animation of the move he's in -- with the made-over moves' (movex.c:
// the ported Superstars' stand-ins played with their own animations)
RECOMP_PATCH s32 func_80130878(u32 w, s32 who) { return movex_anim(w, who); }
// An animation's header and record, by its file (movex.c: Revenge's files, past the game's tables, have theirs)
RECOMP_PATCH s32 func_80011A54(s32 file) { return anim_frames(file); }
RECOMP_PATCH float func_80011A78(s32 file) { return anim_speed(file); }
RECOMP_PATCH s32 func_80011AC0(s32 file) { return anim_endpose(file); }
RECOMP_PATCH s32 func_80011AE8(s32 n) { return anim_pose_now(n); }
RECOMP_PATCH void func_80011BD8(u32 out, s32 file) { anim_details(out, file); }
RECOMP_PATCH u32 func_80011D30(s32 file) { return anim_recpart(file); }
RECOMP_PATCH void func_80011340(s32 n, s32 pri) { anim_stream(n, pri); }   // (the pack's animations streamed)
#endif

// The entrance's video (func_8010C998, replaced: media.c knows the videos past the game's): the entrance
// table (an overlay's data) is read then -- Big Show's video and effects in it.
RECOMP_PATCH s32 func_8010C998(s32 v) { return media_video_select(v); }
u32 roster_entrance_fx(u32 id, s32 c); s32 media_play_sound(s32 snd);
RECOMP_PATCH u32 func_8000AB64(s32 id, s32 c) { return roster_entrance_fx((u32)id, c); }   // (media_entrance_tables first)
RECOMP_PATCH s32 func_800037EC(s32 snd) { return media_play_sound(snd); }
extern s32 tron_stop_fade;   // (2026-11-11: the fade's length reaches the exe, so a native theme fades with the game's song: tron.c)
RECOMP_HOOK("func_800037BC") void tron_on_music_stop(s32 fade) { tron_stop_fade = fade; tron_music(0); }   // (the music stopped, or fading out)
RECOMP_HOOK("func_8011E488") void media_on_menu_video(s32 v) { media_video_picked(v); }

// The audio DMA callback (libultra's sample scheme), replaced: themes.c serves Big Show's theme,
// which isn't in the ROM, and passes everything else to the game's cache (func_8002B12C).
RECOMP_PATCH u32 func_8002B0C0(u32 addr, s32 len, u32 state) { return themes_audio_dma(addr, len); }

// The game's files (files.c): its two functions, replaced by ones that know the arcade's new files
// (IDs from 0x4C03: photos 100-127). They tell bigshow.c and media.c of every read.
void files_info(u32 info, u32 file); u32 files_read(u32 info, u32 file, u32 dest);
RECOMP_PATCH void func_80003BD0(u32 info, u32 file) { files_info(info, file); }
// a model's skeleton: No Mercy's joins, or WrestleMania 2000's for its models (skel.c)
RECOMP_PATCH void func_80015D8C(u32 desc, s32 slot) { skel_setup(desc, slot); }
// JR and King at the announce table (announce.c): made with the table, moved with it every frame
RECOMP_HOOK_RETURN("func_8001DB4C") void announce_on_table_setup(void) { announce_scene(); }
RECOMP_HOOK_RETURN("func_8001DF38") void announce_on_table_frame(void) { announce_table_frame(); }
RECOMP_PATCH void func_80014F5C(s32 slot) { skel_visible(slot); }
RECOMP_PATCH void func_80014E44(s32 slot, s32 flag) { skel_show(slot, flag); }
#ifdef ARCADE_STORY      // the story runner (story.c; on in what ships, build_mod.ps1 -NoStory leaves it out). With no
                         // story of ours running each is the game's own code, exactly (story.c's fall-throughs).
RECOMP_PATCH u32 func_800DD740(s32 id) { return story_card_text(id); }
RECOMP_PATCH u32 func_8010CC38(s32 id) { return story_scene_text(id); }
RECOMP_PATCH u32 func_800EE1F0(void) { return story_branch(); }
RECOMP_PATCH void func_800ED130(s32 n) { story_set_node(n); }
RECOMP_PATCH s32 func_8010D1E0(u32 c) { return story_scene_cond(c); }   // (a scene's op 0x7F test: story.c, the game's code with no story of ours)
#endif
// (2026-11-13, filepool: after its second step a slot's model is loaded -- nothing reads its descriptor's file numbers
//  again -- so the exe can give them to the next load: exe arcade_wrestlers.cpp)
RECOMP_PATCH void func_80016464(u32 desc, s32 slot, s32 phase) {
    skel_textures(desc, slot, phase);
    if ((s16)phase == 1) recomp_arcade_wrestler_model_loaded(desc);
}
RECOMP_PATCH void func_80016D2C(void) { hands_frame(); }
RECOMP_PATCH u32 func_80003C94(u32 info, u32 file, u32 dest) { return files_read(info, file, dest); }

// The content packs: files in the game's "content" folder, which the program loads into game memory the first time any
// of them is asked for (exe src/main/arcade_packs.cpp). plat_pack gives a pack's address, by PACK_* number (arcade.h).
RECOMP_IMPORT("*", u32 recomp_arcade_pack_load(s32 which));
u32 plat_pack(int which) {
    static u32 known[PACK_COUNT + 16 + FOLDER_THEMES + FOLDER_VIDEOS];     // (Those built in, then from wrestler folders: models, themes, videos.)
    if (!known[which]) known[which] = recomp_arcade_pack_load(which);
    return known[which];
}
// QA: tells the exe which memory holds the packs, so that it can make it read-only and catch anything that writes there.
void plat_guard(u32 at) { RD32(at + 4) = plat_pack(PACK_CONTENT); RD32(at + 8) = recomp_arcade_pack_load(-1); RD32(at) = 0x47415244u; }
u32 plat_wmport_rom(u32 off) { return 0; }
void plat_wmport_read(u32 off, u32 dst, u32 len) {
    const u8* s = (const u8*)plat_pack(PACK_WMPORT) + off; u8* d = (u8*)dst;
    for (u32 i = 0; i < len; i++) d[i] = s[i];
}
void plat_content_read(u32 off, u32 dst, u32 len) {
    const u8* s = (const u8*)plat_pack(PACK_CONTENT) + off; u8* d = (u8*)dst;
    for (u32 i = 0; i < len; i++) d[i] = s[i];
}
// Story Mode's people: its parts (ovl_a func_800EC554), its challengers (func_800EBF70) and its
// random picks (func_800EC268) read the tables bigshow_story_tables keeps.
RECOMP_HOOK("func_800EC554") void bigshow_on_story_part(s32 part, s32 slot) { bigshow_story_tables(); }
RECOMP_HOOK("func_800EBF70") void bigshow_on_story_rival(s32 belt) { bigshow_story_tables(); }
RECOMP_HOOK("func_800EC268") void bigshow_on_story_pick(s32 code) { bigshow_story_tables(); }
// The story's Royal Rumbles (ovl_a func_800EE3EC(list) -> entrants): the game's function, with the
// wrestler who stands in for the player (when the player is on the list) from bigshow_stand_in.
extern u32 func_800EC354(s32 slot, u32 who, s32 human, u32 side, u32 extra);
RECOMP_PATCH s32 func_800EE3EC(s32 list) {
    bigshow_story_tables();
    u32 entrants = RD32(0x800F8CB0u + 4u * (u32)list);
    s32 n = RD8(0x800F8CB8u + (u32)list);
    u32 node = RD32(0x80097F14u);
    func_800EC354(0, 0x8000u, 1, RD8(node + 0xB), 0);           // the player
    int partner_in = 0;
    for (s32 i = 1; i < n; i++) {
        u16 e = RD16(entrants + 2u * (u32)i);
        u32 id = e & 0xFFFu, who;
        if (id == RD16(0x80097EECu)) who = bigshow_stand_in();             // the player's own place
        else if (partner_in && id == RD16(0x80097EF0u)) who = 0x204;
        else if (e == 0x8002u) { who = 0x8002u; partner_in = 1; }           // the player's partner
        else who = e & 0x3FFFu;
        func_800EC354(i, who, 0, (u32)e >> 14, 0);
    }
    return n;
}

// ---- the roster, made larger (roster.c): the resident roster module's functions, and ovl_a's that
// move the pages and one Superstar's data between the roster and the save, replaced ----
u32 roster_ord_to_id(s32); s32 roster_id_to_ord(u32); u32 roster_record(s32, s32); u32 roster_moves(s32);
void roster_build_record(u32, s32, u32); void roster_load_moves(u32, u32); void roster_build_all(void);
void roster_build_pages(void); void roster_pages_from_save(void); void roster_pages_to_save(void);
void roster_save_one(u32); void roster_pack_one(u32); void roster_copy_saved(u32); void roster_stats_alloc(void);
RECOMP_PATCH u32 func_8000AE38(s32 ord) { return roster_ord_to_id(ord); }
RECOMP_PATCH s32 func_8000AE78(u32 id) { return roster_id_to_ord(id); }
RECOMP_PATCH u32 func_8000B4B8(s32 ord, s32 c) { return roster_record(ord, c); }
s32 roster_entrance_song(u32 id, s32 c);
RECOMP_PATCH s32 func_8000AAF4(u32 id, s32 c) { return roster_entrance_song(id, c); }
// (a Rumble's later entrants' songs and the winner's: the made ones too -- media.c)
s32 media_entrant_song(s32 id, u32 c); s32 media_winner_song(s32 n);
RECOMP_PATCH s32 func_800E22D8(s32 id, u32 c) { return media_entrant_song((s16)id, c); }
RECOMP_PATCH s32 func_800DBA88(void) {
    s32 n;                                   // (the six-man engine's player count: its $gp)
    __asm__ volatile("move %0, $gp" : "=r"(n));
    return media_winner_song(n);
}
RECOMP_PATCH u32 func_8000B560(s32 ord) { return roster_moves(ord); }
RECOMP_PATCH void func_8000B834(u32 id, s32 c, u32 rec) { roster_build_record(id, c, rec); }
RECOMP_PATCH void func_8000B914(u32 id, u32 buf) { roster_load_moves(id, buf); }
RECOMP_PATCH void func_8000B9B0(void) { roster_build_all(); }
RECOMP_PATCH void func_8000B004(void) { roster_build_pages(); }
s32 roster_page_of(u32 id, int back); u32 roster_page_box(u32 p, s32 i);   // (thirteen pages: WCW's)
RECOMP_PATCH s32 func_8000A6DC(u32 id) { return roster_page_of(id, 0); }
RECOMP_PATCH s32 func_8000A758(u32 id) { return roster_page_of(id, 1); }
RECOMP_PATCH u32 func_8000A7D8(u32 p, s32 i) { return roster_page_box(p, i); }
RECOMP_PATCH void func_800E30DC(void) { roster_pages_from_save(); }
RECOMP_PATCH void func_800E3020(void) { roster_pages_to_save(); }
RECOMP_PATCH void func_800E31C4(u32 id) { roster_save_one(id); }
RECOMP_PATCH void func_800E3330(u32 id) { roster_pack_one(id); }
RECOMP_PATCH void func_800E5A44(u32 id) { roster_copy_saved(id); }
RECOMP_PATCH void func_800E4450(void) { roster_stats_alloc(); }
// body parts, with the new Superstars' (parts.c; func_80018A3C / func_8001858C read its tables: patches.py)
RECOMP_PATCH void func_8001AC44(u32 src, u32 dst) { parts_unpack(src, dst); }
