#!/usr/bin/env python3
"""The QA runner's Python side (qa.ps1 runs it): a scenario -> the mod's qa_* options, and after the run, the
checks and the report.

  python qa_tool.py config <scenario.json> <game folder copy>   writes mod_config\\nomercy_arcade.json there
  python qa_tool.py report <scenario.json> <run folder>         checks state.csv, makes contact.png + report.txt
  python qa_tool.py mods <game folder copy> <run folder>         the mods the game should load -> mods_expected.json
  python qa_tool.py names                                        the Superstars and arenas a scenario can name

The result (report.txt's last line): PASS, FAIL, CRASH, TIMEOUT, INCOMPLETE (closed early: rerun), or STARVED (the game
made under FULL_PACE of its pictures before its time ran out, and only checks that may not have been reached failed: rerun
on a quieter PC). Only PASS is a pass.

A scenario (qa\\scenarios\\<name>.json):
  {
    "about":    "what it tests",
    "wrestlers": ["BRET HART", "THE ROCK"],        names (or IDs: 1546 / "0x060A"); null or "any": anyone
    "costumes":  [0, 0],
    "arena":     "RAW IS WAR",                      or "any"
    "arena2":    "MONDAY NITRO",                    the arena of the second match on (with "matches" 2 or more)
    "control":   ["cpu", "cpu"],                    "cpu", "pad1", "pad2" per slot
    "special":   [false, false],                    SPECIAL filled at the bell
    "matches":   1,                                 then the game quits (0: until "seconds")
    "seconds":   90,                                the longest the run may take
    "shots":     2.0,                               a screenshot every so many seconds
    "options":   { "arcade_speed": false },         any other mod options for the run
    "keep_options": ["controls"],                   options left as the game folder's settings hold them (not reset to
                                                    mod.toml's defaults; absent there: left out, the game's default)
    "pads":      ["12.0 1 a 300", ...],             scripted controller presses (exe's WCW_VPADS_SCRIPT lines)
    "match":     "singles",                         or "rumble" (the arcade's single-player Royal Rumble, wrestlers[0] the player),
                                                    or "tag" / "triple" / "handicap" / "guest_referee" / "tag_ladder" /
                                                    "handicap_ladder" (from the title's demo set of that format,
                                                    no menus: wrestlers, costumes, control up to four -- logic.c qa_demo_set),
    "rules":     "hardcore",                        the match's rules by gen/storyc.py RULES' names for its type (or a preset
                                                    number); left out, the demo's own (logic.c qa_rules)
                                                    or "arcade" (QA mode off: the arcade as it is, driven by "pads")
    "force":     { "slot": 0, "entries": [0, 69], "as": 73 },   that slot's moveset entries in the range done as "as"
                                                    (state.csv's "forced" counts them)
    "camera":    { "eye": [55, 170, -900], "at": [0, 60, -100] },   a camera of the scenario's own, in world units
                                                    ("from": 30 -- the game's cameras until match frame 30 x 60: the match clock runs 30 a second, so its first 60 s; logic.c qa_frame)
    "shot_when": "$c.eye_z -lt -667",               a screenshot (*_WHEN) whenever this PowerShell test of the newest
                                                    state row ($c.<column>) is true, every 2 s at most
    "expect": [                                     what must be true, in state.csv's columns
                                                    (s<k>_state is the wrestler's PREVIOUS state, his record's +0x1C,
                                                     and s<k>_phase his state, +0x14: labels kept as the scenarios use
                                                     them -- agent "names4", 2026-11-12; mod src/logic.c)
      { "name": "Bret is in slot 0", "when": "match", "check": "s0_id == 0x060D" },
      { "name": "the match ended",   "when": "end",   "check": "matches >= 1" },
      { "name": "nobody's position went bad", "when": "always", "check": "finite(s0_x, s0_y, s0_z, s1_x, s1_y, s1_z)" }
    ]
  }
  when: "any" (some row), "always" (every row), "match" (some row while the match is on), "end" (the last row).
"""
import os, sys, json, csv, math, re, glob
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp

ARENAS = ["RAW IS WAR", "NO MERCY", "SMACKDOWN", "KING OF THE RING", "SUMMERSLAM", "SURVIVOR SERIES",
          "ROYAL RUMBLE", "WRESTLEMANIA", "BACKLASH", "ARMAGEDDON",   # (the venue byte 0..9; logic_v1.c)
          "MONDAY NITRO", "HALLOWEEN HAVOC", "BASH AT THE BEACH", "RAW IS WAR WM 2000",
          "SOULED OUT", "SUPERBRAWL", "STARRCADE",
          "SUNDAY NIGHT HEAT",
          "ROYAL RUMBLE WM 2000",
          "KING OF THE RING WM 2000",
          "SURVIVOR SERIES WM 2000",
          "WRESTLEMANIA WM 2000",
          "SUMMERSLAM WM 2000"]   # (then the arcade's: mod/src/arena.c, gen/arenas.py's order)
