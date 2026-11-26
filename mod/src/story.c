// The story runner: stories of our own (JSON, made into the game's own Story Mode data) played by the
// game's Story Mode engine (ovl_a: nodes, branches, match set-up; ovl_b: scenes), its tables pointed at
// ours. STORY_NOTES.md has the engine's formats. The game's own seven championship stories are as ever.
//
// The text: two lookups give the engine its words -- func_800DD740(id) (the match card, the requirement
// page, the chapter titles; ovl_a) and func_8010CC38(id) (what's said in a scene; ovl_b, the
// championship's own text file) -- and both are ours: while a story of ours runs, ids from STORY_TEXT0 on
// are its strings; every other id (and every id when none runs) is the game's, looked up exactly as the
// game's own code does (its four patched functions fall through to copies of the game's code).
//
// The stories themselves: mod\stories\*.json, made into packs by gen/storyc.py when the mod is built
// (src/story_packs.h); the championship list's "Custom Story" row (logic.c) starts one.
#include "arcade.h"
#include "gamefn.h"
#ifndef ARCADE_TEST
#include "story_packs.h"
#endif

#define STORY_TEXT0   0xE000u
#ifdef ARCADE_TEST                    // (the rig's memory is all but full: a little)
#define STORY_TEXT_N  64
#define STORY_BUF     2048
#define STORY_SAVE_A  1024              // the engine's tables a story covers, kept to put back
#define STORY_SAVE_B  256
#else
#define STORY_TEXT_N  512
#define STORY_BUF     16384
#define STORY_SAVE_A  8192
#define STORY_SAVE_B  8192
#endif

static u16 st_off[STORY_TEXT_N];      // each string's place in st_buf (+1; 0 = none)
static char st_buf[STORY_BUF];
static u32 st_used;
static const char st_empty[] = "";

// the story's strings: id = STORY_TEXT0 + i
void story_text_clear(void) {
    for (int i = 0; i < STORY_TEXT_N; i++) st_off[i] = 0;
    st_used = 0;
}
int story_text_set(int i, const char* s) {
    if (i < 0 || i >= STORY_TEXT_N) return -1;
    u32 n = 0;
    while (s[n]) n++;
    if (st_used + n + 1 > STORY_BUF) return -1;
    for (u32 k = 0; k <= n; k++) st_buf[st_used + k] = s[k];
    st_off[i] = (u16)(st_used + 1);
    st_used += n + 1;
    return (int)(STORY_TEXT0 + (u32)i);
}
static u32 pack_string(u32 i);
static u32 pack_text(u32 i);
static u32 pk;                          // the pack (0: no story of ours running)
// The championship list's row for a story of ours: its label is a text id the game never asks for (0xF1CB:
// between the 0xF0CE.. table and the first range -- the game's lookup gives "" for it), ours while the
// row is on the list (logic.c: story_menu_label).
#define STORY_LABEL_ID 0xF1CBu
static const char* st_label;
void story_menu_label(const char* s) { st_label = s; }
static u32 ours(s32 id) {
    if (st_label && (u32)id == STORY_LABEL_ID) return (u32)st_label;
    u32 i = (u32)id - STORY_TEXT0;
    if (i >= 0x1000u) return 0;
    u32 p = pack_text(i);
    if (p) return p;
#ifdef ARCADE_TEST
    if (i < STORY_TEXT_N && st_off[i]) return (u32)&st_buf[st_off[i] - 1];
#endif
    if (!pk) return 0;                  // (no story of ours: the game's own answer, below)
    return (u32)st_empty;
}

// func_800DD740(id): ids 0xF0CE.. 0xF1CA from the table at 0x800B6F58; others by the 15 ranges at
// 0x800F3938 {first, last, file} -> func_800DD6F4(file, id - first); else the empty string 0x800F3684
u32 lumberjack_caption(s32 id);   // (lumberjack2: the match card's "Lumberjack Match" -- lumberjack.c)
u32 story_card_text(s32 id) {
    { u32 lc = lumberjack_caption(id); if (lc) return lc; }   // (New feature (Ultimate No Mercy) -- lumberjack2, 2026-11-21)
    u32 o = ours(id);
    if (o) return o;
    if ((u32)(id - 0xF0CE) < 0xFDu) return RD32(0x800B0000u + (u32)id * 4u + 0x6F58u);
    for (u32 i = 0; i < 15; i++) {
        u32 e = 0x800F3938u + i * 6u;
        s32 first = (s32)RD16(e);
        if (id < first || (s32)RD16(e + 2) < id) continue;
        return (u32)plat_game_call(GF_TEXT_BANK, RD16(e + 4), (u32)(id - first), 0);
    }
    return 0x800F3684u;
}

// func_8010CC38(id): the 8 ranges at 0x80125CC4 {first, last, file}, only the current championship's
// (0x80106630) -> func_8010CC10(file, id - first); else the empty string 0x80125CF4
u32 story_scene_text(s32 id) {
    u32 o = ours(id);
    if (o) return o;
    for (s32 i = 0; i < 8; i++) {
        u32 e = 0x80125CC4u + (u32)i * 6u;
        s32 first = (s32)RD16(e);
        if (id < first || (s32)RD16(e + 2) < id) continue;
        if (i != RDS16(0x80106630u)) break;
        return (u32)plat_game_call(GF_SCENE_TEXT_BANK, RD16(e + 4), (u32)(id - first), 0);
    }
    return 0x80125CF4u;
}


// ---- the pack: a story made into the engine's own data (gen/storyc: the compiler) ------------------
// Big-endian, 4-aligned arrays after a 64-byte header:
//   nodes (18 B, the engine's: index 1..), parts (12 B, 1..), scenes (6 B, 1..), script commands (4 B, 0..),
//   branches (20 B, ours), conditions (8 B), sets (4 B), variables' starting values (s32), strings
//   (u16 offsets from the strings' start, then the bytes). Before the strings, after the roles: the arena list
//   (u16 node, u16 arena -- an arena of ours, arena.c; its count the header's +0x38, pad[0]).
// A node's +8 (the engine's branch list) is the first of its branches here; a branch with no conditions
// (game mask 0, none of ours) ends the list and is taken when none before it is.
#define PACK_MAGIC 0x4E4D5350u          // 'NMSP'
typedef struct {
    u32 magic; u16 version, belt, start, n_nodes, n_parts, n_scenes, n_cmds, n_branches, n_conds, n_sets,
    n_strings, n_vars;
    u16 chapters[14];                   // the championship's start / chapter table (0x800F8E90 + 28 belt)
    u16 pad[3];
} PackHeader;                           // (64 bytes)
typedef struct { u32 game_mask; u16 cond_first; u8 cond_n, effect; s16 money; u16 next, scene, set_first; u8 set_n, flags; } SBranch;
typedef struct { u8 kind, op; u16 a; s32 b; } SCond;
typedef struct { u8 kind, a; s16 b; } SSet;
enum { C_VAR = 1, C_CHOICE, C_CHANCE, C_ROLE_IS, C_GAME, C_HOW, C_ROLE_IN, C_AND = 0x10, C_OR, C_NOT };
enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE };
enum { S_SET = 1, S_ADD, S_ROLE_COPY, S_ROLE_SET };
// (2026-11-02, the story upgrade) the pack's extension sections, after the arena list (header pad[1] of them): u16 kind,
// u16 count, the records (gen/storyc.py; STORY_UPGRADE_REPORT.md)
enum { X_RECAST = 1, X_GROUPDEF, X_GROUPMEM, X_VARIANT, X_VOICE, X_SCENEOP, X_MSCRIPT, X_KINDS };
static const u8 x_size[X_KINDS] = { 0, 4, 4, 4, 8, 8, 8, 12 };
static u32 pk_x[X_KINDS], pk_xn[X_KINDS];   // each section's records (0: none)
enum { SO_COND = 1, SO_SET, SO_STORE, SO_TRON, SO_CAM };   // scene ops (op 0x7F, param 0xF000 | op: story_scene_cond)
#define STORY_VARS 32

static const PackHeader* ph;
static u32 pk_nodes, pk_parts, pk_scenes, pk_cmds, pk_branches, pk_conds, pk_sets, pk_vars, pk_roles, pk_xarena, pk_strings;
static int st_armed;                   // started: the engine's story start (func_800ED28C) not seen yet
static s32 st_var[STORY_VARS];
u32 dbg_story_branch = 0xFFFFFFFFu;     // tests: the last branch taken

