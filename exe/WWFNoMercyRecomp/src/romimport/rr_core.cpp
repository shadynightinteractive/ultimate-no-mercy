// rr_core.cpp (romrender, 2026-11-18; phase3 2026-11-19: moved into the game's src/romimport as a library, namespace rr; the job
// runner below is built with -DRR_MAIN): the C++ twin of tools/romrender/rr.py -- the deterministic software renderer's core,
// whole-number arithmetic only, line for line the Python reference (rr_check.py proves the two byte-identical on every
// scene the chain renders). This is the code that moves into the game's first-launch builder (Phase 3).
//
// The harness (main) runs one job file and writes its result:
//   rr_core <job> <out>
// job: "RRJ1", then little-endian int64 words: op, then the op's input (rr_check.py writes them):
//   op 0 RENDER   W H S, camera (eye 3, right 3, up 3, fwd 3, focal16, cx16, cy16, near8), background RGBA 4,
//                 textures (n; each w h ws wt then w*h colours), triangles (n; each tex flags then 3 x 8 vertex words)
//                 -> W*H*4 bytes RGBA (render + downsample)
//   op 1 QUANT    n_colours, count, then count x (r5 g5 b5) -> palette count, palette (3 bytes each), indices (2 bytes each)
//   op 2 RESAMPLE sw sh dw dh, then sw*sh x RGBA -> dw*dh*4 bytes
//   op 3 TRIG     -> isin of every angle 0..65535 (int32 each), then isqrt of 0..65535 squared-ish probes (int64 each)
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <array>
#include <algorithm>

