#!/usr/bin/env python3
"""The commentary pack: Jim Ross & Jerry Lawler from WWE '13 (Wii), for the exe's director.

Inputs (made in /tmp/devbox-0/wwe13 from the disc: tools/rvz.py, tools/akpk.py, tools/dlg.py, tools/lang.py):
  wem_tags.pkl   clip -> [(WWE '13 dialogue event, its switch path)]  (the game's own sorting of its lines)
  dlg.pkl        the dialogue events (their switch groups) and the names we know for hashes
  w16/1_<clip>.wav / .wav.txt   the clip (16 kHz) and what's said in it (whisper.cpp)
  wav/1_<clip>.wav              the clip as the game has it (20 kHz), encoded here
Output: build/commentary.pak (+ .pak.1, ... each under 28 MB: the exe reads them in order as one).

Which lines: those for Attitude Era mode (JR & King) and those shared by both shows; none from a story's own
matches, none naming someone who isn't in the game, none about a match type No Mercy hasn't got. Each WWE '13
dialogue event is one of the director's events (EVMAP; some sorted further by what's said). A line about a
Superstar (WWE '13's wrestler switch, or his name in it) is his: the director uses it when he's the one.
A line naming a move is that move's: used when that's the move (the mod passes it; MOVEKEYS).
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle, os, re, struct, collections, subprocess, sys, hashlib
T = nmp("/tmp/devbox-0/wwe13/")
OUT = nmp("/home/devbox/work/arcade/build/commentary.pak")
PART = 20 * 1024 * 1024

def fnv(s):
    h = 2166136261
    for c in s.lower().encode(): h = (h * 16777619) & 0xFFFFFFFF; h ^= c
    return h
wt = pickle.load(open(T + "wem_tags.pkl", "rb"))
events, NMH = pickle.load(open(T + "dlg.pkl", "rb"))
EVG = {e: gs for e, gs, _ in events}
AEM, WRG, SEXG = fnv("AEM"), fnv("wrestler"), 0x69ea00aa
STORY = {fnv("Story"), fnv("CutScene")}

# ---- WWE '13's dialogue events -> the director's ----------------------------------------------------------
MOVES_BY_TEXT = "*moves"     # sorted by what's said (strike / big move / hold / top rope / generic)
EVMAP = {
    0x95ed1113: MOVES_BY_TEXT, 0xf0ec51f5: MOVES_BY_TEXT, 0xc24c4f85: MOVES_BY_TEXT, 0x9e09b1be: MOVES_BY_TEXT,
    0xa0326643: MOVES_BY_TEXT, 0xd177842c: "big_move", 0x633e0948: "big_move", 0x4b0464af: "reversal",
    0x3856db23: "finisher", 0x567a7cc3: "finisher", 0x12c4f69a: None,
    0xa9f0f0ee: "special", 0x727ce73f: "entrance", 0xda06fa19: "reversal", 0x08f854ad: "reversal",
    0xcdc273f2: "reversal", 0x00e3ac2e: "*ropes",
    0xa4e21389: "dominate", 0x55d9ca6e: "danger", 0xa4544c46: "danger", 0x2f67d005: "comeback",
    0x9a7e9804: "top_rope", 0x973772a4: "submission", 0x0eaf5e13: "run_in", 0xcc4dc0c7: "elimination",
    0xf46e8fc6: "win", 0x9ff7d4fd: "match_start", 0x4313fded: "kickout_2", 0x0703fa9c: "kickout_2",
    0x8db6b18c: "kickout_1", 0xf903b5d5: "pin", 0xdcdd8e36: "pin", 0x7945918f: "pin", 0x8c83dc4d: "taunt",
    0x46437a40: "weapon", 0x6d1c4f14: "weapon_hit", 0x3f8d5fff: "*landing", 0xd9c28619: "blood",
    0x0f191262: "tag", 0x66a8616d: "tag", 0xce9b244d: "outside", 0x73ad32a7: "outside", 0x803f55c5: "outside",
    0x1adb2b83: "outside_hit", 0x02cd6a26: "outside_hit", 0xa096be35: "outside_hit", 0x5522274d: "back_in",
    0x959312ef: "rumble_entry", 0x20731e57: "aerial_miss", 0x15c33178: "*landing", 0xfa2035e4: "sign_off",
    0xc5f04581: "save", 0xa1621ef8: "save", 0xe989f3c5: "count", 0x17fce881: "lockup", 0x809c5b5c: None,
    0x3cb888b2: "escape", 0x94109dd8: "table", 0x6b9d27f6: "*event", 0x5cb513b4: "*event", 0x79a6737b: "cage",
    0xcc2ac257: "*start", 0xb5980e62: "filler", 0x7f1030a8: "filler", 0x21fc6ca8: "tired",
    0x0a13f9ab: "*opening", 0x14bd850c: "kickout_2", 0xd6ffb590: "dominate", 0xf45a787f: MOVES_BY_TEXT,
    0x3abf6beb: "knockdown", 0x4a784feb: "finisher", 0x371ae601: "save", 0x1bdde36d: "filler",
    0xf505a939: MOVES_BY_TEXT, 0x028719ea: "top_rope", 0x7fd9d2f1: "cage", 0x1a8319d4: "cage", 0x1952005c: "filler",
}

NM_EVENTS = ["match_start", "lockup", "strike", "strike_big", "grapple", "big_move", "knockdown", "reversal",
             "special", "finisher", "pin", "count", "kickout_1", "kickout_2", "pinfall", "submission",
             "rope_break", "escape", "tap_out", "outside", "back_in", "outside_hit", "top_rope", "aerial",
             "aerial_miss", "weapon", "weapon_hit", "table", "blood", "tag", "save", "taunt", "run_in",
             "dominate", "comeback", "tired", "elimination", "rumble_entry", "cage", "ladder", "win", "dq",
             "entrance", "sign_off", "filler", "danger"]

def by_text(kind, t):
    """the director's events for a line of a sorted-by-text event (several: the same line used for each)"""
    l = t.lower()
    if kind == "*ropes":
        return ["rope_break"] if "rope" in l else ["reversal"]
    if kind == "*landing":
        if "table" in l: return ["table"]
        if "chair" in l: return ["weapon_hit"]
        if "out of the way" in l or "nick of time" in l: return ["aerial_miss"]
        return []
    if kind == "*event":
        if re.search(r"\bcage\b", l): return ["cage"]
        if "table" in l: return ["table"]
        if "chair" in l: return ["weapon_hit"]
        return []
    if kind == "*start":
        if re.search(r"battle|royal|rumble|contestants|tag|team|submission|cell|cage|ladder|table|iron man|contender|elimination|rules|title|"
                     r"championship|six|triple|fatal|hardcore|anything goes|falls|ladies|divas|i quit|first blood", l):
            return []
        return ["match_start"]
    if kind == "win" and re.search(r"\bthey\b|\bthey're\b|\bteam|\bboth\b", l): return []
    if kind == "win":
        return ["win", "pinfall"] + (["tap_out"] if not re.search(r"pin|three|count|cover|shoulders", l) else [])
    if kind == "*opening":
        return ["danger"] if re.search(r"trouble|hurt|danger|breath|can.t|reeling|in a bad way|struggling", l) else ["special"]
    if kind == "top_rope" and re.search(r"cage|structure|cell|break for it", l): return []
    if kind == "sign_off":
        # only the plain good-nights: none from a show or a place
        if re.search(r"\bfrom\b|night of|presented|brought to you|pay.?per", l): return []
        return ["sign_off"]
    if kind == "count":
        if "kick" in l: return ["kickout_2"]
        return ["count"] if re.search(r"\b(one|two|three|1|2|3)\b", l) and len(l) < 30 else []
    if kind in ("match_start", "*start") and (len(l.split()) > 16 or re.search(r"how this one works|this match|this one|"
                                               r"rules|type of|game\b|format|object|winner will|whoever|first to", l)):
        return []
    if kind in ("match_start", "*start") and re.search(r"structure|hell|steel|cage|cell|blood|three|four|competitors|everyone|anyone.s|their|they\b|battle|royal|rumble|contestants|trio|respective|partner|tag|team|title|championship|cell|cage|"
                                           r"ladder|table|six|fatal|triple threat", l):
        return []
    if kind == "filler":
        return [] if re.search(r"quit|submi|give in|refus|ladder|cage|cell|table|tag|rules|hardcore|chant|\bref\b|that thing|weapon|chair|object|drop it|put it down|enough", l) else ["filler"]
    if kind in ("*bio", "*momentum"):
        # a Superstar's own lines in Legends: how he's doing (taking a beating / fighting back / on top)
        if re.search(r"hang in there|come on|keep it together|might be it for|could be it for|\bhurt|in trouble|trouble here|"
                     r"resilience is|tested|in a bad way|struggl|reeling|through the pain|in pain|taking a beating|"
                     r"in danger|can.t take|wobbl|stagger|on the ropes|needs to|survive|weather", l): return ["danger"]
        if re.search(r"comeback|come back|fight(ing|s)? back|fir(ed|ing) up|rall(y|ies)|not done|won.t stay down|"
                     r"getting up|turn(ed|ing)? the (tide|tables)|momentum|back in (it|this)|shift|second wind|"
                     r"refuses|won.t quit|never say die|digging deep|way back|write .{0,20}off|risen|turn ?around|tide|"
                     r"pendulum .{0,20}(swung|swing)|swung|changed|turning point|on the up|back on (his|top)", l): return ["comeback"]
        if kind == "*momentum" and re.search(r"seesaw|see-saw|anyone.s (game|match)|back and forth|either (man|way)", l): return ["filler"]
        if kind == "*momentum" and len(l.split()) > 14: return ["filler"]
        return ["dominate"]
    if kind == "*landing2":
        return ["aerial_miss"] if re.search(r"miss|nobody|no one|out of the way|moved|empty|crash|nothing but", l) else ["aerial"]
    if kind == "*elimrecover":
        return ["escape"] if re.search(r"hang|hold|still in|saved|stay|survive|not yet|close", l) else []
    if kind == "*lowevent":
        return []
    if kind == "*goingfor":
        return ["special"]
    if kind == "tired":
        # (the time-limit match's: only "five minutes have elapsed", for the mod's five minutes in)
        return ["tired"] if "elapsed" in l else []
    if kind == "*fatigue": return ["tired"]
    if kind == MOVES_BY_TEXT:
        if re.search(r"kick(s|ed)? out|kickout|shoulder up|two count|near fall", l): return ["kickout_2"]
        if re.search(r"team|tag\b|partner|trio|both men|both of them|they are|they're", l): return []
        if re.search(r"sleeper|headlock|lock|hold|crank|stretch|clutch|crab|armbar|arm bar|figure|camel|surfboard|"
                     r"nelson|submi|tap|choke|claw|crossface|sharpshooter|working on|going to work|grinding", l):
            return ["submission"]
        if re.search(r"top rope|off the top|moonsault|high.?fly|from the top|frog splash|swanton|leap|flying|"
                     r"senton|diving|leaping|airborne|aerial", l):
            return ["aerial"]
        if re.search(r"punch|right hand|left hand|jab|kick|chop|stomp|knee|forearm|headbutt|head butt|slap|"
                     r"uppercut|elbow|haymaker|fist|rake|kick", l):
            return ["strike_big"] if re.search(r"big|huge|massive|vicious|thunderous|stiff|devastating|boot|"
                                               r"dropkick|clothesline|superkick|spear|lariat", l) else ["strike"]
        if re.search(r"clothesline|lariat|big boot|dropkick|spear|shoulder block|tackle|superkick", l):
            return ["strike_big"]
        if re.search(r"suplex|slam|ddt|powerbomb|piledriver|driver|breaker|spinebuster|bulldog|facebuster|"
                     r"chokeslam|leg sweep|neck|back|drop|toss|throw|flip|impact|devastat|destructive|"
                     r"dangerous|monkey|hip toss|arm drag|snapmare|press", l):
            return ["big_move", "grapple"] if re.search(r"monkey|hip toss|arm drag|snapmare|takedown|toss", l) else ["big_move"]
        if len(l) < 60: return ["grapple", "big_move"]       # "Oh, man!", "What a move!"
        return ["big_move"]
    return [kind]

