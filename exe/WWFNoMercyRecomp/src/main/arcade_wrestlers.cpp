// Wrestlers added from folders, without rebuilding the game.
//
// Each wrestler is a folder holding a wrestler.json, anywhere inside the game's "wrestlers" folder: directly, or inside
// collection folders of the user's own (WCW\, Japan\80s\ ...), as deep as they like (2026-11-12). At the start of the game
// this reads them all and hands them to the mod (mod src/roster.c, "wrestlers from folders"), which puts them on the roster
// alongside the rest. Folders starting with an underscore (or a dot) are skipped with everything in them, so
// wrestlers\_example can sit there as a template.
//
// Each has a string ID (2026-11-12): his folder's own name in lower case ("Rock On Austin" -> rock_on_austin), or "id" in
// his wrestler.json. Stories and select_pages.json name him by it. Two of the same ID -- an exact copy of a folder in
// another collection, two folders pinning one "id", or a built-in Superstar's ID -- never stop either: the later one (by
// path) is <id>~2, <id>~3 ..., and the log says so ("ID CLASH"). The game's number for him is given here, never clashing.
//
// What a wrestler.json can say (see wrestlers\_example\wrestler.json for one with comments in its "about"):
//   "id"          optional. A string: his ID, if not his folder's name ("stone_cold_99"). A number like "0x0320" (the
//                 folders made before string IDs): kept as the game's number for him if no one has it, else he gets
//                 another (said in the log); his string ID is then his folder's name.
//   "based_on"    the wrestler he starts as a copy of: a string ID ("steve_austin", the built-ins' are in the mod's
//                 gen/roster_ids.json), or a number ("0x0002"); a name ("STEVE AUSTIN") is still read for older folders
//                 (LEGACY, startup 2026-11-19: names on screen change -- the game's own files use IDs). He starts with his looks, moves,
//                 music, size. Everything below is optional and overrides that copy.
//   "name"        up to 20 characters: the full name on screen.
//   "short_name"  up to 10: the name in the match's bars.
//   "nick"        up to 16: the nickname. null: none (2026-11-13: "" or no "nick" keeps the one he's based on has).
//   "height"      inches over 5'0".
//   "weight"      pounds, less 100.
//   "size"        2026-11-13: No Mercy's size for his model, 0-255 (the game's scale 0.97 + 0.00047 x size), in place of
//                 the one his height gives (height x 255 / 36). Left out: his height's, as before. The source-game
//                 handlers write it from the man's own size there (Revenge: his body word's scale class).
//   "music"       the music slot.
//   "video"       the Titantron video (0: none). Changed for new features (Ultimate No Mercy): 2026-11-20 (revvisual) left
//                 out, his Titantron is his folder's own video file, and with none there, none at all -- no longer the
//                 video of the man he's "based_on" (the Revenge imports, based on The Rock or Kane, showed theirs).
//   "packed_variants" (2026-11-14: was "costumes", still read)
//                 four hex strings of 23 bytes each, the game's packed look for each of his costumes (body parts, colours).
//   "looks"       2026-11-13, in place of "costumes": his four costumes' looks as numbers, each a list of the 38 fields the
//                 game unpacks a costume's packed look into (func_8001AC44), in its order -- the looks struct's bytes
//                 0x00, 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0F, 0x12, 0x13, 0x15, 0x18,
//                 0x1B, 0x1E, 0x1F, 0x21, 0x22, 0x24, 0x25, 0x27, 0x28, 0x2A, 0x2B, 0x2D, 0x2E, 0x30, 0x31, 0x32, 0x33,
//                 0x36, 0x37, 0x39, 0x3C (0x00 the body, 0x09 the entrance attire, 0x3C the photo ...), each 0-255.
//                 Unlike a packed look, these hold the parts added after the game's own (numbers too big for its
//                 packing), and two wrestlers' may be alike: the mod keeps them as they are (mod src/roster.c).
//   "moveset"     a hex string of 216 bytes, the game's packed moveset.
//                 (2026-11-13) Its moves are limited to what the game's packing can say: each slot only a move from its
//                 own few lists. A moves.json (below) has no such limit, and wins for the moves; the packed bytes still
//                 give his abilities and are what the Create-a-Wrestler editor copies.
//   "builtin"     in place of "based_on" (2026-11-12): this folder IS that built-in Superstar (his frozen string ID, or his
//                 number), not a copy of him. 2026-11-13 (nmhandler): such a stock folder may carry his data too -- what
//                 the No Mercy handler (tools\handlers\nm_import.py) writes from the user's ROM for every
//                 stock Superstar: the record fields above, "looks", "moveset", photo.bin -- and then the game loads him
//                 from it: each field the folder has goes over what the game builds from its own tables, which are only
//                 the fallback (a missing field is the built-in's). See "stock folders" below. 2026-11-13 (removal, the
//                 user's decision): a missing folder of a man his collection's stock_roster.json lists REMOVES him (see
//                 "removing a stock Superstar" below); with no such list a missing folder is the built-in, as before.
//   "per_variant" (2026-11-14: was "per_costume", still read) 2026-11-13, a stock folder's: a list of 4 objects, one per costume, each with any of "name", "short_name",
//                 "nick", "height", "weight", "music", "video" that costume has otherwise than the folder's own (a few
//                 stock costumes are another man: Taka's second is Funaki, Jim Ross's Jerry Lawler ...). {}: the same.
//
//   "class"       New feature (Ultimate No Mercy) -- rostertags, 2026-11-21: what he is on the roster, as No Mercy's own roster
//                 tells its people apart: "wrestler", "woman", "valet", "manager", "announcer", "referee", "official" (an agent,
//                 a commissioner's man) or "authority" (an owner or a company's president who wrestles: No Mercy's Mr. McMahon and
//                 Shane). The random pools (the Royal Rumble's draw with its NO WOMEN / ANNOUNCERS on, a Lumberjack match's
//                 lumberjacks) take only a wrestler or an authority; whom the player picks is never checked. Left out: as before
//                 (the mod's rule by his number). The importers write it from each game's own data (wrestler.json "class_from").
// wrestler.override.json (2026-11-17): the user's or the editor's changes, laid over wrestler.json as it is read (a JSON
//                  Merge Patch: apply_override below; the rules O1-O7 in tools/wrestler_override.py and the wrestlers
//                  folder's README). The importers never write it, so it outlives every re-import.
// Files a folder can also hold, beside wrestler.json (tools/wrestler_folder.py makes them):
//   moves.json     2026-11-13: his moveset as a plain list of slots, any move in any slot: {"slots": [[move, flag, flag,
//                  flag], ... 292 of them]} -- the game's 292 unpacked entries in order, each its move and its three flags
//                  (0 or 1; the third marks a special), as the importers write it (tools\handlers). A move is No
//                  Mercy's move number (or "nomercy:<number>"), a library move's ID ("revenge:58": see move_library), or
//                  null for what his packed moveset has there. The mod puts them straight into what the game unpacks (mod
//                  src/moveset_unpack.h), so nothing is lost to the packing. (2026-11-13, movelib) A library move is played on
//                  the No Mercy move the library says, or on the one after an "@": "revenge:126c_1270@940"; "own:<name>" is
//                  one of his folder's own moves (moves\movex.json, below). "abilities" there are not read yet: they come
//                  from "moveset" (or his base's). Anything wrong in it: he keeps the packed moveset, and the log says why.
//   moves\movex.json, moves\anims\
//                  2026-11-13 (movelib): moves of his own, each with its own animations, for his moves.json to name as
//                  "own:<name>": {"moves": {"<name>": {"on": <the No Mercy move it's played on: its record, its timing>,
//                  "anims": [<the attacker's file>, <the defender's>], "records": [<36 bytes of hex>, <36>],
//                  "then": {"anims": [8 files], "records": [8]}}}} -- the files relative to moves\, as the game plays an
//                  animation (No Mercy's form: a 4-byte header, then a key every 2 frames), each with its animation
//                  record (No Mercy's file 0x2BF's form: when it's struck, its sounds); "then", for a move that goes on to a
//                  hold, the hold's loop pair and its three endings' pairs. Any number of moves; their files get game file
//                  numbers only while a match has him in it (as his model's do), so how many there are costs nothing.
//   photo.bin      his select-screen photo: 16-colour palette (32 bytes), then 64 x 64 at 4 bits a pixel (2048 bytes).
//   photo_c1.bin .. photo_c3.bin
//                  2026-11-13, a stock folder's: the photo of a costume whose photo isn't his first's (the same format).
//   hd.bin, hd.json, pictures.pak
//                  his own full-detail model, the way imported wrestlers come in. All three are needed, and so is a photo:
//                  the photo number is how the model code recognises him. "based_on" must be a built-in WCW/nWo Revenge
//                  port (Goldberg, Sting ...), because he takes his skeleton's setup from that model (mod parts.c; no2k15,
//                  2026-11-23: it was one of the two WWE 2K15 test slots, 0x011A / 0x011B, removed since).
//
//   theme.bin, theme.meta
//                  his own entrance theme. tools/wrestler_theme.py makes them from a music file.
//   video.bin      his own Titantron video. tools/wrestler_video.py makes it from a video file.
//   theme.mp3 / .ogg / .flac / .wav (or another sound file), titantron.mp4 / .webm / .wmv ... (or video.*)
//                  2026-11-10: his theme and Titantron video as they are, no conversion (arcade_media.cpp plays them).
//                  With one of these, the .bin of the same kind isn't played.
//                  2026-11-12: theme.bin and video.bin are played by arcade_media.cpp too (no 64-slot banks, nothing
//                  loaded at start-up), unless WCW_MEDIA=0 turns that player off: then they go the old way.
//
//   model\model.json, model\c0.bin .. c3.bin, model\files\*
//                  his own low-detail model, the N64 kind (2026-11-12, handlers: the source-game handlers write it; see
//                  read_low below). It needs a photo.bin too: the photo number is how the model code recognises him.
//
// The limit is how many fit in the game's one-byte wrestler numbers (its roster ordinals): 256 - 102 - the 42 already
// built in = 112. The mod ignores any beyond that, and says so in the log. (2026-11-12: that limit is the game's record format,
// not this file's: this file reads every folder, the string IDs here have no limit of their own, and the game numbers -- 7
// groups x 255 -- are far more than that. Themes and videos from folders have no count limit while the media player is on.
// Until the ordinals are wide (Stage 2's native records), a wrestler past the 112th is never handed to the game: see
// seatable(), final081.)
// (2026-11-13, seatsroot: no count any more. Every folder is handed to the game, however many: the mod gives the game's
// one-byte ordinals out as "staging seats", to the wrestlers the game needs now and back when nothing names them -- mod
// src/roster.c "staging seats", TEMPORARY until Stage 2's native records with wide IDs. This file finds a folder by his
// game number for it (recomp_arcade_wrestler_find) and logs the seats ("[seats]" lines). What still has a count here: the
// game numbers, 7 groups x 255 (the game's 12-bit ID -- staged with the seats, or Stage 2, past ~1,700 folders), and 14
// full-detail models (HD_MAX: the descriptor's 4 bits, the packs loaded at start -- given to the first 14 folders that have
// one, as before).)
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <chrono>
#include <string>
#include <tuple>
#include <variant>
#include <vector>
#include "json/json.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/mods.hpp"         // (automerge, 2026-11-23: the option MERGE SAME WRESTLERS, as the mod keeps it)
#include "arcade_host.h"
#include "ultramodern/ultramodern.hpp"   // (the save's path: the sidecar beside it, 2026-11-13)

int arcade_packs_first_theme();     // (arcade_packs.cpp: the content pack number of the first theme)

namespace {
using json = nlohmann::json;

constexpr int NAME_BYTES = 0x15, SHORT_BYTES = 0x0B, NICK_BYTES = 0x11, KEY_BYTES = 0x17, MOVES_BYTES = 0xD8, PHOTO_BYTES = 32 + 2048;
// A moveset as a list of slots (2026-11-13): the game's 292 unpacked entries, 8 bytes each for the mod (src/moveset_unpack.h:
// the No Mercy move number, its flags -- bit 7 "not given: his packed moveset's" --, a 0, the move's ID).
constexpr int SLOTS_N = 292, SLOT_STRIDE = 8, SLOTS_BYTES = SLOT_STRIDE * SLOTS_N;
constexpr uint8_t SLOT_KEEP = 0x80;
// The moves a slot can name, by ID (2026-11-13, the user: every AKI game's moves and animations may be built in, a library per
// source game, and folders may add their own; no fixed caps). "nomercy:<number>" (or the number alone) is one of No Mercy's
// own. Every other ID is "<library>:<move>" -- "revenge:58", "own:sharpshooter" -- and is looked up here: a library move is
// built on a No Mercy move (`base`, whose record and slot it takes) and has its own number (`id`, from 1, as many as there
// are) that the mod's move rows use for its animations and record. (2026-11-13, movelib / movelib2: the built-in libraries
// register below, move_library_fill; an ID no library has is reported and the slot keeps what his packed moveset gives.)
struct MoveRef { uint32_t id; uint16_t base; uint16_t in_place = 0; };   // (in_place: "@<move>/<move>", below; 0 none)
std::map<std::string, MoveRef> move_library;
// (2026-11-13, movelib; the whole of Revenge's list on demand 2026-11-13, movelib2) The built-in libraries: made from each
// source game's ROM by its handler (tools\handlers\revlib.py: Revenge's), a content file the exe reads
// (move_library_fill, below), with the mod's src/move_lib.h made from the same -- the same count and stamp, which the mod
// checks. (2026-11-13, wm2klib) WrestleMania 2000's: the moves its Superstars use that No Mercy has no move for
// (tools\handlers\wmlib.py), read after Revenge's -- see move_library_fill.
// A move's animations: a library move's from the library's file, a folder's own (moves\movex.json) from his folder.
struct OwnAnim {
    std::filesystem::path path;        // (a folder's own: its file; empty for a library's, whose bytes are read with it)
    std::vector<uint8_t> record;       // 36 bytes.
    std::vector<uint8_t> bytes;        // The file (read with the folders: animations are a few KB, and the game's thread
                                       //  shouldn't wait on the disk for one in a match).
};
// A set of animations that get their numbers together: one library move's (its pair, its hold's 8), or one folder's own
// moves' (all of them).
struct AnimSet {
    std::string shown;                 // Its library move's ID, or his folder, for the log.
    std::vector<OwnAnim> files;        // In order: each move's pair, then its hold's 8 -- runs given numbers in a row.
    std::vector<std::pair<uint32_t, uint32_t>> runs;   // (first file, how many) that must have numbers one after another.
};
std::vector<AnimSet> anim_sets;
// Every move with an ID past No Mercy's, by its number - 1: the built-in libraries' first (1 .. lib_n), then the folders'
// own. anims: its set in anim_sets (-1: none -- a move made by hand, played with the No Mercy move's own animation); pair /
// then: its files' places in that set; ground: the entry the game's ground grapple picks after it (0: none; mod movex.c).
// (2026-11-20, revmoves) n: how many animations the move has from its pair's first on, one after another -- 2, or more for a
// move that has more roles than its pair (a double team's man, a turnbuckle dive's landings: the library's extension below).
struct OwnMove { int anims; uint32_t pair, then; bool has_then; uint16_t ground = 0; uint16_t n = 2; };
std::vector<OwnMove> all_moves;
constexpr int LOOK_FIELDS = 38;    // (the looks fields of a costume: "looks", above)
constexpr int HD_MAX = 14;     // Sixteen full-detail men in all (the descriptor keeps the number in 4 bits), less the two built in.
constexpr int HD_BONES = 28, HD_JOINTS = 15;

struct Wrestler {
    uint16_t id = 0, based_on = 0;     // the game's numbers: id given at load (scan); 0 until then
    std::string sid;                   // his string ID: the folder's name, or wrestler.json's "id" (2026-11-09)
    std::string based_on_sid;          // "based_on" as a string ID, until scan turns it into a number
    bool stock = false;                // "builtin": this folder is that built-in Superstar himself (2026-11-12, below)
    std::string replaces;              // (2026-11-14, revfix) "replaces": the built-in Superstar this folder wrestler takes the
                                       //  place of -- his string ID and game number; the built-in's own data is not used
    std::string name, short_name, nick;
    int height = -1, weight = -1, music = -1, video = -1;     // -1: keep what the wrestler he's based on has.
    int size = -1;                  // "size": his model's size, 0-255, or -1: his height's (2026-11-13, revbody).
    std::vector<uint8_t> keys;      // Empty, or 4 x KEY_BYTES.
    std::vector<uint8_t> looks;     // Empty, or 4 x LOOK_FIELDS: "looks", his costumes as numbers (2026-11-13, handlers1).
    bool no_nick = false;           // "nick": null -- no nickname, not the one he's based on has (2026-11-13, handlers1).
    std::vector<uint8_t> moves;     // Empty, or MOVES_BYTES.
    std::vector<uint8_t> slots;     // Empty, or SLOTS_BYTES: moves.json's list, as the mod reads it (2026-11-13).
    std::vector<uint8_t> edits;     // (2026-11-13, movelib2) moves.json's "records" and "extra": EDIT_BYTES each (read_edits).
    std::map<std::string, MoveRef> own;   // (2026-11-13, movelib) His folder's own moves by name ("own:<name>"), if any.
    std::vector<uint8_t> photo;     // Empty, or PHOTO_BYTES.
    int hd_index = -1;              // His place in hd_models below, or -1.
    // (2026-11-13, nmhandler) What a stock folder gives, besides the fields above: which record fields it has at all (the
    // others are the built-in's), each costume's own, and the photos of costumes with one of their own.
    bool has_name = false, has_short = false, has_nick = false;
    struct Costume { std::string name, short_name, nick; bool has_name = false, has_short = false, has_nick = false;
                     int height = -1, weight = -1, music = -1, video = -1; };
    std::vector<Costume> per_costume;          // Empty, or 4.
    std::vector<uint8_t> photo_c[4];           // [1..3]: photo_c<k>.bin, empty if none.
    int low_index = -1;             // (2026-11-12, handlers) His place in low_models below, or -1.
    std::filesystem::path folder;   // (2026-11-10: for arcade_media.cpp, which looks in it for a theme and a video in common formats)
    std::string source_game;        // (2026-11-19, roster) "source": {"game": ...} -- the game an importer took him from ("WCW/nWo
                                    //  Revenge"), or "": select_pages.json's {"game": ...} entries put a game's imports on its pages
    uint8_t cls = 0;                // New feature (Ultimate No Mercy) -- rostertags, 2026-11-21: "class" (CLASS_* below), 0 none given
    // New feature (Ultimate No Mercy) -- automerge, 2026-11-23: what the F2 option MERGE SAME WRESTLERS reads of him, as the
    // roster editor reads it (its app/families.py people()): his "name" in full (name above is cut to the game's record),
    // his "about" text, and his place in his source game's roster ("source": "ordinal" / "roster_index" / "code"; -1 none).
    std::string full_name, about;
    int64_t source_pos = -1;
};
// A full-detail model from a folder: where its files are, and hd.json as read.
struct HdModel {
    std::filesystem::path pack, pictures;
    uint32_t fields[9] = {};        // vtx_off, vtx_bytes, dl_off, dl_bytes, bl_off, ha_off, hand_r_n, hand_l_n, desc_off
    uint32_t desc_bytes = 0, idx_off = 0;
    uint32_t bones[HD_BONES][6] = {};
    struct { uint32_t bone, parent; float x, y, z; } joints[HD_JOINTS] = {};
};
std::vector<HdModel> hd_models;
// An entrance theme from a folder.
constexpr int THEME_MAX = 64;           // (Must match FOLDER_THEMES in the mod's arcade.h. 2026-11-12: only with WCW_MEDIA=0: see read_files)
constexpr int THEME_META_BYTES = 0xD0 + 8;
struct Theme { uint16_t id; std::filesystem::path sound; uint8_t meta[THEME_META_BYTES]; };
std::vector<Theme> themes;
// A Titantron video from a folder.
constexpr int VIDEO_MAX = 64;           // (Must match FOLDER_VIDEOS in the mod's arcade.h. 2026-11-12: only with WCW_MEDIA=0: see read_files)
constexpr uint32_t VIDEO_FRAME_BYTES = 16 + 512 + 4096;
struct Video { uint16_t id; std::filesystem::path file; uint32_t frames; };
std::vector<Video> videos;
std::vector<Wrestler> wrestlers;
// How many folder wrestlers the game can seat (final081, 2026-11-12: the guard until the ordinals are wide). The mod gives each
// folder wrestler one of the game's one-byte roster ordinals, and there are 256 - 102 - the 42 already built in = 112 of them
// (mod src/roster.c FOLDER_MAX, which this must match): it reads the first 112 of `wrestlers`, in this order, and never the
// rest. A wrestler past them has a game number the mod knows nothing of, and a match with him in it crashes (nolimits'
// nl_past_112, 2026-11-12: 0xC0000005 about 70 match frames in). So such a wrestler is never handed out: stories and the
// select pages can't name him (arcade_wrestlers_each_name / _each_id leave him out) and the log says so once. Every folder is
// still read and keeps his string ID, game number, theme and video; the root fix, wide IDs in the game's records (Stage 2's
// native memory), lifts this.
// (2026-11-13, seatsroot: the guard is gone -- every folder wrestler can be seated now, by the mod's staging seats, so all
//  of them are handed out: stories and select pages can name any of them. A match with any of them in it gets him a seat,
//  nolimits' nl_past_112. seatable() stays as the one place that would say otherwise.)
bool seatable(size_t) { return true; }
// His place in `wrestlers` by his game number (recomp_arcade_wrestler_find): -1 for none. Made at the end of scan.
std::vector<int32_t> folder_of_id;
// The built-in Superstars that have a folder of their own ("builtin"): his ID from the folder, and his game number. They
// aren't handed to the mod (he is already on its roster); stories and select_pages.json name him by this ID. (2026-11-12)
std::vector<std::pair<std::string, uint16_t>> stock_ids;
// ---- stock folders (2026-11-13, nmhandler) -------------------------------------------------------------------------------
// A stock folder that carries the built-in Superstar's data (the No Mercy handler writes all of it for the 64 stock men):
// his record fields, looks, moveset, photos. Handed to the mod by recomp_arcade_stock_count / _get / _photo, which puts each
// field over the record the game builds from its own tables (mod src/roster.c "stock folders"): the folder is where he comes
// from, the built-in tables the fallback for whatever it doesn't say (and for a stock man with no folder at all). The
// QA option qa_stock_twin has the mod compare the two and write each result here (recomp_arcade_stock_note).
std::vector<Wrestler> stock_folders;
// ---- removing a stock Superstar (2026-11-13, removal) -------------------------------------------------------------------
// The user, 2026-11-13: every wrestler is a folder, the game's own too, and deleting a stock man's folder REMOVES him from
// the game; his built-in data can be brought back from the ROM. A collection the No Mercy handler wrote has a
// stock_roster.json beside its folders ({"builtin": [the frozen IDs it holds]}): a man it lists whose folder is gone is
// removed. Without that file nobody is removed -- a half-written or older install never loses anyone by accident -- and a
// stock man no list names stays the built-in, as before.
// Removed is a matter of who can be picked, drawn and cast, never of his data: the game still builds his records from its
// own tables (mod src/roster.c), so whatever still names him -- a save, a story pack, an ally list -- finds his data
// instead of breaking (the mod's "removed" handling says what it does with each: mod src/roster.c, save.c, story.c,
// bigshow.c, variants.c). The user's decisions (2026-11-13): a title he holds is vacated at once; a preset or a story that
// needs him plays his stand-in; his costume variants go with him; and the ONE way back is the importer again
// (nm_import.py --only <id>), which writes his full folder -- the game itself never writes one (no restore.txt, no menu
// row: the user's R17, 2026-11-13). His folder back, he is on the roster again; a title vacated stays vacated.
struct Removed { std::string sid; uint16_t id = 0; std::filesystem::path collection; };
std::vector<Removed> removed;
std::map<uint16_t, std::string> sid_of_number;   // (every game number's string ID, for the log lines the mod asks for)
// (no2k15, 2026-11-23) The built-in Superstars taken out for good (mod gen/retired_ids.json, via stories\_roster.json's
// "@RETIRED:" keys: the two WWE 2K15 test slots): their old game numbers and string IDs. A save sidecar's title of one of them
// is vacated (sidecar_get, mod save.c), the game's rule for a removed wrestler's title. A folder of that string ID is his own.
std::set<uint16_t> retired_numbers;
std::set<std::string> retired_sids;
// New feature (Ultimate No Mercy) -- rostertags, 2026-11-21: by game number, his folder's "class" (0: none given), and the rivals
// rivals.json files say (rivals_read below): a pair (low number << 12 | high) -> 1 rivals, -1 "not_rivals" (a file takes a pair
// out: the game's own Rival 1-3 included).
std::vector<uint8_t> class_of_id;
std::map<uint32_t, int8_t> rival_pairs;
bool scanned = false;
// ---- families: one select box, an ordered list of variants (2026-11-14, families) ----------------------------------------
// The user (2026-11-14, "consolidation"): one box on the Superstar Select is a FAMILY -- an ordered list of VARIANTS, each
// one wrestler's variant (one of his looks, 1-4), any number of them, in any order, interleaved as the user likes (No Mercy
// Austin's variant 1, WrestleMania 2000 Austin's 1, No Mercy Austin's 2 ...). Each variant is still that wrestler, with his
// own model, name, moves, music and Titantron: the select hands the game (his game number, his look) as from a box of his
// own. Families are DATA beside the wrestlers, never inside a wrestler's folder, so making, reordering or splitting one
// never touches a wrestler: families.json in the wrestlers folder itself and/or in any collection folder (any folder that
// isn't a wrestler's). The format (the editor's FORMATS text; families_check.py checks a file by the same rules):
//   {"families": [ {"id": "undertaker",                       optional: the family's name, for the log and the editor
//                   "variants": [ {"wrestler": "undertaker", "variant": 1}, ... ]}, ... ]}
//   "wrestler": his string ID (a built-in's frozen ID or a folder's), or his game number ("0x0003"), or both, {"id":
//   "aki_man", "game_number": "0x0379"} (startup 2026-11-19: the number when it is that string ID's, else by the string ID
//   -- the user's fallback; "hidden" takes the same); "variant": 1-4, his
//   look, as the select's Type shows it for him alone. The box is where the FIRST variant's wrestler's box would be; the
//   other wrestlers' own boxes are off the pages. A wrestler is in one family at most (a later mention is left out, said in
//   the log); a variant is in it once.
// The files are read in this order: the wrestlers folder's own, then the collections' in case-insensitive path order (the
// user's D2). With NO families.json anywhere the game has the families it always had (default_families_json below, the same
// text as the default families.json); once any file has a "families" list, the files are all there is (an empty list:
// no families at all).
// NESTING (the user, 2026-11-14): a wrestler's folder moved INSIDE another wrestler's folder is in that one's family too --
// the outer one's four variants, then the nested one's, nested ones in case-insensitive path order, deeper ones in the
// same (outermost) family. The data wins: a families.json that names a nested wrestler puts him where it says (the log
// says so), and one it doesn't name goes at the end of the outer wrestler's family. Deleting the outer folder deletes the
// nested ones with it (they are inside it).
// PARTIAL (2026-11-14, the editor's round 4): a family that lists only some of a wrestler's variants: the first wrestler's
// others go at its end, any other wrestler's others keep a box of his own (rest_of_id); "unlisted": "off" on a family leaves
// them out (the default data's Revenge second looks: their first look only, as they always had).
// HIDDEN (the user, 2026-11-14): "hidden": [{"wrestler": ...}, {"wrestler": ..., "variant": 1-4}, ...] in any families.json
// (read whether or not it has "families"): a hidden wrestler has no box, a hidden variant no step of the Type arrows, and
// neither is ever drawn at random (Random, the Royal Rumble's field); whatever names him (a title, a story, a preset, a
// save) keeps him -- un-hiding brings him back as he was. Nothing in his folder changes.
// The mod (variants.c) asks for all of it by the exports below and decides, as the game goes, which variants are in play:
// a wrestler off the roster, removed or hidden is left out. A family keeps its place on the select (its SLOT: its first
// listed wrestler's box) when that wrestler is removed or hidden: the next one in play heads it there (the user,
// 2026-11-14: members in folders of their own are NOT removed with the head). big_show counts only while the Big Show
// option is on (else 0x0201 is Chris Benoit).
struct FamilyVariant { uint16_t id = 0; uint8_t variant = 0; };      // variant 0-3 (shown 1-4)
struct Family { std::string name; uint16_t slot = 0; bool unlisted_off = false; std::vector<FamilyVariant> variants;
                // (automerge, 2026-11-23) for MERGE SAME WRESTLERS: what the family LISTS, its hidden variants too (variants
                // above leaves them out) -- the list the roster editor reads; and whether it is one of the game's default
                // families (from the default data, or a file's family written exactly as a default one)
                std::vector<FamilyVariant> all; bool is_default = false; };   // (unlisted_off: "unlisted": "off")   // (slot: its first listed wrestler)
std::vector<Family> families;
std::vector<int32_t> family_of_id;     // by game number: his family (-1: none)
bool families_from_files = false;      // (false: the default data -- no families.json anywhere)
std::vector<int32_t> rest_of_id;     // by game number: the family of his variants another family leaves out (his own box), -1 none
std::vector<uint8_t> hidden_mask;      // by game number: his hidden variants, bit k for variant k + 1 (0xF: he is hidden)
// (automerge, 2026-11-23: what MERGE SAME WRESTLERS reads, kept by the scan) _roster.json's names for each built-in number;
// each built-in's own folder ("builtin": the editor's stock entries); by game number, a folder nested in another wrestler's
struct StockSeen { uint16_t number; std::string sid, builtin_sid; bool has_name; std::string name, source_game; };
std::map<uint16_t, std::vector<std::string>> roster_names_of;
std::vector<StockSeen> stock_seen;
std::vector<bool> nested_kid;
// Today's families as data (2026-11-14): what mod src/variants.c had as its table until now, in its order -- the No Mercy
// Plus Undertaker 1999 and the WCW/nWo Revenge Giant after the base's four. (2026-11-15, revfix -- the user: the Revenge ports
// get exactly Revenge's own variants: the six second looks' families -- a port's four, then his "_b" look -- are gone, and
// the second looks themselves are hidden below: DROPPED_LOOKS.)
const char* const default_families_json = R"FAM({
 "about": "The families the game has with no families.json anywhere (2026-11-14): each one select box, its variants in the order the Type arrows step through them.",
 "families": [
  {"id": "undertaker", "variants": [{"wrestler": "undertaker", "variant": 1}, {"wrestler": "undertaker", "variant": 2}, {"wrestler": "undertaker", "variant": 3}, {"wrestler": "undertaker", "variant": 4},
                                    {"wrestler": "undertaker_1999", "variant": 1}, {"wrestler": "undertaker_1999", "variant": 2}, {"wrestler": "undertaker_1999", "variant": 3}, {"wrestler": "undertaker_1999", "variant": 4}]},
  {"id": "big_show", "variants": [{"wrestler": "big_show", "variant": 1}, {"wrestler": "big_show", "variant": 2}, {"wrestler": "big_show", "variant": 3}, {"wrestler": "big_show", "variant": 4},
                                  {"wrestler": "the_giant", "variant": 1}, {"wrestler": "the_giant", "variant": 2}, {"wrestler": "the_giant", "variant": 3}, {"wrestler": "the_giant", "variant": 4}]}
 ]
})FAM";
// (2026-11-15, revfix) The Revenge ports' second looks the user dropped (gen/revbanks.py's six "_b" IDs: looks Revenge doesn't
// give the man). Their numbers stay frozen (mod gen/roster_ids.json) and their built-in records are still in the mod until the
// ports' tables go: so they are HIDDEN whatever any families.json says -- no box, never drawn; a save, preset, title or
// story that names one keeps him (as any hidden wrestler: families.json "hidden").
const char* const DROPPED_LOOKS[] = { "bret_hart_b", "randy_savage_b", "scott_steiner_b", "booker_t_b", "stevie_ray_b", "rey_mysterio_b" };
int64_t number_of(const json& v);     // (below: a JSON integer, or a string that is wholly a C number)
// (automerge, 2026-11-23) A family as a file writes it is one of the game's default families: the same JSON as one of
// default_families_json's (as the roster editor's is_default compares them: keys in any order, numbers by value).
bool family_is_default(const json& fj) {
    static const json defaults = json::parse(default_families_json)["families"];
    for (const json& d : defaults) if (fj == d) return true;
    return false;
}
// One families file's list into `families` (where: the file, for the log; "the default data" for the text above).
// who(the "wrestler" value) -> his game number (0: nobody); name_of(number) -> his string ID, for the log.
void families_take(const json& list, const std::string& where, const std::function<uint16_t(const json&)>& who,
                   const std::function<std::string(uint16_t)>& name_of) {
    for (size_t fi = 0; fi < list.size(); fi++) {
        const json& fj = list[fi];
        Family f;
        f.name = fj.is_object() && fj.contains("id") && fj["id"].is_string() ? fj["id"].get<std::string>() : "";
        f.unlisted_off = fj.is_object() && fj.contains("unlisted") && fj["unlisted"] == "off";
        const std::string label = where + ", family " + std::to_string(fi + 1) + (f.name.empty() ? "" : " (" + f.name + ")");
        if (!fj.is_object() || !fj.contains("variants") || !fj["variants"].is_array()) {
            fprintf(stderr, "[families] %s: needs a \"variants\" list: left out\n", label.c_str());
            continue;
        }
        f.is_default = where.rfind("the default data", 0) == 0 || family_is_default(fj);   // (automerge, 2026-11-23)
        std::vector<uint16_t> mine;      // (the wrestlers this family has: one may be in it more than once, in no other)
        int hidden_here = 0;
        std::vector<uint32_t> seen;      // (each variant once: id << 2 | variant)
        for (size_t k = 0; k < fj["variants"].size(); k++) {
            const json& v = fj["variants"][k];
            const std::string at = label + ", variant " + std::to_string(k + 1);
            if (!v.is_object() || !v.contains("wrestler") || !v.contains("variant")) {
                fprintf(stderr, "[families] %s: needs \"wrestler\" and \"variant\": left out\n", at.c_str());
                continue;
            }
            const uint16_t id = who(v["wrestler"]);
            const int64_t n = number_of(v["variant"]);
            if (!id) { fprintf(stderr, "[families] %s: no wrestler %s: left out\n", at.c_str(), v["wrestler"].dump().c_str()); continue; }
            if (n < 1 || n > 4) { fprintf(stderr, "[families] %s: \"variant\" must be 1-4 (a wrestler's four looks): left out\n", at.c_str()); continue; }
            if (family_of_id[id] >= 0) {
                fprintf(stderr, "[families] %s: %s is in family %d (%s) already: left out (a wrestler is in one family)\n", at.c_str(),
                        name_of(id).c_str(), family_of_id[id] + 1, families[size_t(family_of_id[id])].name.c_str());
                continue;
            }
            if (std::find(seen.begin(), seen.end(), uint32_t(id) << 2 | uint32_t(n - 1)) != seen.end()) {
                fprintf(stderr, "[families] %s: %s's variant %d is in it already: left out\n", at.c_str(), name_of(id).c_str(), int(n));
                continue;
            }
            seen.push_back(uint32_t(id) << 2 | uint32_t(n - 1));
            if (!f.slot) f.slot = id;
            if (std::find(mine.begin(), mine.end(), id) == mine.end()) mine.push_back(id);
            f.all.push_back({ id, uint8_t(n - 1) });                            // (automerge: the list as written, hidden too)
            if (hidden_mask[id] & (1u << (n - 1))) { hidden_here++; continue; }   // (hidden: never stepped to; the family's place kept)
            f.variants.push_back({ id, uint8_t(n - 1) });
        }
        if (!f.slot) { fprintf(stderr, "[families] %s: no variant left: left out\n", label.c_str()); continue; }
        if (f.name.empty()) f.name = name_of(f.slot);
        for (uint16_t id : mine) family_of_id[id] = int32_t(families.size());
        std::string list_text;
        for (const FamilyVariant& x : f.variants) list_text += (list_text.empty() ? "" : ", ") + name_of(x.id) + " " + std::to_string(x.variant + 1);
        fprintf(stderr, "[families] family %zu, %s (%s): %zu variants%s: %s\n", families.size() + 1, f.name.c_str(), where.c_str(),
                f.variants.size(), hidden_here ? (" (and " + std::to_string(hidden_here) + " hidden)").c_str() : "", list_text.c_str());
        families.push_back(std::move(f));
    }
}
// Every families.json (the wrestlers folder's first, then the collections' in case-insensitive path order) or, with none,
// the default data. sids: every string ID -> game number; used: the game numbers in use. (End of scan.)
void families_read(const std::filesystem::path& root, std::vector<std::filesystem::path> files,
                   const std::map<std::string, uint16_t>& sids, const std::vector<bool>& used, const std::map<uint16_t, std::string>& whose,
                   const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& nests);

