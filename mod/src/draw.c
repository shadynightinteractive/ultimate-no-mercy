// Arcade overlay: builds a small F3DEX2 display list (text in an 8x8 font, filled rectangles and
// the roster portraits) and appends it to the game's frame display list, right before the
// frame's gfx task starts.
//
// Display lists, font and portraits live in game RDRAM above the 4 MB the game uses
// (0x80780000-0x807F0FFF), so the RSP/RDP (or RT64) can read them like the game's own data.
#include "arcade.h"
#include "arcade_font.h"   // Press Start 2P (the operator menu, as the other arcade ports have it)

#define DL_BASE    0x807E0000u
#define DL_BYTES   0x8000u            // per buffer, two buffers
// The operator menu's frames (a whole picture of text: bigger lists): two buffers in the expansion
// RAM's top half after media.c's wave tables (0x807C0000.., 9 KB), up to the overlay's own.
#define MENU_DL_BASE  0x807C4000u
#define MENU_DL_BYTES 0xD000u
#define FONT_ADDR  0x807F0000u        // 128 x 48 I4 atlas (16 x 6 glyphs), 3072 bytes
#define FONT_W 128
#define FONT_H 48
#define PHYS(a) ((a) & 0x1FFFFFFFu)

static int assets_ready;
static int buf_index;
static u32* dl;        // write pointer
static u32* dl_start;
static u32* dl_limit;
static int fb_w = 480, fb_h = 240; // No Mercy renders 480 x 240; read from the frame's SETCIMG
static u32 cur_color;  // last prim colour set
static int cc_mode;    // 1 = combiner TEXEL0*PRIM, 2 = PRIM only
static int tex_loaded; // -1 none, -2 font, >= 0 portrait index

#define APPLY(m, ...) m(__VA_ARGS__)
#define GFX(a, b) do { if (dl < dl_limit) { dl[0] = (a); dl[1] = (b); dl += 2; } } while (0)

int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void str_num(char* out, int n) {
    if (n < 0) n = 0;
    if (n > 999) n = 999;
    int k = 0;
    if (n >= 100) out[k++] = (char)('0' + n / 100);
    if (n >= 10) out[k++] = (char)('0' + (n / 10) % 10);
    out[k++] = (char)('0' + n % 10);
    out[k] = 0;
}

static void build_assets(void) {
    // font: I4, two texels per byte, high nibble = left texel
    volatile u8* tex = (volatile u8*)FONT_ADDR;
    for (int i = 0; i < FONT_W * FONT_H / 2; i++) tex[i] = 0;
    for (int c = 0; c < 96; c++) {
        int gx = (c % 16) * 8, gy = (c / 16) * 8;
        for (int y = 0; y < 8; y++) {
            u8 bits = c == 95 ? 0xFF : gArcadeFont[c][y]; // glyph 127: solid block (rectangles)
            for (int x = 0; x < 8; x++) {
                if (bits & (0x80 >> x)) {
                    int px = gx + x, py = gy + y;
                    int off = py * (FONT_W / 2) + px / 2;
                    tex[off] |= (px & 1) ? 0x0F : 0xF0;
                }
            }
        }
    }
    assets_ready = 1;
}

// Combiner LERP encodings (see gbi.h gsDPSetCombineLERP).
#define CC(a0, b0, c0, d0, Aa0, Ab0, Ac0, Ad0, a1, b1, c1, d1, Aa1, Ab1, Ac1, Ad1) \
    (0xFC000000u | ((a0) & 0xF) << 20 | ((c0) & 0x1F) << 15 | ((Aa0) & 7) << 12 | ((Ac0) & 7) << 9 | \
     ((a1) & 0xF) << 5 | ((c1) & 0x1F)), \
    (((u32)(b0) & 0xF) << 28 | ((b1) & 0xF) << 24 | ((Aa1) & 7) << 21 | ((Ac1) & 7) << 18 | ((d0) & 7) << 15 | \
     ((Ab0) & 7) << 12 | ((Ad0) & 7) << 9 | ((d1) & 7) << 6 | ((Ab1) & 7) << 3 | ((Ad1) & 7))

static void set_tex_mode(void) {
    if (cc_mode == 1) return;
    GFX(0xE7000000u, 0);
    APPLY(GFX, CC(1, 8, 3, 7, 1, 7, 3, 7, 1, 8, 3, 7, 1, 7, 3, 7)); // TEXEL0 * PRIM
    cc_mode = 1;
}