MOD = nmp("/home/devbox/work/arcade")

def roster():
    """name -> Superstar ID: the game's (gen/roster_base.json) and the ported ones (src/roster_ext.h)"""
    out = {}
    for r in json.load(open(os.path.join(MOD, "gen", "roster_base.json"))):
        out[r["name"].upper()] = r["id"]; out.setdefault(r["short"].upper(), r["id"])
    ext = set()
    for m in re.finditer(r'\{\s*0x([0-9A-Fa-f]{4}),\s*\d+,\s*"([^"]+)",\s*"([^"]*)"', open(os.path.join(MOD, "src", "roster_ext.h")).read()):
        i = int(m.group(1), 16); k = m.group(2).upper()
        if k not in ext: out[k] = i; ext.add(k)     # (a second ID, gen/revbanks.py, has his base's name: the name stays the base's)
        out.setdefault(m.group(3).upper(), i)
    return out

def wrestler_id(v, names):
    if v in (None, "any", "ANY", 0): return 0
    if isinstance(v, int): return v
    if re.fullmatch(r"0x[0-9a-fA-F]+|\d+", v): return int(v, 0)
    k = v.upper()
    if k not in names: raise SystemExit("no Superstar called %r (python qa_tool.py names)" % v)
    return names[k]

def toml_defaults():
    """the mod's options as mod.toml declares them: {id: default} (its [[manifest.config_options]] blocks: key = value
    lines -- read here, the toolchain's Python has no TOML module)"""
    # (QA_MOD_DIR: the mod folder qa.ps1 -ModDir tests -- a copy beside the real one, 2026-11-07; else the project's mod\)
    if os.environ.get("QA_MOD_DIR"):
        toml = os.path.join(os.environ["QA_MOD_DIR"], "recomp", "mod.toml")
    else:
        root = next(d for d in (os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), *[".."] * i)) for i in range(4))
                    if os.path.exists(os.path.join(d, "mod", "recomp", "mod.toml")))
        toml = os.path.join(root, "mod", "recomp", "mod.toml")
    out, cur = {}, None
    for line in open(toml, encoding="utf8"):
        line = line.strip()
        if line.startswith("["): cur = {} if line == "[[manifest.config_options]]" else None; continue
        if cur is None or "=" not in line: continue
        k, v = (x.strip() for x in line.split("=", 1))
        if k == "id": cur["id"] = json.loads(v)
        elif k == "default":
            cur["default"] = json.loads(v) if v[:1] in '"-0123456789' or v in ("true", "false") else v
        if "id" in cur and "default" in cur: out[cur["id"]] = cur["default"]
    return out

