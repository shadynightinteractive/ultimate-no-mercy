// WWF No Mercy Arcade -- shared definitions.
//
// The same C sources build two ways:
//   - the real mod (.nrm) for WWF No Mercy: Recompiled (hooks_recomp.c), and
//   - a raw MIPS blob that the test rig loads into an emulated N64 at 0x80700000
//     (hooks_test.c), so the logic and the drawing are tested on the real game.
//
// All addresses are WWF No Mercy (USA) Rev 1, found with the test rig (see NOTES.md).
#ifndef ARCADE_H
#define ARCADE_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;   // (the roster's mask: roster_frame)
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

#include "six_addrs.h"   // (the six-man engine build, NM_SIX: its moved arrays)
// Memory: the game can use 512 MB, not the N64's 8. A pointer from the game can be anywhere in this range.
#define GAME_RAM_TOP 0xA0000000u
#define GAME_PTR(p) ((u32)(p) >= 0x80000400u && (u32)(p) < GAME_RAM_TOP)
// The game heap's two added regions, on top of its original 3.5 MB (see arcade_heap_service in logic.c). Sizes are in
// the allocator's units of 16 bytes.
// The middle region is below 16 MB, where every address still fits the N64's 24-bit fields, so all code is safe with it.
// It starts past the runtime's PI handles at 0x80800000 and the wave tables below, and ends where the mod's image
// starts (0x81000000).
#define HEAP_MID        0x80910000u
#define HEAP_MID_UNITS  0x6F000u
// Set aside just below the middle region: 1 MB for the sound instruments' wave tables (media.c).
#define THEME_WAVE_RAM  0x80810000u
// The high region is 256 MB, for what's too big to fit lower down: large models and imported content. Code that packs
// an address into 24 bits can't handle it, so it only gets blocks that nothing lower can hold.
#define HEAP_HIGH       0x90000000u
#define HEAP_HIGH_UNITS 0xFFFFFFu
#define RD8(a)   (*(volatile u8*)(a))
#define RD16(a)  (*(volatile u16*)(a))
#define RD32(a)  (*(volatile u32*)(a))
#define RDS8(a)  (*(volatile s8*)(a))
#define RDS16(a) (*(volatile s16*)(a))
#define RDS32(a) (*(volatile s32*)(a))

// ---- game state -------------------------------------------------------------------------
#define NM_MODE            0x800457A0u // s32: 0 menus/title, 1 leave intro, -6 attract demo running
#define NM_SCREEN          0x800457A4u // s16: menu screen (0 = title)
#define NM_ATTRACT_COUNTER 0x800457AAu // u8: odd = intro movie next, even = demo set (value / 2)
#define NM_INTRO_SCENE     0x800A872Cu // s32: intro transition pending (0 = none)
#define NM_INTRO_SKIPPABLE 0x8015C8E4u // s32: nonzero once Start may skip the intro
#define NM_TITLE_IDLE      0x80158150u // s32: title idle frames; attract starts at 900; <0 = not counting
#define NM_TITLE_IDLE_LIMIT 900
#define NM_DEMO_TABLE      0x80102998u // 10-byte records: 4 x u16 wrestler (id | costume << 12), u16 flags
#define NM_DEMO_SINGLES_FIRST 3        // set 1 ("Singles A") = records 3..10
#define NM_DEMO_SINGLES_END   11
#define NM_DEMO_SET_SINGLES   1
#define NM_VENUE           0x800A10F1u // u8: venue the match loads (0-9 ring arenas)
#define NM_MATCH_FLAGS     0x80097EC0u // u32: bit0 = slot 0 is CPU, bit1 = slot 1 is CPU,
#define NM_FLAG_DEMO       0x03000000u //   0x02000000 attract demo, 0x01000000 demo (no HUD)
#define NM_FLAG_ATTRACT    0x02000000u
#define NM_SLOT_PAD        SIXA_800A6028 // s32[4]: controller driving the slot (1-4), 0 = CPU
#define NM_SLOT_WRESTLER   SIXA_800A4038 // s32[4]: wrestler id
#define NM_SLOT_ELIM_TIME  SIXA_8009FE58 // s32[4]: match second the slot was eliminated, -1 = still in
#define NM_SLOT_HUMAN      SIXA_8009ED20 // u8[4]: 1 = slot driven by a controller (rebuilt every frame)
#define NM_TIME_LIMIT      0x8009FE6Cu // s8: match time limit index (0 = none; demos get 7 = 1 min)
#define NM_MATCH_FRAMES    0x801532C0u // u16: match frame counter, 0xFFFF outside a match
#define NM_VI_COUNT        0x80057404u // u32: +1 per vertical retrace (60 Hz)
#define NM_PADS            0x80063350u // OSContPad[4] (u16 buttons, s8 x, s8 y, u8 err, pad)
#define NM_PRESSED         0x8010663Eu // u16: buttons newly pressed this frame, as the menus and story read them

