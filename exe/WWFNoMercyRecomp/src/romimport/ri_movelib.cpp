// ri_movelib.cpp (phase3, 2026-11-19): the move libraries (ri_movelib.h). movelib_revenge: movelib_rev.py line for line.
#include "ri_movelib.h"
#include "json/json.hpp"
#include <map>

namespace ri {

// (gen/revanims.py's constants)
static const uint32_t REV_RECS = 0x020F, REV_REC0 = 0xE39;
static const uint32_t NM_RECS = 0x2BF, NM_REC0 = 0x2DD9, NM_RECN = 0x169B;
static const int FRAME_AT[] = { 15, 16, 18, 20, 22, 24, 26, 28, 30, 32 };
static const int SOUND_AT[] = { 20, 22, 24, 26, 28, 30 };
// (ours: Nash's jackknife's thud, the user 2026-10-30) record file -> {frame byte: sound}
static const std::map<uint32_t, std::map<uint8_t, uint8_t>> REC_SOUND = { { 0x1545, { { 0x8D, 0x1D } } } };

static Bytes tail1(const Bytes& d) { return Bytes(d.begin() + 1, d.end()); }

// An animation's first byte differs between the games for the same motion (an index of another table): Revenge's value ->
// No Mercy's, the most usual one over the animations both games have (No Mercy 0x3322.. vs Revenge 0x110B..).
std::map<uint8_t, uint8_t> byte0_table(const RomFiles& NM, const RomFiles& RV) {
    std::map<Bytes, OrderedCounter<uint8_t>> nm;
    for (uint32_t f = 0x3322; f < 0x4400; f++) {
        const Bytes& d = NM(f);
        if (d.size() > 8) nm[tail1(d)].add(d[0]);
    }
    std::map<uint8_t, OrderedCounter<uint8_t>> m;
    for (uint32_t f = 0x110B; f < 0x1D00; f++) {
        const Bytes& d = RV(f);
        if (d.size() <= 8) continue;
        auto it = nm.find(tail1(d));
        if (it == nm.end()) continue;
        for (auto& [b, c] : it->second.items) m[d[0]].add(b, c);
    }
    std::map<uint8_t, uint8_t> out;
    for (auto& [a, c] : m) out[a] = c.most();
    return out;
}

// A Revenge move record (36 bytes, file 0x20F) as No Mercy's (file 0x2BF): byte by byte through the most usual mapping
// over the moves both games have, frame bytes kept, sounds mapped where both name the same frame.
Bytes RecConv::rec(uint32_t f) const {
    const uint8_t* rr = &RT[36 * (f - REV_REC0)];
    Bytes out(36);
    for (int k = 0; k < 36; k++) { auto it = conv[k].find(rr[k]); out[k] = it == conv[k].end() ? rr[k] : it->second; }
    for (int k : FRAME_AT) out[k] = rr[k];
    for (int a : SOUND_AT) { auto it = snd.find(rr[a + 1]); out[a + 1] = it == snd.end() ? rr[a + 1] : it->second; }
    auto rs = REC_SOUND.find(f);
    if (rs != REC_SOUND.end())
        for (auto& [fr, s] : rs->second)
            for (int a : SOUND_AT) if (rr[a] == fr) out[a + 1] = s;
    return out;
}
RecConv rec_conv(const RomFiles& NM, const RomFiles& RV) {
    RecConv R; R.RT = RV(REV_RECS);
    const Bytes& NT = NM(NM_RECS);
    std::map<Bytes, std::vector<uint32_t>> rv;
    for (uint32_t f = REV_REC0; f < REV_REC0 + R.RT.size() / 36; f++) {
        const Bytes& d = RV(f);
        if (d.size() > 8) rv[tail1(d)].push_back(f);
    }
    std::vector<std::map<uint8_t, OrderedCounter<uint8_t>>> m(36);
    std::map<uint8_t, OrderedCounter<uint8_t>> sm;
    for (uint32_t f = NM_REC0; f < NM_REC0 + NM_RECN; f++) {
        const Bytes& d = NM(f);
        if (d.size() <= 8) continue;
        auto it = rv.find(tail1(d));
        if (it == rv.end()) continue;
        const uint8_t* nr = &NT[36 * (f - NM_REC0)];
        for (uint32_t r : it->second) {
            const uint8_t* rr = &R.RT[36 * (r - REV_REC0)];
            for (int k = 0; k < 36; k++) m[k][rr[k]].add(nr[k]);
            for (int a : SOUND_AT) if (nr[a] && nr[a] == rr[a]) sm[rr[a + 1]].add(nr[a + 1]);
        }
    }
    R.conv.resize(36);
    for (int k = 0; k < 36; k++) for (auto& [v, c] : m[k]) R.conv[k][v] = c.most();
    for (auto& [v, c] : sm) R.snd[v] = c.most();
    return R;
}

// (2026-11-20, revmoves2) The cross-reference and the measured rules (movelib_rev.py xref / anim_conv, line for line).
// An animation's header word: the end pose's kind << 10 | a turn (byte 0 = kind << 2 | the turn's top two bits); its record
// (36 bytes): byte 0 = the pose kind the camera reads << 4 | the end level (1 / 3 / 7 the mat, 2 the floor), then windows,
// frames, sounds. KIND_SEEN: Revenge's kind 15 is in no file both games have (0x1BCC, No Mercy's dive 12430's third role, is
// its 0x40CD but for byte 0: kind 30).
static const std::map<int, int> KIND_SEEN = { { 15, 30 } };

// {Revenge animation file: its No Mercy twin} -- the file with the same bytes but the first; of several, the one whose record
// agrees with Revenge's own in the most bytes, the lowest number among equals. From the ROMs at every first start.
static std::map<uint32_t, uint32_t> xref(const RomFiles& NM, const RomFiles& RV) {
    std::map<Bytes, std::vector<uint32_t>> T;
    for (uint32_t g = NM_REC0; g < NM_REC0 + NM_RECN; g++) {
        const Bytes& d = NM(g);
        if (d.size() > 8) T[tail1(d)].push_back(g);
    }
    const Bytes& RT = RV(REV_RECS); const Bytes& NT = NM(NM_RECS);
    std::map<uint32_t, uint32_t> X;
    for (uint32_t f = REV_REC0; f < REV_REC0 + RT.size() / 36; f++) {
        const Bytes& d = RV(f);
        if (d.size() <= 8) continue;
        auto it = T.find(tail1(d));
        if (it == T.end()) continue;
        const uint8_t* rr = &RT[36 * (f - REV_REC0)];
        int best = -1; uint32_t bg = 0;
        for (uint32_t g : it->second) {
            const uint8_t* nr = &NT[36 * (g - NM_REC0)];
            int s = 0; for (int k = 0; k < 36; k++) s += nr[k] == rr[k];
            if (s > best) { best = s; bg = g; }
        }
        X[f] = bg;
    }
    return X;
}

// Revenge animation file f as No Mercy plays it: its twin's own bytes and record where it has one, else converted by the
// rules measured on the twins (one pair a Revenge file, in file order): the end pose of the class Revenge's kind's No Mercy
// kind is of, the kind of the No Mercy animation of that class whose last key pose is nearest the file's own, the turn bits
// kept; the record's end level kept, the rest of its byte 0 and bytes 1 .. 35 by the No Mercy value of Revenge's (where the
// twins have it), the frames kept, the sounds by their own mapping, REC_SOUND.
// What state_after_animation makes of end pose k: L lying (20-47), R rising (4), C the corner (7, 8, 10), S standing (others).
static char pose_class(int k) { return k >= 20 && k <= 47 ? 'L' : k == 4 ? 'R' : (k == 7 || k == 8 || k == 10) ? 'C' : 'S'; }
static const size_t KEY = 0x62;   // (an animation: its 4-byte header, then its key poses, 0x62 bytes each)
static const uint8_t* last_key(const Bytes& d) {
    size_t n = d.size() >= 4 ? (d.size() - 4) / KEY : 0;
    return n ? &d[4 + KEY * (n - 1)] : nullptr;
}
struct AnimConv {
    const RomFiles *NM, *RV;
    std::map<uint32_t, uint32_t> X;
    std::map<int, int> KIND, HI;
    std::vector<std::map<int, int>> conv;
    std::map<int, int> snd;
    struct Cand { uint32_t g; int kind; char cls; const uint8_t* key; };
    std::vector<Cand> cands;              // (No Mercy's animations: the last key pose's nearest -- movelib_rev.py nearest_kind)
    AnimConv(const RomFiles& nm, const RomFiles& rv) : NM(&nm), RV(&rv), X(xref(nm, rv)), conv(36) {
        for (uint32_t g = NM_REC0; g < NM_REC0 + NM_RECN; g++) {
            const Bytes& d = nm(g);
            if (const uint8_t* kk = last_key(d)) cands.push_back({ g, d[0] >> 2, pose_class(d[0] >> 2), kk });
        }
        const Bytes& RT = rv(REV_RECS); const Bytes& NT = nm(NM_RECS);
        std::map<int, OrderedCounter<int>> kind, hi, sm;
        std::vector<std::map<int, OrderedCounter<int>>> m(36);
        for (auto& [f, g] : X) {
            const uint8_t* rr = &RT[36 * (f - REV_REC0)]; const uint8_t* nr = &NT[36 * (g - NM_REC0)];
            kind[rv(f)[0] >> 2].add(nm(g)[0] >> 2);
            hi[rr[0] >> 4].add(nr[0] >> 4);
            for (int k = 1; k < 36; k++) m[k][rr[k]].add(nr[k]);
            for (int a : SOUND_AT) if (nr[a] && nr[a] == rr[a]) sm[rr[a + 1]].add(nr[a + 1]);
        }
        for (auto& [k, c] : kind) KIND[k] = c.most();
        for (auto& [v, c] : hi) HI[v] = c.most();
        for (int k = 1; k < 36; k++) for (auto& [v, c] : m[k]) conv[k][v] = c.most();
        for (auto& [v, c] : sm) snd[v] = c.most();
    }
    std::pair<Bytes, Bytes> operator()(uint32_t f) const {
        const Bytes& NT = (*NM)(NM_RECS);
        auto x = X.find(f);
        if (x != X.end()) {
            const uint8_t* nr = &NT[36 * (x->second - NM_REC0)];
            return { (*NM)(x->second), Bytes(nr, nr + 36) };
        }
        Bytes d = (*RV)(f);
        int k = d[0] >> 2;
        auto ki = KIND.find(k);
        auto ks = KIND_SEEN.find(k);
        if (ki == KIND.end() && ks == KIND_SEEN.end()) fail("movelib: Revenge file 0x%X: end-pose kind %d is in no file both games have", f, k);
        int km = ki != KIND.end() ? ki->second : ks->second, nk = -1;
        if (const uint8_t* kk = last_key(d)) {
            long best = -1;
            for (auto& c : cands) {
                if (c.cls != pose_class(km)) continue;
                long s = 0; for (size_t i = 0; i < KEY; i++) s += std::abs((int)c.key[i] - (int)kk[i]);
                if (best < 0 || s < best) { best = s; nk = c.kind; }
            }
        }
        d[0] = (uint8_t)((nk < 0 ? km : nk) << 2 | (d[0] & 3));
        const Bytes& RT = (*RV)(REV_RECS);
        const uint8_t* rr = &RT[36 * (f - REV_REC0)];
        Bytes out(36);
        auto h = HI.find(rr[0] >> 4);
        out[0] = (uint8_t)((h == HI.end() ? rr[0] >> 4 : h->second) << 4 | (rr[0] & 15));
        for (int k2 = 1; k2 < 36; k2++) { auto c = conv[k2].find(rr[k2]); out[k2] = (uint8_t)(c == conv[k2].end() ? rr[k2] : c->second); }
        for (int k2 : FRAME_AT) out[k2] = rr[k2];
        for (int a : SOUND_AT) { auto s = snd.find(rr[a + 1]); out[a + 1] = (uint8_t)(s == snd.end() ? rr[a + 1] : s->second); }
        auto rs = REC_SOUND.find(f);
        if (rs != REC_SOUND.end())
            for (auto& [fr, s] : rs->second)
                for (int a : SOUND_AT) if (rr[a] == fr) out[a + 1] = s;
        return { d, out };
    }
};

static uint32_t hexnum(const std::string& s) { return (uint32_t)std::stoul(s, nullptr, 16); }
static void le32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)(v >> (8 * i))); }
static void le16(Bytes& b, uint32_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); }

