// [commentary2 2026-11-18] The commentary pack made from the player's own WWE '13 (Wii) disc image, on the PC, by the game
// itself: no Python, no other program, nothing for the player to do but put the image in roms\.
//
// The user's rules (2026-11-18):
//  - built only the FIRST time the game sees a WWE '13 image in roms\ (on the first start or a later one); after that the
//    pack stays in the game folder for good: never built again, never written over. The "already built" mark is the
//    "commentary" entry of firstrun's record of processed inputs, content\firstrun.json ({"commentary": {"wwe13":
//    {"state": "done", ...}}}); a build that fails is "failed" and tried again on the next start.
//  - existing content always wins: nothing in the commentary folder is ever written over; if the pack's name is taken,
//    it is written under another (wwe13_built.pak, wwe13_built2.pak, ...) and the import report says so. A pack the
//    player put there is used as it is (arcade_commentary_packs.cpp: the player's pack beats the built one).
// How: packbuild\ (comm_builder.cpp) with the shipped recipe assets\commentary\wwe13\, in a background thread at low
// priority (half the cores, at most 4) while the game runs; then the packs are loaded again. Disc images: RVZ or WIA
// (Dolphin's), or a folder of the disc's files; a plain .iso / .wbfs is encrypted and is reported ("convert it to RVZ").
// Not in lockstep (QA replays: the same packs every run) and not with NM_COMMENTARY_BUILD=0.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include "../arcade_host.h"
#include "../arcade_startup_status.h"
#include "json/json.hpp"
#include "packbuild/comm_builder.h"
#include "packbuild/comm_moves.h"
#include "ultramodern/lockstep.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

void low_priority() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
}

void log(const std::string& s) { fprintf(stderr, "[commentary build] %s\n", s.c_str()); fflush(stderr); }

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// content\firstrun.json: read whole, change only our "commentary" entry, write whole (firstrun.cpp keeps other keys)
// (firstrun3 2026-11-18) Another program -- an antivirus scan, a backup or sync tool, a copy of the folder -- may hold a file
// for a moment: an open or a rename refused is tried again a few times, waiting a little longer each time (about 5 s in all).
// Reading the record matters most: read as empty, writing it back would lose the mod builder's keys.
template <class F> bool with_retries(F&& attempt) {
    for (int k = 0;; k++) {
        if (attempt()) return true;
        if (k == 9) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(100 * (k + 1), 1000)));
    }
}
std::mutex record_mtx;
nlohmann::json read_record(const fs::path& note) {
    std::error_code ec;
    std::string text;
    with_retries([&] {
        std::ifstream in(note, std::ios::binary);
        if (!in) return !fs::exists(note, ec);   // (no record yet: nothing to wait for)
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return !in.bad();
    });
    try {
        nlohmann::json j = nlohmann::json::parse(text);
        return j.is_object() ? j : nlohmann::json::object();
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json::object();
    }
}
bool write_record_entry(const fs::path& note, const std::string& source, const nlohmann::json& entry) {
    std::lock_guard<std::mutex> lk(record_mtx);
    nlohmann::json rec = read_record(note);
    if (!rec.contains("commentary") || !rec["commentary"].is_object()) rec["commentary"] = nlohmann::json::object();
    rec["commentary"][source] = entry;
    std::error_code ec;
    fs::create_directories(note.parent_path(), ec);
    const fs::path tmp = note.string() + ".tmp";
    const std::string text = rec.dump(1) + "\n";
    const bool ok = with_retries([&] {
        {
            std::ofstream o(tmp, std::ios::binary);
            if (!o) return false;
            o << text;
            if (!o) return false;
        }
        fs::rename(tmp, note, ec);
        return !ec;
    });
    if (!ok) fs::remove(tmp, ec);
    return ok;
}