static u32 st_seed = 0x2545F491u;
static u32 rng(void) { st_seed = st_seed * 1664525u + 1013904223u + RD32(NM_VI_COUNT); return st_seed >> 8; }
static u32 align4(u32 a) { return (a + 3u) & ~3u; }
static int pack_parse(u32 p) {
    const PackHeader* h = (const PackHeader*)p;
    if (h->magic != PACK_MAGIC || h->version != 1) return 0;
    u32 a = p + sizeof(PackHeader);
    pk_nodes = a;    a = align4(a + 18u * h->n_nodes);
    pk_parts = a;    a = align4(a + 12u * h->n_parts);
    pk_scenes = a;   a = align4(a + 6u * h->n_scenes);
    pk_cmds = a;     a = align4(a + 4u * h->n_cmds);
    pk_branches = a; a = align4(a + sizeof(SBranch) * h->n_branches);
    pk_conds = a;    a = align4(a + sizeof(SCond) * h->n_conds);
    pk_sets = a;     a = align4(a + sizeof(SSet) * h->n_sets);
    pk_vars = a;     a = align4(a + 4u * h->n_vars);
    pk_roles = a;    a += 16u;
    pk_xarena = a;   a += 4u * h->pad[0];
    for (u32 k = 0; k < X_KINDS; k++) pk_x[k] = pk_xn[k] = 0;
    for (u32 k = 0; k < h->pad[1]; k++) {
        u32 kind = RD16(a), n = RD16(a + 2);
        if (kind == 0 || kind >= X_KINDS) return 0;       // (a section this mod doesn't know: not its pack)
        pk_x[kind] = a + 4u; pk_xn[kind] = n;
        a += 4u + x_size[kind] * n;
    }
    pk_strings = a;
    ph = h; pk = p;
    return 1;
}
static u32 pack_string(u32 i) {
    if (!pk || i >= ph->n_strings) return 0;
    return pk_strings + 2u * ph->n_strings + RD16(pk_strings + 2u * i);
}

// ---- who's who (2026-11-02): one man in two IDs (variants.c: Undertaker 1999 is the Undertaker) ----
#ifdef ARCADE_TEST
static int same_man(u16 a, u16 b) { return a == b; }
#else
int variant_same_man(u16 a, u16 b);
static int same_man(u16 a, u16 b) { return a && b && variant_same_man(a, b); }
#endif
static u16 role_id(u32 k) { return RD16(0x80097EECu + 2u * (k & 7u)) & 0x0FFFu; }
// (content2 2026-11-19) A group's member is a Superstar by his string ID; the pack has his number: big_show's is 0x0201,
// Chris Benoit's box -- the ROM's ordinal 14 -- while Big Show has it. With Big Show off (no No Mercy Plus: extras.c) that box
// is Benoit's own again, so big_show is no one (Benoit doesn't take his groups), and chris_benoit (0x0205, his number among
// the new Superstars while Big Show is on) is the man in 0x0201 -- as the select's pages read them (roster.c file_id).
int bigshow_is_on(void);
static u16 group_member(u16 m) {
    if (bigshow_is_on()) return m;
    if (m == 0x0201u) return 0;          // big_show: not there
    if (m == 0x0205u) return 0x0201u;    // chris_benoit: in his own box
    return m;
}
static int in_group(u16 id, u32 g) {                // the pack's group g (its members: X_GROUPMEM)
    if (g >= pk_xn[X_GROUPDEF]) return 0;
    u32 d = pk_x[X_GROUPDEF] + 4u * g, first = RD16(d), n = RD16(d + 2);
    for (u32 k = 0; k < n && first + k < pk_xn[X_GROUPMEM]; k++)
        if (same_man(id, group_member(RD16(pk_x[X_GROUPMEM] + 4u * (first + k))))) return 1;
    return 0;
}

// ---- the text as it's served: a line by the player's Superstar (X_VARIANT), voice tokens filled in ----
// A voice token: ESC, 0x80 | field (| 0x40: a named Superstar), then the role (0x80 | k) or the ID (0x80 | id >> 6,
// 0x80 | id & 63). Its words: X_VOICE {id, field, n, first string} -- his, or the default's (id 0). A catchphrase is
// one of his, the same one all story (st_vseed). What's filled in is kept (exp_*) till the roles change.
#define VX_ESC 0x1Bu
#define EXP_N  24
#define EXP_LEN 320
static u16 exp_id[EXP_N];                // (the string + 1; 0 free)
static char exp_buf[EXP_N][EXP_LEN];
static u32 exp_next, st_vseed;
static void exp_clear(void) { for (u32 k = 0; k < EXP_N; k++) exp_id[k] = 0; }
#ifdef ARCADE_TEST
static u16 base_of(u16 id) { return 0; }
#else
u32 variant_base_of(u16 id);     // (a u32 since 2026-11-14: see variants.c)
static u16 base_of(u16 id) { return variant_base_of(id); }
#endif
static u32 voice_line(u16 id, u32 field, u32 pick) {
    u32 best = 0, base = 0, dflt = 0;
    u16 b = base_of(id);                // (a costume variant: his base's voice, unless he has his own)
    for (u32 k = 0; k < pk_xn[X_VOICE]; k++) {
        u32 r = pk_x[X_VOICE] + 8u * k;
        if (RD8(r + 2) != field) continue;
        u16 v = RD16(r);
        if (v == id) { best = r; break; }
        if (b && v == b) base = r;
        if (v == 0) dflt = r;
    }
    if (!best) best = base ? base : dflt;
    if (!best) return 0;
    u32 n = RD8(best + 3);
    return n ? pack_string(RD16(best + 4) + pick % n) : 0;
}
static u32 expand(u32 i, u32 p) {
    for (u32 k = 0; k < EXP_N; k++) if (exp_id[k] == i + 1) return (u32)exp_buf[k];
    u32 e = exp_next++ % EXP_N, o = 0, tok = 0;
    char* out = exp_buf[e];
    for (u32 q = p; RD8(q) && o < EXP_LEN - 1; q++) {
        u8 c = RD8(q);
        if (c != VX_ESC) { out[o++] = (char)c; continue; }
        u8 f = RD8(q + 1);
        if (!f) break;
        u16 id;
        if (f & 0x40) { if (!RD8(q + 2) || !RD8(q + 3)) break; id = (u16)((RD8(q + 2) & 0x3Fu) << 6 | (RD8(q + 3) & 0x3Fu)); q += 3; }
        else { if (!RD8(q + 2)) break; id = role_id(RD8(q + 2) & 0x3Fu); q += 2; }
        u32 w = voice_line(id, f & 0x3Fu, st_vseed + i * 31u + tok++ * 7u);
        if (w) for (; RD8(w) && o < EXP_LEN - 1; w++) out[o++] = (char)RD8(w);
    }
    out[o] = 0;
    exp_id[e] = (u16)(i + 1);
    return (u32)out;
}
static u32 pack_text(u32 i) {
    if (!pk || i >= ph->n_strings) return 0;
    u32 j = i;
    if (pk_xn[X_VARIANT]) {
        u16 me = role_id(0);
        for (u32 k = 0; k < pk_xn[X_VARIANT]; k++) {
            u32 r = pk_x[X_VARIANT] + 8u * k;
            if (RD16(r) != i) continue;
            u16 key = RD16(r + 2);
            if ((key & 0x8000u) ? in_group(me, key & 0x7FFFu) : same_man(me, key)) { j = RD16(r + 4); break; }
        }
    }
    u32 p = pack_string(j);
    if (!p) return p;
    for (u32 q = p; RD8(q); q++) if (RD8(q) == VX_ESC) return expand(j, p);
    return p;
}

