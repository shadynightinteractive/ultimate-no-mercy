// ri_build.cpp (phase3, 2026-11-19): the ROM importer's first-start step (ri_build.h).
#include "ri_build.h"
#include "ri_movelib.h"
#include "ri_place.h"
#include "ri_redux.h"
#include "ri_wmimport.h"
#include "ri_wmmoves.h"
#include "ri_revimport.h"
#include "ri_revmoves.h"
#include "ri_imports2.h"
#include "ri_goldust.h"
#include "ri_wmport.h"
#include "ri_ports.h"
#include "ri_content.h"
#include "ri_photos.h"
#include "ri_hdrfill.h"
#include "ri_images.h"
#include <map>
#include <memory>
#include <optional>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace ri {

using json = nlohmann::json;

uint32_t sources_present(Roms& R) {
    uint32_t s = 0;
    if (R.rom("revenge")) s |= SRC_REVENGE;
    if (R.rom("wm2000")) s |= SRC_WM2000;
    if (R.rom("nmplus_v3")) s |= SRC_NMPLUS;
    if (R.rom("revenge_redux")) s |= SRC_REDUX;
    return s;
}

// (startup 2026-11-19) the names people know the sources by, for the start-up status window
const char* game_title(const std::string& game) {
    if (game == "nomercy") return "WWF No Mercy (Rev 1)";
    if (game == "nomercy10") return "WWF No Mercy (v1.0)";
    if (game == "nmplus_v3") return "No Mercy Plus V3 (v1.0 + its patch)";
    if (game == "revenge") return "WCW/nWo Revenge";
    if (game == "wm2000") return "WWF WrestleMania 2000";
    if (game == "baddblood") return "WWF WarZone - Badd Blood";
    if (game == "revenge_redux") return "WCW/nWo Revenge Redux patch";
    return "another game";
}
static const char* pack_title(const std::string& file) {
    if (file == "movelib_revenge.bin") return "WCW/nWo Revenge's moves";
    if (file == "movelib_wm2000.bin") return "WrestleMania 2000's moves";
    if (file == "content.bin") return "the themes and Titantron videos of the built-in extras";
    if (file == "wmport.bin") return "the arenas and built-in Superstars from WrestleMania 2000 and/or Revenge";   // (arenasplit: either)
    if (file == "modextra.bin") return "the built-in extras' tables (Superstars, arenas, media)";
    return "a content pack";
}

static std::string now_text() {
    std::time_t t = std::time(nullptr);
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
    return b;
}

// ---- the game's own packs -------------------------------------------------------------------------------------------
// One entry per pack: its file in content\, the sources it is made from at all, and how it is made (empty: no source of
// it there). A maker that gives no bytes leaves its pack unmade: the game plays without it (the mod leaves out what needs it).
//
// The packs share the steps they are made from -- each made once a start, on first need, and kept until the packs are done
// (Made): the built-in ports' tables (ri_ports: mkroster's roster_ext.h, revanims' rev_anims.json, mkparts, mkovr, move_lib.h),
// the content pack (ri_content: content.bin + content_index.h), wmport (ri_wmport: wmport.bin, its 11 headers, its models).
struct Made {
    std::unique_ptr<Ports> ports;
    std::map<std::string, std::string> headers;      // (the generated headers' text, by name: the mod's src\<name>)
    std::optional<std::string> rev_anims;            // (data\gen_data\rev_anims.json)
    std::optional<ContentPack> content;
    std::optional<WmportOut> wmport;
};
static Made g_made;
// (nmplus 2026-11-19) the build's layouts (firstrun\hdrfill.bin): the shape a source game's absent rows are written in
static std::optional<HdrFill> g_shape;
static const HdrFill& shape_of(const BuildPaths& P) {
    if (!g_shape) g_shape = read_hdrfill(read_file(P.data.parent_path() / "hdrfill.bin"));
    return *g_shape;
}
static std::vector<std::pair<std::string, std::string>> g_wmport_headers;
const std::vector<std::pair<std::string, std::string>>& wmport_headers_made() { return g_wmport_headers; }
static std::string text_file(const fs::path& p) { Bytes b = read_file(p); return std::string(b.begin(), b.end()); }