namespace rr {

typedef int64_t i64;

// ---- integer helpers ----------------------------------------------------------------------------------------------------
static i64 fdiv(i64 a, i64 b) {           // a / b rounded toward minus infinity (b > 0)
    i64 q = a / b;
    if ((a % b != 0) && (a < 0)) q -= 1;
    return q;
}
static i64 rdiv(i64 a, i64 b) { return fdiv(2 * a + b, 2 * b); }
i64 isqrt(i64 n) {
    if (n <= 0) return 0;
    i64 x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}
static const i64 PI_Q30 = 3373259426LL;
static i64 sin_q30(i64 x) {
    i64 x2 = (x * x) >> 30;
    i64 term = x, total = x;
    for (i64 k = 1; k < 14; k += 2) {
        term = (term * x2) >> 30;
        term = term / ((k + 1) * (k + 2));
        if ((k / 2) % 2 == 0) total -= term; else total += term;
    }
    return total;
}
i64 isin(i64 a) {
    a &= 0xFFFF;
    i64 quarter = a >> 14, r = a & 0x3FFF;
    if (quarter & 1) r = 0x4000 - r;
    i64 x = (r * PI_Q30) >> 15;
    i64 s = (sin_q30(x) + (1 << 13)) >> 14;
    return quarter >= 2 ? -s : s;
}
static i64 c8(i64 c5) { return (c5 << 3) | (c5 >> 2); }

// ---- textures -----------------------------------------------------------------------------------------------------------
enum { WRAP = 0, MIRROR = 1, CLAMP = 2 };
struct Texture { i64 w, h, ws, wt; std::vector<uint16_t> texels; };
static i64 wrapi(i64 i, i64 n, i64 mode) {
    if (mode == CLAMP) return i < 0 ? 0 : (i >= n ? n - 1 : i);
    i64 q = fdiv(i, n), r = i - q * n;
    if (mode == MIRROR && (q & 1)) r = n - 1 - r;
    return r;
}
static uint16_t texel(const Texture& t, i64 s, i64 u) {
    i64 x = wrapi(fdiv(s, 32), t.w, t.ws), y = wrapi(fdiv(u, 32), t.h, t.wt);
    return t.texels[(size_t)(y * t.w + x)];
}

// ---- camera, scene ------------------------------------------------------------------------------------------------------
struct Camera { i64 eye[3], right[3], up[3], fwd[3], focal16, cx16, cy16, near8; };
typedef std::array<i64, 8> Vtx;           // x y z s t shade r g b
struct Tri { i64 tex, flags; Vtx v[3]; };
static const i64 CULL_BACK = 1;

static Vtx to_cam(const Camera& c, const Vtx& v) {
    i64 d[3] = { v[0] - c.eye[0], v[1] - c.eye[1], v[2] - c.eye[2] };
    i64 x = d[0] * c.right[0] + d[1] * c.right[1] + d[2] * c.right[2];
    i64 y = d[0] * c.up[0] + d[1] * c.up[1] + d[2] * c.up[2];
    i64 z = d[0] * c.fwd[0] + d[1] * c.fwd[1] + d[2] * c.fwd[2];
    Vtx o = v; o[0] = fdiv(x, 256); o[1] = fdiv(y, 256); o[2] = fdiv(z, 256);
    return o;
}
static std::vector<Vtx> clip_near(const std::vector<Vtx>& poly, i64 near8) {
    std::vector<Vtx> out;
    size_t n = poly.size();
    for (size_t i = 0; i < n; i++) {
        const Vtx& a = poly[i]; const Vtx& b = poly[(i + 1) % n];
        bool ina = a[2] >= near8, inb = b[2] >= near8;
        if (ina) out.push_back(a);
        if (ina != inb) {
            i64 num = near8 - a[2], den = b[2] - a[2];
            if (den < 0) { num = -num; den = -den; }
            Vtx q;
            for (int k = 0; k < 8; k++) q[k] = a[k] + fdiv((b[k] - a[k]) * num, den);
            out.push_back(q);
        }
    }
    return out;
}
struct P8 { i64 v[8]; };                  // sx sy iz s t r g b
static i64 edge(const P8& a, const P8& b, i64 px, i64 py) {
    return (b.v[0] - a.v[0]) * (py - a.v[1]) - (b.v[1] - a.v[1]) * (px - a.v[0]);
}
static bool top_left(const P8& a, const P8& b) {
    i64 dy = b.v[1] - a.v[1], dx = b.v[0] - a.v[0];
    return (dy == 0 && dx < 0) || dy > 0;
}
struct Target { i64 WS, HS; std::vector<uint8_t> col; std::vector<uint8_t> cov; std::vector<i64> depth; };

static void raster(Target& T, const Texture& t, i64 flags, P8 p0, P8 p1, P8 p2) {
    i64 area = edge(p0, p1, p2.v[0], p2.v[1]);
    if (area == 0) return;
    if (area > 0) { if (flags & CULL_BACK) return; }
    else { std::swap(p1, p2); area = -area; }
    i64 x0 = std::max<i64>(0, fdiv(std::min({ p0.v[0], p1.v[0], p2.v[0] }), 16));
    i64 x1 = std::min<i64>(T.WS - 1, fdiv(std::max({ p0.v[0], p1.v[0], p2.v[0] }), 16));
    i64 y0 = std::max<i64>(0, fdiv(std::min({ p0.v[1], p1.v[1], p2.v[1] }), 16));
    i64 y1 = std::min<i64>(T.HS - 1, fdiv(std::max({ p0.v[1], p1.v[1], p2.v[1] }), 16));
    if (x0 > x1 || y0 > y1) return;
    i64 b0 = top_left(p1, p2) ? 0 : 1, b1 = top_left(p2, p0) ? 0 : 1, b2 = top_left(p0, p1) ? 0 : 1;
    for (i64 y = y0; y <= y1; y++) {
        i64 py = 16 * y + 8;
        for (i64 x = x0; x <= x1; x++) {
            i64 px = 16 * x + 8;
            i64 w0 = edge(p1, p2, px, py), w1 = edge(p2, p0, px, py), w2 = edge(p0, p1, px, py);
            if (w0 < b0 || w1 < b1 || w2 < b2) continue;
            i64 n0 = fdiv(w0 * 65536, area), n1 = fdiv(w1 * 65536, area), n2 = fdiv(w2 * 65536, area);
            i64 den = n0 * p0.v[2] + n1 * p1.v[2] + n2 * p2.v[2];
            if (den <= 0) continue;
            size_t i = (size_t)(y * T.WS + x);
            if (T.depth[i] >= den) continue;
            i64 s = fdiv(n0 * p0.v[2] * p0.v[3] + n1 * p1.v[2] * p1.v[3] + n2 * p2.v[2] * p2.v[3], den);
            i64 u = fdiv(n0 * p0.v[2] * p0.v[4] + n1 * p1.v[2] * p1.v[4] + n2 * p2.v[2] * p2.v[4], den);
            uint16_t c = texel(t, s, u);
            if (!(c & 1)) continue;
            i64 r = c8((c >> 11) & 31), g = c8((c >> 6) & 31), b = c8((c >> 1) & 31);
            i64 sr = fdiv(n0 * p0.v[5] + n1 * p1.v[5] + n2 * p2.v[5], 65536);
            i64 sg = fdiv(n0 * p0.v[6] + n1 * p1.v[6] + n2 * p2.v[6], 65536);
            i64 sb = fdiv(n0 * p0.v[7] + n1 * p1.v[7] + n2 * p2.v[7], 65536);
            T.col[3 * i] = (uint8_t)std::min<i64>(255, (r * sr + 128) >> 8);
            T.col[3 * i + 1] = (uint8_t)std::min<i64>(255, (g * sg + 128) >> 8);
            T.col[3 * i + 2] = (uint8_t)std::min<i64>(255, (b * sb + 128) >> 8);
            T.cov[i] = 1;
            T.depth[i] = den;
        }
    }
}

std::vector<uint8_t> render(const std::vector<Texture>& texs, const std::vector<Tri>& tris, const Camera& cam,
                                   const i64 bg[4], i64 W, i64 H, i64 S) {
    Target T; T.WS = W * S; T.HS = H * S;
    size_t N = (size_t)(T.WS * T.HS);
    T.col.resize(3 * N); T.cov.assign(N, bg[3] > 0 ? 1 : 0); T.depth.assign(N, 0);
    for (size_t i = 0; i < N; i++) { T.col[3 * i] = (uint8_t)bg[0]; T.col[3 * i + 1] = (uint8_t)bg[1]; T.col[3 * i + 2] = (uint8_t)bg[2]; }
    i64 f16 = cam.focal16 * S, cx = cam.cx16 * S, cy = cam.cy16 * S;
    for (const Tri& tr : tris) {
        std::vector<Vtx> poly = clip_near({ to_cam(cam, tr.v[0]), to_cam(cam, tr.v[1]), to_cam(cam, tr.v[2]) }, cam.near8);
        if (poly.size() < 3) continue;
        std::vector<P8> P;
        for (const Vtx& q : poly) {
            i64 z = q[2];
            P8 p; p.v[0] = cx + fdiv(f16 * q[0], z); p.v[1] = cy - fdiv(f16 * q[1], z); p.v[2] = fdiv((i64)1 << 28, z);
            for (int k = 3; k < 8; k++) p.v[k] = q[k];
            P.push_back(p);
        }
        for (size_t k = 1; k + 1 < P.size(); k++) raster(T, texs[(size_t)tr.tex], tr.flags, P[0], P[k], P[k + 1]);
    }
    // downsample
    std::vector<uint8_t> out((size_t)(W * H * 4), 0);
    for (i64 y = 0; y < H; y++)
        for (i64 x = 0; x < W; x++) {
            i64 n = 0, sr = 0, sg = 0, sb = 0;
            for (i64 j = 0; j < S; j++) {
                i64 row = (y * S + j) * T.WS + x * S;
                for (i64 i = 0; i < S; i++) {
                    size_t k = (size_t)(row + i);
                    if (T.cov[k]) { n++; sr += T.col[3 * k]; sg += T.col[3 * k + 1]; sb += T.col[3 * k + 2]; }
                }
            }
            size_t o = (size_t)(4 * (y * W + x));
            if (2 * n >= S * S && n) { out[o] = (uint8_t)rdiv(sr, n); out[o + 1] = (uint8_t)rdiv(sg, n); out[o + 2] = (uint8_t)rdiv(sb, n); out[o + 3] = 255; }
        }
    return out;
}

// ---- median cut -----------------------------------------------------------------------------------------------------------
typedef std::array<i64, 3> C3;
struct Entry { C3 c; i64 n; };
void quantise(const std::vector<C3>& colours, i64 n, std::vector<C3>& pal, std::vector<i64>& index) {
    std::map<C3, i64> hist;                     // (ordered by the colour: the sorted keys)
    for (const C3& c : colours) hist[c] += 1;
    std::vector<std::vector<Entry>> boxes;
    if (!hist.empty()) {
        std::vector<Entry> b0;
        for (auto& kv : hist) b0.push_back({ kv.first, kv.second });
        boxes.push_back(b0);
    }
    while ((i64)boxes.size() < n) {
        i64 best = -1, best_n = 0;
        for (size_t bi = 0; bi < boxes.size(); bi++) {
            if (boxes[bi].size() < 2) continue;
            i64 cnt = 0; for (const Entry& e : boxes[bi]) cnt += e.n;
            if (cnt > best_n) { best = (i64)bi; best_n = cnt; }
        }
        if (best < 0) break;
        std::vector<Entry> bx = boxes[(size_t)best];
        i64 rng[3];
        for (int ch = 0; ch < 3; ch++) {
            i64 lo = bx[0].c[ch], hi = bx[0].c[ch];
            for (const Entry& e : bx) { lo = std::min(lo, e.c[ch]); hi = std::max(hi, e.c[ch]); }
            rng[ch] = hi - lo;
        }
        int ch = 0;
        for (int k = 1; k < 3; k++) if (rng[k] > rng[ch]) ch = k;
        std::stable_sort(bx.begin(), bx.end(), [ch](const Entry& a, const Entry& b) {
            if (a.c[ch] != b.c[ch]) return a.c[ch] < b.c[ch];
            return a.c < b.c;
        });
        i64 half = (best_n + 1) / 2, acc = 0; size_t cut = 1;
        for (size_t i = 0; i < bx.size(); i++) { acc += bx[i].n; if (acc >= half) { cut = i + 1; break; } }
        if (cut >= bx.size()) cut = bx.size() - 1;
        std::vector<Entry> a(bx.begin(), bx.begin() + (long)cut), b(bx.begin() + (long)cut, bx.end());
        boxes[(size_t)best] = a;
        boxes.insert(boxes.begin() + best + 1, b);
    }
    pal.clear();
    for (const auto& bx : boxes) {
        i64 cnt = 0, s[3] = { 0, 0, 0 };
        for (const Entry& e : bx) { cnt += e.n; for (int ch = 0; ch < 3; ch++) s[ch] += e.c[ch] * e.n; }
        pal.push_back({ rdiv(s[0], cnt), rdiv(s[1], cnt), rdiv(s[2], cnt) });
    }
    std::map<C3, i64> nearest;
    for (auto& kv : hist) {
        i64 bi = 0, bd = -1;
        for (size_t i = 0; i < pal.size(); i++) {
            i64 d = 0; for (int ch = 0; ch < 3; ch++) d += (kv.first[ch] - pal[i][ch]) * (kv.first[ch] - pal[i][ch]);
            if (bd < 0 || d < bd) { bi = (i64)i; bd = d; }
        }
        nearest[kv.first] = bi;
    }
    index.clear();
    for (const C3& c : colours) index.push_back(nearest[c]);
}

// ---- resampling -----------------------------------------------------------------------------------------------------------
std::vector<uint8_t> resample(const std::vector<uint8_t>& px, i64 sw, i64 sh, i64 dw, i64 dh) {
    std::vector<uint8_t> out;
    for (i64 y = 0; y < dh; y++) {
        i64 ya = y * sh, yb = (y + 1) * sh;
        for (i64 x = 0; x < dw; x++) {
            i64 xa = x * sw, xb = (x + 1) * sw;
            i64 tw = 0, ta = 0, tr = 0, tg = 0, tb = 0;
            for (i64 j = ya / dh; j < std::min(sh, (yb + dh - 1) / dh); j++) {
                i64 oy = std::min(yb, (j + 1) * dh) - std::max(ya, j * dh);
                if (oy <= 0) continue;
                for (i64 i = xa / dw; i < std::min(sw, (xb + dw - 1) / dw); i++) {
                    i64 ox = std::min(xb, (i + 1) * dw) - std::max(xa, i * dw);
                    if (ox <= 0) continue;
                    const uint8_t* p = &px[(size_t)(4 * (j * sw + i))];
                    i64 w = ox * oy;
                    tw += w; ta += w * p[3]; tr += w * p[3] * p[0]; tg += w * p[3] * p[1]; tb += w * p[3] * p[2];
                }
            }
            if (ta == 0) { out.insert(out.end(), { 0, 0, 0, 0 }); continue; }
            out.push_back((uint8_t)rdiv(tr, ta)); out.push_back((uint8_t)rdiv(tg, ta)); out.push_back((uint8_t)rdiv(tb, ta));
            out.push_back((uint8_t)rdiv(ta, tw));
        }
    }
    return out;
}

// ---- the harness ----------------------------------------------------------------------------------------------------------
}  // namespace rr