// The engine's tables (ovl_a's data: nodes, parts, scenes, the championships' start tables; ovl_b's: the
// scene scripts) are reloaded with their overlays: ours go over them each time one is loaded.
#define G_NODES   0x800F8FA4u
#define G_PARTS   0x800FBE68u
#define G_SCENES  0x80102030u
#define G_STARTS  0x800F8E90u
#define G_SCRIPTS 0x80125CF8u
static void copy(u32 dst, u32 src, u32 n) { for (u32 i = 0; i < n; i++) RD8(dst + i) = RD8(src + i); }
// (loaded: a function of each nobody patches, its first words)
static int ovl_a_here(void) { return RD32(0x800DD6F4u) == 0x27BDFFE8u && RD32(0x800DD6F8u) == 0xAFB00010u; }
static int ovl_b_here(void) { return RD32(0x8010D29Cu) == 0x27BDFFC8u && RD32(0x8010D2A0u) == 0xAFBE0030u; }
u32 dbg_story_installs, dbg_story_ninst;
// What a story covers of the engine's tables is kept (when the overlay holds the game's own) and put back
// when it stops: ovl_a (the menus' and Story Mode's) can stay loaded from the story into the game's own.
static u8 sv_a[STORY_SAVE_A], sv_b[STORY_SAVE_B];
static int sv_a_ok, sv_b_ok;
static u32 n_a(void) { return 18u * ph->n_nodes + 12u * ph->n_parts + 6u * ph->n_scenes + 28u; }
static u32 n_b(void) { return 4u * ph->n_cmds; }
static int same(u32 a, u32 b, u32 n) { for (u32 i = 0; i < n; i++) if (RD8(a + i) != RD8(b + i)) return 0; return 1; }
// (ours: the nodes are -- the parts aren't a test: bigshow.c changes Richards' / Benoit's there, ours too)
static int ours_a(void) { return same(G_NODES + 18u, pk_nodes, 18u * ph->n_nodes); }
static int ours_b(void) { return same(G_SCRIPTS, pk_cmds, 4u * ph->n_cmds); }
static void save_a(int back) {      // (back: the kept copy goes in)
    u32 o = 0;
    #define SV(addr, n) do { u32 n_ = (n); for (u32 i = 0; i < n_; i++, o++) { if (back) RD8((addr) + i) = sv_a[o]; else sv_a[o] = RD8((addr) + i); } } while (0)
    SV(G_NODES + 18u, 18u * ph->n_nodes); SV(G_PARTS + 12u, 12u * ph->n_parts); SV(G_SCENES + 6u, 6u * ph->n_scenes);
    SV(G_STARTS + 28u * ph->belt, 28u);
    #undef SV
}
// A Superstar's own taunts in a scene (2026-11-02; data/storyup/tron, proved in the rig): the scene animation table
// (ovl_b 0x8012588C: u16 animation, u16 flags) takes 0..291 as the actor's own moveset entry -- 227..230 his four
// standing taunts, 235 his winner's -- so entries the game's stories never use (18..21, 24) point at those while a
// story of ours runs (the compilers' "taunt 1".."taunt 4", "win taunt"); put back with the scripts.
#define SCENE_ANIM(k) (0x8012588Cu + 4u * (u32)(k))
static const u8 taunt_at[5] = { 18, 19, 20, 21, 24 };
static const u16 taunt_entry[5] = { 227, 228, 229, 230, 235 };
static u32 sv_anim[5];
// No Superstar in two slots of a match (2026-11-02; data/storyup/six: the engine's func_800EC554 takes a part's first
// candidate that isn't the player (role 0), the champion (role 2; roles 1 and 3 too in the tag championship) -- it
// never checks the men already in the match): the current node's slots of Superstars (not roles) are decided here as
// the engine would, but past anyone in an earlier slot, and that one written as the part's only candidate. A slot of a
// role is the engine's as ever (the recast keeps the roles apart). Again whenever the roles change.
static void resolve_slots(void) {
    if (!pk || !ovl_a_here()) return;
    u32 n = RD16(0x80097ED4u);
    if (n < 1 || n > ph->n_nodes) return;
    u32 node = pk_nodes + 18u * (n - 1u), first = RD16(node + 6);
    u32 count = RD8(0x800F8D98u + RD8(node + 0xD));
    if (count > 4) return;
    u16 used[4];
    u32 nu = 0, belt = RD8(0x80097ED1u);
    for (u32 i = 0; i < count && first + i <= ph->n_parts; i++) {      // (the roles' men first: theirs wherever they are)
        u16 c0 = RD16(pk_parts + 12u * (first + i - 1u));
        if ((c0 & 0x8F00u) == 0x8000u && role_id(c0 & 7u)) used[nu++] = role_id(c0 & 7u);
    }
    for (u32 i = 0; i < count && first + i <= ph->n_parts; i++) {
        u32 src = pk_parts + 12u * (first + i - 1u), dst = G_PARTS + 12u * (first + i);
        if (RD16(src) & 0x8000u) continue;
        u16 pick = 0;
        for (u32 k = 0; k < 5; k++) {
            u16 id = RD16(src + 2u * k) & 0x0FFFu;
            if (!id) continue;
            int out = same_man(id, role_id(0)) || (belt < 7 && same_man(id, role_id(2))) ||
                      (belt == 1 && (same_man(id, role_id(1)) || same_man(id, role_id(3))));
            for (u32 u = 0; u < nu && !out; u++) out = same_man(id, used[u]);
            if (!out) { pick = RD16(src + 2u * k); break; }
        }
        if (!pick) continue;                    // (all taken: the engine's own way, its part as it is)
        RD16(dst) = pick;
        for (u32 k = 1; k < 5; k++) RD16(dst + 2u * k) = 0;
        used[nu++] = pick & 0x0FFFu;
    }
}
void story_install(void) {
    if (!pk) return;
    if (ovl_a_here()) {
        if (!ours_a()) { save_a(0); sv_a_ok = 1; }
        copy(G_NODES + 18u, pk_nodes, 18u * ph->n_nodes);
        copy(G_PARTS + 12u, pk_parts, 12u * ph->n_parts);
        copy(G_SCENES + 6u, pk_scenes, 6u * ph->n_scenes);
        copy(G_STARTS + 28u * ph->belt, (u32)ph->chapters, 28u);
        resolve_slots();
        dbg_story_installs |= 1; dbg_story_ninst++;
    }
    if (ovl_b_here()) {
        if (!ours_b()) { copy((u32)sv_b, G_SCRIPTS, n_b()); for (u32 k = 0; k < 5; k++) sv_anim[k] = RD32(SCENE_ANIM(taunt_at[k])); sv_b_ok = 1; }
        copy(G_SCRIPTS, pk_cmds, 4u * ph->n_cmds);
        for (u32 k = 0; k < 5; k++) RD32(SCENE_ANIM(taunt_at[k])) = (u32)taunt_entry[k] << 16;
        dbg_story_installs |= 2; dbg_story_ninst += 0x10000u;
    }
}
int story_active(void) { return pk != 0; }
// the arena of ours the story's current node's match is held in (its venue the node's: the base), -1 none
int story_xarena(void) {
    if (!pk) return -1;
    for (u32 k = 0; k < ph->pad[0]; k++)
        if (RD16(pk_xarena + 4u * k) == RD16(0x80097ED4u)) return (int)RD16(pk_xarena + 4u * k + 2u);
    return -1;
}

// The save's story in progress (the save image, *0x80062C20 + 0x6D5C, 0xC4 bytes: +0 the node, 0 = none;
// func_800ECFFC writes it at each node, func_800ECEF0 continues from it): a story of ours mustn't be left in
// it -- its node numbers are ours, the game would take them for its own story's. What was there is kept
// when ours starts (and ours starts with none in progress), put back when it stops, and is what the save
// gets while ours runs (save.c: story_save_swap round the write).
#define SAVE_IMAGE_PTR 0x80062C20u
#define SAVE_STORY     0x6D5Cu
#define SAVE_STORY_N   0xC4u
static u8 sv_rec[SAVE_STORY_N], sv_tmp[SAVE_STORY_N];
static u32 sv_rec_img;               // (the image it was kept from; 0: none kept)
static int sv_swapped;
static u32 save_image(void) { u32 img = RD32(SAVE_IMAGE_PTR); return (GAME_PTR(img)) ? img : 0; }
static void rec_keep(void) {
    u32 img = save_image();
    sv_rec_img = img;
    if (!img) return;
    copy((u32)sv_rec, img + SAVE_STORY, SAVE_STORY_N);
    RD16(img + SAVE_STORY) = 0;                          // (no story in progress: ours starts afresh)
}
static void rec_back(void) {
    if (sv_rec_img && save_image() == sv_rec_img) copy(sv_rec_img + SAVE_STORY, (u32)sv_rec, SAVE_STORY_N);
    sv_rec_img = 0;
}
void story_save_swap(int before) {   // (before / after the game writes its save)
    u32 img = save_image();
    if (before) {
        sv_swapped = 0;
        if (!pk || !sv_rec_img || img != sv_rec_img) return;
        copy((u32)sv_tmp, img + SAVE_STORY, SAVE_STORY_N);
        copy(img + SAVE_STORY, (u32)sv_rec, SAVE_STORY_N);
        sv_swapped = 1;
    } else if (sv_swapped) {
        if (img) copy(img + SAVE_STORY, (u32)sv_tmp, SAVE_STORY_N);
        sv_swapped = 0;
    }
}