static Ports& ports(Roms& R, const BuildPaths& P) {
    if (!g_made.ports) {
        g_made.ports = std::make_unique<Ports>(R, P.data);
        // (nmplus: No Mercy Plus's men's rows zero, in the build's shape; c2hands: WrestleMania 2000's two likewise)
        // (romsets 2026-11-24: Revenge's likewise -- its ports, Jarrett and the Meanie -- when No Mercy Plus comes without it)
        if (!R.rom("nmplus_v3") || !R.rom("wm2000") || !R.rom("revenge")) g_made.ports->set_shape(&shape_of(P));
    }
    return *g_made.ports;
}
static const std::string& header_made(const std::string& name, const std::function<std::string()>& make) {
    auto it = g_made.headers.find(name);
    if (it == g_made.headers.end()) it = g_made.headers.emplace(name, make()).first;
    return it->second;
}
// mkroster (gen/mkroster.py): roster_ext.h
static const std::string& roster_ext(Roms& R, const BuildPaths& P) { return header_made("roster_ext.h", [&] { return ports(R, P).roster_ext_h(); }); }
// revanims (gen/revanims.py): rev_anims.json -- an input of wmport.bin and of move_lib.h
static const std::string& rev_anims(Roms& R, const BuildPaths& P) {
    if (!g_made.rev_anims) g_made.rev_anims = ports(R, P).revanims().json;
    return *g_made.rev_anims;
}
// mkcontent (gen/mkcontent.py: ri_content): the themes' samples and the Titantron frames; it reads the roster_ext.h text and our
// mod/gen/roster_base.json (firstrun/romimport/roster_base.json). The player's own music files are not here: the pack is the
// no-music-files configuration.
static const ContentPack& content_made(Roms& R, const BuildPaths& P) {
    if (!g_made.content) {
        // (nmplus: without Revenge Redux, user_music's rows past the made ones are no one's -- the build's count of them)
        const size_t um = R.rom("revenge_redux") ? 0 : hdr_symbol_size(shape_of(P), "user_music") / 4;
        // (content2: without No Mercy Plus, the build's rows -- ct_song's 28 bytes each, the made music slots' effects 4 -- so
        //  every row is where the build has it)
        ContentShape cs;
        if (!R.rom("nmplus_v3")) { cs.songs = hdr_symbol_size(shape_of(P), "ct_song") / 28; cs.ext_slots = hdr_symbol_size(shape_of(P), "ext_slot_fx") / 4; }
        g_made.content = make_content(R, P.data / "loadmaps", text_file(P.data / "roster_base.json"), roster_ext(R, P), um,
                                      R.rom("nmplus_v3") ? nullptr : &cs);
        g_made.headers["content_index.h"] = g_made.content->index_h;
    }
    return *g_made.content;
}
// (content2 2026-11-19) the content pack is made when a source of its added themes is there: No Mercy Plus (its own themes and
// videos) or Revenge (the built-in ports' themes: Revenge's song 0x214, Redux's songs; WrestleMania 2000's two with it)
// (arenasplit 2026-11-24) or WrestleMania 2000: Jeff Jarrett's and The Blue Meanie's themes, from its own music bank
static bool content_wanted(Roms& R) { return R.rom("nmplus_v3") || R.rom("revenge") || R.rom("wm2000"); }
// (romsets 2026-11-24) the extras' tables (modextra.bin) are made when a source of a built-in Superstar is there: Revenge (its
// ports) or No Mercy Plus V3 (its men, Big Show's moves, its themes and videos). They needed Revenge before, so No Mercy Plus
// without Revenge gave nothing at all. (arenasplit 2026-11-24) Or WrestleMania 2000: Jeff Jarrett, The Blue Meanie and its
// seven arenas, made from it alone now (wmport.bin is made from either of it and Revenge).
static bool extras_wanted(Roms& R) { return R.rom("nmplus_v3") || R.rom("revenge") || R.rom("wm2000"); }
// (arenasplit 2026-11-24) wmport.bin -- the built-in ports' models and the arcade's arenas -- is made from Revenge (its ports,
// its six arenas) or WrestleMania 2000 (Jarrett, the Meanie, its hands and videos, its seven arenas), or both as always
static bool wmport_wanted(Roms& R) { return R.rom("revenge") || R.rom("wm2000"); }
// wmport (gen/wmport.py twice with gen/arenas_collide.py between: ri_wmport + ri_collide): the pack, its 11 headers, its models
static const WmportOut& wmport_made(Roms& R, const BuildPaths& P) {
    if (!g_made.wmport) {
        // (arenasplit 2026-11-24: Revenge's move animations only with Revenge; the build's layouts for an absent game's rows)
        const bool full = R.rom("revenge") && R.rom("wm2000");
        g_made.wmport = wmport_build(R, RiData{ P.data }, R.rom("revenge") ? rev_anims(R, P) : std::string(), false,
                                     full ? nullptr : &shape_of(P));
        g_wmport_headers = g_made.wmport->headers;
        for (auto& [name, text] : g_made.wmport->headers) g_made.headers[name] = text;
    }
    return *g_made.wmport;
}