# ---- the No Mercy roster: id -> the names they're called -------------------------------------------------
R = [
 (0x0001, r"the rock|\brocky\b|great one|brahma bull|people'?s champ"),
 (0x0002, r"stone cold|\baustin\b|rattlesnake|bionic redneck"),
 (0x0003, r"undertaker|dead ?man|phenom\b|\btaker\b"),
 (0x000A, r"undertaker|dead ?man|phenom\b|\btaker\b"),
 (0x0004, r"\bkane\b|\bcain\b|big red (machine|monster)|devil'?s favou?rite"),
 (0x0005, r"steven richards|stevie richards"),
 (0x0006, r"mankind|hardcore legend|hardcore icon|foley"),
 (0x0007, r"cactus|hardcore legend|hardcore icon|foley"),
 (0x0008, r"foley|hardcore legend|hardcore icon"),
 (0x0009, r"shawn|michaels|michael'?s\b|\bhbk\b|h\.b\.k|heart.?break kid|show ?stopper"),
 (0x0101, r"triple h|\bhhh\b|helmsley|\bhunter\b|the game\b|cerebral assassin|connecticut blue ?blood"),
 (0x0102, r"x-?pac|x-?poc|1-2-3 kid|waltman"),
 (0x0103, r"billy gunn|billigan|mr\.? ass\b|bad ass billy"),
 (0x0104, r"road dogg|road dog|jesse james"),
 (0x0201, r"big show|the show\b|\bgiant\b|biggest athlete"),
 (0x0202, r"malenko|thousand holds|1,?000 holds"),
 (0x0203, r"perry saturn|\bsaturn\b"),
 (0x0204, r"guerrero|latino heat|\beddie\b"),
 (0x0301, r"jericho|y2j|y-2-j"),
 (0x0302, r"kurt angle|olympian|olympic hero|gold medal"),
 (0x0303, r"\btazz?\b|human suplex machine"),
 (0x0305, r"godfather"),
 (0x0306, r"d'?lo\b|d-lo"),
 (0x0307, r"val venis|valvenus|\bvenis\b"),
 (0x0308, r"shamrock|world'?s most dangerous"),
 (0x0401, r"rikishi"),
 (0x0402, r"sexay|grand ?master"),
 (0x0403, r"scotty|too hott?y|to hottie"),
 (0x0404, r"\bedge\b"),
 (0x0405, r"\bchristian\b"),
 (0x0406, r"matt hardy|\bmatt\b|hardy boyz"),
 (0x0407, r"jeff hardy|\bjeff\b|hardy boyz"),
 (0x0501, r"faarooq|farooq|\bapa\b|acolytes"),
 (0x0502, r"bradshaw|\bapa\b|acolytes"),
 (0x0503, r"d-?von|dudley"),
 (0x0504, r"bubba|buh buh|dudley"),
 (0x0505, r"hardcore holly|\bbob holly"),
 (0x0506, r"crash holly|\bcrash\b"),
 (0x0507, r"boss ?man|boss van"),
 (0x0508, r"\balbert\b"),
 (0x0601, r"al snow"),
 (0x0602, r"blackman|lethal weapon"),
 (0x0603, r"british bulldog|davey boy"),
 (0x0604, r"mark henry|sexual chocolate|world'?s strongest"),
 (0x0605, r"viscera"),
 (0x0606, r"andre the giant|\bandre\b|eighth wonder"),
 (0x0607, r"buchanan"),
 (0x0608, r"essa rios|\brios\b"),
 (0x0609, r"\btaka\b|michinoku"),
 (0x0701, r"chyna|ninth wonder"),
 (0x0702, r"stephanie"),
 (0x0703, r"\btori\b"),
 (0x0704, r"\bterri\b"),
 (0x0705, r"\btrish\b|stratus"),
 (0x0706, r"\blita\b"),
 (0x0801, r"\bivory\b"),
 (0x0802, r"\blinda\b"),
 (0x0803, r"the kat\b"),
 (0x0804, r"mae young"),
 (0x0805, r"moolah"),
 (0x0901, r"mcmahon|\bvince\b|the chairman|mr\.? macmahon"),
 (0x0902, r"\bshane\b"),
 (0x0903, r"hebner"),
 (0x0205, r"benoit|crippler|rabid wolverine"),
 (0x060A, r"funaki"),
 (0x0408, r"gangrel"),
 (0x030B, r"jarrett|double j\b"),
 (0x0509, r"\bdroz\b"),
 (0x050A, r"mideon"),
 (0x060B, r"meanie"),
 (0x030C, r"pete gas|mean street posse"),
 (0x030D, r"\brodney\b|mean street posse"),
 (0x030E, r"joey abs|mean street posse"),
 (0x060C, r"\braven\b"),
 (0x030F, r"regal"),
 (0x0409, r"tiger ali singh|\btiger\b"),
 (0x060D, r"\bbret\b|\bbrett\b|hitman|excellence of execution|hart foundation|hart attack|king of harts"),
 (0x060E, r"neidhart|nite ?heart|night ?heart|\banvil\b|hart foundation|hart attack"),
]
# names / nicknames of people who aren't in the game (their lines go)
NOT_HERE = (r"anaconda|go to sleep|\bgts\b|attitude adjustment|\b619\b|\brko\b|sister abigail|end of days|"
            r"trouble in paradise|brogue|zig ?zag|code ?breaker|cross rhodes|skull crushing|white noise|kiss of death|"
            r"neutralizer|\bwmd\b|world'?s strongest slam|batista|\bf-?5\b|shell shock|five knuckle|"
            r"cattle mutilation|yes lock|yes! lock|kofi|boom drop|flatliner|flat liner|peacekeeper|tilt.?a.?whirl slam|"
            r"\bking (kicks|wins|is|gets|has|with|covers|hits|booker|of)|king booker|cena|\bpunk\b|orton|sheamus|mysterio|listerio|ziegler|primo|r-?truth|our truth|\bkelly\b|ryback|"
            r"\bmiz\b|del rio|barrett|daniel bryan|kofi|santino|cody|rhodes|dibiase|brodus|tyson|khali|jinder|"
            r"otunga|ryder|\baj\b|\bbeth\b|natalya|layla|\beve\b|kaitlyn|alicia|tamina|rosa|\bbrie\b|nikki|bella|"
            r"\bwade\b|swagger|mcintyre|heath|slater|hornswoggle|tensai|sin cara|booker|goldust|\bhawk|\banimal\b|"
            r"road warrior|legion of doom|omega|\bowen\b|samoan|macho|savage|"
            r"scorpion|sting\b|princess|disaster|bo-?dawg|\bangel\b|dynamite|samurai|ahmed|yokozuna|bearer|"
            r"\bkid\b|warrior|hogan|hulk|piper|diesel|nash|razor|ramon|hall\b|\bdibiase|million dollar|"
            r"jake|snake|roberts|vader|sid\b|sycho|ultimate|perfect|hennig|steamboat|flair|naitch|rude|"
            r"rock 'n' roll|bigelow|sgt|slaughter|mike tyson|iron mike|\bdog\b|the pound|money in the bank|"
            r"briefcase|extreme rules|summerslam|wrestlemania|survivor series|royal rumble|smackdown|raw\b|"
            r"heat\b|pay.per.view|ppv|iron man|guest referee|special ref|\binferno|flames|fire\b|2x4|elimination chamber|"
            r"chamber|hell in a cell|the cell\b|cell roof|ladder|the 2012|divas?\b|i quit|kiss|gauntlet")
