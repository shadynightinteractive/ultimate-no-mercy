#!/usr/bin/env python3
"""The commentary's move classes (src/comm_moves.h): what kind of move each of the game's moves is, from its
name (nmport/names.pkl: the game's own move names, by move number; a wrestler's current move is +0xD6, the
number / 10 the move, the last digit a variant).

  STRIKE      punches, kicks, chops, stomps            STRIKE_BIG  clothesline, big boot, dropkick, spear...
  GRAPPLE     the lighter takedowns and throws         BIG_MOVE    suplexes, slams, DDTs, bombs, drivers...
  SUBMISSION  holds and locks                          AERIAL      off the top / second rope, dives
  TAUNT       showboating                              REVERSAL    counters
  FINISHER    the Superstars' own finishers by name    DROP        elbow / leg / knee drops on a man down
  OUTSIDE     moves that end up (or start) on the floor
"""
import os as _os, sys as _sys; _sys.path.append(next(d for d in (_os.path.abspath(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), *[".."] * i)) for i in range(8)) if _os.path.exists(_os.path.join(d, "nmpaths.py"))))
from nmpaths import nmp
import pickle, re
W = nmp("/home/devbox/work/")
OUT = W + "arcade/src/comm_moves.h"
vnames, _ = pickle.load(open(W + "nmport/names.pkl", "rb"))

CLASSES = ["NONE", "STRIKE", "STRIKE_BIG", "GRAPPLE", "BIG_MOVE", "SUBMISSION", "AERIAL", "TAUNT", "REVERSAL",
           "FINISHER", "DROP", "OUTSIDE"]
C = {n: i for i, n in enumerate(CLASSES)}

FINISHERS = {
    "Stone Cold Stunner", "Rockbottom", "People's Elbow", "Pedigree", "Tombstone Piledriver", "Last Ride",
    "Chokeslam from Hell", "Huge Chokeslam", "Mandible Claw", "Sweet Chin Music", "Sharpshooter",
    "Crippler Crossface", "Walls of Jericho", "X Factor", "Twist of Fate", "Diamond Dust", "Dominator",
    "Doomsday Device", "Shake Rattle and Roll", "Rikishi Banzai Drop", "Rikishi Driver", "Olympic Slam",
    "Ankle Lock", "Tazzmission", "Rings of Saturn", "Death Valley Driver", "Impaler", "Fire Thunder Driver",
    "Emerald Fusion", "Burning Hammer", "The Morality Check", "Mac Stunner", "FameAsser", "Spinning Torture Rack",
    "Boss Man Slam", "Bradshaw Hammer", "Downward Spiral", "Rios Driver", "Six Seconds Magic", "Sky High",
    "Pump Handle Slam", "Kohya-Otoshi", "Insider Edge", "Austin Elbow Drop", "Vince Elbow Drop",
    "Figure 4 Leg Lock", "Jack Hammer", "Faarooq Spinebuster", "Rock Spinebuster", "Spiral Bomb",
    "Pimp Lariat", "Buchanan Stomp", "Undertaker's Pin", "Tiger Driver", "Shouda 01", "Tazzplex",
    "Senton Splash 03", "Swinging Chokeslam", "Superkick",
}
TAUNT_NAMES = {"Angle", "Austin", "Benoit", "Brisco", "Buchanan", "Buh Buh", "Cactus", "Christian", "Chyna",
               "Crash", "D'lo", "D-Von", "DX", "Debra", "Dude", "Dudleyz", "Edge", "Edge & Christian",
               "GENERIC", "Gangrel", "Godfather", "Guerrero", "HBK", "Hardcore", "Hardyz", "Hebner", "Jeff",
               "Jericho", "Kane", "Linda", "Lita", "Malenko", "Mankind", "Mr. Ass", "Patterson", "Radicalz",
               "Rikishi", "Rios", "Road Dogg", "Rock", "Saturn", "Scotty", "Sexay", "Shamrock", "Shane", "Taka",
               "Tazz", "Terri", "Too Cool", "Triple H", "Undertaker", "Venis", "Vince", "Women", "X-Pac",
               "PAngle", "PCartwheel"}

