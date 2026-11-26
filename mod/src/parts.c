// Body parts, with more of them: the new Superstars' (No Mercy Plus V3's, gen/mkparts.py).
//
// A costume's looks are kept packed in 0x17 bytes (func_8001A9A0 packs, func_8001AC44 unpacks into a
// 0x3F-byte struct) and turned into a model by func_80018A3C through the game's tables of parts. The
// new parts are entries after the game's own in copies of those tables (parts_ext.h): the game's code
// reads these instead (gen/patches.py), and takes the new entries' numbers (a lower body of 128, a
// face of 128, a mask of 32, ...), which don't fit the packed fields. So a new Superstar's costume is
// kept packed as a key, its fields cut to their widths and bit 183 set (the packing's 184th bit, which
// the game never sets); the unpacking here (func_8001AC44's) gives the whole struct for a key.
#include "arcade.h"
#include "gamefn.h"
#include "skel.h"
#include "parts_ext.h"
#include "parts_ovr.h"

// func_8001AC44's fields, in order: (offset in the unpacked struct, bits), read from bit 0 up
static const u8 fields[38][2] = {
    { 0x00, 5 }, { 0x01, 3 }, { 0x03, 7 }, { 0x04, 5 }, { 0x05, 5 }, { 0x06, 7 }, { 0x07, 5 }, { 0x08, 5 },
    { 0x09, 5 }, { 0x0A, 5 }, { 0x0B, 5 }, { 0x0C, 4 }, { 0x0F, 7 }, { 0x12, 5 }, { 0x13, 3 }, { 0x15, 6 },
    { 0x18, 5 }, { 0x1B, 5 }, { 0x1E, 3 }, { 0x1F, 5 }, { 0x21, 3 }, { 0x22, 5 }, { 0x24, 3 }, { 0x25, 5 },
    { 0x27, 3 }, { 0x28, 5 }, { 0x2A, 4 }, { 0x2B, 5 }, { 0x2D, 4 }, { 0x2E, 5 }, { 0x30, 6 }, { 0x31, 5 },
    { 0x32, 5 }, { 0x33, 5 }, { 0x36, 3 }, { 0x37, 5 }, { 0x39, 5 }, { 0x3C, 7 },
};
#define KEY_BYTE 0x16
#define KEY_BIT  0x80

// a new Superstar's costume -> its key (the record's packed looks)
const u8* parts_costume_key(int k, int c) { return ext_looks_key[k * 4 + (c & 3)]; }

static int key_of(u32 src) {
    if (!(RD8(src + KEY_BYTE) & KEY_BIT)) return -1;
    for (int i = 0; i < EXT_COSTUMES; i++) {
        int j = 0;
        while (j < 0x17 && RD8(src + (u32)j) == ext_looks_key[i][j]) j++;
        if (j == 0x17) return i;
    }
    return -1;
}

// The 38 fields' values, in fields[] order, into the looks struct at dst (a folder wrestler's "looks": roster.c, 2026-11-13).
void parts_fields_put(const u8* values, u32 dst) {
    for (int i = 0; i < 38; i++) RD8(dst + fields[i][0]) = values[i];
}

// The looks struct's offset of field i (2026-11-13, nmhandler: roster.c's stock folders check what the packing kept).
int stock_looks_offset(int i) { return fields[i][0]; }

// func_8001AC44(packed, unpacked)
const u8* roster_folder_key(u32 src, int* photo);
int roster_folder_looks(u32 src, u32 dst);
int roster_folder_hd_of_photo(int photo);
void parts_unpack(u32 src, u32 dst) {
    if (roster_folder_looks(src, dst)) return;   // (2026-11-13, handlers1: a folder wrestler's looks as numbers, as they are)
    {   // A folder wrestler with his own photo: his real looks, with his photo in place of theirs.
        int photo = 0;
        const u8* real = roster_folder_key(src, &photo);
        if (real) { parts_unpack((u32)real, dst); RD8(dst + 0x3C) = (u8)photo; return; }
    }
    int k = key_of(src);
    if (k >= 0) {
        for (int i = 0; i < 38; i++) RD8(dst + fields[i][0]) = ext_looks[k][fields[i][0]];
        return;
    }
    u32 bit = 0;
    for (int i = 0; i < 38; i++) {
        u32 v = 0;
        for (u32 b = 0; b < fields[i][1]; b++, bit++) v |= (u32)((RD8(src + (bit >> 3)) >> (bit & 7)) & 1u) << b;
        RD8(dst + fields[i][0]) = (u8)v;
    }
}