static u32 st_launch_vi;             // story_start's moment: the list is still up while the game takes the row
void story_stop(void) {
    if (pk) {
        if (sv_a_ok && ovl_a_here() && ours_a()) save_a(1);
        if (sv_b_ok && ovl_b_here() && ours_b()) {
            copy(G_SCRIPTS, (u32)sv_b, n_b());
            for (u32 k = 0; k < 5; k++) RD32(SCENE_ANIM(taunt_at[k])) = sv_anim[k];
        }
        rec_back();
    }
    sv_a_ok = sv_b_ok = 0; st_armed = 0; st_launch_vi = 0;
    pk = 0; ph = 0;
    exp_clear();
    tron_story(0);
}
// The story's beginning: its variables and roles (0..7: 0 the player, 1 the partner, 2 / 3 the champions the
// engine picks; 4..7 and any it names: the story's) as the pack has them.
// A role with a list (X_RECAST: its Superstars in order) takes the first who isn't someone else's already -- the
// player's pick, the partner, the champions, a role with one Superstar, a listed role before it -- or his first
// if all are (2026-11-02: the player picks the rival's Superstar, the rival is the next on the list).
static int role_listed(u32 r) {
    for (u32 k = 0; k < pk_xn[X_RECAST]; k++) if (RD8(pk_x[X_RECAST] + 4u * k) == r) return 1;
    return 0;
}
static int role_taken(u16 id, u32 r) {
    for (u32 k = 0; k < 8; k++) {
        if (k == r || (k > r && role_listed(k))) continue;     // (a listed role after it: not decided yet)
        if (same_man(role_id(k), id)) return 1;
    }
    return 0;
}
static void story_recast(void) {
    for (u32 r = 0; r < 8; r++) {
        u16 first = 0, pick = 0;
        for (u32 k = 0; k < pk_xn[X_RECAST]; k++) {
            u32 e = pk_x[X_RECAST] + 4u * k;
            if (RD8(e) != r) continue;
            u16 id = RD16(e + 2);
            if (!first) first = id;
            if (!pick && !role_taken(id, r)) pick = id;
        }
        if (!first) continue;
        RD16(0x80097EECu + 2u * r) = pick ? pick : first;
        RD8(0x80097EFCu + r) = 0;
    }
}
u32 dbg_story_cast[2];                // tests: roles 4..7's Superstars after the recast (4 | 5 << 16, 6 | 7 << 16)
static void story_begin(void) {
    for (int i = 0; i < STORY_VARS; i++) st_var[i] = i < ph->n_vars ? RDS32(pk_vars + 4u * (u32)i) : 0;
    for (u32 k = 0; k < 8; k++) {
        u16 id = RD16(pk_roles + 2u * k);
        if (!id) continue;
        RD16(0x80097EECu + 2u * k) = id;
        RD8(0x80097EFCu + k) = 0;
    }
    story_recast();
    exp_clear();
    st_vseed = rng();
    dbg_story_cast[0] = role_id(4) | (u32)role_id(5) << 16;
    dbg_story_cast[1] = role_id(6) | (u32)role_id(7) << 16;
}
static void rm_pack_parts(void);       // (2026-11-13, removal: below)
int story_start(u32 pack) {
    story_stop();
    if (!pack_parse(pack)) return 0;
    if (n_a() > STORY_SAVE_A || n_b() > STORY_SAVE_B) { pk = 0; ph = 0; return 0; }
    st_armed = 1;
    st_launch_vi = RD32(NM_VI_COUNT) | 1u;
    rec_keep();
    rm_pack_parts();                     // (2026-11-13, removal: a removed man's places in its parts to his stand-in)
    story_begin();
    story_install();
    return 1;
}

// func_800ED130(n): the current node (the engine's: 0x7ED4 its number, *0x7F14 the node) -- the game's code,
// exactly (no story of ours: nothing else). The engine's story start (func_800ED28C) ends with it, after it
// has cleared the cast and set the player, partner and champions: the story's own roles go in then.
// ---- removed stock Superstars in a story (2026-11-13, removal) ---------------------------------------------------------
// A stock man whose folder is deleted is off the roster (roster.c "removed"); wherever a story still asks for him his
// STAND-IN plays (the user's R8 / R9, 2026-11-13; roster_stand_in: the man after him on his select page, passing over
// anyone removed). Story Mode's own tables are bigshow.c's; here:
//   - the engine's roles (story_removed_roles, at every node, after the engine and the story have cast them): a role a
//     removed man has goes to his stand-in, one no other role has. (The champions' roles never are: a removed man's
//     title is vacated at once, save.c.)
//   - a story of ours (rm_pack_parts, when it starts): a part's candidates -- a match slot, a run-in, a scene's cast --
//     naming him name his stand-in, one not among that part's others. (A cast list passes over him already: the exe's
//     compiler.) Lines keyed by him and "role is" tests naming him aren't changed: they just never come up.
int roster_removed_n(void);
int roster_removed_qa(void);
int roster_id_removed(u32 id);
u16 roster_stand_in(u32 id, int nth);
void removal_s(const char* s); void removal_who(u32 id); void removal_num(s32 v); void removal_say(void);
static void role_put(u32 k, u16 id) {
    const u16 was = role_id(k);
    RD16(0x80097EECu + 2u * k) = id;
    RD8(0x80097EFCu + k) = 0;
    removal_s("story: "); removal_who(was); removal_s(" is removed: "); removal_who(id); removal_s(" stands in, role "); removal_num((s32)k); removal_say();
}
static void story_removed_roles(void) {
    if (roster_removed_n() <= 0) return;
    for (u32 k = 1; k < 8; k++) {
        if (!roster_id_removed(role_id(k))) continue;
        u16 s = 0;
        for (int nth = 0; nth < 8 && !s; nth++) {
            const u16 c = roster_stand_in(role_id(k), nth);
            if (!c) break;
            int taken = 0;
            for (u32 j = 0; j < 8; j++) if (j != k && same_man(role_id(j), c)) taken = 1;
            if (!taken) s = c;
        }
        if (s) role_put(k, s);
        else { removal_s("story: "); removal_who(role_id(k)); removal_s(" is removed and nobody can stand in: he keeps role "); removal_num((s32)k); removal_say(); }
    }
}
static void rm_pack_parts(void) {
    if (roster_removed_n() <= 0 || !pk) return;
    int n = 0;
    for (u32 i = 0; i < ph->n_parts; i++) {
        const u32 part = pk_parts + 12u * i;
        for (u32 k = 0; k < 5; k++) {
            const u16 v = RD16(part + 2u * k), id = v & 0x0FFFu;
            if ((v & 0x8000u) || !id || !roster_id_removed(id)) continue;
            u16 s = 0;
            for (int nth = 0; nth < 8 && !s; nth++) {
                const u16 c = roster_stand_in(id, nth);
                if (!c) break;
                int there = 0;
                for (u32 j = 0; j < 5; j++) if (j != k && !(RD16(part + 2u * j) & 0x8000u) && same_man(RD16(part + 2u * j) & 0x0FFFu, c)) there = 1;
                if (!there) s = c;
            }
            if (!s) continue;
            RD16(part + 2u * k) = (u16)((v & 0xF000u) | s);
            if (!n) { removal_s("story of ours: "); removal_who(id); removal_s(" is removed: "); removal_who(s); removal_s(" stands in for him in its matches and scenes"); removal_say(); }
            n++;
        }
    }
    if (n) { removal_s("story of ours: "); removal_num(n); removal_s(" places of removed men given to stand-ins"); removal_say(); }
}
static void story_qa_roles(void) {           // (QA qa_removal 2: the roles at each node, for comparing runs)
    if (!(roster_removed_qa() & 2)) return;
    removal_s("qa story node "); removal_num(RD16(0x80097ED4u)); removal_s(" belt "); removal_num(RD8(0x80097ED1u)); removal_s(" roles:");
    for (u32 k = 0; k < 8; k++) { removal_s(" "); removal_who(role_id(k)); }
    removal_say();
}
void story_set_node(s32 n) {
    RD16(0x80097ED4u) = (u16)n;
    RD32(0x80097F14u) = G_NODES + 18u * (u32)n;
    if (pk && st_armed) { st_armed = 0; st_launch_vi = 0; story_begin(); }
    story_removed_roles();                   // (2026-11-13, removal: before the node's slots are decided)
    resolve_slots();
    story_qa_roles();
}

