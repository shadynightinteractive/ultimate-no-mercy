// ri_selftest_redux.inc (phase3, 2026-11-19): reduxthemes.py's --selftest, in C++ (included by ri_cli.cpp): the user's rules
// on made-up folders (theme files made here), with the real ROMs: no patch -> nothing written; the patch -> the importer's
// themes replaced, the player's kept, a taken-out theme not put back; a second start -> nothing written; a new input later
// -> the player's changed theme kept, this step's own still its own.
static int redux_selftest(const fs::path& roms, const fs::path& tmp) {
    using ojson = nlohmann::ordered_json;
    std::error_code ec;
    if (fs::exists(tmp, ec) && !fs::is_empty(tmp, ec)) fail("an EMPTY scratch folder");
    Roms R(roms);
    auto rv = R.rom("revenge"); auto rx = R.rom("revenge_redux");
    if (!rv || !rx) fail("the selftest needs Revenge and the Redux patch in the roms folder");
    ThemeFiles stdt = revenge_theme_files(*rv);
    const fs::path W = tmp / "wrestlers", C = tmp / "content", E = tmp / "no_roms";
    fs::create_directories(E);
    auto man = [&](const std::string& rel, const std::string& fid, const Bytes* tb, const Bytes* tm, const std::string& code = "", const std::string& game = "WCW/nWo Revenge") {
        fs::path d = W / rel; fs::create_directories(d);
        ojson w = ojson::object(); w["id"] = fid; std::string up = fid; for (auto& c : up) c = (char)toupper((unsigned char)c); w["name"] = up;
        if (!code.empty()) { w["source"] = ojson::object(); w["source"]["game"] = game; w["source"]["code"] = code; }
        write_file(d / "wrestler.json", py_dumps(w));
        if (tb) { write_file(d / "theme.bin", *tb); write_file(d / "theme.meta", *tm); }
    };
    std::string pt = "the player's own theme"; Bytes pb(pt.begin(), pt.end()), pm(0xD8, 0);
    man("Revenge/kevin_nash", "kevin_nash", &stdt.bin, &stdt.meta);
    man("Revenge/scott_steiner", "scott_steiner", &stdt.bin, &stdt.meta);
    man("Revenge/scott_steiner/scott_steiner_b", "scott_steiner_b", &stdt.bin, &stdt.meta);
    man("Revenge/sting", "sting", &pb, &pm);
    man("Revenge/booker_t", "booker_t", nullptr, nullptr);
    man("Revenge/jim_neidhart", "jim_neidhart", &stdt.bin, &stdt.meta);
    man("Revenge/chris_benoit", "chris_benoit", &stdt.bin, &stdt.meta, "0x009");
    man("Revenge/aki_man", "aki_man", &stdt.bin, &stdt.meta, "0x601");
    man("No Mercy/benoit_lookalike", "chris_benoit_nm", &stdt.bin, &stdt.meta, "0x009", "WWF No Mercy");
    auto snap = [&]() {
        std::map<std::string, Bytes> m;
        for (auto it = fs::recursive_directory_iterator(tmp); it != fs::recursive_directory_iterator(); ++it)
            if (it->is_regular_file()) m[fs::relative(it->path(), tmp).generic_string()] = read_file(it->path());
        return m;
    };
    ojson record = ojson::object();
    auto run = [&](const fs::path& rf) {
        Roms RR(rf);
        Bytes b; ojson full = read_file_if(C / "firstrun.json", b) ? ojson::parse(std::string(b.begin(), b.end())) : ojson::object();
        ojson sec = full.contains("redux_themes") ? full["redux_themes"] : ojson::object();
        auto save = [&]() { full["redux_themes"] = sec; PyDump h; h.indent = 1; write_file(C / "firstrun.json", py_dumps(full, h) + "\n"); };
        return redux_themes_apply(W, RR, sec, save);
    };
    bool ok = true;
    auto check = [&](const char* what, bool cond) { ok &= cond; printf("%s  %s\n", cond ? "PASS" : "FAIL", what); };
    auto a = snap(); run(E);
    check("no Redux patch: nothing written, no record", snap() == a);
    run(roms); auto b = snap();
    auto rf = [&](const char* name) { return redux_theme_files(*rx, *redux_song_of(*rx, name)); };
    ThemeFiles nash = rf("Kevin Nash"), steiner = rf("Scott Steiner"), neid = rf("Jim Neidhart"), benoit = redux_theme_files(*rx, 0x24);
    check("the patch: the importer's themes replaced by Redux's (Nash, Neidhart, Scott Steiner + his second look, Benoit by code)",
          b["wrestlers/Revenge/kevin_nash/theme.bin"] == nash.bin && b["wrestlers/Revenge/kevin_nash/theme.meta"] == nash.meta &&
          b["wrestlers/Revenge/scott_steiner/scott_steiner_b/theme.bin"] == steiner.bin && b["wrestlers/Revenge/scott_steiner/theme.bin"] == steiner.bin &&
          b["wrestlers/Revenge/jim_neidhart/theme.bin"] == neid.bin && b["wrestlers/Revenge/chris_benoit/theme.bin"] == benoit.bin);
    check("the player's theme kept (Sting), a taken-out theme not put back (Booker T), no Redux song / not Revenge untouched",
          b["wrestlers/Revenge/sting/theme.bin"] == pb && !fs::exists(W / "Revenge/booker_t/theme.bin") &&
          b["wrestlers/Revenge/aki_man/theme.bin"] == stdt.bin && b["wrestlers/No Mercy/benoit_lookalike/theme.bin"] == stdt.bin);
    {
        Bytes t = read_file(C / "firstrun.json"); auto j = ojson::parse(std::string(t.begin(), t.end()));
        check("recorded in the record (input, done, the 5 folders written)", j["redux_themes"]["state"] == "done" && j["redux_themes"]["wrote"].size() == 5);
    }
    auto r3 = run(roms);
    check("a second start with nothing new: no file written", snap() == b && !r3.ran);
    std::string pn = "the player's Nash"; write_file(W / "Revenge/kevin_nash/theme.bin", Bytes(pn.begin(), pn.end()));
    {
        Bytes t = read_file(C / "firstrun.json"); auto j = ojson::parse(std::string(t.begin(), t.end()));
        j["redux_themes"]["input"] = "an older patch"; PyDump h; h.indent = 1; write_file(C / "firstrun.json", py_dumps(j, h) + "\n");
    }
    run(roms); auto c = snap();
    check("a new input later: the player's changed theme kept (Nash), this step's own still its own",
          c["wrestlers/Revenge/kevin_nash/theme.bin"] == Bytes(pn.begin(), pn.end()) && c["wrestlers/Revenge/scott_steiner/theme.bin"] == steiner.bin);
    printf("SELFTEST %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