// The game's own text look (its menus colour their lettering the same way): the letter between the
// glow's edge colour and the face colour by the texel, alpha by the texel. A dark edge colour keeps
// the words readable on a white logo page as well as on a dark scene.
#define GTEXT_EDGE 0x101018FFu
#define GTEXT_EDGE_RGB 0x10101800u
static void set_gtext_mode(void) {
    if (cc_mode == 3) return;
    GFX(0xE7000000u, 0);
    APPLY(GFX, CC(3, 5, 1, 5, 1, 7, 3, 7, 3, 5, 1, 5, 1, 7, 3, 7)); // (PRIM - ENV) * TEXEL0 + ENV
    GFX(0xFB000000u, GTEXT_EDGE);
    cc_mode = 3;
}

static void set_color(u32 rgba) {
    if (rgba == cur_color) return;
    GFX(0xFA000000u, rgba);
    cur_color = rgba;
}

static void load_font(void) {
    if (tex_loaded == -2) return;
    GFX(0xE7000000u, 0);
    GFX(0xFD000000u | (4u << 21) | (2u << 19), PHYS(FONT_ADDR));         // SETTIMG I 16b w=1
    GFX(0xF5000000u | (4u << 21) | (2u << 19), 0x07080200u);             // SETTILE load tile 7
    GFX(0xE6000000u, 0);                                                  // load sync
    GFX(0xF3000000u, (7u << 24) | ((u32)(FONT_W * FONT_H / 4 - 1) << 12) | 256u); // LOADBLOCK
    GFX(0xE7000000u, 0);
    GFX(0xF5000000u | (4u << 21) | (0u << 19) | (8u << 9), 0x00080200u); // SETTILE render I4 line 8
    GFX(0xF2000000u, ((u32)((FONT_W - 1) * 4) << 12) | (u32)((FONT_H - 1) * 4)); // SETTILESIZE
    tex_loaded = -2;
}

static int menu_frame;         // this frame is the operator menu's: a picture of its own (draw_menu_frame)
static int menu_buf;
void draw_menu_frame(void) { menu_frame = 1; }
// arcade_frame: a frame's drawing is about to be decided
void draw_begin(void) {
    if (!assets_ready) build_assets();
}
// a list of the arcade's drawing starts (the scene's end: two buffers, one for each frame in flight)
static void dl_begin(void) {
    if (!assets_ready) build_assets();
    buf_index ^= 1;
    dl_start = (u32*)(DL_BASE + buf_index * DL_BYTES);
    dl = dl_start;
    dl_limit = (u32*)((u8*)dl_start + DL_BYTES - 64); // room for the closing commands
    cur_color = 0;
    cc_mode = 0;
    tex_loaded = -1;
}

static void setup_state(u32 fb_addr) {
    GFX(0xE7000000u, 0);
    if (fb_addr) GFX(0xFF100000u | (u32)(fb_w - 1), PHYS(fb_addr));   // colour image: RGBA16
    GFX(0xED000000u, ((u32)(fb_w * 4) << 12) | (u32)(fb_h * 4));      // scissor: full frame
    // 1-cycle, no persp, point filter, TC_FILT, no dither; XLU surface blend, no Z
    GFX(0xEF000000u | 0x000CF0u, 0x00504A40u);
}

// Queue of draw requests for this frame (drawn on flush, once the frame size is known).
enum { IT_TEXT, IT_BOX, IT_RECT, IT_IMAGE, IT_GTEXT, IT_PTEXT, IT_FILL };
#define MAX_ITEMS 192
#define MAX_CHARS 64
typedef struct {
    u8 type, scale;
    s16 x, y, w, h;   // grid units for TEXT/BOX, pixels (480x240 frame) for RECT/IMAGE
    s16 img;
    u32 rgba;
    const char* str;  // IT_GTEXT: the string (a fixed address: the game caches by it)
    u32 style;
    char text[MAX_CHARS];
} Item;
static Item items[MAX_ITEMS];
static int n_items;

static Item* add(int type) {
    if (n_items >= MAX_ITEMS) return 0;
    Item* it = &items[n_items++];
    it->type = (u8)type; it->scale = 1; it->x = it->y = it->w = it->h = 0; it->img = 0; it->rgba = 0;
    it->text[0] = 0;
    return it;
}

void draw_text(int col, int row, u32 rgba, const char* s) {
    Item* it = add(IT_TEXT);
    if (!it) return;
    it->x = (s16)col; it->y = (s16)row; it->rgba = rgba;
    int i = 0;
    for (; s[i] && i < MAX_CHARS - 1; i++) it->text[i] = s[i];
    it->text[i] = 0;
}

