#pragma once
// [xplat 2026-11-12] What the display-list reader (arcade_view.cpp) and a picture backend (arcade_view_d3d12.cpp, arcade_view_vulkan.cpp,
// arcade_view_none.cpp) share: the frame the reader builds, and the few file-level variables the two halves have always talked through
// (before the split they were `static` in one file; now they are defined in arcade_view.cpp and declared here). Nothing was renamed.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace arcade_view {

typedef uint32_t u32; typedef uint8_t u8;
struct V { float x, y, z, px, py, pz, s, t; u8 r, g, b, a; u32 xf; };   // (x y z: the point as the object has it this update; px py pz: last update's. xf: which matrix)
struct DrawCB { float uv[4]; float vp[4]; float prim[4]; float env[4]; int comb[16]; float zx[4]; };    // (vp: clip to the target, as its viewport has it. comb: the combiner's two cycles: colour a b c d, alpha a b c d. zx: the alpha a pixel's dropped under; [visual 2026-11-19] then the see-through pull: units, the projection's z term, its w scale)
struct Draw { int tex; u32 start, count; u8 zmode; u8 cull; bool point; u8 cms, cmt; u8 target; float sc[4]; DrawCB cb; u32 rm_l = 0, rm_h = 0, geom = 0; };     // (rm_l, rm_h, geom: [visual 2026-11-19] the other modes and the geometry mode it was drawn with)      // (target 0 the frame, 1 the Titantron's picture. sc: the scissor, 0..1 of the target. cull: 0 none, 1 back faces, 2 front faces)
struct NewTex { int id; u32 w, h; std::vector<u8> rgba; };
// A matrix taken apart so it can be blended: a turn, a stretch, a place. (A straight line between two matrices would shrink whatever's turning)
struct Dec { float q[4]; float s[6]; float t[3]; float sign; };
// (kind 0: direct, as it is. kind 1: a world matrix: a to b, then the target's camera. [visual 2026-11-19, for the picture trace]
//  key: what it belongs to (0: nothing known); why: how its world matrix came out: 0 taken apart, 1 not affine, 2 flat, 3 unchanged
//  on the screen, 4 no camera to take it from)
struct Xf { Dec a, b; float direct[16]; u8 target; u8 kind; uint64_t key = 0; u8 why = 0; };
struct Cam { bool valid = false, moving = false; Dec va{}, vb{}; float pa[16] = {}, pb[16] = {}, vp[16] = {}; };
struct Frame { std::vector<Xf> xf; Cam cam[2]; bool snap = false; std::vector<V> v; std::vector<u32> idx; std::vector<Draw> draws; std::vector<NewTex> fresh; std::vector<int> dead; bool in_match = false; bool uses_screen = false; std::vector<u32> video; int swaps = -1;
               uint64_t serial = 0, vi = 0, vi_prev = 0, vi_hand = 0, t_begin = 0, t_begin_prev = 0, t_hand = 0; double w_vi = 0, w_vi_prev = 0;
               uint32_t loads_keyed = 0, loads_matched = 0, verts_unmatched = 0, loads_plain = 0, verts_plain = 0; };     // ([visual 2026-11-19] the trace: points loaded with an owner, how many found last update's to blend from; and without one)   // (video: the Titantron's picture as it is in memory, when no camera pass draws it)
// ([visual 2026-11-19] serial: the updates handed over, counted. vi: the game's retraces when this update was begun, vi_prev the last
//  one's, vi_hand when it was handed over; t_begin, t_begin_prev, t_hand: the same moments on arcade::platform::ticks(). w_vi, w_vi_prev:
//  when the retraces vi and vi_prev came, on clock_ms's clock -- the game's own even beat, 1/60 s each, whatever its thread was doing)
double clock_ms(uint64_t ticks);            // (arcade::platform::ticks() in ms, from a fixed start)
// ---- [visual 2026-11-19] Smooth Motion's clock (WCW_NATIVE_PACE; the backends) --------------------------------------------------
// A picture shows the game as it was a fixed while ago (lag): the update in hand blended by where that moment falls between the
// retraces its last update and it were begun at, and the next update taken when that moment has passed the one in hand. Before,
// the blend ran from when the picture's thread happened to find each update -- once a picture, so up to a picture late, and later
// still behind a slow picture -- and each update's arrival restarted it: a man walking moved in uneven steps.
struct Pace {
    double lag = -1.0;          // (ms: how far behind the clock the pictures show the game; -1 none yet)
    double shown = -1e300;      // (the game's moment last shown: it never goes back)
    bool due(const Frame& cur, bool have, const Frame& next, double now) const;    // (the next update to be taken now)
    void took(const Frame& f, double refresh_ms, double now);                      // (it was: the lag kept to what its hand-over needed)
    float k(const Frame& f, double now);                                           // (the blend for a picture made at now)
};

