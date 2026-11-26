// ri_arenascan.cpp (phase3, 2026-11-19): the arena sets' tables (ri_arenascan.h): stage3d extract.py read_ram / build,
// wm3d.py read_ram, venues_json.py + wmtab.py tables -- what they write, key for key.
#include "ri_arenascan.h"
#include <cmath>
#include <map>
#include <tuple>

namespace ri {

const char* const REV_SCENE_NAMES[9] = { "nitro", "souledout", "superbrawl", "bash", "havoc", "starrcade", "scene6", "scene7", "scene8" };

std::string json_file_text(const ojson& v, int indent) {
    PyDump h; h.indent = indent;
    std::string s = py_dumps(v, h), o;
    o.reserve(s.size() + s.size() / 8);
    for (char c : s) { if (c == '\n') o += '\r'; o += c; }       // (text mode on Windows: \n written as \r\n)
    return o;
}

namespace {
struct Ram {
    const Bytes& b;
    uint32_t u8(uint32_t a) const { return b[a & 0x7FFFFF]; }
    uint32_t u16(uint32_t a) const { return be16(&b[a & 0x7FFFFF]); }
    int32_t s16(uint32_t a) const { return sx16(be16(&b[a & 0x7FFFFF])); }
    uint32_t u32(uint32_t a) const { return be32(&b[a & 0x7FFFFF]); }
    std::string raw(uint32_t a, size_t n) const {
        static const char* H = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; i++) { uint8_t c = b[(a & 0x7FFFFF) + i]; s += H[c >> 4]; s += H[c & 15]; }
        return s;
    }
};

// a container file: u32 count, u32 offsets (from the file's start), the entries back to back (the last to the file's end)
std::vector<Bytes> entries(const Bytes& d) {
    uint32_t n = be32(d.data());
    std::vector<uint32_t> offs;
    for (uint32_t i = 0; i < n; i++) offs.push_back(be32(&d[4 + 4 * i]));
    offs.push_back((uint32_t)d.size());
    std::vector<Bytes> out;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = std::min<uint32_t>(offs[i], (uint32_t)d.size()), e = std::min<uint32_t>(offs[i + 1], (uint32_t)d.size());
        out.emplace_back(d.begin() + a, d.begin() + std::max(a, e));      // (Python's slice: empty when e < a)
    }
    return out;
}

// a scene model's 20-byte descriptor + its vertex batches (the same in both games)
ojson model_desc(const Ram& R, uint32_t d) {
    ojson m = ojson::object();
    m["addr"] = strf("%08x", d);
    m["n_tris"] = R.u16(d); m["tri_entry"] = R.u16(d + 2); m["n_verts"] = R.u16(d + 4); m["vtx_entry"] = R.u16(d + 6);
    m["ci8"] = (R.u16(d + 8) & 0x8000) != 0; m["tex_entry"] = R.u8(d + 9); m["pal_entry"] = R.u16(d + 10);
    m["width"] = R.u16(d + 12); m["height"] = R.u16(d + 14);
    m["cms"] = R.u8(d + 16); m["masks"] = R.u8(d + 17); m["cmt"] = R.u8(d + 18); m["maskt"] = R.u8(d + 19);
    ojson batches = ojson::array();
    for (uint32_t j = d + 20; R.u8(j); j += 2) batches.push_back(ojson::array({ R.u8(j), R.u8(j + 1) }));
    m["batches"] = batches;
    return m;
}

ojson vtx_row(const Ram& R, uint32_t a) {          // ">hhhHhhBBBB"
    return ojson::array({ R.s16(a), R.s16(a + 2), R.s16(a + 4), R.u16(a + 6), R.s16(a + 8), R.s16(a + 10),
                          R.u8(a + 12), R.u8(a + 13), R.u8(a + 14), R.u8(a + 15) });
}
}  // namespace