/// A low-detail model of his own, the N64 kind (2026-11-12, handlers; file numbers on demand 2026-11-13, handlers1):
// model\model.json in his folder, as the source-game handlers write it (tools\handlers\handlers\aki\model.py has
// the format). Per costume a model descriptor (0x270 bytes, what the game's func_80018A3C makes from a costume's looks)
// with its file fields zeroed, and the files its parts name (meshes, pictures, palettes), listed once for all his costumes.
// The mod puts the descriptor in place of the one the game made (mod parts.c parts_model_post), and the game reads the
// files through recomp_arcade_wrestler_file.
constexpr int LOW_DESC_BYTES = 0x270;
struct LowCostume {
    std::vector<uint8_t> desc;                  // The descriptor as the folder has it: its file fields 0.
    std::vector<uint8_t> keep;                  // Per byte: 1 = the game's own byte stays (0x254, its state byte).
    std::vector<std::pair<int, int>> fields;    // (offset in the descriptor, index in "files"): where his files are named.
    std::vector<int> numbered;                  // The file indices that get a number while the game loads his model, in order (below).
};
struct LowModel {
    std::filesystem::path dir;                       // His folder's model\ folder.
    std::vector<std::filesystem::path> files;        // "files", in list order.
    std::vector<std::vector<uint8_t>> bytes;         // The files themselves, each read the first time the game asks for it.
    LowCostume costume[4];
};
std::vector<LowModel> low_models;


// His model's files, read from disk off the game's thread (2026-11-13, handlers1). The game asks for a model's files in the
// very call that builds it (build_model_from_looks -> load_mesh), so a file read then is read while the game waits: in a
// Royal Rumble of 120 folder models, three opens of a never-read file took over 12 seconds each (the exe's freeze reports,
// qa run 2026-11-13_080106_h1_cycle_rumble, the game thread in wrestler_low_file's open). So once the folders are read, a
// thread of its own reads every folder model's files into memory, seated wrestlers first, up to LOW_PREFETCH_BYTES; the game
// thread reads one itself only if the thread hasn't got to it yet (and the log says how long that took if it was slow). The
// memory figure only says how much is read AHEAD: a model past it is read when the game asks, as before.
constexpr size_t LOW_PREFETCH_BYTES = size_t(512) << 20;
std::mutex low_bytes_lock;          // (guards LowModel::bytes while the reading thread fills it)
std::string utf8(const std::filesystem::path& p);
std::vector<uint8_t> file_bytes(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void low_prefetch() {
    size_t total = 0;
    for (LowModel& m : low_models)
        for (size_t k = 0; k < m.files.size() && total < LOW_PREFETCH_BYTES; k++) {
            { std::lock_guard<std::mutex> g(low_bytes_lock); if (!m.bytes[k].empty()) continue; }
            std::vector<uint8_t> b = file_bytes(m.files[k]);
            total += b.size();
            std::lock_guard<std::mutex> g(low_bytes_lock);
            if (m.bytes[k].empty()) m.bytes[k] = std::move(b);
        }
    fprintf(stderr, "[wrestlers] folder models' files read ahead: %zu MB%s\n", total >> 20, total >= LOW_PREFETCH_BYTES ? " (the rest are read when the game asks)" : "");
    fflush(stderr);
}
// File k of model m, read now if the reading thread hasn't yet. Once filled, an entry never changes.
const std::vector<uint8_t>& low_file_bytes(LowModel& m, size_t k) {
    { std::lock_guard<std::mutex> g(low_bytes_lock); if (!m.bytes[k].empty()) return m.bytes[k]; }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<uint8_t> b = file_bytes(m.files[k]);
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    if (ms >= 250) { fprintf(stderr, "[wrestlers] reading a folder model's file took %lld ms (%s)\n", ms, utf8(m.files[k].filename()).c_str()); fflush(stderr); }
    std::lock_guard<std::mutex> g(low_bytes_lock);
    if (m.bytes[k].empty()) m.bytes[k] = std::move(b);
    return m.bytes[k];
}

// Reads a folder's model\model.json. Returns an empty string, or what's wrong with it. Only the list and the descriptors
// are read here; the files by low_prefetch's thread, or when the game asks for them.
std::string read_low(const std::filesystem::path& folder, LowModel& m) {
    m.dir = folder / "model";
    std::ifstream file(m.dir / "model.json");
    if (!file.good()) return "model.json can't be opened";
    const json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("files") || !j["files"].is_array() || !j.contains("costumes") || !j["costumes"].is_array())
        return "model.json needs \"files\" and \"costumes\"";
    if (j["costumes"].size() != 4) return "model.json needs 4 costumes";
    for (const json& f : j["files"]) {
        if (!f.is_string()) return "\"files\" must be file names";
        const std::filesystem::path p = m.dir / f.get<std::string>();
        if (!std::filesystem::exists(p)) return "model.json names a file that isn't there: " + f.get<std::string>();
        m.files.push_back(p);
    }
    m.bytes.resize(m.files.size());
    for (int c = 0; c < 4; c++) {
        const json& cj = j["costumes"][size_t(c)];
        LowCostume& lc = m.costume[c];
        if (!cj.is_object() || !cj.contains("descriptor") || !cj["descriptor"].is_string() || !cj.contains("fields") || !cj["fields"].is_array())
            return "each costume needs a \"descriptor\" and \"fields\"";
        std::ifstream d(m.dir / cj["descriptor"].get<std::string>(), std::ios::binary);
        lc.desc.assign(std::istreambuf_iterator<char>(d), std::istreambuf_iterator<char>());
        if (lc.desc.size() != size_t(LOW_DESC_BYTES)) return "a costume's descriptor isn't 0x270 bytes";
        std::vector<bool> named(m.files.size() + 1, false);
        for (const json& fld : cj["fields"]) {
            if (!fld.is_array() || fld.size() != 2 || !fld[0].is_number_integer() || !fld[1].is_number_integer()) return "a file field must be [offset, file index]";
            const int at = fld[0].get<int>(), k = fld[1].get<int>();
            if (at < 0 || at + 2 > LOW_DESC_BYTES || k < 0 || size_t(k) >= m.files.size()) return "a file field is out of range";
            lc.fields.push_back({ at, k });
            named[size_t(k)] = true;
            if (size_t(k) + 1 < m.files.size()) named[size_t(k) + 1] = true;     // (its picture, if it's a palette)
        }
        for (size_t k = 0; k < m.files.size(); k++) if (named[k]) lc.numbered.push_back(int(k));
        lc.keep.assign(LOW_DESC_BYTES, 0);
        if (cj.contains("keep") && cj["keep"].is_array())
            for (const json& k : cj["keep"]) if (k.is_number_integer() && k.get<int>() >= 0 && k.get<int>() < LOW_DESC_BYTES) lc.keep[size_t(k.get<int>())] = 1;
    }
    return "";
}

// ---- game file numbers, held only while a model loads (2026-11-13, filepool; on handlers1's numbers on demand) ---------
// The game names a model's files by 16-bit numbers in its descriptor (0x270 bytes in a model slot: 0x8054B8C0 + slot x
// 0x270). Where the game keeps such a number once it is written there (traced through the game's code, 2026-11-13):
//   - The descriptor: read while the slot LOADS, never after. Its meshes are read in the very call that builds it
//     (build_model_from_looks -> the skeleton, mod skel.c), its pictures by func_80016464's two steps (mod skel.c
//     skel_textures, phases 0 and 1), which a match's build_slot_model runs at once and the menus' loader
//     (update_slot_loading, states 2 and 3) and the scenes' (step_actor_model_load) on the frames after the build. Each file
//     is read into memory of the part's own (load_mesh: into the slot's pool; load_part_picture_files: the part's +0x20 /
//     +0x24) and no part keeps the number. After the load, only the descriptor's flag bytes (+0x254 / +0x255) and the mod's
//     magic and flags words are read (blood, fades, sounds, skel_visible, hands, hd.c): no file field.
//   - The shared file list (acquire_shared_file, func_8000BD18): a picture loaded once for all the parts that name it, kept
//     by its number until the last of them lets go (release_shared_file). Only fixed cartridge numbers go there (a slot's
//     extra parts' 0x10EC / 0x10ED / 0x10F4, load_model_parts' tables), never a folder model's. Checked anyway, below.
//   - The menus' sprite caches (load_wanted_pictures) keep pictures by number too, but only menu pictures (photos, labels),
//     never a model's. No DMA request is left waiting: the mod serves its files inside the read call (files.c). The
//     renderer's texture cache and texture packs go by a picture's contents, not its number.
// So a costume gets numbers when the game builds its model into a slot, and that load is over at func_80016464's second
// step (recomp_arcade_wrestler_model_loaded) or when the slot is built again. Between loads a folder model needs no number
// at all: the numbers only have to cover the models LOADING at the same time -- a match loads one at a time, the menus up to
// four, the scenes one a frame -- not the models on screen, and not how many wrestlers, costumes, files or texture mods
// exist. A finished load's numbers are kept as they are until they're needed (given out again last: the cursor below), so
// that a read nobody expected would still get the right file, and is written to the log ("LATE READ").
//   The pool: every number below 0x8000 that no other file has -- the mod says which (files.c files_pool_send, from its own
// file map), about 7,600 -- not a range set aside. A costume takes one for each file its descriptor's fields name and one
// for the file after each (a palette's picture is the palette's number + 1): 624 at the very most (0x270 bytes of 16-bit
// fields, two numbers each), Revenge's largest 76. So every load the game can make fits many times over.
//   INTERIM, over the game's own record: the descriptor's file fields are 16-bit game file numbers (read signed: below
// 0x8000) and its 0x270 bytes fix how many there are. The root fix is Stage 2's native records (scratch\agents\stage2\
// PLAN.md): the model descriptor as a native record whose file fields are wide handles into a file table the exe owns, read
// by native code (skel.c's skeleton and pictures, build_model_from_looks, the face decal), with no game file number at all.
constexpr uint32_t GAME_FILE_END = 0x8000;            // (a file number is read as a signed 16-bit value)
struct Lease {                         // A costume's numbers for one load.
    int model = -1, costume = -1;      // Whose (low_models), which costume; model -1: a free entry.
    int slot = -1;                     // The model slot loading it; -1 once that load is over, or if it wasn't a slot's.
    bool ended = false;                // Its load is over: its numbers can go to another as soon as the shared list lets go.
    uint64_t ended_at = 0;             // (when, in loads ended so far: the oldest is taken back first)
    std::vector<uint32_t> number;      // For each of the costume's `numbered` files, its game file number.
    int anims = -1;                    // (2026-11-13, movelib) Or a set of moves' animations' (anim_sets): all its files.
    bool used() const { return model >= 0 || anims >= 0; }
};
std::vector<Lease> leases;
struct Owner { int lease = -1, file = -1; };      // A number's lease and file index; lease -1: free.
std::vector<Owner> owners(GAME_FILE_END);         // By number (only the pool's are ever given).
std::vector<uint32_t> pool;                       // The numbers that can be given, in order.
uint32_t pool_cursor = 0;                         // Where the next search starts, in `pool`: numbers come back round last.
bool pool_from_mod = false;
// Where the game keeps what holds a number (the mod says, recomp_arcade_wrestler_model_where): the first model slot's
// descriptor, how many slots there are, and the word holding the shared file list's first node.
uint32_t where_slots = 0, where_slot_n = 0, where_shared = 0;
// For the log: numbers given out, loads taken back, the most in use at once, loads refused, and reads of a number after
// its load was over (LATE: nothing of the game should do that) or of a number no load has (STALE).
uint64_t low_given = 0, low_recycled = 0, low_ended = 0, low_refused = 0, low_late = 0, low_stale = 0;
uint32_t low_peak = 0;
constexpr uint64_t LATE_SAID = ~0ull >> 1;          // (a lease's ended_at once a late read of it is said)

// The pool as the given [first, end) ranges, below 0x8000 and above 0. QA: WCW_QA_FILE_POOL=n keeps only n of them, one
// after another (the first run of the pool that long), for the squeeze test: a pool of one costume's size still gives
// every model his own.
void pool_set(const std::vector<std::pair<uint32_t, uint32_t>>& ranges) {
    std::vector<uint8_t> in(GAME_FILE_END, 0);
    for (const auto& [a, b] : ranges)
        for (uint32_t n = std::max<uint32_t>(a, 1); n < std::min(b, GAME_FILE_END); n++) in[n] = 1;
    pool.clear();
    for (uint32_t n = 1; n < GAME_FILE_END; n++) if (in[n]) pool.push_back(n);
    const char* qa = getenv("WCW_QA_FILE_POOL");
    const size_t cut = (qa && qa[0] && atoi(qa) > 0) ? size_t(atoi(qa)) : 0;
    if (cut && cut < pool.size()) {
        size_t from = 0;
        for (size_t i = 0, run = 0; i < pool.size(); i++) {
            run = (i && pool[i] == pool[i - 1] + 1) ? run + 1 : 1;
            if (run == cut) { from = i + 1 - cut; break; }
        }
        pool = std::vector<uint32_t>(pool.begin() + std::ptrdiff_t(from), pool.begin() + std::ptrdiff_t(from + cut));
    }
    pool_cursor = 0;
}
void pool_default() {     // (until the mod says: handlers1's range, as a mod without files_pool_send had it)
    if (pool.empty() && !pool_from_mod) pool_set({ { 0x6400, 0x7000 } });
}

// Whether the shared file list names one of lease li's numbers (above: a picture loaded once for several parts).
bool shared_holds(uint8_t* rdram, int li) {
    if (!where_shared) return false;
    uint32_t node = uint32_t(MEM_W(0, int32_t(where_shared)));
    for (int guard = 0; node && guard < 0x10000; guard++) {      // (nodes of 0xC: +0 the next, +8 the file's number)
        if ((node & 0xFF000000u) != 0x80000000u) break;
        const uint32_t n = uint16_t(MEM_HU(8, int32_t(node)));
        if (n < GAME_FILE_END && owners[n].lease == li) return true;
        node = uint32_t(MEM_W(0, int32_t(node)));
    }
    return false;
}
// For a load that wasn't a model slot's (none known: kept for safety) -- held while any slot's descriptor names one of its
// numbers or the shared list does, as handlers1 had it for all of them.
bool lease_held(uint8_t* rdram, int li) {
    for (uint32_t s = 0; s < where_slot_n; s++)
        for (int at = 0; at < LOW_DESC_BYTES; at += 2) {
            const uint32_t n = uint16_t(MEM_HU(at, int32_t(where_slots + s * LOW_DESC_BYTES)));
            if (n < GAME_FILE_END && owners[n].lease == li) return true;
        }
    return shared_holds(rdram, li);
}

void lease_free(int li) {
    for (uint32_t n : leases[size_t(li)].number) owners[n] = {};
    leases[size_t(li)] = Lease{};
    low_recycled++;
}
// Lease li's load is over (its slot's pictures are in, or the slot is being built again).
void lease_end(int li) {
    Lease& l = leases[size_t(li)];
    if (l.ended) return;
    l.slot = -1;
    l.ended = true;
    l.ended_at = ++low_ended;
}
// The model slot whose descriptor is at `desc`, or -1.
int slot_of(uint32_t desc) {
    if (!where_slot_n || desc < where_slots || desc >= where_slots + where_slot_n * LOW_DESC_BYTES) return -1;
    return (desc - where_slots) % LOW_DESC_BYTES ? -1 : int((desc - where_slots) / LOW_DESC_BYTES);
}
// Numbers in use now (given and not yet taken back), and how many of those belong to loads still going on.
uint32_t numbers_in_use(uint32_t* loading = nullptr) {
    uint32_t n = 0, live = 0;
    for (const Lease& l : leases) if (l.used()) { n += uint32_t(l.number.size()); if (l.model >= 0 && !l.ended) live += uint32_t(l.number.size()); }
    if (loading) *loading = live;
    return n;
}

// Takes back one lease so its numbers can be given again: the oldest load that's over (and not named by the shared list),
// else a load that wasn't a slot's once nothing names it. Returns false if there's none.
bool lease_take_back(uint8_t* rdram) {
    int best = -1;
    for (size_t li = 0; li < leases.size(); li++) {
        const Lease& l = leases[li];
        if (l.model < 0 || !l.ended || shared_holds(rdram, int(li))) continue;
        if (best < 0 || l.ended_at < leases[size_t(best)].ended_at) best = int(li);
    }
    if (best < 0)
        for (size_t li = 0; li < leases.size(); li++)
            if (leases[li].model >= 0 && !leases[li].ended && leases[li].slot < 0 && !lease_held(rdram, int(li))) { best = int(li); break; }
    if (best < 0) return false;
    lease_free(best);
    return true;
}

// Gives costume c of low model m, being built into model slot `slot` (-1: not a slot), numbers of its own for this load.
// The slot's last load is over now (its descriptor is being written again). Returns the lease, or -1 if even after taking
// back every finished load there's no room -- only if more than the whole pool were loading at once.
uint32_t anims_collect(uint8_t* rdram);
int lease_for(uint8_t* rdram, int m, int c, int slot) {
    pool_default();
    if (slot >= 0) for (size_t i = 0; i < leases.size(); i++) if (leases[i].model >= 0 && leases[i].slot == slot) lease_end(int(i));
    const std::vector<int>& want = low_models[size_t(m)].costume[c].numbered;
    // Files next to each other in "files" take numbers next to each other (a palette's picture is its number + 1), so runs
    // of them are given as runs. Each search starts at the cursor, going round once. `whole`: the costume's numbers all in
    // one run, so that a load holds one piece of the pool.
    const auto try_give = [&](std::vector<uint32_t>& got, bool whole) -> bool {
        got.clear();
        const size_t N = pool.size();
        if (!N) return false;
        std::vector<uint32_t> taken;
        size_t cursor = pool_cursor % N;
        for (size_t i = 0; i < want.size();) {
            size_t j = i + 1;
            while (j < want.size() && (whole || want[j] == want[j - 1] + 1)) j++;
            const size_t len = j - i;
            size_t run = 0, at = 0;
            for (size_t step = 0; step < N + len; step++) {
                at = (cursor + step) % N;
                const uint32_t n = pool[at];
                if (owners[n].lease >= 0 || std::find(taken.begin(), taken.end(), n) != taken.end()) { run = 0; continue; }
                // (a run is of numbers one after another, and doesn't go round the end of the pool)
                run = (run && at > 0 && pool[at] == pool[at - 1] + 1) ? run + 1 : 1;
                if (run == len) break;
            }
            if (run < len) return false;
            for (size_t k = at + 1 - len; k <= at; k++) { taken.push_back(pool[k]); got.push_back(pool[k]); }
            cursor = (at + 1) % N;
            i = j;
        }
        pool_cursor = uint32_t(cursor);
        return true;
    };
    // From the cursor, then from the pool's start (a run the cursor's place cut in two).
    const auto give = [&](std::vector<uint32_t>& got, bool whole) -> bool {
        if (try_give(got, whole)) return true;
        if (!pool_cursor) return false;
        pool_cursor = 0;
        return try_give(got, whole);
    };
    // In one run, taking back finished loads (the oldest first) until one fits; run by run only once there's none left to
    // take back -- so no load is ever cut in pieces while a whole run could be had, and a pool of (2L - 1) x the largest
    // load always has room for L loads at once.
    std::vector<uint32_t> got;
    for (int collected = 0;;) {
        if (give(got, true)) break;
        if (lease_take_back(rdram)) continue;
        if (!collected++ && anims_collect(rdram)) continue;     // (2026-11-13, movelib2: moves' animations nothing plays now)
        if (give(got, false)) break;
        low_refused++;
        return -1;
    }
    size_t li = 0;
    while (li < leases.size() && leases[li].used()) li++;
    if (li == leases.size()) leases.push_back({});
    leases[li] = Lease{ m, c, slot, false, 0, got };
    for (size_t k = 0; k < got.size(); k++) owners[got[k]] = { int(li), want[k] };
    low_given += got.size();
    low_peak = std::max(low_peak, numbers_in_use());
    return int(li);
}
// ---- moves' animations: game file numbers from the same pool, while a match plays them (2026-11-13, movelib; the library
// and the pool 2026-11-13, movelib2) ------------------------------------------------------------------------------------
// A move with animations of its own -- a built-in library move (movelib_revenge.bin, below) or a folder's own (moves\
// movex.json) -- gets game file numbers the first time a match unpacks a moveset with it in it (the mod's movex.c asks,
// recomp_arcade_move_own), from the same pool and owners[] as the folder models' (one allocator): its pair two numbers in a
// row (the game takes the defender's as the attacker's + 1), and a hold's block eight (the loop's pair, then its endings at
// + 2 / + 4 / + 6). A set of animations (AnimSet: one library move's, or one folder's own moves', all at once) holds its
// numbers while anything names one: a wrestler slot's entry (the mod's per-slot animation table, mx_file) or one of the
// game's animation players (the file each plays, +4). Unlike a model's, an animation's number is read all through the match
// (each piece as it streams, anim_stream; its header and record by number, movex.c anim_hdr), so it is never taken back
// while named. How many library moves there are costs nothing: only those in the match have numbers.
//   Each number also gets a "ROM" place of its own for the game's streaming (the mod's anim_stream reads it from here): from
// ANIM_OWN_ROM on, each set's files one after another, the next set after the last -- round again at the end, by which time
// those of the first are long let go.
constexpr uint32_t ANIM_OWN_ROM = 0x60000000, ANIM_OWN_ROM_SIZE = 0x1E000000;   // (the mod's arcade.h: the same)
uint32_t movex_files = 0, movex_slots = 0, movex_entries = 0, movex_players = 0, movex_player_n = 0, movex_stride = 0;
uint32_t anim_rom_next = 0;
std::map<uint32_t, uint32_t> anim_rom_of;      // (game file number -> its "ROM" place, while its lease lasts)
std::map<uint32_t, uint32_t> anim_at_rom;      // (its place -> the number: anim_read)
uint64_t anim_given = 0, anim_sets_given = 0, anim_sets_back = 0, anim_refused = 0;   // (for the log)
const std::vector<uint8_t>& own_anim_bytes(OwnAnim& a) {
    if (a.bytes.empty() && !a.path.empty()) a.bytes = file_bytes(a.path);
    return a.bytes;
}
// Every animation set no slot's entries and no animation player names now is let go, in one look at both (a set's lease is
// held while any of its numbers is named). Returns how many sets were let go.
uint32_t anims_collect(uint8_t* rdram) {
    std::vector<uint8_t> held(leases.size(), 0);
    const auto mark = [&](uint32_t n) {
        if (n < GAME_FILE_END && owners[n].lease >= 0 && leases[size_t(owners[n].lease)].anims >= 0) held[size_t(owners[n].lease)] = 1;
    };
    for (uint32_t k = 0; k < movex_slots * movex_entries; k++) mark(uint16_t(MEM_HU(int32_t(2 * k), int32_t(movex_files))));
    for (uint32_t k = 0; k < movex_player_n; k++) mark(uint16_t(MEM_HU(4, int32_t(movex_players + k * movex_stride))));
    uint32_t freed = 0;
    for (size_t li = 0; li < leases.size(); li++) {
        if (leases[li].anims < 0 || held[li]) continue;
        for (uint32_t n : leases[li].number) {
            owners[n] = {};
            if (const auto it = anim_rom_of.find(n); it != anim_rom_of.end()) { anim_at_rom.erase(it->second); anim_rom_of.erase(it); }
        }
        leases[li] = Lease{};
        freed++;
        anim_sets_back++;
    }
    return freed;
}
// `len` numbers of the pool one after another that are free and not in `taken`, from the cursor on (round once). Returns
// the first, or 0.
uint32_t pool_run(size_t len, const std::vector<uint32_t>& taken) {
    const size_t N = pool.size();
    if (!N || !len) return 0;
    size_t run = 0;
    for (size_t step = 0; step < N; step++) {
        const size_t at = (pool_cursor + step) % N;
        const uint32_t n = pool[at];
        if (owners[n].lease >= 0 || std::find(taken.begin(), taken.end(), n) != taken.end()) { run = 0; continue; }
        run = (run && at > 0 && pool[at] == pool[at - 1] + 1) ? run + 1 : 1;    // (not round the pool's end)
        if (run == len) { pool_cursor = uint32_t((at + 1) % N); return pool[at + 1 - len]; }
    }
    return 0;
}
// Animation set `fa`'s numbers: those it has, or new ones. Returns the lease, or -1 (none free even after letting go every
// set nothing names and taking back every finished model load).
int anims_lease_for(uint8_t* rdram, int fa) {
    pool_default();
    for (size_t i = 0; i < leases.size(); i++) if (leases[i].anims == fa) return int(i);
    AnimSet& f = anim_sets[size_t(fa)];
    const auto try_give = [&](std::vector<uint32_t>& got) -> bool {
        got.assign(f.files.size(), 0);
        std::vector<uint32_t> taken;
        for (const auto& [first, len] : f.runs) {
            uint32_t at = pool_run(len, taken);
            if (!at && pool_cursor) { pool_cursor = 0; at = pool_run(len, taken); }
            if (!at) return false;
            for (uint32_t k = 0; k < len; k++) { taken.push_back(at + k); got[first + k] = at + k; }
        }
        return true;
    };
    std::vector<uint32_t> got;
    for (int round = 0; !try_give(got); round++) {
        if (round == 0 && anims_collect(rdram)) continue;
        if (lease_take_back(rdram)) continue;
        anim_refused++;
        return -1;
    }
    size_t li = 0;
    while (li < leases.size() && leases[li].used()) li++;
    if (li == leases.size()) leases.push_back({});
    leases[li] = Lease{};
    leases[li].anims = fa; leases[li].number = got;
    for (size_t k = 0; k < got.size(); k++) {
        owners[got[k]] = { int(li), int(k) };
        // (its "ROM" place: a piece of the place's range no number held now has -- round again past the end)
        const uint32_t size = uint32_t(own_anim_bytes(f.files[k]).size());
        if (anim_rom_next + size > ANIM_OWN_ROM_SIZE) anim_rom_next = 0;
        anim_rom_of[got[k]] = ANIM_OWN_ROM + anim_rom_next;
        anim_at_rom[ANIM_OWN_ROM + anim_rom_next] = got[k];
        anim_rom_next += (size + 0xF) & ~0xFu;
    }
    anim_given += got.size();
    anim_sets_given++;
    low_peak = std::max(low_peak, numbers_in_use());
    return int(li);
}
// The animation game file number n is now, or nullptr.
OwnAnim* own_anim_of(uint32_t n) {
    if (n >= GAME_FILE_END) return nullptr;
    const Owner& o = owners[n];
    if (o.lease < 0 || leases[size_t(o.lease)].anims < 0) return nullptr;
    return &anim_sets[size_t(leases[size_t(o.lease)].anims)].files[size_t(o.file)];
}