// Once func_80018A3C has made a model (its looks struct, the model): the costumes made otherwise
// (parts_ovr.h, gen/mkovr.py) get theirs.
static int parts_model_ovr(u32 un, u32 model);
int roster_folder_of_photo(int photo);
int roster_folder_size(int i);
// (2026-11-13, revbody) A folder wrestler's own size ("size" in his folder): the game sets a model's size from the man's
// height right after it has made the model (build_model_from_looks: func_80013F48 with superstar_size), so it is kept
// here per model slot as the model is made, and put on in place of the height's once the game is done with it
// (parts_size_put, from skel.c's func_80016464, which every model build calls next). Kept as size + 1: 0, none -- the
// height's stays.
static s16 size_of_slot[NM_SLOTS_CAP];
GAMEFN(void, func_80013F48, 0x80013F48u, (s32 slot, s32 size));   // (set_slot_model_size: stores it, remakes the scale)
void parts_size_put(s32 slot) {
    if (slot < 0 || slot >= NM_SLOTS_CAP || !size_of_slot[slot]) return;
    func_80013F48(slot, size_of_slot[slot] - 1);
    size_of_slot[slot] = 0;
}
void roster_folder_base_looks(int i, int c, u32 dst);
// (2026-11-12, handlers: a folder wrestler's own low-detail model, below)
static void parts_model_folder(int f, u32 un, u32 model, int built_in);
u32 files_ext_read(u32 file, u32 dest);
void files_pool_send(void);
extern int plat_wrestler_model(int i, int costume, u32 model);
extern void plat_wrestler_model_note(int i, int costume, int bytes_differ, u32 files);
extern void plat_wrestler_model_where(u32 slots, u32 slot_count, u32 shared_list);
extern u32 plat_wrestler_model_check(int i, int costume, u32 model);
void parts_model_post(u32 un, u32 model) {
    // The whole-body models below are chosen by the photo number in the looks. A folder wrestler with his own photo has to
    // be matched on the photo of the wrestler he's based on, or he'd lose that model and be built from basic parts.
    const int own_photo = RD8(un + 0x3C), folder = roster_folder_of_photo(own_photo);
    {   // (2026-11-13, revbody) his own size, if his folder gives one, for the model slot this is (the slot's descriptor)
        const u32 slot = (model - SIXA_800A8980) / 0x270u;
        if (model >= SIXA_800A8980 && slot < NM_SLOTS_CAP && model == SIXA_800A8980 + slot * 0x270u)
            size_of_slot[slot] = (s16)((folder >= 0 ? roster_folder_size(folder) : -1) + 1);
    }
    if (folder >= 0) {
        u8 base_looks[0x40] __attribute__((aligned(8)));
        roster_folder_base_looks(folder, 0, (u32)base_looks);    // (2026-11-13: his packed looks, or his looks as numbers)
        RD8(un + 0x3C) = base_looks[0x3C];
    }
    const int built_in = parts_model_ovr(un, model);
    RD8(un + 0x3C) = (u8)own_photo;
    // A folder wrestler's own full-detail model (hd.c: model hd, the folders' in their order). The rest of the descriptor
    // stays his base's: the game builds and animates that model as ever, and hd.c draws his over its bones. So his base has
    // to be a built-in WCW/nWo Revenge port (SKEL_MAGIC_WM with SKEL_WM_REV: the kind hd.c reads, whose flags word has the
    // model bits free -- a WrestleMania 2000 port's holds its game's head offsets there).
    // (no2k15, 2026-11-23) The model numbers used to start at 2, after the two built-in WWE 2K15 test slots' (0x011A, 0x011B),
    // and SKEL_WM_HD came only from their descriptors (Goldberg's parts, gen/wmport.py) -- "based_on" had to be one of them.
    // They are removed, so SKEL_WM_HD is set here, on a Revenge port's descriptor; on any other base's nothing is set and he
    // is drawn as his base.
    const int hd = roster_folder_hd_of_photo(RD8(un + 0x3C));
    if (hd >= 0 && RD32(model + SKEL_MAGIC_AT) == SKEL_MAGIC_WM && (RD32(model + SKEL_WM_FLAGS) & SKEL_WM_REV)) {
        const u32 flags = RD32(model + SKEL_WM_FLAGS) & ~SKEL_WM_HD_MODEL;
        RD32(model + SKEL_WM_FLAGS) = flags | SKEL_WM_HD | ((u32)hd << SKEL_WM_HD_MODEL_AT);
    }
    // (2026-11-12, handlers) A folder wrestler's own low-detail model (model\model.json, as the source-game handlers write
    // it): the descriptor for this costume, whole but for the game's state byte, in place of the one just made.
    if (folder >= 0) parts_model_folder(folder, un, model, built_in);
}