// Each frame: a story of ours ends when the menus are back on the title or the championship list (the
// story done, the game over, or left) -- the engine's tables go back to the game's. (keep: the arcade is
// holding single player for a challenger's match, to pick the story up again after it.) Started from the
// list, the list is still up a moment while the game takes the row: a story not under way 5 s on (the row
// not taken) is stopped.
static void story_match_frame(void);
void story_frame(int keep) {
    story_match_frame();
    if (RDS16(NM_SCREEN) != 23) tron_story(0);       // (a Titantron shot ends with its scene)
    if (!pk || RDS32(NM_MODE) != 0 || keep) return;
    s32 scr = RDS16(NM_SCREEN);
    if (st_launch_vi) {
        if (scr == 22 && RD32(NM_VI_COUNT) - st_launch_vi < 300u) return;
        if (scr != 22 && scr != 0) { st_launch_vi = 0; return; }       // (off the list: it's under way)
    }
    if (scr == 0 || scr == 22) story_stop();
}

// The stories built into the mod (src/story_packs.h, made by gen/storyc.py from mod\stories\*.json)
#ifdef ARCADE_TEST
int story_count(void) { return 0; }
int story_error(int i, char* buf, int len) { if (len > 0) buf[0] = 0; return 0; }
const char* story_title(int i) { return ""; }
int story_belt(int i) { return 0; }
int story_start_builtin(int i) { return 0; }
void story_rescan(void) { }
#else
// ... and the stories in the game folder's stories\*.json (2026-10-28 evening, the user: the Custom Story list is
// the JSON files there): the exe compiles them (arcade_stories.cpp, storyc.py's format) when the list is made; a
// story is fetched into rt_pack when it's started. With none there (or an exe without them), the built-in ones.
// How many stories the Custom Story list can hold. The list scrolls, so this is only the size of its table.
#define RT_MAX 4096
static int rt_n = -1;
static char rt_menu[RT_MAX][28];
static u8 rt_belt[RT_MAX];
// The compiled story being played. This only limits the file: its parts are still limited by the game's tables (story_start).
static u8 rt_pack[0x100000] __attribute__((aligned(16)));
void story_rescan(void) {
    int n = plat_story_scan();
    if (n > RT_MAX) n = RT_MAX;
    int k = 0;
    for (int i = 0; i < n; i++) {
        int b = plat_story_menu(i, rt_menu[k], (int)sizeof rt_menu[k]);
        if (b < 0 || b > 7) continue;            // (7: one that doesn't compile -- listed, its reason shown when it's picked)
        rt_menu[k][sizeof rt_menu[k] - 1] = 0;
        rt_belt[k] = (u8)b;
        k++;
    }
    rt_n = n > 0 ? k : 0;
}
static int rt_on(void) { if (rt_n < 0) story_rescan(); return rt_n > 0; }
int story_count(void) { return rt_on() ? rt_n : STORY_PACKS_N; }
const char* story_title(int i) {
    if (rt_on()) return i >= 0 && i < rt_n ? rt_menu[i] : "";
    return i >= 0 && i < STORY_PACKS_N ? story_packs[i].menu : "";
}
int story_belt(int i) {
    if (rt_on()) return i >= 0 && i < rt_n ? (int)rt_belt[i] : 0;
    return i >= 0 && i < STORY_PACKS_N ? (int)story_packs[i].belt : 0;
}
int story_error(int i, char* buf, int len) {
    if (len > 0) buf[0] = 0;
    if (!rt_on() || i < 0 || i >= rt_n || rt_belt[i] != 7) return 0;
    return plat_story_error(i, buf, len);     // (every story the exe lists is on ours: the same index)
}
int story_start_builtin(int i) {
    if (rt_on()) {
        if (i < 0 || i >= rt_n || plat_story_pack(i, rt_pack, (int)sizeof rt_pack) <= 0) return 0;
        return story_start((u32)rt_pack);
    }
    return i >= 0 && i < STORY_PACKS_N ? story_start((u32)story_packs[i].data) : 0;
}
#endif

// ---- which way the story goes after a match: func_800EE1F0, the engine's, with ours ------------------
// The engine's: the chapter's history record gets the match's rating (func_80138360; a Royal Rumble's
// func_800EE084), then the node's branch records (0x800FA720 + 8 n: condition, effect, money, next node,
// scene after) are tried in turn -- condition 0 is the default -- each condition a mask (0x800F8DC4) of
// result fields, every one tested: fields 0..4 by mask 7 / 0x3000 / 0x70 (func_800EDD84) / 0xF00
// (func_800EDE48) / 0x70000, bits 20..31 one each; func_800ED958(bits, rating) -> -1 when it holds.
static s32 game_cond(u32 mask, s32 rating) {
    static const u32 fmask[5] = { 7u, 0x3000u, 0x70u, 0xF00u, 0x70000u };
    s32 a = 0;
    for (u32 f = 0; f < 32; f++) {
        u32 fm = f < 5 ? fmask[f] : f < 20 ? 0 : (1u << f);
        if (!(mask & fm)) continue;
        if (f == 2) a = plat_game_call(GF_COND_RANK, mask & 0x70u, 0, 0);
        else if (f == 3) a = plat_game_call(GF_COND_STATS, mask & 0xF00u, 0, 0);
        else a = plat_game_call(GF_COND, mask & fm, (u32)rating, 0);
        if (a == 1) return 0;
    }
    return a < 0;
}
static s32 cmp(s32 x, int op, s32 y) {
    switch (op) { case OP_EQ: return x == y; case OP_NE: return x != y; case OP_LT: return x < y;
                  case OP_LE: return x <= y; case OP_GT: return x > y; default: return x >= y; }
}
// Our terms (2026-11-02: a list all to hold, as before, or a postfix program -- the branch's flags & 1, a scene op's
// always). "win" / "lose" in a scene test: the last match's, as its branch saw it (st_res: 1 win, 2 lose, 4 notlose).
static u32 st_res;
static u8 st_how;                       // how the last match ended (HOW_*: story_match_frame), 0 not known
static int term(const SCond* c, int br, s32 rating) {
    switch (c->kind) {
    case C_VAR: return cmp(c->a < STORY_VARS ? st_var[c->a] : 0, c->op, c->b);
    case C_CHOICE: return cmp(RD8(0x80097ED2u), c->op, c->b);
    case C_CHANCE: return (s32)(rng() % 100u) < c->b;
    case C_ROLE_IS: { int s = same_man(role_id(c->a), (u16)c->b); return c->op == OP_NE ? !s : s; }
    case C_ROLE_IN: return in_group(role_id(c->a), (u32)c->b);
    case C_GAME: return br ? game_cond((u32)c->b, rating) : ((u32)c->b & ~st_res) == 0;
    case C_HOW: return cmp(st_how, c->op, c->b);
    default: return 0;
    }
}
static int run_conds(u32 first, u32 n, int prog, int br, s32 rating) {
    u8 stk[16];
    u32 sp = 0;
    for (u32 i = 0; i < n; i++) {
        const SCond* c = (const SCond*)(pk_conds + sizeof(SCond) * (first + i));
        if (!prog) { if (!term(c, br, rating)) return 0; continue; }
        if (c->kind == C_AND || c->kind == C_OR) {
            if (sp < 2) return 0;
            sp--;
            stk[sp - 1] = c->kind == C_AND ? (stk[sp - 1] && stk[sp]) : (stk[sp - 1] || stk[sp]);
        } else if (c->kind == C_NOT) {
            if (sp < 1) return 0;
            stk[sp - 1] = !stk[sp - 1];
        } else {
            if (sp >= 16) return 0;
            stk[sp++] = (u8)term(c, br, rating);
        }
    }
    return prog ? (sp == 1 && stk[0]) : 1;
}
static int our_conds(const SBranch* b, s32 rating) { return run_conds(b->cond_first, b->cond_n, b->flags & 1, 1, rating); }
static void apply_sets(u32 first, u32 n) {
    for (u32 i = 0; i < n; i++) {
        const SSet* t = (const SSet*)(pk_sets + sizeof(SSet) * (first + i));
        switch (t->kind) {
        case S_SET: if (t->a < STORY_VARS) st_var[t->a] = t->b; break;
        case S_ADD: if (t->a < STORY_VARS) st_var[t->a] += t->b; break;
        case S_ROLE_COPY:                    // role a takes role b's Superstar (and costume)
            RD16(0x80097EECu + 2u * (t->a & 7u)) = RD16(0x80097EECu + 2u * ((u32)t->b & 7u));
            RD8(0x80097EFCu + (t->a & 7u)) = RD8(0x80097EFCu + ((u32)t->b & 7u));
            exp_clear(); resolve_slots();
            break;
        case S_ROLE_SET: RD16(0x80097EECu + 2u * (t->a & 7u)) = (u16)t->b; RD8(0x80097EFCu + (t->a & 7u)) = 0; exp_clear(); resolve_slots(); break;
        }
    }
}
static void our_sets(const SBranch* b) { apply_sets(b->set_first, b->set_n); }