// ---- the built-in move libraries (2026-11-13, movelib2) ---------------------------------------------------------------
// Each AKI source game's moves, made from the user's ROM by its handler (tools\handlers\revlib.py: Revenge's
// whole move list), as a content file beside wmport.bin: content\movelib_revenge.bin (QA: WCW_CONTENT_DIR). Read once,
// with the folders; its moves are IDs 1 .. lib_n (move_library), its animations given numbers only while a match plays
// them (above). The mod's src/move_lib.h was made with the same library: it checks the count and the stamp
// (recomp_arcade_move_lib_n / _stamp) and plays no library move if they differ. The file's layout: revlib.py.
// (2026-11-13, wm2klib) More libraries after it, in a fixed order: content\movelib_wm2000.bin (tools\handlers\wmlib.py,
// the same layout) -- its moves numbered after Revenge's, lib_n + 1 .., and the folders' own after all of them. The mod
// numbers only Revenge's (the built-in ports' lists, the hand-made move): lib_n and lib_stamp stay that library's, so a
// library after it changes nothing the mod knows. One that isn't there, or isn't a library, is said in the log and left
// out (its moves' IDs are then unknown: a slot naming one keeps his packed moveset's move).
uint32_t lib_n = 0, lib_stamp = 0;
uint32_t lib_all_n = 0;                        // (2026-11-13, wm2klib: every built-in library's moves, Revenge's and after)
std::string lib_wrong;                         // (why there is no library, for the log)
std::filesystem::path content_dir() {
    const char* from_environment = getenv("WCW_CONTENT_DIR");
    if (from_environment && from_environment[0]) return std::filesystem::path(from_environment);
    return arcade::program_folder() / "content";
}
// One library file's moves into move_library, all_moves and anim_sets, numbered from all_moves.size() + 1 (2026-11-13,
// wm2klib: what move_library_fill did for Revenge's, for any of them). Returns how many moves it had (0: none -- `wrong`
// says why), and its stamp in *stamp.
uint32_t move_library_read(const std::filesystem::path& p, uint32_t* stamp, std::string& wrong) {
    const std::vector<uint8_t> b = file_bytes(p);
    const auto u32 = [&](size_t at) -> uint32_t { uint32_t v = 0; if (at + 4 <= b.size()) memcpy(&v, b.data() + at, 4); return v; };
    const auto u16 = [&](size_t at) -> uint16_t { uint16_t v = 0; if (at + 2 <= b.size()) memcpy(&v, b.data() + at, 2); return v; };
    if (b.size() < 32 || memcmp(b.data(), "NMMOVLIB", 8) || u32(8) != 1) {
        wrong = b.empty() ? "isn't there" : "isn't a move library of this kind";
        return 0;
    }
    const uint32_t n = u32(12), na = u32(16), sbytes = u32(24), dbytes = u32(28);
    const size_t mv = 32, an = mv + 16ull * n, st = an + 44ull * na, da = st + sbytes;
    if (da + dbytes > b.size() || n > 1000000 || na > 4000000) { wrong = "is cut short"; return 0; }
    const auto anim = [&](uint32_t k) {
        OwnAnim a;
        const uint32_t off = u32(an + 44ull * k), size = u32(an + 44ull * k + 4);
        if (da + uint64_t(off) + size <= b.size()) a.bytes.assign(b.begin() + std::ptrdiff_t(da + off), b.begin() + std::ptrdiff_t(da + off + size));
        a.record.assign(b.begin() + std::ptrdiff_t(an + 44ull * k + 8), b.begin() + std::ptrdiff_t(an + 44ull * k + 44));
        return a;
    };
    const uint32_t first = uint32_t(all_moves.size());
    // (2026-11-20, revmoves) The extension after the data (a library made before it has none, and an exe before it reads
    // past nothing it doesn't know: the moves, IDs and stamp are the same): "NMLIBX01", a count, then { row, first
    // animation, how many } -- a move's animations after its pair, its roles 2 .. (the man in a double team, a turnbuckle
    // dive's landings and miss), given numbers in a row with the pair.
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> more;
    {
        const size_t x = da + dbytes;
        if (x + 12 <= b.size() && !memcmp(b.data() + x, "NMLIBX01", 8)) {
            const uint32_t k = u32(x + 8);
            if (x + 12 + 12ull * k <= b.size())
                for (uint32_t j = 0; j < k; j++) {
                    const size_t at = x + 12 + 12ull * j;
                    const uint32_t row = u32(at), a0 = u32(at + 4), c = u32(at + 8);
                    if (row < n && c && c < 16 && uint64_t(a0) + c <= na) more[row] = { a0, c };
                }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        const size_t at = mv + 16ull * i;
        const uint32_t name = u32(at), pair = u32(at + 8), block = u32(at + 12);
        const uint16_t base = u16(at + 4), ground = u16(at + 6);
        std::string id;
        for (size_t c = st + name; c < da && b[c]; c++) id += char(b[c]);
        OwnMove om{ -1, 0, 0, false, ground };
        if (pair != 0xFFFFFFFFu && pair + 1 < na) {
            AnimSet s;
            s.shown = id;
            s.files.push_back(anim(pair)); s.files.push_back(anim(pair + 1));
            if (const auto m = more.find(i); m != more.end()) {      // (2026-11-20, revmoves: its roles after the pair)
                for (uint32_t k = 0; k < m->second.second; k++) s.files.push_back(anim(m->second.first + k));
                om.n = uint16_t(2 + m->second.second);
            }
            s.runs.push_back({ 0, om.n });
            if (block != 0xFFFFFFFFu && block + 7 < na) {
                for (uint32_t k = 0; k < 8; k++) s.files.push_back(anim(block + k));
                s.runs.push_back({ om.n, 8 });
                om.then = om.n; om.has_then = true;
            }
            om.anims = int(anim_sets.size());
            anim_sets.push_back(std::move(s));
        }
        move_library[id] = { first + i + 1, base };
        all_moves.push_back(om);
    }
    *stamp = u32(20);
    fprintf(stderr, "[moves] %s: %u library moves (numbers %u .. %u), %u animations (game file numbers given while a match plays them)\n",
            arcade::shown(p).c_str(), n, first + 1, first + n, na);
    fflush(stderr);
    return n;
}
// (romsets 2026-11-24) Without WCW/nWo Revenge there is no content\movelib_revenge.bin -- and so far then no library at all:
// the mod plays library moves only when the exe's first library has its move_lib.h's count and stamp, and the libraries
// after it are numbered past it. So a player with WrestleMania 2000 and no Revenge had WrestleMania 2000's 56 wrestlers
// without their own moves (each slot naming one kept his packed moveset's move: Al Snow 45 of them). Now the first library's
// NUMBERS are kept from our own data, which the release ships: firstrun\romimport\handlers\revenge\library_index.json (each
// move's ID, its No Mercy move, its ground pick) and library_head.json (the count and stamp the mod was built with) -- none
// of its moves has an animation (each plays as its No Mercy move: move_own's "made by hand" case), the libraries after it
// are numbered and played as with Revenge. Returns how many moves (0: our data isn't there or doesn't fit -- `wrong` says).
uint32_t move_library_numbers_only(uint32_t* stamp, std::string& wrong) {
    const std::filesystem::path dir = arcade::program_folder() / "firstrun" / "romimport" / "handlers" / "revenge";
    const auto read_json = [](const std::filesystem::path& p) { std::ifstream f(p); return f.good() ? json::parse(f, nullptr, false) : json(); };
    const json index = read_json(dir / "library_index.json"), head = read_json(dir / "library_head.json");
    if (!index.is_array() || !head.is_object() || !head.contains("moves") || !head.contains("stamp")) {
        wrong += "; the library's numbers (firstrun\\romimport\\handlers\\revenge) aren't there either";
        return 0;
    }
    const int64_t n = number_of(head["moves"]), st = number_of(head["stamp"]);
    if (n <= 0 || st < 0 || st > 0xFFFFFFFFll || size_t(n) != index.size()) { wrong += "; library_index.json isn't the library library_head.json counts"; return 0; }
    for (const json& x : index)
        if (!x.is_object() || !x.contains("id") || !x["id"].is_string() || !x.contains("base") || number_of(x["base"]) < 0) {
            wrong += "; library_index.json has a row that isn't a move";
            return 0;
        }
    const uint32_t first = uint32_t(all_moves.size());
    for (size_t i = 0; i < index.size(); i++) {
        const json& x = index[i];
        const int64_t ground = x.contains("ground") ? number_of(x["ground"]) : 0;
        move_library[x["id"].get<std::string>()] = { first + uint32_t(i) + 1, uint16_t(number_of(x["base"])) };
        all_moves.push_back(OwnMove{ -1, 0, 0, false, uint16_t(ground < 0 ? 0 : ground) });
    }
    *stamp = uint32_t(st);
    fprintf(stderr, "[moves] content\\movelib_revenge.bin %s: its %u moves' numbers kept from %s (stamp %08X) -- none of them played; the libraries after it as with it\n",
            wrong.c_str(), unsigned(n), arcade::shown(dir / "library_index.json").c_str(), unsigned(st));
    fflush(stderr);
    return uint32_t(n);
}
void move_library_fill() {
    static bool done = false;
    if (done) return;
    done = true;
    // (Revenge's first: the library the mod's move_lib.h numbers -- without it none at all, as the mod would play none)
    const std::filesystem::path p = content_dir() / "movelib_revenge.bin";
    lib_n = move_library_read(p, &lib_stamp, lib_wrong);
    if (!lib_n) lib_n = move_library_numbers_only(&lib_stamp, lib_wrong);   // (romsets: its numbers kept without Revenge)
    if (!lib_n) {
        fprintf(stderr, "[moves] %s %s: no built-in library moves (the built-in ports play No Mercy's stand-ins)\n", arcade::shown(p).c_str(), lib_wrong.c_str());
        fflush(stderr);
        return;
    }
    // (2026-11-15, revfix -- proposal (d), the user's yes for a later version: movelib_revenge_strikes.bin, Revenge's one-
    //  animation moves -- the strikes, which the pair library leaves out -- each written as a pair whose second file is the
    //  first again (a strike plays no other man's file: the game takes his reaction from the hit), so this reader, the
    //  number pool and the mod's movex need nothing new; a library after Revenge's keeps the mod's count and stamp)
    static const char* const AFTER[] = { "movelib_wm2000.bin", "movelib_revenge_strikes.bin" };     // (in this order: their moves' numbers follow)
    for (const char* name : AFTER) {
        uint32_t stamp = 0;
        std::string wrong;
        const std::filesystem::path q = content_dir() / name;
        if (!move_library_read(q, &stamp, wrong)) {
            fprintf(stderr, "[moves] %s %s: none of its moves (a folder's slot naming one keeps his packed moveset's move)\n", arcade::shown(q).c_str(), wrong.c_str());
            fflush(stderr);
        }
    }
    lib_all_n = uint32_t(all_moves.size());
}
// ---- end of the file numbers block (2026-11-13, filepool) ----------------------------------------------------------
// "0x0320", "800" or a number in JSON, to a number. Returns -1 if it isn't one.
int64_t number_of(const json& v) {
    if (v.is_number_integer()) return v.get<int64_t>();
    if (!v.is_string()) return -1;
    const std::string s = v.get<std::string>();
    char* end = nullptr;
    const long long n = strtoll(s.c_str(), &end, 0);
    return (s.empty() || *end) ? -1 : n;
}

// A path as UTF-8, for IDs and the log (2026-11-12: .string() can throw on Windows for a name outside the code page).
std::string utf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.c_str()), u.size());
}

// A string of hex digits (spaces allowed), to bytes. Returns false if it isn't exactly `want` bytes.
bool bytes_of(const std::string& hex, int want, std::vector<uint8_t>& out) {
    std::string digits;
    for (char c : hex) if (c != ' ') digits += c;
    if (int(digits.size()) != want * 2) return false;
    for (int i = 0; i < want; i++) {
        char* end = nullptr;
        const std::string two = digits.substr(size_t(i) * 2, 2);
        const long v = strtol(two.c_str(), &end, 16);
        if (*end) return false;
        out.push_back(uint8_t(v));
    }
    return true;
}

// Reads a folder's hd.json. Returns an empty string, or what's wrong with it.
std::string read_hd(const std::filesystem::path& folder, HdModel& m) {
    std::ifstream file(folder / "hd.json");
    if (!file.good()) return "has hd.bin but no hd.json";
    const json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return "hd.json isn't valid JSON";
    static const char* const NAMES[9] = { "vtx_off", "vtx_bytes", "dl_off", "dl_bytes", "bl_off", "ha_off", "hand_r_n", "hand_l_n", "desc_off" };
    for (int i = 0; i < 9; i++) {
        if (!j.contains(NAMES[i])) return std::string("hd.json has no ") + NAMES[i];
        m.fields[i] = j[NAMES[i]].get<uint32_t>();
    }
    if (!j.contains("desc_bytes") || !j.contains("idx_off")) return "hd.json has no desc_bytes or idx_off";
    m.desc_bytes = j["desc_bytes"].get<uint32_t>();
    m.idx_off = j["idx_off"].get<uint32_t>();
    if (!j.contains("bones") || j["bones"].size() != HD_BONES) return "hd.json needs 28 bones";
    if (!j.contains("joints") || j["joints"].size() != HD_JOINTS) return "hd.json needs 15 joints";
    for (int b = 0; b < HD_BONES; b++) for (int k = 0; k < 6; k++) m.bones[b][k] = j["bones"][b][k].get<uint32_t>();
    for (int n = 0; n < HD_JOINTS; n++) {
        const json& r = j["joints"][n];
        m.joints[n] = { r[0].get<uint32_t>(), r[1].get<uint32_t>(), r[2].get<float>(), r[3].get<float>(), r[4].get<float>() };
    }
    m.pack = folder / "hd.bin";
    m.pictures = folder / "pictures.pak";
    if (!std::filesystem::exists(m.pictures)) return "has hd.bin but no pictures.pak";
    return "";
}

// The exe's media player is on (WCW_MEDIA unset or not 0: arcade_media.cpp media_on, read the same way).
bool native_media() { const char* e = getenv("WCW_MEDIA"); return !(e && e[0] && atoi(e) == 0); }
void read_hd_files(const std::filesystem::path& folder, Wrestler& w);

// A slot's move as moves.json names it: a number or "nomercy:<number>" (No Mercy's own), "<library>:<move>" (looked up in
// move_library), or null (the packed moveset's). Returns false if it isn't a move at all (`wrong` says why); an ID no library
// has is `keep` with the ID in `wrong`, for the log.
bool slot_move(const json& v, MoveRef& m, bool& keep, std::string& wrong, const std::map<std::string, MoveRef>& own) {
    keep = false;
    if (v.is_null()) { keep = true; return true; }
    std::string text = v.is_string() ? v.get<std::string>() : "";
    if (text.rfind("nomercy:", 0) == 0) text = text.substr(8);
    const int64_t n = v.is_string() ? number_of(json(text)) : number_of(v);
    if (n >= 0) {
        if (n > 0xFFFF) { wrong = "a move number must be 0-65535"; return false; }
        m = { 0, uint16_t(n) };
        return true;
    }
    if (!v.is_string()) { wrong = "a move must be a number or an ID like \"revenge:126c_1270\""; return false; }
    // (2026-11-13, movelib) "<library>:<move>" or "own:<name>", any case, then "@<No Mercy move>" to play it on another move
    // than the library's usual one (a slot's own: a hold's grab in a hold's slot ...). (2026-11-13, movelib2) "@<move>/<move>":
    // the slot has the first (a move that goes on: the game numbers its continuation entries by it) and the match plays the
    // second in its place (a move that ends, as the library move does) -- put in after the unpack, as a built-in port's row
    // does it, so the tables are the port's.
    std::string id = v.get<std::string>();
    for (char& c : id) c = char(tolower((unsigned char)c));
    int64_t on = -1, in_place = 0;
    if (const size_t at = id.find('@'); at != std::string::npos) {
        std::string after = id.substr(at + 1);
        if (const size_t sl = after.find('/'); sl != std::string::npos) {
            in_place = number_of(json(after.substr(sl + 1)));
            if (in_place <= 0 || in_place > 0xFFFF) { wrong = "\"" + v.get<std::string>() + "\": after / goes the No Mercy move put in its place"; return false; }
            after = after.substr(0, sl);
        }
        on = number_of(json(after));
        if (on < 0 || on > 0xFFFF) { wrong = "\"" + v.get<std::string>() + "\": after @ goes the No Mercy move it's played on"; return false; }
        id = id.substr(0, at);
    }
    const bool is_own = id.rfind("own:", 0) == 0;
    const auto it = is_own ? own.find(id.substr(4)) : move_library.find(id);
    if (it == (is_own ? own.end() : move_library.end())) { keep = true; wrong = v.get<std::string>(); return true; }   // (reported by the caller)
    m = it->second;
    if (on >= 0) m.base = uint16_t(on);
    m.in_place = uint16_t(in_place);
    return true;
}

// moves.json: his moveset as a list of slots (2026-11-13, movesets). Returns an empty string, or what's wrong with it.
// moves.json's "records" and "extra" (2026-11-13, movelib2): edits of his match's move entries past what his slots say.
//   "records": {"<slot>": {"<byte>": <value>, ...}, ...}  -- his slot's move record (the 36 bytes the match copies from No
//              Mercy's file 0x1EF for its move: damage at 6 / 7 / 8, timing, flags ...) with these bytes changed: a move of
//              his played harder, sooner or later than No Mercy's own (a library move's from its source game's data, where
//              the handler can map it). Values -128 .. 255 (a negative one is the byte's two's complement).
//   "extra":   [{"move": <a No Mercy move or a library / own ID>, "use": "legdrop", "special": true,
//                "record": {"<byte>": <value>}}, ...]  -- entries of his own past the moveset's 292 and the game's
//              continuations (the game's table has 548 entries a wrestler: as many as are left), each a move with its
//              record (and its library animation, for an ID) -- what Hollywood Hogan's leg drop is (an entry past 292, a
//              finisher's record). "use" says how the match comes to it: "legdrop" -- his ground strike at a lying man's head
//              with SPECIAL (mod logic.c move_entry). An extra entry with no "use" is there for nothing to reach yet.
// Each edit, for the mod (recomp_arcade_wrestler_edit): u16 entry (a slot; 0xFFFF: a new entry past the continuations), u16
// use (0 none, 1 the leg drop), u16 the No Mercy move (0: the slot's), u8 special, u8 0, u32 the move's ID (0: none), 36
// bytes: which record bytes are set (1), 36: their values -- EDIT_BYTES in all.
constexpr int EDIT_BYTES = 84;
std::string read_record_edit(const json& r, uint8_t* mask, uint8_t* val) {
    if (!r.is_object()) return "a record's changes must be {\"<byte>\": value}";
    for (auto it = r.begin(); it != r.end(); ++it) {
        const int64_t at = number_of(json(it.key())), v = number_of(it.value());
        if (at < 0 || at >= 36) return "a record has bytes 0 .. 35 (\"" + it.key() + "\")";
        if (v < -128 || v > 255) return "a record byte's value is -128 .. 255";
        mask[at] = 1; val[at] = uint8_t(v & 0xFF);
    }
    return "";
}
std::string read_edits(const json& j, Wrestler& w) {
    std::vector<uint8_t> out;
    const auto edit = [&](uint16_t entry, uint16_t use, uint16_t move, bool special, uint32_t id) -> uint8_t* {
        out.resize(out.size() + EDIT_BYTES, 0);
        uint8_t* e = out.data() + out.size() - EDIT_BYTES;
        e[0] = uint8_t(entry >> 8); e[1] = uint8_t(entry); e[2] = uint8_t(use >> 8); e[3] = uint8_t(use);
        e[4] = uint8_t(move >> 8); e[5] = uint8_t(move); e[6] = special ? 1 : 0;
        for (int b = 0; b < 4; b++) e[8 + b] = uint8_t(id >> (24 - 8 * b));
        return e;
    };
    if (j.contains("records")) {
        if (!j["records"].is_object()) return "\"records\" must be {\"<slot>\": {\"<byte>\": value}}";
        for (auto it = j["records"].begin(); it != j["records"].end(); ++it) {
            const int64_t slot = number_of(json(it.key()));
            if (slot < 0 || slot >= SLOTS_N) return "\"records\" names slot \"" + it.key() + "\": slots are 0 .. 291";
            uint8_t* e = edit(uint16_t(slot), 0, 0, false, 0);
            const std::string wrong = read_record_edit(it.value(), e + 12, e + 48);
            if (!wrong.empty()) return "\"records\" slot " + it.key() + ": " + wrong;
        }
    }
    if (j.contains("extra")) {
        if (!j["extra"].is_array()) return "\"extra\" must be a list of entries";
        for (const json& x : j["extra"]) {
            if (!x.is_object() || !x.contains("move")) return "an \"extra\" entry needs a \"move\"";
            MoveRef m{};
            bool keep = false;
            std::string wrong;
            if (!slot_move(x["move"], m, keep, wrong, w.own) || keep) return "an \"extra\" entry's move: " + (wrong.empty() ? std::string("none given") : wrong);
            uint16_t use = 0;
            if (x.contains("use") && !x["use"].is_null()) {
                if (!x["use"].is_string() || x["use"].get<std::string>() != "legdrop") return "an \"extra\" entry's \"use\" is \"legdrop\" (or none)";
                use = 1;
            }
            const bool special = x.contains("special") && x["special"].is_boolean() && x["special"].get<bool>();
            uint8_t* e = edit(0xFFFF, use, m.base, special, m.id);
            if (x.contains("record")) {
                const std::string bad = read_record_edit(x["record"], e + 12, e + 48);
                if (!bad.empty()) return "an \"extra\" entry's record: " + bad;
            }
        }
    }
    w.edits = std::move(out);
    return "";
}

std::string read_slots(const std::filesystem::path& path, Wrestler& w) {
    std::ifstream file(path);
    if (!file.good()) return "can't be opened";
    const json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("slots") || !j["slots"].is_array()) return "has no \"slots\" list";
    if (j["slots"].size() != size_t(SLOTS_N)) return "\"slots\" must have " + std::to_string(SLOTS_N) + " entries";
    std::vector<uint8_t> out;
    std::string unknown;
    int n_unknown = 0;
    std::vector<std::pair<uint16_t, uint16_t>> in_place;    // (2026-11-13, movelib2: slot, the move put in its place)
    for (const json& e : j["slots"]) {
        if (!e.is_array() || e.size() != 4) return "each slot must be [move, flag, flag, flag]";
        MoveRef move{};
        bool keep = false;
        std::string wrong;
        if (!slot_move(e[0], move, keep, wrong, w.own)) return wrong;
        if (keep && !wrong.empty()) {         // (an ID no library has: listed in the log, the slot his packed moveset's)
            if (n_unknown++ < 8) unknown += (unknown.empty() ? "" : ", ") + wrong + " (slot " + std::to_string(out.size() / SLOT_STRIDE) + ")";
        }
        int flags = 0;
        for (int k = 1; k < 4; k++) {
            const int64_t f = number_of(e[size_t(k)]);
            if (f != 0 && f != 1) return "a slot's flags must be 0 or 1";
            flags |= int(f) << (k - 1);
        }
        if (keep) flags |= SLOT_KEEP;
        if (move.in_place) in_place.push_back({ uint16_t(out.size() / SLOT_STRIDE), move.in_place });
        out.push_back(uint8_t(move.base >> 8)); out.push_back(uint8_t(move.base)); out.push_back(uint8_t(flags)); out.push_back(0);
        for (int b = 24; b >= 0; b -= 8) out.push_back(uint8_t(move.id >> b));
    }
    w.slots = std::move(out);
    {   // (2026-11-13, movelib2: anything wrong in them leaves him without them, and says so: his slots stay)
        const std::string bad = read_edits(j, w);
        if (!bad.empty()) { w.edits.clear(); fprintf(stderr, "[wrestlers] %s: moves.json %s: no record changes or extra entries\n", arcade::shown(path.parent_path()).c_str(), bad.c_str()); }
        // (and each "@<move>/<move>" as an edit: its slot, the move put in its place)
        for (const auto& [slot, mv] : in_place) {
            w.edits.resize(w.edits.size() + EDIT_BYTES, 0);
            uint8_t* x = w.edits.data() + w.edits.size() - EDIT_BYTES;
            x[0] = uint8_t(slot >> 8); x[1] = uint8_t(slot); x[4] = uint8_t(mv >> 8); x[5] = uint8_t(mv);
        }
    }
    if (n_unknown)
        fprintf(stderr, "[wrestlers] %s: moves.json names %d move%s no built-in or folder library has yet: %s%s -- %s kept from his packed moveset\n",
                arcade::shown(path.parent_path()).c_str(), n_unknown, n_unknown == 1 ? "" : "s", unknown.c_str(), n_unknown > 8 ? " ..." : "",
                n_unknown == 1 ? "that slot is" : "those slots are");
    return "";
}