constexpr int HD_MEN = 16;     // (the full-detail men: the wrestler folders' own (no2k15: none built in now). The descriptor keeps the number in 4 bits. Each has six pictures, so texture numbers below HD_MEN x 6 are theirs: man x 6 + which)
extern int mode_;                                                          // (WCW_NATIVE_VIEW: 3 = the game's own renderer)
extern std::atomic<uint32_t> screen_hz;                                    // (the screen's rate, kept by the backend's thread)
extern std::atomic<int> pic_window, pic_shape, pic_smooth, pic_limit, pic_crt, pic_hdr;     // (the operator menu's PICTURE page)
extern const float SHAPES[6];
// [qa12x 2026-11-17] The tests' "draw only what's captured" (WCW_QA_DRAW_EVERY=<game seconds>, qa.ps1 -DrawShots): one picture
// for each that many seconds of the game's own pictures and no others. 0: off, every frame drawn as always (players never set it).
// (a frame's "swaps": the game's pictures made when it was handed over, 30 a second of its clock: the shot's name)
double qa_draw_every();
inline int env_int(const char* k, int fallback) { const char* e = getenv(k); return e && e[0] ? atoi(e) : fallback; }
extern std::mutex mtx_;                                                    // (guards the hand-over:)
extern Frame ready_; extern bool have_ready_;                              // (the finished frame, waiting for the backend to take it)
inline void mul4(const float* a, const float* b, float* o) { for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) { float s = 0; for (int k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c]; o[r * 4 + c] = s; } }       // (matrix maths the backend uses to blend each picture: inline, as it was inlined in the one file)
void put_together(const Dec& a, const Dec& b, float k, float* o);
int world_depth_on();
extern std::atomic<uint64_t> depth_draws_[4];                              // (the [view] log line's counters)
extern std::atomic<uint64_t> see_pulled_, see_kept_;                     // [selbg 2026-11-20] (see-through draws pulled / not: the camera's projection or another)
extern std::atomic<uint64_t> cuts_, xf_world_, xf_direct_, xf_matched_, cam_lost_, read_ns_, read_n_;
extern std::atomic<float> worst_key_, worst_move_, worst_turn_;

// ---- [visual 2026-11-19] the picture trace: measuring what the pictures show, one after another --------------------------------
// WCW_VIEW_TRACE=<file>: a CSV row for each picture drawn -- when, which update and how far between it and the last, the Titantron's
// picture, the shadows' parts and how many of their samples passed the depth test -- and with WCW_VIEW_TRACE_XF=1 each man's and
// shadow's part's place on the screen. Unset (players): nothing is written and nothing is worked out for it.
FILE* trace_out();
double trace_ms(uint64_t ticks);            // (arcade::platform::ticks() since the trace began, in ms)
bool trace_xf();
int trace_class(uint64_t key);              // (1: a matrix with an owner the trace follows -- a fixed quarter of them, by the owner's address -- else 0)
bool trace_shadow(const Draw& d);           // (a shadow's draw: see-through, the game's dark shadow colour)
bool trace_shot_wanted();                   // (a picture where the Titantron came out black: kept, WCW_VIEW_TRACE_SHOTS of them)
// ---- [visual 2026-11-19] the playtest fixes, each with its old way kept to compare against (WCW_NATIVE_<name>=0) ----------------------
int tron_keep_on();     // the Titantron's live picture kept over an update that brought no camera pass (old: cleared to black)
int pace_on();          // Smooth Motion's blend timed by when the game made each update (old: by when the picture's thread found it)

} // namespace arcade_view