MOVEKEYS = [   # (what a line says, the word for it in the game's move names)
    (r"super ?plex", "superplex"), (r"super ?kick|sweet chin", "superkick"), (r"stunner", "stunner"),
    (r"pedigree", "pedigree"), (r"tombstone", "tombstone"), (r"rock ?bottom", "rockbottom"),
    (r"people'?s elbow", "people's elbow"), (r"mandible", "mandible claw"), (r"last ride", "last ride"),
    (r"choke ?slam", "chokeslam"), (r"crossface", "crossface"), (r"walls of jericho", "walls of jericho"),
    (r"sharp ?shooter", "sharpshooter"), (r"ankle lock", "ankle lock"), (r"x.?factor", "x factor"),
    (r"twist of fate", "twist of fate"), (r"downward spiral", "downward spiral"), (r"death valley", "death valley driver"),
    (r"jack ?hammer", "jack hammer"), (r"dominator", "dominator"), (r"doomsday", "doomsday device"),
    (r"banzai", "banzai drop"), (r"olympic slam|angle slam", "olympic slam"), (r"tazz ?mission", "tazzmission"),
    (r"spiral bomb", "spiral bomb"), (r"pump ?handle", "pump handle"), (r"tiger (bomb|driver)", "tiger driver"), (r"swanton|senton", "senton"), (r"frog splash", "frog splash"),
    (r"moon ?sault", "moonsault"), (r"pile ?driver", "piledriver"), (r"power ?bomb", "powerbomb"),
    (r"\bddt\b|d\.d\.t", "ddt"), (r"suplex", "suplex"), (r"neck ?breaker", "neck breaker"),
    (r"back ?breaker", "back breaker"), (r"spine ?buster", "spinebuster"), (r"bulldog", "bulldog"),
    (r"face ?buster", "facebuster"), (r"body ?slam|scoop slam", "scoop slam"), (r"power ?slam", "powerslam"),
    (r"samoan drop", "samoan drop"), (r"monkey (flip|toss)", "monkey"), (r"russian leg sweep", "russian leg sweep"),
    (r"hip toss", "hip toss"), (r"arm ?drag", "arm drag"), (r"snap ?mare", "snapmare"), (r"sleeper", "sleeper"),
    (r"head ?lock", "headlock"), (r"arm ?bar", "armbar"), (r"boston crab", "boston crab"),
    (r"figure.?(four|4)", "figure 4"), (r"camel clutch", "camel clutch"), (r"abdominal stretch", "abdominal stretch"),
    (r"surfboard", "surfboard"), (r"full nelson", "full nelson"), (r"elbow ?drop", "elbow drop"),
    (r"leg ?drop", "leg drop"), (r"knee ?drop", "knee drop"), (r"big boot", "big boot"), (r"drop ?kick", "dropkick"),
    (r"clothes ?line|lariat", "clothesline"), (r"\bspear", "spear"), (r"splash", "splash"),
    (r"head ?butt", "head butt"), (r"upper ?cut", "uppercut"), (r"\bchops?\b", "chop"),
    (r"punch|right hand|left hand|\bjab|\bfist", "punch"), (r"eye rake", "eye rake"), (r"stomp", "stomp"),
    (r"elbow", "elbow"), (r"kick", "kick"), (r"knee", "knee")]
# words that make a line about one particular kind of move: without a key we can match, it isn't used for moves
MOVEWORDS = (r"plex|slam|ddt|bomb|driver|breaker|buster|splash|elbow|leg drop|knee|kick|punch|fist|chop|"
             r"clothesline|lariat|boot|tackle|spear|dropkick|moonsault|senton|crab|lock|hold|sleeper|headlock|"
             r"armbar|stretch|claw|crossface|rana|arm drag|hip toss|toss|sweep|flip|press|bulldog|stunner|"
             r"pedigree|tombstone|chokeslam|choke|throw|takedown|scoop|spine|neck|nelson|clutch|bear ?hug|"
             r"head ?butt|uppercut|stomp|rake|forearm|shoulder|gutwrench|backdrop|back drop|drop|scissor|wrench|cutter|facelock|enzu|rana|irish whip|whip|"
             r"cross ?body|body press|bull ?arm|flying|airborne|aerial|leap|ax(e)? handle|throat|face|nose|jaw|ribs|"
             r"chest|midsection|gut\b|stomach|head|spine|\bback\b|\blegs?\b|\barms?\b|neck|corner|rope|turnbuckle|"
             r"canvas|\bmat\b|apron|floor|outside|steel|steps|barricade|lift|carry|up and over|over the top|"
             r"submission|hold|grab|tackle|charge|running|spin|twist|combination|series|repeat|hub\b|squeez|breath")
