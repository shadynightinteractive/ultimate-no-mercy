// [startup 2026-11-19] Test for the wrestler references' fallback (arcade_roster.h resolve_ref): a reference with both a
// string ID and a game number, {"id": "aki_man", "game_number": "0x0379"}, resolves by the number when that wrestler IS
// aki_man, else by the string ID (never by the name on screen); every older form resolves as before. It runs the select's
// pages (arcade_pages.cpp build) and the story compiler (arcade_stories.cpp Pack::who) themselves, built alone.
//   arcade_refs_test <stories\_roster.json> [<wrestlers folder> <string ID> <old number> <his number now>]
// With a wrestlers folder (a game the ROM importer built: ri_cli build): his folders' string IDs and numbers are added to the
// roster as the game's scan adds them, and a select_pages.json naming him by <string ID> + <old number> must give <his number
// now> -- an import renumbered by a clash still found -- while <old number> alone still gives whoever has it now.
// Build (from the source root, the project's toolchain):
//   toolchain\llvm-mingw\bin\clang++ -std=c++20 -O1 -Wno-deprecated-literal-operator
//     -I exe\WWFNoMercyRecomp\lib\N64ModernRuntime\thirdparty exe\WWFNoMercyRecomp\src\main\test\arcade_refs_test.cpp
//     -o <scratch>\arcade_refs_test.exe
// Exit 0 = all passed.
#define ARCADE_STORIES_TEST
#define ARCADE_PAGES_TEST
#include "../arcade_stories.cpp"
#include "../arcade_pages.cpp"

#include <map>

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); failures++; } } while (0)

static std::filesystem::path scratch;

// The select's pages for these entries (one page): the IDs the pack gives, in order.
static std::vector<int> pages_of(const nlohmann::ordered_json& entries, const std::filesystem::path& roster_file) {
    const std::filesystem::path f = scratch / "refs_test_select_pages.json";
    {
        std::ofstream o(f, std::ios::binary | std::ios::trunc);
        o << nlohmann::ordered_json({ { "pages", { { { "name", "TEST" }, { "wrestlers", entries } } } } }).dump(1);
    }
    std::vector<uint8_t> b = build(f, roster_file);
    std::filesystem::remove(f);
    std::vector<int> ids;
    for (size_t at = 8; at + 18 <= b.size();) {      // (each page: name[16], flags, count, count x u16)
        const int n = b[at + 17];
        for (int k = 0; k < n; k++) ids.push_back(b[at + 18 + 2 * k] << 8 | b[at + 19 + 2 * k]);
        at += 18 + 2 * size_t(n);
    }
    return ids;
}

// The game's clean_id, as the folder scan makes a string ID from a folder's name (arcade_wrestlers.cpp): lower case, "_"
// for spaces and punctuation.
static std::string clean(const std::string& s) {
    std::string o;
    for (unsigned char c : s) o += (c >= 'A' && c <= 'Z') ? char(c + 32) : ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80 || c == '~') ? char(c) : '_';
    return o;
}