// The mod's extras holes' text (ri_hdrfill's HeaderText): every header the holes are made from, made here from the ROMs --
//   roster_ext.h, parts_ext.h, parts_ovr.h, move_lib.h (ri_ports), content_index.h (ri_content), wmport_files.h and the arena
//   headers (ri_wmport), rev_photos.h, jarrett_photo.h, meanie_photo.h, photos_nm.h, mideon / droz / bigshow_photo.h,
//   tv_carriers.h (ri_photos). (handsrom 2026-11-25: WrestleMania 2000's hand poses are wmport_files.h's wmh_pose.)
// (no2k15, 2026-11-23: hd_index.h / hd_index_rock.h, the WWE 2K15 test slots' models -- no ROM gave them -- are gone with them.)
// (nmplus 2026-11-19) No Mercy Plus V3 is a source game of its own: without it, its photo headers aren't made -- their holes
// zero --, and its men's rows in roster_ext.h, parts_ext.h and parts_ovr.h are zeros (ri_ports set_shape). (content2
// 2026-11-19) content_index.h is made with it or with Revenge (the ports' themes): its absent sources' rows empty.
// (romsets 2026-11-24) Without Revenge: no wmport (its 11 headers' holes zero -- the game's own arenas' stage sets come from
// content\wmport_base.bin, as with No Mercy alone), no move library (move_lib.h's hole zero), no Revenge port photos; the ports'
// tables made with Revenge's rows zero in the build's shape (ri_ports rev()). The mod leaves out what needs Revenge (extras.c).
static void extras_headers(Roms& R, const BuildPaths& P) {
    roster_ext(R, P);
    if (content_wanted(R)) content_made(R, P);
    const bool rv = R.rom("revenge") != nullptr;
    static const WmportOut no_wmport{};
    const WmportOut& W = wmport_wanted(R) ? wmport_made(R, P) : no_wmport;   // (arenasplit: Revenge or WrestleMania 2000)
    Ports& X = ports(R, P);
    if (!g_made.headers.count("parts_ext.h")) g_made.headers["parts_ext.h"] = X.mkparts().parts_ext_h;
    if (!g_made.headers.count("parts_ovr.h")) g_made.headers["parts_ovr.h"] = X.parts_ovr_h(W.models);
    if (!g_made.headers.count("move_lib.h") && rv)
        g_made.headers["move_lib.h"] = X.move_lib_h(movelib_revenge(R, P.data / "handlers" / "revenge" / "library_index.json"), rev_anims(R, P));
    if (!g_made.headers.count("rev_photos.h")) {
        for (auto& [name, text] : port_photo_headers(R, RiData{ P.data })) g_made.headers[name] = text;
        for (auto& [name, text] : nm_photo_headers(R, RiData{ P.data })) g_made.headers[name] = text;
    }
    // (handsrom 2026-11-25: WrestleMania 2000's hand poses, hands.c's wm_pose before, are wmport_files.h's wmh_pose now -- made with
    //  the other tables of its hands in ri_wmport -- so no "hands.c" text is made: hands.c holds none of the game's data)
}