MOVE_EVS = ("strike", "strike_big", "grapple", "big_move", "submission", "aerial", "finisher", "reversal")
# a line that's about a kind of match goes only where that's the kind of match
TYPE_WORDS = (r"battle royal|rumble|contestant|entrant|\btag\b|tag team|\bteams?\b|partner|trio|respective|"
              r"title|championship|champion|\bbelt|\bgold\b|contender|\bcage|\bcell\b|ladder|table|\btlc\b|"
              r"t\.l\.c|chairs\b|hardcore|falls count|extreme|\brules\b|street fight|iron man|submission match|"
              r"quit|first blood|elimination|chamber|gauntlet|handicap|triple threat|fatal|four.?way|4-way|"
              r"six.?man|eight.?man|divas?\b|women'?s|ladies|main event|pay.?per.?view|\braw\b|smackdown|"
              r"guest referee|special referee|special ref|the bell rings for|world heavyweight|intercontinental|"
              r"tonight on|last week|next week|heat\b|flames|inferno|casket|ambulance|buried|"
              r"bragging rights|night of champions|over the limit|survivor series|summer ?slam|"
              r"wrestle ?mania|money in the bank|no way out|vengeance|backlash|judgment day|"
              r"unforgiven|armageddon|no mercy|cyber sunday|great american bash|capitol punishment|"
              r"king of the ring|in your house|breaking point|fatal 4|tribute to the troops|saturday night|"
              r"superstars|\bnxt\b|\becw\b|wcw|nitro|this sunday|tonight'?s event|our event|this event")
TYPE_OK = {"tag": r"\btag\b|tag team|\bteams?\b|partner", "save": r"\btag\b|\bteams?\b|partner",
           "rumble_entry": r"battle royal|rumble|contestant|entrant", "elimination": r"battle royal|rumble|contestant|entrant",
           "weapon": r"chairs\b", "weapon_hit": r"chairs\b", "table": r"table", "cage": r"\bcage|\bcell\b"}
NUMS = {w: i for i, w in enumerate("zero one two three four five six seven eight nine ten eleven twelve thirteen "
        "fourteen fifteen sixteen seventeen eighteen nineteen twenty".split())}
def rumble_number(l):
    """a Rumble line's entrant number ('-': none said; None: a line we can't place)"""
    if re.search(r"third|half|final|last|way through|quarter|first", l): return None
    m = re.search(r"\b(\d{1,2})(st|nd|rd|th)?\b", l)
    if m: return "#%d" % int(m.group(1))
    m = re.search(r"\b(twenty|thirty)[ -]?(one|two|three|four|five|six|seven|eight|nine)?\b", l)
    if m: return "#%d" % ((20 if m.group(1) == "twenty" else 30) + NUMS.get(m.group(2) or "zero", 0))
    for w, i in NUMS.items():
        if i >= 2 and re.search(r"\b(number|no\.?) %s\b|\b%s(th)? entrant" % (w, w), l): return "#%d" % i
    return "-"

QUALIFIERS = ("german|aztec|belly|gutwrench|gut wrench|snap|northern|fisherman|tiger|vertical|dragon|exploder|"
              "fallaway|side|cobra|underhook|jumping|running|diving|spinning|flying|russian|reverse|inverted|"
              "tornado|cradle|sitout|sit-out|jackknife|missile|michinoku|springboard|double|triple|super|top rope|"
              "brain|cross|crucifix|swinging|front|back|leaping|corkscrew|shooting|praying|lionsault|stalling|"
              "release|bridging|half|full|scoop|military|sleeper|scissor|helicopter|torture|olympic|pump handle")
CAPS_OK = {"Oh", "God", "Lord", "JR", "J.R.", "King", "I", "I'm", "I've", "I'd", "I'll", "OK", "Okay", "Good", "My", "Man",
           "Wow", "Whoa", "Woah", "No", "Yes", "Ah", "Aw", "Ow", "Ooh", "Uh", "Boy", "Mama", "Almighty", "Heaven",
           "Goodness", "Gosh", "Bam", "Folks", "Jim", "Jerry"}
def proper_noun(t):
    """a capitalised word inside the line (a name we can't place: a move's, someone's)"""
    words = re.findall(r"(?:^|[.!?]\s+|\s)([A-Z][\w.'-]*)", t)
    starts = set(m.group(1) for m in re.finditer(r"(?:^|[.!?,]\s+)([A-Z][\w.'-]*)", t))
    for w in re.findall(r"\b[A-Z][\w.'-]*", t):
        if w in CAPS_OK or w.rstrip(".!?,") in CAPS_OK: continue
        if w in starts: continue
        return True
    return False

def movekey(t):
    """the move a line names (the game's word for it; with the kind of it when the line says, e.g. german suplex)"""
    l = t.lower().replace("-", " ")
    for rx, k in MOVEKEYS:
        m = re.search(rx, l)
        if not m: continue
        q = re.search(r"(%s)\s+$" % QUALIFIERS, l[:m.start()])
        if q and q.group(1) not in k: return q.group(1).replace("gut wrench", "gutwrench") + " " + k
        return k
    return "-"

def clean(t):
    t = " ".join(t.split())
    m = re.match(r"^(.{6,}?)(?:[ ,.!?]+\1)+[ .!?]*$", t)
    return (m.group(1).rstrip(" ,") + ("" if m.group(1)[-1:] in ".!?" else "!")) if m else t
FIX = [(r"\bEthico\b", "Jericho"), (r"X-POC", "X-Pac"), (r"\bCain\b", "Kane"), (r"Valvenus", "Val Venis"),
       (r"Billigan", "Billy Gunn"), (r"\bpen\b", "pin"), (r"Chokeslan", "Chokeslam")]

# ---- WWE Legends of WrestleMania (PS3): JR & King again, sorted by the clip's own name -------------------
# Made in /tmp/devbox-0/lowm from SOUND/COMMEN_A.YAF: mp3/NNNN.mp3 (each clip, its ID3 title the game's name
# for it: LOW09_JR_Fin_Rock_002, 09_KING_MOVE_Suplex_001, ...), titles.pkl, w16/NNNN.wav(.txt) (whisper.cpp).
# Its lines go in as source 1 (the operator's COMMENTARY LINES: WWE 13 + LEGENDS).
L = nmp("/tmp/devbox-0/lowm/")
LOWM_ON = os.path.exists(L + "titles.pkl") and "--no-legends" not in sys.argv
LOWM_AF = open(L + "af.txt").read().strip() if os.path.exists(L + "af.txt") else "anull"
# the clip names' Superstars who are in No Mercy
TMAP = {"austin": [0x0002], "rock": [0x0001], "undertaker": [0x0003, 0x000A], "taker": [0x0003, 0x000A],
        "kane": [0x0004], "hbk": [0x0009], "hhh": [0x0101], "helmsley": [0x0101], "show": [0x0201],
        "jericho": [0x0301], "y2j": [0x0301], "tazz": [0x0303], "edge": [0x0404], "mhardy": [0x0406],
        "jhardy": [0x0407], "holly": [0x0505], "bossman": [0x0507], "bulldog": [0x0603], "henry": [0x0604],
        "andre": [0x0606], "regal": [0x030F]}
