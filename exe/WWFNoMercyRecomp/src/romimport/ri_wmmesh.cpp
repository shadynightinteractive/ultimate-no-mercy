// ri_wmmesh.cpp (phase3 p3wmport, 2026-11-19): arenas_ring.py's Mesh -- see ri_wmmesh.h.
#include "ri_wmmesh.h"
#include <algorithm>
#include <cmath>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace ri {

double py_round_half_even(double x) { return std::nearbyint(x); }     // (the default rounding mode: to nearest, ties to even)

double py_floordiv(double vx, double wx) {
    double mod = std::fmod(vx, wx), div = (vx - mod) / wx;
    if (mod != 0.0 && ((wx < 0) != (mod < 0))) { mod += wx; div -= 1.0; }
    double fl;
    if (div != 0.0) { fl = std::floor(div); if (div - fl > 0.5) fl += 1.0; }
    else fl = std::copysign(0.0, vx / wx);
    return fl;
}

static int s8(uint8_t b) { return b > 127 ? (int)b - 256 : (int)b; }

Mesh Mesh::from_file(const Bytes& d) {
    if (d.size() < 8) fail("a mesh file of %zu bytes", d.size());
    Mesh m;
    int sc = (d[0] & 0x7F) + 1, nv = d[1] & 0x7F, nt = d[2];
    int off[3] = { s8(d[4]) * 16, s8(d[5]) * 16, s8(d[6]) * 16 };
    int pu = (d[7] >> 4) * 16, pv = (d[7] & 15) * 16;
    if (d.size() < (size_t)(8 + 8 * nv + 3 * nt)) fail("a short mesh file");
    for (int i = 0; i < nv; i++) {
        const uint8_t* v = &d[8 + 8 * i];
        m.pts.push_back({ (double)((s8(v[0]) + off[0]) * sc), (double)((s8(v[1]) + off[1]) * sc), (double)((s8(v[2]) + off[2]) * sc) });
        m.uv.push_back({ (int64_t)v[3] + pu, (int64_t)v[4] + pv });
        m.rgb.push_back({ v[5], v[6], v[7] });
    }
    size_t o = 8 + 8 * (size_t)nv;
    for (int i = 0; i < nt; i++) m.tris.push_back({ d[o + 3 * i], d[o + 3 * i + 1], d[o + 3 * i + 2] });
    m.b1 = d[1] & 0x80; m.b3 = d[3];
    return m;
}

Mesh Mesh::rot(int r) const {
    Mesh m = *this;
    for (auto& p : m.pts)
        for (int k = 0; k < (r & 3); k++) { double x = p[0], z = p[2]; p[0] = z; p[2] = -x; }
    return m;
}

Mesh Mesh::lit(uint8_t bright) const {
    Mesh m = *this;
    for (auto& c : m.rgb) if (c[0] == 0x49 && c[1] == 0x49 && c[2] == 0x49) c = { bright, bright, bright };
    return m;
}

Mesh Mesh::operator+(const Mesh& o) const {
    if (b1 != o.b1 || b3 != o.b3) fail("pieces of one mesh: one flags");
    Mesh m = *this;
    int n = (int)pts.size();
    m.pts.insert(m.pts.end(), o.pts.begin(), o.pts.end());
    m.uv.insert(m.uv.end(), o.uv.begin(), o.uv.end());
    m.rgb.insert(m.rgb.end(), o.rgb.begin(), o.rgb.end());
    for (auto t : o.tris) m.tris.push_back({ t[0] + n, t[1] + n, t[2] + n });
    return m;
}

Mesh join(const std::vector<Mesh>& ms) {
    Mesh m = ms[0];
    for (size_t i = 1; i < ms.size(); i++) m = m + ms[i];
    return m;
}

std::optional<Bytes> Mesh::file_opt() const {
    if (pts.empty() || pts.size() > 127 || tris.size() > 127) return std::nullopt;
    int sc = 1; int off[3] = {}; std::vector<std::array<double, 3>> q(pts.size());
    bool ok = false;
    for (sc = 1; sc < 129; sc++) {
        for (int k = 0; k < 3; k++) {
            double lo = pts[0][k], hi = pts[0][k];
            for (auto& p : pts) { if (p[k] < lo) lo = p[k]; if (p[k] > hi) hi = p[k]; }
            off[k] = (int)py_round_half_even((lo + hi) / 2 / sc / 16);
        }
        bool fit = true;
        for (int k = 0; k < 3; k++) if (off[k] < -128 || off[k] > 127) fit = false;
        for (size_t i = 0; i < pts.size() && fit; i++)
            for (int k = 0; k < 3; k++) {
                double c = pts[i][k] / sc - off[k] * 16;
                q[i][k] = c;
                if (!(c >= -128 && c <= 127 && c == std::trunc(c))) { fit = false; break; }
            }
        if (fit) { ok = true; break; }
    }
    if (!ok) return std::nullopt;
    int pu = -1, pv = -1;
    for (int p = 0; p < 16; p++) {
        bool fu = true, fv = true;
        for (auto& t : uv) {
            if (!(0 <= t[0] - 16 * p && t[0] - 16 * p <= 255)) fu = false;
            if (!(0 <= t[1] - 16 * p && t[1] - 16 * p <= 255)) fv = false;
        }
        if (fu) pu = p;
        if (fv) pv = p;
    }
    if (pu < 0 || pv < 0) return std::nullopt;
    Bytes out = { (uint8_t)(sc - 1), (uint8_t)(b1 | (int)pts.size()), (uint8_t)tris.size(), (uint8_t)b3,
                  (uint8_t)(off[0] & 0xFF), (uint8_t)(off[1] & 0xFF), (uint8_t)(off[2] & 0xFF), (uint8_t)(pu << 4 | pv) };
    for (size_t i = 0; i < pts.size(); i++) {
        for (int k = 0; k < 3; k++) out.push_back((uint8_t)((int64_t)q[i][k] & 0xFF));
        out.push_back((uint8_t)(uv[i][0] - 16 * pu)); out.push_back((uint8_t)(uv[i][1] - 16 * pv));
        out.push_back(rgb[i][0]); out.push_back(rgb[i][1]); out.push_back(rgb[i][2]);
    }
    for (auto& t : tris) { out.push_back((uint8_t)t[0]); out.push_back((uint8_t)t[1]); out.push_back((uint8_t)t[2]); }
    if (out.size() & 1) out.push_back(0);
    return out;
}

Bytes Mesh::file() const {
    auto b = file_opt();
    if (!b) fail("a mesh that doesn't fit No Mercy's mesh file (%zu points, %zu triangles)", pts.size(), tris.size());
    return *b;
}

}  // namespace ri