// modextra.bin (tools/firstrun/mkplan.py's file): the mod's extras holes filled from the headers' text by firstrun\hdrfill.bin's
// layouts (ri_hdrfill). arcade_extras_in (extras.c) is COMPUTED: the source bits there (the first-start builder sets it again
// from its record when it makes the mod); a hole whose header no ROM gives is zero.
const std::map<std::string, std::string>& extras_texts(Roms& R, const BuildPaths& P) { extras_headers(R, P); return g_made.headers; }
Bytes modextra_from_texts(const HdrFill& F, const std::map<std::string, std::string>& texts, uint32_t bits) {
    HdrFill G = F;
    for (auto& h : G.holes)
        if (h.header != "extras.c" && !texts.count(h.header)) h.flags |= HF_COMPUTED;    // (no text: zero)
    // (nmplus 2026-11-19) without No Mercy Plus its men's symbols aren't in the texts (zero) and tables of them end early;
    // without Revenge Redux, the made music slots' tables end before its songs' rows (zero)
    const uint32_t all = SRC_REVENGE | SRC_WM2000 | SRC_NMPLUS | SRC_REDUX;
    const bool partial = (bits & all) != all;
    auto holes = fill_holes(G, [&](const std::string& name) { return texts.at(name); },
                            [&](const HdrHole& h) {
                                Bytes b(h.size);
                                if (h.symbol == "arcade_extras_in" && h.size == 4) put_be32(b.data(), bits);
                                return b; }, partial);
    return modextra_bin(F, holes);
}
Bytes modextra_made(Roms& R, const BuildPaths& P, std::map<std::string, std::string>* texts) {
    const auto& T = extras_texts(R, P);
    if (texts) *texts = T;
    // (relclean 2026-11-19: load_hdrfill -- the pointer words come from firstrun\mod_binary.holes and plan.bin beside it)
    return modextra_from_texts(load_hdrfill(P.data.parent_path() / "hdrfill.bin"), T, sources_present(R));
}
void made_reset() { g_made.ports.reset(); g_made.headers.clear(); g_made.rev_anims.reset(); g_made.content.reset(); g_made.wmport.reset(); g_shape.reset(); }

struct PackMaker {
    const char* file;
    uint32_t needs;          // made only when all of these are there
    std::function<Bytes(Roms&, const BuildPaths&)> make;
    bool (*wanted)(Roms&) = nullptr;   // (romsets: and, when given, only when this says so -- one of several sources there)
};
static std::vector<PackMaker> pack_makers() {
    return {
        { "movelib_revenge.bin", SRC_REVENGE, [](Roms& R, const BuildPaths& P) {
              return movelib_revenge(R, P.data / "handlers" / "revenge" / "library_index.json"); } },
        { "movelib_wm2000.bin", SRC_WM2000, [](Roms& R, const BuildPaths& P) {
              // (wmlib.py lib: ri_wmmoves -- its two JSON beside it are the developer build's, not needed here)
              WmMoveLib L(R, P.data, P.data / "handlers" / "wm2000" / "wrestlers");
              const fs::path tmp = P.content / "_import_tmp" / "wmlib";
              std::error_code ec; fs::remove_all(tmp, ec);
              L.write_lib(tmp);
              Bytes b = read_file(tmp / "movelib_wm2000.bin");
              fs::remove_all(tmp, ec);
              return b; } },
        // (nmplus 2026-11-19) No Mercy Plus V3 is a source game of its own: the mod's extras (modextra.bin) are made from
        //  Revenge and WrestleMania 2000 with it or without it (its parts zero, in the build's layout: firstrun\hdrfill.bin).
        //  (c2hands 2026-11-19) wmport.bin and the extras' tables are made from Revenge with WrestleMania 2000 or without it:
        //  without it the Revenge ports alone (Revenge's own hands), WrestleMania 2000's two, hands, videos and the arenas left
        //  out (their rows zero, in the build's layout).
        // (content2 2026-11-19) The content pack is every added theme's sample (and No Mercy Plus's Titantron frames), each
        //  played through our own sound script: made with whichever of its sources are there (content_wanted).
        //  With Revenge alone (c2hands) the ports' themes are in it too: Revenge's 0x214, Redux's songs.
        // (romsets 2026-11-24) content.bin and modextra.bin with Revenge OR No Mercy Plus V3 (content_wanted, extras_wanted):
        //  they needed Revenge, so No Mercy Plus alone gave the player nothing of it. wmport.bin is Revenge's (its ports'
        //  models, and WrestleMania 2000's with it): without Revenge the game's own arenas' stage sets come from
        //  content\wmport_base.bin (arcade_firstrun.cpp), as with No Mercy alone.
        { "content.bin", 0, [](Roms& R, const BuildPaths& P) { return content_made(R, P).bin; }, content_wanted },
        // (arenasplit 2026-11-24) wmport.bin with Revenge OR WrestleMania 2000 (wmport_wanted): each game's own part, the
        //  other's arenas' rows zero; content.bin and modextra.bin with WrestleMania 2000 too (Jarrett's and the Meanie's
        //  themes, its arenas' tables).
        { "wmport.bin", 0, [](Roms& R, const BuildPaths& P) { return wmport_made(R, P).pack; }, wmport_wanted },
        { "modextra.bin", 0, [](Roms& R, const BuildPaths& P) { return modextra_made(R, P, nullptr); }, extras_wanted },
    };
}