// func_8010D1E0(cmd): the scene script's op 0x7F test (ovl_b; its runner func_8010D29C skips byte1 commands when it
// says so: nonzero) -- the game's code exactly, but for a scene op of ours (a story of ours running, F & 8 clear, the
// param 0xF000 | op: a cast test the engine never makes, cast 13): SO_COND -- F & 1 skip when it fails (then), F & 4
// skip when it held (else: the then's answer, kept), else skip when it holds; SO_SET / SO_STORE do it, skip nothing.
static u8 so_latch[0x1000 / 8];
static s32 st_tron_v;                   // the Titantron cut's video (SO_TRON), taken by the next effect 4
s32 story_tron_video(void) { s32 v = st_tron_v; st_tron_v = 0; return pk ? v : 0; }
// the arena the current node's scene is in (its match's; arena_current() is still the last match's before its match): an
// arena of ours, or the node's venue
static int story_arena_now(void) {
    int x = story_xarena();
    if (x >= 0) return x;
    u32 n = RD16(0x80097ED4u);
    return (pk && n >= 1 && n <= ph->n_nodes) ? (int)(RD8(pk_nodes + 18u * (n - 1u) + 0xAu) & 0xFu) : 0;
}
// A shot of our own, by the game's own camera (ovl_a; func_800E2120 / func_800E1C2C, what a scene's op 0x60 ends in; func_800E2388
// makes the frame's camera with guPosition): where it is (0x80104478) and how it's turned (0x80104484: degrees -- roll about
// x, then y, then z; forward (0, 0, 1) when all are 0: the pitch and the heading a look-at point needs are worked out here),
// the eye and the point looked at now (0x80104490, 0x8010449C), the place again (0x801044A8), the path's six values
// (0x80104540) with none left to run (0x80104528), the shot's memory (0x800F5594) off so the scene's next shot is taken. So the
// scene, its bubbles and all, draw as for any shot of the game's.
static float cam_sqrt(float v) { if (v <= 0) return 0; float x = v > 1 ? v : 1; for (int i = 0; i < 20; i++) x = 0.5f * (x + v / x); return x; }
static float cam_atan2_deg(float y, float x) {
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (ax < 1e-6f && ay < 1e-6f) return 0;
    int swap = ay > ax;
    float t = swap ? ax / ay : ay / ax, t2 = t * t;
    float a = t * (0.9998660f + t2 * (-0.3302995f + t2 * (0.1801410f + t2 * (-0.0851330f + t2 * 0.0208351f))));   // radians, 0..pi/4
    if (swap) a = 1.5707963f - a;
    if (x < 0) a = 3.1415927f - a;
    if (y < 0) a = -a;
    return a * 57.29578f;
}
static void story_cam_cut(float ex, float ey, float ez, float lx, float ly, float lz) {
    // (2026-11-14, from the native mod's triage: cam_sqrt is pure, and the compiled code works n out first and h only after the
    //  early return -- the C now says so. The same results.)
    float dx = lx - ex, dy = ly - ey, dz = lz - ez, n = cam_sqrt(dx * dx + dy * dy + dz * dz);
    if (n < 1.0f) return;
    float h = cam_sqrt(dx * dx + dz * dz);
    const float v[6] = { ex, ey, ez, cam_atan2_deg(-dy, h), cam_atan2_deg(dx, dz), 0.0f };
    const float look[3] = { ex + dx / n * 500.0f, ey + dy / n * 500.0f, ez + dz / n * 500.0f };
    for (u32 k = 0; k < 6; k++) {
        *(volatile float*)(0x80104478u + 4u * k) = v[k];
        *(volatile float*)(0x80104540u + 4u * k) = v[k];
        RD32(0x80104528u + 4u * k) = 0;
    }
    for (u32 k = 0; k < 3; k++) {
        *(volatile float*)(0x80104490u + 4u * k) = v[k];
        *(volatile float*)(0x8010449Cu + 4u * k) = look[k];
        *(volatile float*)(0x801044A8u + 4u * k) = v[k];
    }
    RDS32(0x800F5594u) = -1;
    RD32(0x80133C68u) = 0x100u;              // (ovl_b func_8010D9A4: the shot taken (0x100), no men to look at (low nibble) -- it would turn the camera on them)
    *(volatile s32*)0x80106138u = 0;
}
u32 dbg_story_scene;                    // tests: scene ops run | the last test's answer << 16 | its op << 20
s32 story_scene_cond(u32 c) {
    u8 f = RD8(c);
    u16 prm = RD16(c + 2u);
    if (pk && !(f & 8) && (prm & 0xF000u) == 0xF000u) {
        u32 k = prm & 0xFFFu;
        if (k >= pk_xn[X_SCENEOP]) return 0;
        u32 r = pk_x[X_SCENEOP] + 8u * k;
        u32 kind = RD8(r), n = RD8(r + 1), first = RD16(r + 2), a = RD16(r + 4);
        dbg_story_scene = ((dbg_story_scene + 1u) & 0xFFFFu) | (dbg_story_scene & 0xFFFF0000u);
        if (kind == SO_SET) { apply_sets(first, n); return 0; }
        if (kind == SO_CAM) {            // JR and King at their table: skip the next command (the game's ring shot with the
            float c[3];                  // table in frame, for when they aren't built) if the close shot's taken
            if (!announce_where(c)) return 0;
            story_cam_cut(c[0], c[1] + 110.0f, c[2] + 330.0f, c[0], c[1] + 104.0f, c[2]);
            return -1;
        }
        if (kind == SO_TRON && (a & 0x80u)) { tron_story(0); return 0; }   // (the Titantron shot over)
        if (kind == SO_TRON && (a & 0x40u)) {      // (on the stage: the close set's screen is in frame already)
            u16 w = RD16(0x800BD900u + 2u * (a & 7u));
            u32 v = (w & 0xFFFu) ? func_8000BBD0(w & 0xFFFu, (w >> 12) & 3u) : 0;
            tron_story((s32)v > 0 ? (s32)v : 0);
            return -1;
        }
        if (kind == SO_TRON) {           // in the ring: the arena's own screen, close, his video on it. An arena with no
            // screen of its own: the game's Titantron cut, full screen. A man with no video: the bubble alone.
            {
                u16 w = RD16(0x800BD900u + 2u * (a & 7u));
                u32 v = (w & 0xFFFu) ? func_8000BBD0(w & 0xFFFu, (w >> 12) & 3u) : 0;
                float c[3], sw;
                if ((s32)v > 0 && tron_screen_place(story_arena_now(), c, &sw)) {
                    float d = sw * 1.5f;  // (the lens: 1.96 x the distance is the picture's width; so the screen ~ 3/4 of it)
                    tron_story((s32)v);
                    { extern void tron_story_arena(int a); tron_story_arena(story_arena_now()); }
                    story_cam_cut(c[0] - c[0] * 0.25f, c[1] - d * 0.10f, c[2] - d, c[0], c[1], c[2]);
                    return -1;
                }
            }
            // his entrance video for the next command,
            u16 w = RD16(0x800BD900u + 2u * (a & 7u));   // effect 4 (media.c: story_tron_video) -- none: skip the effect
            u32 v = (w & 0xFFFu) ? func_8000BBD0(w & 0xFFFu, (w >> 12) & 3u) : 0;
            st_tron_v = (s32)v > 0 ? (s32)v : 0;
            return st_tron_v ? 0 : -1;
        }
        if (kind == SO_STORE) { if (a < STORY_VARS) st_var[a] = (s32)RD8(0x80097ED2u) + 1; return 0; }
        if (kind != SO_COND) return 0;
        int v;
        if (f & 4) v = (so_latch[k >> 3] >> (k & 7)) & 1;
        else {
            v = run_conds(first, n, 1, 0, 0);
            if (v) so_latch[k >> 3] |= (u8)(1u << (k & 7)); else so_latch[k >> 3] &= (u8)~(1u << (k & 7));
            dbg_story_scene = (dbg_story_scene & 0xFFFFu) | (u32)(v & 1) << 16 | (k & 0xFFFu) << 20;
        }
        return (f & 1) ? (v ? 0 : -1) : (v ? -1 : 0);
    }
    s32 hit = 0;
    if (f & 8) hit = RD8(0x80097ED2u) == prm ? -1 : 0;
    else {
        u32 top = prm & 0xF000u;
        u16 v = top == 0 ? RD16(0x80097EECu) : top == 0x1000u ? RD16(0x80097EF0u) : RD16(0x800BD900u + ((top - 0x2000u) >> 11));
        if (v == (prm & 0xFFFu)) hit = -1;
    }
    if (hit) return (f & 1) ? 0 : -1;
    return (f & 1) ? -1 : 0;
}