// a name in the commentary folder that nothing has: base.ext, base_built.ext, base_built2.ext, ...
fs::path free_name(const fs::path& dir, const std::string& base, const std::string& ext) {
    std::error_code ec;
    for (int k = 0;; k++) {
        const std::string n = base + (k == 0 ? "" : k == 1 ? "_built" : "_built" + std::to_string(k)) + ext;
        // (a pack's parts count too: name.pak.1 taken means name.pak is someone's)
        if (!fs::exists(dir / n, ec) && !fs::exists(dir / (n + ".1"), ec)) return dir / n;
    }
}

bool write_new(const fs::path& to, const void* data, size_t n) {
    const fs::path tmp = to.string() + ".part";
    {
        std::ofstream o(tmp, std::ios::binary);
        if (!o) return false;
        o.write((const char*)data, (std::streamsize)n);
        if (!o) return false;
    }
    std::error_code ec;
    if (fs::exists(to, ec)) { fs::remove(tmp, ec); return false; }   // (never over anything: someone wrote it meanwhile)
    if (!with_retries([&] { fs::rename(tmp, to, ec); return !ec; })) { fs::remove(tmp, ec); return false; }
    return true;
}

std::string now_text() {
    const std::time_t t = std::time(nullptr);
    char b[32];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", std::localtime(&t));
    return b;
}

// the disc images (and extracted disc folders) in roms\: candidates, by name
std::vector<fs::path> disc_candidates(const fs::path& roms) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(roms, ec)) {
        const std::string ext = lower(e.path().extension().string());
        if (e.is_regular_file(ec) && (ext == ".rvz" || ext == ".wia" || ext == ".iso" || ext == ".wbfs" || ext == ".gcz" ||
                                      ext == ".ciso" || ext == ".gcm" || ext == ".wdf"))
            out.push_back(e.path());
        else if (e.is_directory(ec))
            out.push_back(e.path());   // (a folder of the disc's files, as Dolphin's "Extract Entire Disc" writes it)
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::atomic<bool> running{false};

}  // namespace

// (startup 2026-11-19) Whether this start will make the commentary pack: the recipe is there, it was never made (nor
// failed: a retry of an image that can't be read stays quiet), and roms\ holds a disc image file or an extracted disc
// (a folder with sys\ and files\). Cheap -- the folder listed, nothing opened -- so the start-up status window (main.cpp)
// can say so before the game starts; the build itself runs in the background as before.
bool arcade_commentary_will_build() {
    const char* off = std::getenv("NM_COMMENTARY_BUILD");
    if ((off && std::strcmp(off, "0") == 0) || ultramodern::lockstep::on()) return false;
    const fs::path top = arcade::program_folder();
    std::error_code ec;
    if (!fs::is_regular_file(top / "assets" / "commentary" / "wwe13" / "recipe.json", ec)) return false;
    const nlohmann::json rec = read_record(top / "content" / "firstrun.json");
    if (rec.contains("commentary") && rec["commentary"].is_object() && rec["commentary"].contains("wwe13") &&
        rec["commentary"]["wwe13"].is_object() && !rec["commentary"]["wwe13"].value("state", std::string()).empty()) return false;
    for (const fs::path& p : disc_candidates(top / "roms"))
        if (!fs::is_directory(p, ec) || (fs::is_directory(p / "sys", ec) && fs::is_directory(p / "files", ec))) return true;
    return false;
}

