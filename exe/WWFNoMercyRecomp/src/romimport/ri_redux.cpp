// ri_redux.cpp (phase3, 2026-11-19): Revenge Redux's themes into the folders (ri_redux.h; reduxthemes.py apply).
#include "ri_redux.h"
#include "ri_themes.h"
#include <algorithm>
#include <functional>
#include <map>

namespace ri {

using ojson = nlohmann::ordered_json;

namespace {
struct Folder { fs::path dir; std::string fid, code; };

// every wrestler folder (a wrestler.json; folders starting with _ skipped, as the game does), in os.walk's order: a
// folder, then its subfolders by name
void walk(const fs::path& d, std::vector<Folder>& out) {
    std::error_code ec;
    if (fs::is_regular_file(d / "wrestler.json", ec)) {
        Bytes b;
        if (read_file_if(d / "wrestler.json", b)) {
            try {
                auto w = ojson::parse(std::string(b.begin(), b.end()));
                std::string fid;
                if (w.contains("string_id") && w["string_id"].is_string() && !w["string_id"].get<std::string>().empty()) fid = w["string_id"].get<std::string>();
                else if (w.contains("id") && w["id"].is_string()) fid = w["id"].get<std::string>();
                std::string code;
                if (w.contains("source") && w["source"].is_object()) {
                    auto& s = w["source"];
                    if (s.value("game", std::string()) == "WCW/nWo Revenge" && s.contains("code") && s["code"].is_string()) code = s["code"].get<std::string>();
                }
                if (!fid.empty()) out.push_back({ d, fid, code });
            } catch (...) {}
        }
    }
    std::vector<fs::path> subs;
    for (auto it = fs::directory_iterator(d, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        if (it->is_directory(ec) && it->path().filename().string()[0] != '_') subs.push_back(it->path());
    std::sort(subs.begin(), subs.end(), [](const fs::path& a, const fs::path& b) { return a.filename().u8string() < b.filename().u8string(); });
    for (auto& s : subs) walk(s, out);
}

bool read_opt(const fs::path& p, Bytes& b) { return read_file_if(p, b); }
}  // namespace

ReduxResult redux_themes_apply(const fs::path& wrestlers, Roms& R, ojson& rec, const std::function<void()>& save) {
    ReduxResult res;
    auto rom = R.rom("revenge_redux");
    if (!rom) { res.log.push_back("no WCW/nWo Revenge Redux patch in roms\\: nothing to do (the Revenge Superstars keep Revenge's own theme)"); return res; }
    const std::string inp = sha1_hex(*rom);
    if (rec.is_object() && rec.value("input", std::string()) == inp && rec.value("state", std::string()) == "done") {
        res.log.push_back("Revenge Redux's themes: nothing new (done for this patch already)");
        return res;
    }
    res.ran = true;
    ojson wrote = rec.is_object() && rec.contains("wrote") && rec["wrote"].is_object() ? rec["wrote"] : ojson::object();
    rec = ojson::object(); rec["input"] = inp; rec["state"] = "failed"; rec["wrote"] = wrote;
    save();
    // Revenge's own theme as the importer writes it (no vanilla Revenge ROM: only this step's own files are replaced)
    std::optional<ThemeFiles> std_theme;
    if (auto rv = R.rom("revenge")) std_theme = revenge_theme_files(*rv);
    std::map<int, ThemeFiles> made;
    std::vector<Folder> fs_;
    walk(wrestlers, fs_);
    for (auto& f : fs_) {
        auto c = redux_code_of_folder(f.fid, f.code);
        if (!c) continue;
        auto song = redux_song_for(*rom, *c);
        if (!song) continue;
        std::string rel = fs::relative(f.dir, wrestlers).generic_string();
        Bytes cb, cm;
        bool hb = read_opt(f.dir / "theme.bin", cb), hm = read_opt(f.dir / "theme.meta", cm);
        if (!made.count(*song)) made[*song] = redux_theme_files(*rom, *song);
        const ThemeFiles& nw = made[*song];
        if (hb && hm && cb == nw.bin && cm == nw.meta) { res.same++; continue; }
        bool ours = (std_theme && hb && hm && cb == std_theme->bin && cm == std_theme->meta);
        if (!ours && hb && hm && wrote.contains(rel)) {
            Bytes both = cb; both.insert(both.end(), cm.begin(), cm.end());
            ours = sha256_hex(both) == wrote[rel].get<std::string>();
        }
        bool others = false;
        std::error_code ec;
        for (auto it = fs::directory_iterator(f.dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::string n = it->path().filename().string();
            std::string l = n; for (auto& ch : l) ch = (char)tolower((unsigned char)ch);
            if (l.rfind("theme.", 0) == 0 && l != "theme.bin" && l != "theme.meta") others = true;
        }
        if (!ours || others) { res.log.push_back("  " + rel + ": kept the theme there (the player's: never written over)"); res.kept++; continue; }
        write_file(f.dir / "theme.bin", nw.bin); write_file(f.dir / "theme.meta", nw.meta);
        Bytes both = nw.bin; both.insert(both.end(), nw.meta.begin(), nw.meta.end());
        wrote[rel] = sha256_hex(both);
        res.log.push_back("  " + rel + strf(": Revenge Redux's song 0x%02X", *song));
        res.done++;
    }
    rec = ojson::object(); rec["input"] = inp; rec["state"] = "done"; rec["wrote"] = wrote;
    save();
    res.log.push_back(strf("Revenge Redux's themes: %d folders given theirs, %d already had it, %d kept the player's", res.done, res.same, res.kept));
    return res;
}

}  // namespace ri