static u8 st_rec[8];
static u16 st_retry_node, st_retries;   // (a "gameover" branch: the node's retries used)
u32 story_branch(void) {
    u32 node = RD32(0x80097F14u);
    u32 first = RD16(node + 8);
    u32 hist = 0x80097ED0u + 0x3Eu + RD8(0x80097ED0u) * 10u;
    s32 rating = plat_game_call(GF_RATING, 0, 0, 0);
    if (RD32(0x800A957Cu) & 0x200u) plat_game_call(GF_HISTORY_RUMBLE, hist, 0, 0);
    else RD8(hist + 9) = (u8)rating;
    if (!pk) {                       // (the game's own story: its code, exactly)
        u32 r = 0x800FA720u + first * 8u;
        for (; RD8(r); r += 8) if (game_cond(RD32(0x800F8DC4u + RD8(r) * 4u), rating)) return r;
        return r;
    }
    st_res = (game_cond(1u, rating) ? 1u : 0u) | (game_cond(2u, rating) ? 2u : 0u) | (game_cond(4u, rating) ? 4u : 0u);
    for (u32 i = first; i < ph->n_branches; i++) {
        const SBranch* b = (const SBranch*)(pk_branches + sizeof(SBranch) * i);
        int last = !b->game_mask && !b->cond_n;
        if (!last && b->game_mask && !game_cond(b->game_mask, rating)) continue;
        if (!last && !our_conds(b, rating)) continue;
        our_sets(b);
        dbg_story_branch = i;
        if (!b->scene) {
            u32 sc = RD16(node + 2);                       // (the node's scene: its script's end)
            if (sc && sc <= ph->n_scenes) {
                u32 q = RD16(pk_scenes + 6u * (sc - 1u) + 2u);
                while (q < ph->n_cmds && RD8(pk_cmds + 4u * q) != 0x00u) q++;
                if (q < ph->n_cmds) RD16(0x80097EEAu) = (u16)q;
            }
        }
        st_rec[0] = 1; st_rec[1] = b->effect;
        st_rec[2] = (u8)((u16)b->money >> 8); st_rec[3] = (u8)b->money;
        // next 0, "gameover" (found 2026-11-01, QA story_retry): the engine's own retry (its phase 6 -> 4) doesn't get a match
        // past the arcade's card and results (no match, the old result), and its game over (phase 10) goes round for ever. A
        // retry is the node again by the way every node comes (its scene, its card, its match); none left: the engine's phase 10,
        // the arcade's GAME OVER (logic.c).
        u16 nx = b->next;
        if (!nx) {
            u16 cur = RD16(0x80097ED4u);
            if (st_retry_node != cur) { st_retry_node = cur; st_retries = 0; }
            if (st_retries < (RD8(node + 0xFu) & 0xFu)) { st_retries++; nx = cur; }
            else RD8(0x80097ED3u) = 0xFF;       // (func_800EECE0: retries used not under the allowed -> phase 10)
        } else st_retry_node = 0;
        st_rec[4] = (u8)(nx >> 8); st_rec[5] = (u8)nx;
        st_rec[6] = (u8)(b->scene >> 8); st_rec[7] = (u8)b->scene;
        return (u32)st_rec;
    }
    st_rec[0] = 0; st_rec[1] = 0; st_rec[2] = st_rec[3] = st_rec[4] = st_rec[5] = st_rec[6] = st_rec[7] = 0;
    return (u32)st_rec;
}

// the pack's strings are the text ids from STORY_TEXT0 (ours() above reads them first)
#ifdef ARCADE_TEST
u32 roster_ord_to_id(s32 ord); s32 roster_entrance_song(u32 id, s32 c);
// tests: a few strings of ours
u32 dbg_story_cmd, dbg_story_id, dbg_story_ptr;
void story_test_frame(void) {
    if (dbg_story_cmd == 2) { dbg_story_cmd = 0; dbg_story_ptr = story_card_text((s32)dbg_story_id); }
    if (dbg_story_cmd == 3) { dbg_story_cmd = 0; dbg_story_ptr = story_scene_text((s32)dbg_story_id); }
    if (dbg_story_cmd == 4) { dbg_story_cmd = 0; dbg_story_ptr = roster_ord_to_id((s32)dbg_story_id); }
    if (dbg_story_cmd == 5) { dbg_story_cmd = 0; dbg_story_ptr = func_800007D4(dbg_story_id); }   // (room for a pack)
    if (dbg_story_cmd == 6) { dbg_story_cmd = 0; dbg_story_ptr = (u32)story_start(dbg_story_id); }
    if (dbg_story_cmd == 7) { dbg_story_cmd = 0; story_stop(); }
    if (dbg_story_cmd == 8) { dbg_story_cmd = 0; plat_game_call(GF_PLAY_SONG, dbg_story_id, 2, 1); }   // (music tests)
    if (dbg_story_cmd == 9) { dbg_story_cmd = 0; dbg_story_ptr = (u32)roster_entrance_song(dbg_story_id, 0); }
    if (dbg_story_cmd == 1) {
        dbg_story_cmd = 0;
        story_text_clear();
        story_text_set(0, "STEEL CAGE GRUDGE MATCH");
        story_text_set(1, "Win this one and the title shot is yours.\nLose, and you go to the back of the line.");
        story_text_set(2, "You think you can just walk in here\nand take what's mine?");
        story_text_set(3, "Tonight, %c01, I end your little run.");
    }
}
#endif