// ---- the player's content: wrestler folders, per source ROM ---------------------------------------------------------
// maker(R, P, tmp) builds that source's folders into tmp\<collection>\<name>\ and returns them as (collection, name).
struct FolderMaker {
    const char* game;        // the source ROM (roms.py's name)
    uint32_t needs;
    std::function<std::vector<std::pair<std::string, std::string>>(Roms&, const BuildPaths&, const fs::path&)> make;
    const char* also = nullptr;   // another ROM the folders need (not a source bit): until it is there the maker waits
};
static std::vector<FolderMaker> folder_makers() {
    return {
        { "revenge", SRC_REVENGE, [](Roms& R, const BuildPaths& P, const fs::path& tmp) {
              // (aki_import.py revenge --all --rest-only --legacy-ids 0x0340 --records, then --refresh: ri_revimport + ri_revmoves --
              //  the 52 never-ported men. Their themes: Revenge's own 0x214; the Redux step below gives those Redux has a song for
              //  theirs. c2hands 2026-11-19: with WrestleMania 2000 their hands as always; without it Revenge's own hands --
              //  rev_build_model. A folder is made once: a WrestleMania 2000 ROM added later doesn't make them again)
              RevImportData D{ { P.data }, P.data / "handlers" / "revenge" };
              RevMoves M(R, D.ri.dir, D.handler_dir);
              RevFoldersResult r = revenge_folders(R, D, tmp / "Revenge", {}, M.fn());
              std::vector<std::pair<std::string, std::string>> made;
              for (auto& sid : r.made) made.push_back({ "Revenge", sid });
              return made; } },
        { "wm2000", SRC_WM2000, [](Roms& R, const BuildPaths& P, const fs::path& tmp) {
              // (wm2k_import.py + wm2k_refresh.py: ri_wmimport -- the 56 men, the two built-in ports left out as the Python does)
              WmImportData D{ { P.data }, P.data / "handlers" / "wm2000" / "wrestlers", P.data / "roster_ids.json" };
              const fs::path col = tmp / "WrestleMania 2000";
              wm2000_folders(R, D, col);
              std::vector<std::pair<std::string, std::string>> made;
              for (auto& d : fs::directory_iterator(col)) if (d.is_directory()) made.push_back({ "WrestleMania 2000", d.path().filename().string() });
              std::sort(made.begin(), made.end());
              return made; } },
        { "baddblood", 0, [](Roms& R, const BuildPaths& P, const fs::path& tmp) {
              // (imports2.py vader: ri_imports2 -- Badd Blood's Vader, his moveset ri_wmmoves baddblood_moveset)
              // (c2hands 2026-11-19: his moveset is mapped through WrestleMania 2000's move tables -- ri_wmmoves init_base reads
              //  them -- so the maker waits for that ROM: without it the whole first-start import stopped at "wmmoves: the
              //  WrestleMania 2000 and No Mercy ROMs are needed", Revenge's folders and packs included)
              // (arenasplit 2026-11-24: no longer -- Badd Blood is a game of WrestleMania 2000's engine, and without that ROM his
              //  moves are read through Badd Blood's own files and code (ri_wmmoves init_base bb_alone): the same folder, byte for
              //  byte, as with WrestleMania 2000)
              vader_folder(R, RiData{ P.data }, tmp / "Imported");
              return std::vector<std::pair<std::string, std::string>>{ { "Imported", "vader" } }; } },
        // (nmplus 2026-11-19: Goldust is no longer made -- the user's rule, "no item may need two source games": he is No
        //  Mercy Plus V3's Create-a-Wrestler parts with Badd Blood's record, moveset, theme and Titantron, and his photo is a
        //  stand-in (his face paint), not a photo. A folder made by an earlier start stays, as every folder of the player's.
        //  ri_goldust keeps the developer's twin of imports2.py goldust: ri_cli goldust.)
    };
}

