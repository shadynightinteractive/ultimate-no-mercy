// Families: one box on the Superstar Select, an ordered list of VARIANTS (the user, 2026-10-31: "When there are cases like
// Undertaker 99, add him into the character slots of existing wrestlers ... retaining their new names, moves, music, etc.
// ... you cycle to him via the same button"; 2026-11-14, "consolidation": any number of variants, in any order,
// interleaved -- No Mercy Austin's variant 1, WrestleMania 2000 Austin's, No Mercy Austin's 2 ... -- kept in DATA).
//
// A family is a list of variants, each one wrestler's variant (one of his four looks): (his game number, his look 0-3).
// The list is data: families.json beside the wrestlers (exe arcade_wrestlers.cpp "families" reads it and hands it here by
// plat_family_*), or with no such file the families the game always had (the same exe's default data: Undertaker +
// Undertaker 1999, Big Show + The Giant, the Revenge ports' second looks). Until 2026-11-14 they were a table here, with
// at most 4 wrestlers a family; now there is no count at all: a family is walked in the exe's list as it is.
//
// A variant keeps his own ID: the select hands the game (his ID, his look) as from a box of his own, so his name, moves,
// music, Titantron, weight class, the saves and everything after the select are his. What changes is the select:
//  - one box for the family, where its FIRST wrestler's box would be (the "head"); the other wrestlers' own boxes are off
//    the pages (roster.c place_ext / json_layout / glayout_run: the page closes up);
//  - the Type arrows (C-left / C-right; ovl_c func_80111158 0x80111974, its step func_8010A960 -- gen/patches.py calls
//    sel_type_step in its place) step through the family's variants in its order, round, passing over a variant already
//    picked. The box's row in the menu (+0x62, its Superstar) is made the variant's wrestler: the select draws everything
//    from that row each frame -- the box's photo, the big portrait, the name bar, height / weight, the model and the theme
//    (rig-tried on vanilla: the row's ID poked, all of it followed);
//  - a row the cursor has left is the head's again (variant_select_frame); a row the cursor lands on shows the family's
//    first variant not picked yet, when the game's own pick of a look isn't one of the family's (variant_select_frame);
//  - Random (C-up, func_8010A9E8) can land on any of the family: when it draws the head, one of the family's wrestlers at
//    random (variant_random_pick, then variant_select_frame once the cursor is there).
// Which variants are in play is decided as the game goes, never stored: a wrestler off the roster (an option off, no folder),
// removed (his stock folder deleted) or HIDDEN (families.json "hidden") is left out -- the exe has already left out each
// hidden variant; big_show (0x0201) counts only while the Big Show option is on (with it off 0x0201 is Chris Benoit's box).
// A family keeps its PLACE on the select -- its first listed wrestler's box, the "slot" -- when that wrestler is removed or
// hidden: the next one in play heads it there (the user, 2026-11-14: a removed head's family members in folders of their
// own are NOT removed with him -- only folders nested inside his go, being inside it; hiding is the way to take one off
// without deleting anything). (That needs the pages from select_pages.json, as the game ships: with the game's own twelve
// pages the next one has a box of his own, as an unpaired variant had before.) A family left with one wrestler's variants
// only is no family for the pages: he has his own box, as before.
// A PARTIAL family (2026-11-14, the editor's round 4): a family listing only some of a wrestler's variants never makes the
// others vanish -- the exe puts the first wrestler's unlisted ones at the family's end and gives any other wrestler a family
// of his own of his unlisted ones (plat_family_rest), shown in a box of his own where his box is; a family with "unlisted":
// "off" leaves them out (the default data's Revenge second looks). Which list a box steps through is told by the box: the
// page's entry for it (box_owner) -- a family's head, or that wrestler's own box.
// The Royal Rumble's draws (logic.c rr_random_superstar / arcade_rr_offer) take the family from a head's box, and its
// "one of each" rule (rumble_one_each) is a family's too (variant_same_man); a story's "the same man" too (story.c).
#include "arcade.h"
#include "gamefn.h"
GAMEFN(s32, func_80036110, 0x80036110u, (void));   // the game's rand() (the select's Random draws with it)