#define NM_SCENE_STEP      0x800A872Cu // s32: ovl_a's scene sequencer step (func_800EA774)
#define NM_ENTRANCE_STEP   0x800A872Cu// s32: entrance scene step (3 arena, 4 titantron video, 5 stage; 9/10/11 2nd)
#define NM_MENU_RECORDS    0x80158360u // menu screen n's record: + n * 0x138, u8 cursor at +5
#define NM_MENU_STRIDE     0x138
#define NM_RETURN_SCREEN   0x800457A6u // s16: menu screen the menus open on next (0 = title, 2/3/4 a main page)
#define NM_MENU_SOUND      0x8015B9E0u // s32: the sound the menus play at the end of the frame (-1 none)
#define NM_SCREEN_CHANGED  0x8015B7C8u // s16: the menus' "an item was chosen" flag (screen changes run set-up)
#define NM_FADE_FLAGS      0x8016B599u // u8: 0x80 = the match is fading out to leave (a demo: Start)
#define NM_MATCH_QUIT      0x80151ED5u // u8: the match ends as "quit" (the pause menu's QUIT MATCH)
#define NM_MATCH_LEAVE     0x80151EE2u // s16: frames until the match fades out and returns (0 = not leaving)
#define NM_MATCH_DONE      0x80151ED6u // u8: the match loop is finishing
#define NM_MATCH_FADING_IN 0x80151ED0u // u8: the match's opening fade-in is still running
#define NM_MATCH_PAUSED    0x80151EDCu // s16: the match loop's pause state (nonzero: nothing moves)
#define NM_SLOT_COSTUME    SIXA_800A8958 // s32[4] costume (id >> 12)
#define NM_SLOT_COSTUME_B  0x800A9358u // u8[4]
#define NM_SLOT_ID16       0x800BD4D0u // u16[4] wrestler id copy

// wrestler runtime structs (match): + slot * size
#define NM_WRESTLERS       (SIXA_80159990 + 8u)
#define NM_WRESTLER_SIZE   0x358
#define NM_WR_FLAGS        0x82        // u8: 0x40 = lying on the mat
#define NM_WR_FLAG_LYING   0x40
#define NM_WR_ANIM_LEFT    0x9A        // s16: frames left in the current animation
#define NM_WR_ANIM_ID      0x9E        // u16
// entrances
#define NM_ENTRANT_MTX     (SIXA_80071DA0 + 0x1D4u) // float[16] model matrix of the wrestler walking out (stage walk)
#define NM_CAMERA_MATRIX   0x80062BA8u // float[16] view x projection, built by func_80001580

#define WR_AUSTIN 0x0002
#define WR_ROCK   0x0001

// N64 buttons
#define BTN_A 0x8000
#define BTN_B 0x4000
#define BTN_Z 0x2000
#define BTN_START 0x1000
#define BTN_DU 0x0800
#define BTN_DD 0x0400
#define BTN_DL 0x0200
#define BTN_DR 0x0100
#define BTN_L 0x0020
#define BTN_R 0x0010
#define BTN_CU 0x0008
#define BTN_CD 0x0004
#define BTN_CL 0x0002
#define BTN_CR 0x0001

// SDL scancodes used for the cabinet keys
#define SC_1 30
#define SC_2 31
#define SC_5 34
#define SC_6 35
#define SC_F2 59
#define SC_UP 82
#define SC_DOWN 81
#define SC_LEFT 80
#define SC_RIGHT 79
#define SC_RETURN 40

// ---- platform (hooks_recomp.c / hooks_test.c) ----------------------------------------------
int plat_key_down(int scancode);
u32 plat_config(const char* key, u32 fallback);
void plat_host(s32 volume, s32 rumble, s32 deadzone);  // the SOUND & FEEL page's, to the exe
void plat_picture(u32 v);                           // the PICTURE page's settings, to the renderer: the window, its shape, smooth motion, the frame rate, CRT, HDR
void plat_config_set(const char* key, u32 value);   // keeps an option (the next power-on starts with it)
#define ROSTER_OPTS 42             // arcade_roster_opt: Big Show, No Mercy Plus's media, the four new Superstars, Benoit,
enum { RO_BIG_SHOW, RO_MEDIA, RO_EXT0, RO_BENOIT = RO_EXT0 + 4, RO_JARRETT, RO_DROZ, RO_MIDEON, RO_MEANIE,   // the late ones
       RO_PETEGAS, RO_RODNEY, RO_JOEYABS, RO_TAKER99, RO_RAVEN, RO_REGAL, RO_TIGER, RO_BRET, RO_NEIDHART,
       RO_HOGAN, RO_NASH, RO_HALL, RO_SAVAGE, RO_HENNIG, RO_GIANT, RO_STING, RO_LUGER, RO_GOLDBERG, RO_RSTEINER, RO_SSTEINER, RO_BOOKER, RO_STEVIE, RO_REY, RO_PIPER, RO_BISCHOFF,
       RO_BRET_B, RO_SAVAGE_B, RO_SSTEINER_B, RO_BOOKER_B, RO_STEVIE_B, RO_REY_B };   // (the second IDs, gen/revbanks.py: no option of