// moves\movex.json: his own moves (2026-11-13, movelib). Returns an empty string, or what's wrong with it (then none of them).
std::string read_own_moves(const std::filesystem::path& folder, Wrestler& w) {
    const std::filesystem::path dir = folder / "moves";
    std::ifstream file(dir / "movex.json");
    if (!file.good()) return "can't be opened";
    const json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("moves") || !j["moves"].is_object()) return "has no \"moves\"";
    AnimSet fa;
    fa.shown = arcade::shown(folder);
    std::vector<std::pair<std::string, OwnMove>> moves;
    std::vector<uint16_t> on;
    // (a list of files and their records: added to fa as one run)
    const auto files = [&](const json& m, size_t want, uint32_t& first, std::string& wrong) -> bool {
        if (!m.contains("anims") || !m["anims"].is_array() || m["anims"].size() != want || !m.contains("records") || !m["records"].is_array() ||
            m["records"].size() != want) { wrong = "needs \"anims\" and \"records\", " + std::to_string(want) + " of each"; return false; }
        first = uint32_t(fa.files.size());
        for (size_t k = 0; k < want; k++) {
            if (!m["anims"][k].is_string() || !m["records"][k].is_string()) { wrong = "an animation is a file name, a record 72 hex digits"; return false; }
            OwnAnim a;
            a.path = dir / m["anims"][k].get<std::string>();
            std::error_code ec;
            const auto size = std::filesystem::file_size(a.path, ec);
            if (ec || size < 4) { wrong = "names an animation that isn't there: " + m["anims"][k].get<std::string>(); return false; }
            a.bytes = file_bytes(a.path);
            if (a.bytes.size() < 4) { wrong = "an animation that can't be read: " + m["anims"][k].get<std::string>(); return false; }
            const std::string hex = m["records"][k].get<std::string>();
            if (hex.size() != 72) { wrong = "a record is 36 bytes (72 hex digits)"; return false; }
            for (size_t b = 0; b < 72; b += 2) {
                const int64_t v = number_of(json("0x" + hex.substr(b, 2)));
                if (v < 0) { wrong = "a record is hex digits"; return false; }
                a.record.push_back(uint8_t(v));
            }
            fa.files.push_back(std::move(a));
        }
        fa.runs.push_back({ first, uint32_t(want) });
        return true;
    };
    for (auto mi = j["moves"].begin(); mi != j["moves"].end(); ++mi) {
        const std::string name = mi.key();
        const json& m = mi.value();
        std::string wrong;
        OwnMove om{ int(anim_sets.size()), 0, 0, false };
        if (!m.is_object()) return "move \"" + name + "\" isn't an object";
        const int64_t base = m.contains("on") ? number_of(m["on"]) : -1;
        if (base < 0 || base > 0xFFFF) return "move \"" + name + "\" needs \"on\": the No Mercy move it's played on";
        if (!files(m, 2, om.pair, wrong)) return "move \"" + name + "\" " + wrong;
        if (m.contains("then")) {
            if (!files(m["then"], 8, om.then, wrong)) return "move \"" + name + "\" \"then\" " + wrong;
            om.has_then = true;
        }
        std::string key = name;
        for (char& c : key) c = char(tolower((unsigned char)c));
        moves.push_back({ key, om });
        on.push_back(uint16_t(base));
    }
    if (moves.empty()) return "";
    for (size_t k = 0; k < moves.size(); k++) {
        w.own[moves[k].first] = { uint32_t(all_moves.size() + 1), on[k] };
        all_moves.push_back(moves[k].second);
    }
    fprintf(stderr, "[wrestlers] %s: %zu move%s of his own (moves\\movex.json), %zu animation files (game file numbers given while he's in a match)\n",
            fa.shown.c_str(), moves.size(), moves.size() == 1 ? "" : "s", fa.files.size());
    anim_sets.push_back(std::move(fa));
    return "";
}

// A stock folder carries data (2026-11-13, nmhandler): any record field, his looks, moveset, per-costume fields or a photo.
bool stock_has_data(const Wrestler& w) {
    return w.has_name || w.has_short || w.has_nick || w.height >= 0 || w.weight >= 0 || w.music >= 0 || w.video >= 0 ||
           !w.looks.empty() || !w.moves.empty() || !w.per_costume.empty() || !w.photo.empty() || !w.keys.empty();
}
// A stock folder's photos (2026-11-13, nmhandler): photo.bin, and photo_c1.bin .. photo_c3.bin for costumes with their own.
void read_stock_photos(const std::filesystem::path& folder, Wrestler& w) {
    for (int c = 0; c < 4; c++) {
        const std::filesystem::path p = folder / (c == 0 ? std::string("photo.bin") : "photo_c" + std::to_string(c) + ".bin");
        std::ifstream f(p, std::ios::binary);
        if (!f.good()) continue;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (int(bytes.size()) != PHOTO_BYTES) { fprintf(stderr, "[wrestlers] %s: must be %d bytes: not used\n", arcade::shown(p).c_str(), PHOTO_BYTES); continue; }
        (c == 0 ? w.photo : w.photo_c[c]) = std::move(bytes);
    }
}

// A folder's optional files. Anything wrong with them leaves him without them and says so; he still gets on the roster.
void read_files(const std::filesystem::path& folder, Wrestler& w) {
    const std::string name = arcade::shown(folder);     // (2026-11-12: the whole path: two folders may have one name in different collections; 2026-11-13: from the game folder on)
    move_library_fill();
    if (std::filesystem::exists(folder / "moves" / "movex.json")) {   // (2026-11-13, movelib: first -- moves.json names them)
        const std::string wrong = read_own_moves(folder, w);
        if (!wrong.empty()) fprintf(stderr, "[wrestlers] %s: moves\\movex.json %s: no moves of his own\n", name.c_str(), wrong.c_str());
    }
    if (std::filesystem::exists(folder / "moves.json")) {
        const std::string wrong = read_slots(folder / "moves.json", w);
        if (!wrong.empty()) fprintf(stderr, "[wrestlers] %s: moves.json %s: his packed moveset is used\n", name.c_str(), wrong.c_str());
    }
    std::ifstream photo(folder / "photo.bin", std::ios::binary);
    if (photo.good()) {
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(photo)), std::istreambuf_iterator<char>());
        if (int(bytes.size()) == PHOTO_BYTES) w.photo = std::move(bytes);
        else fprintf(stderr, "[wrestlers] %s: photo.bin must be %d bytes: no photo\n", name.c_str(), PHOTO_BYTES);
    }
    // 2026-11-12: with the exe's media player on (the default), theme.bin / theme.meta and video.bin are played by it, found
    // by his ID like a theme.mp3 (arcade_media.cpp look_in), so they're not handed to the mod's banks here: no count of
    // them, and nothing of them loaded into the content window at start-up. Only with WCW_MEDIA=0 (the player off: a test
    // switch) do they go the old way, through the game's sound bank and video lists.
    if (native_media()) return read_hd_files(folder, w);
    std::ifstream meta(folder / "theme.meta", std::ios::binary);
    if (meta.good()) {
        Theme t{ w.id, folder / "theme.bin", {} };
        meta.read(reinterpret_cast<char*>(t.meta), THEME_META_BYTES);
        if (meta.gcount() != THEME_META_BYTES || !std::filesystem::exists(t.sound)) fprintf(stderr, "[wrestlers] %s: theme.meta is the wrong size, or there's no theme.bin: no theme\n", name.c_str());
        else if (int(themes.size()) >= THEME_MAX) fprintf(stderr, "[wrestlers] %s: only %d themes can come from folders: no theme\n", name.c_str(), THEME_MAX);
        else themes.push_back(t);
    }
    {
        std::error_code ec;
        const uintmax_t bytes = std::filesystem::file_size(folder / "video.bin", ec);
        if (!ec) {
            if (bytes == 0 || bytes % VIDEO_FRAME_BYTES) fprintf(stderr, "[wrestlers] %s: video.bin isn't a whole number of frames: no video\n", name.c_str());
            else if (int(videos.size()) >= VIDEO_MAX) fprintf(stderr, "[wrestlers] %s: only %d videos can come from folders: no video\n", name.c_str(), VIDEO_MAX);
            else videos.push_back({ w.id, folder / "video.bin", uint32_t(bytes / VIDEO_FRAME_BYTES) });
        }
    }
    read_hd_files(folder, w);
}

// New feature (Ultimate No Mercy): [revvisual 2026-11-20] does his folder have a Titantron video file of its own -- video.bin, or
// titantron.* / video.* in another format (the names arcade_media.cpp look_in plays; its .meta / .json / .txt notes aren't videos)
bool folder_has_video(const std::filesystem::path& folder) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string stem = entry.path().stem().string(), ext = entry.path().extension().string();
        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
        for (char& ch : ext) ch = char(std::tolower((unsigned char)ch));
        if ((stem == "video" || stem == "titantron") && !ext.empty() && ext != ".meta" && ext != ".json" && ext != ".txt" && (ext != ".bin" || stem == "video")) return true;
    }
    return false;
}

// His models, if his folder has them: a low-detail one of his own (model\model.json), and a full-detail one (hd.bin).
// (2026-11-13, handlers1: both media paths come here, so the low-detail model is read with the player on or off.)
void read_hd_files(const std::filesystem::path& folder, Wrestler& w) {
    const std::string name = folder.filename().string();
    if (std::filesystem::exists(folder / "model" / "model.json")) {     // (2026-11-12, handlers: his own low-detail model)
        // The model code knows a folder wrestler by his photo number (mod roster.c), so this one needs a photo too.
        if (w.photo.empty()) fprintf(stderr, "[wrestlers] %s: a model of his own needs a photo.bin too: no model\n", name.c_str());
        else {
            LowModel m;
            const std::string wrong = read_low(folder, m);
            if (!wrong.empty()) fprintf(stderr, "[wrestlers] %s: %s: no model of his own\n", name.c_str(), wrong.c_str());
            else {
                w.low_index = int(low_models.size());
                fprintf(stderr, "[wrestlers] %s: his own model, %zu files (game file numbers given while the game loads it)\n", name.c_str(), m.files.size());
                low_models.push_back(std::move(m));
            }
        }
    }
    if (!std::filesystem::exists(folder / "hd.bin")) return;
    if (w.photo.empty()) { fprintf(stderr, "[wrestlers] %s: a full-detail model needs a photo.bin too: no model\n", name.c_str()); return; }
    if (int(hd_models.size()) >= HD_MAX) { fprintf(stderr, "[wrestlers] %s: only %d full-detail wrestlers can come from folders: no model\n", name.c_str(), HD_MAX); return; }
    HdModel m;
    const std::string wrong = read_hd(folder, m);
    if (!wrong.empty()) { fprintf(stderr, "[wrestlers] %s: %s: no model\n", name.c_str(), wrong.c_str()); return; }
    w.hd_index = int(hd_models.size());
    hd_models.push_back(m);
}

// The override file (2026-11-17, the user's decision): wrestler.override.json beside a wrestler.json -- written by the user or
// the editor, never by an importer, so a re-import leaves it alone -- is laid over his wrestler.json as it is read, as a
// JSON Merge Patch (RFC 7386): an object goes into the object at the same place key by key, anything else (a string, a
// number, a list) replaces what is there, and null removes the key. So {"name": "THE MAN", "nick": null, "body": {"nm_size":
// 70}} renames him, takes his nickname away and changes one member of "body" only. His identity is the folder's, not the
// override's: "id", "string_id", "builtin" and "replaces" in it are left out (the log says so). A file that isn't a JSON
// object is not used (the log says so) and he loads as his wrestler.json says. No override file: nothing changes.
constexpr const char* OVERRIDE_FILE = "wrestler.override.json";
void apply_override(const std::filesystem::path& path, json& j) {
    const std::filesystem::path op = path.parent_path() / OVERRIDE_FILE;
    std::error_code ec;
    if (!std::filesystem::exists(op, ec)) return;
    std::ifstream f(op);
    json o = f.good() ? json::parse(f, nullptr, false) : json();
    if (o.is_discarded() || !o.is_object()) {
        fprintf(stderr, "[wrestlers] %s isn't a JSON object: not used\n", arcade::shown(op).c_str());
        return;
    }
    for (const char* k : { "id", "string_id", "builtin", "replaces" })
        if (o.erase(k)) fprintf(stderr, "[wrestlers] %s: \"%s\" left out (a wrestler's identity is his folder's)\n", arcade::shown(op).c_str(), k);
    j.merge_patch(o);
    fprintf(stderr, "[wrestlers] %s: laid over his wrestler.json (%zu keys)\n", arcade::shown(op).c_str(), o.size());
}

// (rostertags, 2026-11-21) "class": the numbers the mod has for them (mod src/roster.c CLASS_*: must match)
constexpr uint8_t CLASS_WRESTLER = 1, CLASS_WOMAN = 2, CLASS_VALET = 3, CLASS_MANAGER = 4, CLASS_ANNOUNCER = 5, CLASS_REFEREE = 6,
                  CLASS_OFFICIAL = 7, CLASS_AUTHORITY = 8;
const char* const CLASS_NAMES[] = { "", "wrestler", "woman", "valet", "manager", "announcer", "referee", "official", "authority" };
// Reads one wrestler.json. Returns an empty string, or what's wrong with it.
std::string read_one(const std::filesystem::path& path, Wrestler& w) {
    std::ifstream file(path);
    if (!file.good()) return "can't be opened";
    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return "isn't valid JSON";
    apply_override(path, j);     // (2026-11-17: wrestler.override.json, when there)

    // "id": his string ID, if not his folder's name. A number (the wrestlers made before string IDs): kept as his game number
    // when it's free. Neither is required. (2026-11-09)
    if (j.contains("id") && j["id"].is_string() && number_of(j["id"]) < 0) w.sid = j["id"].get<std::string>();
    else if (j.contains("id")) {
        const int64_t id = number_of(j["id"]);
        if (id > 0 && id < 0x1000 && (id & 0xF00) != 0xA00 && (id & 0xF00) != 0xB00) w.id = uint16_t(id);
    }
    // "based_on": the string ID of the one he starts as a copy of ("stone_cold"), or the old number.
    // "builtin" instead (2026-11-12, the stock roster as folders): this folder IS that built-in Superstar -- the game's
    // own data is his, and the folder gives him his ID (scan).
    w.stock = j.contains("builtin") && !j.contains("based_on");
    // (2026-11-14, revfix -- the user: every wrestler is a folder, the built-in imports too) "replaces": "<a built-in's ID>"
    // -- this folder wrestler (his own model, moves, photo, theme ...) IS that Superstar from now on: his string ID and his
    // game number, so the saves, presets, titles and stories that name him keep him; the built-in copy is off the roster.
    if (j.contains("replaces")) {
        if (!j["replaces"].is_string() || j["replaces"].get<std::string>().empty()) return "\"replaces\" must be a built-in Superstar's ID (\"sting\")";
        w.replaces = j["replaces"].get<std::string>();
    }
    const char* const from = w.stock ? "builtin" : "based_on";
    if (!j.contains(from)) return "needs \"based_on\": the ID of the wrestler he starts as a copy of";
    const int64_t base = number_of(j[from]);
    if (base > 0 && base < 0x1000) w.based_on = uint16_t(base);
    else if (j[from].is_string()) w.based_on_sid = j[from].get<std::string>();
    else return std::string("\"") + from + "\" must be a wrestler's ID";

    const auto text = [&](const char* key, std::string& to, int bytes) {
        if (j.contains(key) && j[key].is_string()) to = j[key].get<std::string>().substr(0, size_t(bytes) - 1);
    };
    text("name", w.name, NAME_BYTES);
    text("short_name", w.short_name, SHORT_BYTES);
    text("nick", w.nick, NICK_BYTES);
    w.no_nick = j.contains("nick") && j["nick"].is_null();
    w.has_name = j.contains("name") && j["name"].is_string();         // (2026-11-13: for a stock folder, which has no base)
    w.has_short = j.contains("short_name") && j["short_name"].is_string();
    w.has_nick = j.contains("nick") && (j["nick"].is_string() || j["nick"].is_null());
    // (2026-11-19, roster) The game he was imported from, as his importer wrote it: the Superstar Select's pages group by it
    // (select_pages.json {"game": ...}). Anything else in "source" stays a note.
    if (j.contains("source") && j["source"].is_object() && j["source"].contains("game") && j["source"]["game"].is_string())
        w.source_game = j["source"]["game"].get<std::string>();
    // (automerge, 2026-11-23) for MERGE SAME WRESTLERS: his full name, "about", and his place in his game's roster -- the first
    // of "ordinal" / "roster_index" that is a whole number, else "code" read as a number ("0x303"), as the editor reads them
    if (w.has_name) w.full_name = j["name"].get<std::string>();
    if (j.contains("about") && j["about"].is_string()) w.about = j["about"].get<std::string>();
    if (j.contains("source") && j["source"].is_object()) {
        const json& src = j["source"];
        for (const char* k : { "ordinal", "roster_index" })
            if (w.source_pos < 0 && src.contains(k) && (src[k].is_number_integer() || src[k].is_boolean()))
                w.source_pos = src[k].is_boolean() ? int64_t(src[k].get<bool>()) : src[k].get<int64_t>();
        if (w.source_pos < 0 && src.contains("code") && src["code"].is_string() && number_of(src["code"]) >= 0) w.source_pos = number_of(src["code"]);
    }

    const auto value =[&](const char* key, int& to, int top) -> bool {
        if (!j.contains(key)) return true;
        const int64_t n = number_of(j[key]);
        if (n < 0 || n > top) return false;
        to = int(n);
        return true;
    };
    if (!value("height", w.height, 255)) return "\"height\" must be 0-255";
    if (!value("weight", w.weight, 65535)) return "\"weight\" must be 0-65535";
    if (!value("music", w.music, 255)) return "\"music\" must be 0-255";
    if (!value("video", w.video, 255)) return "\"video\" must be 0-255";
    if (!value("size", w.size, 255)) return "\"size\" must be 0-255";     // (2026-11-13, revbody)
    if (j.contains("class")) {      // (rostertags, 2026-11-21)
        std::string c = j["class"].is_string() ? j["class"].get<std::string>() : std::string();
        for (char& ch : c) ch = char(tolower(uint8_t(ch)));
        for (uint8_t k = 1; k <= CLASS_AUTHORITY; k++) if (c == CLASS_NAMES[k]) w.cls = k;
        if (!w.cls) fprintf(stderr, "[wrestlers] %s: \"class\" %s isn't one of wrestler, woman, valet, manager, announcer, referee, "
                                    "official, authority: left out (the rule by his number)\n", arcade::shown(path.parent_path()).c_str(), j["class"].dump().c_str());
    }

    // (2026-11-14, the user: "variant", never "costume": "packed_variants" is the key now, "costumes" still read as it)
    const char* const packed = j.contains("packed_variants") ? "packed_variants" : "costumes";
    if (j.contains(packed)) {
        if (!j[packed].is_array() || j[packed].size() != 4) return std::string("\"") + packed + "\" must be a list of 4";
        for (const json& c : j[packed]) {
            if (!c.is_string() || !bytes_of(c.get<std::string>(), KEY_BYTES, w.keys)) return std::string("each of \"") + packed + "\" must be 23 bytes of hex";
        }
    }
    if (j.contains("looks")) {      // (2026-11-13, handlers1: his costumes' looks as numbers; "costumes" is then not read)
        w.keys.clear();
        if (!j["looks"].is_array() || j["looks"].size() != 4) return "\"looks\" must be a list of 4 variants";
        for (const json& c : j["looks"]) {
            if (!c.is_array() || c.size() != size_t(LOOK_FIELDS)) return "each of \"looks\" must be a list of 38 numbers";
            for (const json& v : c) {
                if (!v.is_number_integer() || v.get<int64_t>() < 0 || v.get<int64_t>() > 255) return "the numbers in \"looks\" must be 0-255";
                w.looks.push_back(uint8_t(v.get<int64_t>()));
            }
        }
    }
    if (j.contains("moveset")) {
        if (!j["moveset"].is_string() || !bytes_of(j["moveset"].get<std::string>(), MOVES_BYTES, w.moves)) return "\"moveset\" must be 216 bytes of hex";
    }
    // (2026-11-14, the user: "variant", never "costume": "per_variant" is the key now, "per_costume" still read as it)
    const char* const per = j.contains("per_variant") ? "per_variant" : "per_costume";
    if (j.contains(per)) {   // (2026-11-13, nmhandler: a variant's own record fields, read for stock folders)
        if (!j[per].is_array() || j[per].size() != 4) return std::string("\"") + per + "\" must be a list of 4";
        for (const json& c : j[per]) {
            if (!c.is_object()) return std::string("each of \"") + per + "\" must be an object ({} for none)";
            Wrestler::Costume o;
            const auto ctext = [&](const char* key, std::string& to, bool& has, int bytes) {
                if (!c.contains(key)) return true;
                if (c[key].is_null()) { has = true; to.clear(); return true; }
                if (!c[key].is_string()) return false;
                to = c[key].get<std::string>().substr(0, size_t(bytes) - 1);
                has = true;
                return true;
            };
            const auto cvalue = [&](const char* key, int& to, int top) {
                if (!c.contains(key)) return true;
                const int64_t n = number_of(c[key]);
                if (n < 0 || n > top) return false;
                to = int(n);
                return true;
            };
            if (!ctext("name", o.name, o.has_name, NAME_BYTES) || !ctext("short_name", o.short_name, o.has_short, SHORT_BYTES) ||
                !ctext("nick", o.nick, o.has_nick, NICK_BYTES)) return std::string("a variant's names in \"") + per + "\" must be text (null: none)";
            if (!cvalue("height", o.height, 255) || !cvalue("weight", o.weight, 65535) || !cvalue("music", o.music, 255) || !cvalue("video", o.video, 255))
                return std::string("a variant's height, weight, music or video in \"") + per + "\" is out of range";
            w.per_costume.push_back(o);
        }
    }
    return "";
}

// A string ID as the roster keeps it: lower case, "_" for spaces and punctuation. Letters beyond ASCII (UTF-8) stay as they
// are, so a folder named in any language is still his own ID. "~" stays: it's how a clash is told apart. (2026-11-12)
std::string clean_id(const std::string& s) {
    std::string o;
    for (char c : s) {
        const uint8_t u = uint8_t(c);
        if (u >= 0x80 || isalnum(u) || c == '~') o += char(tolower(u));
        else if (o.empty() || o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    while (!o.empty() && o[0] == '_') o.erase(0, 1);
    return o.empty() ? std::string("wrestler") : o;
}

// (2026-11-14, families) See "families" above.
void families_read(const std::filesystem::path& root, std::vector<std::filesystem::path> files,
                   const std::map<std::string, uint16_t>& sids, const std::vector<bool>& used, const std::map<uint16_t, std::string>& whose,
                   const std::vector<std::pair<uint16_t, std::vector<uint16_t>>>& nests) {
    families.clear();
    family_of_id.assign(0x1000, -1);
    // (the collections' in case-insensitive path order, the user's D2: A-Z folded to a-z, ties by the exact bytes)
    const auto folded = [&](const std::filesystem::path& p) {
        std::string s = utf8(p.lexically_relative(root));
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        return s;
    };
    std::sort(files.begin(), files.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
        const std::string fa = folded(a), fb = folded(b);
        return fa != fb ? fa < fb : utf8(a) < utf8(b);
    });
    std::error_code ec;
    if (std::filesystem::exists(root / "families.json", ec)) files.insert(files.begin(), root);
    const auto name_of = [&](uint16_t n) {
        const auto s = whose.find(n);
        char num[8];
        snprintf(num, sizeof num, "0x%04X", unsigned(n));
        return s != whose.end() ? s->second : std::string(num);
    };
    // "wrestler": a string ID (as clean_id makes it), else a game number someone has ("0x0003")
    const auto who_one = [&](const json& v) -> uint16_t {
        const int64_t n = number_of(v);
        if (n > 0 && n < 0x1000) return used[size_t(n)] ? uint16_t(n) : 0;
        if (!v.is_string()) return 0;
        const auto s = sids.find(clean_id(v.get<std::string>()));
        return s != sids.end() ? s->second : 0;
    };
    // (startup 2026-11-19) or both, {"id": "aki_man", "game_number": "0x0379"}: the number when it is aki_man's, else
    // aki_man's own, by the string ID (the user: references fall back to the string ID, never the name on screen; the rule
    // as arcade_roster.h resolve_ref has it). A string ID nobody has: the number, as before.
    const auto who = [&](const json& v) -> uint16_t {
        if (!v.is_object()) return who_one(v);
        const uint16_t by_number = v.contains("game_number") ? who_one(v["game_number"]) : 0;
        if (!v.contains("id") || !v["id"].is_string()) return by_number;
        const auto s = sids.find(clean_id(v["id"].get<std::string>()));
        if (s == sids.end()) {
            fprintf(stderr, "[families] %s: no wrestler has that string ID: by the game number\n", v.dump().c_str());
            return by_number;
        }
        if (s->second != by_number)
            fprintf(stderr, "[families] %s: the game number isn't %s's: found by his string ID (0x%04X)\n", v.dump().c_str(), s->first.c_str(), unsigned(s->second));
        return s->second;
    };
    hidden_mask.assign(0x1000, 0);
    std::vector<std::pair<std::string, json>> read_files;      // (each file's text, read once)
    for (const auto& folder : files) {
        const std::filesystem::path path = folder / "families.json";
        const std::string where = arcade::shown(path);
        std::ifstream f(path);
        const json j = f.good() ? json::parse(f, nullptr, false) : json();
        if (j.is_discarded() || !j.is_object()) { fprintf(stderr, "[families] %s isn't valid JSON: not read\n", where.c_str()); continue; }
        read_files.push_back({ where, j });
    }
    // HIDDEN first (every file's "hidden", whether it has "families" or not): a hidden variant is left out of every family.
    for (const auto& [where, j] : read_files) {
        if (!j.contains("hidden")) continue;
        if (!j["hidden"].is_array()) { fprintf(stderr, "[families] %s: \"hidden\" must be a list: not read\n", where.c_str()); continue; }
        for (size_t k = 0; k < j["hidden"].size(); k++) {
            const json& h = j["hidden"][k];
            const uint16_t id = h.is_object() && h.contains("wrestler") ? who(h["wrestler"]) : 0;
            if (!id) { fprintf(stderr, "[families] %s, hidden %zu: no such wrestler: not read\n", where.c_str(), k + 1); continue; }
            if (h.contains("variant")) {
                const int64_t n = number_of(h["variant"]);
                if (n < 1 || n > 4) { fprintf(stderr, "[families] %s, hidden %zu: \"variant\" must be 1-4: not read\n", where.c_str(), k + 1); continue; }
                hidden_mask[id] |= uint8_t(1u << (n - 1));
                fprintf(stderr, "[families] hidden: %s's variant %d (%s) -- never shown or drawn; whatever names him keeps him\n", name_of(id).c_str(), int(n), where.c_str());
            } else {
                hidden_mask[id] = 0xF;
                fprintf(stderr, "[families] hidden: %s (%s) -- no box, never drawn at random; whatever names him keeps him\n", name_of(id).c_str(), where.c_str());
            }
        }
    }
    for (const char* d : DROPPED_LOOKS) {         // (2026-11-15, revfix: the ports' dropped second looks, above)
        const uint16_t id = who(json(d));
        if (!id || hidden_mask[id] == 0xF) continue;
        hidden_mask[id] = 0xF;
        fprintf(stderr, "[families] hidden: %s -- a Revenge port's second look, dropped (the ports have Revenge's own variants only)\n", name_of(id).c_str());
    }
    for (const auto& [where, j] : read_files) {
        if (!j.contains("families")) continue;
        if (!j["families"].is_array()) { fprintf(stderr, "[families] %s: \"families\" must be a list: not read\n", where.c_str()); continue; }
        families_from_files = true;
        families_take(j["families"], where, who, name_of);
    }
    if (!families_from_files) {
        const json j = json::parse(default_families_json);
        families_take(j["families"], "the default data: no families.json", who, name_of);
    }
    const auto list_of = [&](const Family& f) {
        std::string t;
        for (const FamilyVariant& x : f.variants) t += (t.empty() ? "" : ", ") + name_of(x.id) + " " + std::to_string(x.variant + 1);
        return t;
    };
    const auto add_four = [&](Family& f, uint16_t id) {      // (his four variants, but those hidden; all four in the list: automerge)
        for (uint8_t v = 0; v < 4; v++) { f.all.push_back({ id, v }); if (!(hidden_mask[id] & (1u << v))) f.variants.push_back({ id, v }); }
    };
    // NESTING (the user, 2026-11-14): a wrestler's folder inside another wrestler's folder is in that one's family -- his
    // four variants after the outer one's, nested ones in case-insensitive path order. The data wins: one a families.json
    // (or the default data) already puts in a family stays there, said here; one it doesn't name goes at the end of the
    // outer wrestler's family, or into a new family of the outer wrestler's four variants and his.
    nested_kid.assign(0x1000, false);                 // (automerge: who is nested, for the editor's tidy())
    for (const auto& [top, kids] : nests) for (uint16_t k : kids) if (k < 0x1000) nested_kid[k] = true;
    for (const auto& [top, kids] : nests) {
        std::vector<uint16_t> join;
        for (uint16_t k : kids) {
            if (family_of_id[k] >= 0) {
                fprintf(stderr, "[families] %s is nested in %s's folder, but %s puts him in family %d (%s): there -- the data wins\n",
                        name_of(k).c_str(), name_of(top).c_str(), families_from_files ? "families.json" : "the default data",
                        family_of_id[k] + 1, families[size_t(family_of_id[k])].name.c_str());
                continue;
            }
            join.push_back(k);
        }
        if (join.empty()) continue;
        int32_t fam = family_of_id[top];
        if (fam < 0) {
            Family f;
            f.name = name_of(top);
            f.slot = top;
            add_four(f, top);
            fam = int32_t(families.size());
            family_of_id[top] = fam;
            families.push_back(std::move(f));
        }
        Family& f = families[size_t(fam)];
        for (uint16_t k : join) { add_four(f, k); family_of_id[k] = fam; }
        fprintf(stderr, "[families] family %d, %s (with the folders nested in %s's): %zu variants: %s\n", fam + 1, f.name.c_str(),
                name_of(top).c_str(), f.variants.size(), list_of(f).c_str());
    }
    // (automerge, 2026-11-23) The rest -- PARTIAL families and hidden variants' own boxes -- is families_finish below: it runs
    // once the F2 option MERGE SAME WRESTLERS has had its say (families_ready), as the game reads a families.json the
    // roster editor's automatic consolidation would write.
}

// (2026-11-14, families; automerge 2026-11-23: out of families_read) What the game makes of the families once they are
// listed: a PARTIAL family's unlisted variants, a hidden variant's wrestler's own box; and the count, for the log.
void families_finish() {
    const auto name_of = [&](uint16_t n) {
        const auto s = sid_of_number.find(n);
        char num[8];
        snprintf(num, sizeof num, "0x%04X", unsigned(n));
        return s != sid_of_number.end() ? s->second : std::string(num);
    };
    const auto list_of = [&](const Family& f) {
        std::string t;
        for (const FamilyVariant& x : f.variants) t += (t.empty() ? "" : ", ") + name_of(x.id) + " " + std::to_string(x.variant + 1);
        return t;
    };
    const auto add_four = [&](Family& f, uint16_t id) {      // (his four variants, but those hidden)
        for (uint8_t v = 0; v < 4; v++) { f.all.push_back({ id, v }); if (!(hidden_mask[id] & (1u << v))) f.variants.push_back({ id, v }); }
    };
    // A PARTIAL family (the editor, round 4; 2026-11-14): one that lists only some of a wrestler's variants never makes the
    // others vanish. The first wrestler's unlisted variants go at the family's end (his box is the family's); any other
    // wrestler's unlisted ones keep a box of his own, where his box is (a family of his own of them: rest_of_id). A family
    // saying "unlisted": "off" leaves them out (the default data does, for the Revenge ports' second looks: their own
    // first look only, as always). Hidden variants are never put back.
    rest_of_id.assign(0x1000, -1);
    const size_t listed_n = families.size();
    for (size_t fi = 0; fi < listed_n; fi++) {
        if (families[fi].unlisted_off) continue;
        std::vector<uint16_t> ws;
        for (const FamilyVariant& x : families[fi].variants) if (std::find(ws.begin(), ws.end(), x.id) == ws.end()) ws.push_back(x.id);
        for (uint16_t w : ws) {
            uint8_t have = hidden_mask[w];
            for (const FamilyVariant& x : families[fi].variants) if (x.id == w) have |= uint8_t(1u << x.variant);
            if (have == 0xF) continue;
            if (w == families[fi].slot) {
                for (uint8_t v = 0; v < 4; v++) if (!(have & (1u << v))) families[fi].variants.push_back({ w, v });
                fprintf(stderr, "[families] family %zu, %s: %s's variants it doesn't list go at its end: %s\n", fi + 1,
                        families[fi].name.c_str(), name_of(w).c_str(), list_of(families[fi]).c_str());
                continue;
            }
            Family r;
            r.name = name_of(w);
            r.slot = w;
            for (uint8_t v = 0; v < 4; v++) if (!(have & (1u << v))) r.variants.push_back({ w, v });
            rest_of_id[w] = int32_t(families.size());
            fprintf(stderr, "[families] %s's variants family %zu (%s) doesn't list keep his own box: %s\n", name_of(w).c_str(), fi + 1,
                    families[fi].name.c_str(), list_of(r).c_str());
            families.push_back(std::move(r));
        }
    }
    // A wrestler in no family with a variant hidden (not all four): a family of his own of the rest, so the Type arrows pass
    // over it.
    for (uint32_t id = 1; id < 0x1000; id++) {
        if (!hidden_mask[id] || hidden_mask[id] == 0xF || family_of_id[id] >= 0) continue;
        Family f;
        f.name = name_of(uint16_t(id));
        f.slot = uint16_t(id);
        add_four(f, uint16_t(id));
        family_of_id[id] = int32_t(families.size());
        fprintf(stderr, "[families] family %zu, %s (his variants not hidden): %s\n", families.size() + 1, f.name.c_str(), list_of(f).c_str());
        families.push_back(std::move(f));
    }
    size_t n = 0;
    for (const Family& f : families) n += f.variants.size();
    fprintf(stderr, "[families] %zu families, %zu variants, from %s\n", families.size(), n,
            families_from_files ? "families.json" : "the default data (no families.json: the families the game always had)");
}

// ---- rivals (New feature (Ultimate No Mercy) -- rostertags, 2026-11-21) ------------------------------------------------------
// Who is whose rival, beyond the game's own (each No Mercy moveset's "Rival 1-3": the mod reads those itself, roster.c). As
// families.json: rivals.json in the wrestlers folder itself and/or in any collection folder, every one read (the wrestlers
// folder's first, then the collections' in case-insensitive path order). The importers write one into each collection they
// make (wrestlers\Revenge\rivals.json, wrestlers\WrestleMania 2000\rivals.json: each game's men only); a player's own go in
// wrestlers\rivals.json (never written by anything). The format:
//   {"rivals": [ ["goldberg", "hollywood_hogan"],                                 a pair: two wrestlers, either way round
//                {"pair": ["sting", "hollywood_hogan"], "why": "Starrcade 1997"}, ... the same with a note
//              ],
//    "not_rivals": [ ["the_rock", "triple_h"] ] }                                 optional: a pair that is NOT (the game's own too)
// A wrestler: a string ID, a game number ("0x0105") or {"id": ..., "game_number": ...} as in families.json. In a collection's
// file a string ID is first the folder of that name inside that collection (so Revenge's file says "chris_jericho" for its
// own man even where the built-in Chris Jericho has that ID), then anyone's. A later file's word on a pair wins ("not_rivals"
// over "rivals" in the same file). Rivals are by man: a variant's family head is asked as well (the mod).
void rivals_read(const std::filesystem::path& root, std::vector<std::filesystem::path> files, const std::map<std::string, uint16_t>& sids,
                 const std::vector<bool>& used, const std::map<std::filesystem::path, uint16_t>& number_of_folder) {
    rival_pairs.clear();
    const auto folded = [&](const std::filesystem::path& p) {
        std::string s = utf8(p.lexically_relative(root));
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32);
        return s;
    };
    std::sort(files.begin(), files.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
        const std::string fa = folded(a), fb = folded(b);
        return fa != fb ? fa < fb : utf8(a) < utf8(b);
    });
    std::error_code ec;
    if (std::filesystem::exists(root / "rivals.json", ec)) files.insert(files.begin(), root);
    size_t n_yes = 0, n_no = 0;
    for (const auto& folder : files) {
        const std::filesystem::path path = folder / "rivals.json";
        const std::string where = arcade::shown(path);
        std::ifstream f(path);
        const json j = f.good() ? json::parse(f, nullptr, false) : json();
        if (j.is_discarded() || !j.is_object()) { fprintf(stderr, "[rivals] %s isn't valid JSON: not read\n", where.c_str()); continue; }
        const bool collection = folder != root;
        const std::string inside = utf8(folder) + "/";
        const auto by_sid = [&](const std::string& raw) -> uint16_t {
            const std::string id = clean_id(raw);
            if (collection)      // (this collection's own folder of that name first)
                for (const auto& [p, n] : number_of_folder) {
                    std::string ps = utf8(p);
                    for (char& c : ps) if (c == '\\') c = '/';
                    std::string in = inside;
                    for (char& c : in) if (c == '\\') c = '/';
                    if (ps.rfind(in, 0) != 0) continue;
                    if (clean_id(utf8(p.filename())) == id) return n;
                }
            const auto s = sids.find(id);
            return s != sids.end() ? s->second : 0;
        };
        const auto who = [&](const json& v) -> uint16_t {
            if (v.is_object()) {
                if (v.contains("id") && v["id"].is_string()) { const uint16_t n = by_sid(v["id"].get<std::string>()); if (n) return n; }
                if (v.contains("game_number")) { const int64_t n = number_of(v["game_number"]); return n > 0 && n < 0x1000 && used[size_t(n)] ? uint16_t(n) : 0; }
                return 0;
            }
            const int64_t n = number_of(v);
            if (n > 0 && n < 0x1000) return used[size_t(n)] ? uint16_t(n) : 0;
            return v.is_string() ? by_sid(v.get<std::string>()) : 0;
        };
        size_t here = 0;
        for (const char* key : { "rivals", "not_rivals" }) {
            if (!j.contains(key)) continue;
            if (!j[key].is_array()) { fprintf(stderr, "[rivals] %s: \"%s\" must be a list: not read\n", where.c_str(), key); continue; }
            const bool yes = key[0] == 'r';
            for (size_t k = 0; k < j[key].size(); k++) {
                const json& e = j[key][k];
                const json& pr = e.is_object() && e.contains("pair") ? e["pair"] : e;
                if (!pr.is_array() || pr.size() != 2) { fprintf(stderr, "[rivals] %s, %s %zu: a pair is a list of two wrestlers: not read\n", where.c_str(), key, k + 1); continue; }
                const uint16_t a = who(pr[0]), b = who(pr[1]);
                if (!a || !b || a == b) {
                    fprintf(stderr, "[rivals] %s, %s %zu (%s): %s: not read\n", where.c_str(), key, k + 1, pr.dump().c_str(),
                            a == b && a ? "the same wrestler twice" : "no such wrestler (not imported, or another ID)");
                    continue;
                }
                const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
                rival_pairs[lo << 12 | hi] = yes ? 1 : -1;
                (yes ? n_yes : n_no)++;
                here++;
            }
        }
        fprintf(stderr, "[rivals] %s: %zu pairs read\n", where.c_str(), here);
    }
    size_t n_pairs = 0;
    for (const auto& [k, v] : rival_pairs) n_pairs += v > 0;
    fprintf(stderr, "[rivals] %zu rival pairs from rivals.json files (%zu named, %zu taken out), besides the game's own Rival 1-3\n", n_pairs, n_yes, n_no);
}

