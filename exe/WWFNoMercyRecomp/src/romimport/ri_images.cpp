// ri_images.cpp (phase3, 2026-11-19): RAM images from the ROMs (ri_images.h): images.py build, banks.py place, make_data.py's
// image extras (van_sel_extra / van_sel_records, wm2k_ram_extra, bank_extra, rev_boot_extra) and its No Mercy Plus bank.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif
#include "ri_images.h"
#include "ri_mips.h"
#include "json/json.hpp"

namespace ri {

using json = nlohmann::json;

Bytes image_from_map(const std::string& text, Roms& R) {
    json m = json::parse(text);
    std::string game = m["game"].get<std::string>();
    auto rom = R.rom(game);
    if (!rom) fail("the %s ROM isn't there", game.c_str());
    const RomFiles* F = R.files(game);
    Bytes img(IMAGE_SIZE, 0);
    for (auto& r : m["runs"]) {
        uint32_t a = r[0].get<uint32_t>() & 0x7FFFFF;
        const uint8_t* src; size_t L;
        if (r[1].get<std::string>() == "rom") {
            uint32_t o = r[2].get<uint32_t>(); L = r[3].get<size_t>();
            if (o + L > rom->size()) fail("%s map: a run past the ROM's end", game.c_str());
            src = rom->data() + o;
        } else {
            uint32_t f = r[2].get<uint32_t>(), o = r[3].get<uint32_t>(); L = r[4].get<size_t>();
            const Bytes& d = (*F)(f);
            if (o + L > d.size()) L = o < d.size() ? d.size() - o : 0;      // (Python's slice: what the file has)
            src = d.data() + o;
        }
        if (a + L > IMAGE_SIZE) fail("%s map: a run past 8 MB", game.c_str());
        memcpy(&img[a], src, L);
    }
    if (m.contains("words"))
        for (auto& w : m["words"]) put_be32(&img[w[0].get<uint32_t>() & 0x7FFFFF], w[1].get<uint32_t>());
    return img;
}

// ---- music banks (banks.py) -----------------------------------------------------------------------------------------
// game -> (header ROM offset, ctl ROM offset, samples ROM offset, B, C). B and C are the rig session's heap: our numbers,
// kept so the pointer words the content index copies come out the same; the bytes are the ROM's.
struct BankInfo { const char* game; uint32_t hdr, ctl, smp, B, C; };
static const BankInfo BANKS[] = {
    { "nmplus_v3", 0x2E73826, 0x2E74E56, 0x2E77B56, 0x807BFB80, 0x807C11C0 },
    { "wm2000",    0x143FD70, 0x14407C0, 0x1442E20, 0x80362B50, 0x803635B0 },
    { "baddblood", 0x140045C, 0x1400EAC, 0x140350C, 0x80362B50, 0x803635B0 },
};

BankAt bank_place(Bytes& img, const Bytes& rom, const std::string& game) {
    const BankInfo* b = nullptr;
    for (auto& x : BANKS) if (game == x.game) b = &x;
    if (!b) fail("no music bank known for %s", game.c_str());
    const uint32_t B = b->B, C = b->C, hl = b->ctl - b->hdr, cl = b->smp - b->ctl;
    auto r32 = [&](uint32_t a) { return be32(&img[a & 0x7FFFFF]); };
    auto w32 = [&](uint32_t a, uint32_t v) { put_be32(&img[a & 0x7FFFFF], v); };
    memcpy(&img[B & 0x7FFFFF], rom.data() + b->hdr, hl);
    memcpy(&img[C & 0x7FFFFF], rom.data() + b->ctl, cl);
    // set_up_sound_effect_bank (0x80027CA0)
    w32(B + 0xC, 1); w32(B + 0x10, C);
    w32(B + 0x14, r32(B + 0x14) + B);
    int32_t n_fx = s32(r32(B));
    for (int32_t i = 0; i < n_fx; i++) w32(B + 8 * i + 0x18, r32(B + 8 * i + 0x18) + B);
    // relocate_sample_bank (0x80029244)
    w32(C + 0x10, r32(C + 0x10) | 0x80000000u);
    auto add_nonzero = [&](uint32_t at, uint32_t n) {
        for (uint32_t k = 0; k < n; k++) { uint32_t v = r32(at + 4 * k); if (v) w32(at + 4 * k, v + C); }
    };
    add_nonzero(C + 0x24, 3);
    add_nonzero(r32(C + 0x2C), r32(C + 0x20));
    uint32_t n = r32(C + 0x20);
    for (uint32_t k = 0; k < n; k++) {
        uint32_t ta = r32(C + 0x28) + 4 * k;
        int fine = sx8(img[ta & 0x7FFFFF]);
        float f = (float)((double)(float)fine / 100.0);                    // (the double 100.0 at 0x80055F08)
        int coarse = sx8((uint8_t)(img[(r32(C + 0x24) + k) & 0x7FFFFF] - 0x30));
        f = f + (float)coarse;
        uint32_t fw; memcpy(&fw, &f, 4);
        put_be32(&img[ta & 0x7FFFFF], fw);
        uint32_t e = r32(r32(C + 0x2C) + 4 * k);
        if (img[(e + 9) & 0x7FFFFF] == 0) {
            uint32_t v = r32(e);
            if ((v & 0xFF000000u) != 0xFF000000u) w32(e, v + b->smp);
            img[(e + 9) & 0x7FFFFF] = 1;
            if (r32(e + 0xC)) w32(e + 0xC, r32(e + 0xC) + C);
            if (img[(e + 8) & 0x7FFFFF] == 0) w32(e + 0x10, r32(e + 0x10) + C);
        }
    }
    return { B, C };
}

// ---- the images' extras (make_data.py) ------------------------------------------------------------------------------
// No Mercy's Superstars' costume records at its Select (van_sel_records): the game's own func_8000B834(id, costume, record)
// run on the ROM (its main segment in an empty image, files 1 and 2 where the game keeps them), ordinals 1-64.
static void van_sel_records(Bytes& img, Roms& R) {
    auto rom = R.rom("nomercy"); const RomFiles& F = *R.files("nomercy");
    const uint32_t MAIN_VRAM = 0x80000400, MAIN_ROM = 0x1000, OVL_A_VRAM = 0x800D9960, ROSTER_TABLE = 0x80047124, REC_SIZE = 0x4E;
    Bytes code(IMAGE_SIZE, 0);
    uint32_t n = OVL_A_VRAM - MAIN_VRAM;
    memcpy(&code[MAIN_VRAM & 0x7FFFFF], rom->data() + MAIN_ROM, n);
    Emu E(code);
    const uint32_t FILES_AT = 0x80600000;
    E.wbytes(FILES_AT, F(1)); E.wbytes(FILES_AT + 0x10000, F(2));
    E.w32(0x80047EA0, FILES_AT); E.w32(0x80047EA4, FILES_AT + 0x10000);
    E.hooks[0x80003C94] = [&F](Emu& e) { e.wbytes(e.r[6], F(e.r[5] & 0xFFFF)); return true; };   // func_80003C94(info, file, dest)
    const uint32_t RECS = 0x8031A260, REC = 0x80790000;
    put_be32(&img[0x47E90], RECS);
    Bytes zero(REC_SIZE, 0);
    for (uint32_t o = 1; o <= 64; o++) {
        uint32_t p = be32(rom->data() + (ROSTER_TABLE - MAIN_VRAM + MAIN_ROM) + 16 * o);
        uint32_t wid = be16(rom->data() + (p - MAIN_VRAM + MAIN_ROM));
        for (uint32_t c = 0; c < 4; c++) {
            E.wbytes(REC, zero); E.call(0x8000B834, { wid, c, REC });
            uint32_t a = (RECS + (o * 4 + c) * REC_SIZE) & 0x7FFFFF;
            Bytes rec = E.rbytes(REC, 0x4D);
            memcpy(&img[a], rec.data(), 0x4D);
        }
    }
}

// Every ordinal's moveset at the Select (van_sel_extra): its moves file (0xB8 bytes) and abilities file (0x20), 0xD8 an
// ordinal, from the heap address the word at 0x80047E94 holds; the ordinal's first costume struct names the two files.
static void van_sel_extra(Bytes& img, Roms& R) {
    auto rom = R.rom("nomercy"); const RomFiles& F = *R.files("nomercy");
    const uint32_t HEAP = 0x803208D0;
    put_be32(&img[0x47E94], HEAP);
    for (uint32_t o = 0; o < 84; o++) {
        uint32_t p = be32(rom->data() + 0x47124 - 0x400 + 0x1000 + 16 * o);
        const uint8_t* cs = rom->data() + (p - 0x80000400 + 0x1000);
        uint32_t mf = be16(cs + 10), af = be16(cs + 12);
        uint8_t blk[0xD8] = {};
        if (mf) {
            const Bytes& m = F(mf); const Bytes& ab = F(af);
            memcpy(blk, m.data(), std::min<size_t>(0xB8, m.size()));
            memcpy(blk + 0xB8, ab.data(), std::min<size_t>(0x20, ab.size()));
            // (Python: F(mf)[:0xB8] + F(af)[:0x20] -- a shorter file makes a shorter block; the files are longer)
            if (m.size() < 0xB8 || ab.size() < 0x20) fail("van_sel: a moveset file shorter than the game reads");
        }
        memcpy(&img[(HEAP + o * 0xD8) & 0x7FFFFF], blk, 0xD8);
    }
    van_sel_records(img, R);
}

// Revenge's costume table as its boot leaves it (rev_boot_extra): the heap block filled by the game's own 0x8002BAB8, then
// the first colour row (+38) of entries 0 and 4-255 set to 0, as its save load writes them back.
void rev_boot_extra(Bytes& img) {
    const uint32_t T = 0x803ACFB0, N = 0x3480;
    Emu E(img);
    E.w32(0x8003A594, T); E.wbytes(T, Bytes(N, 0));
    E.call(0x8002BAB8, {}, {}, 0x807FF000, 50'000'000);
    Bytes tab = E.rbytes(T, N);
    tab[42 * 0 + 38] = 0;
    for (int i = 4; i < 256; i++) tab[42 * i + 38] = 0;
    memcpy(&img[T & 0x7FFFFF], tab.data(), N);
    put_be32(&img[0x3A594], T);
}

const char* const IMAGE_NAMES[] = { "van_sel", "wm2k_ram", "nm_pick", "rev_bm", "wm_match", "wm2k_entr", "wm_sel1", "bb_vsel",
                                    "rev_boot", "rev_ent0", "wm_close1", "wm_fly1", "rev_arena", nullptr };

const char* image_rel_path(const std::string& name) {
    static const std::pair<const char*, const char*> P[] = {
        { "van_sel", "gen_data/van_sel_ram.bin" }, { "wm2k_ram", "gen_data/wm2k_ram.bin" }, { "nm_pick", "gen_data/nm_pick_ram.bin" },
        { "rev_bm", "extracted/rev_bm_ram.bin" }, { "wm_match", "wmport/wm/match_ram.bin" }, { "wm2k_entr", "gen_data/wm2k_entr_ram.bin" },
        { "wm_sel1", "handlers_rig/wm2000/cap/sel1_ram.bin" }, { "bb_vsel", "handlers_rig/imports2/cap/vsel_ram.bin" },
        { "rev_boot", "revport/attires/cap/v_boot_ram.bin" }, { "rev_ent0", "arenaport/rev/cap/ent0_ram.bin" },
        { "wm_close1", "wmarena/rig/cap/close1_ram.bin" }, { "wm_fly1", "wmarena/rig/cap/fly1_ram.bin" },
        { "rev_arena", "arenaport/rev/cap/m0_ram.bin" } };     // (one map for all six m0..m5)
    for (auto& p : P) if (name == p.first) return p.second;
    return nullptr;
}

Bytes make_image(const std::string& name, const fs::path& maps_dir, Roms& R) {
    Bytes text = read_file(maps_dir / (name + ".json"));
    Bytes img = image_from_map(std::string(text.begin(), text.end()), R);
    if (name == "van_sel") van_sel_extra(img, R);
    else if (name == "rev_boot") rev_boot_extra(img);
    else if (name == "wm2k_ram" || name == "wm_sel1" || name == "bb_vsel") {
        std::string game = name == "bb_vsel" ? "baddblood" : "wm2000";
        auto rom = R.rom(game);
        if (!rom) fail("the %s ROM isn't there", game.c_str());
        BankAt at = bank_place(img, *rom, game);
        put_be32(&img[0x571EC], at.B);
    }
    return img;
}

NmBank nmplus_bank(const Bytes& nm_pick, Roms& R) {
    auto rom = R.rom("nmplus_v3");
    if (!rom) fail("No Mercy Plus V3 isn't there");
    NmBank out; out.ram = nm_pick;
    BankAt at = bank_place(out.ram, *rom, "nmplus_v3");
    // (make_data.py NMBANK_OVERRIDE) instrument 43's sample length as the shipped content index has it: OUR override word
    put_be32(&out.ram[0x807C254C & 0x7FFFFF], 0x0003BDF9);
    out.B = at.B; out.C = at.C;
    return out;
}

}  // namespace ri