// (no2k15, 2026-11-23: RO_STONE15 / RO_ROCK15, the two WWE 2K15 test slots, removed with them -- gen/mkroster.py)
#define RO_BANK0 RO_BRET_B     //  their own -- each follows his base's, logic.c arcade_roster_opt)
#define ROSTER_BANKS_N 6
int arcade_roster_opt(int i);
void announce_frame(void);   // announce.c: JR and King at the table
void announce_scene(void);
void announce_table_frame(void);
void announce_watch(void);
void announce_table_move(void);   // the table moved up toward the ring (its pieces' places, its collision)
u32 announce_file(u32 file, u32 dest);
u32 announce_owner(u32 node);   // whose a model node is (QA)
void announce_qa(u32 out);       // JR's and King's bones and roots (QA)
extern int ann_on, comm_speaker;
void arcade_roster_service(int on);   // each frame (the controller read): the roster as the options have it
// roster_frame's mask: bits 0-3 the new Superstars (No Mercy Plus's four); bits 4 and 5 unused since 2026-11-19 (roster: Benoit
// is always in -- his own box, or among the new ones while Big Show has his ordinal: roster.c asks bigshow_is_on)
// the Superstar Select's pages (roster.c): 0x38 bytes each (9 x {u16 ID, u16 ordinal}, u16 count at +0x24, the
// name at +0x26), roster_pages_n of them (the game's thirteen, or select_pages.json's: up to 32); their flags
extern u8 roster_pages_tab[];
extern s32 roster_pages_n;
extern u8 roster_pages_flags[];
#define PAGE_NO_RANDOM 0x01u          // ... not in the Rumble's random draw (the ladies, the hidden, Create-a-Wrestler's)
#define ROSTER_EXT_JARRETT 0x40u      // ... Jeff Jarrett (from WrestleMania 2000); << 1.. the others after him (RO_DROZ..)
#define ROSTER_LATE_N 35           // (ext_late's: Jarrett .. Neidhart, Hogan, Nash, Hall, Savage, Hennig, The Giant, ports4's 10 (Sting ..), the second IDs' 6 (gen/revbanks.py); the mask a u64)
// controllers (the exe's: arcade_pads.cpp)
int plat_pad_name(int player, char* buf, int len);   // 0 none, 1 a known layout, 2 no layout; the name
u32 plat_pad_buttons(int player);                    // bit 0 BACK (coin), 1 START, 2 south, 3 east, 4-7 up down left right
int plat_pad_learn_start(int player);
int plat_pad_learn_read(int player, char* buf, int len);
int plat_pad_learn_apply(int player, char* elements);  // 15 x 16 bytes
void plat_pad_swap(void);
void plat_quit(void);
// the N64 controls' bindings (the exe's arcade_pads.cpp, controls_n64.json; 2026-11-19): dev 0 keyboard (player 1),
// 1 the controllers' default, 2..5 players 1..4's own (a row he hasn't set: the default's); row 0..18 (19: the whole stick) -- A B Z L R START, C up down left right, the d-pad up down left right,
// the stick up down left right, SPECIAL
int plat_bind_name(int dev, int row, char* buf, int len);   // bit 0 bound, bit 1 a player's own row; its text ("NONE")
int plat_bind_start(int dev, int row);                      // 1 a capture started; 0 no controller; 2 only ones without a layout
int plat_bind_poll(void);                                   // 0 waiting, 1 set and saved, 2 cancelled
void plat_bind_clear(int dev, int row);
void plat_bind_reset(int dev);
// row 19 (2026-11-20): the whole analog stick -- its name ("LEFT STICK" / "RIGHT STICK", the keyboard's four keys), a controller's
// capture (the stick moved), a clear of its four rows; plat_bind_stick: a controllers' set's stick on the pads' left (0) / right (1)
int plat_bind_stick(int dev, int which);                    // 1 set and saved
// Game functions the arcade calls (the menus' own code, overlay ovl_c, only while it's loaded):
enum {
    GF_PAGE_INIT,     // func_8012D2EC(page)        main menu pages 2-4
    GF_EXH_INIT,      // func_8012D720()            7  Exhibition
    GF_INIT_5,        // func_8012D614()
    GF_INIT_14,       // func_8013DA18()
    GF_SELECT_INIT,   // func_80110FD0(screen, how) 26 / 27 superstar select (how: 0 set-up, 1 before loading)
    GF_INIT_17,       // func_80132EB4()
    GF_80003714,      // func_80003714(x)           (resident)
    GF_INIT_21,       // func_80133DF8()            rules
    GF_INIT_20,       // func_8013437C()
    GF_INIT_33,       // func_8014227C()
    GF_INIT_34,       // func_80143390()
    GF_INIT_25,       // func_80135534(1)           VS card
    GF_INIT_22,       // func_80133CD8()            championship list
    GF_ACTION,        // func_8010F0A8(action, record, item) -> next screen: the menus' action interpreter
    GF_RULES_PRESET,  // func_80134140(apply)       the match type's default rules (ovl_c)
    GF_SELECT_DONE,   // func_80135728(1)           a Superstar Select is complete (ovl_c)
    GF_FINALIZE,      // func_80136784(0)           the match is set up from the menus' choices (ovl_c)
    GF_SETTINGS_SLOT, // func_8010D874(2)           the match type's remembered-settings slot (ovl_c)
    GF_BUILD_RECORDS, // func_80136CC8(0)           the wrestlers' match records (moves, attributes) from the entrant list (ovl_c)
    // the match's (ovl_e, only while a match runs):
    GF_RR_ENTRANT,    // func_801199F0(slot) -> 1: the Royal Rumble's next entrant is on the way to the slot
    GF_ELIMINATE,     // func_80118A20(winners*, losers*, how, 0): the match's own elimination
    GF_RETARGET,      // func_800EC484(wrestler, 1) -> 1: the wrestler turns to the nearest other one
    GF_FACING,        // func_8012EA9C(from pos*, to pos*) -> the angle that faces from one place to the other
    GF_MATCH_STEP,    // func_80114514(): one step of the match's logic (the match loop's, once a frame)
    // resident:
    GF_PLAYER_SHOW,   // func_800159FC(slot, on): the engine draws / stops drawing the slot's model
    GF_LOOK,          // func_8000B60C(record, look): a wrestler's looks (0x26 bytes) packed into a costume record
    GF_PAGE_MEMBER,   // func_8000AEC8(page, i) -> the Superstar Select page's i-th Superstar (ID)
    // the menus' (ovl_c):
    GF_RR_ELIGIBLE,   // func_80134830(id) -> nonzero: the Superstar can be in a Royal Rumble's random field
    // resident:
    GF_AUDIO_DMA,     // func_8002B12C(addr, len) -> the audio DMA cache's buffer holding them (it reads it if need be)
    GF_SPRITE_RELEASE, // func_800DF5DC(file): the sprite cache lets go of that file's picture
    // the match's (ovl_e):
    GF_ROPES_TEST,    // func_801202F4(wrestler) -> 1: he's at the ropes with them square at his back
                      //   or front (+0xCC: which ropes) -- the over-the-top test of a man reeling
    GF_SET_STATE,     // func_800EB6D4(wrestler, state): his state changes (0x70: over the top rope)
    // Story Mode's (ovl_a / ovl_b):
    GF_TEXT_BANK,     // func_800DD6F4(file, i) -> the i-th string of a text file (ovl_a)
    GF_SCENE_TEXT_BANK, // func_8010CC10(file, i) -> the i-th string of a scene text file (ovl_b)
    GF_RATING,        // func_80138360() -> the match's result as Story Mode rates it (ovl_c)
    GF_COND,          // func_800ED958(bits, rating) -> -1: that result condition holds (ovl_a)
    GF_COND_RANK,     // func_800EDD84(bits & 0x70) -> -1 / 1 (ovl_a)
    GF_COND_STATS,    // func_800EDE48(bits & 0xF00) -> -1 / 1 (ovl_a)
    GF_HISTORY_RUMBLE, // func_800EE084(record): a Royal Rumble's result into the chapter's history (ovl_a)
    GF_PLAY_SONG,     // func_80017C64(song): a song played (tests)
    GF_MENU_TEXT,     // func_800DF3BC(string, style) -> the menus' picture of the string (their lettering; the sprite cache's, ovl_a)
    // the match's (ovl_e):
    GF_GROUND_GRAPPLE, // func_8013E870(wrestler, man, input) -> 1: the ground grapple on a lying man begun, as for a press
                      //   of A (input 0x8000): states 70 / 71 (movex.c movex_chain_step)
};
// The story runner (story.c)
u32 story_card_text(s32 id);         // replaces func_800DD740 (ovl_a)
u32 story_scene_text(s32 id);        // replaces func_8010CC38 (ovl_b)
void story_text_clear(void);
int story_text_set(int i, const char* s);
u32 story_branch(void);              // replaces func_800EE1F0 (ovl_a): the branch taken after a match
void story_set_node(s32 n);           // replaces func_800ED130 (ovl_a): the current node
void story_frame(int keep);          // each frame: a story of ours ends back on the title / championship list
                                     //   (keep: not now -- single player held for a challenger)