# Legends' (and SvR 2008/09's) people who aren't in No Mercy, as JR and King call them (and as whisper hears them)
LOWM_NOT_HERE = (r"snuka|snooker|superfly|super fly|dusty|american dream|"
                 r"valentine|beefcake|leafcake|brutus|barber|hacksaw|duggan|sheik|volkoff|volkov|kamala|koko|bird ?man|"
                 r"junkyard|\bjyd\b|bundy|studd|honky|bam ?bam|dibiase|d\.? ?b\.? ?asi|million|hayes|freebird|heenan|hienan|"
                 r"weasel|fuji|jimmy hart|mouth of the south|\barn\b|anderson|horsem[ae]n|hammer|hogan|hovin|hulk|hawk|"
                 r"animal|piper|rowdy|roberts|rude|flair|perfect|warrior|bearer|slaughter|sarge|"
                 r"yokozuna|savage|macho|megaphone|kilt|khali|batista|cena|orton|kofi|cody|santino|\bmiz\b|morrison|"
                 r"\bmvp\b|\bjbl\b|\bpunk\b|finlay|hornswoggle|umaga|boogey|festus|jesse|\bken\b|kennedy|kendrick|"
                 r"carlito|chavo|mysterio|\brey\b|\bshad\b|\bjtg\b|cryme|snitsky|dreamer|ryder|hawkins|\bcurt\b|\bben\b|"
                 r"benjamin|shelton|\bjimmy\b|\byang\b|mean gene|madison square|the garden|silverdome|pontiac|"
                 r"\bmichael\b|\bjake\b|\bsnake\b|cobra|\bducky\b|\bdeuce\b|\bbrain\b|manager|legends? of|"
                 r"\bera\b|\b19\d\d\b|debut|back in the day|years ago|in his day|eighties|80s|history|hall of fame|\burn\b|"
                 # (their finishers)
                 r"bionic|perfect.?plex|rude awakening|dream\b|superfly|gorilla press|cobra|atomic|earthquake|avalanche|"
                 r"ax bomber|heart punch|five star|slingshot|french|megaphone|hot ?rod|enforcer|nature boy|the dragon|the hammer|the model|the doctor|hulkster|the mouth|"
                 r"the natural|immortal|ravishing|the genius|madness|the beast|the brute|the bird|"
                 r"the barbarian|the bruiser|the butcher|the cowboy|the crusher|the gangster|the maniac|the rocker|"
                 r"the tornado|the soldier|the matador|the express|the outlaw|the patriot|the sheriff|the samurai|cane\b|2 ?by ?4|two by four")
# what a clip's name says the move is: (the director's event, the game's word for it ('-': any of them))
_TM = {
    "strike": "Punch:punch ComboPnch:punch LeftPunch:punch OverhandPunch:punch PunchGut:punch HeadlockPunch:punch "
              "Kick:kick ComboKck:kick KickGut:kick Chop:chop LeftChop:chop Elbow:elbow BackElbow:elbow "
              "Knee:knee KneeLift:knee Stomp:stomp StompLeg:stomp Headbutt:head~butt Uppercut:uppercut Slap:- "
              "ThrtThrst:- ThroatThrust:- Ax:- AxHandle:- EyePoke:- Bite:- BackRake:eye~rake Dirty:- LowBlow:- Combo:-",
    "strike_big": "Boot:big~boot Lariat:clothesline ShortRangeClothesline:clothesline Dopkick:dropkick Spear:spear "
                  "Tackle:- ShoulderTackle:- ShoulderBlock:- Superkick:superkick SweetChinMusic:superkick "
                  "Enzuigiri:- FlyForearm:- FlyingForearm:- Bronco:- LouThesz:- LouPress:- Crossbody:- CrossBody:-",
    "grapple": "ArmDrag:arm~drag Armdrag:arm~drag HipToss:hip~toss Toss:- MonkeyFlip:monkey Takedown:- Irish:- "
               "IrishWhip:- Fireman:- JudoFlip:- DropToe:- Basic:- ElbowDrop:elbow~drop FistDrop:-",
    "big_move": "Slam:slam ScoopSlam:scoop~slam OkSlam:slam Power:- Powerbomb:powerbomb Piledriver:piledriver DDT:ddt "
                "Suplex:suplex German:german~suplex Bly2BlySpx:belly~suplex InvSpx:suplex NorthLight:suplex "
                "BrainBstr:brain~buster Samoan:samoan~drop Sideslam:slam Sidewalk:slam SidewalkSlam:slam STO:- "
                "Russian:russian~leg~sweep InvRussian:- RussianLegSweep:russian~leg~sweep NeckBrkr:neck~breaker "
                "SwingNeckbreaker:neck~breaker BackBrkr:back~breaker JawBrkr:- SpineBstr:spinebuster Bulldog:bulldog "
                "FaceBuster:facebuster ChokeSlam:chokeslam Stunner:stunner Impact:- Hur:- Gutwrench:- AtomicDrop:- "
                "Airplane:- BackDrop:- Driver:- Splash:splash LegDrop:leg~drop JackKnife:powerbomb",
    "submission": "ArmSub:- Armbar:armbar Embar:- BodySub:- Camel:camel~clutch Choke:- Crab:boston~crab "
                  "BostonCrab:boston~crab FigFour:figure~4 HdScissor:- HeadScis:- Headlock:headlock HeadLock:headlock "
                  "LegLock:- Sleeper:sleeper Sharpshooter:sharpshooter BearHug:- Claw:- WristClutch:- NeckHang:- "
                  "KeyLock:- HammerLock:- ArmLock:- ArmWrench:- HoldingLeg:- HoldingBack:- FrontFace:-",
    "aerial": "HighRisk:- Moonsault:moonsault Senton:senton Frog:frog~splash DivingAx:- MissileDropKick:dropkick",
    "pin": "RollUp:- InsideCradle:- SchoolBoy:- BackSlide:- Sunset:-",
    "reversal": "ReverseKick:-",
    "knockdown": "Dropped:-", "danger": "Staggered:-",
}
TITLE_MOVE = {}
for ev, items in _TM.items():
    for it in items.split():
        k, key = it.split(":"); TITLE_MOVE[k.lower()] = (ev, key.replace("~", " "))

LOWM_FIX = [(r"\bSean\b", "Shawn"), (r"\bHelmsley\b", "Helmsley"), (r"\bRiggle\b", "Regal"), (r"\bPooldog\b", "Bulldog"),
            (r"\bSphere\b", "Spear"), (r"\bedge\b", "Edge")]
FIN_OK = set(k for _, k in MOVEKEYS[:MOVEKEYS.index((r"moon ?sault", "moonsault"))])
NUMKIND = {"1190": "strike", "1210": "outside_hit", "1160": "outside_hit", "1200": "big_move", "0404": MOVES_BY_TEXT,
           "0405": MOVES_BY_TEXT, "0410": "pin", "0120": "top_rope", "0880": "taunt", "0840": "*fatigue", "1140": "dominate"}
def lowm_kind(title):
    """a Legends clip's name -> (kind, the Superstar's ids or None, the move's word or None) (kind None: not used)"""
    # (a number for a name, 10405301a: SvR's -- the first digit who says it, 1 JR / 2 King; the next four the moment)
    if re.fullmatch(r"[12]\d{7}[a-z]", title):
        return NUMKIND.get(title[1:5]), None, None
    p = title.split("_")
    if p[0] in ("LOW09", "09", "08"): p = p[1:]
    if len(p) < 2: return None, None, None
    spk, cat, rest = p[0].lower(), p[1].lower(), [x for x in p[2:] if not re.fullmatch(r"\d+[a-z]*|cas|short", x.lower())]
    if spk not in ("jr", "king", "low"): return None, None, None      # (RAW: Michael Cole's, and the like)
    name = rest[0].lower() if rest else None
    whom = None
    if cat in ("bio", "fin", "finisher", "elim", "eliminated", "winner", "out", "enter", "chain", "blood") and name:
        if name not in TMAP: return None, None, None        # someone not in No Mercy
        whom = TMAP[name]
    if cat == "bio": return "*bio", whom, None
    if cat in ("fin", "finisher"): return "finisher", whom, None
    if cat in ("moves", "moverepeat") or (cat == "move" and (not name or name == "generic")): return MOVES_BY_TEXT, None, None
    if cat in ("move", "chainst", "chainend", "chainrepeat"):
        if not name or name not in TITLE_MOVE: return None, None, None
        ev, key = TITLE_MOVE[name]
        return ev, None, (key if key != "-" else None)
    # King's "Here goes Andre!" / "Rocky! Rocky! Go!", JR's "And this could do it.": going for the big one
    if cat in ("chainstart", "chainstrt"): return "*goingfor", None, None
    if cat in ("chainrev", "chainreverse"): return "reversal", None, None
    if cat == "chat": return "filler", None, None
    if cat == "momentum": return "*momentum", None, None
    if cat == "event": return "*lowevent", None, None
    if cat == "blood": return None, None, None
    if cat in ("weaponhold", "weapHold".lower(), "weapon"): return "weapon", None, None
    if cat == "weaponhit": return "weapon_hit", None, None
    if cat == "winner": return "win", whom, None
    if cat in ("eliminated", "elim"): return "elimination", whom, None
    if cat == "elimrecover": return "*elimrecover", None, None
    if cat == "tagteam": return "tag", None, None
    # (SvR's Enter: walking out; Legends' Enter / WrstEnter: tagged in. Legends' Out: out of the match)
    if cat == "enter" and spk == "low": return "entrance", whom, None
    if cat in ("enter", "wrstenter"): return "tag", whom, None
    if cat == "out": return "elimination", whom, None
    if cat in ("exit", "exitring"): return "outside", whom, None
    # (Legends' top-rope lines are all about it going wrong: pushed off, crashing, nobody home)
    if cat in ("toprope", "toprperslt"): return "aerial_miss", None, None
    if cat == "pincount": return "count", None, None
    if cat == "kickout": return "kickout_2", None, None
    if cat == "match": return "*start", None, None
    return None, None, None