void draw_text_center(int row, u32 rgba, const char* s) {
    draw_text((40 - str_len(s)) / 2, row, rgba, s);
}

// Text in the game's own lettering: the game's rasterizer (func_80009F6C) turns the string into an
// I4 texture in the chosen style; it's drawn centred on cx, top at y (pixels, 480 x 240), tinted.
int dbg_no_gtext;
void draw_gtext(const char* s, u32 style, int cx, int y, u32 rgba) {
    if (dbg_no_gtext) return;
    Item* it = add(IT_GTEXT);
    if (!it) return;
    it->str = s; it->style = style; it->x = (s16)cx; it->y = (s16)y; it->rgba = rgba;
}

// The operator menu's drawing (as the other arcade ports draw theirs): 640 x 480 units, whatever the
// frame's size; text in Press Start 2P, 8 x 8 units a letter at size 1, with a black drop shadow;
// x < 0 centres it.
int ptext_width(const char* s, int size) { return str_len(s) * 8 * size; }
void draw_ptext(const char* s, int x, int y, int size, u32 rgba) {
    Item* it = add(IT_PTEXT);
    if (!it) return;
    if (x < 0) x = (640 - ptext_width(s, size)) / 2;
    it->x = (s16)x; it->y = (s16)y; it->scale = (u8)size; it->rgba = rgba;
    int i = 0;
    for (; s[i] && i < MAX_CHARS - 1; i++) it->text[i] = s[i];
    it->text[i] = 0;
}
void draw_fill(int x, int y, int w, int h, u32 rgba) {
    Item* it = add(IT_FILL);
    if (!it) return;
    it->x = (s16)x; it->y = (s16)y; it->w = (s16)w; it->h = (s16)h; it->rgba = rgba;
}

void draw_box(int col, int row, int w, int h, u32 rgba) {
    Item* it = add(IT_BOX);
    if (!it) return;
    it->x = (s16)col; it->y = (s16)row; it->w = (s16)w; it->h = (s16)h; it->rgba = rgba;
}

void draw_rect(int x, int y, int w, int h, u32 rgba) {
    Item* it = add(IT_RECT);
    if (!it) return;
    it->x = (s16)x; it->y = (s16)y; it->w = (s16)w; it->h = (s16)h; it->rgba = rgba;
}

void draw_frame(int x, int y, int w, int h, int t, u32 rgba) {
    draw_rect(x - t, y - t, w + 2 * t, t, rgba);
    draw_rect(x - t, y + h, w + 2 * t, t, rgba);
    draw_rect(x - t, y, t, h, rgba);
    draw_rect(x + w, y, t, h, rgba);
}