// QA (logic.c, qa_story): state.csv's bs_tries / bs_grabs = roles 4 | 5 << 16 / 6 | 7 << 16, bs_step = the scene ops
// run | the last scene test's answer << 16 | its op << 20, pal_wm_skipped = how the last match ended, pal_room_skipped /
// pal_calls = variables 0 / 1, rr_put_out = variable 2, rr_kept_out = the match script's triggers fired (a bit each)
void story_qa(u32 q) {
    if (!pk) return;
    RD32(q) = role_id(4) | (u32)role_id(5) << 16; RD32(q + 4) = role_id(6) | (u32)role_id(7) << 16; RD32(q + 8) = dbg_story_scene;
    RD32(q + 0xC) = (u32)st_how | ((u32)st_var[0] & 0xFFu) << 8 | (u32)(st_var[1] & 0xFFFF) << 16;
    extern u32 dbg_story_ms;
    RD32(q + 0x10) = ((u32)st_var[2] & 0xFFFFu) | (dbg_story_ms & 0x3FFFu) << 16 | (RD32(0x800A9950u) & 0x2000u ? 1u : 0u) << 30 | (RD8(0x800A9462u) ? 1u : 0u) << 31;   // (top bits: the DQ rule on, count-outs on)
}

// ---- the match as it plays (2026-11-02; data/storyup/result: every address proved in the N64 rig) ----------------
// How it ended: 0x800A10EC once the result is up (0x801594D0) with 0x400 (the match over) -- how | team | winners 0-3 |
// losers 4-7 -- kept for the branch's "win by ...". The match's script (X_MSCRIPT: u16 node, u8 when, u8 its role, u16
// its number, u8 do, u8 a1..a3): each trigger once, while the match runs and isn't over.
#define TB(s)          (SIXA_80159990 + 0x358u * (u32)(s))   // (the game's wrestler records: the mod's W(s) is TB + 8)
#define NM_RESULT_W    0x800A10ECu
#define NM_RESULT_UP_W 0x801594D0u
#define NM_RULES_W     0x800A9950u     // the live rules: DQ 0x2000
#define NM_COUNTOUT_W  0x800A9462u     // 0 none, 1 ten count, 2 twenty
#define NM_CLOCK_M     0x8016B930u     // the HUD's clock (s32 minutes, seconds)
#define NM_CLOCK_S     0x8016B934u
#define NM_RUNIN_W     SIXA_800AA078   // u32 a side: 0x1nn = he comes at nn seconds (0x1FF: held; 0x100: now)
enum { MS_TIME = 1, MS_HEALTH, MS_OUTSIDE, MS_HOLD, MS_FINISHER };
enum { MA_END = 1, MA_RUNIN, MA_NODQ, MA_NOCOUNT };
static const u16 lost_by[8] = { 0, 0x0008, 0x0020, 0x0040, 0x0400, 0x0010, 0x0800, 0x1000 };   // (END_HOW: pin, submit, TKO, DQ, count-out, ladder, cage)
u32 dbg_story_ms;                      // tests: the triggers fired (bit k: the node's k-th)
static int ms_live;
static u16 ms_move[8];
// A script's count-out (found 2026-11-02, QA story_triggers: "lose by countout" never true): func_80117438 calls a count-out by who's out at the
// bell, and with the sides level (a script's men may be anywhere) a draw; holding the men in and out for it didn't answer (2 runs). So func_80116CDC's
// call of it (the lost bit 0x10), a script's count-out on: what it ends in with a side out, the game's func_80118A20 with the winner, the loser, count-out + over.
static s8 ms_co_win, ms_co_lose;
extern s32 func_80117438(u32 how, s32 a1);
extern void func_80118A20(s32* win, s32* lose, u32 how, s32 a3);
s32 story_co_call(u32 how, s32 a1) {
    if (ms_co_win) {
        s32 w[4] = { ms_co_win - 1, -1, -1, -1 }, l[4] = { ms_co_lose - 1, -1, -1, -1 };
        ms_co_win = 0;
        func_80118A20(w, l, 0x80400u, 0);
        return 1;
    }
    return func_80117438(how, a1);
}
static u8 how_of(u32 w) {
    if (w & 0x1000u) return 8;                          // no winner: a draw (time limit, double count-out)
    if (w & 0x4000u) return 1;                          // pinfall
    if (w & 0x2000u) return 2;                          // submission
    if (w & 0x20000u) return 3;                         // DQ
    if (w & 0x80000u) return 4;                         // count-out
    if (w & 0x10000u) return 5;                         // TKO
    if (w & 0x40000u) return 6;                         // ladder: took the belt
    if (w & 0x2000000u) return 7;                       // cage: escaped
    if (w & 0x1000000u) return 9;                       // ironman: a decision
    return 0;
}
static int slot_of(u32 role) {         // the match slot role's man is in (-1: not in it)
    u16 id = role_id(role);
    if (!id) return -1;
    for (int s = 0; s < nm_slots() && s < 8; s++) {
        s16 v = RDS16(TB(s) + 0xA0u);
        if (v >= 0 && same_man((u16)v & 0x0FFFu, id)) return s;
    }
    return -1;
}
static void story_match_frame(void) {
    if (!pk) { ms_live = 0; return; }
    if (RDS32(NM_MODE) != -2 || RD16(NM_MATCH_FRAMES) == 0xFFFFu) { ms_live = 0; return; }
    if (!ms_live) {                    // (a match starting: nothing fired yet, the last result gone)
        ms_live = 1; dbg_story_ms = 0; st_how = 0; ms_co_win = ms_co_lose = 0;
        for (int s = 0; s < 8; s++) ms_move[s] = 0;
    }
    u32 w = RD32(NM_RESULT_W);
    if (RD16(NM_RESULT_UP_W) && (w & 0x400u)) { st_how = how_of(w); return; }
    int n = nm_slots(), fin[8];
    for (int s = 0; s < 8; s++) {
        fin[s] = 0;
        if (s >= n) continue;
        u16 m = RD16(TB(s) + 0x30Au);
        fin[s] = m != ms_move[s] && (RD16(TB(s) + 0x8Cu) & 1u);
        ms_move[s] = m;
    }
    u32 node = RD16(0x80097ED4u), kk = 0;
    for (u32 k = 0; k < pk_xn[X_MSCRIPT]; k++) {
        u32 r = pk_x[X_MSCRIPT] + 12u * k;
        if (RD16(r) != node) continue;
        u32 bit = kk < 16 ? 1u << kk : 0; kk++;
        if (!bit || (dbg_story_ms & bit)) continue;
        u32 ck = RD8(r + 2), cr = RD8(r + 3), cv = RD16(r + 4), ak = RD8(r + 6), a1 = RD8(r + 7), a2 = RD8(r + 8), a3 = RD8(r + 9);
        if (ak == MA_RUNIN) RD32(NM_RUNIN_W + 4u * (a1 & 1u)) = 0x1FFu;   // (held till it fires)
        int s = ck == MS_TIME ? 0 : slot_of(cr), hit = 0;
        if (s < 0) continue;
        switch (ck) {
        case MS_TIME: hit = RDS32(NM_CLOCK_M) * 60 + RDS32(NM_CLOCK_S) >= (s32)cv; break;
        case MS_HEALTH: hit = RDS16(TB(s) + 0xAAu) * 100 < (s32)cv * 255; break;
        case MS_OUTSIDE: hit = (RD16(TB(s) + 0x82u) & 4u) != 0; break;
        case MS_HOLD: hit = RDS32(TB(s) + 0x14u) == 73; break;
        case MS_FINISHER: hit = fin[s]; break;
        }
        if (!hit) continue;
        dbg_story_ms |= bit;
        if (ak == MA_END) {
            int ws = slot_of(a1), ls;
            if (ws < 0) continue;
            if (a2 == 0xFFu) ls = (ws == 0 || ws == 2) ? 1 : 0;    // (one on one: the other man; type 14: the other side's)
            else ls = slot_of(a2);
            if (ls < 0 || ls == ws || a3 < 1 || a3 > 7) continue;
            // the game's own end: who beat him (the DQ's: +0x6C), then the way he lost (+0x94) -- its replay, its
            // title, the announcer, the results, as a match it ended itself
            if (a3 == 4) RDS32(TB(ls) + 0x6Cu) = ws; else RDS32(TB(ls) + 0x00u) = ws;
            RD16(TB(ls) + 0x94u) |= lost_by[a3];
            if (a3 == 5) { ms_co_win = ws + 1; ms_co_lose = ls + 1; }
        } else if (ak == MA_RUNIN) RD32(NM_RUNIN_W + 4u * (a1 & 1u)) = 0x100u;
        else if (ak == MA_NODQ) RD32(NM_RULES_W) &= ~0x2000u;
        else if (ak == MA_NOCOUNT) RD8(NM_COUNTOUT_W) = 0;
    }
}
