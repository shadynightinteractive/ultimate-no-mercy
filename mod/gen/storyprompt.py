#!/usr/bin/env python3
# storyprompt.py: mod\stories\AI_STORY_PROMPT.md, the prompt the user gives an AI to write a story, made from the
# compiler's own tables (gen/storyc.py), the roster, the groups, the voices and the samples -- so every limit it states
# is the one the compilers enforce. Run again when any of them changes:
#   python mod/gen/storyprompt.py mod/stories/AI_STORY_PROMPT.md
#
# STATUS below says what QA has shown (STORY_UPGRADE_REPORT.md). A line starting "@name " is kept only if STATUS[name] is
# on; "exp" keeps it, marked EXPERIMENTAL. Nothing goes in as working until its scenario passed in the real game.
import os, sys, json, runpy, re
HERE = os.path.dirname(os.path.abspath(__file__))
S = runpy.run_path(os.path.join(HERE, "storyc.py"), run_name="storyc_lib")
STORIES = os.path.join(HERE, "..", "stories")

STATUS = {
    "tron": True, "commentary": True, "taunts": True, "recast": True, "variants": True, "store": True,
    "t_time": True, "t_health": True, "t_outside": True, "t_hold": True, "t_finisher": True,
    "a_end": True, "a_runin": True, "a_nodq": True, "a_nocount": True,
    "cage": True, "ladder": True, "ironman": True, "tag": True, "triple": True, "handicap": True, "fourway": True,
    "runins": True, "title": True, "location": True,
}
EXP_WHY = {}     # name -> its limits, when STATUS[name] == "exp"

BAR = "=" * 100
TRON_ARENAS = "RAW IS WAR, NO MERCY, SMACKDOWN, KING OF THE RING, WRESTLEMANIA, BACKLASH, BASH AT THE BEACH, RAW IS WAR WM 2000, SUNDAY NIGHT HEAT"
def head(t): return "%s\n%s\n%s\n" % (BAR, t, BAR)

def roster_block():
    v = json.load(open(os.path.join(STORIES, "_voices.json"), encoding="utf-8"))
    out = []
    for name, d in v.items():
        if name == "default": continue
        c = " / ".join(x for x in d.get("catchphrases", []))
        out.append("%s | nickname: %s | finisher: %s | insult: %s | catchphrases: %s | signoff: %s" % (
            name, d.get("nickname", ""), d.get("finisher", ""), d.get("insult", ""), c, (d.get("signoff", "") or "").replace("\n", " ")))
    return "\n".join(out), v.get("default", {})

def groups_block():
    g = json.load(open(S["groups_file"](), encoding="utf-8"))
    return "\n".join("  %-16s %s" % (k, ", ".join(m)) for k, m in g.items())   # (startup 2026-11-19: string IDs, as written)

def sample(name):
    return open(os.path.join(STORIES, name), encoding="utf-8").read().strip()