int bigshow_is_on(void);
int roster_id_on(u32 id);            // (roster.c: on the roster now, no seat taken; not removed)
int roster_id_removed(u32 id);
int roster_pages_from_file(void);    // (roster.c: the select's pages are select_pages.json's)
extern u32 plat_family_slot(int f);  // (the exe: family f's first listed wrestler -- its place on the select)
extern u32 plat_hidden(u32 id);      // (the exe: wrestler id's hidden variants, bit k for variant k + 1; 0xF: he is hidden)
static void fl_s(const char* s); static void fl_who(u32 id); static void fl_say(void); static void fl_num(s32 v);
extern int plat_family_of(u32 id);   // (the exe: his family, -1 none; its size; its variant k: ID | look << 16)
extern int plat_family_size(int f);
extern u32 plat_family_variant(int f, int k);
extern void plat_family_text(const char* text);
extern int plat_family_rest(u32 id);   // (the exe: the family of his variants his family doesn't list -- his own box; -1 none)
u32 roster_page_box(u32 p, s32 i);     // (roster.c: box i of page p's entry -- its Superstar)

// ---- the families (2026-11-14) --------------------------------------------------------------------------------------
#define BIG_SHOW 0x0201
static int fam_of(u16 id) { return id && id < 0x1000 ? plat_family_of(id) : -1; }
static u16 var_id(u32 v) { return (u16)(v & 0xFFFFu); }
static int var_look(u32 v) { return (int)((v >> 16) & 3u); }
// (2026-11-14) A hidden wrestler (all his variants hidden): no box, never drawn at random; whatever names him keeps him.
int variant_wrestler_hidden(u16 id) { return id && id < 0x1000 && plat_hidden(id) == 0xFu; }
// Is anyone hidden at all (asked once: the exe's list is read at start-up and never changes)?
int variants_any_hidden(void) {
    static int any = -1;
    if (any < 0) { any = 0; for (u32 id = 1; id < 0x1000 && !any; id++) any = plat_hidden(id) != 0; }
    return any;
}
// A wrestler of a family is in play now: on the roster, not removed, not hidden; big_show only as Big Show.
static int member_on(u16 id) {
    if (id == BIG_SHOW && !bigshow_is_on()) return 0;
    return !variant_wrestler_hidden(id) && roster_id_on(id);
}
// The family's slot: its first wrestler as listed, whose box is its place (0: none -- big_show listed first with the option
// off: as before, the others then have boxes of their own).
static u16 fam_listed_head(int f) {
    const u16 h = (u16)plat_family_slot(f);
    return h == BIG_SHOW && !bigshow_is_on() ? 0 : h;
}
// The family's head now: the wrestler of its first variant in play (0: none in play).
static u16 fam_head(int f) {
    for (int k = 0, n = plat_family_size(f); k < n; k++) { const u16 id = var_id(plat_family_variant(f, k)); if (member_on(id)) return id; }
    return 0;
}
// How many wrestlers of family f are in play now (the head counted once), and the j-th of them (in the family's order,
// each at his first variant).
static int fam_wrestlers(int f, int j, u16* out) {
    int m = 0;
    for (int k = 0, n = plat_family_size(f); k < n; k++) {
        const u16 id = var_id(plat_family_variant(f, k));
        int seen = 0;
        for (int q = 0; q < k && !seen; q++) seen = var_id(plat_family_variant(f, q)) == id;
        if (seen || !member_on(id)) continue;
        if (m == j && out) *out = id;
        m++;
    }
    return m;
}