def config(scn_path, game):
    s = json.load(open(scn_path))
    if s.get("mode") == "record":   # (qa.ps1 -Record: the game as it is, QA mode off; the state still written)
        path = os.path.join(game, "mod_config", "nomercy_arcade.json")
        cfg = json.load(open(path))
        cfg["storage"].update({k: 0 for k in ("qa_mode", "qa_fslot", "qa_bsuplex", "qa_rr_ports", "qa_rr_out", "qa_tron", "qa_p2", "qa_story", "qa_versus")})
        json.dump(cfg, open(path, "w"), indent=4)
        print("recording: the game as it is (your settings), QA mode off")
        return
    names = roster()
    w = [wrestler_id(x, names) for x in (s.get("wrestlers") or [None, None]) + [None, None, None, None]][:4]
    c = ((s.get("costumes") or [0, 0]) + [0, 0, 0, 0])[:4]
    a = s.get("arena", "any")
    arena = 0 if a in (None, "any", "ANY") else (a if isinstance(a, int) else ARENAS.index(a.upper()) + 1)
    a2 = s.get("arena2")   # (the second match on: another arena -- one after another in one game)
    arena2 = 0 if a2 in (None, "any", "ANY") else (a2 if isinstance(a2, int) else ARENAS.index(a2.upper()) + 1)
    ctl = {"cpu": 0, "pad1": 1, "pad2": 2}
    ctls = [ctl[x] for x in ((s.get("control") or ["cpu", "cpu"]) + ["cpu", "cpu", "cpu", "cpu"])[:4]]
    # (a QA match's format: the attract demo set it's made from -- logic.c qa_demo_set)
    fmt = s.get("match", "singles")
    # (the title's demo sets, named by what their runs showed 2026-11-06: 2 a guest referee -- set-up flag 0x10 --,
    #  0 a tag ladder match and 4 a handicap ladder match -- 0x40, ladders and a belt to pull down)
    demo_set = {"singles": 1, "tag": 6, "triple": 7, "handicap": 8, "guest_referee": 2, "tag_ladder": 0, "handicap_ladder": 4}.get(fmt, 1)
    if s.get("demo_set") is not None: demo_set = int(s["demo_set"])   # (a demo set by its number: finding out what one is)
    # (the rules by the story compiler's names for the type: the game's preset; a number is a preset as it is)
    rules = s.get("rules")
    if isinstance(rules, str):
        import importlib.util
        spec = importlib.util.spec_from_file_location("storyc", os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mod", "gen", "storyc.py"))
        storyc = importlib.util.module_from_spec(spec); spec.loader.exec_module(storyc)
        kind = {"singles": "single"}.get(fmt, fmt)
        rules = dict(storyc.RULES[kind])[rules]
    rules = int(rules or 0)
    sp = (s.get("special") or [False, False]) + [False, False]
    path = os.path.join(game, "mod_config", "nomercy_arcade.json")
    # (newhome 2026-11-17: a fresh game folder has no mod settings yet -- the game writes them when it first saves: start from
    #  an empty set, every option then at mod.toml's default below, as for any scenario)
    if not os.path.exists(path):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        json.dump({"mod_id": "nomercy_arcade", "storage": {}}, open(path, "w"), indent=4)
    cfg = json.load(open(path))
    st = cfg["storage"]
    # every option at mod.toml's default first: a scenario's options are its own, not left from the run before
    # (2026-10-29: qa_p2, qa_no_zbuf, controls ... carried over into later runs)
    defaults = toml_defaults()
    # (ctrlsave 2026-11-25: a scenario's "keep_options" -- options left as the game folder holds them, the game's own saved
    #  values, for a test of what a start before saved: an option the folder doesn't hold stays out, mod.toml's default
    #  then the game's -- qa\scenarios\ctl_mode_restart_*)
    keep = {k: st[k] for k in (s.get("keep_options") or []) if k in st}
    st.update({k: v for k, v in defaults.items() if k != "settings_version"})
    for k in (s.get("keep_options") or []):
        if k in keep: st[k] = keep[k]
        else: st.pop(k, None)
    # (a mod with the Regular / Arcade modes, game_mode: the scenarios, written for the arcade's flow, run in Arcade unless
    #  one says otherwise in its options -- mod.toml's default, Regular, would quietly change what all of them test.
    #  The modes work, 2026-11-07; a mod without the option is left as it was.)
    if "game_mode" in defaults:
        st["game_mode"] = "Arcade"
        # (and settings_version 5, the modes' own: the game folder's older one would run the migration, which sets
        #  game_mode from "enabled" -- a Regular scenario came back Arcade, 2026-11-07)
        # (ctrlsave 2026-11-25: 6, the mod's own now -- version 6 puts CONTROL MODE back to N64 once on a machine of 1..5,
        #  which would undo a scenario's "controls": "Arcade"; a scenario that sets settings_version itself still has it)
        st["settings_version"] = 6
    st.update({"qa_mode": {"singles": 1, "tag": 1, "triple": 1, "handicap": 1, "guest_referee": 1, "tag_ladder": 1, "handicap_ladder": 1, "rumble": 2, "arcade": 0}[fmt], "qa_demo_set": demo_set, "qa_rules": rules, "qa_setup_flags": int(s.get("setup_flags", 0)),
               "qa_w1": w[0], "qa_w2": w[1], "qa_w3": w[2], "qa_w4": w[3], "qa_c1": int(c[0]), "qa_c2": int(c[1]), "qa_c3": int(c[2]), "qa_c4": int(c[3]), "qa_arena": arena, "qa_arena2": arena2,
               "qa_ctl1": ctls[0], "qa_ctl2": ctls[1], "qa_ctl3": ctls[2], "qa_ctl4": ctls[3], "qa_special": (1 if sp[0] else 0) | (2 if sp[1] else 0),
               "qa_matches": int(s.get("matches", 1))})
    cam = s.get("camera")   # {"eye": [x, y, z], "at": [x, y, z]}: the scenario's own camera (each option + 3000)
    st["qa_cam"] = (1 + int(cam.get("from", 0))) if cam else 0   # ("from": seconds into the match it starts)
    for k, v in zip(("ex", "ey", "ez", "ax", "ay", "az"), (cam["eye"] + cam["at"]) if cam else [0] * 6):
        st["qa_cam_" + k] = int(round(v)) + 3000
    f = s.get("force")   # {"slot": 0, "entries": [from, to], "as": entry}: that slot's moves in the range done as "as"
    st.update({"qa_fslot": f["slot"] + 1, "qa_ffrom": f["entries"][0], "qa_fto": f["entries"][1], "qa_fas": f["as"]} if f
              else {"qa_fslot": 0, "qa_ffrom": 0, "qa_fto": 0, "qa_fas": 0})
    st.update(s.get("options", {}))
    json.dump(cfg, open(path, "w"), indent=4)
    pads = s.get("pads") or []
    if pads: open(os.path.join(game, "qa_pads.txt"), "w").write("\n".join(pads) + "\n")
    print("superstars %s, arena %s, control %s" % (["0x%04X" % x for x in w], (ARENAS[arena - 1] if arena <= len(ARENAS) else "number %d" % arena) if arena else "any", ctls))

def finite(*v): return all(math.isfinite(x) and abs(x) < 1e6 for x in v)

def rows_of(run):
    out = []
    for r in csv.DictReader(open(os.path.join(run, "state.csv"))):
        d = {}
        for k, v in r.items():
            try: d[k] = int(v, 0) if re.fullmatch(r"-?(0x[0-9A-Fa-f]+|\d+)", v) else float(v)
            except (ValueError, TypeError): d[k] = v
        out.append(d)
    return out

def quit_on_time(run):
    """the game quit by itself when its WCW_QUIT_AFTER time was up (its log says so)"""
    try:
        return "WCW_QUIT_AFTER=" in open(os.path.join(run, "game_stderr.txt"), encoding="utf-8", errors="replace").read()
    except OSError:
        return False
def in_match(r): return r.get("stage") == 2 and r.get("mode") in (-6, -2) and r.get("match_frames", 0xFFFF) not in (0xFFFF,)

# The game's pace (2026-11-12). A scenario's "seconds" are the game's clock, and that clock runs at the speed asked for
# whether or not the game keeps up: on a busy PC the game skips the frames it has no time for (0.77's tests: ~30 game
# updates a second for 58), so the clock -- and WCW_QUIT_AFTER -- ran out with a fifth to a half of the game played, and
# checks of the "it happened" kind failed only because it had not happened yet. The pictures the game finished (state.csv
# "swaps": its logic frames, one a picture) say how much was played: 30 for each second of the game's clock at full pace
# (story_upgrade_b on a quiet PC: 18,181 in its 600 s; the nightly set's quiet runs 86% to 106% of seconds x 30, the
# shortest the most). Not "frame", the mod's count of video interrupts (60 a second): in every run of 11-09 to 11-12 it
# went with "swaps" (twice it, starved or not), but a game held to one CPU beside two busy loops took all of its interrupts
# and finished only 37% of its pictures -- one match of blocks_hogan's four (2026-11-12_124633). So a run's pace = the
# pictures it made / the pictures its seconds give, and a run below FULL_PACE that ran out of time is starved, not failed.
PICTURES_A_SECOND = 30
FULL_PACE = 0.8   # (quiet runs of the nightly set: 0.86 to 1.06; the starved runs of 11-10 / 11-11: 0.16 to 0.54)

def pictures_made(rows):
    """the game's pictures from its first one (state.csv "swaps" > 0) to the last row"""
    first = next((r for r in rows if isinstance(r.get("swaps"), int) and r["swaps"] > 0), None)
    if first is None or not isinstance(rows[-1].get("swaps"), int): return 0
    return max(0, rows[-1]["swaps"] - first["swaps"])

def ran_out_of_time(run, info):
    """the run ended on the scenario's time: the game's clock (WCW_QUIT_AFTER, in its log), or qa.ps1 closed it when it
    had made the scenario's frames (run.json "frame_limit", 2026-11-12)"""
    return bool(info.get("frame_limit")) or quit_on_time(run)

# The mods (2026-11-12). Twice in 1,118 runs a mod in the game's mods folder was never loaded (a start-up race in the
# loader: mods.json written while it was being read, <outside>\NOTES.md "The three-mods run") and the
# tests passed: nothing looked. Each mod the game should load must log "Loading mod <id>" (librecomp mods.cpp). Which
# ones it should: qa.ps1 writes them before the game starts (mods_expected.json, "python qa_tool.py mods"); for a run
# from before that, the mods the log says it opened, less those it could not (a texture pack is a folder without a
# mod.json: "Mod is missing a mod.json").
def mod_ids_in(folder, mods_json=None):
    """the code mods in a game's mods folder, by their manifests' ids, and those its mods.json switched off (in
    mod_order, not in enabled_mods: the user's choice, librecomp load_mods_config)"""
    import zipfile
    ids = []
    for p in sorted(glob.glob(os.path.join(folder, "*"))):
        try:
            if os.path.isdir(p):
                m = os.path.join(p, "mod.json")
                man = json.load(open(m, encoding="utf-8")) if os.path.exists(m) else None
            elif p.lower().endswith(".nrm"):
                with zipfile.ZipFile(p) as z: man = json.loads(z.read("mod.json").decode("utf-8"))
            else: man = None
        except (OSError, KeyError, ValueError, zipfile.BadZipFile):
            man = None
        if man and man.get("id") and not man.get("custom_gamemode"): ids.append(man["id"])
    off = []
    if mods_json and os.path.exists(mods_json):
        try:
            mj = json.load(open(mods_json, encoding="utf-8"))
            off = [m for m in mj.get("mod_order", []) if m not in mj.get("enabled_mods", [])]
        except (OSError, ValueError):
            pass
    return ids, off

def write_expected_mods(game, run):
    """qa.ps1, before the game starts: the mods it should load (run\\mods_expected.json)"""
    ids, off = mod_ids_in(os.path.join(game, "mods"), os.path.join(game, "mods.json"))
    json.dump({"mods": [i for i in ids if i not in off], "switched_off": off}, open(os.path.join(run, "mods_expected.json"), "w"), indent=1)
    print("mods it should load: %s%s" % (", ".join(i for i in ids if i not in off), "; switched off in mods.json: " + ", ".join(off) if off else ""))

def mods_check(run, log_lines, info):
    """(lines for the report, the failure or None)"""
    exp_path = os.path.join(run, "mods_expected.json")
    if os.path.exists(exp_path):
        exp = json.load(open(exp_path))
        want = exp["mods"]; whence = "the copy's mods folder"
        # (2026-11-13, 0.82: the exe switches bundled mods that mods.json lists as off back on before the loader reads it --
        #  main.cpp repair_bundled_mods_config, one line naming them; " -- but mods.json could not be written" means they
        #  stayed off. Those it switched back on are expected too.)
        rep = [l.strip() for l in log_lines if l.startswith("[wcw] mods.json:") and "switched back on" in l and " -- but " not in l]
        if rep:
            back = [x.strip() for x in rep[-1].rsplit(": ", 1)[1].split(",")]
            add = [m for m in exp.get("switched_off", []) if m in back and m not in want]
            if add:
                want = want + add
                whence += "; switched back on by the exe: " + ", ".join(add)
    else:
        opened = [m.group(1) for l in log_lines for m in [re.match(r"Opening mod (\S+)", l)] if m]
        bad = {os.path.splitext(os.path.basename(m.group(1)))[0] for l in log_lines for m in [re.match(r"Error opening mod (.+?): ", l)] if m}
        want = [o for o in opened if o not in bad]; whence = "the mods its log opened"
    if not want: return [], None   # (a run from before mods, or no log)
    loaded = {m.group(1) for l in log_lines for m in [re.search(r"Loading mod (\S+)", l)] if m}
    out, missing = [], []
    for w in want:
        if w in loaded: continue
        # (a line of its garbled by another thread's print -- 2026-11-07_221726_versus_1p: " mki_cheats", the mod loaded
        #  all the same: its code was made): the line's end is the mod's id but its first letter
        # (no longer than a "Loading mod" line and not another print's own "[tag] ..." line: race's test lines of 2026-11-13,
        #  "[race-test] ... scan holds 1000 ms at aki_camera", let three mods that never loaded through as garbled ones)
        g = [l.strip() for l in log_lines if len(w) > 3 and l.rstrip().endswith(w[1:]) and not re.match(r"(Opening|Error opening) mod", l)
             and len(l.strip()) <= len("Loading mod ") + len(w) and not l.strip().startswith("[")]
        if g: out.append("  (%s: its \"Loading mod\" line garbled in the log: %r)" % (w, g[0])); continue
        missing.append(w)
    head = "mods: %d of %d loaded (%s)" % (len(want) - len(missing), len(want), whence)
    if missing:
        # (which loader, 2026-11-13, race: the start-up save of mods.json landed while the scan was opening the mods, and that
        #  launch read it back as those mods switched off -- every exe from before the 0.80 source has it; 0.80's librecomp
        #  mods.cpp makes the save wait for load_mods_config. Stage 1's "[aot] linked:" line came in with that same source, so a
        #  log without it is an exe from before: rebuild it. With it, the fix was there and something else did this.)
        if any(l.startswith("[aot] linked:") for l in log_lines):
            whose = "an exe with 0.80's mods.json fix: not the known start-up race -- look at the log"
        else:
            whose = "an exe from before the 0.80 source, whose loader saves mods.json before reading it: the known start-up race, fixed since -- rebuild the exe"
        why = "mods not loaded: %s (the game ran without them; %s)" % (", ".join(missing), whose)
        return [head, "FAIL  " + why] + out, why
    return [head] + out, None

def aot_check(log_lines, info, n_mods):
    """Stage 1's linked translation (librecomp mods.cpp "[aot] linked: N mods; live: M mods, F functions ..."): every mod
    the game loaded must be linked and nothing translated live -- unless the run asked for live (NM_AOT=0, recorded by
    qa.ps1 in run.json; else the environment, as Stage 1's own diff read it) or the exe is one from before (no such
    line). After <outside>\\qa_qa_tool.py.diff, which this takes in. (lines, failure)"""
    said = [l.strip() for l in log_lines if l.startswith("[aot] linked:")]
    per_mod = [l for l in log_lines if re.match(r"\[aot\] (?!linked:|hooked )\S+: ", l)]
    # (2026-11-14, merge083: a "linked:" line garbled by another thread's print -- "[aot] linked: 4 mods; live: 0 mods, 0
    #  fAutuo-asnctsignions", modnative7's music_rumble_late -- or lost from the start of its line is no reason to call the
    #  run translated live: then the mods' own lines decide, below. Only a run with neither is one from an exe before Stage 1.)
    if not said and not per_mod: return [], None
    m = re.match(r"\[aot\] linked: (\d+) mods; live: (\d+) mods, (\d+) functions", said[-1]) if said else None
    nm_aot = str(info.get("nm_aot", os.environ.get("NM_AOT", "")))
    # (a run from before qa.ps1 recorded it: the loader's own lines say it, "(NM_AOT=0; the mod's hash ...)" for every mod)
    if per_mod and all("(NM_AOT=0;" in l for l in per_mod): nm_aot = "0"
    said_text = said[-1][len("[aot] "):] if said else "(the loader's linked line not in the log)"
    if nm_aot == "0":
        return ["translation (NM_AOT=0, by request): " + said_text], None
    lines = ["translation: " + said_text]
    # (the hooked functions' summary says "0 hook(s) left to the live recompiler" when all are linked: not one translated live)
    live_said = [l.strip() for l in log_lines if ("the live recompiler translates it" in l or "left to the live recompiler" in l)
                 and not re.search(r"\b0 hook\(s\) left to the live recompiler", l)]
    if not m and not live_said:
        # (the linked line unreadable: every mod loaded must say its linked translation is in use, and none was translated
        #  live -- the same two things the line counts. A mod translated live never says "in use", so one short fails.)
        in_use = [l for l in per_mod if "the linked translation is in use" in l]
        if in_use and (not n_mods or len(in_use) >= n_mods):
            return lines + ["  (the linked line garbled by another print or not there: %d mods say their linked translation is in use, "
                            "none translated live)" % len(in_use)], None
    if not m or m.group(2) != "0" or m.group(3) != "0":
        why = "the live recompiler translated mod code: the exe's linked translation is not this mod's (build_exe.ps1 after build_mod.ps1)"
        return lines + ["FAIL  " + why] + ["  " + l.strip() for l in log_lines if "the live recompiler translates it" in l or "left to the live recompiler" in l][:12], why
    # (fewer linked than loaded, not "not the same", 2026-11-12, final080: n_mods counts the "Loading mod" lines, and another
    #  thread's print can garble one -- mods_check above lets such a mod through -- so it can be one short of the linked count
    #  with every mod loaded and linked. A mod loaded and not linked is one translated live, which the line above fails already.)
    if n_mods and int(m.group(1)) < n_mods:
        why = "%s mods linked, %d loaded" % (m.group(1), n_mods)
        return lines + ["FAIL  " + why], why
    return lines, None

def report(scn_path, run):
    s = json.load(open(scn_path))
    lines = ["QA run: %s" % os.path.basename(run), "scenario: %s -- %s" % (os.path.basename(scn_path), s.get("about", "")), ""]
    ok_all = True
    rows = rows_of(run) if os.path.exists(os.path.join(run, "state.csv")) else []
    lines.append("state rows: %d%s" % (len(rows), "" if rows else "  (the game wrote no QA state: did the mod start in QA mode?)"))
    if rows:
        m = [r for r in rows if in_match(r)]
        lines.append("in the match: %d rows, match frames up to %d" % (len(m), max([r["match_frames"] for r in m] or [0])))
        if "bad_mtx" in rows[0]:
            # the scene's matrices put right (gameplay.c scene_world): when they started, and whose
            OWN = {0: "other", 1: "JR", 2: "King", 3: "JR's chair", 4: "King's chair", 0x20: "the table"}
            jumps = [(a, b) for a, b in zip(rows, rows[1:]) if b["bad_mtx"] > a["bad_mtx"]]
            if rows[-1]["bad_mtx"]:
                who = sorted({OWN.get(b["bad_owner"], "slot %d" % (b["bad_owner"] - 0x10)) for _, b in jumps})
                first = jumps[0][1] if jumps else rows[0]
                lines.append("BAD MATRICES: %d, the first at %.1f s (match frame %s, clock %s), whose: %s" % (
                    rows[-1]["bad_mtx"], first["ms"] / 1000.0, first["match_frames"], first["clock"], ", ".join(who)))
                if "king_x" in first:
                    # the moment it started, and a little before: the announcers, the bad node and the parent it named
                    i = rows.index(first)
                    lines.append("  around it (ms, bad, owner, node, its parent, JR bones/root, King bones/root, slots' Superstars):")
                    for r in rows[max(0, i - 3): i + 4]:
                        lines.append("    %7d %5d 0x%02X %s %s  JR %s (%.0f,%.0f,%.0f)  King %s (%.0f,%.0f,%.0f)  %s" % (
                            r["ms"], r["bad_mtx"], r["bad_owner"], hex(r["bad_node"]), hex(r["bad_parent"]),
                            hex(r["jr_bones"]), r["jr_x"], r["jr_y"], r["jr_z"], hex(r["king_bones"]), r["king_x"], r["king_y"], r["king_z"],
                            " ".join("%04X" % r["s%d_id" % k] for k in range(6))))
            else:
                lines.append("bad matrices: none")
        if "king_head_x" in rows[0]:
            # King's body away from where he stands (his head more than 150 from his root, along the floor)
            away = [r for r in rows if r["king_bones"] and math.hypot(r["king_head_x"] - r["king_x"], r["king_head_z"] - r["king_z"]) > 150]
            if away:
                a = away[0]
                lines.append("KING'S BODY DRAWN AWAY FROM HIS PLACE: %d rows, the first at %.1f s: root (%.0f, %.0f, %.0f), head (%.0f, %.0f, %.0f), hips (%.0f, %.0f, %.0f)" % (
                    len(away), a["ms"] / 1000.0, a["king_x"], a["king_y"], a["king_z"], a["king_head_x"], a["king_head_y"], a["king_head_z"],
                    a["king_hips_x"], a["king_hips_y"], a["king_hips_z"]))
            else:
                lines.append("King's body: always at his place")
        if "king_sum0" in rows[0]:
            # King's head pictures (announce.c): each time their contents changed while he was built (written over)
            ch = [(a, b) for a, b in zip(rows, rows[1:]) if b["king_pic0"] and a["king_pic0"] == b["king_pic0"] and
                  (a["king_sum0"], a["king_sum1"]) != (b["king_sum0"], b["king_sum1"])]
            if ch:
                lines.append("KING'S HEAD PICTURES WRITTEN OVER: %d times (pictures at %s / %s)" % (len(ch), hex(ch[0][1]["king_pic0"]), hex(ch[0][1]["king_pic1"])))
                for a, b in ch[:12]:
                    lines.append("    %7.1f s  clock %4s  mode %s  slots %s" % (b["ms"] / 1000.0, b["clock"], b["mode"],
                                 " ".join("%04X" % b["s%d_id" % k] for k in range(6))))
            else:
                lines.append("King's head pictures: never written over")
    failed_seen, failed_unreached = [], []
    for e in s.get("expect", []):
        when = e.get("when", "any")
        sel = rows if when in ("any", "always") else [r for r in rows if in_match(r)] if when == "match" else rows[-1:]
        env = {"finite": finite, "math": math}
        res = []
        for r in sel:
            try: res.append(bool(eval(e["check"], env, dict(r))))
            except Exception as x: res.append(False); err = str(x)
        ok = bool(sel) and (all(res) if when in ("always", "end") else any(res))
        ok_all &= ok
        # (which kind failed: an "always" check that failed saw a wrong row -- a failure at any pace; the others may only
        #  not have been reached in the time the game had, 2026-11-12)
        if not ok: (failed_seen if when == "always" and sel else failed_unreached).append(e["name"])
        lines.append("%s  %s   [%s: %s]" % ("PASS" if ok else "FAIL", e["name"], when, e["check"]))
    from PIL import Image
    for b in glob.glob(os.path.join(run, "shots", "*.bmp")):        # (qa.ps1 grabs .bmp)
        Image.open(b).convert("RGB").save(b[:-4] + ".png", compress_level=1); os.remove(b)   # (level 1: half the time of the default 6 a shot, qa12x 2026-11-17)
    shots = sorted(glob.glob(os.path.join(run, "shots", "*.png")))
    lines += ["", "screenshots: %d (contact.png)" % len(shots)]
    if shots: contact(shots, os.path.join(run, "contact.png"))
    log = os.path.join(run, "game_stderr.txt")
    crashed = bool(glob.glob(os.path.join(run, "crash_reports", "crash_*.txt")) + glob.glob(os.path.join(run, "crash_reports", "freeze_*.txt")))
    if os.path.exists(log):
        bad = [l.strip() for l in open(log, errors="replace") if re.search(r"(?i)\[arcade\] (crash|freeze)|unhandled exception|fatal|lost the device|E_OUTOFMEMORY", l)]
        if bad: crashed = True; lines += ["", "log problems:"] + ["  " + b for b in bad[:20]]
    # how the run ended: the scenario finished (the game quit by itself), it ran out of time, or it was closed early
    info = json.load(open(os.path.join(run, "run.json"))) if os.path.exists(os.path.join(run, "run.json")) else {}
    # the mods the game should have loaded, and (Stage 1) that their translation was the linked one (2026-11-12)
    log_lines = open(log, encoding="utf-8", errors="replace").read().splitlines() if os.path.exists(log) else []
    mod_lines, mods_bad = mods_check(run, log_lines, info)
    n_loaded = len({m.group(1) for l in log_lines for m in [re.search(r"Loading mod (\S+)", l)] if m})
    aot_lines, aot_bad = aot_check(log_lines, info, n_loaded)
    if mod_lines or aot_lines: lines += [""] + mod_lines + aot_lines
    if mods_bad or aot_bad: ok_all = False; failed_seen.append(mods_bad or aot_bad)
    # the game's pace: the pictures it made against those the scenario's seconds give at full pace (2026-11-12)
    secs = int(s.get("seconds", 90))
    made, budget = (pictures_made(rows), secs * PICTURES_A_SECOND) if rows else (0, 0)
    timed_out = ran_out_of_time(run, info)
    pace = made / budget if budget else 0.0
    if rows:
        lines += ["", "the game's pictures: %d of the %d its %d s give at full pace (%d%%)%s" % (made, budget, secs, round(100 * pace),
                  ("; the run ended on its time" + (" (qa.ps1: the pictures made)" if info.get("frame_limit") else " (the game's clock)")) if timed_out else "")]
    want = int(s.get("matches", 1))
    finished = bool(rows) and want > 0 and rows[-1].get("matches", 0) >= want
    if crashed: verdict = "CRASH (see crash_reports\\ and the log)"
    elif info.get("killed"): verdict = "TIMEOUT (still running after %d s: stopped)" % info.get("seconds", 0)
    # (the game ending itself on WCW_QUIT_AFTER is the scenario's time run out, not a game closed early: with
    #  fast-forward that time is the game's, and it can be up before the run's real limit -- 2026-11-06)
    elif want > 0 and not finished and info.get("seconds", 0) < info.get("limit", 0) and not timed_out:
        verdict = "INCOMPLETE (the game was closed after %d s, before the scenario finished: rerun it)" % info.get("seconds", 0)
    # (starved: the time ran out with the game well short of its pictures, and every check that failed is one that may only
    #  not have been reached -- none saw a wrong row. Not a pass: the set counts it as not passing, and a rerun on a quieter
    #  PC decides. 2026-11-12)
    elif not ok_all and timed_out and pace < FULL_PACE and not failed_seen:
        verdict = "STARVED, rerun (the game made %d%% of its pictures -- the PC too busy, or the game itself slower: a rerun on a quiet PC tells which -- and %s not reached in that: %s)" % (
            round(100 * pace), "this was" if len(failed_unreached) == 1 else "these were", "; ".join(failed_unreached))
    else:
        verdict = "PASS" if ok_all else "FAIL"
        if want > 0 and not finished and info.get("frame_limit"):   # (qa.ps1 of 2026-11-12: its time is the game's frames)
            verdict += " (its %d s of the game's pictures ran out before the match ended: the checks are for what it played)" % secs
        elif want > 0 and not finished: verdict += " (the run's %d s ran out before the match ended: the checks are for what it played)" % info.get("limit", 0)
        if timed_out and pace < FULL_PACE: verdict += " (the game made only %d%% of its pictures: the PC busy, or the game slower)" % round(100 * pace)
    ok_all = verdict == "PASS"
    lines += ["", "RESULT: %s" % verdict]
    # (2026-11-17, pipefix: a character the code page has no place for -- pt2_select_random's about had U+0082 -- crashed the
    #  report, writing it and printing it (qa084): written as "?" instead, the rest as ever)
    open(os.path.join(run, "report.txt"), "w", errors="replace").write("\n".join(lines) + "\n")
    enc = sys.stdout.encoding or "utf-8"
    print("\n".join(lines).encode(enc, "replace").decode(enc))
    return ok_all

def contact(shots, out, cols=4, w=480):
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert("RGB") for p in shots]
    h = int(w * ims[0].height / ims[0].width)
    rows = (len(ims) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w, rows * (h + 18)), (16, 16, 16))
    d = ImageDraw.Draw(sheet)
    for i, (p, im) in enumerate(zip(shots, ims)):
        x, y = (i % cols) * w, (i // cols) * (h + 18)
        sheet.paste(im.resize((w, h)), (x, y + 18))
        d.text((x + 4, y + 3), os.path.splitext(os.path.basename(p))[0], fill=(230, 230, 230))
    sheet.save(out, compress_level=1)

if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["config"] and len(a) == 3: config(a[1], a[2])
    elif a[:1] == ["report"] and len(a) == 3: sys.exit(0 if report(a[1], a[2]) else 1)
    elif a[:1] == ["mods"] and len(a) == 3: write_expected_mods(a[1], a[2])   # (qa.ps1: <game folder copy> <run folder>)
    elif a[:1] == ["names"]:
        for n, i in sorted(roster().items(), key=lambda x: x[1]): print("0x%04X  %s" % (i, n))
        print("arenas:", ", ".join(ARENAS))
    else: print(__doc__)