void scan() {
    if (scanned) return;
    scanned = true;
    const char* from_environment = getenv("WCW_WRESTLERS_DIR");       // (QA: the test wrestlers)
    const std::filesystem::path root = (from_environment && from_environment[0]) ? std::filesystem::path(from_environment) : arcade::program_folder() / "wrestlers";
    std::error_code ec;
    std::vector<std::filesystem::path> folders;
    std::vector<std::filesystem::path> rosters;    // (2026-11-13, removal: the collections with a stock_roster.json)
    std::vector<std::filesystem::path> family_files;   // (2026-11-14, families: the collections with a families.json)
    std::vector<std::filesystem::path> rival_files;    // (rostertags, 2026-11-21: the collections with a rivals.json)
    class_of_id.assign(0x1000, 0);
    std::map<std::filesystem::path, std::filesystem::path> nested_in;   // (2026-11-14, families: a nested wrestler's folder ->
    std::filesystem::path top;                                          //  the outermost wrestler's folder he is in)
    int top_depth = -1;
    if (std::filesystem::exists(root / "stock_roster.json", ec)) rosters.push_back(root);
    ec.clear();
    // Folders at any depth (2026-11-09, made to work 2026-11-12). One with a wrestler.json is a wrestler, and nothing inside
    // it is looked at as one (his own sub-folders are his). Any other is the user's own organisation -- WCW, Japan, 80s,
    // nested as deep as they like -- and is in no one's ID. A folder starting with _ or . is skipped with all it holds
    // (wrestlers\_example, a parked collection). Links to folders aren't followed, so a loop on disk can't trap the scan.
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::string leaf = utf8(it->path().filename());
        if (leaf.empty() || leaf[0] == '_' || leaf[0] == '.') { it.disable_recursion_pending(); continue; }
        // (2026-11-14, families: NESTING. A wrestler's folder inside another wrestler's folder -- a folder of his own there
        //  with a wrestler.json -- is a wrestler like any other (his ID, number, clash rules: the same), and his family is the
        //  outer one's: the outer wrestler's variants, then his. Deeper still joins the same family: everything is nested
        //  in the OUTERMOST wrestler's folder ("top"). Inside a wrestler's folder a folder without a wrestler.json is his own
        //  (model\, moves\ ...), never a collection: no stock_roster.json or families.json is read there. So the scan goes
        //  on into a wrestler's folder now, where it used to stop.)
        if (top_depth >= 0 && it.depth() <= top_depth) top_depth = -1;      // (out of the top wrestler's folder again)
        if (std::filesystem::exists(it->path() / "wrestler.json", e2)) {
            folders.push_back(it->path());
            if (top_depth >= 0) nested_in[it->path()] = top;
            else { top = it->path(); top_depth = it.depth(); }
        } else if (top_depth >= 0) continue;
        else {
            if (std::filesystem::exists(it->path() / "stock_roster.json", e2)) rosters.push_back(it->path());
            if (std::filesystem::exists(it->path() / "families.json", e2)) family_files.push_back(it->path());   // (2026-11-14, families)
            if (std::filesystem::exists(it->path() / "rivals.json", e2)) rival_files.push_back(it->path());      // (rostertags, 2026-11-21)
        }
    }
    // (2026-11-13, removal) Each collection's stock list: the men it holds, by their frozen IDs. (Nothing is written here:
    // a removed man comes back only by the importer writing his folder again -- the user's R17.)
    std::vector<std::pair<std::filesystem::path, std::vector<std::string>>> stock_lists;
    for (const auto& c : rosters) {
        std::vector<std::string> ids;
        {
            std::ifstream f(c / "stock_roster.json");
            const json j = f.good() ? json::parse(f, nullptr, false) : json();
            if (j.is_object() && j.contains("builtin") && j["builtin"].is_array())
                for (const auto& x : j["builtin"]) if (x.is_string() && !x.get<std::string>().empty()) ids.push_back(clean_id(x.get<std::string>()));
            if (ids.empty()) { fprintf(stderr, "[wrestlers] %s: no \"builtin\" list of string IDs: nobody in it is taken as removed\n", arcade::shown(c / "stock_roster.json").c_str()); continue; }
        }
        stock_lists.push_back({ c, ids });
    }
    if (ec && std::filesystem::exists(root)) fprintf(stderr, "[wrestlers] reading %s stopped: %s (the folders found so far are used)\n", arcade::shown(root).c_str(), ec.message().c_str());   // (privacy 2026-11-13: relative to the game folder)
    std::sort(folders.begin(), folders.end());    // The same order every time, whatever the disk gives us.
    // The built-in Superstars: their string IDs and numbers (stories\_roster.json's "@ID:<id>": number, from the mod's
    // gen/roster_ids.json), their names, and every number the roster uses, so a folder's number never lands on one.
    // (2026-11-09: a folder whose number was a built-in Superstar's took his place wherever a number is looked up, without a
    //  word; a second folder with a number already used was skipped. Now both always load.)
    std::map<std::string, uint16_t> sids, names;
    std::map<std::string, std::string> where;      // a folder wrestler's ID -> his folder (for the clash message)
    std::map<uint16_t, std::string> whose;         // a game number -> whose it is (for the clash message)
    std::vector<bool> used(0x1000, false);
    std::vector<bool> is_builtin(0x1000, false);    // (the numbers _roster.json gives a string ID: the built-in Superstars)
    {
        std::ifstream f(arcade::program_folder() / "stories" / "_roster.json");
        const json r = f.good() ? json::parse(f, nullptr, false) : json();
        if (r.is_object()) for (auto it = r.begin(); it != r.end(); ++it) {
            if (!it.value().is_number_integer() || it.key().empty()) continue;
            const int64_t v = it.value().get<int64_t>();
            if (v <= 0 || v >= 0x1000) continue;
            const bool is_id = it.key().rfind("@ID:", 0) == 0;
            if (it.key().rfind("@RETIRED:", 0) == 0) {           // (no2k15: one taken out for good -- sidecar_get; not a number in use)
                const std::string k = it.key().substr(9);
                if (k.size() > 2 && k[0] == '0' && (k[1] == 'x' || k[1] == 'X')) retired_numbers.insert(uint16_t(strtol(k.c_str() + 2, nullptr, 16)));
                else retired_sids.insert(k);
                continue;
            }
            if (!is_id && it.key()[0] == '@') continue;        // (arenas and the like: not wrestlers' numbers)
            used[size_t(v)] = true;
            if (is_id) { sids[it.key().substr(4)] = uint16_t(v); whose[uint16_t(v)] = it.key().substr(4); is_builtin[size_t(v)] = true; }
            else { names.emplace(it.key(), uint16_t(v)); roster_names_of[uint16_t(v)].push_back(it.key()); }   // (automerge: the editor's builtins())
        }
        if (sids.empty()) fprintf(stderr, "[wrestlers] %s has no built-in IDs: \"based_on\" works by number or name only, and a folder's number may land on a built-in's\n",
                                  (std::filesystem::path("stories") / "_roster.json").string().c_str());   // [deck 2026-11-13] the name with this system's folder separator (stories/_roster.json on Linux; on Windows the same text as before)
    }
    // (startup 2026-11-19) The numbers folders pin ("id": "0x0379" -- every import's, frozen so saves and stories keep naming
    // him), all of them, before any folder is given one: a folder with no number never gets a number another folder pins,
    // whatever their order on disk (before, a folder sorting first could hash onto an import's number and that import was
    // renumbered as he loaded). Filled below, before the folders are numbered.
    std::vector<bool> pinned(0x1000, false);
    // A free number, in the group of the one he's based on (his faction), from his string ID: the same every time. Groups 0-6:
    // 7-9 are the game's managers and officials, 0xA and 0xB its Create-a-Wrestlers. Never one a folder pins (above).
    const auto free_id = [&](const std::string& sid, uint16_t based_on) -> uint16_t {
        uint32_t h = 2166136261u;
        for (char c : sid) { h ^= uint8_t(c); h *= 16777619u; }
        const int g0 = (based_on >> 8) & 0xF;
        for (int gi = 0; gi < 7; gi++) {
            const int g = (g0 <= 6 ? g0 + gi : gi) % 7;
            for (int i = 0; i < 0x100; i++) {
                const int id = (g << 8) | int((h + uint32_t(i)) & 0xFF);
                if ((id & 0xFF) && !used[size_t(id)] && !pinned[size_t(id)]) return uint16_t(id);
            }
        }
        return 0;
    };
    std::map<std::filesystem::path, uint16_t> number_of_folder;    // (2026-11-14, families: each loaded folder's game number)
    std::map<uint16_t, std::string> replaced;                       // (2026-11-14, revfix: a built-in's number -> the folder that replaces him)
    std::vector<std::pair<std::filesystem::path, Wrestler>> read;
    for (const auto& folder : folders) {
        Wrestler w;
        const std::string wrong = read_one(folder / "wrestler.json", w);
        if (!wrong.empty()) { fprintf(stderr, "[wrestlers] %s: wrestler.json %s: skipped\n", arcade::shown(folder).c_str(), wrong.c_str()); continue; }
        read.push_back({ folder, std::move(w) });
    }
    // A stock wrestler's folder that carries the built-in's own ID (as the export writes it) goes first, so it is he,
    // whatever collection it sorts in; any other folder for him is a copy. The rest stay in path order. (2026-11-12)
    std::stable_partition(read.begin(), read.end(), [&](const std::pair<std::filesystem::path, Wrestler>& x) {
        const Wrestler& w = x.second;
        if (!w.stock) return false;
        auto b = w.based_on_sid.empty() ? sids.end() : sids.find(clean_id(w.based_on_sid));
        const uint16_t man = w.based_on ? w.based_on : b != sids.end() ? b->second : 0;
        const auto own = whose.find(man);
        return man && is_builtin[man] && own != whose.end() && clean_id(w.sid.empty() ? utf8(x.first.filename()) : w.sid) == own->second;
    });
    // (startup 2026-11-19) the pinned numbers: every folder's own "id" number (read_one keeps only a usable one: 1 .. 0xFFF,
    // no Create-a-Wrestler group); a built-in's number is already used
    for (const auto& [folder, w] : read)
        if (w.id && !w.stock && w.replaces.empty()) pinned[w.id] = true;
    for (auto& [folder, w] : read) {
        const std::string place = arcade::shown(folder), json_file = arcade::shown(folder / "wrestler.json");   // (for the log: privacy 2026-11-13)
        if (!w.based_on_sid.empty()) {         // "based_on" by string ID, else by name ("STEVE AUSTIN")
            auto b = sids.find(clean_id(w.based_on_sid));
            if (b == sids.end()) {
                std::string u = w.based_on_sid;
                for (char& c : u) if (c >= 'a' && c <= 'z') c = char(c - 32);
                b = names.find(u);
                if (b == names.end()) { fprintf(stderr, "[wrestlers] %s: \"based_on\": no wrestler %s: skipped\n", place.c_str(), w.based_on_sid.c_str()); continue; }
            }
            w.based_on = b->second;
        }
        // A stock wrestler's folder ("builtin", what the stock export writes): the first folder for a built-in Superstar is
        // him -- no new wrestler, his ID is the folder's, his old ID stays too. Another folder for the same man is a copy of
        // him like any other: a wrestler of his own, based on him, both on the roster.
        if (w.stock) {
            const bool built_in = w.based_on && is_builtin[w.based_on] && whose.count(w.based_on);
            const bool first = built_in && std::none_of(stock_ids.begin(), stock_ids.end(), [&](const auto& x) { return x.second == w.based_on; });
            if (!built_in) fprintf(stderr, "[wrestlers] %s: \"builtin\": 0x%04X isn't a built-in Superstar: a wrestler of his own, a copy of him\n", place.c_str(), w.based_on);
            else if (first) {
                std::string sid = clean_id(w.sid.empty() ? utf8(folder.filename()) : w.sid);
                if (sids.count(sid) && sids[sid] != w.based_on) {
                    int n = 2;
                    while (sids.count(sid + "~" + std::to_string(n))) n++;
                    fprintf(stderr, "[wrestlers] ID CLASH: %s: the ID %s is already %s's: this one is %s~%d (to give him another, set \"id\" in %s)\n",
                            place.c_str(), sid.c_str(), where.count(sid) ? where[sid].c_str() : "another built-in Superstar", sid.c_str(), n, json_file.c_str());
                    sid += "~" + std::to_string(n);
                }
                sids[sid] = w.based_on;
                where[sid] = place;
                stock_ids.push_back({ sid, w.based_on });
                stock_seen.push_back({ w.based_on, sid, whose[w.based_on], w.has_name, w.full_name, w.source_game });   // (automerge)
                if (w.cls) class_of_id[w.based_on] = w.cls;     // (rostertags: his folder's "class" over the built-in's)
                number_of_folder[folder] = w.based_on;     // (2026-11-14, families: nesting)
                // (2026-11-13, nmhandler) His data, if his folder has it: the game loads him from it (stock_folders).
                read_stock_photos(folder, w);
                if (stock_has_data(w)) {
                    w.id = w.based_on;
                    w.sid = sid;
                    w.folder = folder;
                    int photos = 0;
                    for (int c = 0; c < 4; c++) photos += !(c == 0 ? w.photo : w.photo_c[c]).empty();
                    fprintf(stderr, "[wrestlers] %s: %s (0x%04X), the built-in %s himself, loaded from his folder:%s%s%s%s %d photo%s (the built-in's for anything it doesn't give)\n",
                            place.c_str(), sid.c_str(), w.based_on, whose[w.based_on].c_str(),
                            (w.has_name || w.height >= 0 || w.weight >= 0 || w.music >= 0 || w.video >= 0) ? " record" : "",
                            w.per_costume.empty() ? "" : " per_costume", w.looks.empty() ? "" : " looks", w.moves.empty() ? "" : " moveset",
                            photos, photos == 1 ? "" : "s");
                    if (!w.keys.empty()) fprintf(stderr, "[wrestlers] %s: \"costumes\" (packed looks) isn't read for a stock folder: give \"looks\"\n", place.c_str());
                    stock_folders.push_back(std::move(w));
                } else fprintf(stderr, "[wrestlers] %s: %s (0x%04X), the built-in %s himself\n", place.c_str(), sid.c_str(), w.based_on, whose[w.based_on].c_str());
                continue;
            } else fprintf(stderr, "[wrestlers] %s: another folder for the built-in %s: a wrestler of his own, a copy of him\n", place.c_str(), whose[w.based_on].c_str());
        }
        // (2026-11-14, revfix) "replaces": the built-in's string ID and number are his (the first folder that asks; the mod
        // leaves the built-in out when a folder has his number: roster.c ext_wanted). A second folder for the same man, or
        // a name that is no built-in's, loads as a wrestler of his own, with the log saying why.
        bool took_place = false;
        if (!w.replaces.empty()) {
            const auto b = sids.find(clean_id(w.replaces));
            const uint16_t number = b != sids.end() ? b->second : 0;
            if (!number || !is_builtin[number]) fprintf(stderr, "[wrestlers] %s: \"replaces\": %s isn't a built-in Superstar: a wrestler of his own\n", place.c_str(), w.replaces.c_str());
            else if (replaced.count(number)) fprintf(stderr, "[wrestlers] %s: \"replaces\": %s is already %s: a wrestler of his own\n", place.c_str(), w.replaces.c_str(), replaced[number].c_str());
            else {
                w.sid = b->first; w.id = number; took_place = true;
                replaced[number] = place;
                where[w.sid] = place;
                fprintf(stderr, "[wrestlers] %s: %s (0x%04X) replaces the built-in %s: his folder is he now (his number and ID, his saves and "
                                "stories), the built-in's copy is off the roster\n", place.c_str(), w.sid.c_str(), number, w.sid.c_str());
            }
        }
        // His string ID: "id" in his wrestler.json, else his folder's own name (never the collections' path: moving a
        // folder changes nothing). One already taken -- a built-in's, or another folder's -- gets ~2, ~3 ...: both load.
        std::string sid = clean_id(w.sid.empty() ? utf8(folder.filename()) : w.sid);
        if (!took_place && sids.count(sid)) {
            int n = 2;
            while (sids.count(sid + "~" + std::to_string(n))) n++;
            const auto first = where.find(sid);
            fprintf(stderr, "[wrestlers] ID CLASH: %s: the ID %s is already %s's: this one is %s~%d (to give him another, set \"id\" in %s)\n",
                    place.c_str(), sid.c_str(), first == where.end() ? "a built-in Superstar" : first->second.c_str(), sid.c_str(), n, json_file.c_str());
            sid += "~" + std::to_string(n);
        }
        w.sid = sid;
        // His game number (the game's own 12-bit wrestler ID, which the game and the mod use everywhere): his old "id" number
        // if it's free, else a free one made from his string ID.
        if (!took_place && w.id && used[w.id]) {
            const uint16_t was = w.id;
            w.id = free_id(w.sid, w.based_on);
            const auto o = whose.find(was);
            fprintf(stderr, "[wrestlers] ID CLASH: %s: the number 0x%04X in his wrestler.json is already %s's: this one is 0x%04X\n",
                    place.c_str(), was, o == whose.end() ? "a built-in Superstar" : o->second.c_str(), w.id);
        }
        if (!w.id) w.id = free_id(w.sid, w.based_on);
        if (!w.id) { fprintf(stderr, "[wrestlers] %s: no game number left: skipped\n", place.c_str()); continue; }
        used[w.id] = true;
        if (w.cls) class_of_id[w.id] = w.cls;            // (rostertags, 2026-11-21)
        sids[w.sid] = w.id;
        where[w.sid] = place;
        whose[w.id] = w.sid;
        read_files(folder, w);
        w.folder = folder;
        // Changed for new features (Ultimate No Mercy): [revvisual 2026-11-20] (the user, 2026-11-20: "Revenge Wrestlers without
        //  titantrons seem to have The Rock's titantron. They should have none unless a video is in their folder.") His record
        //  is a copy of his base's, its video number with it: the Revenge imports are based on The Rock (Kane for the big men,
        //  Stephanie for the women), and WrestleMania 2000's men with no video of their own on theirs. With no "video" number
        //  in his wrestler.json and no video file in his folder, his number is 0: the game shows none for him, as for any
        //  Superstar without one (the arena's screen stays its own; the entrance's full-screen video step is passed over).
        //  A video file in his folder still beats any number (mod roster.c, arcade_media.cpp); a number he names is kept.
        if (w.video < 0 && !folder_has_video(folder)) w.video = 0;
        fprintf(stderr, "[wrestlers] %s: %s (0x%04X), based on 0x%04X%s%s%s%s\n", place.c_str(), w.sid.c_str(), w.id, w.based_on, w.photo.empty() ? "" : ", his own photo", w.hd_index < 0 ? "" : ", his own model",
                w.slots.empty() ? "" : ", his moveset as slots (moves.json)",
                w.size < 0 ? "" : (", size " + std::to_string(w.size)).c_str());   // (2026-11-13, revbody)
        number_of_folder[folder] = w.id;                // (2026-11-14, families: nesting)
        wrestlers.push_back(std::move(w));
    }
    // (2026-11-13, removal) Who is removed: a man a stock list names, by his frozen ID, with no stock folder of his anywhere.
    for (const auto& [n, s] : whose) sid_of_number[n] = s;
    for (const auto& [c, ids] : stock_lists)
        for (const std::string& id : ids) {
            const auto b = sids.find(id);
            const uint16_t number = b != sids.end() && is_builtin[b->second] ? b->second : 0;
            if (!number) { fprintf(stderr, "[wrestlers] %s lists %s, who isn't a built-in Superstar: not taken as removed\n", arcade::shown(c / "stock_roster.json").c_str(), id.c_str()); continue; }
            if (std::any_of(stock_ids.begin(), stock_ids.end(), [&](const auto& x) { return x.second == number; })) continue;
            if (std::any_of(removed.begin(), removed.end(), [&](const Removed& r) { return r.id == number; })) continue;
            removed.push_back({ id, number, c });
            fprintf(stderr, "[wrestlers] REMOVED: %s (0x%04X): %s lists him and his folder isn't there -- he is off the roster (no box, never drawn"
                            " or cast; a title he holds is vacated, a preset or story that needs him plays a stand-in, a family he heads stays, headed by the next of it:"
                            " the mod's [removal] lines). To bring him back: run the importer again for him (nm_import.py --only %s)\n",
                    id.c_str(), number, arcade::shown(c / "stock_roster.json").c_str(), id.c_str());
        }
    if (!removed.empty()) {
        for (const Wrestler& w : wrestlers)
            for (const Removed& r : removed)
                if (w.based_on == r.id)
                    fprintf(stderr, "[wrestlers] %s is based on %s, who is removed: he still starts from %s's built-in data (always there)\n", w.sid.c_str(), r.sid.c_str(), r.sid.c_str());
        fprintf(stderr, "[wrestlers] %zu stock Superstar%s removed\n", removed.size(), removed.size() == 1 ? "" : "s");
    }
    folder_of_id.assign(0x1000, -1);
    for (size_t i = 0; i < wrestlers.size(); i++) if (wrestlers[i].id < 0x1000) folder_of_id[wrestlers[i].id] = int32_t(i);
    std::vector<std::pair<uint16_t, std::vector<uint16_t>>> nests;   // (2026-11-14, families: top -> his nested, in order)
    {
        std::map<std::filesystem::path, std::vector<std::filesystem::path>> by_top;
        for (const auto& [in, t] : nested_in) by_top[t].push_back(in);
        for (auto& [t, ins] : by_top) {
            const auto folded = [&](const std::filesystem::path& p) {
                std::string x = utf8(p.lexically_relative(t));
                for (char& c : x) if (c >= 'A' && c <= 'Z') c = char(c + 32);
                return x;
            };
            std::sort(ins.begin(), ins.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                const std::string fa = folded(a), fb = folded(b);
                return fa != fb ? fa < fb : utf8(a) < utf8(b);
            });
            const auto tn = number_of_folder.find(t);
            std::vector<uint16_t> kids;
            for (const auto& in : ins) {
                const auto n = number_of_folder.find(in);
                if (n == number_of_folder.end()) continue;     // (skipped: the log said why)
                if (tn == number_of_folder.end())
                    fprintf(stderr, "[families] %s is nested in %s, which isn't loaded: no family, a wrestler of his own\n",
                            arcade::shown(in).c_str(), arcade::shown(t).c_str());
                else kids.push_back(n->second);
            }
            if (tn != number_of_folder.end() && !kids.empty()) nests.push_back({ tn->second, kids });
        }
    }
    families_read(root, family_files, sids, used, whose, nests);     // (2026-11-14, families)
    rivals_read(root, rival_files, sids, used, number_of_folder);     // (rostertags, 2026-11-21)
    fprintf(stderr, "[wrestlers] %zu folder wrestlers, all on the roster (2026-11-13: seats given as the game needs them)\n", wrestlers.size());
    fflush(stderr);
    if (!low_models.empty()) std::thread(low_prefetch).detach();     // (2026-11-13, handlers1: their files read ahead, above)
}