// The family's place is kept for it (its slot's box, headed by whoever is first in play): the slot's wrestler is in play,
// or out only as removed or hidden -- not off the roster (an option, Big Show) -- and the pages are the file's.
static int slot_kept(int f) {
    const u16 s = fam_listed_head(f);
    if (!s || !roster_pages_from_file()) return 0;
    return member_on(s) || roster_id_removed(s) || variant_wrestler_hidden(s);
}
// (2026-11-14) The box at id's place on the pages shows another (-> him; 0: id himself or nobody): id is a family's slot,
// removed or hidden, and another of his family is in play -- the next one heads the box where his was (said once).
static u8 heir_said[0x1000 / 8];
u16 variant_slot_heir(u16 id) {
    const int f = fam_of(id);
    if (f < 0 || fam_listed_head(f) != id || member_on(id) || !slot_kept(f)) return 0;
    const u16 e = fam_head(f);
    if (e && e != id && !(heir_said[id >> 3] & (1u << (id & 7)))) {
        heir_said[id >> 3] |= (u8)(1u << (id & 7));
        fl_s("pages: "); fl_who(id); fl_s(roster_id_removed(id) ? " is removed" : " is hidden");
        fl_s(": his family's box stays where his was, headed by "); fl_who(e); fl_say();
    }
    return e != id ? e : 0;
}
// (2026-11-13, removal) The listed head of variant id's family, in play or not (0: id isn't another's variant).
// (2026-11-14: no longer roster.c's removed test -- the user: a removed head's family stays, headed by the next one.)
u16 variant_listed_base(u16 id) {
    const int f = fam_of(id);
    if (f < 0) return 0;
    const u16 h = fam_listed_head(f);
    return h && h != id ? h : 0;
}
// (2026-11-13, removal) The log's line for each wrestler that goes with removed man base; how many (roster.c, once).
void removal_s(const char* s); void removal_who(u32 id); void removal_say(void);
// (2026-11-14, the user: a removed head's family stays in his box, headed by the next one in play; only folders nested in
// his folder go with him, being inside it.) -> 1 if base heads a family (and the line said), 0 if not.
int variants_removed_with(u16 base) {
    const int f = fam_of(base);
    if (f < 0 || fam_listed_head(f) != base) return 0;
    const u16 e = fam_head(f);
    removal_s("variants: "); removal_who(base);
    if (e) { removal_s(" heads a family: it stays, in his box, headed by "); removal_who(e); removal_s(" (the next of it in play)"); }
    else removal_s(" heads a family with nobody else in play: no box");
    removal_say();
    return 1;
}
// a variant of another's family, in play now -> that family's head (0: not one -- no family, or the head himself)
// (2026-11-14, from the native mod's triage: a u32 -- its callers compare the whole register, so a narrow u16 result's
//  extension was something only the compiler knew. It returns the same numbers, every one below 0x10000.)
u32 variant_base_of(u16 id) {
    const int f = fam_of(id);
    if (f < 0 || !member_on(id)) return 0;
    const u16 h = fam_head(f);
    return h && h != id ? h : 0;
}
// (2026-11-14) id's family as wrestlers: how many are in play (the head first; 0: no family -- id alone), and each.
int variant_family_n(u16 id) {
    const int f = fam_of(id);
    if (f < 0 || !member_on(id)) return 0;
    const int n = fam_wrestlers(f, -1, 0);
    return n > 1 ? n : 0;
}
u16 variant_family_at(u16 id, int j) {
    u16 out = id;
    const int f = fam_of(id);
    if (f >= 0) fam_wrestlers(f, j, &out);
    return out;
}
// a Superstar the select's pages leave out as his own box: another's variant (he's in the head's box), or the head himself
// when his family's place is a removed or hidden slot's (he heads the box there: variant_slot_heir)
// (2026-11-14, a PARTIAL family: a wrestler with variants his family doesn't list keeps a box of his own for those)
int variant_hidden(u16 id) {
    if (variant_base_of(id)) return plat_family_rest(id) < 0;
    const int f = fam_of(id);
    if (f < 0 || !member_on(id)) return 0;
    const u16 s = fam_listed_head(f);
    return s && s != id && !member_on(s) && slot_kept(f) && fam_head(f) == id;
}
// two of one family (the Royal Rumble's one of each, a story's same man)
int variant_same_man(u16 a, u16 b) {
    if (a == b) return 1;
    u16 ba = variant_base_of(a), bb = variant_base_of(b);
    return (ba ? ba : a) == (bb ? bb : b);
}
// The family's variants as the select steps through them: those whose wrestler is in play, in order. The k-th of them
// (-> the exe's index, -1 none) and how many.
static int live_n(int f) {
    int m = 0;
    for (int k = 0, n = plat_family_size(f); k < n; k++) m += member_on(var_id(plat_family_variant(f, k)));
    return m;
}
static u32 live_at(int f, int j) {
    for (int k = 0, n = plat_family_size(f); k < n; k++) {
        const u32 v = plat_family_variant(f, k);
        if (member_on(var_id(v)) && j-- == 0) return v;
    }
    return 0;
}
// The family the select steps through for the box showing id (-1: the game's own four looks, round). A family is the
// game's own steps when what's in play of it is id's four looks in order (a family of one wrestler, in his order: no
// change), so only a real family, or one reordered, takes the select's steps.
static int sel_family(u16 id) {
    const int f = fam_of(id);
    if (f < 0 || !member_on(id)) return -1;
    const int n = live_n(f);
    if (n != 4) return f;
    for (int j = 0; j < 4; j++) { const u32 v = live_at(f, j); if (var_id(v) != id || var_look(v) != j) return f; }
    return -1;
}
// how many of a wrestler's variants the family has in play (the Rumble's draw: as many chances; a wrestler in no family: 4)
static int member_n(u16 id) {
    const int f = fam_of(id);
    if (f < 0 || variant_family_n(id) == 0) return 4;
    int m = 0;
    for (int k = 0, n = plat_family_size(f); k < n; k++) m += var_id(plat_family_variant(f, k)) == id;
    return m;
}
// one of id's family, by a random number, as many chances as variants (id with no family)
u16 variant_pick_member(u16 id, u32 r) {
    const int n = variant_family_n(id);
    if (!n) return id;
    int total = 0;
    for (int i = 0; i < n; i++) total += member_n(variant_family_at(id, i));
    int q = (int)(r % (u32)total);
    for (int i = 0; i < n; i++) { const u16 m = variant_family_at(id, i); q -= member_n(m); if (q < 0) return m; }
    return variant_family_at(id, 0);
}