VOCAB = set()
def vocab():
    """the words whisper wrote in lower case somewhere (in either game's lines, not first in a sentence): real words"""
    c = collections.Counter()
    for d, pat in ((T + "w16/", ".wav.txt"), (L + "w16/", ".wav.txt")):
        for f in os.listdir(d):
            if not f.endswith(pat): continue
            for w in re.findall(r"(?<![.!?] )(?<!^)\b[a-z][\w'-]*", open(d + f).read()):
                c[w.replace("'s", "")] += 1
    return set(w for w, n in c.items() if n >= 3)
def lowm_entries():
    VOCAB.update(vocab())
    titles = pickle.load(open(L + "titles.pkl", "rb"))
    out = []
    for i, title in sorted(titles.items()):
        if not title or title == "?": continue
        f = L + "w16/%04d.wav.txt" % i
        if not os.path.exists(f): continue
        kind, whom, tkey = lowm_kind(title)
        if not kind: continue
        t = clean(open(f).read().strip())
        for a, b in FIX + LOWM_FIX: t = re.sub(a, b, t)
        if len(t) < 2 or len(t) > 220: continue
        l = t.lower()
        # a created Superstar's lines call him by his kind ("What a move by the cowboy!"): those go
        if "_CAS" in title and "Finisher" not in title: continue
        if kind == "count" and re.search(r"\bno\b|kick", l): continue
        if kind in ("win", "elimination") and re.search(r"\bthey\b|\bthey're\b|\bteam", l): continue
        if kind == "*start" and re.search(r"structure|steel|cell|cage|graphic|ghastly|grotesque|blood|emt|hell|careers|"
                                          r"demolition|odds|their|three|family", l): continue
        # a capitalised word that's no word we know (whisper's go at a Legend's name): not one of ours
        if not any(re.search(rx, l) for _, rx in R) and any(
                w.lower().rstrip(".,!?").replace("'s", "") not in VOCAB and w.rstrip(".,!?") not in CAPS_OK
                for w in re.findall(r"\b[A-Z][\w'-]*", t)): continue
        if "_CAS" in title and re.search(r"\b(by|for|from|of|to) the \w+[.!?]*$|\bthe \w+('s| got| is| has| with| just| looks| can| did| was| hits| takes)\b", l) \
                and not re.search(r"the (move|crowd|fans|ring|match|ref|referee|mat|canvas|ropes|corner|end)\b", l): continue
        # a finisher not said to be whose: only No Mercy's finishers by name (else it's a Legend's)
        if kind == "finisher" and not whom and movekey(t) not in FIN_OK | {"-"}: continue
        # King's lines that go Superstar by Superstar (numbered by them: "Here goes Andre!", "Bulldog reversed it!"):
        # only those we can name, or that name nobody ("He reversed it!"); whisper's way with the Legends' names
        # makes the rest no use
        if re.match(r"LOW09_(King_(ChainRev|ChainStrt|Momentum|Winner|Eliminated|WrstEnter|Moves|Enter)|JR_(Winner|Out|Enter))_\d", title) \
                and not any(re.search(rx, l) for _, rx in R) and not (
                    re.search(r"\b(he|he's|his|him|himself)\b", l) and not proper_noun(t)
                    and not re.match(r"[\w.'-]+ (reversed|countered|counted|is|has|with|in|got)\b", t)) \
                and not ("_ChainStrt_" in title and int(re.sub(r"\D", "", title.split("_")[-1]) or 0) >= 77): continue
        # an ordinary cover: not the roll-ups'
        if kind == "pin" and re.search(r"roll|cradle|sunset|backslide|back slide|package|power ?ball|flip|school", l): continue
        spk = title.split("_")[1 if title.split("_")[0] in ("LOW09", "09", "08") else 0][:1].upper()
        if re.fullmatch(r"[12]\d{7}[a-z]", title): spk = "J" if title[0] == "1" else "K"
        spk = spk if spk in ("J", "K") else "-"
        sex = "M" if re.search(r"\b(he|he's|his|him|himself|man|guy|fella|boy)\b", t.lower()) else "-"
        out.append(((1, i), kind, 0, sex, t, 1, spk, whom, tkey))
    return out


SPK = nmp("/tmp/devbox-0/spk/pking_plain.pkl")
PLACE_LINES = [   # (clip, event, "@place", speaker)
    ((0, 432013268), "match_start", "@rrmatch", "J"), ((0, 540858993), "match_start", "@rrmatch", "J"),
    ((0, 551525933), "match_start", "@rrmatch", "J"), ((0, 347706420), "filler", "@rrmatch", "-"),
    ((0, 318555970), "sign_off", "@rumble", "J"), ((0, 978901096), "sign_off", "@rumble", "J"),
    ((0, 1013727837), "match_start", "@wrestlemania", "J"), ((0, 1013727837), "sign_off", "@wrestlemania", "J"),
    ((0, 1072283497), "sign_off", "@wrestlemania", "J"),
    ((0, 309579642), "sign_off", "@summerslam", "J"), ((0, 350396445), "sign_off", "@summerslam", "J"),
    ((0, 927185777), "sign_off", "@survivor", "J"),
    ((0, 847618682), "match_start", "@kotr", "-"), ((0, 847618682), "sign_off", "@kotr", "-"),
    ((0, 670404166), "sign_off", "@raw", "J"), ((0, 710965321), "sign_off", "@raw", "J"),
    ((0, 112130925), "sign_off", "@smackdown", "J"), ((0, 393107473), "sign_off", "@smackdown", "J"),
    ((0, 735705785), "sign_off", "@smackdown", "J"),
    # Legends': WrestleMania's, and SvR 2009's Rumble bells
    ((1, 1542), "match_start", "@wrestlemania", "J"), ((1, 1544), "match_start", "@wrestlemania", "K"),
    ((1, 1545), "match_start", "@wrestlemania", "J"), ((1, 1543), "filler", "@wrestlemania", "K"),
    ((1, 1546), "filler", "@wrestlemania", "J"), ((1, 1547), "filler", "@wrestlemania", "-"),
    ((1, 2019), "match_start", "@rrmatch", "J"), ((1, 2020), "match_start", "@rrmatch", "J"),
    ((1, 2021), "match_start", "@rrmatch", "J"),
]

# ---- JR's names and lead-ins (gen/mknames.py cut them from his lines; the AI voice's from out_ai) ----------------
# The spliced calls (gen/mkkeep.py: what was kept in QA, REVIEW\Keep): JR's lead-ins ("What a move by", "Here comes")
# -- wrestler "*" (the one doing it) / "v*" (the one it's done to) -- and his names (event "name", the wrestler's id),
# each with its heat (how worked up he is at the join) and level: the director joins a lead-in with a name of the same
# game and heat. They're cut from his lines and are in addition to them. Clip key (4, n): keep.json's n.
ELIM_LINES = [   # (clip, speaker): a Rumble elimination, whoever it is (hand-picked, kept in QA)
    ((0, 323212955), "K"), ((0, 967308979), "K"), ((0, 603297573), "K"), ((0, 254660659), "K"), ((0, 115821515), "K"),
    ((0, 980878755), "K"), ((0, 838001049), "J"), ((0, 497907252), "J"), ((0, 614565879), "J"),
    ((1, 1628), "-"), ((1, 1630), "-")]