// recomp_arcade_wrestler_find(id) -> the folder with this game number, or -1 (the mod's staging seats: roster.c ext_of_id).
void wrestler_find(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    scan();
    _return<int32_t>(ctx, id < folder_of_id.size() ? folder_of_id[id] : -1);
}
// recomp_arcade_wrestler_id(i) -> folder i's game number (0: none) -- the mod's pages, without fetching the whole folder.
void wrestler_id(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    scan();
    _return<uint32_t>(ctx, i >= 0 && size_t(i) < wrestlers.size() ? wrestlers[size_t(i)].id : 0u);
}
// ---- the save sidecar (2026-11-13, seatsroot; the mod's save.c) ----------------------------------------------------------
// The game's save keeps a champion as a 7-bit ordinal, which no folder wrestler's can be. The mod writes the titles whose
// champion is one of ours here instead, and this keeps them beside the save, <save>.ours.json, by string ID: a folder's
// number changes when folders are added, his string ID doesn't. TEMPORARY over the game's save format, until Stage 2's
// native records and a save codec of our own. The mod's buffer: 'S' 'C' 1 n, then n x 12 bytes { kind (1: a title's
// champion), title, 0 champion / 1 partner, costume, folder (big-endian; FF FE <game number>: a built-in port), the
// title's bytes' check }.
std::filesystem::path sidecar_path() {
    std::filesystem::path p = ultramodern::get_save_file_path();
    p += ".ours.json";
    return p;
}
// recomp_arcade_sidecar_put(buf, len): the mod's sidecar as the save is written (save.c save_sanitize).
void sidecar_put(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) buf = _arg<0, PTR(void)>(rdram, ctx);
    const int32_t len = _arg<1, int32_t>(rdram, ctx);
    scan();
    const auto b = [&](int i) { return uint32_t(uint8_t(MEM_B(i, buf))); };
    if (len < 4 || b(0) != 'S' || b(1) != 'C' || b(2) != 1) return;
    const int n = int(b(3));
    const std::filesystem::path path = sidecar_path();
    std::error_code ec;
    if (n == 0 && !std::filesystem::exists(path, ec)) return;     // (nothing of ours, and nothing written before: no file)
    json out = { { "about", "Ultimate No Mercy: what the save can't hold -- its champions from folders and the built-in ports, by"
                            " string ID (written with the save; read back with it; a title changed since keeps the save's)" },
                 { "version", 1 }, { "titles", json::array() } };
    for (int k = 0; k < n && 4 + 12 * k + 12 <= len; k++) {
        const int e = 4 + 12 * k;
        if (b(e) != 1) continue;
        const uint32_t f = b(e + 4) << 24 | b(e + 5) << 16 | b(e + 6) << 8 | b(e + 7);
        const uint32_t check = b(e + 8) << 24 | b(e + 9) << 16 | b(e + 10) << 8 | b(e + 11);
        json t = { { "title", b(e + 1) }, { "role", b(e + 2) ? "partner" : "champion" }, { "costume", b(e + 3) }, { "check", check } };
        // (startup 2026-11-19) both his string ID and his game number, whichever the mod gave: read back by the string ID
        // whenever a folder has it (sidecar_get; the user: references fall back to the string ID)
        char h[8];
        if ((f >> 16) == 0xFFFEu) {
            snprintf(h, sizeof h, "0x%04X", f & 0xFFFFu); t["game_number"] = h;
            const auto s = sid_of_number.find(uint16_t(f & 0xFFFFu));
            if (s != sid_of_number.end()) t["id"] = s->second;
        } else if (f < wrestlers.size()) {
            t["id"] = wrestlers[f].sid;
            snprintf(h, sizeof h, "0x%04X", unsigned(wrestlers[f].id)); t["game_number"] = h;
        } else continue;
        fprintf(stderr, "[sidecar] title %u's %s: %s\n", b(e + 1), b(e + 2) ? "partner" : "champion",
                t.contains("id") ? t["id"].get<std::string>().c_str() : t["game_number"].get<std::string>().c_str());
        out["titles"].push_back(t);
    }
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) { fprintf(stderr, "[sidecar] can't write %s\n", path.filename().string().c_str()); fflush(stderr); return; }
        o << out.dump(1) << "\n";
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) fprintf(stderr, "[sidecar] can't write %s: %s\n", path.filename().string().c_str(), ec.message().c_str());
    else fprintf(stderr, "[sidecar] written with the save: %zu of ours\n", out["titles"].size());
    fflush(stderr);
}
// recomp_arcade_sidecar_get(buf, max) -> bytes: the sidecar back as the mod's buffer, folders by their numbers now (a
// string ID no folder has any more: left out, said in the log).
void sidecar_get(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) buf = _arg<0, PTR(void)>(rdram, ctx);
    const int32_t max = _arg<1, int32_t>(rdram, ctx);
    scan();
    std::ifstream in(sidecar_path(), std::ios::binary);
    if (!in || max < 4) { _return<int32_t>(ctx, 0); return; }
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.contains("titles") || !j["titles"].is_array()) {
        fprintf(stderr, "[sidecar] %s isn't one: not read\n", sidecar_path().filename().string().c_str());
        fflush(stderr);
        _return<int32_t>(ctx, 0);
        return;
    }
    std::map<std::string, size_t> by_sid;
    for (size_t i = 0; i < wrestlers.size(); i++) by_sid[wrestlers[i].sid] = i;
    int at = 4, n = 0;
    const auto put = [&](uint32_t v) { MEM_B(at++, buf) = int8_t(v); };
    for (const json& t : j["titles"]) {
        if (at + 12 > max || !t.is_object()) break;
        uint32_t f = 0;
        // (startup 2026-11-19) An entry may have both (sidecar_put writes both now): the string ID decides whenever a folder
        // has it -- his folder, whatever number he has now; the game number only when no folder has the string ID (the
        // user: references fall back to the string ID). An entry with only one of them reads as it always did.
        const int64_t g = t.contains("game_number") ? number_of(t["game_number"]) : -1;
        const bool has_g = g > 0 && g < 0x10000;
        // (no2k15, 2026-11-23) a champion taken out of the game for good (retired_numbers): no folder has his string ID, so
        // the title is VACATED -- the game's rule for a removed wrestler's title (mod save.c: FF FD <his old number>)
        const bool sid_here = t.contains("id") && t["id"].is_string() && by_sid.count(t["id"].get<std::string>());
        const bool retired = !sid_here && ((t.contains("id") && t["id"].is_string() && retired_sids.count(t["id"].get<std::string>())) ||
                                           (has_g && g < 0x1000 && retired_numbers.count(uint16_t(g))));
        if (retired) {
            f = 0xFFFD0000u | uint32_t(has_g ? g & 0xFFFF : 0);
            fprintf(stderr, "[sidecar] %s: taken out of the game (mod gen/retired_ids.json) -- his title is vacated\n",
                    t.contains("id") && t["id"].is_string() ? t["id"].get<std::string>().c_str() : t["game_number"].dump().c_str());
        } else if (t.contains("id") && t["id"].is_string()) {
            const auto it = by_sid.find(t["id"].get<std::string>());
            if (it != by_sid.end()) {
                f = uint32_t(it->second);
                if (has_g && wrestlers[it->second].id != uint16_t(g))
                    fprintf(stderr, "[sidecar] %s: game number 0x%04X isn't his now: found by his string ID (0x%04X)\n",
                            t["id"].get<std::string>().c_str(), unsigned(g), unsigned(wrestlers[it->second].id));
            } else if (has_g) {
                f = 0xFFFE0000u | uint32_t(g);
            } else { fprintf(stderr, "[sidecar] %s: no such folder now -- that title stays as the save has it\n", t["id"].get<std::string>().c_str()); continue; }
        } else if (has_g) {
            f = 0xFFFE0000u | uint32_t(g);
        } else continue;
        const uint32_t check = t.value("check", 0u);
        put(1); put(t.value("title", 0u)); put(t.value("role", std::string()) == "partner" ? 1 : 0); put(t.value("costume", 0u));
        put(f >> 24); put(f >> 16); put(f >> 8); put(f);
        put(check >> 24); put(check >> 16); put(check >> 8); put(check);
        n++;
    }
    MEM_B(0, buf) = 'S'; MEM_B(1, buf) = 'C'; MEM_B(2, buf) = 1; MEM_B(3, buf) = int8_t(n);
    fprintf(stderr, "[sidecar] read with the save: %d of ours\n", n);
    fflush(stderr);
    _return<int32_t>(ctx, at);
}

// recomp_arcade_seat_note(kind, seat, folder, other): the mod's staging seats, in the log (roster.c SEAT_NOTE_*). The
// takes are counted, not each printed past the first 400 (a long run hands out thousands): a line every 100 after that.
void seat_note(uint8_t* rdram, recomp_context* ctx) {
    const int32_t kind = _arg<0, int32_t>(rdram, ctx), seat = _arg<1, int32_t>(rdram, ctx);
    const int32_t folder = _arg<2, int32_t>(rdram, ctx), other = _arg<3, int32_t>(rdram, ctx);
    static uint32_t takes, backs, in_match;
    const auto who = [](int32_t f) -> std::string {
        if (f < 0 || size_t(f) >= wrestlers.size()) return "nobody";
        char b[16]; snprintf(b, sizeof b, " (0x%04X)", wrestlers[size_t(f)].id);
        return wrestlers[size_t(f)].sid + b;
    };
    switch (kind) {
    case 1:
        fprintf(stderr, "[seats] %d folder wrestlers, %d seats: seats are given as the game needs them and taken back when"
                        " nothing names their wrestler (TEMPORARY: the game's one-byte ordinals; root fix Stage 2's wide IDs)\n", folder, seat);
        break;
    case 2:
        takes++;
        if (other >= 0) backs++;
        if (takes <= 400 || takes % 100 == 0)
            fprintf(stderr, "[seats] seat %d (ordinal %d): %s%s%s  [taken %u, taken back %u]\n", seat, 142 + seat, who(folder).c_str(),
                    other >= 0 ? ", in place of " : "", other >= 0 ? who(other).c_str() : "", takes, backs);
        break;
    case 3:
        fprintf(stderr, "[seats] NO SEAT for %s: all of them are in use at once (refused %d so far)\n", who(folder).c_str(), other);
        break;
    case 4:
        in_match++;
        fprintf(stderr, "[seats] seat %d taken in a match for %s (%u so far): his records were made from what was there\n", seat, who(folder).c_str(), in_match);
        break;
    case 5:
        fprintf(stderr, "[seats] the select's pages: a window of %d on %d pages\n", seat, folder);
        break;
    case 6:     // (QA: a title's champion -- roster.c qa_seats_frame)
        fprintf(stderr, "[seats] title %d: champion %s (ordinal %d)\n", seat, folder >= 0 ? who(folder).c_str() : "the game's", other);
        break;
    default:
        fprintf(stderr, "[seats] note %d: %d %d %d\n", kind, seat, folder, other);
    }
    fflush(stderr);
}

// The full-detail models handed out (2026-11-13, seats; seatsroot): every folder can be seated now, so every model is
// handed out -- the first HD_MAX folders that have one, in the order of `wrestlers`, as before seats' guard. Worked out
// once, at the first call that hands one out (the mod's model count and meta, a wrestler's model flag, the content packs,
// the theme pack numbers), so all of those agree.
size_t hd_handed() {
    static size_t n = SIZE_MAX;
    if (n != SIZE_MAX) return n;
    scan();
    size_t handed = 0;
    for (size_t i = 0; i < wrestlers.size(); i++) {
        if (wrestlers[i].hd_index >= 0) handed++;
    }
    n = handed;
    fflush(stderr);
    return n;
}

// recomp_arcade_wrestler_count() -> how many folders were read.
void wrestler_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(wrestlers.size()));
}

// recomp_arcade_wrestler_get(i, dst): writes wrestler i into the mod's buffer. Its layout is RTW_* in mod src/roster.c,
// byte for byte, so nothing here depends on how either side packs a struct.
void wrestler_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= wrestlers.size()) return;
    const Wrestler& w = wrestlers[size_t(i)];
    int at = 0;
    const auto put8 = [&](int v) { MEM_B(at++, dst) = int8_t(v); };
    const auto put16 = [&](int v) { put8(v >> 8); put8(v); };
    const auto put_text = [&](const std::string& s, int bytes) { for (int k = 0; k < bytes; k++) put8(size_t(k) < s.size() ? s[size_t(k)] : 0); };
    put16(w.id);
    put16(w.based_on);
    put16(w.weight);            // Offset 4, with 0xFFFF meaning "keep".
    put8(w.height);             // These three with 0xFF meaning "keep".
    put8(w.music);
    put8(w.video);
    const bool hd = w.hd_index >= 0 && size_t(w.hd_index) < hd_handed();     // (every model is handed out: hd_handed. 2026-11-13)
    put8((w.keys.empty() ? 0 : 1) | (w.moves.empty() ? 0 : 2) | (w.photo.empty() ? 0 : 4) | (hd ? 8 : 0) |
         (w.looks.empty() ? 0 : 0x10) | (w.no_nick ? 0x20 : 0));     // (0x10 looks as numbers, 0x20 no nickname: 2026-11-13)
    put8(hd ? w.hd_index : 0);
    put_text(w.name, NAME_BYTES);
    put_text(w.short_name, SHORT_BYTES);
    put_text(w.nick, NICK_BYTES);
    for (int k = 0; k < 4 * KEY_BYTES; k++) put8(w.keys.empty() ? 0 : w.keys[size_t(k)]);
    for (int k = 0; k < MOVES_BYTES; k++) put8(w.moves.empty() ? 0 : w.moves[size_t(k)]);
    for (int k = 0; k < PHOTO_BYTES; k++) put8(w.photo.empty() ? 0 : w.photo[size_t(k)]);
    for (int k = 0; k < 4 * LOOK_FIELDS; k++) put8(w.looks.empty() ? 0 : w.looks[size_t(k)]);   // (RTW_LOOKS, 2026-11-13)
    put8(w.size < 0 ? 0 : 1);   // (RTW_SIZE, 2026-11-13, revbody: 1 if he has a "size", then it)
    put8(w.size < 0 ? 0 : w.size);
}

// recomp_arcade_wrestler_slots(i, dst) -> 1 if wrestler i has a moves.json, his list of slots then written to dst (unless dst
// is 0) as mod src/moveset_unpack.h reads it; 0 if he has none. (2026-11-13, movesets: the lists stay here, in the exe's
// memory, and the mod asks for one each time the game unpacks his moveset, so the mod keeps no room for them.)
void wrestler_slots(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= wrestlers.size() || wrestlers[size_t(i)].slots.empty()) { _return<int32_t>(ctx, 0); return; }
    const std::vector<uint8_t>& l = wrestlers[size_t(i)].slots;
    if (dst) for (int k = 0; k < SLOTS_BYTES; k++) MEM_B(k, dst) = int8_t(l[size_t(k)]);
    _return<int32_t>(ctx, 1);
}

// recomp_arcade_wrestler_edit(i, k, dst) -> how many edits folder wrestler i's moves.json makes to his match's entries
// ("records", "extra": read_edits), edit k written at dst (EDIT_BYTES) if dst isn't 0. (2026-11-13, movelib2)
void wrestler_edit(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx), k = _arg<1, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<2, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= wrestlers.size()) { _return<int32_t>(ctx, 0); return; }
    const std::vector<uint8_t>& e = wrestlers[size_t(i)].edits;
    const int32_t n = int32_t(e.size() / EDIT_BYTES);
    if (dst && k >= 0 && k < n) for (int b = 0; b < EDIT_BYTES; b++) MEM_B(b, dst) = int8_t(e[size_t(k) * EDIT_BYTES + size_t(b)]);
    _return<int32_t>(ctx, n);
}

// recomp_arcade_moves_note(what, id, a, b): the mod's QA note about a moveset the match unpacked (option qa_moves_twin,
// mod src/roster.c), written to the log. what 1: the game's tables against the same made from its list -- a = slot << 16 |
// layout kind, b = bytes different << 16 | the first one's offset (0xFFFF none, 0xFFFE only the ability bits). what 2: a
// folder wrestler's list put in -- a = slot << 16 | folder number, b = how many of the layout's slots it changed. what 3: the
// sweep of every roster moveset found one different -- a = roster ordinal << 16 | layout kind, b as for 1. what 4: the sweep's
// count -- a made both ways, b different.
void moves_note(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t what = _arg<0, uint32_t>(rdram, ctx), id = _arg<1, uint32_t>(rdram, ctx);
    const uint32_t a = _arg<2, uint32_t>(rdram, ctx), b = _arg<3, uint32_t>(rdram, ctx);
    if (what == 1)
        fprintf(stderr, "[moves] twin 0x%04X slot %u kind %u: %s (%u bytes different, first at 0x%X)\n", id, a >> 16, a & 0xFFFF,
                (b >> 16) ? "DIFFERENT" : "IDENTICAL", b >> 16, b & 0xFFFF);
    else if (what == 3)
        fprintf(stderr, "[moves] sweep 0x%04X (roster ordinal %u) kind %u: DIFFERENT (%u bytes different, first at 0x%X)\n", id, a >> 16, a & 0xFFFF,
                b >> 16, b & 0xFFFF);
    else if (what == 4)
        fprintf(stderr, "[moves] sweep: every roster moveset, both layout kinds -- %u made both ways, %u different%s\n", a, b, b ? "" : ": IDENTICAL");
    else if (what == 2)
        fprintf(stderr, "[moves] list 0x%04X slot %u (folder %u): his list put in, %u of the layout's slots changed from his packed moveset\n",
                id, a >> 16, a & 0xFFFF, b);
    else if (what == 5)     // (2026-11-13, movelib) a built-in port's move rows, as they were (movex_data.h) against the library's
        fprintf(stderr, "[moves] movex twin port %u (ext_late) kind %u: %s (%u table bytes different, %u entries' animations different, %u rows)\n",
                id, a >> 16, (b >> 16) || (a & 0xFFFF) ? "DIFFERENT" : "IDENTICAL", b >> 16, a & 0xFFFF, b & 0xFFFF);
    else if (what == 6)
        fprintf(stderr, "[moves] movex twin sweep: %u port movesets x layout kinds made both ways, %u different%s\n", a, b, b ? "" : ": IDENTICAL");
    else if (what == 8) {   // (2026-11-13, movelib) a list's library or folder move put in: id, a = slot << 16 | entry, b = file << 16 | move
        std::string name = "?";
        for (const auto& [k, v] : move_library) if (v.id == id) name = k;
        for (const Wrestler& w : wrestlers) for (const auto& [k, v] : w.own) if (v.id == id) name = w.sid + " own:" + k;
        fprintf(stderr, "[moves] list slot %u entry %u: move %u %s on No Mercy's %u -> animation 0x%04X%s\n", a >> 16, a & 0xFFFF, id, name.c_str(),
                b & 0xFFFF, b >> 16, (b >> 16) ? "" : " (its own: none put)");
    }
    else if (what == 9)     // (2026-11-13, movelib QA) a = slot << 16 | entries' animations different, b = port << 16 | table bytes different
        fprintf(stderr, "[moves] list twin 0x%04X slot %u against built-in port %u (ext_late): %s (%u table bytes different, %u entries' animations different)\n",
                id, a >> 16, b >> 16, ((a & 0xFFFF) || (b & 0xFFFF)) ? "DIFFERENT" : "IDENTICAL", b & 0xFFFF, a & 0xFFFF);
    else if (what == 10) {  // (2026-11-13, movelib) a list's move with no hold of its own on a move that goes on: a = slot << 16 | entry, b = the move
        std::string name = "?";
        for (const auto& [k, v] : move_library) if (v.id == id) name = k;
        for (const Wrestler& w : wrestlers) for (const auto& [k, v] : w.own) if (v.id == id) name = w.sid + " own:" + k;
        fprintf(stderr, "[moves] list slot %u entry %u: %s has no hold of its own, and No Mercy's %u it's played on goes on: its own continuation plays after it (meant? else name a move that ends, \"@<move>\")\n",
                a >> 16, a & 0xFFFF, name.c_str(), b);
    }
    else if (what == 7)
        fprintf(stderr, "[moves] the mod's built-in move library has %u moves, the exe's %u (stamp %08X): a mod and an exe of different builds, or no library file -- no library moves\n", a, b, id);
    else if (what == 11)    // (2026-11-13, movelib2) a moves.json edit put in: id = its move ID, a = slot << 16 | entry, b = use << 16 | the move
        fprintf(stderr, "[moves] list slot %u entry %u: %s -- No Mercy's %u%s, move ID %u\n", a >> 16, a & 0xFFFF,
                (b >> 16) == 1 ? "an extra entry, his leg drop" : (b >> 16) == 2 ? "the move put in its place (\"@<move>/<move>\")" : (a & 0xFFFF) >= SLOTS_N ? "an extra entry" : "its record changed",
                b & 0xFFFF, (b >> 16) == 1 ? " (his ground strike at a lying man's head, with SPECIAL)" : "", id);
    fflush(stderr);
}

// ---- moves of other games and of folders (2026-11-13, movelib: the mod's movex.c) ----------------------------------------
// recomp_arcade_move_lib_n() -> how many built-in library moves there are, and recomp_arcade_move_lib_stamp() -> the
// library's stamp (the mod's move_lib.h must have the same: else it was made from another library, and plays none).
void move_lib_n(uint8_t* rdram, recomp_context* ctx) { scan(); move_library_fill(); _return<uint32_t>(ctx, lib_n); }
void move_lib_stamp(uint8_t* rdram, recomp_context* ctx) { scan(); move_library_fill(); _return<uint32_t>(ctx, lib_stamp); }
// recomp_arcade_move_own(id, out) -> 1 with move `id` (a built-in library's, 1 .. lib_n, or a folder's own past them) written
// at out as the mod's LibMove { the attacker's file (the defender's the next; 0: the No Mercy move's own), the hold's loop's
// file or 0, the ground grapple's pick or 0, how many animations from the first (2026-11-20, revmoves) }, its animations'
// numbers given now (2026-11-13, movelib2: a library move's
// too); 0 if it isn't one, or the numbers can't be had (said in the log: the entry plays its own No Mercy move).
void move_own(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    const PTR(void) out = _arg<1, PTR(void)>(rdram, ctx);
    scan(); move_library_fill();
    if (!id || id > all_moves.size() || !out) { _return<int32_t>(ctx, 0); return; }
    const OwnMove& m = all_moves[id - 1];
    if (m.anims < 0) {          // (a move made by hand: no animation of its own)
        MEM_H(0, out) = 0; MEM_H(2, out) = 0; MEM_H(4, out) = int16_t(m.ground); MEM_H(6, out) = 0;
        _return<int32_t>(ctx, 1);
        return;
    }
    const bool had = std::any_of(leases.begin(), leases.end(), [&](const Lease& l) { return l.anims == m.anims; });
    const int li = anims_lease_for(rdram, m.anims);
    const AnimSet& fa = anim_sets[size_t(m.anims)];
    if (li < 0) {
        static std::vector<bool> said;
        if (said.size() <= size_t(m.anims)) said.resize(size_t(m.anims) + 1);
        if (!said[size_t(m.anims)]) {
            said[size_t(m.anims)] = true;
            fprintf(stderr, "[moves] %s: no game file numbers free for its animations (%zu wanted, %u in use of %zu): played with its No Mercy move's\n",
                    fa.shown.c_str(), fa.files.size(), numbers_in_use(), pool.size());
            fflush(stderr);
        }
        _return<int32_t>(ctx, 0);
        return;
    }
    const Lease& l = leases[size_t(li)];
    if (!had && (id > lib_all_n || anim_sets_given % 64 == 1)) {  // (a folder's own each time; the libraries' now and then -- wm2klib: past them all)
        fprintf(stderr, "[moves] numbers: %s: %zu game file numbers from 0x%04X (in use now %u of %zu; animation sets given %llu, let go %llu)\n",
                id > lib_all_n ? (fa.shown + "'s own moves").c_str() : fa.shown.c_str(), l.number.size(), l.number.empty() ? 0u : l.number[0],
                numbers_in_use(), pool.size(), (unsigned long long)anim_sets_given, (unsigned long long)anim_sets_back);
        fflush(stderr);
    }
    MEM_H(0, out) = int16_t(l.number[m.pair]);
    MEM_H(2, out) = int16_t(m.has_then ? l.number[m.then] : 0);
    MEM_H(4, out) = int16_t(m.ground);
    MEM_H(6, out) = int16_t(m.n);      // (2026-11-20, revmoves: how many animations from the first: the mod plays the No Mercy
                                       //  move's own for a role past them)
    _return<int32_t>(ctx, 1);
}
// recomp_arcade_anim_meta(file, dst) -> 1 with folder animation `file`'s header (its first 4 bytes) and record (36) at dst;
// 0 if the number isn't one now.
void anim_meta(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t file = _arg<0, uint32_t>(rdram, ctx) & 0xFFFF;
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    OwnAnim* a = own_anim_of(file);
    if (!a || !dst) { _return<int32_t>(ctx, 0); return; }
    const std::vector<uint8_t>& b = own_anim_bytes(*a);
    for (int k = 0; k < 4; k++) MEM_B(k, dst) = int8_t(b.size() > size_t(k) ? b[size_t(k)] : 0);
    for (int k = 0; k < 36; k++) MEM_B(4 + k, dst) = int8_t(a->record[size_t(k)]);
    _return<int32_t>(ctx, 1);
}
// recomp_arcade_anim_rom(file) -> folder animation `file`'s "ROM" place, or 0.
void anim_rom(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t file = _arg<0, uint32_t>(rdram, ctx) & 0xFFFF;
    const auto it = own_anim_of(file) ? anim_rom_of.find(file) : anim_rom_of.end();
    _return<uint32_t>(ctx, it == anim_rom_of.end() ? 0u : it->second);
}
// recomp_arcade_anim_read(rom, dst, len): len bytes of a folder animation from its "ROM" place on (the game streams a piece
// at a time). Past its end, or a place no animation has now: zeros (and the log says so once).
void anim_read(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t rom = _arg<0, uint32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    const uint32_t len = _arg<2, uint32_t>(rdram, ctx);
    auto it = anim_at_rom.upper_bound(rom);
    OwnAnim* a = nullptr;
    uint32_t from = 0;
    if (it != anim_at_rom.begin()) { --it; a = own_anim_of(it->second); from = rom - it->first; }
    const std::vector<uint8_t>* b = a ? &own_anim_bytes(*a) : nullptr;
    if (!b) {
        static bool said;
        if (!said) { said = true; fprintf(stderr, "[moves] an animation asked for at 0x%08X, but no folder animation is there now\n", rom); fflush(stderr); }
    }
    for (uint32_t k = 0; k < len; k++) MEM_B(int32_t(k), dst) = int8_t(b && from + k < b->size() ? (*b)[from + k] : 0);
}
// recomp_arcade_movex_where(files, slots << 16 | entries, players, player count << 16 | stride): where a folder animation's
// number can be held (anims_collect): the mod's per-slot table of entries' animations (u16 each), and the game's animation
// players (the file each plays at +4). The mod says it before its first folder move.
void movex_where(uint8_t* rdram, recomp_context* ctx) {
    movex_files = _arg<0, uint32_t>(rdram, ctx);
    const uint32_t se = _arg<1, uint32_t>(rdram, ctx), ps = _arg<3, uint32_t>(rdram, ctx);
    movex_slots = std::min<uint32_t>(se >> 16, 64); movex_entries = std::min<uint32_t>(se & 0xFFFF, 4096);
    movex_players = _arg<2, uint32_t>(rdram, ctx);
    movex_player_n = std::min<uint32_t>(ps >> 16, 64); movex_stride = ps & 0xFFFF;
}

// recomp_arcade_wrestler_theme_count() -> how many themes came from folders.
void wrestler_theme_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(themes.size()));
}

// recomp_arcade_wrestler_theme_get(i, dst): writes theme i into the mod's FolderTheme (mod src/media.c): his ID, the
// content pack number holding its sound, then theme.meta's bytes as they are.
void wrestler_theme_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= themes.size()) return;
    const Theme& t = themes[size_t(i)];
    MEM_W(0, dst) = t.id;
    MEM_W(4, dst) = arcade_packs_first_theme() + i;
    for (int k = 0; k < THEME_META_BYTES; k++) MEM_B(8 + k, dst) = int8_t(t.meta[k]);
}

// recomp_arcade_wrestler_video_count() -> how many Titantron videos came from folders.
void wrestler_video_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(videos.size()));
}

// recomp_arcade_wrestler_video_get(i, dst): writes video i into the mod's FolderVideo (mod src/media.c): his ID, the content
// pack holding its frames, and how many frames.
void wrestler_video_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= videos.size()) return;
    MEM_W(0, dst) = videos[size_t(i)].id;
    MEM_W(4, dst) = arcade_packs_first_theme() + int(themes.size()) + i;
    MEM_W(8, dst) = int32_t(videos[size_t(i)].frames);
}

// recomp_arcade_wrestler_hd_count() -> how many full-detail models came from folders (hd_handed: all of them, 2026-11-13).
void wrestler_hd_count(uint8_t* rdram, recomp_context* ctx) {
    _return<int32_t>(ctx, int32_t(hd_handed()));
}