// ---- the log's lines (2026-11-14): "[families] ..." -- "@" + four hex digits is a game number (the exe names him) ----
static char fl_line[200];
static int fl_len;
static void fl_s(const char* s) { while (*s && fl_len < (int)sizeof fl_line - 1) fl_line[fl_len++] = *s++; fl_line[fl_len] = 0; }
static void fl_who(u32 id) {
    char h[6] = { '@', 0, 0, 0, 0, 0 };
    for (int i = 0; i < 4; i++) { const u32 d = (id >> (12 - 4 * i)) & 15u; h[1 + i] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    fl_s(h);
}
static void fl_num(s32 v) {
    char b[12];
    int n = 0;
    u32 u = v < 0 ? (u32)-v : (u32)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u && n < 11);
    if (v < 0) fl_s("-");
    while (n) { char c[2] = { b[--n], 0 }; fl_s(c); }
}
static void fl_say(void) { plat_family_text(fl_line); fl_len = 0; fl_line[0] = 0; }

// ---- the Superstar Select -----------------------------------------------------------------------------------
#define SEL_PICKS      0x800A95A0u   // 50 x 12: u16 Superstar, u8 look, u8 human (the picks so far)
#define SEL_PAGE       0x800A9586u   // s16: the page shown
#define SEL_COSTUME    0x800A9588u   // s16: the look under the cursor (Type - 1)
#define SEL_BOX        0x800A9584u   // s16: the box Random put the cursor on
#define SEL_RULES      0x8015B80Cu   // u32: bit 0x80 -- a Superstar once only (any look)
#define SEL_MODEL_WAIT 0x8015B1ACu   // s32: frames since the hovered Superstar / look changed (the model loads at 6)
#define SEL_SOUND      0x8015B9E0u   // s32: the menus' sound (0x401 the Type's click)
#define ROW_SIZE       0x88u
#define ROW_LOOK       0x60u         // u16: ... | the look (low 2 bits) -- what the select draws the box's look from
#define ROW_ID         0x62u
#define ROW_DEF        0x64u
#define DEF_KIND_BOX   0x41F