Bytes movelib_revenge(Roms& R, const fs::path& index_path) {
    const RomFiles* NMp = R.files("nomercy");
    const RomFiles* RVp = R.files("revenge");
    if (!NMp || !RVp) return {};                     // (no Revenge ROM: no library -- the game plays without it)
    const RomFiles &NM = *NMp, &RV = *RVp;
    auto B0 = byte0_table(NM, RV);
    RecConv REC = rec_conv(NM, RV);
    struct Anim { Bytes b, rec; bool operator<(const Anim& o) const { return b != o.b ? b < o.b : rec < o.rec; } };
    // (2026-11-20, revmoves) The first byte is the header's top byte: the end pose's kind (bits 2-7) and the speed's top two
    // bits (0-1). Over the animations both games have the speed bits are always kept and each kind maps to one of No
    // Mercy's: a value no shared animation has (a move's later roles: Ultimo Dragon's dive's 0x1BC7 0x3D, AKI Man's 0x1BE5
    // 0x3C / 0x1BE6 0x41) gets its kind's No Mercy kind (the most usual over the table) and keeps its speed bits; Revenge's kind
    // 15 is in no shared animation -- No Mercy's is 30 (KIND_SEEN: Revenge's 0x1BCA..0x1BCE is No Mercy's dive 12430 role
    // for role, four of the five byte for byte; its third, 0x1BCC kind 15, is No Mercy's 0x40CD kind 30). movelib_rev.py the same.
    // (2026-11-20, revmoves2) The layout -- which files share one copy, the pairs' and blocks' numbers, so the moves table and
    // the stamp (the mod's move_lib.h has it) -- stays the one these converted files made (each copy's key); what is written
    // for a copy is the cross-reference's (AnimConv), the first file's. movelib_rev.py the same.
    std::map<int, OrderedCounter<int>> kinds;
    for (auto& [a, b] : B0) kinds[a >> 2].add(b >> 2);
    AnimConv CONV(NM, RV);
    std::map<Anim, std::pair<Bytes, Bytes>> content;
    auto anim = [&](uint32_t f) {
        Bytes d = RV(f);
        auto it = B0.find(d[0]);
        if (it != B0.end()) d[0] = it->second;
        else {
            auto k = kinds.find(d[0] >> 2);
            auto s = KIND_SEEN.find(d[0] >> 2);
            if (k != kinds.end()) d[0] = (uint8_t)(k->second.most() << 2 | (d[0] & 3));
            else if (s != KIND_SEEN.end()) d[0] = (uint8_t)(s->second << 2 | (d[0] & 3));
            else fail("movelib: Revenge file 0x%X's first byte has no No Mercy value", f);
        }
        Anim key{ d, REC.rec(f) };
        if (!content.count(key)) content.emplace(key, CONV(f));
        return key;
    };
    Bytes t = read_file(index_path);
    auto lib = nlohmann::ordered_json::parse(std::string(t.begin(), t.end()));
    std::map<Anim, uint32_t> anim_ix;
    std::vector<Anim> anims;
    auto anim_of = [&](const Anim& a) {
        auto it = anim_ix.find(a);
        if (it != anim_ix.end()) return it->second;
        anim_ix[a] = (uint32_t)anims.size(); anims.push_back(a);
        return (uint32_t)anims.size() - 1;
    };
    struct Row { const nlohmann::ordered_json* x; uint32_t pair, block; };
    std::vector<Row> rows;
    for (auto& x : lib) {
        if (!x.contains("fa")) { rows.push_back({ &x, 0xFFFFFFFF, 0xFFFFFFFF }); continue; }
        Anim pa = anim(hexnum(x["fa"].get<std::string>())), pd = anim(hexnum(x["fd"].get<std::string>()));
        std::vector<Anim> bk;
        if (x.contains("cont") && !x["cont"].empty()) for (auto& f : x["cont"]) bk.push_back(anim(hexnum(f.get<std::string>())));
        uint32_t pair_ix = anim_of(pa); anim_of(pd);
        if (anim_ix[pd] != pair_ix + 1) { pair_ix = (uint32_t)anims.size(); anims.push_back(pa); anims.push_back(pd); }
        uint32_t block_ix = 0xFFFFFFFF;
        if (!bk.empty()) {
            block_ix = (uint32_t)anims.size();
            std::vector<int64_t> ix;
            bool all = true;
            for (auto& a : bk) { auto it = anim_ix.find(a); if (it == anim_ix.end()) { all = false; ix.push_back(-1); } else ix.push_back(it->second); }
            bool run = all;
            for (size_t i = 0; run && i < ix.size(); i++) if (ix[i] != ix[0] + (int64_t)i) run = false;
            if (run && ix.size() == 8) block_ix = (uint32_t)ix[0];
            else for (auto& a : bk) anims.push_back(a);
        }
        rows.push_back({ &x, pair_ix, block_ix });
    }
    // (2026-11-20, revmoves) A move's roles after its pair (the index's "fx": a double team's man, a turnbuckle dive's
    // landings and miss -- No Mercy asks the move for them, file + 2 ..): fresh copies after every other animation, in a
    // row, named in the extension after the data (movelib_rev.py the same). The moves, IDs and stamp don't change.
    struct More { uint32_t row, first, n; };
    std::vector<More> more;
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& x = *rows[i].x;
        if (!x.contains("fx") || x["fx"].empty() || rows[i].pair == 0xFFFFFFFF) continue;
        More m{ (uint32_t)i, (uint32_t)anims.size(), (uint32_t)x["fx"].size() };
        for (auto& f : x["fx"]) anims.push_back(anim(hexnum(f.get<std::string>())));
        more.push_back(m);
    }
    Bytes strings, data, mv, an;
    for (auto& r : rows) {
        uint32_t off = (uint32_t)strings.size();
        std::string id = (*r.x)["id"].get<std::string>();
        strings.insert(strings.end(), id.begin(), id.end()); strings.push_back(0);
        le32(mv, off); le16(mv, (*r.x)["base"].get<uint32_t>()); le16(mv, r.x->contains("ground") ? (*r.x)["ground"].get<uint32_t>() : 0);
        le32(mv, r.pair); le32(mv, r.block);
    }
    for (auto& key : anims) {
        const auto& [b, rec] = content.at(key);
        if (rec.size() != 36) fail("movelib: a record isn't 36 bytes");
        le32(an, (uint32_t)data.size()); le32(an, (uint32_t)b.size());
        an.insert(an.end(), rec.begin(), rec.end());
        data.insert(data.end(), b.begin(), b.end());
        data.resize(data.size() + ((8 - b.size() % 8) % 8), 0);
    }
    Bytes ms = mv; ms.insert(ms.end(), strings.begin(), strings.end());
    uint32_t stamp = (uint32_t)std::stoul(md5_hex(ms).substr(0, 8), nullptr, 16);
    Bytes out = { 'N', 'M', 'M', 'O', 'V', 'L', 'I', 'B' };
    for (uint32_t v : { 1u, (uint32_t)rows.size(), (uint32_t)anims.size(), stamp, (uint32_t)strings.size(), (uint32_t)data.size() }) le32(out, v);
    for (auto* p : { &mv, &an, &strings, &data }) out.insert(out.end(), p->begin(), p->end());
    if (!more.empty()) {                             // (2026-11-20, revmoves: the extension)
        for (char c : std::string("NMLIBX01")) out.push_back((uint8_t)c);
        le32(out, (uint32_t)more.size());
        for (auto& m : more) { le32(out, m.row); le32(out, m.first); le32(out, m.n); }
    }
    return out;
}

}  // namespace ri