// quarter-pixel rectangle (texrect coordinates are 10.2): RT64 draws it at its own resolution. Cut at the frame's top
// and left edges, its texture moved on as much (dsdx, dtdy 5.10: a quarter pixel is dsdx / 4096 texels), and every
// coordinate kept to its 12 bits: the RDP's are unsigned, and a negative one put into the command as it was (a glow's
// line above the frame's top, -8 = 0xFFFFFFF8) took the command's first byte with it -- 0xFF, a colour image 4089
// pixels wide at an address made of the rest: RT64 drew into it and wrote it back to RAM (a Royal Rumble, 2026-10-27:
// 3.9 MB of the arcade's memory zeroed -- the announcers in the ring, the wrestlers out of place -- and a crash into the
// read-only packs in QA's guard, RT64's copyToRAM the writer).
static void emit_texrect_q(int xq, int yq, int wq, int hq, int s, int t, u32 dsdx, u32 dtdy) {
    if (xq < 0) { s += (int)(((u32)(-xq) * dsdx) >> 12); wq += xq; xq = 0; }
    if (yq < 0) { t += (int)(((u32)(-yq) * dtdy) >> 12); hq += yq; yq = 0; }
    if (wq <= 0 || hq <= 0) return;
    u32 xl = (u32)xq & 0xFFFu, yl = (u32)yq & 0xFFFu;
    u32 xh = (u32)(xq + wq) > 0xFFFu ? 0xFFFu : (u32)(xq + wq), yh = (u32)(yq + hq) > 0xFFFu ? 0xFFFu : (u32)(yq + hq);
    GFX(0xE4000000u | (xh << 12) | yh, (xl << 12) | yl);           // TEXRECT tile 0
    GFX(0xE1000000u, ((u32)(s << 5) << 16) | (u32)(t << 5) & 0xFFFFu); // RDPHALF_1: s, t (10.5)
    GFX(0xF1000000u, (dsdx << 16) | dtdy);                         // RDPHALF_2 (5.10)
}
static void emit_texrect(int x, int y, int w, int h, int s, int t, u32 dsdx, u32 dtdy) {
    emit_texrect_q(x * 4, y * 4, w * 4, h * 4, s, t, dsdx, dtdy);
}
// 640 x 480 units -> quarter pixels of the frame
static int qx(int x) { return x * fb_w * 4 / 640; }
static int qy(int y) { return y * fb_h * 4 / 480; }
static void emit_ptext(const Item* it) {
    int s = it->scale ? it->scale : 1, sh = s > 1 ? s / 2 + 1 : 1;
    int gw = qx(8 * s), gh = qy(8 * s);
    if (gw <= 0 || gh <= 0) return;
    u32 dsdx = (32u << 10) / (u32)gw, dtdy = (32u << 10) / (u32)gh;   // 8 texels over gw / 4 pixels
    load_font();
    set_tex_mode();
    for (int pass = 0; pass < 2; pass++) {
        int ox = pass ? 0 : sh, oy = pass ? 0 : sh;
        set_color(pass ? it->rgba : 0x000000FFu);
        for (int k = 0; it->text[k]; k++) {
            int c = (u8)it->text[k];
            if (c == ' ') continue;
            if (c < 32 || c > 126) c = '?';
            c -= 32;
            emit_texrect_q(qx(it->x + k * 8 * s + ox), qy(it->y + oy), gw, gh, (c % 16) * 8, (c / 16) * 8, dsdx, dtdy);
        }
    }
}
static void emit_fill(const Item* it) {
    load_font();
    set_tex_mode();
    set_color(it->rgba);
    emit_texrect_q(qx(it->x), qy(it->y), qx(it->x + it->w) - qx(it->x), qy(it->y + it->h) - qy(it->y),
                   (95 % 16) * 8 + 4, (95 / 16) * 8 + 4, 0, 0);
}

static void emit_char(int x, int y, int cw, int ch, int c) {
    if (c < 32 || c > 127) c = '?';
    c -= 32;
    emit_texrect(x, y, cw, ch, (c % 16) * 8, (c / 16) * 8, (8u << 10) / (u32)cw, (8u << 10) / (u32)ch);
}

// The game's text textures the arcade shows. The game's rasterizer allocates from the game's heap,
// so it's only called from the controller hook (draw_gtext_service: the game's main loop, at a
// point where it reads the pads); drawing here only reads the finished textures.
#define MAX_GTEXT 16
typedef struct { const char* s; u32 style; u32 rec; u8 wanted, idle; } GText;
static GText gt[MAX_GTEXT];
u32 dbg_gt_invalid, dbg_gt_drawn, dbg_flush_noend, dbg_flush_n;

static GText* gt_find(const char* s, u32 style, int make) {
    GText* free_slot = 0;
    for (int i = 0; i < MAX_GTEXT; i++) {
        if (gt[i].s == s && gt[i].style == style) return &gt[i];
        if (!gt[i].s && !free_slot) free_slot = &gt[i];
    }
    if (!make || !free_slot) return 0;
    free_slot->s = s; free_slot->style = style; free_slot->rec = 0; free_slot->idle = 0;
    return free_slot;
}

static int gt_valid(const GText* g) {
    u32 r = g->rec;
    return GAME_PTR(r) && RD32(r) == (u32)g->s && RD16(r + 0x12) == (u16)g->style;
}

void draw_gtext_service(void) {
    for (int i = 0; i < MAX_GTEXT; i++) {
        GText* g = &gt[i];
        if (!g->s) continue;
        if (g->wanted) {
            g->wanted = 0; g->idle = 0;
            if (!gt_valid(g)) g->rec = plat_game_text(g->s, g->style);   // (re)made by the game
        } else if (++g->idle > 30) {                                     // half a second unused
            if (gt_valid(g)) plat_game_text_free(g->rec);
            g->s = 0; g->rec = 0;
        }
    }
}