// recomp_arcade_wrestler_hd_meta(i, dst): writes model i's numbers into the mod's buffer, laid out as HdFolderMeta in mod
// src/hd.c: 11 words, then the bones (HdBone, 6 words each), then the joints (HdJoint: two bytes, two spare, three floats).
void wrestler_hd_meta(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= hd_handed()) return;
    const HdModel& m = hd_models[size_t(i)];
    int at = 0;
    const auto word = [&](uint32_t v) { MEM_W(at, dst) = int32_t(v); at += 4; };
    for (uint32_t v : m.fields) word(v);
    word(m.desc_bytes);
    word(m.idx_off);
    for (const auto& bone : m.bones) for (uint32_t v : bone) word(v);
    for (const auto& joint : m.joints) {
        MEM_B(at + 0, dst) = int8_t(joint.bone);
        MEM_B(at + 1, dst) = int8_t(joint.parent);
        MEM_B(at + 2, dst) = 0;
        MEM_B(at + 3, dst) = 0;
        at += 4;
        for (float v : { joint.x, joint.y, joint.z }) { uint32_t bits; memcpy(&bits, &v, 4); word(bits); }
    }
}

/// (2026-11-12, handlers; numbers on demand 2026-11-13, handlers1; per load 2026-11-13, filepool) recomp_arcade_wrestler_file(
// file, dest) -> the size of a folder model's file (0: not one), and the file itself written at dest if dest isn't 0. The
// mod's files.c asks here first for any number of the pool (files_pool_send). A number of a load that is over is still
// answered with his file (its numbers are given again last), and said in the log once a load ("LATE READ"): something of
// the game read a descriptor after its load, which the trace above says nothing does. A number no load has is answered 0
// and said once ("STALE"). QA counts both: they must stay 0.
void wrestler_low_file(uint8_t* rdram, recomp_context* ctx) {
    scan();
    const uint32_t file = _arg<0, uint32_t>(rdram, ctx) & 0xFFFF;
    const PTR(void) dest = _arg<1, PTR(void)>(rdram, ctx);
    if (file >= GAME_FILE_END || owners[file].lease < 0) {
        static std::vector<bool> said(GAME_FILE_END, false);
        if (file < GAME_FILE_END && std::binary_search(pool.begin(), pool.end(), file) && !said[file]) {
            said[file] = true;
            low_stale++;
            fprintf(stderr, "[wrestlers] STALE: file number 0x%04X asked for, but no load has it (%llu so far)\n", file, (unsigned long long)low_stale);
            fflush(stderr);
        }
        _return<int32_t>(ctx, 0);
        return;
    }
    const Owner& o = owners[file];
    if (OwnAnim* a = own_anim_of(file)) {     // (2026-11-13, movelib: a move's animation -- a library move's or a folder's own)
        const std::vector<uint8_t>& ab = own_anim_bytes(*a);
        if (dest) for (size_t n = 0; n < ab.size(); n++) MEM_B(int32_t(n), dest) = int8_t(ab[n]);
        _return<int32_t>(ctx, int32_t(ab.size()));
        return;
    }
    Lease& l = leases[size_t(o.lease)];
    if (l.ended && l.ended_at != LATE_SAID) {
        low_late++;
        fprintf(stderr, "[wrestlers] LATE READ: file 0x%04X (%s costume %d) read after his load was over (%llu so far)\n", file,
                utf8(low_models[size_t(l.model)].dir.parent_path().filename()).c_str(), l.costume, (unsigned long long)low_late);
        fflush(stderr);
        l.ended_at = LATE_SAID;     // (said once a load; and taken back last from now on)
    }
    LowModel& m = low_models[size_t(l.model)];
    const std::vector<uint8_t>& b = low_file_bytes(m, size_t(o.file));
    if (dest) for (size_t n = 0; n < b.size(); n++) MEM_B(int32_t(n), dest) = int8_t(b[n]);
    _return<int32_t>(ctx, int32_t(b.size()));
}

// recomp_arcade_wrestler_model_where(slots, slot_count, shared_list): where the game keeps what can hold a file number:
// the first model slot's descriptor (the mod's SIXA_800A8980), how many slots (NM_SLOTS_CAP), and the word holding the
// shared file list's first node (0x80047EB4). The mod says it before its first model. (2026-11-13, handlers1)
void wrestler_low_where(uint8_t* rdram, recomp_context* ctx) {
    where_slots = _arg<0, uint32_t>(rdram, ctx);
    where_slot_n = std::min<uint32_t>(_arg<1, uint32_t>(rdram, ctx), 64);
    where_shared = _arg<2, uint32_t>(rdram, ctx);
}

// recomp_arcade_wrestler_file_pool(ranges, count): the numbers no other file has, as `count` pairs of words [first, end),
// from the mod's file map (files.c files_pool_send), said before its first model. (2026-11-13, filepool)
void wrestler_file_pool(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) at = _arg<0, PTR(void)>(rdram, ctx);
    const uint32_t count = std::min<uint32_t>(_arg<1, uint32_t>(rdram, ctx), 256);
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    for (uint32_t k = 0; k < count; k++)
        ranges.push_back({ uint32_t(MEM_W(int32_t(8 * k), at)), uint32_t(MEM_W(int32_t(8 * k + 4), at)) });
    pool_from_mod = true;
    pool_set(ranges);
    std::string said;
    for (const auto& [a, b] : ranges) { char t[24]; snprintf(t, sizeof t, " %04X-%04X", a, b - 1); said += t; }
    fprintf(stderr, "[wrestlers] file number pool: %zu numbers (%s )%s\n", pool.size(), said.c_str(),
            getenv("WCW_QA_FILE_POOL") ? " -- QA: cut to WCW_QA_FILE_POOL" : "");
    fflush(stderr);
}

// recomp_arcade_wrestler_model_loaded(desc): the mod's func_80016464 has put in the pictures of the model whose descriptor
// is at `desc` (its second step): that slot's load is over, and its numbers can go to others. (2026-11-13, filepool)
void wrestler_model_loaded(uint8_t* rdram, recomp_context* ctx) {
    const int slot = slot_of(_arg<0, uint32_t>(rdram, ctx));
    if (slot < 0) return;
    for (size_t li = 0; li < leases.size(); li++)
        if (leases[li].model >= 0 && leases[li].slot == slot) lease_end(int(li));
}

// recomp_arcade_wrestler_model(i, costume, model) -> 1 if folder wrestler i has a model of his own, which is then written
// over the game's model at `model` (all but the bytes his model.json keeps: 0x254, the game's state byte), with game file
// numbers given to that costume for this load (lease_for); else 0, and the game's model stays.
void wrestler_low_model(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const int32_t c = _arg<1, int32_t>(rdram, ctx) & 3;
    const PTR(void) model = _arg<2, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= wrestlers.size() || wrestlers[size_t(i)].low_index < 0 || !model) { _return<int32_t>(ctx, 0); return; }
    const Wrestler& w = wrestlers[size_t(i)];
    const int mi = w.low_index;
    const int slot = slot_of(uint32_t(model));
    const int li = lease_for(rdram, mi, c, slot);
    uint32_t loading = 0;
    const uint32_t used = numbers_in_use(&loading);
    if (li < 0) {
        fprintf(stderr, "[wrestlers] %s costume %d: no game file numbers free for his model (%zu numbers wanted, %u of the pool's %zu loading now): the model he's based on instead\n",
                w.sid.c_str(), c, low_models[size_t(mi)].costume[c].numbered.size(), loading, pool.size());
        fflush(stderr);
        _return<int32_t>(ctx, 0);
        return;
    }
    {
        const Lease& l = leases[size_t(li)];
        fprintf(stderr, "[wrestlers] numbers: %s costume %d slot %d: %zu game file numbers from 0x%04X (pool %zu: in use now %u, %u of them loading; most at once %u; given so far %llu; loads taken back %llu; refused %llu; late reads %llu; stale %llu)\n",
                w.sid.c_str(), c, slot, l.number.size(), l.number.empty() ? 0u : l.number[0], pool.size(), used, loading, low_peak,
                (unsigned long long)low_given, (unsigned long long)low_recycled, (unsigned long long)low_refused,
                (unsigned long long)low_late, (unsigned long long)low_stale);
        fflush(stderr);
    }
    const LowModel& m = low_models[size_t(mi)];
    const LowCostume& lc = m.costume[c];
    const Lease& l = leases[size_t(li)];
    std::vector<uint8_t> desc = lc.desc;
    for (const auto& [at, k] : lc.fields) {
        const size_t slot_k = size_t(std::lower_bound(lc.numbered.begin(), lc.numbered.end(), k) - lc.numbered.begin());
        const uint32_t n = l.number[slot_k];
        desc[size_t(at)] = uint8_t(n >> 8);
        desc[size_t(at) + 1] = uint8_t(n);
    }
    for (int n = 0; n < LOW_DESC_BYTES; n++) if (!lc.keep[size_t(n)]) MEM_B(n, model) = int8_t(desc[size_t(n)]);
    _return<int32_t>(ctx, 1);
}

// recomp_arcade_wrestler_model_note(i, costume, bytes_differ, files_same << 16 | files_differ): the mod's QA twin check
// (parts.c, option qa_model_twin) -- his folder model against the built-in one the game made first -- written to the log,
// once per wrestler and costume. (2026-11-13, handlers1: with bytes_differ -1, he has no built-in twin -- a wrestler never
// ported -- and the check is of his own: every file field of the model the game now has names one of HIS files, by the
// numbers his costume was given, and those numbers' files are his folder's.)
void wrestler_low_note(uint8_t* rdram, recomp_context* ctx) {
    static std::vector<int64_t> said;
    const int32_t i = _arg<0, int32_t>(rdram, ctx), c = _arg<1, int32_t>(rdram, ctx) & 3;
    const int32_t bytes = _arg<2, int32_t>(rdram, ctx);
    const uint32_t files = _arg<3, uint32_t>(rdram, ctx);
    const int32_t same = int32_t(files >> 16), differ = int32_t(files & 0xFFFF);
    if (i < 0 || size_t(i) >= wrestlers.size()) return;
    // (once per wrestler, costume and the numbers it holds: a costume given numbers again, after his last ones went to
    //  others, is checked again -- 2026-11-13)
    uint32_t first = 0;
    for (const Lease& l : leases) if (l.model == wrestlers[size_t(i)].low_index && l.costume == c && !l.number.empty()) first = l.number[0];
    const int64_t key = int64_t(i * 4 + c) << 16 | first;
    if (std::find(said.begin(), said.end(), key) != said.end()) return;
    said.push_back(key);
    const std::string who = utf8(wrestlers[size_t(i)].folder.filename());
    if (bytes < 0)
        fprintf(stderr, "[wrestlers] own %s costume %d: %s -- %d file fields name his own files, %d don't\n",
                who.c_str(), c, differ == 0 ? "HIS OWN MODEL (no built-in twin)" : "DIFFERENT", same, differ);
    else
        fprintf(stderr, "[wrestlers] twin %s costume %d: %s -- %d bytes differ (file numbers aside), %d files the same, %d files differ\n",
                who.c_str(), c, (bytes == 0 && differ == 0) ? "IDENTICAL to the built-in model" : "DIFFERENT", bytes, same, differ);
    fflush(stderr);
}

// recomp_arcade_wrestler_model_check(i, costume, model) -> (fields naming his own files) << 16 | (fields that don't): for
// the QA note above, a model with no built-in twin -- each file field of his costume, read back from the game's model,
// must hold a number his costume holds now, for the very file the field names in his folder. (2026-11-13, handlers1)
void wrestler_low_check(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const int32_t c = _arg<1, int32_t>(rdram, ctx) & 3;
    const PTR(void) model = _arg<2, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= wrestlers.size() || wrestlers[size_t(i)].low_index < 0 || !model) { _return<uint32_t>(ctx, 0); return; }
    const int mi = wrestlers[size_t(i)].low_index;
    uint32_t good = 0, bad = 0;
    for (const auto& [at, k] : low_models[size_t(mi)].costume[c].fields) {
        const uint32_t n = uint16_t(MEM_HU(at, model));
        const bool ok = n < GAME_FILE_END && owners[n].lease >= 0 && owners[n].file == k &&
                        leases[size_t(owners[n].lease)].model == mi && leases[size_t(owners[n].lease)].costume == c;
        ok ? good++ : bad++;
    }
    _return<uint32_t>(ctx, good << 16 | bad);
}
// ---- stock folders, for the mod (2026-11-13, nmhandler) -----------------------------------------------------------------
// recomp_arcade_stock_count() -> how many stock folders carry data (stock_folders).
void stock_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(stock_folders.size()));
}
// recomp_arcade_stock_get(i, dst): stock folder i into the mod's buffer, laid out as STK_* in mod src/roster.c byte for byte:
// his game number, u16 flags (1: a moveset), then per costume 0x60 bytes -- u16 flags of what it gives (1 name, 2 short name,
// 4 nickname, 8 height, 0x10 weight, 0x20 music, 0x40 video, 0x80 looks), name, short name, nickname, height, weight,
// music, video, the 38 looks fields -- then the 216 moveset bytes. A costume's fields are per_costume's, else the folder's.
void stock_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<1, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= stock_folders.size()) return;
    const Wrestler& w = stock_folders[size_t(i)];
    int at = 0;
    const auto put8 = [&](int v) { MEM_B(at++, dst) = int8_t(v); };
    const auto put16 = [&](int v) { put8(v >> 8); put8(v); };
    const auto put_text = [&](const std::string& s, int bytes) { for (int k = 0; k < bytes; k++) put8(size_t(k) < s.size() ? s[size_t(k)] : 0); };
    put16(w.id);
    put16(w.moves.empty() ? 0 : 1);
    for (int c = 0; c < 4; c++) {
        const Wrestler::Costume* o = w.per_costume.empty() ? nullptr : &w.per_costume[size_t(c)];
        const bool hn = (o && o->has_name) || w.has_name, hs = (o && o->has_short) || w.has_short, hk = (o && o->has_nick) || w.has_nick;
        const int height = o && o->height >= 0 ? o->height : w.height, weight = o && o->weight >= 0 ? o->weight : w.weight;
        const int music = o && o->music >= 0 ? o->music : w.music, video = o && o->video >= 0 ? o->video : w.video;
        put16((hn ? 1 : 0) | (hs ? 2 : 0) | (hk ? 4 : 0) | (height >= 0 ? 8 : 0) | (weight >= 0 ? 0x10 : 0) | (music >= 0 ? 0x20 : 0) |
              (video >= 0 ? 0x40 : 0) | (w.looks.empty() ? 0 : 0x80));
        put_text(o && o->has_name ? o->name : w.name, NAME_BYTES);
        put_text(o && o->has_short ? o->short_name : w.short_name, SHORT_BYTES);
        put_text(o && o->has_nick ? o->nick : (w.no_nick ? std::string() : w.nick), NICK_BYTES);
        put8(height < 0 ? 0 : height);
        put16(weight < 0 ? 0 : weight);
        put8(music < 0 ? 0 : music);
        put8(video < 0 ? 0 : video);
        for (int k = 0; k < LOOK_FIELDS; k++) put8(w.looks.empty() ? 0 : w.looks[size_t(c * LOOK_FIELDS + k)]);
        while (at < 4 + (c + 1) * 0x60) put8(0);
    }
    for (int k = 0; k < MOVES_BYTES; k++) put8(w.moves.empty() ? 0 : w.moves[size_t(k)]);
}
// recomp_arcade_stock_photo(i, costume, dst) -> 1 and the photo (32 + 2048 bytes) at dst (dst 0: only asked), if stock folder i
// has one for that costume: photo_c<costume>.bin, else photo.bin. 0 if neither.
void stock_photo(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx), c = _arg<1, int32_t>(rdram, ctx);
    const PTR(void) dst = _arg<2, PTR(void)>(rdram, ctx);
    if (i < 0 || size_t(i) >= stock_folders.size() || c < 0 || c > 3) { _return<int32_t>(ctx, 0); return; }
    const Wrestler& w = stock_folders[size_t(i)];
    const std::vector<uint8_t>& p = !w.photo_c[c].empty() ? w.photo_c[c] : w.photo;
    if (p.empty()) { _return<int32_t>(ctx, 0); return; }
    if (dst) for (int k = 0; k < PHOTO_BYTES; k++) MEM_B(k, dst) = int8_t(p[size_t(k)]);
    _return<int32_t>(ctx, 1);
}
// recomp_arcade_stock_note(i, what << 8 | costume, a, b): a result from the mod about stock folder i, into the log: the QA twin
// (option qa_stock_twin: his folder's against the built-in, the game's own tables built the game's own way) and problems.
//   what 1 record (a: bytes of the 0x4E that differ, b: the first's offset), 2 moveset (the same over 0xD8), 3 model (the
//   descriptor the game builds from each record's looks, over 0x270), 4 photo (costume: the photo number; a: bytes that
//   differ over its three files), 5 a looks field the game's packed looks can't hold (a: the field, b: the value), 6 the
//   built-in isn't one the game builds from its own tables (b: his number), 7 the music (a: 0 the same slot, b: the slot)
void stock_note(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx), wc = _arg<1, int32_t>(rdram, ctx), a = _arg<2, int32_t>(rdram, ctx);
    const uint32_t b = _arg<3, uint32_t>(rdram, ctx);
    const int what = wc >> 8, c = wc & 0xFF;
    if (i < 0 || size_t(i) >= stock_folders.size()) return;
    const char* sid = stock_folders[size_t(i)].sid.c_str();
    const char* verdict = a == 0 ? "IDENTICAL to the built-in" : "DIFFERENT from the built-in";
    switch (what) {
    case 1: fprintf(stderr, "[wrestlers] stock twin %s costume %d: record %s -- %d of 0x4E bytes differ", sid, c, verdict, a);
            if (a) fprintf(stderr, ", the first at 0x%02X", unsigned(b));
            fprintf(stderr, "\n"); break;
    case 2: fprintf(stderr, "[wrestlers] stock twin %s: moveset %s -- %d of 0xD8 bytes differ\n", sid, verdict, a); break;
    case 3: fprintf(stderr, "[wrestlers] stock twin %s costume %d: model %s -- %d of 0x270 descriptor bytes differ\n", sid, c, verdict, a); break;
    case 4: fprintf(stderr, "[wrestlers] stock twin %s photo %d: %s -- %d bytes differ over its TEX, palette and picture files\n", sid, c, verdict, a); break;
    case 5: fprintf(stderr, "[wrestlers] stock %s costume %d: looks field %d is %u, more than the game's packed looks hold: the costume shows another value\n", sid, c, a, unsigned(b)); break;
    case 6: fprintf(stderr, "[wrestlers] stock %s: 0x%04X isn't a Superstar the game builds from its own tables: his folder's data isn't read\n", sid, unsigned(b)); break;
    case 7: fprintf(stderr, "[wrestlers] stock twin %s costume %d: music %s -- slot %u\n", sid, c, verdict, unsigned(b)); break;
    default: break;
    }
    fflush(stderr);
}
// ---- removed stock Superstars, for the mod (2026-11-13, removal) ----------------------------------------------------------
// recomp_arcade_removed_count() -> how many stock Superstars are removed; recomp_arcade_removed_get(i) -> removed man i's game
// number (0: no such i).
void removed_count(uint8_t* rdram, recomp_context* ctx) {
    scan();
    _return<int32_t>(ctx, int32_t(removed.size()));
}
void removed_get(uint8_t* rdram, recomp_context* ctx) {
    const int32_t i = _arg<0, int32_t>(rdram, ctx);
    _return<int32_t>(ctx, i >= 0 && size_t(i) < removed.size() ? int32_t(removed[size_t(i)].id) : 0);
}
// recomp_arcade_removal_text(text): a line of the mod's about what it did with something that names a removed man, into the
// log after "[removal] ". "@" and four hex digits in it is a game number: written as his string ID with the number.
void removal_text(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) src = _arg<0, PTR(void)>(rdram, ctx);
    std::string t;
    for (int k = 0; k < 400; k++) { const char ch = char(MEM_B(k, src)); if (!ch) break; t += ch; }
    std::string out;
    for (size_t k = 0; k < t.size(); k++) {
        if (t[k] == '@' && k + 4 < t.size() &&isxdigit(uint8_t(t[k + 1])) && isxdigit(uint8_t(t[k + 2])) && isxdigit(uint8_t(t[k + 3])) && isxdigit(uint8_t(t[k + 4]))) {
            const uint16_t n = uint16_t(strtol(t.substr(k + 1, 4).c_str(), nullptr, 16));
            const auto s = sid_of_number.find(n);
            char num[16];
            snprintf(num, sizeof num, "0x%04X", unsigned(n));
            out += s != sid_of_number.end() ? s->second + " (" + num + ")" : std::string(num);
            k += 4;
        } else out += t[k];
    }
    fprintf(stderr, "[removal] %s\n", out.c_str());
    fflush(stderr);
}
// ---- MERGE SAME WRESTLERS (New feature (Ultimate No Mercy) -- automerge, 2026-11-23) ------------------------------------------
// The user (2026-11-23): "can we add the option in F2 settings to auto merge wrestlers into the same select spot as variants
// (they retain all of their moves, music, display name, etc). They always appear after the No Mercy 4 variants."
// F2 > GAME SETTINGS > MERGE SAME WRESTLERS (the mod's option "auto_merge", OFF unless set; read here once, at the start-up:
// a change in F2 counts from the next one). ON, the game makes at start-up EXACTLY the families the roster editor's
// "Consolidate automatically..." makes with its ticked proposals (the "same" kind) -- its app/families.py propose() and
// consolidate(), ported step for step below -- WITHOUT writing any file: as though families.json said so. The rule's data is
// the editor's own, shipped with the game (one source: the package copies it): data\aliases.json (who is who across the
// games; its "rules" say how names are matched) and data\builtin_roster.json (the built-in Superstars, in roster order).
// In short (aliases.json "rules"): the FULL name only (never a short name or nickname), AKI marks, accents, a quoted
// nickname and a leading THE handled, then letters and digits; or one character in the known-identity list with kind
// "same" and confidence "sure" ("likely", "persona" and "different" are never joined automatically). A family's members in
// tier order -- No Mercy (its stock men, the restored Big Show, the No Mercy Plus built-ins), WrestleMania 2000, Revenge,
// the rest -- each game's own roster order within it; each member keeps his own variant order and is still himself (his
// game number, name, moves, music, Titantron, entrance). A family the data made stays, the others added at its end; one
// of the game's default families is put in that order; wrestlers in two or more families already: left as they are.
// Everything after is as for any family (variants.c: the box, the Type arrows, Random, the Royal Rumble's draw and its one
// of each, stories' same man; a hidden wrestler or variant stays hidden). The log says what it did: "[families] auto-merge".
bool families_done = false;            // (families_ready: the merge and families_finish, once)

// The option, as the mod keeps it (mod.toml "auto_merge": a Number, 0 or 1).
bool auto_merge_setting() {
    const recomp::config::ConfigValueVariant v = recomp::mods::get_mod_config_value("nomercy_arcade", "auto_merge");
    if (const uint32_t* u = std::get_if<uint32_t>(&v)) return *u != 0;
    if (const double* d = std::get_if<double>(&v)) return *d != 0.0;
    if (const bool* b = std::get_if<bool>(&v)) return *b;
    if (const std::string* s = std::get_if<std::string>(&v)) return *s == "1" || *s == "On" || *s == "ON" || *s == "on";
    return false;
}