void story_install(void);            // an overlay was loaded: a running story's tables go over the engine's
int story_start(u32 pack);
void story_stop(void);
int story_active(void);
int story_count(void);               // the stories built into the mod (mod\stories\*.json -> src/story_packs.h)
const char* story_title(int i);      //   the i-th's name on the menus
int story_belt(int i);               //   the championship it runs as (0 World .. 6 Women's: the list's row)
int story_start_builtin(int i);      //   started (1: ok)
void story_menu_label(const char* s); // the championship list's Custom Story row: its label (0: none)
void story_save_swap(int before);    // round the game's save write (save.c): no story of ours goes into it
s32 story_scene_cond(u32 cmd);       // replaces func_8010D1E0 (ovl_b): a scene's op 0x7F test (ours: the scene ops)
void tron_story(s32 v); int tron_screen_place(int a, float* xyz, float* width);
int announce_where(float* xyz);
s32 story_tron_video(void);          // a story scene's Titantron cut: the video for the next effect 4 (0: none)
void story_qa(u32 q);                // QA: the story's numbers into state.csv (logic.c, qa_story)
int announce_where(float* xyz);      // JR's and King's middle at the table (0: not built; announce.c)
s32 plat_game_call(int fn, u32 a0, u32 a1, u32 a2);
// The single-player Royal Rumble, in the game's own functions (hooks_recomp.c / tramp.S):
s32 arcade_elim_check(s32 slot);     // replaces func_8011723C: the slot is eliminated -> 1: the match is over
s32 arcade_rr_entrant(s32 slot);     // replaces func_801199F0: the slot's next entrant, when one is due
void arcade_rr_frame(void);          // the Rumble's entrance manager (func_8011D400) is about to run
void ring_void_guard(void);          // ... and before it, every match step: a man left at the mat's height past the apron
void arcade_rr_match_setup(void);    // the match's set-up (func_80115000) is done
void arcade_rr_hud(void);            // the match HUD (func_800DEDE0) has updated its panels
void arcade_rr_finalize_pre(void);   // the menus are about to set the match up (func_80136784)
void arcade_rr_finalize_post(void);  // ... and have
// The menus' sprite queue (ovl_a, loaded with the menus): text rasterized in the game's font
// (func_800DF3BC: string, style -> text sprite), and a sprite requested for this frame
// (func_800DD5A4: id or text sprite, x, y, 0, flags), drawn with the queue's own fade.
u32 plat_text_sprite(const char* s, u32 style);
void plat_sprite_request(u32 id, s32 x, s32 y, u32 style, u32 flags);
// The game's text rasterizer (resident func_80009F6C): string, style -> text texture record
// { char* s, u8* texels (I4), u32 bytes, u16 stride (texels), u16 h, u16 w, u16 style }.
u32 plat_game_text(const char* s, u32 style);
void plat_sound(u32 id);           // the game's sound effects (resident func_800173C0)
void plat_game_text_free(u32 rec);   // func_8000A1BC: gives a text texture back