#ifdef RR_MAIN
using namespace rr;
struct Reader {
    std::vector<uint8_t> d; size_t o = 4;
    i64 w() { i64 v; memcpy(&v, &d[o], 8); o += 8; return v; }
};
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "rr_core <job> <out>\n"); return 2; }
    FILE* f = fopen(argv[1], "rb"); if (!f) return 2;
    Reader R; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); R.d.resize((size_t)n);
    if (fread(R.d.data(), 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);
    if (n < 12 || memcmp(R.d.data(), "RRJ1", 4)) return 2;
    std::vector<uint8_t> out;
    i64 op = R.w();
    if (op == 0) {
        i64 W = R.w(), H = R.w(), S = R.w();
        Camera c;
        for (int k = 0; k < 3; k++) c.eye[k] = R.w();
        for (int k = 0; k < 3; k++) c.right[k] = R.w();
        for (int k = 0; k < 3; k++) c.up[k] = R.w();
        for (int k = 0; k < 3; k++) c.fwd[k] = R.w();
        c.focal16 = R.w(); c.cx16 = R.w(); c.cy16 = R.w(); c.near8 = R.w();
        i64 bg[4]; for (int k = 0; k < 4; k++) bg[k] = R.w();
        std::vector<Texture> texs((size_t)R.w());
        for (Texture& t : texs) {
            t.w = R.w(); t.h = R.w(); t.ws = R.w(); t.wt = R.w();
            t.texels.resize((size_t)(t.w * t.h));
            for (auto& x : t.texels) x = (uint16_t)R.w();
        }
        std::vector<Tri> tris((size_t)R.w());
        for (Tri& t : tris) {
            t.tex = R.w(); t.flags = R.w();
            for (int v = 0; v < 3; v++) for (int k = 0; k < 8; k++) t.v[v][k] = R.w();
        }
        out = render(texs, tris, c, bg, W, H, S);
    } else if (op == 1) {
        i64 nc = R.w(), cnt = R.w();
        std::vector<C3> cs((size_t)cnt);
        for (C3& c : cs) { c[0] = R.w(); c[1] = R.w(); c[2] = R.w(); }
        std::vector<C3> pal; std::vector<i64> idx;
        quantise(cs, nc, pal, idx);
        out.push_back((uint8_t)pal.size()); out.push_back((uint8_t)(pal.size() >> 8));
        for (const C3& p : pal) { out.push_back((uint8_t)p[0]); out.push_back((uint8_t)p[1]); out.push_back((uint8_t)p[2]); }
        for (i64 k : idx) { out.push_back((uint8_t)k); out.push_back((uint8_t)(k >> 8)); }
    } else if (op == 2) {
        i64 sw = R.w(), sh = R.w(), dw = R.w(), dh = R.w();
        std::vector<uint8_t> px((size_t)(4 * sw * sh));
        for (auto& b : px) b = (uint8_t)R.w();
        out = resample(px, sw, sh, dw, dh);
    } else if (op == 3) {
        for (i64 a = 0; a < 65536; a++) { int32_t v = (int32_t)isin(a); out.insert(out.end(), (uint8_t*)&v, (uint8_t*)&v + 4); }
        for (i64 k = 0; k < 65536; k++) {
            i64 v = isqrt(k * k * 977 + k * 31 + 7);
            out.insert(out.end(), (uint8_t*)&v, (uint8_t*)&v + 8);
        }
    } else return 2;
    FILE* g = fopen(argv[2], "wb"); if (!g) return 2;
    fwrite(out.data(), 1, out.size(), g); fclose(g);
    return 0;
}
#endif  // RR_MAIN