// ---- Revenge (stage3d extract.py) -----------------------------------------------------------------------------------
ojson rev_scenes(const Bytes& ram) {
    Ram R{ ram };
    const uint32_t SCENE_TABLE = 0x800C8E84, OBJ_TYPES = 0x800C6508, ENV_COLOURS = 0x800C5E00, SPRITE_TLUT = 0x800C5D70;
    ojson out = ojson::object();
    out["source"] = "ent0_ram.bin";
    out["scenes"] = ojson::array(); out["obj_types"] = ojson::array(); out["env_colours"] = ojson::array();
    out["sprite_tlut"] = R.raw(SPRITE_TLUT, 32);
    for (int k = 0; k < 9; k++) {
        uint32_t b = SCENE_TABLE + 52 * k;
        ojson sc = ojson::object();
        sc["index"] = k; sc["name"] = REV_SCENE_NAMES[k]; sc["geo_file"] = R.u16(b); sc["tex_file"] = R.u16(b + 2);
        sc["n_models"] = R.u16(b + 4); sc["field6"] = R.u16(b + 6);
        sc["models"] = ojson::array(); sc["nodes"] = ojson::array(); sc["raw52"] = R.raw(b, 52);
        uint32_t ml = R.u32(b + 8), nl = R.u32(b + 12);
        for (uint32_t i = 0; i < R.u16(b + 4); i++) sc["models"].push_back(model_desc(R, R.u32(ml + 4 * i)));
        for (uint32_t a = nl;; a += 16) {
            uint32_t f = R.u16(a), c = R.u16(a + 2), fl = R.u16(a + 4), p = R.u16(a + 6);
            if (c == 0) break;
            ojson n = ojson::object();
            n["first"] = f; n["count"] = c; n["flags"] = fl; n["param"] = p;
            n["pos"] = ojson::array({ R.s16(a + 8), R.s16(a + 10), R.s16(a + 12) }); n["ry_deg"] = R.s16(a + 14);
            sc["nodes"].push_back(n);
        }
        ojson ep = ojson::array();
        for (int i = 0; i < 9; i++) ep.push_back(strf("%08x", R.u32(b + 16 + 4 * i)));
        sc["extra_ptrs"] = ep;
        ojson scr = ojson::array();
        for (int i = 0; i < 5; i++) { uint32_t p = R.u32(b + 28 + 4 * i); scr.push_back(p ? ojson(R.raw(p, 64)) : ojson(nullptr)); }
        sc["scripts"] = scr;
        out["scenes"].push_back(sc);
    }
    for (int i = 0; i < 12; i++) {
        uint32_t a = OBJ_TYPES + 16 * i, dl = R.u32(a), vt = R.u32(a + 4), nv = R.u16(a + 8), tex0 = R.u16(a + 10);
        ojson t = ojson::object();
        t["type"] = i; t["dl"] = strf("%08x", dl); t["vtx"] = strf("%08x", vt); t["n_verts"] = nv; t["first_frame"] = tex0;
        t["frames"] = R.u8(a + 12); t["log2w"] = R.u8(a + 13); t["log2h"] = R.u8(a + 14); t["b15"] = R.u8(a + 15);
        ojson verts = ojson::array();
        for (uint32_t v = 0; v < nv; v++) verts.push_back(vtx_row(R, (vt & 0x7FFFFF) + 16 * v));
        t["verts"] = verts;
        out["obj_types"].push_back(t);
    }
    for (int i = 0; i < 7; i++)
        out["env_colours"].push_back(ojson::array({ R.u8(ENV_COLOURS + 3 * i), R.u8(ENV_COLOURS + 3 * i + 1), R.u8(ENV_COLOURS + 3 * i + 2) }));
    out["glow_blob_tex"] = R.raw(0x800C5BB0, 128); out["glow_blob_tlut"] = R.raw(0x800C5C30, 32);
    ojson gq = ojson::array();
    for (int v = 0; v < 4; v++) gq.push_back(vtx_row(R, (0x800C5AD0 & 0x7FFFFF) + 16 * v));
    out["glow_quad"] = gq;
    return out;
}

// x, z turned by deg about y (Python: math.cos / math.sin of math.radians(deg))
static void rot_y(int x, int y, int z, int deg, ojson& v0, ojson& v1, ojson& v2, int px, int py, int pz) {
    if (!deg) { v0 = x + px; v1 = y + py; v2 = z + pz; return; }
    double r = deg * (3.141592653589793 / 180.0), c = std::cos(r), s = std::sin(r);
    v0 = (x * c + z * s) + px; v1 = y + py; v2 = (-x * s + z * c) + pz;   // (y stays a whole number)
}

// The triangles of a scene model: 4 bytes (a, b, c, rot), indices local to the current vertex batch.
static ojson scene_tris(const ojson& m, const Bytes& tb) {
    std::vector<std::pair<int, int>> starts;
    int acc = 0;
    for (auto& b : m["batches"]) { starts.push_back({ b[1].get<int>(), acc }); acc += b[0].get<int>(); }
    ojson T = ojson::array();
    int cur = 0;
    for (int ti = 0; ti < m["n_tris"].get<int>(); ti++) {
        for (auto& [at, off] : starts) if (at == ti) cur = off;
        int a = tb[4 * ti], b = tb[4 * ti + 1], c = tb[4 * ti + 2], rot = tb[4 * ti + 3];
        int t0 = a, t1 = b, t2 = c;
        if (rot == 1) { t0 = b; t1 = c; t2 = a; } else if (rot == 2) { t0 = c; t1 = a; t2 = b; }
        T.push_back(ojson::array({ cur + t0, cur + t1, cur + t2 }));
    }
    return T;
}

