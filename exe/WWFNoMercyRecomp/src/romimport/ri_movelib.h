// ri_movelib.h (phase3, 2026-11-19): the move libraries -- the source games' own move animations and their records, in
// No Mercy's form, which imported wrestlers' moves play (content\movelib_revenge.bin, movelib_wm2000.bin).
//   movelib_revenge: the C++ twin of tools\romimport\movelib_rev.py (the Revenge ROM + the No Mercy ROM + our frozen index
//                    tools\handlers\handlers\revenge\library_index.json: IDs, base moves, Revenge file numbers).
#pragma once
#include "ri_roms.h"
#include <map>

namespace ri {

// The library's bytes, or empty when the Revenge (or No Mercy) ROM isn't there.
Bytes movelib_revenge(Roms& R, const fs::path& library_index_json);

// Python's Counter.most_common(1)[0][0] over (value, count) pairs kept in first-insertion order: the largest count, the
// first inserted among equals.
template <class K> struct OrderedCounter {
    std::vector<std::pair<K, long>> items;
    void add(const K& k, long n = 1) {
        for (auto& it : items) if (it.first == k) { it.second += n; return; }
        items.push_back({ k, n });
    }
    const K& most() const {
        size_t best = 0;
        for (size_t i = 1; i < items.size(); i++) if (items[i].second > items[best].second) best = i;
        return items[best].first;
    }
};

// gen/revanims.py byte0_table: Revenge's first byte of an animation -> No Mercy's (the most usual over the animations both
// games have, the rest of the file the same).
std::map<uint8_t, uint8_t> byte0_table(const RomFiles& NM, const RomFiles& RV);
// gen/revanims.py rec_conv: a Revenge animation record (36 bytes, file 0x20F, from file 0xE39) as No Mercy's (file 0x2BF):
// byte by byte through the most usual mapping over the animations both games have, the frames kept, the sounds by their
// own mapping (and our REC_SOUND: Nash's jackknife's thud).
struct RecConv {
    Bytes RT;
    std::vector<std::map<uint8_t, uint8_t>> conv;
    std::map<uint8_t, uint8_t> snd;
    Bytes rec(uint32_t f) const;          // REC(f): the record of Revenge animation file f, No Mercy's
};
RecConv rec_conv(const RomFiles& NM, const RomFiles& RV);

}  // namespace ri
