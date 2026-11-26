// ri_capture.cpp (phase3, 2026-11-19): models out of match RAM images (ri_capture.h; wmport.py capture / nm_descriptor /
// rev_capture).
#include "ri_capture.h"

namespace ri {

// where No Mercy's layout keeps what WM2000's skeleton reads after the parts (mod src skel.h: ours)
static const uint32_t SKEL_WM_ATTACH = 0x25A, SKEL_WM_PLACE = 0x260, SKEL_WM_HANDS = 0x262, SKEL_WM_FLAGS = 0x264, SKEL_MAGIC_AT = 0x268;
static const uint32_t SKEL_MAGIC_WM = 0x574D324B;
static const uint32_t WM_DESC = 0x8009DF90, WM_DESC_SIZE = 0x258, WM_PARTS = 0x8009B0D0;
static const uint32_t REV_PARTS = 0x80073EB0;   // 0x3F4 a slot, 0x2C a part: +0x18 picture, +0x1C palette

uint32_t tex_size(const uint8_t* hdr) {
    uint32_t w = hdr[0] + 1u, h = hdr[1] + 1u;
    return 8 + w * h * (hdr[2] == 1 ? 8 : 4) / 8;
}
uint32_t pal_size(const uint8_t* hdr) {
    return hdr[2] == 1 ? 512 : std::max<uint32_t>(0x20, 2u * be16(hdr + 2));
}

Bytes nm_descriptor(const Bytes& wm, const std::map<uint32_t, uint32_t>* files) {
    Bytes nm(0x270, 0);
    auto f = [&](uint32_t v) -> uint32_t {
        if (files && v) { auto it = files->find(v); if (it == files->end()) fail("nm_descriptor: file 0x%X not in the table", v); return it->second; }
        return v;
    };
    auto put = [&](uint32_t to, uint32_t from) {
        put_be16(&nm[to], f(be16(&wm[from]))); put_be16(&nm[to + 2], f(be16(&wm[from + 2]))); put_be16(&nm[to + 4], f(be16(&wm[from + 4])));
        nm[to + 6] = wm[from + 6]; nm[to + 7] = wm[from + 7];
    };
    for (uint32_t k = 1; k <= 20; k++) put(8 * k, 8 * k);                // parts 1-20
    for (uint32_t i = 0; i < 3; i++) put(0xC8 + 8 * i, 0xB0 + 8 * i);     // the attachments
    memcpy(&nm[SKEL_WM_ATTACH], &wm[0x24A], 6);
    memcpy(&nm[SKEL_WM_PLACE], &wm[0x250], 2);
    memcpy(&nm[SKEL_WM_FLAGS], &wm[0x244], 4);
    put_be32(&nm[SKEL_MAGIC_AT], SKEL_MAGIC_WM);
    return nm;
}

static Bytes rb(const Bytes& ram, uint32_t a, size_t n) {
    size_t at = a & 0x7FFFFF;
    if (at + n > ram.size()) n = at < ram.size() ? ram.size() - at : 0;
    return Bytes(ram.begin() + at, ram.begin() + at + n);
}

WmCapture wm_capture(const Bytes& ram, int slot) {
    WmCapture c;
    c.desc = rb(ram, WM_DESC + slot * WM_DESC_SIZE, WM_DESC_SIZE);
    std::vector<std::pair<int, int>> order;
    for (int k = 1; k <= 20; k++) order.push_back({ k, 8 * k });
    for (int i = 0; i < 3; i++) order.push_back({ 22 + i, 0xB0 + 8 * i });
    for (auto [k, at] : order) {
        uint32_t m = be16(&c.desc[at]), t = be16(&c.desc[at + 2]), p = be16(&c.desc[at + 4]);
        if (!m) continue;
        uint32_t ps = WM_PARTS + slot * 0x514 + k * 0x34;
        uint32_t ta = be32(&ram[(ps + 0x20) & 0x7FFFFF]), pa = be32(&ram[(ps + 0x24) & 0x7FFFFF]);
        WmPart part{ k, at, m, t, p, std::nullopt, std::nullopt };
        if (t && ta) {
            Bytes h = rb(ram, ta, 8);
            part.tex = rb(ram, ta, tex_size(h.data()));
            if (pa) part.pal = rb(ram, pa, pal_size(part.tex->data()));
        }
        c.parts.push_back(std::move(part));
    }
    return c;
}

std::map<int, RevPart> rev_capture(const Bytes& ram, const std::vector<RevCall>& calls, int slot) {
    // The match's load is the log's last pass (each starts with part 1): an earlier pass's part it doesn't load isn't his.
    std::map<int, uint32_t> mesh;
    for (auto& c : calls) {
        int64_t o = (int64_t)c.a0 - (int64_t)(REV_PARTS + slot * 0x3F4);
        if (o >= 0 && o < 0x3F4 && o % 0x2C == 0) {
            if (o / 0x2C == 1) mesh.clear();
            mesh[(int)(o / 0x2C)] = c.a1 & 0xFFFF;
        }
    }
    std::map<int, RevPart> out;
    for (int k = 1; k <= 20; k++) {
        auto it = mesh.find(k);
        if (it == mesh.end()) continue;
        uint32_t ps = REV_PARTS + slot * 0x3F4 + k * 0x2C;
        uint32_t ta = be32(&ram[(ps + 0x18) & 0x7FFFFF]), pa = be32(&ram[(ps + 0x1C) & 0x7FFFFF]);
        RevPart p{ it->second, std::nullopt, std::nullopt };
        if (ta) { Bytes h = rb(ram, ta, 8); p.tex = rb(ram, ta, tex_size(h.data())); }
        if (p.tex && pa) p.pal = rb(ram, pa, pal_size(p.tex->data()));
        out[k] = std::move(p);
    }
    return out;
}

}  // namespace ri