static void emit_gtext(const Item* it) {
    GText* g = gt_find(it->str, it->style, 1);
    if (!g) return;
    g->wanted = 1;
    if (!gt_valid(g)) { dbg_gt_invalid++; return; }   // not made yet (next frame)
    dbg_gt_drawn++;
    u32 r = g->rec;
    u32 texels = RD32(r + 4);
    int stride = RD16(r + 0xC), h = RD16(r + 0xE), w = RD16(r + 0x10);
    if (!GAME_PTR(texels) || stride < 16 || (stride & 15) || h <= 0 || w <= 0) return;
    int bpr = stride / 2;                       // bytes per row (I4)
    int rows = 4096 / bpr;                      // rows that fit TMEM at once
    if (rows > h) rows = h;
    u32 words = (u32)bpr / 8u;                  // 64-bit words per row
    u32 dxt = (2048u + words - 1u) / words;
    int x0 = it->x - w / 2;
    // a dark edge all round first (the letters drawn dark one pixel off each way), then the letters:
    // readable on a white logo page and on a dark scene alike
    static const s8 off[9][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1},{0,0} };
    u32 edge_a = (it->rgba & 0xFFu) * 0xE0u / 0xFFu;
    for (int pass = 0; pass < 9; pass++) {
        if (pass < 8) { set_tex_mode(); set_color(GTEXT_EDGE_RGB | edge_a); }
        else { set_gtext_mode(); set_color(it->rgba); }
        for (int r0 = 0; r0 < h; r0 += rows) {
            int n = h - r0 < rows ? h - r0 : rows;
            GFX(0xE7000000u, 0);
            GFX(0xFD000000u | (4u << 21) | (2u << 19), PHYS(texels + (u32)(r0 * bpr)));   // SETTIMG I 16b
            GFX(0xF5000000u | (4u << 21) | (2u << 19), 0x07000000u);                      // load tile 7
            GFX(0xE6000000u, 0);
            GFX(0xF3000000u, (7u << 24) | ((u32)(bpr * n / 2 - 1) << 12) | dxt);          // LOADBLOCK
            GFX(0xE7000000u, 0);
            GFX(0xF5000000u | (4u << 21) | (0u << 19) | (words << 9), 0x00000000u);       // render tile 0: I4
            GFX(0xF2000000u, ((u32)((stride - 1) * 4) << 12) | (u32)((n - 1) * 4));
            emit_texrect(x0 + off[pass][0], it->y + r0 + off[pass][1], w, n, 0, 0, 1u << 10, 1u << 10);
        }
    }
    tex_loaded = -1;
}

static void emit_list(const Item* list, int n) {
    int cw = fb_w / 40, ch = fb_h / 30;
    int sh = cw / 8 > 0 ? cw / 8 : 1; // shadow offset
    for (int i = 0; i < n; i++) {
        const Item* it = &list[i];
        if (it->type == IT_GTEXT) { emit_gtext(it); continue; }
        if (it->type == IT_PTEXT) { emit_ptext(it); continue; }
        if (it->type == IT_FILL) { emit_fill(it); continue; }
        if (it->type == IT_BOX || it->type == IT_RECT) {
            int x = it->x, y = it->y, w = it->w, h = it->h;
            if (it->type == IT_BOX) { x *= cw; y *= ch; w *= cw; h *= ch; }
            if (w <= 0 || h <= 0) continue;
            // a textured rectangle stretched from the font's solid block: RT64 can take a
            // FILLRECT that covers the frame for a framebuffer clear and draw it first
            load_font();
            set_tex_mode();
            set_color(it->rgba);
            emit_texrect(x, y, w, h, (95 % 16) * 8 + 4, (95 / 16) * 8 + 4, 0, 0);
        } else {
            int x = it->x * cw, y = it->y * ch;
            load_font();
            set_tex_mode();
            set_color(0x000000C0u); // drop shadow, then the text
            for (int k = 0; it->text[k]; k++)
                if (it->text[k] != ' ') emit_char(x + k * cw + sh, y + sh, cw, ch, it->text[k]);
            set_color(it->rgba);
            for (int k = 0; it->text[k]; k++)
                if (it->text[k] != ' ') emit_char(x + k * cw, y, cw, ch, it->text[k]);
        }
    }
}
static void emit_items(void) { emit_list(items, n_items); }

static int in_our_buffers(u32 a) {
    u32 p = PHYS(a);
    return p >= PHYS(DL_BASE) && p < PHYS(DL_BASE) + 2 * DL_BYTES;
}