// ---- logic.c ----------------------------------------------------------------------------
// The game's two modes (2026-11-07, "No Mercy Forever"): Regular -- No Mercy's own title, menus and attract, with
// every enhancement of the mod -- and Arcade -- the coin-op (credits, continues, the challenger, INSERT COIN, the
// arcade's routing and menus). The mode is the option game_mode, read once, the first time anything asks
// (nm_mode: the settings' migration runs first), and never again until the next start-up: F2 changes the option only.
enum { MODE_REGULAR, MODE_ARCADE };
int nm_mode(void);                   // the running mode (MODE_REGULAR / MODE_ARCADE), latched at the first call
int nm_arcade(void);                 // nm_mode() == MODE_ARCADE
void arcade_pad_hook(void);          // after the game reads the controllers
void arcade_frame(u32 task);         // once per rendered frame, before the gfx task starts
void arcade_demo_setup_done(void);   // after the game picked the attract demo match
int arcade_entrance_cam(void);       // the stage-walk camera may run
int arcade_story_page(void);        // a story page waits for the player (each frame): 1 = move on
void arcade_menu_sprites(void);      // the menus are about to draw their sprite queue

// ---- gameplay.c ---------------------------------------------------------------------------
void arcade_camera(const float* proj, float* out); // after the game built CAMERA_MATRIX
s32 arcade_down_anim(u32 w, int on);      // ovl_e func_8013FEF4: the lying animation of a knocked-down wrestler (on: FAST GET UP may apply)
int arcade_fast_getup(void);         // (the operator's FAST GET UP)
void arcade_match_step(void);
void arcade_overlay_loaded(void);   // an overlay was loaded (func_8000073C returns)        // the match loop has run a step of the match's logic (func_80114514)

// ---- bigshow.c: Big Show in Benoit's place -------------------------------------------------
void bigshow_frame(int on);             // each frame (the controller read): Big Show wanted (Benoit always is, 2026-11-19)
void bigshow_pack_pre(int moves, u32 src); // the game packs a costume record (0) / a moveset (1) for the save
void bigshow_pack_post(void);
void bigshow_story_tables(void);     // Story Mode's code is about to read its tables of people
u32 bigshow_stand_in(void);          // who the story's Royal Rumbles put in for the player