ojson rev_scene_meshes(int k, const ojson& S, const RomFiles& rev) {
    const ojson& sc = S["scenes"][k];
    auto geo = entries(rev(sc["geo_file"].get<uint32_t>()));
    std::map<std::tuple<int, int, bool>, std::string> texnames;
    for (auto& m : sc["models"]) {
        auto key = std::make_tuple(m["tex_entry"].get<int>(), m["pal_entry"].get<int>(), m["ci8"].get<bool>());
        if (!texnames.count(key)) texnames[key] = strf("t%02d_p%02d.png", std::get<0>(key), std::get<1>(key));
    }
    ojson meshes = ojson::array();
    int ni = 0;
    for (auto& nd : sc["nodes"]) {
        if (!(nd["flags"].get<int>() & 0x8000)) {
            int first = nd["first"].get<int>(), count = nd["count"].get<int>(), deg = nd["ry_deg"].get<int>();
            int px = nd["pos"][0].get<int>(), py = nd["pos"][1].get<int>(), pz = nd["pos"][2].get<int>();
            for (int mi = first; mi < first + count; mi++) {
                const ojson& m = sc["models"][mi];
                const Bytes& vb = geo[m["vtx_entry"].get<int>()];
                const Bytes& tb = geo[m["tri_entry"].get<int>()];
                int w = m["width"].get<int>(), h = m["height"].get<int>();
                ojson V = ojson::array(), UV = ojson::array(), RGBA = ojson::array();
                for (int v = 0; v < m["n_verts"].get<int>(); v++) {
                    const uint8_t* p = &vb[16 * v];
                    int x = sx16(be16(p)), y = sx16(be16(p + 2)), z = sx16(be16(p + 4)), s = sx16(be16(p + 8)), t = sx16(be16(p + 10));
                    ojson a, b, c; rot_y(x, y, z, deg, a, b, c, px, py, pz);
                    V.push_back(ojson::array({ a, b, c }));
                    UV.push_back(ojson::array({ s / 32.0 / w, t / 32.0 / h }));
                    RGBA.push_back(ojson::array({ p[12], p[13], p[14], p[15] }));
                }
                ojson me = ojson::object();
                me["name"] = strf("n%02d_m%02d", ni, mi); me["node"] = ni; me["model"] = mi;
                me["texture"] = texnames[std::make_tuple(m["tex_entry"].get<int>(), m["pal_entry"].get<int>(), m["ci8"].get<bool>())];
                me["tex_size"] = ojson::array({ w, h }); me["ci8"] = m["ci8"];
                ojson wrap = ojson::object();
                wrap["cms"] = m["cms"]; wrap["masks"] = m["masks"]; wrap["cmt"] = m["cmt"]; wrap["maskt"] = m["maskt"];
                me["wrap"] = wrap;
                me["verts"] = V; me["uv"] = UV; me["rgba"] = RGBA; me["tris"] = scene_tris(m, tb);
                meshes.push_back(me);
            }
        }
        ni++;
    }
    ojson out = ojson::object();
    out["scene"] = k; out["name"] = sc["name"];
    out["geo_file"] = strf("%04x", sc["geo_file"].get<int>()); out["tex_file"] = strf("%04x", sc["tex_file"].get<int>());
    out["meshes"] = meshes;
    return out;
}

// ---- WrestleMania 2000 (wm3d.py read_ram) ---------------------------------------------------------------------------
ojson wm3d_scenes(const Bytes& ram) {
    Ram R{ ram };
    const uint32_t SCENE_TABLE = 0x80105D70; const int N_SCENES = 15;
    ojson out = ojson::object();
    out["source"] = "close1_ram.bin"; out["scenes"] = ojson::array();
    for (int k = 0; k < N_SCENES; k++) {
        uint32_t b = SCENE_TABLE + 52 * k;
        ojson sc = ojson::object();
        sc["index"] = k; sc["geo_file"] = R.u16(b); sc["tex_file"] = R.u16(b + 2); sc["n_models"] = R.u16(b + 4); sc["field6"] = R.u16(b + 6);
        sc["models"] = ojson::array(); sc["nodes"] = ojson::array(); sc["raw52"] = R.raw(b, 52);
        uint32_t ml = R.u32(b + 8), nl = R.u32(b + 12);
        for (uint32_t i = 0; i < R.u16(b + 4); i++) sc["models"].push_back(model_desc(R, R.u32(ml + 4 * i)));
        uint32_t a = nl;
        for (int it = 0; it < 400; it++, a += 16) {       // ">BBHHhhhhH" (WM2000: u8 first, u8 count)
            uint32_t f = R.u8(a), c = R.u8(a + 1);
            if (c == 0) break;
            ojson n = ojson::object();
            n["first"] = f; n["count"] = c; n["flags"] = R.u16(a + 2); n["param"] = R.u16(a + 4);
            n["pos"] = ojson::array({ R.s16(a + 6), R.s16(a + 8), R.s16(a + 10) }); n["ry_deg"] = R.s16(a + 12);
            sc["nodes"].push_back(n);
        }
        out["scenes"].push_back(sc);
    }
    return out;
}