// Walks a frame's display list the way the RSP would (calls, branches, segments), calling cb
// for every command. Returns the address of the top-level G_ENDDL (0 if not found).
typedef void (*dl_cb)(u32 at, u32 w0, u32 w1, void* ctx);
static u32 dl_walk_from(u32 p, dl_cb cb, void* ctx);
static u32 dl_walk(u32 task, dl_cb cb, void* ctx) {
    if (!GAME_PTR(task)) return 0;
    u32 ostask = task + 0x10;
    if (RD32(ostask) != 1) return 0; // not M_GFXTASK
    return dl_walk_from(0x80000000u | PHYS(RD32(ostask + 0x30)), cb, ctx);
}
static u32 dl_walk_from(u32 p, dl_cb cb, void* ctx) {
    u32 seg[16];
    for (int i = 0; i < 16; i++) seg[i] = 0;
    u32 stack[18];
    int sp = 0;
    for (int n = 0; n < 0x20000; n++) {
        if (!GAME_PTR(p)) return 0;
        u32 w0 = RD32(p), w1 = RD32(p + 4);
        u32 op = w0 >> 24;
        if (cb) cb(p, w0, w1, ctx);
#ifdef ARCADE_TEST
        { extern u32 dbg_walk_n, dbg_walk_last; dbg_walk_n = (u32)n; dbg_walk_last = p; }
#endif
        if (op == 0xDB && ((w0 >> 16) & 0xFF) == 6) { seg[((w0 & 0xFFFF) >> 2) & 15] = w1 & 0x1FFFFFFFu; p += 8; continue; }
        if (op == 0xDF) {
            if (sp == 0) return p;
            p = stack[--sp];
            continue;
        }
        if (op == 0xDE) {
            if (in_our_buffers(w1)) return sp == 0 ? p : 0; // our own patch from an earlier frame
            u32 t = w1;
            u32 sg = (t >> 24) & 0xF;
            if ((t >> 24) < 0x10u && sg) t = seg[sg] + (t & 0xFFFFFF);   // (A top byte of 0x10 or more is a whole address: no segment number is that big.)
            t = 0x80000000u | PHYS(t);
            if (((w0 >> 16) & 0xFF) == 0) { if (sp >= 18) return 0; stack[sp++] = p + 8; }
            p = t;
            continue;
        }
        if (op == 0xE4 || op == 0xE5) { p += 24; continue; } // texrect + its two RDPHALF words
        p += 8;
    }
    return 0;
}

typedef struct { void (*fn)(u32); u32 seg_unused; } TexScan;
static void scan_cb(u32 at, u32 w0, u32 w1, void* ctx) {
    (void)at;
    if ((w0 >> 24) != 0xFD) return;
    TexScan* ts = (TexScan*)ctx;
    if ((w1 >> 24) == 0 || (w1 >> 24) >= 0x10u) ts->fn(0x80000000u | PHYS(w1));   // (Not a segmented address: see the list walker.)
}
void draw_scan_textures(u32 task, void (*cb)(u32 addr)) {
    TexScan ts = { cb, 0 };
    dl_walk(task, scan_cb, &ts);
}

typedef struct { u32 fb; u32 last_cimg; int w; } FlushScan;
static void flush_cb(u32 at, u32 w0, u32 w1, void* ctx) {
    (void)at;
    if ((w0 >> 24) != 0xFF) return;
    FlushScan* fs = (FlushScan*)ctx;
    fs->last_cimg = PHYS(w1);
    if (!fs->w && PHYS(w1) == PHYS(fs->fb)) {
        int w = (int)(w0 & 0xFFF) + 1;
        if (w >= 256 && w <= 1024) fs->w = w;
    }
}

// The operator menu's frame: the task draws the menu alone. Its display list is replaced by ours
// (the OSTask's data_ptr / data_size): every picture the game makes while the menu is up is the
// menu -- whichever the screen shows, however many pictures the game makes a frame (the title makes
// more than one: text drawn after the game's own there doesn't hold still), and nothing of the game
// shows through.
static void flush_menu(u32 task) {
    u32 ostask = task + 0x10;
    if (!GAME_PTR(task) || RD32(ostask) != 1) { n_items = 0; return; }
    FlushScan fs = { RD32(task + 0xC), 0, 0 };
    dl_walk(task, flush_cb, &fs);
    if (fs.w) { fb_w = fs.w; fb_h = (fs.w >= 640) ? 480 : 240; }
    menu_buf ^= 1;
    dl_start = (u32*)(MENU_DL_BASE + (u32)menu_buf * MENU_DL_BYTES);
    dl = dl_start;
    dl_limit = (u32*)((u8*)dl_start + MENU_DL_BYTES - 64);
    cur_color = 0; cc_mode = 0; tex_loaded = -1;
    GFX(0xE7000000u, 0);
    setup_state(fs.fb);
    emit_items();
    GFX(0xE7000000u, 0);
    GFX(0xE9000000u, 0);                     // full sync: the frame's end, as the game's has it
    dl[0] = 0xDF000000u; dl[1] = 0; dl += 2;
    RD32(ostask + 0x30) = (u32)dl_start;
    RD32(ostask + 0x34) = (u32)((u8*)dl - (u8*)dl_start);
    { extern void plat_view_replace(u32 head, u32 end); plat_view_replace((u32)dl_start, (u32)dl); }     // (the native picture: this is the frame now)
    n_items = 0;
}