// ---- roster.c: the roster made larger (the new Superstars, ordinals 102 and up) ----------------
void roster_frame(u64 mask, int media);  // each frame: who's wanted (ROSTER_*) / NM+'s themes and videos are on
void roster_moves_pre(u32 moves, u32 slots, u32 abil_out);  // a moveset is unpacked (func_80005EFC, the match's func_8013CBAC)
void roster_moves_post(void);
const u8* roster_bigshow_moveset(void); // Big Show's moveset, packed (roster_ext.h)
int roster_is_ext_ord(s32 ord);
int roster_is_ext_id(u32 id);
int roster_ord_is_new(s32 ord);        // an ordinal past the game's (a new Superstar's)
int roster_seat_folder(int seat);      // the exe's folder number for the wrestler in a seat, -1: empty (roster.c staging seats)
s32 roster_random_ord(s32 r);          // the Superstar Select's Random: an ordinal
// ---- save.c: the save stays the game's ------------------------------------------------------------
void save_sanitize(void);               // func_80004EE8: before the image is written ...
void save_restore(void);                // ... and after
// ---- parts.c: body parts, with the new Superstars' -----------------------------------------------
const u8* parts_costume_key(int k, int c);   // a new Superstar's costume, packed as the records keep it
void parts_unpack(u32 src, u32 dst);         // func_8001AC44: packed looks -> the 0x3F-byte struct
u32 parts_file(u32 file, u32 dest);          // a file of the new parts: its size (0: not one), read

// ---- media.c: No Mercy Plus's themes and Titantron videos (from the content pack) -------------
void media_frame(int on, int bs, int media, u32 late); // each frame: the arcade on; Big Show's (bs) / everyone's (media) / the themes of Jarrett, Droz, Mideon, the Meanie (late: bit each)
void media_entrance_tables(void);    // the game is about to read the entrance table (a video, the effects)
void media_video_picked(s32 v);      // the game picks video v (func_8010C998 v >= 0 / func_8011E488)
s32 media_video_select(s32 v);       // func_8010C998, replaced: the entrance's video (its frames' step)
void media_file_pre(u32 file, u32 dest); // the game reads a file (func_80003C94)
void media_file_post(void);
int entrance_table_loaded(void);     // the entrance table's overlay is loaded
u32 themes_audio_dma(u32 addr, s32 len); // the audio DMA callback (func_8002B0C0, replaced)
// How many entrance themes can come from wrestler folders. Must match THEME_MAX in the exe's arcade_wrestlers.cpp.
#define FOLDER_THEMES 64
// How many Titantron videos can come from wrestler folders. Must match VIDEO_MAX in the exe's arcade_wrestlers.cpp.
#define FOLDER_VIDEOS 64
// The built-in extras (extras.c): 1 when this copy of the mod holds them -- the ported Superstars, No Mercy Plus's themes
// and videos, the arcade's arenas, and their content packs -- 0 when the first start had only the No Mercy
// ROM to make the mod's tables from (a release without the extras' content files): then none of them is offered.
extern u32 arcade_extras_in;
static inline int arcade_extras(void) { return arcade_extras_in != 0; }
// (phase3 2026-11-19) The built-in extras come from several source games: arcade_extras_in holds one bit per source the
// first start built them from (a build of the mod: all). An extra is in only when every source it needs is (extras.c).
// (no2k15, 2026-11-23: bit 16, EXTRAS_HD2K15 -- the two WWE 2K15 test slots' model packs -- is gone with them; unused)
enum { EXTRAS_REVENGE = 1, EXTRAS_WM2000 = 2, EXTRAS_NMPLUS = 4, EXTRAS_REDUX = 8, EXTRAS_ALL = 15 };
static inline int arcade_extras_has(u32 need) { return arcade_extras_in != 0 && (arcade_extras_in & need) == need; }
u32 arcade_ro_needs(int i);        // the sources roster option i needs (extras.c)
u32 arcade_arena_needs(int k);     // ... the arcade's arena 10 + k
// The content packs, by number: must match PACK_FILES in the exe's src/main/arcade_packs.cpp. After them, a wrestler folder's
// own full-detail model (hd.bin: hd.c, PACK_COUNT + its number), then the folders' themes and videos.
// (no2k15, 2026-11-23: PACK_HD_AUSTIN / PACK_HD_ROCK, hd.bin and hd_rock.bin -- the two 2K15 test slots' models -- removed)
enum { PACK_CONTENT, PACK_WMPORT, PACK_COUNT };
u32 plat_pack(int which);                          // The address of a content pack in game memory.
void plat_content_read(u32 off, u32 dst, u32 len); // bytes of the content pack -> RAM
void plat_wmport_read(u32 off, u32 dst, u32 len);  // ... of WrestleMania 2000's Superstars' files (build/wmport.bin)
// The custom stories in the game folder's stories\*.json (the exe's arcade_stories.cpp): scanned and compiled
// (how many), story i's menu name (its championship 0..6, -1 none), its pack copied into dst (its size, 0 none)
int plat_story_scan(void);
int plat_story_menu(int i, char* buf, int len);
int plat_story_error(int i, char* buf, int len);   // story i's reason it doesn't compile (0: none)
int story_error(int i, char* buf, int len);        //   (story.c: of the list's i-th)
int plat_story_pack(int i, u8* dst, int maxlen);
int plat_pages_pack(u8* dst, int maxlen);   // <program folder>\select_pages.json's pages (exe arcade_pages.cpp): its size, 0: none
void story_rescan(void);   // (story.c: the list made again from the folder)
u32 plat_wmport_rom(u32 off);    // ... its place on the cartridge (the PI's), 0: none (the PC's is in memory)
void plat_guard(u32 at);
int tron_ring_song(u32 id);      // (2026-11-12: his theme file as a song in the ring, sound only: NATIVE_THEME or 0; media.c)
int tron_music(int on);          // (2026-11-10: returns what the exe plays natively for the man entering, NATIVE_* bits; 0 outside an entrance)
// 2026-11-10: themes and Titantron videos from common-format files, played by the exe (arcade_media.cpp). The mod only
// signals: the entering wrestler's ID as his song starts (on 1; variant: 0 for now, kept for named variants), and the
// song's stop (on 0; then id is the fade in the sound driver's ticks, as func_800037BC got it). Returns NATIVE_* bits.
int plat_entrance_media(u32 id, u32 variant, int on);
#define NATIVE_THEME 1           // a theme file of his plays
#define NATIVE_VIDEO 2           // a video file of his plays on the Titantron
#define NATIVE_VIDEO_SOUND 4     // ... with its own sound (he has no theme file)
extern s32 tron_stop_fade;       // func_800037BC's argument, set by its hook before tron_music(0) (0 if the hook doesn't)
void tron_native_cancel(void);   // the exe's part of this entrance called off (media.c: no silent song to play)
s32 arcade_entrant_video(void);  // the entering Superstar's Titantron video, -1 none (logic.c)         // the music started (1: func_800037EC) or stopped / fading (0: func_800037BC) -- tron.c         // where the packs are (they never change), at at: 'GARD', first, end (the exe's QA guard)
// match commentary (commentary.c; the exe plays it: commentary.pak)
void plat_comm_event(int ev, int a, int b, int arg);   // a, b: wrestler ids (-1 none); arg & 0x100: a is a woman
int plat_comm_tick(int in_match);                     // each frame -> 1 while a line plays
void plat_comm_config(int on, int volume, int talk);
void plat_comm_reset(void);
int plat_comm_available(void);                        // lines in commentary.pak (0: none)
void commentary_frame(void);
void commentary_settings(int on, int volume, int talk);