// ---- a folder wrestler's own low-detail model (2026-11-12, handlers) --------------------------------------------------
// Which of his four costumes these looks are: the one whose looks are most like them, as parts_model_ovr matches (the game
// changes a field or two of a costume's looks for the screen it's on: the entrance attire, 0x09). His photo (0x3C) is his
// own in un and his base's in his looks, so it isn't compared. -1: none close enough.
static int folder_costume(int f, u32 un) {
    int best = -1, best_n = 0;
    for (int c = 0; c < 4; c++) {
        u8 l[0x40] __attribute__((aligned(8)));
        roster_folder_base_looks(f, c, (u32)l);
        int n = 0;
        for (int i = 0; i < 38; i++) { const u32 o = fields[i][0]; n += o == 0x09 || o == 0x3C || RD8(un + o) == l[o]; }
        if (n > best_n) { best_n = n; best = c; }
    }
    return best_n >= 34 ? best : -1;
}
// QA (option qa_model_twin): the model the game made just before his own went in -- the built-in port's, when his costumes
// are a built-in port's (parts_model_ovr put it in) -- compared with his, byte for byte, a file number by the file's bytes.
// Written to the exe's log once per wrestler and costume ("[wrestlers] twin ..."). Off in play.
// (2026-11-13, handlers1) A model with no built-in twin -- a wrestler never ported -- is checked against his own folder
// instead: each file field of the model now in the game names, by the numbers his costume holds now, the very file of his
// that his folder says ("[wrestlers] own ...", exe recomp_arcade_wrestler_model_check).
static u8 twin_before[0x270] __attribute__((aligned(8)));
static int twin_on = -1;
static int twin_same_file(u32 a, u32 b) {
    if (a == b) return 1;
    if (a < 0x4C03u || b < 0x4C03u) return 0;          // (a cartridge file against another file: different numbers, different files)
    const u32 na = files_ext_read(a, 0), nb = files_ext_read(b, 0);
    if (!na || na != nb) return 0;
    const u32 ba = func_800007D4(na), bb = func_800007D4(nb);
    int same = ba && bb;
    if (same) {
        files_ext_read(a, ba); files_ext_read(b, bb);
        for (u32 i = 0; i < na && same; i++) same = RD8(ba + i) == RD8(bb + i);
    }
    if (ba) func_8000093C(ba);
    if (bb) func_8000093C(bb);
    return same;
}
static void parts_twin(int f, int c, u32 model) {
    static u8 field[0x270];
    for (u32 i = 0; i < 0x270; i++) field[i] = 0;
    for (u32 k = 1; k < 28; k++) {                     // (every place a file can be named: parts 1-27, mesh / picture / palette)
        const u32 o = k <= 20 ? 8 * k : 0xA8 + 8 * (k - 21);
        for (u32 j = 0; j < 6; j++) field[o + j] = 1;
    }
    int bytes = 0, same = 0, differ = 0;
    for (u32 i = 0; i < 0x270; i++) {
        if (i == 0x254) continue;                      // (the game's own state byte: kept)
        if (!field[i]) { bytes += twin_before[i] != RD8(model + i); continue; }
        if (i & 1) continue;
        const u32 a = (u32)twin_before[i] << 8 | twin_before[i + 1], b = RD16(model + i);
        if (a == b) continue;
        if (twin_same_file(a, b)) same++; else differ++;
    }
    plat_wrestler_model_note(f, c, bytes, (u32)same << 16 | (u32)differ);
}
static void parts_model_folder(int f, u32 un, u32 model, int built_in) {
    static int where_said;
    if (!where_said) {     // (2026-11-13, handlers1: where the game keeps what can hold a file number -- the exe gives his
        where_said = 1;    //  model's numbers only while the game loads it, and checks these before giving them to another)
        plat_wrestler_model_where(SIXA_800A8980, NM_SLOTS_CAP, 0x80047EB4u);
        files_pool_send(); //  (2026-11-13, filepool: and from which numbers: every one no other file has, files.c)
    }
    const int c = folder_costume(f, un);
    if (c < 0) return;
    const int fx = roster_seat_folder(f);   // (f is his seat; the exe knows him by his folder: seatsroot's staging seats)
    if (fx < 0) return;
    if (twin_on < 0) twin_on = (int)plat_config("qa_model_twin", 0);
    if (twin_on) for (u32 i = 0; i < 0x270; i++) twin_before[i] = RD8(model + i);
    if (!plat_wrestler_model(fx, c, model)) return;
    if (!twin_on) return;
    if (built_in) parts_twin(fx, c, model);
    else {
        const u32 r = plat_wrestler_model_check(fx, c, model);
        plat_wrestler_model_note(fx, c, -1, r);
    }
}
// (returns 1 if it put a model in: 2026-11-13, for the QA twin)
static int parts_model_ovr(u32 un, u32 model) {
    // Droz's and Mideon's whole models, Jeff Jarrett's and The Blue Meanie's heads: the bytes each names.
    // Theirs are the looks with their photo (theirs alone); of their costumes, the one most like these
    // looks (the game changes a field or two of a costume's looks for the screen it's on: field 0x09)
    int best = -1, best_n = 0;
    for (int k = 0; k < OVR_N; k++) {
        const u8* l = ext_looks[ovr_costume[k]];
        if (RD8(un + 0x3C) != l[0x3C]) continue;
        if (ovr_entr[k] != 0xFF && RD8(un + 0x09) != ovr_entr[k]) continue;   // (with / without its entrance attire)
        int n = 0;
        for (int i = 0; i < 38; i++) n += fields[i][0] == 0x09 || RD8(un + fields[i][0]) == l[fields[i][0]];
        if (n > best_n) { best_n = n; best = k; }
    }
    if (best >= 0 && best_n >= 34) {
        for (u32 i = 0; i < 0x270; i++)
            if (ovr_mask[best][i >> 3] & (1u << (i & 7))) RD8(model + i) = ovr_model[best][i];
        return 1;
    }
    return 0;
}

// the files of the new parts (files.c): size, and the file read into dest
u32 parts_file(u32 file, u32 dest) {
    for (int i = 0; i < OVR_FILES_N; i++) {
        if (ovr_files[i].id != file) continue;
        if (dest) for (u32 j = 0; j < ovr_files[i].size; j++) RD8(dest + j) = ovr_files[i].data[j];
        return ovr_files[i].size;
    }
    for (int i = 0; i < PART_FILES_N; i++) {
        if (part_files[i].id != file) continue;
        if (dest) for (u32 j = 0; j < part_files[i].size; j++) RD8(dest + j) = part_files[i].data[j];
        return part_files[i].size;
    }
    return 0;
}