// UTF-8 to code points (a byte that isn't UTF-8 is taken as itself: the editor would not have read such a file at all).
std::u32string utf32_of(const std::string& s) {
    std::u32string o;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        const int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        char32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        bool ok = n >= 0;
        for (int k = 1; ok && k <= n; k++) {
            if (i + size_t(k) >= s.size() || (uint8_t(s[i + size_t(k)]) & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (uint8_t(s[i + size_t(k)]) & 0x3F);
        }
        if (!ok) { o += char32_t(c); i++; continue; }
        o += cp;
        i += size_t(n) + 1;
    }
    return o;
}
// Python's str.upper() and then NFKD with the combining marks dropped (the editor's match_key), for what wrestlers' names
// hold: ASCII, Latin-1 and Latin Extended-A. A letter with no plain letter in it (AE, O with a stroke ...) stays itself: like
// any other character beyond A-Z and 0-9 it is dropped at the end, as the editor drops it.
std::u32string upper_plain(char32_t c) {
    if (c >= U'a' && c <= U'z') return std::u32string(1, char32_t(c - 32));
    if (c == 0xDF) return U"SS";                                             // (sharp s)
    if (c == 0xFF) c = 0x178;                                                // (y with diaeresis: its capital)
    else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c -= 0x20;                 // (Latin-1's small letters)
    else if (c == 0x131) return U"I";                                        // (dotless i)
    else if (c == 0x17F) return U"S";                                        // (long s)
    else if (c == 0x149) return U"ʼN";                                  // (n after an apostrophe)
    else if (c >= 0x100 && c <= 0x137 && (c & 1)) c -= 1;                    // (Latin Extended-A: pairs, the capital first ...)
    else if (c >= 0x139 && c <= 0x148 && !(c & 1)) c -= 1;                   // (... here the capital is the odd one)
    else if (c >= 0x14A && c <= 0x177 && (c & 1)) c -= 1;
    else if (c >= 0x179 && c <= 0x17E && !(c & 1)) c -= 1;
    if (c >= 0x300 && c <= 0x36F) return U"";                                // (a combining mark: dropped)
    if (c == 0xA0 || c == 0x202F || c == 0x205F || c == 0x3000 || (c >= 0x2000 && c <= 0x200A)) return U" ";   // (spaces)
    static const struct { char32_t from, to; const char32_t* plain; } spans[] = {
        { 0xC0, 0xC5, U"A" }, { 0xC7, 0xC7, U"C" }, { 0xC8, 0xCB, U"E" }, { 0xCC, 0xCF, U"I" }, { 0xD1, 0xD1, U"N" },
        { 0xD2, 0xD6, U"O" }, { 0xD9, 0xDC, U"U" }, { 0xDD, 0xDD, U"Y" }, { 0x178, 0x178, U"Y" },
        { 0x100, 0x105, U"A" }, { 0x106, 0x10D, U"C" }, { 0x10E, 0x10F, U"D" }, { 0x112, 0x11B, U"E" }, { 0x11C, 0x123, U"G" },
        { 0x124, 0x125, U"H" }, { 0x128, 0x130, U"I" }, { 0x132, 0x133, U"IJ" }, { 0x134, 0x135, U"J" }, { 0x136, 0x137, U"K" },
        { 0x139, 0x13E, U"L" }, { 0x13F, 0x140, U"L·" }, { 0x143, 0x148, U"N" }, { 0x14C, 0x151, U"O" }, { 0x154, 0x159, U"R" },
        { 0x15A, 0x161, U"S" }, { 0x162, 0x165, U"T" }, { 0x168, 0x173, U"U" }, { 0x174, 0x175, U"W" }, { 0x176, 0x177, U"Y" },
        { 0x179, 0x17E, U"Z" } };
    for (const auto& sp : spans) if (c >= sp.from && c <= sp.to) return sp.plain;
    return std::u32string(1, c);
}
bool is_space32(char32_t c) {
    return c == U' ' || (c >= 9 && c <= 13) || (c >= 0x1C && c <= 0x1F) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
std::u32string strip32(const std::u32string& s) {
    size_t a = 0, b = s.size();
    while (a < b && is_space32(s[a])) a++;
    while (b > a && is_space32(s[b - 1])) b--;
    return s.substr(a, b - a);
}
// The editor's match_key (app/families.py; aliases.json "rules"): a full name as the known-identity list matches it.
//   AKI marks: {...} dropped with its text, < and > dropped (their text kept); control characters dropped; upper case, accents
//   off; a quoted nickname inside the name dropped (a name wholly in quotes only loses them); a leading THE dropped (never
//   inside: ANDRE THE GIANT stays); then letters and digits only.
std::string match_key(const std::string& name) {
    const std::u32string in = utf32_of(name);
    std::u32string s;
    for (size_t i = 0; i < in.size(); i++) {                 // ({...}: up to the first } after it; a { never closed stays)
        if (in[i] == U'{') { const size_t e = in.find(U'}', i + 1); if (e != std::u32string::npos) { i = e; continue; } }
        if (in[i] == U'<' || in[i] == U'>') continue;
        s += in[i];
    }
    std::u32string t;
    for (char32_t c : s) if (c >= 32 && c != 127) t += c;   // (control characters)
    s.clear();
    for (char32_t c : t) s += upper_plain(c);
    s = strip32(s);
    const size_t quotes = size_t(std::count(s.begin(), s.end(), U'"'));
    if (s.size() >= 2 && s.front() == U'"' && s.back() == U'"' && quotes == 2) s = s.substr(1, s.size() - 2);
    else {                                                    // ("..." -> a space, pair by pair from the left)
        std::u32string o;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == U'"') { const size_t e = s.find(U'"', i + 1); if (e != std::u32string::npos) { o += U' '; i = e; continue; } }
            o += s[i];
        }
        s = o;
    }
    s = strip32(s);
    if (s.size() > 3 && s.compare(0, 3, U"THE") == 0 && !((s[3] >= U'A' && s[3] <= U'Z') || (s[3] >= U'0' && s[3] <= U'9'))) s = s.substr(3);
    std::string key;
    for (char32_t c : s) if ((c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9')) key += char(c);
    return key;
}

// The editor's tier_of: 0 No Mercy (No Mercy Plus with it), 1 WrestleMania 2000, 2 Revenge, 3 the rest.
int tier_of(const std::string& game) {
    std::string g;
    for (char c : game) g += char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    if (g.find("no mercy") != std::string::npos) return 0;
    if (g.find("wrestlemania 2000") != std::string::npos || g.find("wm2000") != std::string::npos) return 1;
    if (g.find("revenge") != std::string::npos) return 2;
    return 3;
}

// One wrestler as the editor's people() has him.
struct AmPerson {
    uint16_t n = 0;
    std::string name, game, id, builtin_id;   // (id: his string ID now; builtin_id: a built-in's frozen ID, "" for a folder's)
    bool builtin = false, stock = false, removed = false;
    int64_t pos = 0; bool has_pos = false;    // (his place in his game's roster)
};
using AmKey = std::tuple<int, int, int64_t, int, uint16_t>;
struct AmProp { std::string key, change, title, note; std::vector<uint16_t> members; std::vector<FamilyVariant> result; };

void families_auto_merge() {
    const std::filesystem::path data = arcade::program_folder() / "data";
    const auto read_json = [](const std::filesystem::path& p) { std::ifstream f(p); return f.good() ? json::parse(f, nullptr, false) : json(); };
    const json roster = read_json(data / "builtin_roster.json"), aliases = read_json(data / "aliases.json");
    if (!roster.is_object() || !roster.contains("superstars") || !roster["superstars"].is_array()) {
        fprintf(stderr, "[families] auto-merge: %s can't be read: nothing merged\n", arcade::shown(data / "builtin_roster.json").c_str());
        return;
    }
    if (!aliases.is_object()) fprintf(stderr, "[families] auto-merge: %s can't be read: by names alone (no known identities)\n", arcade::shown(data / "aliases.json").c_str());
    const auto sid_of = [&](uint16_t n) {
        const auto s = sid_of_number.find(n);
        char num[8];
        snprintf(num, sizeof num, "0x%04X", unsigned(n));
        return s != sid_of_number.end() ? s->second : std::string(num);
    };
    const auto upper_ascii = [](std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 32); return s; };
    const auto lower_ascii = [](std::string s) { for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c + 32); return s; };
    // -- the known identities (the editor's Identities): a match key -> its character; each built-in's game -----------------
    std::map<std::string, std::string> sure;
    std::map<std::string, std::string> builtin_game;
    if (aliases.is_object()) for (const char* group : { "people", "by_name_only" }) {
        if (!aliases.contains(group) || !aliases[group].is_array()) continue;
        for (const json& p : aliases[group]) {
            if (!p.is_object() || !p.contains("characters") || !p["characters"].is_array()) continue;
            for (const json& c : p["characters"]) {
                if (!c.is_object() || !c.contains("key") || !c["key"].is_string()) continue;
                const std::string key = c["key"].get<std::string>();
                // ("only_games": a character the list keys to some games only -- not in the automatic table: Python's truth test)
                bool limited = false;
                if (c.contains("only_games")) {
                    const json& g = c["only_games"];
                    limited = g.is_array() || g.is_object() ? !g.empty() : g.is_string() ? !g.get<std::string>().empty() :
                              g.is_boolean() ? g.get<bool>() : g.is_number() ? g.get<double>() != 0.0 : false;
                }
                if (!limited && c.contains("match_key") && c["match_key"].is_string()) sure.emplace(c["match_key"].get<std::string>(), key);
                if (c.contains("aliases") && c["aliases"].is_array())
                    for (const json& a : c["aliases"])
                        if (!limited && a.is_object() && a.contains("kind") && a["kind"] == "same" && a.contains("confidence") && a["confidence"] == "sure"
                            && a.contains("match_key") && a["match_key"].is_string())
                            sure.emplace(a["match_key"].get<std::string>(), key);
                if (c.contains("appearances") && c["appearances"].is_array())
                    for (const json& ap : c["appearances"])
                        if (ap.is_object() && ap.contains("source") && ap["source"] == "built-in" && ap.contains("string_id") && ap["string_id"].is_string()
                            && !ap["string_id"].get<std::string>().empty())
                            builtin_game[ap["string_id"].get<std::string>()] = ap.contains("game") && ap["game"].is_string() ? ap["game"].get<std::string>() : std::string();
            }
        }
    }
    // -- everyone (the editor's people()): the built-ins, in builtin_roster.json's order, then the folders ---------------------
    std::map<uint16_t, AmPerson> people;
    std::vector<uint16_t> insertion;                     // (the editor's dict order: for a second look's base, below)
    std::vector<bool> is_removed(0x1000, false);
    for (const Removed& r : removed) if (r.id < 0x1000) is_removed[r.id] = true;
    const auto printable = [](const std::string& s) { for (char c : s) if (uint8_t(c) < 0x20 || c == 0x7F) return false; return true; };
    const json& list = roster["superstars"];
    for (size_t i = 0; i < list.size(); i++) {
        const json& b = list[i];
        if (!b.is_object() || !b.contains("number") || !b.contains("id") || !b["id"].is_string()) continue;
        const int64_t n = b["number"].is_string() ? strtoll(b["number"].get<std::string>().c_str(), nullptr, 16) : number_of(b["number"]);
        if (n <= 0 || n >= 0x1000 || !sid_of_number.count(uint16_t(n))) continue;
        const std::string bid = b["id"].get<std::string>();
        // (the editor's builtins(): a name that doesn't spell his ID gives way to a roster name that does; none usable: his ID)
        const bool usable = b.contains("name") && b["name"].is_string() && printable(b["name"].get<std::string>());
        std::string name = usable ? b["name"].get<std::string>() : std::string(), spelled;
        for (const std::string& k : roster_names_of[uint16_t(n)]) if (printable(k) && clean_id(k) == bid) { spelled = k; break; }
        if (!(usable && clean_id(name) == bid) && !spelled.empty()) name = spelled;
        else if (!usable) { name = bid; for (char& c : name) if (c == '_') c = ' '; name = upper_ascii(name); }
        AmPerson p;
        p.n = uint16_t(n); p.name = name; p.id = sid_of(p.n); p.builtin_id = bid; p.builtin = true;
        p.stock = b.contains("stock_folder") && b["stock_folder"].is_boolean() && b["stock_folder"].get<bool>();
        const auto g = builtin_game.find(bid);
        p.game = g != builtin_game.end() && !g->second.empty() ? g->second : (p.stock ? "WWF No Mercy" : "");
        p.removed = is_removed[p.n];
        p.pos = int64_t(i); p.has_pos = true;
        if (!people.count(p.n)) insertion.push_back(p.n);
        people[p.n] = p;
    }
    const auto game_of_folder = [&](const std::string& source_game, bool builtin_key, const std::string& about) {
        if (!source_game.empty()) return source_game;
        if (builtin_key) return std::string("WWF No Mercy");
        const std::string a = lower_ascii(about);
        for (const char* g : { "WrestleMania 2000", "Revenge", "No Mercy" }) if (a.find(lower_ascii(g)) != std::string::npos) return std::string(g);
        return std::string();
    };
    for (const StockSeen& s : stock_seen) {             // (a built-in's own folder, "builtin": he is his folder now)
        if (s.sid != s.builtin_sid || !people.count(s.number)) continue;
        AmPerson& p = people[s.number];
        if (s.has_name) p.name = s.name;
        p.game = game_of_folder(s.source_game, true, "");
        p.builtin = false; p.stock = true; p.removed = false;
        p.id = s.sid;
    }
    for (const Wrestler& w : wrestlers) {
        AmPerson p;
        const auto old = people.find(w.id);
        p.n = w.id; p.id = w.sid;
        p.name = w.has_name ? w.full_name : old != people.end() ? old->second.name : std::string();
        if (p.name.empty()) p.name = w.sid;
        p.game = game_of_folder(w.source_game, w.stock, w.about);
        p.pos = w.source_pos; p.has_pos = w.source_pos >= 0;
        if (old == people.end()) insertion.push_back(p.n);
        people[p.n] = p;
    }
    // -- where each comes in a family (the editor's order_key) --------------------------------------------------------------
    const auto order_key = [&](uint16_t n) -> AmKey {
        const AmPerson& p = people[n];
        const int tier = tier_of(p.game);
        const std::string sid = !p.builtin_id.empty() ? p.builtin_id : p.id;
        const int group = tier == 0 ? (p.stock || n == 0x0201 ? 0 : 2) : (p.builtin ? 0 : 1);
        int64_t pos = p.pos; bool has = p.has_pos; int sub = 0;
        if (tier == 0 && p.builtin_id == "chris_benoit" && people.count(0x0201)) { pos = people[0x0201].pos; has = people[0x0201].has_pos; sub = 1; }
        if (p.builtin && sid.size() > 2 && sid.compare(sid.size() - 2, 2, "_b") == 0)
            for (uint16_t q : insertion)
                if (people[q].builtin && people[q].builtin_id == sid.substr(0, sid.size() - 2)) { pos = people[q].pos; has = people[q].has_pos; sub = 1; break; }
        return AmKey(tier, group, has ? pos : int64_t(1) << 20, sub, n);
    };
    const auto by_order = [&](uint16_t a, uint16_t b) { return order_key(a) < order_key(b); };
    std::vector<uint16_t> everyone;
    for (const auto& kv : people) everyone.push_back(kv.first);
    std::stable_sort(everyone.begin(), everyone.end(), by_order);
    // -- the families as they are (the editor's state: what each family lists, its hidden variants too) ----------------------
    const auto family_index_of = [&](uint16_t n) -> int {
        for (size_t i = 0; i < families.size(); i++) for (const FamilyVariant& v : families[i].all) if (v.id == n) return int(i);
        return -1;
    };
    std::vector<int> start_family(0x1000, -1);
    for (uint16_t n : everyone) start_family[n] = family_index_of(n);
    const auto variants_for = [&](uint16_t n) {          // (what he brings: what his family lists of him, else his four)
        std::vector<FamilyVariant> out;
        if (start_family[n] >= 0) { for (const FamilyVariant& v : families[size_t(start_family[n])].all) if (v.id == n) out.push_back(v); return out; }
        if (people[n].builtin && tier_of(people[n].game) == 3) { out.push_back({ n, 0 }); return out; }   // (a full-detail model: one)
        for (uint8_t k = 0; k < 4; k++) out.push_back({ n, k });
        return out;
    };
    // -- the proposals of the "same" kind (the editor's propose(); its "persona" and "likely" ones are never automatic) ------
    std::vector<std::pair<std::string, std::vector<uint16_t>>> groups;
    std::map<std::string, size_t> group_at;
    for (uint16_t n : everyone) {
        const AmPerson& p = people[n];
        if (p.removed) continue;
        const std::string k = match_key(p.name);
        if (k.empty()) continue;
        const auto s = sure.find(k);
        const std::string key = s != sure.end() ? s->second : "auto:" + lower_ascii(k);
        const auto at = group_at.find(key);
        if (at == group_at.end()) { group_at[key] = groups.size(); groups.push_back({ key, { n } }); }
        else groups[at->second].second.push_back(n);
    }
    const auto same_list = [](const std::vector<FamilyVariant>& a, const std::vector<FamilyVariant>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++) if (a[i].id != b[i].id || a[i].variant != b[i].variant) return false;
        return true;
    };
    std::vector<AmProp> props;
    for (const auto& [key, members] : groups) {
        std::vector<int> fams;
        for (uint16_t n : members) { const int f = start_family[n]; if (f >= 0 && std::find(fams.begin(), fams.end(), f) == fams.end()) fams.push_back(f); }
        if (members.size() < 2 && fams.empty()) continue;
        AmProp pr;
        pr.key = "same:" + key; pr.members = members; pr.title = people[members[0]].name;
        if (fams.size() > 1) {
            pr.change = "skipped";
            pr.note = "in " + std::to_string(fams.size()) + " different families already: left as they are";
        } else if (!fams.empty() && !families[size_t(fams[0])].is_default) {
            const Family& f = families[size_t(fams[0])];
            std::vector<uint16_t> fw, extra;
            for (const FamilyVariant& v : f.all) if (std::find(fw.begin(), fw.end(), v.id) == fw.end()) fw.push_back(v.id);
            for (uint16_t n : members) if (start_family[n] < 0) extra.push_back(n);
            pr.result = f.all;
            for (uint16_t n : extra) for (const FamilyVariant& v : variants_for(n)) pr.result.push_back(v);
            pr.members = fw; pr.members.insert(pr.members.end(), extra.begin(), extra.end());
            if (!f.all.empty()) pr.title = people[f.all[0].id].name;
            pr.change = extra.empty() ? "none" : "added";
            pr.note = "a family the data made: kept as it is" + (extra.empty() ? std::string() : ", " + std::to_string(extra.size()) + " added at its end");
        } else {
            const int f = fams.empty() ? -1 : fams[0];
            std::vector<uint16_t> all_m = members;
            if (f >= 0) for (const FamilyVariant& v : families[size_t(f)].all) if (std::find(all_m.begin(), all_m.end(), v.id) == all_m.end()) all_m.push_back(v.id);
            std::stable_sort(all_m.begin(), all_m.end(), by_order);
            for (uint16_t n : all_m) for (const FamilyVariant& v : variants_for(n)) pr.result.push_back(v);
            pr.members = all_m;
            pr.title = people[all_m[0]].name;
            if (all_m.size() < 2) continue;
            if (f < 0) pr.change = "new";
            else {
                const std::vector<FamilyVariant>& was = families[size_t(f)].all;
                pr.change = same_list(pr.result, was) ? "none" : pr.result.size() > was.size() ? "added" : "reordered";
                if (pr.change != "none") pr.note = "one of the game's own default families: put in the usual order, with the others added";
            }
        }
        props.push_back(std::move(pr));
    }
    // -- applied (the editor's consolidate(): each proposal it ticks, in order -- its join()) ---------------------------------
    std::vector<bool> touched(families.size(), false);
    size_t made = 0;
    for (const AmProp& pr : props) {
        if (pr.change == "none" || pr.change == "skipped") continue;
        std::vector<int> fams;
        for (uint16_t n : pr.members) { const int f = family_index_of(n); if (f >= 0 && std::find(fams.begin(), fams.end(), f) == fams.end()) fams.push_back(f); }
        for (size_t i = 1; i < fams.size(); i++) { families[size_t(fams[i])].all.clear(); touched[size_t(fams[i])] = true; }
        const bool fresh = fams.empty() || families[size_t(fams[0])].is_default;
        int t = fams.empty() ? -1 : fams[0];
        if (t < 0) {
            Family f;
            const std::string first = sid_of(pr.members[0]);
            f.name = first.substr(0, first.find('~'));
            t = int(families.size());
            families.push_back(std::move(f));
            touched.push_back(true);
        }
        Family& f = families[size_t(t)];
        f.all = pr.result;
        touched[size_t(t)] = true;
        f.is_default = false;
        bool short_one = false;                          // (a member who brings fewer than his four: "unlisted" off, for a fresh one)
        for (const FamilyVariant& v : f.all) {
            if (v.id == f.all[0].id) continue;
            size_t c = 0;
            for (const FamilyVariant& x : f.all) c += x.id == v.id;
            if (c < 4) short_one = true;
        }
        if (short_one && fresh) f.unlisted_off = true;
        made++;
    }
    // -- the editor's tidy(), then the families as the game reads the file the editor would write (in roster order) ----------
    std::vector<Family> kept;
    for (size_t i = 0; i < families.size(); i++) {
        Family& f = families[i];
        std::vector<uint16_t> ws;
        for (const FamilyVariant& v : f.all) if (std::find(ws.begin(), ws.end(), v.id) == ws.end()) ws.push_back(v.id);
        bool alone = ws.size() == 1 && f.all.size() == 4;
        for (size_t k = 0; alone && k < 4; k++) alone = f.all[k].id == ws[0] && f.all[k].variant == k;
        if (f.all.empty() || (alone && touched[i] && !(ws[0] < nested_kid.size() && nested_kid[ws[0]]))) continue;
        kept.push_back(std::move(f));
    }
    if (made) std::stable_sort(kept.begin(), kept.end(), [&](const Family& a, const Family& b) { return order_key(a.all[0].id) < order_key(b.all[0].id); });
    families = std::move(kept);
    family_of_id.assign(0x1000, -1);
    for (size_t i = 0; i < families.size(); i++) {
        Family& f = families[i];
        f.slot = f.all.empty() ? 0 : f.all[0].id;      // (its place: the first listed wrestler's box, hidden or not)
        f.variants.clear();
        for (const FamilyVariant& v : f.all) {
            if (family_of_id[v.id] < 0) family_of_id[v.id] = int32_t(i);
            if (!(hidden_mask[v.id] & (1u << v.variant))) f.variants.push_back(v);
        }
    }
    // -- the log ---------------------------------------------------------------------------------------------------------------
    // A line a proposal, as the editor's preview has it: the change (new / added / reordered: made; none: as it is already;
    // skipped: why), its key, the family's name, its members in order, then its variants; then the families it ends with.
    size_t changed = 0;
    for (const AmProp& pr : props) {
        std::string m, r;
        for (uint16_t n : pr.members) m += (m.empty() ? "" : " + ") + sid_of(n);
        for (const FamilyVariant& v : pr.result) r += (r.empty() ? "" : ", ") + sid_of(v.id) + " " + std::to_string(v.variant + 1);
        fprintf(stderr, "[families] auto-merge: %s | %s | %s | %s | %s%s%s\n", pr.change.c_str(), pr.key.c_str(), pr.title.c_str(), m.c_str(), r.c_str(),
                pr.note.empty() ? "" : " | ", pr.note.c_str());
        changed += pr.change != "none" && pr.change != "skipped";
    }
    for (size_t i = 0; i < families.size(); i++) {
        std::string r;
        for (const FamilyVariant& v : families[i].all) r += (r.empty() ? "" : ", ") + sid_of(v.id) + " " + std::to_string(v.variant + 1);
        fprintf(stderr, "[families] auto-merge family %zu, %s%s: %s\n", i + 1, families[i].name.c_str(), families[i].unlisted_off ? " (unlisted: off)" : "", r.c_str());
    }
    fprintf(stderr, "[families] auto-merge: MERGE SAME WRESTLERS is on: %zu families made or changed by name (%zu proposals), %zu families listed, no file written\n",
            changed, props.size(), families.size());
}

// The families, ready for the mod: read at the scan; at the first question, MERGE SAME WRESTLERS (when it is on) and then
// families_finish. (The mod asks only once the game runs: its options are there by then.)
void families_ready() {
    scan();
    if (families_done) return;
    families_done = true;
    if (auto_merge_setting()) families_auto_merge();
    else fprintf(stderr, "[families] MERGE SAME WRESTLERS (F2 > GAME SETTINGS) is off: the families are the data's\n");
    families_finish();
    fflush(stderr);
}

// ---- families, for the mod (2026-11-14, families: mod src/variants.c) ----------------------------------------------------
// recomp_arcade_family_of(id) -> the family wrestler `id` (a game number) is in, or -1.
void family_of(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    families_ready();                 // (automerge: the families once MERGE SAME WRESTLERS has had its say)
    _return<int32_t>(ctx, id < family_of_id.size() ? family_of_id[id] : -1);
}
// recomp_arcade_family_size(f) -> how many variants family f has (0: no such family).
void family_size(uint8_t* rdram, recomp_context* ctx) {
    const int32_t f = _arg<0, int32_t>(rdram, ctx);
    families_ready();                 // (automerge: the families once MERGE SAME WRESTLERS has had its say)
    _return<int32_t>(ctx, f >= 0 && size_t(f) < families.size() ? int32_t(families[size_t(f)].variants.size()) : 0);
}
// recomp_arcade_family_variant(f, k) -> family f's variant k (0 the first): his game number | his look (0-3) << 16;
// 0 for none.
void family_variant(uint8_t* rdram, recomp_context* ctx) {
    const int32_t f = _arg<0, int32_t>(rdram, ctx), k = _arg<1, int32_t>(rdram, ctx);
    families_ready();                 // (automerge: the families once MERGE SAME WRESTLERS has had its say)
    if (f < 0 || size_t(f) >= families.size() || k < 0 || size_t(k) >= families[size_t(f)].variants.size()) { _return<uint32_t>(ctx, 0u); return; }
    const FamilyVariant& v = families[size_t(f)].variants[size_t(k)];
    _return<uint32_t>(ctx, uint32_t(v.id) | uint32_t(v.variant) << 16);
}
// recomp_arcade_family_slot(f) -> family f's first listed wrestler (whose box is the family's place on the select), 0 none.
void family_slot(uint8_t* rdram, recomp_context* ctx) {
    const int32_t f = _arg<0, int32_t>(rdram, ctx);
    families_ready();                 // (automerge: the families once MERGE SAME WRESTLERS has had its say)
    _return<uint32_t>(ctx, f >= 0 && size_t(f) < families.size() ? uint32_t(families[size_t(f)].slot) : 0u);
}
// recomp_arcade_family_rest(id) -> the family of wrestler id's variants his family doesn't list (his own box), or -1.
void family_rest(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    families_ready();                 // (automerge: the families once MERGE SAME WRESTLERS has had its say)
    _return<int32_t>(ctx, id < rest_of_id.size() ? rest_of_id[id] : -1);
}
// recomp_arcade_hidden(id) -> wrestler id's hidden variants (bit k: variant k + 1; 0xF: he is hidden), from "hidden".
void hidden_of(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    scan();
    _return<uint32_t>(ctx, id < hidden_mask.size() ? uint32_t(hidden_mask[id]) : 0u);
}
// (rostertags, 2026-11-21) recomp_arcade_wrestler_class(id) -> his folder's "class" (CLASS_*), 0 when none gives one.
void wrestler_class(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t id = _arg<0, uint32_t>(rdram, ctx);
    scan();
    _return<uint32_t>(ctx, id < class_of_id.size() ? uint32_t(class_of_id[id]) : 0u);
}
// (rostertags, 2026-11-21) recomp_arcade_rivals(a, b) -> 1: a rivals.json makes them rivals, -1: one says they are not, 0: no word.
void rivals_of(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t a = _arg<0, uint32_t>(rdram, ctx) & 0xFFFu, b = _arg<1, uint32_t>(rdram, ctx) & 0xFFFu;
    scan();
    const auto it = rival_pairs.find(a < b ? a << 12 | b : b << 12 | a);
    _return<int32_t>(ctx, it == rival_pairs.end() ? 0 : int32_t(it->second));
}
// recomp_arcade_family_text(text): a line of the mod's about families, into the log as "[families] ..." -- "@" + four hex
// digits is a game number, written as his string ID (as removal_text does).
void family_text(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) src = _arg<0, PTR(void)>(rdram, ctx);
    std::string t;
    for (int k = 0; k < 400; k++) { const char ch = char(MEM_B(k, src)); if (!ch) break; t += ch; }
    std::string out;
    for (size_t k = 0; k < t.size(); k++) {
        if (t[k] == '@' && k + 4 < t.size() && isxdigit(uint8_t(t[k + 1])) && isxdigit(uint8_t(t[k + 2])) && isxdigit(uint8_t(t[k + 3])) && isxdigit(uint8_t(t[k + 4]))) {
            const uint16_t n = uint16_t(strtol(t.substr(k + 1, 4).c_str(), nullptr, 16));
            const auto s = sid_of_number.find(n);
            char num[16];
            snprintf(num, sizeof num, "0x%04X", unsigned(n));
            out += s != sid_of_number.end() ? s->second + " (" + num + ")" : std::string(num);
            k += 4;
        } else out += t[k];
    }
    fprintf(stderr, "[families] %s\n", out.c_str());
    fflush(stderr);
}
}  // namespace

// (2026-11-13, removal) The removed stock Superstars, with their string IDs: the story compiler names what a story needs.
void arcade_wrestlers_each_removed(const std::function<void(const std::string&, int)>& use) {
    scan();
    for (const Removed& r : removed) use(r.sid, r.id);
}

// Each folder wrestler's name and short name in capitals, with his ID: stories and select_pages.json look wrestlers up by these.
void arcade_wrestlers_each_name(const std::function<void(const std::string&, int)>& use) {
    scan();
    const auto capitals = [](std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = char(c - 32); return s; };
    for (size_t i = 0; i < wrestlers.size() && seatable(i); i++) {      // (every one: the mod seats them as they're needed, 2026-11-13)
        const Wrestler& w = wrestlers[i];
        if (!w.name.empty()) use(capitals(w.name), w.id);
        if (!w.short_name.empty()) use(capitals(w.short_name), w.id);
    }
}

// (2026-11-19, roster) Each folder wrestler's source game (his wrestler.json "source": {"game": ...}; "" none) with his game
// number, in folder order: select_pages.json's {"game": "WCW/nWo Revenge"} puts every one of that game's imports on its page.
void arcade_wrestlers_each_source(const std::function<void(const std::string&, int)>& use) {
    scan();
    for (size_t i = 0; i < wrestlers.size() && seatable(i); i++) use(wrestlers[i].source_game, wrestlers[i].id);
}

// Each folder's string ID with his game number (2026-11-09): stories and select_pages.json name him by it. A stock
// wrestler's folder ("builtin") gives the built-in's number (2026-11-12).
void arcade_wrestlers_each_id(const std::function<void(const std::string&, int)>& use) {
    scan();
    for (size_t i = 0; i < wrestlers.size() && seatable(i); i++) use(wrestlers[i].sid, wrestlers[i].id);   // (every one: seatable)
    for (const auto& [sid, id] : stock_ids) use(sid, id);
}

// The full-detail models' files, for the content pack loader (and so the theme packs' numbers after them), in model order:
// those hd_handed hands out (every one: 2026-11-13, seatsroot).
std::vector<std::filesystem::path> arcade_wrestlers_hd_packs() {
    std::vector<std::filesystem::path> out;
    for (size_t k = 0, n = hd_handed(); k < n; k++) out.push_back(hd_models[k].pack);
    return out;
}
std::vector<std::filesystem::path> arcade_wrestlers_theme_packs() {
    scan();
    std::vector<std::filesystem::path> out;
    for (const Theme& t : themes) out.push_back(t.sound);
    return out;
}
std::vector<std::filesystem::path> arcade_wrestlers_video_packs() {
    scan();
    std::vector<std::filesystem::path> out;
    for (const Video& v : videos) out.push_back(v.file);
    return out;
}
// 2026-11-10: each folder wrestler's ID and folder (arcade_media.cpp: his theme and Titantron video in common formats).
std::vector<std::pair<int, std::filesystem::path>> arcade_wrestlers_folders() {
    scan();
    std::vector<std::pair<int, std::filesystem::path>> out;
    for (const Wrestler& w : wrestlers) out.push_back({ int(w.id), w.folder });
    return out;
}
// The full-detail models' pictures, for the renderer, in model order: every model's (hd_handed's too, 2026-11-13).
std::vector<std::filesystem::path> arcade_wrestlers_hd_pictures() {
    scan();
    std::vector<std::filesystem::path> out;
    for (const HdModel& m : hd_models) out.push_back(m.pictures);
    return out;
}

void arcade_wrestlers_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_wrestler_theme_count", wrestler_theme_count);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_theme_get", wrestler_theme_get);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_video_count", wrestler_video_count);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_video_get", wrestler_video_get);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_hd_count", wrestler_hd_count);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_hd_meta", wrestler_hd_meta);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_count", wrestler_count);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_get", wrestler_get);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_slots", wrestler_slots);
    recomp::overlays::register_base_export("recomp_arcade_moves_note", moves_note);
    recomp::overlays::register_base_export("recomp_arcade_move_lib_n", move_lib_n);       // (2026-11-13, movelib)
    recomp::overlays::register_base_export("recomp_arcade_move_lib_stamp", move_lib_stamp);   // (2026-11-13, movelib2)
    recomp::overlays::register_base_export("recomp_arcade_wrestler_edit", wrestler_edit);
    recomp::overlays::register_base_export("recomp_arcade_move_own", move_own);
    recomp::overlays::register_base_export("recomp_arcade_anim_meta", anim_meta);
    recomp::overlays::register_base_export("recomp_arcade_anim_rom", anim_rom);
    recomp::overlays::register_base_export("recomp_arcade_anim_read", anim_read);
    recomp::overlays::register_base_export("recomp_arcade_movex_where", movex_where);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_file", wrestler_low_file);          // (2026-11-12, handlers)
    recomp::overlays::register_base_export("recomp_arcade_wrestler_model", wrestler_low_model);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_model_note", wrestler_low_note);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_model_where", wrestler_low_where);   // (2026-11-13, handlers1)
    recomp::overlays::register_base_export("recomp_arcade_wrestler_model_check", wrestler_low_check);
    recomp::overlays::register_base_export("recomp_arcade_stock_count", stock_count);       // (2026-11-13, nmhandler: stock folders)
    recomp::overlays::register_base_export("recomp_arcade_stock_get", stock_get);
    recomp::overlays::register_base_export("recomp_arcade_stock_photo", stock_photo);
    recomp::overlays::register_base_export("recomp_arcade_stock_note", stock_note);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_file_pool", wrestler_file_pool);         // (2026-11-13, filepool)
    recomp::overlays::register_base_export("recomp_arcade_wrestler_model_loaded", wrestler_model_loaded);
    recomp::overlays::register_base_export("recomp_arcade_removed_count", removed_count);   // (2026-11-13, removal)
    recomp::overlays::register_base_export("recomp_arcade_removed_get", removed_get);
    recomp::overlays::register_base_export("recomp_arcade_removal_text", removal_text);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_find", wrestler_find);     // (2026-11-13, seatsroot)
    recomp::overlays::register_base_export("recomp_arcade_wrestler_id", wrestler_id);
    recomp::overlays::register_base_export("recomp_arcade_seat_note", seat_note);
    recomp::overlays::register_base_export("recomp_arcade_sidecar_put", sidecar_put);
    recomp::overlays::register_base_export("recomp_arcade_sidecar_get", sidecar_get);
    recomp::overlays::register_base_export("recomp_arcade_family_of", family_of);           // (2026-11-14, families)
    recomp::overlays::register_base_export("recomp_arcade_family_size", family_size);
    recomp::overlays::register_base_export("recomp_arcade_family_variant", family_variant);
    recomp::overlays::register_base_export("recomp_arcade_family_text", family_text);
    recomp::overlays::register_base_export("recomp_arcade_family_slot", family_slot);
    recomp::overlays::register_base_export("recomp_arcade_hidden", hidden_of);
    recomp::overlays::register_base_export("recomp_arcade_family_rest", family_rest);
    recomp::overlays::register_base_export("recomp_arcade_wrestler_class", wrestler_class);   // (rostertags, 2026-11-21)
    recomp::overlays::register_base_export("recomp_arcade_rivals", rivals_of);
}