KEEP = nmp("/tmp/devbox-0/keep/")
NAME_ALIAS = {0x0003: [0x000A]}      # (the American Bad Ass Undertaker: JR's names for the Dead Man)
SPLICE = {}                          # clip key -> "heat:db" (the index's splice column)
# a lead-in's moment, by its words, when the line it was cut from isn't filed under someone (event, the one it's about)
LEAD_EV = [(r"here comes$", "entrance", "*"), (r"down goes$", "knockdown", "v*"), (r"kick ?out (by|from)$", "kickout_2", "*"),
           (r"(counter(ed)?|reversal) by$", "reversal", "*"), (r"victory by$", "win", "*"),
           (r"(high flying|high.risk) maneuver.*from$", "top_rope", "*"), (r"arrogance.*from$", "taunt", "*"),
           (r"performance|beat down|series of attacks|the way of|raw animalistic|exhibition", "dominate", "*"),
           (r"(shot|kick|headbutt|axe handle).*(by|from)$", "strike_big", "*"),
           (r".", "big_move", "*")]
def splice_rows(rows):
    import json
    if not os.path.exists(KEEP + "keep.json"): return []
    keep = json.load(open(KEEP + "keep.json"))
    by_key = collections.defaultdict(list)
    for r in rows: by_key[r[0]].append(r)
    out = []
    for n, x in enumerate(keep):
        key = (4, n)
        if x["kind"] == "name":
            if x["game"] == 1 and not LOWM_ON: continue
            SPLICE[key] = "%.2f:%.1f" % (x["heat"], x["db"])
            for w in [x["wid"]] + NAME_ALIAS.get(x["wid"], []):
                out.append((key, "name", w, "-", x["text"], "-", x["game"], "J"))
        elif x["kind"] == "lead":
            if x["game"] == 1 and not LOWM_ON: continue
            SPLICE[key] = "%.2f:%.1f" % (x["heat"], x["db"])
            t = x["text"]; l = t.lower().strip(" .!,")
            mk = movekey(t)
            orig = [r for r in by_key.get(tuple(x["key"]), []) if r[2] >= 0]
            if orig and not re.search(r"here comes$|down goes$", l):
                r = orig[0]; ev = r[1]; who = -3 if r[2] & 0x10000 else -2
                if r[5] != "-" and not r[5].startswith(("@", "#", "+")): mk = r[5]
            else:
                ev, w = next((e, w) for rx, e, w in LEAD_EV if re.search(rx, l)); who = -3 if w == "v*" else -2
            if "military press" in l: mk = "military press"
            if ev not in MOVE_EVS: mk = "-"
            out.append((key, ev, who, "-", t, mk, x["game"], "J"))
        else:   # a Rumble elimination: whole lines are ELIM_LINES (the originals); the openings cut from Legends'
            if x["whole"] or not LOWM_ON: continue
            out.append((key, "elimination", -1, "M", x["text"], "-", 1, x["spk"]))
    print("splices:", collections.Counter((r[1] if r[1] in ("name", "elimination") else "lead", r[6]) for r in out))
    print("lead-ins by event:", collections.Counter(r[1] for r in out if r[2] in (-2, -3)))
    return out