// ---- draw.c ------------------------------------------------------------------------------
#define COL_WHITE  0xFFFFFFFFu
#define COL_YELLOW 0xFFE040FFu
#define COL_RED    0xFF4030FFu
#define COL_GREY   0xA0A0A0FFu
#define COL_CYAN   0x60E0FFFFu
void draw_begin(void);
void draw_text(int col, int row, u32 rgba, const char* s);        // 40 x 30 character grid
void draw_text_center(int row, u32 rgba, const char* s);
void draw_box(int col, int row, int w, int h, u32 rgba);          // grid units
void draw_gtext(const char* s, u32 style, int cx, int y, u32 rgba); // the game's lettering, pixels
void draw_gtext_service(void);       // (controller hook) has the game make / give back the textures
#define GT_PLAIN 0x0000u   // the game's text styles (func_80009F6C)
#define GT_ITALIC 0x0008u
#define GT_GLOW  0x0020u
#define GT_TALL  0x0030u
#define GT_BIG   0x8000u
void draw_rect(int x, int y, int w, int h, u32 rgba);             // pixels (480 x 240 frame)
void draw_frame(int x, int y, int w, int h, int t, u32 rgba);     // outline around a pixel rect
void draw_ptext(const char* s, int x, int y, int size, u32 rgba);  // operator menu: 640 x 480 units, x < 0 centred
int ptext_width(const char* s, int size);
void draw_fill(int x, int y, int w, int h, u32 rgba);               // 640 x 480 units
void draw_menu_frame(void);  // this frame's picture is the overlay alone (the game's list replaced)
void draw_flush(u32 task);  // hands this frame's drawing over to the next scene's end (or the menu's picture)
// the arcade's log for crash / freeze reports (logic.c)
enum { FL_PHASE = 1, FL_GOAL, FL_PLAYER_OUT, FL_BACK_IN, FL_ELIM, FL_MATCH_OVER, FL_COIN, FL_CONTINUE,
       FL_RR_OVER, FL_RR_APRON, FL_RR_TOSS, FL_RR_ENTRANT };