// func_8010A89C(id, look): taken by a pick already (the game's rule)
static int taken(u16 id, int c) {
    if ((u32)c >= 4) return 0;
    int once = (RD32(SEL_RULES) & 0x80u) != 0;
    for (u32 i = 0; i < 50; i++) {
        u32 p = SEL_PICKS + 12u * i;
        if (RD16(p) != id) continue;
        if (RD8(p + 2) == (u32)c || once) return 1;
    }
    return 0;
}
// func_8010A908(id): his first look not taken (-1: none) -- of his variants the family has, in its order
static int first_free(u16 id) {
    const int f = sel_family(id);
    if (f < 0) { for (int c = 0; c < 4; c++) if (!taken(id, c)) return c; return -1; }
    for (int j = 0, n = live_n(f); j < n; j++) { const u32 v = live_at(f, j); if (var_id(v) == id && !taken(id, var_look(v))) return var_look(v); }
    return -1;
}
// Where (id, look c) is among family f's variants in play (-1: not one of them).
static int live_index(int f, u16 id, int c) {
    for (int j = 0, n = live_n(f); j < n; j++) { const u32 v = live_at(f, j); if (var_id(v) == id && var_look(v) == c) return j; }
    return -1;
}
static void say_variant(int f, int j, int n, u32 v) {
    fl_s("select: family of "); fl_who(fam_head(f)); fl_s(", variant "); fl_num(j + 1); fl_s(" of "); fl_num(n);
    fl_s(": "); fl_who(var_id(v)); fl_s(" variant "); fl_num(var_look(v) + 1); fl_say();
}
// The Type arrows: in place of func_8010A960(id, look, step) -> the look (ovl_c func_80111158's call, gen/patches.py). In a
// family: its next variant in play not picked yet, round; the row made that variant's wrestler.
// The select's rows of the record last seen (variant_select_frame), and box i's own Superstar on the page shown (the page's
// entry: a family's head, or a wrestler's own box).
static u32 sel_items;
static u16 box_owner(u32 row) {
    if (!GAME_PTR(sel_items) || row < sel_items) return 0;
    const u32 i = (row - sel_items) / ROW_SIZE;
    return i < 9 ? RD16(roster_page_box((u32)RDS16(SEL_PAGE), (s32)i)) : 0;
}
static u16 cur_owner;
// (2026-11-14, QA's picks: logic.c auto_pick_c) The cursor's box's own Superstar; the box holding id's variant look (his own
// box for a variant a partial family doesn't list, else his family's head's, else his).
u16 variant_cursor_box(void) { return cur_owner; }
static int live_index(int f, u16 id, int c);
u16 variant_box_for(u16 id, int look) {
    const u16 b = variant_base_of(id);
    if (b && look >= 0) { const int r = plat_family_rest(id); if (r >= 0 && live_index(r, id, look & 3) >= 0) return id; }
    return b ? b : id;
}
// The list the Type arrows step through in this box: a wrestler's own box of the variants his family doesn't list (a
// partial family), else his family's (sel_family; -1: the game's own four).
static int box_family(u16 id, u32 row) {
    if (box_owner(row) == id && variant_base_of(id)) { const int r = plat_family_rest(id); if (r >= 0) return r; }
    return sel_family(id);
}
s32 sel_type_step(u32 id, s32 c, s32 dir, u32 row) {
    const int f = box_family((u16)id, row);
    if (f < 0) {   // (the game's own: the next look of his not taken, round 0..3)
        if (first_free((u16)id) < 0) return c;
        for (int s = c + dir;; s += dir) {
            if (s > 3) s = 0;
            if (s < 0) s = 3;
            if (s == c) return c;
            if (!taken((u16)id, s)) return s;
        }
    }
    const int total = live_n(f);
    int at = live_index(f, (u16)id, c & 3);
    if (at < 0) for (int j = 0; j < total && at < 0; j++) if (var_id(live_at(f, j)) == (u16)id) at = j;   // (his first)
    if (at < 0) at = 0;
    for (int s = 1; s < total; s++) {
        const int q = ((at + dir * s) % total + total) % total;
        const u32 v = live_at(f, q);
        const u16 m = var_id(v);
        const int mc = var_look(v);
        if (taken(m, mc)) continue;
        if (m != (u16)id) {
            RD16(row + ROW_ID) = m;
            RDS32(SEL_MODEL_WAIT) = 0;           // (as the game does for a new look: the model and theme again)
            RDS32(SEL_SOUND) = 0x401;
        }
        say_variant(f, q, total, v);
        return mc;
    }
    return c;
}