// ---- WrestleMania 2000's venue lists (wmtab.py tables, venues_json.py) ----------------------------------------------
ojson wm_venues(const Bytes& ram) {
    Ram R{ ram };
    auto lst = [&](uint32_t p, int n) { ojson a = ojson::array(); if (p) for (int i = 0; i < n; i++) a.push_back(R.u16(p + 2 * i)); return a; };
    auto two = [](const char* k1, ojson v1, const char* k2, ojson v2) { ojson o = ojson::object(); o[k1] = v1; o[k2] = v2; return o; };
    ojson out = ojson::object();
    for (uint32_t v = 0; v < 7; v++) {
        ojson T = ojson::object();
        const uint32_t cage = 0;
        int n = R.s16(0x80047A48 + 2 * cage);
        T["mat"] = two("mesh", lst(R.u32(0x80047A1C + 4 * cage), n), "pal", lst(R.u32(0x80047A28 + 4 * v), n));
        n = R.s16(0x80047A7C + 2 * cage);
        T["apron"] = two("mesh", lst(R.u32(0x80047A50 + 4 * cage), n), "pal", lst(R.u32(0x80047A5C + 4 * v), n));
        n = R.s16(0x80047AC4 + 2 * v);
        T["floor"] = two("mesh", lst(R.u32(0x80047A84 + 4 * v), n), "pal", lst(R.u32(0x80047AA4 + 4 * v), n));
        ojson bar = ojson::object();
        bar["meshA"] = lst(R.u32(0x8004797C + 4 * v), 3); bar["palA"] = lst(R.u32(0x800479DC + 4 * v), 3);
        bar["meshB"] = lst(R.u32(0x8004799C + 4 * v), 3); bar["palB"] = lst(R.u32(0x800479FC + 4 * v), 3);
        bar["pal_stage_side"] = lst(R.u32(0x800479BC + 4 * v), 3);
        T["barricade"] = bar;
        ojson cor = ojson::object();
        cor["mesh"] = lst(0x8004790C, 3); cor["tex"] = lst(0x80047914, 3); cor["pal"] = lst(0x8004791C, 3);
        T["corner"] = cor;
        ojson G = ojson::array();
        for (uint32_t g = 0; g < 4; g++) {
            uint32_t m, a, b; int nn;
            if (g == 2) { m = R.u32(0x80047B5C + 4 * v); a = R.u32(0x80047B7C + 4 * v); b = R.u32(0x80047B9C + 4 * v); nn = (int)R.u8(0x80047B54 + v); }
            else {
                m = R.u32(R.u32(0x80047AF4 + 4 * v) + 4 * g); a = R.u32(R.u32(0x80047B14 + 4 * v) + 4 * g);
                b = R.u32(R.u32(0x80047B34 + 4 * v) + 4 * g); nn = (int)R.u8(R.u32(0x80047AD4 + 4 * v) + g);
            }
            if (nn > 127) nn -= 256;
            ojson grp = ojson::object();
            grp["mesh"] = lst(m, nn); grp["palA"] = lst(a, nn); grp["palB"] = lst(b, nn);
            G.push_back(grp);
        }
        T["crowd"] = G;
        ojson up = ojson::object();
        up["mesh"] = lst(0x80047160, 3); up["palA"] = lst(0x80047168, 3); up["palB"] = lst(0x80047170, 3);   // (v != 7: always here)
        T["upper"] = up;
        ojson glow = ojson::object(), gm = ojson::array();
        for (int i = 0; i < 12; i++) gm.push_back(0x5A9 + i);
        glow["mesh"] = gm; glow["pal"] = lst(0x80047BBC, 12);
        T["glow"] = glow;
        out[std::to_string(v)] = T;
    }
    return out;
}

}  // namespace ri