def cls(name):
    if not name or name in ("None", "NONE", "'wA", "fAl", "RUN", "Roll", "Throw"): return "NONE"
    base = re.sub(r" ?\d+$", "", name).strip()
    if re.match(r"^[0R]?Taunt", name) or base in TAUNT_NAMES: return "TAUNT"
    if name in FINISHERS or base in FINISHERS: return "FINISHER"
    n = name.lower()
    if "counter" in n or "reversal" in n: return "REVERSAL"
    if "to outside" in n or "suicide dive" in n or "baseball slide" in n or "through ropes" in n or "over ropes" in n:
        return "OUTSIDE"
    if re.search(r"moonsault|diving|3rd rope|2nd rope|missile|senton|flying body press|springboa|asai|praying|"
                 r"super (hurricane|powerbomb|snap|back drop|dragon|shoulder|knee)|corkscrew|tumbling|vaulting|"
                 r"slingshot|climb up|old man flop|flip splash|backflip splash", n): return "AERIAL"
    if re.search(r"lock|hold|clutch|crab|sleeper|stretch|claw|crossface|choke$|naked choke|guillotine choke|"
                 r"deathlock|cloverleaf|^stf$|bearhug|neck crank|neck wrench|triangle|armbar|wishbone|"
                 r"face twist|mission|chin breaker|strangle", n): return "SUBMISSION"
    if re.search(r"elbow drop|leg drop|knee drop|stomp|splash|headbutt to groin|groin knee|falling headbutt|"
                 r"jumping shoulder drop|hip press|body press drop", n): return "DROP"
    if re.search(r"suplex|slam|ddt|powerbomb|piledriver|driver|breaker|spinebuster|bulldog|facebuster|buster|"
                 r"sto\b|samoan|fallaway|jack ?hammer|tiger|falcon|electric chair|manhattan|snowplow|bomb|"
                 r"rana|torture rack|military press|back drop|neck drop|face crusher|jawbreaker|tazzplex|"
                 r"dragonsteiner|pancake|cannon ball|full nelson|fisherman|northern lights|t-bone|capture|"
                 r"chokeslam|choke lift|chokelift|gordbuster|ganso|kohya|screwdriver|tilt a whirl|impaler|"
                 r"spiral|neck breaker|hammer|rack em up|sky high|clinching|atomic drop", n): return "BIG_MOVE"
    if re.search(r"clothesline|lariat|big boot|yakuza|dropkick|spear|shoulder block|body attack|heel kick|"
                 r"wheel kick|roundhouse|running|jumping|flying|haymaker|axe kick|spinning|big kick|"
                 r"strong kick|super knee|knee attack|crescent|body tackle|hard attack|sliding attack|"
                 r"boot to face|bicycle|enzuigiri|cocky kick|censor kick|somersault|high kick|3 point|"
                 r"european uppercut|forearm smash|sumo attack|cyclone|burning combo|mu-ken", n): return "STRIKE_BIG"
    if re.search(r"punch|jab|kick|chop|slap|elbow|knee|stomp|uppercut|forearm|head ?butt|eye|club|thrust|rake|"
                 r"low blow|combination|blow|palm|strike|hook|gouge|smash|thump|mini|shin", n): return "STRIKE"
    if re.search(r"pin\b|school boy|small package|back ?slide|roll up|cradle|takedown|drag|toss|snapmare|"
                 r"sweep|flip|throw|monkey|mare|whip|headlock|rope|takedown|fireman|press", n): return "GRAPPLE"
    return "GRAPPLE"

table = [0] * 6554
seen = {}
for k, name in vnames.items():
    if k >= 65530 or k % 10: continue
    c = cls(name)
    table[k // 10] = C[c]
    seen.setdefault(c, []).append(name)
L = ["// generated by gen/mkcommmoves.py: the commentary's class of each of the game's moves (by move number / 10)"]
for i, n in enumerate(CLASSES): L.append("#define MC_%s %d" % (n, i))
L.append("#define COMM_MOVES_N %d" % len(table))
L.append("static const u8 comm_move_class[COMM_MOVES_N] = {")
for i in range(0, len(table), 40): L.append("    " + ",".join(str(x) for x in table[i:i + 40]) + ",")
L.append("};")
open(OUT, "w").write("\n".join(L) + "\n")
for c in CLASSES:
    v = seen.get(c, [])
    print(c, len(v), sorted(set(v))[:14])