int main(int argc, char** argv) {
    using namespace arcade_stories;
    if (argc != 2 && argc != 6) { std::fprintf(stderr, "usage: arcade_refs_test <_roster.json> [<wrestlers> <string ID> <old number> <number now>]\n"); return 2; }
    const std::filesystem::path roster_file = argv[1];
    scratch = std::filesystem::temp_directory_path();
    std::string err;
    Roster R = load_roster(roster_file, err);
    if (!err.empty()) { std::printf("roster: %s\n", err.c_str()); return 1; }
    const int rock = (int)R["@ID:the_rock"], austin = (int)R["@ID:steve_austin"];
    CHECK(rock > 0 && austin > 0 && rock != austin, "the roster has the_rock and steve_austin");

    // ---- the rule itself (resolve_ref) ----
    std::string note;
    CHECK(resolve_ref(R, rock, "the_rock", note) == rock && note.empty(), "number right + string ID right -> the number");
    CHECK(resolve_ref(R, austin, "the_rock", note) == rock && !note.empty(), "another man's number + string ID -> by the string ID");
    CHECK(resolve_ref(R, 0, "the_rock", note) == rock, "no number (nobody has it) + string ID -> by the string ID");
    CHECK(resolve_ref(R, austin, "", note) == austin && note.empty(), "no string ID -> the number, as today");
    CHECK(resolve_ref(R, austin, "nobody_has_this", note) == austin && !note.empty(), "a string ID nobody has -> the number");
    CHECK(resolve_ref(R, austin, "The Rock", note) == rock, "a string ID written with capitals and spaces -> as the roster keys it");
    CHECK(resolve_ref(R, austin, "THE ROCK", note) == rock, "THE ROCK as a string ID is the_rock (string-ID spelling, not the display-name table)");

    // ---- the select's pages (arcade_pages.cpp who, through build) ----
    using oj = nlohmann::ordered_json;
    char rh[8], ah[8];
    snprintf(rh, sizeof rh, "0x%04X", rock); snprintf(ah, sizeof ah, "0x%04X", austin);
    {
        oj e = oj::array();
        e.push_back(oj{ { "id", "steve_austin" }, { "game_number", ah } });      // right + right
        e.push_back(oj{ { "id", "the_rock" }, { "game_number", ah } });          // Austin's number, the Rock's ID -> the Rock (then a repeat: left out)
        e.push_back(oj{ { "game_number", rh } });                                 // no string ID -> the number
        e.push_back(oj{ { "id", "nobody_has_this" }, { "game_number", ah } });    // unknown ID -> the number (a repeat: left out)
        const std::vector<int> got = pages_of(e, roster_file);
        CHECK(got.size() == 2 && got[0] == austin && got[1] == rock, "pages: right+right -> Austin, Austin's number + the_rock -> the Rock (got %zu)", got.size());
    }
    {   // every older form as before
        const std::vector<int> got = pages_of(oj::array({ ah, "the_rock" }), roster_file);
        CHECK(got.size() == 2 && got[0] == austin && got[1] == rock, "pages: \"0x....\" and a string ID as before");
        const std::vector<int> got2 = pages_of(oj::array({ rock, "STEVE AUSTIN" }), roster_file);
        CHECK(got2.size() == 2 && got2[0] == rock && got2[1] == austin, "pages: a number and a name as before");
    }

    // ---- the story compiler (arcade_stories.cpp Pack::who / star) ----
    {
        const json story = json::object();
        Pack p(story, R);
        CHECK(p.who(json{ { "id", "the_rock" }, { "game_number", ah } }) == rock, "stories: Austin's number + the_rock -> the Rock");
        CHECK(p.who(json{ { "id", "steve_austin" }, { "game_number", ah } }) == austin, "stories: right + right -> the number");
        CHECK(p.who(json{ { "game_number", rh } }) == rock, "stories: a number alone -> the number");
        CHECK(p.who(json("the_rock")) == rock && p.who(json(ah)) == austin, "stories: the older forms as before");
        CHECK(p.star(json{ { "id", "the_rock" }, { "game_number", ah } }) == rock, "stories (star): Austin's number + the_rock -> the Rock");
        bool threw = false;
        try { p.who(json{ { "game_number", "0x2000" } }); } catch (const StoryError&) { threw = true; }
        CHECK(threw, "stories: a game number past 0xFFF is refused");
    }

    // ---- an import renumbered by a clash (ri_place place_batch), in a game the importer built ----
    if (argc == 6) {
        // the folders as the game's scan has them: string ID = "id" when it's a string that isn't a number, else the folder's
        // name; game number = a numeric "id" (the imports pin theirs). Path order, the first of an ID keeps it.
        std::vector<std::filesystem::path> files;
        for (auto& e : std::filesystem::recursive_directory_iterator(argv[2]))
            if (e.path().filename() == "wrestler.json") files.push_back(e.path());
        std::sort(files.begin(), files.end());
        int added = 0;
        std::ifstream rin(roster_file, std::ios::binary);
        nlohmann::ordered_json rj = nlohmann::ordered_json::parse(rin, nullptr, false);     // (the roster + the folders: for the pages)
        for (const auto& f : files) {
            std::ifstream in(f, std::ios::binary);
            const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (!j.is_object()) continue;
            std::string sid = clean(f.parent_path().filename().string());
            long number = 0;
            if (j.contains("id") && j["id"].is_string()) {
                const std::string v = j["id"].get<std::string>();
                char* end = nullptr;
                const long n = v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X') ? strtol(v.c_str() + 2, &end, 16) : -1;
                if (end && *end == 0) number = n; else sid = clean(v);
            }
            if (number <= 0 || number >= 0x1000) continue;          // (no pinned number: the game gives one as it loads)
            if (R.emplace("@ID:" + sid, number).second) { added++; rj["@ID:" + sid] = number; }
        }
        // the select's pages through a roster file that has the folders too (as the game's load_roster has them from its scan)
        const std::filesystem::path rf = scratch / "refs_test_roster.json";
        { std::ofstream o(rf, std::ios::binary | std::ios::trunc); o << rj.dump(1); }
        const std::vector<int> pg = pages_of(nlohmann::ordered_json::array({ nlohmann::ordered_json{ { "id", argv[3] }, { "game_number", argv[4] } }, argv[4] }), rf);
        std::filesystem::remove(rf);
        CHECK(pg.size() == 2 && pg[0] == strtol(argv[5], nullptr, 0) && pg[1] == strtol(argv[4], nullptr, 0),
              "select_pages.json [{\"id\": %s, \"game_number\": %s}, %s] -> [%s, %s] (got %zu: 0x%04X 0x%04X)", argv[3], argv[4], argv[4], argv[5], argv[4],
              pg.size(), pg.size() > 0 ? pg[0] : 0, pg.size() > 1 ? pg[1] : 0);
        const std::string sid = argv[3];
        const long old_n = strtol(argv[4], nullptr, 0), now_n = strtol(argv[5], nullptr, 0);
        std::printf("folders: %d string IDs with numbers added from %s\n", added, argv[2]);
        CHECK(number_of_sid(R, sid) == now_n, "%s has 0x%04lX now in the built game (got 0x%04llX)", sid.c_str(), now_n, (long long)number_of_sid(R, sid));
        // (the roster file has only the built-ins: the folders' IDs are added to R here, where the game's load_roster adds
        //  them from its scan -- so the rule is checked on R, as the pages' and the stories' who() call it)
        CHECK(resolve_ref(R, old_n, sid, note) == now_n, "{\"id\": %s, \"game_number\": %s} -> 0x%04lX (%s)", sid.c_str(), argv[4], now_n, note.c_str());
        std::printf("  %s + %s -> %s\n", sid.c_str(), argv[4], note.empty() ? "the number" : note.c_str());
        std::string n2;
        CHECK(resolve_ref(R, old_n, "", n2) == old_n, "%s alone -> %s, as today (whoever has it now)", argv[4], argv[4]);
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