// The arcade's words are part of the game's own picture. The game builds a frame in func_80002FA4:
// func_80002BB4 starts its display list (colour image, clear), func_800028B4 draws the scene, and
// func_80002DE4 ends the list (G_RDPFULLSYNC, G_ENDDL) and sends the task. When func_800028B4 is
// done, the list of the arcade's drawing is called from the game's list right there (draw_scene_end),
// before the frame's full sync: drawn with the frame, into the frame's framebuffer, like the game's
// HUD. (Commands after the full sync aren't the frame's any more: RT64 finishes and presents the
// frame at the sync, so drawing put after it shows up with the next picture instead -- a flicker.)
//
// arcade_frame (at the task's submit) decides what's drawn; draw_flush hands that over (a copy:
// the next frame's logic can run before the next scene is drawn) and the scene's end draws it.
static Item shown[MAX_ITEMS];
static int n_shown;
static u32 frame_fb;
u32 dbg_scene_n, dbg_scene_emit, dbg_scene_full;

// task: the game's frame task wrapper (func_800012B0's argument); OSTask at +0x10.
void draw_flush(u32 task) {
    if (menu_frame) { menu_frame = 0; flush_menu(task); n_shown = 0; return; }
    for (int i = 0; i < n_items; i++) shown[i] = items[i];
    n_shown = n_items;
    n_items = 0;
    dbg_flush_n++;
}

// func_80002BB4(picture, clear): a frame's display list starts; the picture's first word is its
// framebuffer (the address func_80002DE4 gives the task).
// The frame's display list: the game builds it in the frame block (0x8005D8E0[cur]: 12000 bytes, then the
// scheduler's task for it), with no check of the room -- a busy frame (the arena and a ported Superstar's
// bigger model, with a title intro skipped mid-way) runs past the end into the task, the task's words are
// written over the list's last commands, and the RSP (RT64 on the PC) is given garbage: the game stalls
// (white screen). The six-man engine builds a five- or six-man match's frames in its own lists (rw_dltab,
// 0x8000 bytes each, always there) -- the frames are built there always: the count the graphics thread
// reads (rw_cur_count) made 5+ while it builds a frame (func_80002BB4 starts the list, func_80002DE4 ends
// it: both read it), and put back after.
#ifdef NM_SIX
static s32 dl_count_saved = -1;
u32 dbg_dl_big_off;   // tests: 1 = the stock lists
void dl_big_begin(void) {
    s32 n = RDS32(SIX_CUR_COUNT);
    if (n >= 5 || dbg_dl_big_off) { dl_count_saved = -1; return; }
    dl_count_saved = n;
    RD32(SIX_CUR_COUNT) = 6;
}
void dl_big_end(void) {     // (func_80002DE4 starts: the same list's end read the same way)
    if (dl_count_saved >= 0) RD32(SIX_CUR_COUNT) = 6;
}
void dl_big_done(void) {    // (func_80002DE4 returned)
    if (dl_count_saved >= 0 && RDS32(SIX_CUR_COUNT) == 6) RD32(SIX_CUR_COUNT) = (u32)dl_count_saved;
    dl_count_saved = -1;
}
#else
void dl_big_begin(void) {}
void dl_big_end(void) {}
void dl_big_done(void) {}
#endif

void draw_frame_begin(u32 picture) {
    if (GAME_PTR(picture)) frame_fb = RD32(picture);
}

#define NM_FRAME_DL      0x8005D8E0u   // u32[2]: the two frames' display lists (task wrapper at +0x2EE0)
#define NM_FRAME_DL_IDX  0x80043610u   // s16: the one being built
#define NM_FRAME_DL_ROOM 0x2EE0u