// Random drew id onto page / box (func_8010A9E8's end, gen/patches.py): one of his family's wrestlers at random (the
// cursor may be on the box already, showing another of them).
static u16 rnd_want;
static int rnd_page = -1, rnd_box, rnd_wait;
void variant_random_pick(u32 id, u32 page) {
    rnd_page = -1;
    if (variant_base_of((u16)id) && plat_family_rest((u16)id) >= 0) return;   // (his own box: a partial family's -- him, as drawn)
    const int n = variant_family_n((u16)id);
    if (!n) return;
    rnd_want = variant_family_at((u16)id, (int)((u32)func_80036110() % (u32)n)); rnd_page = (int)page; rnd_box = RDS16(SEL_BOX); rnd_wait = 90;
}
// Each frame on the Superstar Select (logic.c), its record: rows the cursor isn't on are their head's again; the row it
// is on shows one of its family's variants (the first not picked, if the game's own look isn't one); Random's pick put
// in once the cursor is on his box.
u32 dbg_variant_rows, dbg_variant_random, dbg_variant_landed;
// (2026-11-14) The variant's place in its family, "7 / 12", beside the select's Type -- there is no limit to show: the Type's
// own number is the game's (the look under the cursor, 1-4: the select draws it from the box's row, found by the QA probe
// below), so the family's count is drawn over the frame in the game's own lettering (draw.c draw_gtext), right of it.
// Option family_number (on unless 0): off, the select's screen is the game's exactly.
void draw_gtext(const char* s, u32 style, int cx, int y, u32 rgba);
// (draw.c keeps the game's texture of a text by its pointer: so each text has a buffer of its own, and a buffer gets a new
// text only when it hasn't been drawn for 2 s -- draw.c lets a texture go after half a second unused)
#define FN_SLOTS 8
static char fn_text[FN_SLOTS][16];
static u32 fn_used[FN_SLOTS], fn_tick;
static const char* fn_slot(const char* want) {
    int old = -1;
    for (int i = 0; i < FN_SLOTS; i++) {
        int same = 1;
        for (int k = 0; same && (want[k] || fn_text[i][k]); k++) same = want[k] == fn_text[i][k];
        if (same && fn_text[i][0]) { fn_used[i] = fn_tick; return fn_text[i]; }
        if ((!fn_text[i][0] || fn_tick - fn_used[i] > 120) && (old < 0 || fn_used[i] < fn_used[old])) old = i;
    }
    if (old < 0) return 0;
    int k = 0;
    for (; want[k] && k < 15; k++) fn_text[old][k] = want[k];
    fn_text[old][k] = 0;
    fn_used[old] = fn_tick;
    return fn_text[old];
}
static void family_number(u32 items, s32 cur) {
    char fam_number_text[16];
    fn_tick++;
    static int on = -1;
    if (on < 0) on = plat_config("family_number", 1) != 0;
    if (!on || cur < 0 || cur >= 9) return;
    const u32 row = items + ROW_SIZE * (u32)cur, def = RD32(row + ROW_DEF);
    const s32 c = RDS16(SEL_COSTUME);
    if (!GAME_PTR(def) || RDS16(def + 0x12) != DEF_KIND_BOX || c < 0) return;
    const u16 id = RD16(row + ROW_ID);
    const int f = box_family(id, row);
    if (f < 0) return;
    const int j = live_index(f, id, c & 3), n = live_n(f);
    if (j < 0) return;
    int k = 0;
    char b[12];
    int m = 0;
    for (int v = j + 1; v; v /= 10) b[m++] = (char)('0' + v % 10);
    while (m) fam_number_text[k++] = b[--m];
    fam_number_text[k++] = ' '; fam_number_text[k++] = '/'; fam_number_text[k++] = ' ';
    for (int v = n; v; v /= 10) b[m++] = (char)('0' + v % 10);
    while (m && k < (int)sizeof fam_number_text - 1) fam_number_text[k++] = b[--m];
    fam_number_text[k] = 0;
    const char* t = fn_slot(fam_number_text);
    if (t) draw_gtext(t, GT_PLAIN, 366, 199, 0xF0E8D0FFu);
}
// QA (qa_variant & 16, 2026-11-14): the select's rows into the log whenever the look under the cursor changes -- each row's
// +0x60 / +0x62 and its def's kind, to find which row shows the Type's number.
static void fl_hex(u32 v) { char h[5]; for (int i = 0; i < 4; i++) { const u32 d = (v >> (12 - 4 * i)) & 15u; h[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10); } h[4] = 0; fl_s(h); }
static void variants_qa_probe(u32 items, s32 cur) {
    static int on = -1;
    static s32 last = -2;
    if (on < 0) on = ((int)plat_config("qa_variant", 0) & 16) != 0;
    if (!on) return;
    const s32 now = RDS16(SEL_COSTUME) | (s32)RD16(items + ROW_SIZE * (u32)(cur & 15) + ROW_ID) << 8;
    if (now == last) return;
    last = now;
    for (int i = 0; i < 48; i++) {
        const u32 row = items + ROW_SIZE * (u32)i, def = RD32(row + ROW_DEF);
        if (!(i & 3)) { fl_s("probe look "); fl_num(RDS16(SEL_COSTUME)); fl_s(" rows "); fl_num(i); fl_s(":"); }
        fl_s(" "); fl_hex(RD16(row + ROW_LOOK)); fl_s("/"); fl_hex(RD16(row + ROW_ID)); fl_s("/");
        fl_hex(GAME_PTR(def) ? RD16(def + 0x12) : 0xFFFFu); fl_s("/"); fl_hex(RD16(row + 0x56));
        if ((i & 3) == 3) fl_say();
    }
}
void variant_select_frame(u32 rec) {
    u32 items = RD32(rec + 0x14);
    if (!GAME_PTR(items)) return;
    sel_items = items;
    s32 cur = RDS16(rec + 4);
    cur_owner = cur >= 0 && cur < 9 ? box_owner(items + ROW_SIZE * (u32)cur) : 0;
    for (s32 i = 0; i < 9; i++) {
        u32 row = items + ROW_SIZE * (u32)i, def = RD32(row + ROW_DEF);
        if (!GAME_PTR(def) || RDS16(def + 0x12) != DEF_KIND_BOX) continue;
        u16 b = variant_base_of(RD16(row + ROW_ID));
        if (b && box_owner(row) == RD16(row + ROW_ID) && plat_family_rest(b ? RD16(row + ROW_ID) : 0) >= 0) b = 0;   // (his own box)
        if (b && i != cur) { RD16(row + ROW_ID) = b; dbg_variant_rows++; }
    }
    // (2026-11-14) The cursor's box: the game picks the head's first look not taken (func_8010A908) as it lands; in a family
    // that lists the head's looks otherwise (another order, not all four, another wrestler first) that look may be none of
    // the family's -- then its first variant in play not picked yet. (For the families the game always had, the head's
    // four come first, in order: never needed.)
    if (cur >= 0 && cur < 9) {
        const u32 row = items + ROW_SIZE * (u32)cur, def = RD32(row + ROW_DEF);
        const s32 c = RDS16(SEL_COSTUME);
        if (GAME_PTR(def) && RDS16(def + 0x12) == DEF_KIND_BOX && c >= 0) {
            const u16 id = RD16(row + ROW_ID);
            const int f = box_family(id, row);
            if (f >= 0 && live_index(f, id, c & 3) < 0) {
                for (int j = 0, n = live_n(f); j < n; j++) {
                    const u32 v = live_at(f, j);
                    if (taken(var_id(v), var_look(v))) continue;
                    RD16(row + ROW_ID) = var_id(v);
                    RDS16(SEL_COSTUME) = (s16)var_look(v);
                    RD16(row + ROW_LOOK) = (u16)((RD16(row + ROW_LOOK) & ~3u) | (u32)var_look(v));
                    RDS32(SEL_MODEL_WAIT) = 0;
                    dbg_variant_landed++;
                    say_variant(f, j, n, v);
                    break;
                }
            }
        }
    }
    variants_qa_probe(items, cur);
    family_number(items, cur);
    if (rnd_page < 0) return;
    if (--rnd_wait < 0) { rnd_page = -1; return; }
    if (RDS16(SEL_PAGE) != rnd_page || cur != rnd_box) return;
    u32 row = items + ROW_SIZE * (u32)cur;
    u16 id = RD16(row + ROW_ID);
    if (id != rnd_want && variant_same_man(id, rnd_want)) {
        int c = first_free(rnd_want);
        if (c >= 0) {
            RD16(row + ROW_ID) = rnd_want;
            RDS16(SEL_COSTUME) = (s16)c;
            RDS32(SEL_MODEL_WAIT) = 0;
            dbg_variant_random++;
        }
    }
    rnd_page = -1;
}

// QA (logic.c qa_variant & 8): the select's Random draws this Superstar every time (roster.c roster_random_ord)
u16 variant_qa_random;

// (2026-11-14) A look the game draws at random for him (the Royal Rumble's entrants: logic.c rr_draw_field_from): itself,
// or, if that variant of his is hidden (families.json "hidden"), his first one that isn't. (Nothing hidden: always look.)
int variant_shown_look(u16 id, int look) {
    const u32 h = id && id < 0x1000 ? plat_hidden(id) : 0;
    if (!(h & (1u << (look & 3)))) return look;
    for (int c = 0; c < 4; c++) if (!(h & (1u << c))) return c;
    return look;
}