void flight_event(u32 code, u32 a, u32 b);
void arcade_rumble_ai(s32 slot);
int arcade_rumble_ai_on(void);       // the operator's ROYAL RUMBLE > CPU GOES FOR ELIMINATIONS      // func_800F03F0 (the computer player's step) returned
void arcade_rules_preset_done(void); // func_80134140 returned (a match type's RULES preset)
void arcade_menu_action_done(s32 action);   // func_8010F0A8 returned: the menus ran a choice's action (W10: 0x404 the Rumble)
void arcade_number_confirm_done(void);      // func_8012E9F0 returned: the Rumble's NUMBER page confirmed (W10)
void arcade_number_setup(void);             // func_8012E934 / func_8010CE74 begin: NUMBER's set-up, Start's skip-ahead (W10)
void arcade_number_setup_done(void);        // func_8012E934 returned: the NUMBER page made to count by one (W10b)
void arcade_row_update(u32 row);            // func_801079B4 begins / returns: a menu row's per-frame update (W10b: limits)
void arcade_row_update_done(void);
void arcade_qa_sprite_request(u32 id);      // (QA: the menus asked for a sprite -- the title's PRESS START is 15)
void arcade_qa_sprite_draw(u32 req, s32 alpha);   // (QA: the menus drew a queued sprite at this alpha)
void draw_frame_begin(u32 picture);  // func_80002BB4: a frame's display list starts
void draw_scene_end(u32 gfxp);
void dl_big_begin(void);
void tron_frame_begin(u32 picture);           // tron.c: a frame's task starts (func_80002BB4's picture)
void tron_scene_begin(u32 gfxp, s32 slot, s32 first);   // ... the scene's layer starts (matrices' slot, first group)
void tron_scene_end(u32 gfxp);               // ... it's drawn: the Titantron's (the second camera's pass)
void tron_hardcam_build(s32 slot);           // ... the graphics thread takes the frame: the hard camera's list
extern int tron_live, tron_screens;
void tron_video_picked(s32 v);               // the entrance's video picked (media.c)
void tron_main(void);                         // each frame, the main thread's (logic.c): the video's frames
int media_tron_frame(int v, int k, u32 dest, int rom, int* time);
u32 files_read(u32 info, u32 file, u32 dest);
void dl_big_end(void);
void dl_big_done(void);       // func_800028B4 returned: the arcade's drawing joins the frame's list
void draw_scan_textures(u32 task, void (*cb)(u32 addr)); // every texture image the frame's DL loads

// small helpers
int str_len(const char* s);
void str_num(char* out, int n); // writes decimal n (0..999) + NUL

// The match's wrestler slots right now (in a match: the game's count, 0x80151ED7 -- 6 in a six-man match)
static inline int nm_slots(void) { int n = RD8(0x80151ED7u); return (n < 1 || n > NM_SLOTS_CAP) ? 4 : n; }
// The slots a loop over every wrestler goes through: the match's count, or six if it's fewer -- six-man play exactly as
// the loops to NM_SLOTS_MAX had it, and a bigger match's men too. (A slot past the match's count has a zeroed record
// with room for 32, not one marked empty, so a loop doesn't go on to NM_SLOTS_CAP.)
static inline int nm_slot_span(void) { int n = nm_slots(); return n > NM_SLOTS_MAX ? n : NM_SLOTS_MAX; }
// arcctl.c: ARCADE controls (6 buttons)
extern int arc_controls;
void arcctl_reset(void);
void arcctl_port(int port, u16 in, s8 sx, s8 sy, u16* ob, u8* ox, u8* oy);
void arcctl_draw(int port, u32 rgb);
void arcctl_frame(void);
// movex.c: moves done as the Superstar did them in his own game
void movex_pre(s32 slot, s32 late);
void movex_post(const u8* ids);   // (2026-11-13, movelib: ids -- a folder wrestler's slot list, its move IDs; 0: none)
s32 movex_anim(u32 w, s32 who);
s32 anim_frames(s32 file); float anim_speed(s32 file); s32 anim_endpose(s32 file); u32 anim_recpart(s32 file);
s32 anim_pose_now(s32 n); void anim_details(u32 out, s32 file); void anim_stream(s32 n, s32 pri);
#define ANIM_ROM 0x7E000000u   // the pack's animations' "ROM" place (no cartridge has one): anim_stream reads them
// (2026-11-13, movelib) A folder's own moves' animations: their "ROM" places, given by the exe with their game file numbers
// (files.c files_info), from here on; anim_stream reads them from the exe. (No cartridge reaches either.)
#define ANIM_OWN_ROM 0x60000000u
#define ANIM_OWN_ROM_SIZE 0x1E000000u   // (up to ANIM_ROM)
void plat_anim_read(u32 rom, u32 dst, u32 len);   // hooks_recomp.c: a folder animation's bytes at its "ROM" place
void roster_match_moves_pre(s32 slot, u32 moves, u32 bits);
void roster_match_moves_post(void);
void scene_world(u32 n);   // gameplay.c: func_80002268, the world matrices, checked
#endif
void arcade_heap_service(void);