// func_800028B4(&gfx, ...) has drawn the frame's scene: gfx is where the game's list goes on.
void draw_scene_end(u32 gfxp) {
    dbg_scene_n++;
    if (n_shown == 0 || !frame_fb || !GAME_PTR(gfxp)) return;
    u32 g = RD32(gfxp);
    s16 idx = RDS16(NM_FRAME_DL_IDX);
    if (idx < 0 || idx > 1) return;
    u32 head = RD32(NM_FRAME_DL + 4u * (u32)idx), room = NM_FRAME_DL_ROOM;
#ifdef NM_SIX
    // a six-man match builds its frames in the engine's own bigger lists (rw_dltab, SIX_DLBUF_SIZE each)
    if (RDS32(SIX_CUR_COUNT) >= 5) { head = RD32(SIX_DLTAB + 4u * (u32)idx); room = SIX_DLBUF_SIZE; }
#endif
    if (g < head || g + 8u + 16u > head + room || (g & 7)) { dbg_scene_full++; return; }
    // the frame's size from its colour image (the list so far, ended here for the walk)
    RD32(g) = 0xDF000000u; RD32(g + 4) = 0;
    FlushScan fs = { frame_fb, 0, 0 };
    dl_walk_from(0x80000000u | PHYS(head), flush_cb, &fs);
    if (fs.w) { fb_w = fs.w; fb_h = (fs.w >= 640) ? 480 : 240; }
    dl_begin();
    // the frame's own colour image (the scene may have drawn elsewhere: a SETCIMG of the same image
    // is nothing new to RT64), then the words
    setup_state(frame_fb);
    emit_list(shown, n_shown);
    GFX(0xE7000000u, 0);
    dl[0] = 0xDF000000u; dl[1] = 0; dl += 2;
    RD32(g) = 0xDE000000u;                   // G_DL (call): the game's list goes on after ours
    RD32(g + 4) = PHYS((u32)dl_start);
    RD32(gfxp) = g + 8;
    dbg_scene_emit++;
}

#ifdef ARCADE_TEST
// Test rig only: a log of the frame's textured rectangles (texture image, tile, rectangle).
u32 dbg_texlog_on;
u32 dbg_texlog_started, dbg_texlog_n, dbg_texlog_end, dbg_walk_n, dbg_walk_last;
u32 dbg_texlog[512 * 8];
typedef struct { u32 timg_w0, timg, tile_w0, tile_w1, tsize_w1, tlut; } TexState;
static void texlog_cb(u32 at, u32 w0, u32 w1, void* ctx) {
    TexState* s = (TexState*)ctx;
    u32 op = w0 >> 24;
    if (op == 0xFD) { s->timg_w0 = w0; s->timg = w1; }
    if (op == 0xF5 && ((w1 >> 24) & 7) != 7) { s->tile_w0 = w0; s->tile_w1 = w1; }
    if (op == 0xF2 && ((w1 >> 24) & 7) != 7) {
        s->tsize_w1 = w1;
        int dup = 0;
        for (u32 i = 0; i < dbg_texlog_n; i++)
            if (dbg_texlog[i * 8 + 1] == s->timg && dbg_texlog[i * 8 + 3] == w1) { dup = 1; break; }
        if (!dup && dbg_texlog_n < 512) {
            u32* e = &dbg_texlog[dbg_texlog_n++ * 8];
            e[0] = s->timg_w0; e[1] = s->timg; e[2] = s->tile_w0; e[3] = w1; e[4] = 0xF2; e[5] = 0; e[6] = 0; e[7] = s->tlut;
        }
    }
    if (op == 0xF0) s->tlut = s->timg;
    if (op == 0xFF && dbg_texlog_n < 512) {
        u32* e = &dbg_texlog[dbg_texlog_n++ * 8];
        e[0] = w0; e[1] = w1; e[2] = 0xFF; e[3] = at; e[4] = 0xFF; e[5] = 0; e[6] = 0; e[7] = 0;
    }
    if ((op == 0xE4 || op == 0xE5) && dbg_texlog_n < 512) {
        u32* e = &dbg_texlog[dbg_texlog_n++ * 8];
        e[0] = s->timg_w0; e[1] = s->timg; e[2] = s->tile_w0; e[3] = s->tsize_w1;
        e[4] = w0; e[5] = w1; e[6] = RD32(at + 12); e[7] = s->tlut;
    }
}
void dbg_texlog_frame(u32 task) {
    if (!dbg_texlog_on) return;
    TexState s = { 0, 0, 0, 0, 0, 0 };
    if (dbg_texlog_on >= 1000 && !dbg_texlog_started) { dbg_texlog_n = 0; dbg_texlog_started = 1; }
    dbg_texlog_end = dl_walk(task, texlog_cb, &s);
    dbg_texlog_on--;
    if (dbg_texlog_on == 999) dbg_texlog_on = 0;
}
#endif