def main():
    rows = []        # (clip, event, wrestler, sex, text, movekey)
    wset_lines = collections.defaultdict(list)    # WWE '13 wrestler value -> texts
    entries = []
    for w, tl in wt.items():
        if not any(p[0] in (AEM, 0) for e, p in tl): continue
        f = T + "w16/1_%d.wav.txt" % w
        if not os.path.exists(f): continue
        t = clean(open(f).read().strip())
        for a, b in FIX: t = re.sub(a, b, t)
        if len(t) < 2 or len(t) > 220 or re.search(r"show voting|verse 2|drakkic|so mow", t.lower()): continue
        for e, p in tl:
            if p[0] not in (AEM, 0): continue
            gs = EVG[e]
            if any(g in STORY for g in gs): continue
            kind = EVMAP.get(e)
            if not kind: continue
            sex = "-"
            if SEXG in gs:
                v = p[gs.index(SEXG)]
                if v == fnv("tag"): continue
                sex = "M" if v == fnv("M") else "F" if v == fnv("f") else "-"
            wv = p[gs.index(WRG)] if WRG in gs else 0
            if wv: wset_lines[wv].append(t)
            entries.append(((0, w), kind, wv, sex, t, 0, "-", None, None))
    if LOWM_ON: entries += lowm_entries()
    # each WWE '13 wrestler switch value: whose lines (the No Mercy Superstar most of them name)
    wmap = {}
    for wv, texts in wset_lines.items():
        cnt = collections.Counter()
        for t in texts:
            for i, rx in R:
                if re.search(rx, t.lower()): cnt[i] += 1
        bad = sum(1 for t in texts if re.search(NOT_HERE, t.lower()))
        if cnt:
            (i, n), = cnt.most_common(1)
            if n >= 2 and n >= bad: wmap[wv] = [x for x, c in cnt.items() if c >= max(2, n // 2)]
    done = set()
    for w, kind, wv, sex, t, src, spk, twhom, tkey in entries:
        l = t.lower()
        if re.search(NOT_HERE, l): continue
        if src == 1 and re.search(LOWM_NOT_HERE, l): continue
        if wv and wv not in wmap: continue
        named = [i for i, rx in R if re.search(rx, l)]
        # (Legends' giant is Andre, not the Big Show)
        if src == 1 and 0x0201 in named and not re.search(r"big show|the show\b", l):
            named = [0x0606 if i == 0x0201 else i for i in named]
        whom = twhom or wmap.get(wv) or named or [-1]
        # a move's line about the man it was done to ("Down goes the Rock!"): his, as the one it hit
        victim = bool(re.search(r"down goes|goes down|in trouble|hurting|reeling|is down|been hit|taking a beating|"
                                r"on the receiving end|eats|can.t get up|staggered|rocked|dazed|felt that|feel that|feeling that|looks hurt|hurt after|is hurt|has to be hurt", l))
        for ev in by_text(kind, t):
            if ev not in NM_EVENTS: continue
            m = re.search(TYPE_WORDS, l)
            if m and not (ev in TYPE_OK and re.fullmatch(TYPE_OK[ev], m.group(0))):
                # (a second type word after an allowed one still rules it out)
                if not (ev in TYPE_OK and not re.search(TYPE_WORDS, re.sub(TYPE_OK[ev], "", l))): continue
            num = "-"
            if ev == "rumble_entry":
                num = rumble_number(l)
                if num is None: continue
            for i in whom:
                key = (w, ev, i)
                if key in done: continue
                done.add(key)
                mk = movekey(t) if ev in MOVE_EVS else num
                if mk == "-" and tkey and ev in MOVE_EVS: mk = tkey
                if ev in ("finisher", "special") and mk == "-" and not twhom and kind != "*goingfor" and not re.search(
                        r"hurt|done|over\b|anything left|punishing|that'?s it|finish|no getting up|devastat|lights out|"
                        r"out cold|gotta be it|can'?t believe|good lord|my god|oh my|what a move|unbelievable|incredible|"
                        r"lethal|deadly|destroy|should do it|it'?s over|looking to end|opportunity|end this|end it|"
                        r"sees? (the|an) opening|big moment|here it comes|this could be|something big|setting", l): continue
                if ev in MOVE_EVS and mk == "-" and (re.search(MOVEWORDS + r"|sunset|blip|flip|rko|liner|rest\b|project|death", l)
                                                     or proper_noun(t)): continue
                # a line about some other moment that names a particular move ("after that powerbomb...")
                if ev not in MOVE_EVS and movekey(t) not in ("-", "kick", "punch", "elbow", "knee", "chop", "stomp"): continue
                # "Another suplex!", "And again!": for when it's the same move again ("+": the director's repeat)
                if ev in MOVE_EVS and (re.search(r"\banother\b|\bagain\b|one more|yet another|more of the same|"
                                                 r"keeps? (that|it) up|enough is enough|get the message", l)
                                       or kind == "*repeat"):
                    mk = "+" + (mk if mk != "-" else "")
                rows.append((w, ev, i if i < 0 or not (victim and ev in MOVE_EVS) else 0x10000 | i, sex, t, mk, src, spk))
    # where the match is: the lines for a Royal Rumble / an arena's show (hand-picked: the rest naming a show go)
    for key, ev, place, spk in PLACE_LINES:
        f = (T + "w16/1_%d.wav.txt" % key[1]) if key[0] == 0 else (L + "w16/%04d.wav.txt" % key[1])
        if key[0] == 1 and not LOWM_ON: continue
        t = clean(open(f).read().strip())
        rows.append((key, ev, -1, "-", t, place, key[0], spk))
    rows += splice_rows(rows)
    # a Royal Rumble's eliminations: no "And the champ is out!" for whoever goes (nor Legends' garbled "Bam, bam");
    # the lines that fit anyone instead (both games, either man)
    rows = [r for r in rows if not (r[1] == "elimination" and r[2] == -1 and re.search(r"\bchamp|bam, bam", r[4], re.I))]
    for key, spk in ELIM_LINES:
        if key[0] == 1 and not LOWM_ON: continue
        f = (T + "w16/1_%d.wav.txt" % key[1]) if key[0] == 0 else (L + "w16/%04d.wav.txt" % key[1])
        t = clean(open(f).read().strip())
        rows.append((key, "elimination", -1, "M" if re.search(r"\bhe\b|he's|his\b", t, re.I) else "-", t, "-", key[0], spk))
    # who says it: Legends' by its name; WWE '13's by voice (gen/spk: a classifier trained on Legends' JR and King,
    # /tmp/devbox-0/spk/pking_plain.pkl: P(King) per clip). Lower case: said to the other man
    pk = pickle.load(open(SPK, "rb")) if os.path.exists(SPK) else {}
    out = []
    for r in rows:
        spk = r[7]
        if r[6] == 0 and spk == "-" and r[0][1] in pk:
            p = pk[r[0][1]]
            spk = "K" if p > 0.8 else "J" if p < 0.2 else "-"
        if spk == "K" and re.search(r"(,\s*|^)(JR|J\.R\.|Jim|Ross)\b(?! is)|\b(JR|J\.R\.|Ross)[,.!?]|\byou,? (JR|Ross)", r[4]) \
                and not re.search(r"jim ross|stone cold", r[4].lower()):
            spk = "k"
        if spk == "J" and re.search(r"(,\s*|^)(King|Jerry)[,.!?]|(,\s*|^)(King|Jerry)$|, King\b(?! [A-Z])|\bKing\?", r[4]) \
                and not re.search(r"king of|king booker|king sheamus|stephen king|king grell", r[4].lower()):
            spk = "j"
        # (a question put to him: "Did you see that?")
        if spk in ("J", "K") and re.search(r"\b(did|can|could|would|have|do|are|were|will) you\b[^.!]*\?", r[4], re.I) \
                and not re.search(r"folks|ladies|gentlemen|fans|everybody|at home|people|(would|will) you look|kidding", r[4].lower()):
            spk = spk.lower()
        out.append(r[:7] + (spk,))
    rows = out
    clips = sorted(set(r[0] for r in rows))
    print("lines", len(rows), "clips", len(clips), "wrestler sets mapped", len(wmap), "of", len(wset_lines))
    c = collections.Counter(r[1] for r in rows); print(sorted(c.items(), key=lambda x: -x[1]))
    named = collections.Counter(r[2] for r in rows if r[2] >= 0); print("named", len(named), named.most_common(70))
    print("speakers", sorted(collections.Counter((r[6], r[7]) for r in rows).items()))
    for src in (0, 1):
        c = collections.Counter(r[1] for r in rows if r[6] == src)
        print("source", src, "lines", sum(c.values()), "clips", len(set(r[0] for r in rows if r[6] == src)), sorted(c.items(), key=lambda x: -x[1]))
    return rows, clips

# (silence off both ends only: a pause inside a line stays -- stop_periods would cut the line at its first pause)
TRIM = "silenceremove=start_periods=1:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_threshold=-50dB,areverse"
def ogg_of(c):
    if c[0] == 4: return KEEP + "ogg/%04d.ogg" % c[1]
    return T + "ogg/%d.ogg" % c[1] if c[0] == 0 else L + "ogg/%04d.ogg" % c[1]
_keep = None
def wav_of(c):
    global _keep
    if _keep is None:
        import json; _keep = json.load(open(KEEP + "keep.json"))
    return nmp(_keep[c[1]]["file"])   # (keep.json holds the sandbox's paths)
def encode(clips):
    os.makedirs(T + "ogg", exist_ok=True); os.makedirs(L + "ogg", exist_ok=True); os.makedirs(KEEP + "ogg", exist_ok=True)
    data = {}
    todo = [c for c in clips if not os.path.exists(ogg_of(c))]
    procs = []
    for i, c in enumerate(todo):
        # (names and lead-ins: cut to the word already; all at one level, so a name sits right after any lead-in)
        # (the spliced calls' pieces: as cut, levels as they were -- the director matches them)
        if c[0] == 4: src, af, rate = wav_of(c), "anull", ["-ar", "20000"]
        elif c[0] == 0: src, af, rate = T + "wav/1_%d.wav" % c[1], TRIM, []
        # (Legends' lines: evened out to sound like WWE '13's -- its EQ and level -- and at its rate)
        else: src, af, rate = L + "mp3/%04d.mp3" % c[1], LOWM_AF + "," + TRIM, ["-ar", "20000"]
        p = subprocess.Popen(["ffmpeg", "-nostdin", "-loglevel", "error", "-y", "-i", src,
                              "-ac", "1", "-af", af] + rate + ["-c:a", "libvorbis", "-q:a", "0", ogg_of(c)])
        procs.append(p)
        if len(procs) >= 3:
            for q in procs: q.wait()
            procs = []
    for q in procs: q.wait()
    for c in clips: data[c] = open(ogg_of(c), "rb").read()
    return data

def ms_of(c):
    import wave
    w = wave.open(wav_of(c) if c[0] >= 2 else T + "w16/1_%d.wav" % c[1] if c[0] == 0 else L + "w16/%04d.wav" % c[1])
    return int(1000 * w.getnframes() / w.getframerate())

if __name__ == "__main__":
    rows, clips = main()
    if "--dry" in sys.argv: sys.exit()
    data = encode(clips)
    order = {c: i for i, c in enumerate(clips)}
    # the index: one row per line; rows of the same clip share its data
    idx = []
    for w, ev, i, sex, t, mk, src, spk in rows:
        # (Legends' talk-over-the-quiet lines: fewer of them than WWE '13's, half as likely each)
        wt = "0.5" if src == 1 and ev == "filler" else "1"
        idx.append("%d\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s\t%s\t%s" % (order[w], ev, ("v%x" % (i & 0xFFFF) if i & 0x10000 else "%x" % i) if i >= 0 else "*" if i == -2 else "v*" if i == -3 else "-", sex, ms_of(w), wt, mk,
                                                        src, spk, SPLICE.get(w, "-"), t.replace("\t", " ")))
    vnames, _ = pickle.load(open(nmp("/home/devbox/work/nmport/names.pkl"), "rb"))
    idx.append("#moves")
    for k, nm in sorted(vnames.items()):
        if k % 10 == 0 and nm and k < 65530: idx.append("%d\t%s" % (k // 10, nm.lower()))
    index = ("\n".join(idx) + "\n").encode()
    head = 28
    table_off = head + len(index); table_off += -table_off % 4
    blob = bytearray(b"NMCP" + struct.pack("<IIIIII", 4, len(clips), len(rows), head, len(index), table_off))
    blob += index; blob += b"\0" * (table_off - len(blob))
    tab = bytearray(); body = bytearray(); base = table_off + 8 * len(clips)
    for c in clips:
        tab += struct.pack("<II", base + len(body), len(data[c])); body += data[c]
    blob += tab + body
    parts = [blob[i:i + PART] for i in range(0, len(blob), PART)]
    for k, p in enumerate(parts):
        open(OUT + ("" if k == 0 else ".%d" % k), "wb").write(p)
    k = len(parts)
    while os.path.exists(OUT + ".%d" % k): os.remove(OUT + ".%d" % k); k += 1
    print("pack", len(blob), "bytes in", len(parts), "parts; sha1", hashlib.sha1(blob).hexdigest())