BuildResult romimport_build(const BuildPaths& P, json& rec, const std::function<void()>& save) {
    BuildResult res;
    auto say = [&](Say k, const std::string& t) { if (P.say) P.say(k, t); };   // (startup: the status window's list)
    Roms R(P.roms);
    res.sources = sources_present(R);
    for (const char* const* g = ALL_GAMES; *g; g++) if (R.rom(*g)) say(Say::Found, game_title(*g));
    // (romsets 2026-11-24) what is here but can't be used (yet): a patch without the ROM it patches, a ROM that is only a patch's
    // base -- said in the status window, so the player knows what is left out and why
    {
        const bool nm10 = R.rom("nomercy10") != nullptr, rv = R.rom("revenge") != nullptr;
        if (!R.rom("nmplus_v3")) {
            if (R.has_patch("nmplus_v3"))
                say(Say::Removed, nm10 ? std::string("No Mercy Plus V3: its patch doesn't make it from the WWF No Mercy (USA) v1.0 here -- left out")
                                       : std::string("No Mercy Plus V3: its patch is here, but not WWF No Mercy (USA) v1.0, the ROM it patches -- left out"));
            else if (nm10) say(Say::Note, "WWF No Mercy (v1.0) is only No Mercy Plus V3's base: put \"No Mercy Plus V3.xdelta\" beside it for No Mercy Plus");
        }
        if (!R.rom("revenge_redux") && R.has_patch("revenge_redux"))
            say(Say::Removed, rv ? std::string("Revenge Redux: its patch doesn't make it from the WCW/nWo Revenge here -- left out")
                                 : std::string("Revenge Redux: its patch is here, but not WCW/nWo Revenge (USA), the ROM it patches -- left out"));
        // (arenasplit 2026-11-24: Badd Blood's Vader is made from Badd Blood alone now -- no line for him either)
        // (arenasplit 2026-11-24: Jeff Jarrett, The Blue Meanie and the WrestleMania 2000 / Revenge arenas are made from their own
        //  game alone now -- nothing left out with one of the two, so no line for them)
    }
    if (!R.rom("nomercy")) { res.log.push_back("no No Mercy ROM: nothing imported"); return res; }
    std::error_code ec;
    const bool reimport = fs::is_regular_file(P.content / "reimport.txt", ec);
    if (!rec.contains("romimport") || !rec["romimport"].is_object()) rec["romimport"] = json::object();
    json& ri = rec["romimport"];
    std::vector<std::string> report;

    // 1. the packs: made again only when the set of sources changed (or one is missing, or the player asked)
    const uint32_t had = ri.value("packs_sources", 0u);
    for (const auto& [g, bit] : { std::pair<const char*, uint32_t>{ "revenge", SRC_REVENGE }, { "wm2000", SRC_WM2000 },
                                  { "nmplus_v3", SRC_NMPLUS }, { "revenge_redux", SRC_REDUX } })   // (a source there last time, gone now)
        if (ri.contains("packs_sources") && (had & ~res.sources & bit))
            say(Say::Removed, std::string(game_title(g)) + " isn't in roms\\ any more: what needs it is left out (wrestler folders made from it stay)");
    bool missing = false;
    // (romsets: a pack is made when its sources are there -- needs, and wanted when it has one)
    auto can_make = [&](const PackMaker& m) { return m.make && (res.sources & m.needs) == m.needs && (!m.wanted || m.wanted(R)); };
    for (auto& m : pack_makers())
        if (can_make(m) && !fs::is_regular_file(P.content / m.file, ec)) missing = true;
    // (relprep 2026-11-23) content\modextra.bin holds the mod's extras tables in the hole layout of the release that made it
    // (firstrun\hdrfill.bin). A newer release whose layout differs (a hole more or fewer, or one moved) can't use the file
    // that is there: the mod builder left the extras out ("doesn't fit this mod's holes"), so every update had to move the
    // file out by hand. Now the layout it was made in is recorded ("modextra_layout": hdrfill.bin's SHA-256). When the
    // release's layout differs, modextra.bin alone is made again; the other packs don't depend on it. An install from
    // before this record has no "modextra_layout", so its modextra.bin is made again once.
    Bytes layout_bytes;
    const std::string layout = read_file_if(P.data.parent_path() / "hdrfill.bin", layout_bytes) ? sha256_hex(layout_bytes) : std::string();
    // one pack made and written; its SHA-256 recorded in `files`
    auto make_pack = [&](const PackMaker& m, json& files, const char* why) {
        say(Say::Making, pack_title(m.file));
        auto t0 = std::chrono::steady_clock::now();
        Bytes b;
        // (a pack that can't be made is left out -- said in the log -- and the rest go on: e.g. modextra.bin when the mod's
        //  hole layouts, firstrun\hdrfill.bin, were made from other headers than the ones made here)
        try { b = m.make(R, P); }
        catch (const std::exception& e) {
            res.log.push_back(strf("content\\%s not made: %s", m.file, e.what()));
            say(Say::Note, std::string(pack_title(m.file)) + ": not made (" + e.what() + ")"); say(Say::Step, "");
            return;
        }
        say(Say::Step, "");
        if (b.empty()) return;
        write_file(P.content / m.file, b);
        files[m.file] = sha256_hex(b);
        res.changed = true;
        res.log.push_back(strf("made content\\%s (%zu bytes, %.1f s)%s", m.file, b.size(),
                               std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), why));
        say(Say::Added, std::string(pack_title(m.file)) + " (content\\" + m.file + ")");
    };
    if (had != res.sources || missing || reimport) {
        json files = json::object();
        int n_make = 0;
        for (auto& m : pack_makers()) if (can_make(m)) n_make++;
        say(Say::Plan, std::to_string(n_make));
        for (auto& m : pack_makers()) {
            if (!m.make) continue;
            if (!can_make(m)) {
                if (fs::is_regular_file(P.content / m.file, ec) && ri.contains("packs") && ri["packs"].contains(m.file)) {
                    fs::remove(P.content / m.file, ec);         // (ours, made from a source no longer there)
                    res.log.push_back(std::string("removed content\\") + m.file + " (its source ROM is gone)");
                    say(Say::Removed, std::string(pack_title(m.file)) + " (content\\" + m.file + "): its source ROM is gone");
                }
                continue;
            }
            make_pack(m, files, "");
        }
        ri["packs"] = files;
        ri["packs_sources"] = res.sources;
        if (!layout.empty()) ri["modextra_layout"] = layout;
        save();
    } else if (!layout.empty() && ri.value("modextra_layout", std::string()) != layout) {
        // (relprep) the same set of sources, a new release's extras layout: modextra.bin made again in it
        if (!ri.contains("packs") || !ri["packs"].is_object()) ri["packs"] = json::object();
        for (auto& m : pack_makers()) {
            if (std::string(m.file) != "modextra.bin" || !can_make(m)) continue;
            say(Say::Plan, "1");
            make_pack(m, ri["packs"], ": this release's extras layout (firstrun\\hdrfill.bin) is not the one it was made in");
        }
        ri["modextra_layout"] = layout;
        save();
    }
    res.packs_built_for = ri.value("packs_sources", 0u);
    made_reset();                                // (the packs' steps' state: not needed past the packs)

    // 2. the folders: once per source ROM
    if (!ri.contains("folders") || !ri["folders"].is_object()) ri["folders"] = json::object();
    Taken taken;
    taken.scan(P.wrestlers);
    {
        int n_import = 1;                                       // (and the Redux step below)
        for (auto& f : folder_makers()) {
            if (!R.rom(f.game) || !f.make || (res.sources & f.needs) != f.needs || (f.also && !R.rom(f.also))) continue;
            const json& fr = ri["folders"].contains(f.game) ? ri["folders"][f.game] : json();
            if (reimport || !fr.is_object() || fr.value("state", std::string()) != "done" || fr.value("sha1", std::string()) != sha1_hex(*R.rom(f.game))) n_import++;
        }
        say(Say::Plan, std::to_string(n_import));
    }
    for (auto& f : folder_makers()) {
        auto rom = R.rom(f.game);
        if (!rom || !f.make) continue;
        if ((res.sources & f.needs) != f.needs) continue;     // (another ROM it needs isn't there yet: made when it comes)
        if (f.also && !R.rom(f.also)) continue;
        const std::string sha = sha1_hex(*rom);
        json& fr = ri["folders"][f.game];
        if (!reimport && fr.is_object() && fr.value("sha1", std::string()) == sha && fr.value("state", std::string()) == "done") continue;
        fr = { { "sha1", sha }, { "state", "failed" }, { "when", now_text() } };
        save();
        const fs::path tmp = P.content / "_import_tmp" / f.game;
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        say(Say::Making, std::string("the wrestlers from ") + game_title(f.game));
        auto made = f.make(R, P, tmp);
        json list = json::array();
        // (startup 2026-11-19: the whole batch at once -- its own IDs reserved first, only a clashing man renumbered: ri_place.h)
        const size_t report_was = report.size();
        const std::vector<PlaceResult> placed = place_batch(tmp, made, P.wrestlers, f.game, taken, report);
        for (size_t k = report_was; k < report.size(); k++) say(Say::Note, report[k]);   // (startup: a rename or a new ID, in the list too)
        for (size_t i = 0; i < made.size(); i++) {
            const PlaceResult& pr = placed[i];
            if (pr.placed) list.push_back(fs::relative(pr.where, P.wrestlers).generic_string());
            else report.push_back(made[i].first + "\\" + made[i].second + ": not imported -- " + pr.note);
        }
        fs::remove_all(tmp, ec);
        // (rostertags, 2026-11-21) the game's rivals into its collection (aki_import.py / wm2k_import.py: the handler's
        // rivals.json as it is -- Revenge's men's real feuds, WrestleMania 2000's own Rival 1-3), never over one there
        {
            const std::string g = f.game;
            const char* col = g == "revenge" ? "Revenge" : g == "wm2000" ? "WrestleMania 2000" : nullptr;
            const fs::path to = col ? P.wrestlers / col / "rivals.json" : fs::path();
            Bytes rv;
            if (col && !list.empty() && !fs::exists(to, ec) && read_file_if(P.data / "handlers" / g / "rivals.json", rv)) {
                write_file(to, rv);
                res.log.push_back(std::string("wrote wrestlers\\") + col + "\\rivals.json");
            }
        }
        fr["state"] = "done"; fr["folders"] = list;
        res.changed = true;
        res.log.push_back(strf("%s: %zu wrestler folders imported", f.game, list.size()));
        say(Say::Added, strf("%zu wrestler%s from %s (wrestlers\\)", list.size(), list.size() == 1 ? "" : "s", game_title(f.game)));
        say(Say::Step, "");
        save();
    }

    // 3. Revenge Redux's themes into the folders already there (a patch added later; reduxthemes.py)
    {
        nlohmann::ordered_json sec = rec.contains("redux_themes") ? nlohmann::ordered_json::parse(rec["redux_themes"].dump()) : nlohmann::ordered_json::object();
        auto save_sec = [&]() { rec["redux_themes"] = json::parse(sec.dump()); save(); };
        if (R.rom("revenge_redux")) say(Say::Making, "the Revenge Redux themes");
        ReduxResult rr = redux_themes_apply(P.wrestlers, R, sec, save_sec);
        if (rr.ran) { res.changed = true; for (auto& l : rr.log) report.push_back(l); }
        if (rr.done) say(Say::Added, strf("Revenge Redux themes for %d wrestler%s", rr.done, rr.done == 1 ? "" : "s"));
        say(Say::Step, "");
    }

    // 4. the player's report: every rename, every theme kept (appended: a record of each start that imported)
    if (!report.empty()) {
        Bytes old;
        read_file_if(P.content / "import_report.txt", old);
        std::string text(old.begin(), old.end());
        text += "== " + now_text() + "\n";
        for (auto& l : report) text += l + "\n";
        write_file(P.content / "import_report.txt", text);
    }
    return res;
}

}  // namespace ri