def build():
    xb, xn = S["xarenas"]()
    arenas = [a.upper() for a in S["ARENAS"] + xn]
    rules = "\n".join("  %-9s %s" % (t + ":", ", ".join(k for k, e in S["RULES"][t]))
                      for t in ("single", "tag", "triple", "handicap", "fourway"))
    places = {"hall": "hall (or hallway)", "locker room": "locker room (or dressing room)", "boiler room": "boiler room",
              "pool hall": "pool hall (or sports lounge, bar)", "parking lot": "parking lot (or garage)",
              "stage": "stage (or entrance, ramp)", "ring": "ring"}
    pl = []
    for k, txt in places.items():
        n = S["PLACES"][k]
        doors = len(S["DOORS"].get(n, []))
        pl.append("  %-34s %2d spots%s" % (txt, len(S["SPOTS"][n]), ", %d doors" % doors if doors else ""))
    runin_at = ", ".join("%d:%02d" % (k // 60, k % 60) for k in sorted(S["RUNIN_AT"]))
    anims = ", ".join(k for k in S["ANIMS"] if not k.startswith("taunt") and k != "win taunt")
    voice_tbl, dflt = roster_block()
    LM, NL = S["LINE_MAX"], S["NAME_LEN"]
    LN, VM = S["LINES"], S["VOICE_MAX"]
    how = "pinfall, submission, dq, countout, tko, ladder, escape, draw, decision"
    txt = f"""You are writing a custom story for "Ultimate No Mercy", a mod of the Nintendo 64 wrestling game WWF No Mercy. A story is
one JSON file. The game's own Story Mode plays it: narration pages, 3D backstage and in-ring scenes with speech bubbles
and choices, a match, then branches that pick what comes next from how the match ended.

Write the story described under MY STORY at the end.

{head("RULES FIRST")}
A file that breaks ANY rule in this prompt is REJECTED: it does not play. It shows on the game's list as "ERROR" with the
reason, and the player gets nothing. There is no partial credit. Before you answer, re-check your file against the
CHECKLIST near the end, line by line, counting what it asks you to count.

{head("OUTPUT RULES")}
- Output ONE JSON object and nothing else: no comments, no trailing commas, no markdown fences, no explanation.
- Use only the keys and values listed here. Anything not listed does not exist.
- Text is plain ASCII. \\n inside a string starts a new line.
- TEXT LIMITS (counted exactly):
    a line               {LM} characters at most. A {{name}} counts as {NL}. A voice token counts as its longest:
                         nickname {VM[1]}, catchphrase {VM[2]}, finisher {VM[4]}, insult {VM[5]}; a signoff counts as TWO lines of {VM[3]}.
    a speech bubble      {LN["say"]} lines
    a narration page     {LN["page"]} lines
    a requirement        {LN["requirement"]} lines
    a question + options {LN["ask"]} lines together; an option is one line of {S["OPTION_MAX"]} characters at most
    "menu"               24 characters
- Superstars are named by their string ID, exactly as in ROSTER ("the_rock"; never the name on screen). Role names are matched exactly.
- Every "next" is the name of a node, or "end", or "gameover".

{head("TOP LEVEL")}
{{
  "title":        "The story's name (shown in logs).",
  "menu":         "Name on the Custom Story list.",
  "championship": "world",   // the Story Mode slot it runs as: world, tag, ic, european, hardcore, lightheavy, womens.
                             // Use "world" unless you want the game's tag partner (role "partner"): "tag".
  "cast": {{ "rival": "the_rock", "taker": ["undertaker", "kane", "big_show"] }},
                             // your own roles: at most 4, or 3 if ANY match in the story has a "title".
                             // A list recasts: the first who isn't the player or another role's man plays the role, in
                             // every match, scene and line. Give a list (up to 8 IDs) to any role the player might pick.
  "vars":  {{ "heat": 0 }},    // story variables with starting whole numbers: at most {S["VARS_MAX"]}.
  "start": "first_node",     // optional; default: the first node in the file.
  "nodes": {{ "first_node": {{ ... }}, "second_node": {{ ... }} }}
}}

Roles that always exist (never list them in "cast"):
  player     the Superstar the player picked
  partner    the player's tag partner (only with "championship": "tag")
  champion   the champion of the story's championship (whoever wins a "title" match becomes it)
  champion2  the second tag / women's champion

{head("A NODE (one match, with what comes before it and after it)")}
"node_name": {{
  "chapter":     "CHAPTER TITLE",   // nodes in a row with the same chapter are one chapter: at most 11 chapters
  "pages":       ["Page 1", "Page 2"],   // optional narration on black, before the scene
  "scene":       {{ ... }},           // optional 3D scene before the match (see SCENES)
  "match":       {{ ... }},           // REQUIRED (see MATCH)
  "requirement": "Text over the match card.",
  "retries":     0,                 // 0 to 15: retries of a lost match when a branch says "gameover"
  "branches":    [ {{ ... }}, {{ ... }} ]   // see BRANCHES
}}
Names in text: {{player}}, {{rival}} (any role) or {{kane}} (any roster string ID) print that Superstar's name.
A "requirement" containing any {{name}} needs "pages" or a "scene" in the same node, and every man it names must be
named or on screen there.

{head("MATCH")}
"match": {{
  "type":     "single",                  // see MATCH TYPES
  "slots":    ["player", ["rival", "kane"]],   // who is in it: exactly the number the type takes
  "teams":    [["player", "pal"], ["rival", "kane"]],  // INSTEAD of "slots" and "type": see TEAMS
  "rules":    "no dq",                   // optional, see RULES
  "arena":    "WRESTLEMANIA",            // optional, see ARENAS; default RAW IS WAR
@title   "title":    "world",                   // optional: this championship on the line (world, tag, ic, european, hardcore, lightheavy, womens)
@runins   "runins":   [{{ "who": "kane", "helps": "opponents", "at": "0:45" }}],   // optional, see RUN-INS
@location   "location": "parking lot",             // optional: a backstage brawl: pool hall, parking lot, boiler room
  "script":   [ ... ]                    // optional: mid-match triggers, see SCRIPT
}}

A slot is a role, a roster string ID, or a list of up to 5 of them (the first who can be in the match takes it). No Superstar
can fill two slots: the game moves down the list. The player goes in slot 1 unless the type says otherwise.

MATCH TYPES (slots):
  single    2
@tag   tag       4   (slots 1-2 a team, 3-4 a team)
@triple   triple    3
@handicap   handicap  3   (the first two a team against the third: put "player" third)
@fourway   fourway   4
@cage   cage      2
@ladder   ladder    2
@ironman   ironman   2

TEAMS: 1 on 1, 2 on 2, 1 on 2, 2 on 1, 1 on 1 on 1, or four men alone. Nothing bigger: the game's teams are two men at most.
Don't write 3-on-3, 2-on-3, 1-on-3, a gauntlet or a six-man: the file is rejected.

RULES (by type; the first is the default; cage, ladder and ironman have only "normal"):
{rules}

ARENAS (any capitals):
  {", ".join(arenas)}

@runins RUN-INS: a man runs in during the match. "helps": "opponents" (the default) or "player". At most 2 in a single match (one
@runins each side), 1 in a handicap match (for the opponents), none in other types. "at" is when he comes, and must be exactly
@runins one of: {runin_at}. Without "at": 0:30 for the opponents' man, 0:35 for the player's.

SCRIPT: a list of up to 16 triggers, each fires once while the match plays: {{ "when": <condition>, "do": <action>, ... }}
Conditions (<role> is player or a role who is in this match):
@t_time   "time 1:30"                the match clock has reached it
@t_health   "health rival < 30%"       his health is under that share (1 to 99)
@t_outside   "rival outside"            he is out of the ring
@t_hold   "rival in a hold"          he is caught in a submission hold
@t_finisher   "finisher by rival"        he has just hit his finisher
Actions:
@a_end   "do": "end", "winner": <role>, "how": <how>     ends the match with that result, through the game's own bell, replay
@a_end        and results. how: pinfall (default), submission, tko, dq, countout (in cage: escape; in ladder: ladder).
@a_end        With more than two men in the match add "loser": <role>.
@a_end        In a fourway it only eliminates "loser": the match goes on with the others. In an ironman it scores one fall: the match goes on.
@a_runin   "do": "runin"             the match's run-in comes now ("side": player or opponents if it has both). It needs "runins".
@a_nodq   "do": "no dq"             the match is No DQ from now on
@a_nocount   "do": "no countout"       no more count-outs
Not possible, don't write them: a referee bump or swap, a weapon thrown in, lights out, a pause for a bubble in a match.

{head("BRANCHES (after the match; tried in order, the first whose if holds is taken)")}
{{ "if": "win by submission and (heat >= 2 or choice 1)", "set": {{ "heat": "+1", "rival": "partner" }}, "money": 1000,
  "pages": ["After-match page"], "scene": {{ ... }}, "next": "node_name" }}

"if" is terms joined by and, or, not, and parentheses. (Nesting up to {S["COND_DEPTH"]} deep; 255 pieces at most.) Terms:
  win | lose | notlose                the match result
  win by X | lose by X | ended by X   how: {how}
  heat >= 2                           a variable: == != < <= > >= and a whole number
  chance 50%                          random
  choice 1 | choice 2 ...             the answer to the most recent "ask"
  player is STEVE AUSTIN              a role is that Superstar (any role)
  player in dx                        a role is in that group (see GROUPS)

- The LAST branch of every node has no "if": it always holds.
- "set": {{ "var": 3 }} sets, {{ "var": "+1" }} or "-1" adds. A role can take another role's Superstar ({{ "rival": "partner" }}: a
  betrayal) or a roster string ID ({{ "rival": "kane" }}).
- "money": a whole number. "pages" / "scene": shown after the match.
- "next": a node, "end" (the story is over), or "gameover" (retry if "retries" allows, otherwise game over).

{head("SCENES (3D scenes with the game's own sets, animations and speech bubbles)")}
"scene": {{ "place": "locker room", "cast": ["player", "ally"], "steps": [ step, step, ... ] }}
(or a plain list of steps whose first is {{ "place": ..., "cast": [...] }}). A scene starts with a place.

LIMITS: at most 4 DIFFERENT Superstars ON SCREEN in one scene IN TOTAL, across every place it cuts to (every "cast" entry
and every "who" counts, and so does "player"). At most 8 named in one scene, text and tron included. A node's scene and each
branch's scene are separate scenes, with 4 each. Only the hall and the parking lot have doors.

PLACES ("at" uses these spots; spots 1 and 2 face each other: put two people talking on 1 and 2):
{chr(10).join(pl)}
"stage" and "ring" are in the match's arena; put the node's "arena" on its match.

STEPS (each is one object):
  {{ "place": "hall", "cast": ["heel", {{ "who": "champ", "at": 2, "do": "stand" }}] }}
        cut to a place. Without "at": spots 1, 2, 3 ...
  {{ "say": "Line one\\nline two", "who": "rival" }}
        he speaks in a bubble. "say" can be a list (a bubble each), and a line can differ by player (see LINES BY PLAYER).
        Optional: "do", "camera" ("close", "two", "wide", false). Without "who": just the bubble.
  {{ "ask": "Question?", "options": ["Yes", "No"], "who": "player", "store": "answer" }}
        2 to 4 options. "choice 1", "choice 2" test the latest answer. "store" keeps this one in a variable (1, 2 ...), so a
        later question doesn't overwrite it. The variable must be in "vars".
  {{ "if": "answer == 1 or (heat >= 2 and not player in nwo)", "then": [steps], "else": [steps] }}
        steps played only if it holds: the whole language of BRANCHES; "win" and "win by" mean the last match. "else" optional.
  {{ "set": {{ "heat": "+1" }} }}                  changes variables or roles, as a branch's "set"
  {{ "narrate": "Meanwhile..." }}                a page on black; may be a list
  {{ "who": "rival", "do": "point" }}            an action; add "at": 3 if he isn't on screen yet. "wait": 30 (frames) or true
  {{ "who": "rival", "do": "walk", "door": 1 }}  walks in through a door ("run" runs). Door 1 brings him to spot 1: keep it free
  {{ "who": "rival", "do": "entrance" }}         his ring entrance with music: place "stage" only
  {{ "who": "a", "do": "tag entrance", "with": "b" }}   stage only; b must be the next man in the scene's cast
  {{ "camera": "wide", "on": ["rival", "player"] }}     close, two or wide
  {{ "music": "on" }} or {{ "music": "off" }}      the backstage scene music
  {{ "wait": 60 }}                               1 to 254 frames; 60 is one second
@tron   {{ "tron": "boss", "say": "You'll see me when I'm ready." }}
@tron         the camera cuts to a close-up of the arena's Titantron with that Superstar's entrance video playing on it, the bubble over
@tron         it, then back to the men: for a man who isn't in the building. Use it in "ring". He doesn't count toward the 4 on
@tron         screen, but toward the 8 named. Arenas with a screen of their own: {TRON_ARENAS}. Fallbacks, never an error:
@tron         another arena: the same video full screen. A man with no entrance video, or a backstage place: the bubble alone.
@commentary   {{ "commentary": "jr", "say": "Business is about to pick up!" }}     "jr" or "king"; "say" can be a list
@commentary         the camera goes close on JR and King at the announce table, the line in a bubble ("JR: ..."; those 4 characters count in the line, "KING: " 6).
@commentary         Only in "ring". JR and King don't count toward the 4. No voice clip plays.

"do" ACTIONS:
  {anims}
@taunts   taunt (his first own taunt), taunt 1, taunt 2, taunt 3, taunt 4, win taunt: that Superstar's own moves from the game

{head("VOICE TOKENS")}
Words that depend on who the player picked: {{role.field}} or {{ROSTER NAME.field}}, in any "say", "ask", "narrate", "pages" text:
  {{player.nickname}}     his nickname                  (counts {VM[1]})
  {{player.catchphrase}}  one of his catchphrases, the same one all story    (counts {VM[2]})
  {{player.signoff}}      his closing line, up to 2 lines (counts 2 lines of {VM[3]}: nothing else on those lines)
  {{player.finisher}}     his finisher, with "the"       (counts {VM[4]})
  {{player.insult}}       what he calls people           (counts {VM[5]})
Every Superstar has them (see ROSTER; anyone missing uses the defaults: "{dflt.get("nickname","")}", "{dflt.get("finisher","")}", "{dflt.get("insult","")}").

LINES BY PLAYER: anywhere a bubble's or page's line goes, an object picks the line by the player's Superstar:
  "say": {{ "the_rock": "Line for him.", "group:dx": "Line for DX.", "default": "Anyone else." }}
The first match in the file's order wins; "default" is REQUIRED.

{head("ROSTER (string IDs, with each man's voice, so your lines match his cadence)")}
{voice_tbl}

GROUPS (for "player in dx" and "group:dx"; 1999-era defaults):
{groups_block()}

{head("SAMPLE 1: small (narration, win / lose, variables, a stand-in)")}
{sample("the_grudge.json")}

{head("SAMPLE 2: the features (recast, voice tokens and lines by player, a stored choice, a scene if on a variable, a Titantron cut, JR and King, a tag match by teams, mid-match triggers, a win by submission branch)")}
{sample("screwjob.json")}

{head("CHECKLIST (verify every line before answering; a file that fails one is rejected)")}
[ ] One valid JSON object, no comments, no trailing commas, nothing before or after it.
[ ] "menu" 24 characters or fewer.
[ ] "cast" at most 4 roles (3 if any match has "title"); built-in roles not in it; lists of 8 IDs at most.
[ ] Every name is a role or an exact ROSTER name; every {{name}} and {{role.field}} too.
[ ] COUNT every text line: {LM} characters at most, {{name}} = {NL}, tokens as under TEXT LIMITS; an option {S["OPTION_MAX"]}. Bubble {LN["say"]} lines, page {LN["page"]}, requirement {LN["requirement"]}. Write in mixed case, not all capitals.
[ ] Every node has a "match"; "slots" has the type's exact number, or "teams" has an allowed shape; never both.
[ ] "rules", "arena", "title", "location" from their lists; "rules" fit the type.
[ ] Run-ins only in single (2, one a side) or handicap (1, for the opponents); "at" from its list.
[ ] "script" at most 16 triggers; its roles are in this match; "runin" only with "runins"; "end" with 3+ men has "loser".
[ ] A "requirement" that names anyone has "pages" or a "scene" in the same node.
[ ] Every node's last branch has no "if". Every "next" exists, or is "end" / "gameover".
[ ] At most 11 chapters. At most {S["VARS_MAX"]} vars, each one declared in "vars" before it's tested, set or stored.
[ ] Each scene: starts with a place; COUNT the different Superstars on screen across all its places, player included: 4 at
    most (tron and commentary excepted); 8 named; "at" within the place's spots; walk / run only in hall or parking lot;
    entrance / tag entrance only on stage; commentary only in ring; "ask" has 2 to 4 options; "wait" 1 to 254.
[ ] Every "say" object has a "default".
[ ] No file ever mentions something not listed here: no six-man, no referee bump, no new sets, objects or people. Someone not on
    the ROSTER may be mentioned only in narration text.

{head("MY STORY")}
(Describe it here: who's in it, the feud, the matches and where they happen, any twists, choices, title matches, how it
should end. Whatever isn't specified, make good wrestling-storyline choices yourself.)
"""
    out = []
    for l in txt.split("\n"):
        m = re.match(r"@(\w+) ?(.*)$", l)
        if not m: out.append(l); continue
        st = STATUS.get(m.group(1), False)
        if not st: continue
        out.append(m.group(2) + (("   [EXPERIMENTAL: %s]" % EXP_WHY.get(m.group(1), "")) if st == "exp" else ""))
    return "\n".join(out)

if __name__ == "__main__":
    t = build()
    t.encode("ascii")     # (plain ASCII, as it tells the writer to be)
    open(sys.argv[1], "w", encoding="utf-8", newline="\n").write(t)
    print("%s: %d bytes, %d lines" % (sys.argv[1], len(t), t.count("\n") + 1))