void arcade_commentary_build_start(void (*done)()) {
    const char* off = std::getenv("NM_COMMENTARY_BUILD");
    if (off && std::strcmp(off, "0") == 0) return;
    if (ultramodern::lockstep::on()) return;
    if (running.exchange(true)) return;
    const fs::path top = arcade::program_folder(), note = top / "content" / "firstrun.json";
    const fs::path recipe_dir = top / "assets" / "commentary" / "wwe13";
    std::error_code ec;
    if (!fs::is_regular_file(recipe_dir / "recipe.json", ec)) { running = false; return; }   // (no recipe: nothing to build)
    {
        const nlohmann::json rec = read_record(note);
        if (rec.contains("commentary") && rec["commentary"].is_object() && rec["commentary"].contains("wwe13") &&
            rec["commentary"]["wwe13"].is_object() && rec["commentary"]["wwe13"].value("state", std::string()) == "done") {
            running = false;
            return;                                                                        // (built once: never again)
        }
    }
    if (!fs::is_directory(top / "roms", ec)) { running = false; return; }
    std::thread([top, note, recipe_dir, done]() {
        low_priority();
        using namespace comm_build;
        Recipe r;
        std::string why;
        if (!load_recipe(recipe_dir, r, why)) { log("the recipe: " + why); running = false; return; }
        // which image in roms\ is WWE '13: the one holding the recipe's lines (at least half of them)
        std::unique_ptr<Media> media;
        fs::path image;
        std::vector<std::string> refused;
        for (const fs::path& p : disc_candidates(top / "roms")) {
            std::string w;
            std::unique_ptr<Media> m = Media::open(p, w);
            if (!m) {
                // (a folder that isn't a disc, or another game's image: said in the log only)
                if (!fs::is_directory(p)) refused.push_back(arcade::shown(p) + ": " + w);
                continue;
            }
            size_t needed = 0;
            const size_t found = recipe_media_found(r, *m, needed);
            if (found * 2 >= needed && found > 0) { media = std::move(m); image = p; break; }
            log(arcade::shown(p) + ": " + std::to_string(found) + " of WWE '13's " + std::to_string(needed) + " lines: not WWE '13 (Wii)");
        }
        if (!media) {
            for (const std::string& s : refused) log(s);
            if (!refused.empty()) {
                // (a disc image is there but can't be read: recorded, so the player can see why; tried again next start)
                const nlohmann::json old = read_record(note);
                const bool same = old.contains("commentary") && old["commentary"].is_object() && old["commentary"].contains(r.source) &&
                                  old["commentary"][r.source].is_object() && old["commentary"][r.source].value("state", std::string()) == "failed" &&
                                  old["commentary"][r.source].contains("why") && old["commentary"][r.source]["why"] == nlohmann::json(refused);
                if (!same) {
                    // and said in a report in the commentary folder, once per reason (e.g. an encrypted .iso without the
                    // player's key file: add the key file, or convert it to RVZ) -- under a free name, never over a file
                    const fs::path dir = top / "commentary";
                    std::error_code ec3;
                    fs::create_directories(dir, ec3);
                    const fs::path report = free_name(dir, fs::path(r.pack).stem().string() + "_import_report", ".txt");
                    std::string text = "Commentary pack: not made yet (" + now_text() + ")\n\nA disc image in roms\\ could not be read:\n";
                    for (const std::string& s : refused) text += "  " + s + "\n";
                    text += "\nThe game tries again on its next start.\n";
                    const bool report_ok = write_new(report, text.data(), text.size());
                    write_record_entry(note, r.source, { { "state", "failed" }, { "when", now_text() }, { "why", refused },
                                                         { "report", report_ok ? report.filename().string() : std::string() } });
                }
            }
            running = false;
            return;
        }
        // (the pack's moves table: made from the player's own No Mercy ROM -- the names are the game's text, never shipped)
        if (r.moves.empty() && !moves_from_rom(r, find_nomercy_rom({ top / "roms", top }), why)) {
            log("the moves table: " + why);
            write_record_entry(note, r.source, { { "state", "failed" }, { "when", now_text() }, { "why", why } });
            running = false;
            return;
        }
        const std::string gid = media->game_id().empty() ? std::string() : " (" + media->game_id() + ")";
        log("making the commentary pack from " + arcade::shown(image) + gid + ", in the background");
        startup_status::say(startup_status::Kind::Making, "the commentary pack from " + image.filename().string() + gid);   // (startup: the log's list)
        {
            nlohmann::json e = { { "state", "failed" }, { "when", now_text() }, { "image", arcade::shown(image) }, { "why", "stopped while building" } };
            write_record_entry(note, r.source, e);   // (until done: a stop half way is tried again)
        }
        const int threads = std::max(1, std::min(4, (int)std::thread::hardware_concurrency() / 2));
        std::vector<uint8_t> pack;
        BuildReport rep;
        const auto t0 = std::chrono::steady_clock::now();
        std::atomic<int> last_tenth{0};
        const bool ok = build_pack(*media, r, pack, rep, why, threads, [&](size_t d, size_t n) {
            const int tenth = (int)(d * 10 / n);
            if (tenth > last_tenth.exchange(tenth)) log(std::to_string(d) + " of " + std::to_string(n) + " clips");
        }, nullptr, low_priority);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (!ok) {
            log("failed: " + why);
            write_record_entry(note, r.source, { { "state", "failed" }, { "when", now_text() }, { "image", arcade::shown(image) }, { "why", why } });
            running = false;
            return;
        }
        // the pack and its report: never over anything already in the commentary folder
        const fs::path dir = top / "commentary";
        std::error_code ec2;
        fs::create_directories(dir, ec2);
        const std::string stem = fs::path(r.pack).stem().string();
        const fs::path out = free_name(dir, stem, ".pak");
        const fs::path report = free_name(dir, stem + "_import_report", ".txt");
        if (!write_new(out, pack.data(), pack.size())) {
            log("can't write " + arcade::shown(out));
            write_record_entry(note, r.source, { { "state", "failed" }, { "when", now_text() }, { "why", "can't write " + arcade::shown(out) } });
            running = false;
            return;
        }
        std::ostringstream t;
        t << "Commentary pack made by the game from your own disc (" << now_text() << ")\n\n"
          << "disc:    " << arcade::shown(image) << gid << "\n"
          << "pack:    " << arcade::shown(out) << " -- " << rep.clips << " clips, " << rep.lines << " lines, SHA-1 " << rep.pack_sha1 << "\n";
        if (out.filename().string() != r.pack)
            t << "         (" << r.pack << " was already in the commentary folder: it is kept as it is, and the game uses it --\n"
              << "          a pack you put there always comes first. Remove it to use the one made from your disc.)\n";
        // [commdisc 2026-11-18] "expected" = the canonical pack, the one this builder makes from the real disc (recipe.json)
        t << "clips:   " << rep.identical << " identical to the expected ones, " << rep.equivalent << " the same lines encoded slightly\n"
          << "         differently, " << rep.wrong << " left out (not the expected line), " << rep.missing << " left out (not on this disc,\n"
          << "         or not yet placed in this version's recipe)\n"
          << "result:  " << (rep.same_as_target ? "the pack is byte for byte the expected one (" : "the pack is not byte for byte the expected one (") << r.target_sha1 << ")\n"
          << "time:    " << (int)secs << " s\n\n"
          << "It is made once. The game will not make it again or change it: delete it and the \"commentary\" entry of\n"
          << "content\\firstrun.json to have it made again.\n";
        if (!rep.notes.empty()) {
            t << "\nNotes:\n";
            for (const std::string& n : rep.notes) t << "  " << n << "\n";
        }
        const std::string text = t.str();
        const bool report_ok = write_new(report, text.data(), text.size());
        write_record_entry(note, r.source, {
            { "state", "done" }, { "when", now_text() }, { "image", arcade::shown(image) }, { "game_id", media->game_id() },
            { "pack", out.filename().string() }, { "pack_sha1", rep.pack_sha1 }, { "same_as_expected", rep.same_as_target },
            { "clips", rep.clips }, { "identical", rep.identical }, { "equivalent", rep.equivalent }, { "wrong", rep.wrong },
            { "missing", rep.missing }, { "report", report_ok ? report.filename().string() : std::string() } });
        log("done in " + std::to_string((int)secs) + " s: " + arcade::shown(out) + " (" + std::to_string(rep.clips) + " clips; " +
            (rep.same_as_target ? "byte for byte the expected pack" : "not byte for byte the expected pack: see the report") + ")");
        startup_status::say(startup_status::Kind::Added, "the commentary pack " + out.filename().string() + " (" + std::to_string(rep.lines) + " lines)");
        running = false;
        if (done) done();
    }).detach();
}
