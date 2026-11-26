// ri_akimodel.h (phase3 p3folders, 2026-11-19): the AKI handlers' shared model core -- an imported wrestler's own model
// as his folder has it, No Mercy's hands in his own skin, his wrists onto those hands, and his select photo rendered from
// the model. The C++ twins of:
//   tools\handlers\handlers\aki\model.py      Pool (files by content, numbered from 0x5000), write (model\ in the folder)
//   tools\handlers\handlers\aki\skintone.py   nm_hands (the user's rule: his source game's skin; only the hands No Mercy's)
//   tools\handlers\handlers\aki\handfit.py    fit_wrists (the forearms' wrist ends moved onto No Mercy's palms)
//   tools\romrender\rr_model.py (+ rr.py)     photo_from_model (aki\photo.py photo_from_pool): head and shoulders, 64 x 64 CI4
// used by every handler (WrestleMania 2000, Revenge, Badd Blood, No Mercy Plus).
#pragma once
#include "ri_roms.h"
#include <functional>
#include <map>
#include <optional>
#include <set>

namespace ri {

// ---- handlers\aki\model.py -----------------------------------------------------------------------------------------------
constexpr uint32_t POOL0 = 0x5000;          // (gen/wmport.py WMP_FILE0: the numbers the build pool hands out)
constexpr uint32_t DESC_SIZE = 0x270;

// Files by content, numbered from POOL0 as they're first seen; a (picture, palette) pair takes two numbers, the palette's
// first (gen/wmport.py's Files: the same numbering, so a model built here is the same model).
class Pool {
public:
    std::vector<Bytes> data;
    std::set<uint32_t> pairs;               // the first number (the palette's) of each pair
    uint32_t one(const Bytes& b);
    // -> (picture's number, palette's number)
    std::pair<uint32_t, uint32_t> pair(const Bytes& tex, const Bytes& pal);
    const Bytes& get(uint32_t i) const { return data[i - POOL0]; }
    bool ours(uint32_t v) const { return v >= POOL0 && v < POOL0 + data.size(); }
private:
    std::map<Bytes, uint32_t> by_;
    std::map<std::pair<Bytes, Bytes>, uint32_t> by_pair_;
};

// model.py part_fields(): every offset in a descriptor that can name a file (mesh, picture, palette of parts 1-27)
const std::vector<int>& model_fields();
inline int part_offset(int k) { return k <= 20 ? 8 * k : 0xA8 + 8 * (k - 21); }

// model.py write: the descriptors (file fields numbered by the pool) -> model\c<N>.bin, model\files\<sha1 16>.bin,
// model\model.json in the folder; -> the number of files listed
size_t model_write(const fs::path& folder, const std::vector<Bytes>& descs, const Pool& pool);

// ---- handlers\aki\skintone.py ------------------------------------------------------------------------------------------------
// The skins seen on a man's costumes so far (skinned()'s dict): his tone, and for Revenge's built-in ports his bare parts.
struct Skins {
    std::map<std::string, std::array<double, 3>> tone;
    std::map<std::string, std::set<int>> bare;
};
// a hand kit: the hand meshes per part and the hand picture (picture file, palette file) -- No Mercy's men's (the default)
// or its women's (WOMAN_KIT)
struct HandKit { std::vector<std::pair<int, uint32_t>> mesh; uint32_t pic, pal; bool woman; };
const HandKit& man_kit();
const HandKit& woman_kit();
// The "no bare skin to go by" case (the Python's SystemExit, the caller decides)
struct NoSkin : Error { using Error::Error; };
// nm_hands: the descriptor with No Mercy's hands in his own skin (pool: his files; nm: No Mercy's files); rev_ported: the
// man is one of Revenge's built-in ports (wmport.REV_PORTED: his bare parts kept from costume 0). Throws NoSkin.
Bytes nm_hands(const Bytes& desc, Pool& pool, const RomFiles& nm, Skins& skins, const std::string& who, const HandKit& kit,
               bool rev_ported = false);
// the colour helpers (exposed for the handlers' head_tone): wmport._rgb, _pix, _skin
std::array<int, 3> rgb5551(uint32_t v);
std::vector<int> tex_pixels(const Bytes& tex);
bool wm_skin(double r, double g, double b);

// ---- handlers\aki\handfit.py ----------------------------------------------------------------------------------------------
std::vector<Bytes> fit_wrists(const std::vector<Bytes>& descs, Pool& pool, const RomFiles& nm);

// ---- tools\romrender\rr_model.py ------------------------------------------------------------------------------------------
// get(field offset) -> the file the field names (nullopt: none)
typedef std::function<std::optional<Bytes>(int)> FieldGetter;
Bytes photo_from_model(const Bytes& desc, const FieldGetter& get);
// aki\photo.py photo_from_pool: the descriptor numbered by the pool, the cartridge's own files by No Mercy's numbers
Bytes photo_from_pool(const Bytes& desc, const Pool& pool, const RomFiles& nm);
extern const char* const PHOTO_HOW;       // rr_model.PHOTO_HOW

// ---- Python number forms the folders' JSON needs ------------------------------------------------------------------------------
double py_round(double x, int ndigits);   // round(x, n) for a float (correctly rounded decimal, ties to even)
long long py_round_int(double x);         // round(x) -> int (ties to even)

}  // namespace ri
