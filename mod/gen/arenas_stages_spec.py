"""gen/arenas_stages.py's venues: how each one's close set goes in at its match set's place (the SPEC fields are
described there; data/arenaport/allstages: the offline looks)."""
SPEC = {}

def grid(*pals, W=64, H=64):
    """a pack of these pictures in a W x H one, each a cell of a grid (2 stacked; 4: 2 x 2; 8: 2 x 4; 16: 4 x 4)"""
    n = len(pals); cols, rows = (1, 1) if n == 1 else (1, 2) if n == 2 else (2, 2) if n <= 4 else (2, 4) if n <= 8 else (4, 4)
    w, h = W // cols, H // rows
    return (W, H, [(p, (i % cols) * w, (i // cols) * h, w, h) for i, p in enumerate(pals)], None)

# NO MERCY (venue 1): the close set at 0.92 x, 770 further along z (place 430 added; matched: the side screens'
# truss 0x585 <- 0xB28.., the screen's truss 0x58C <- 0xB40.., the screen 0x589 <- 0xB07, the arches 0x58A <- 0xB0B..,
# the doorway 0x587 <- 0xAED, the stage floor's front 0x584 <- 0xAF6 (z 790 <- 20), the floor before it 0x58D <- 0xAFB:
# within 2% of their sizes). Kept: the aisle 0x586 (after the floor before the stage: drawn over it, as it was) and the
# floor round the ring 0x582 (last); the close aisle's part on the stage kept (cut at the stage's front, z 789: the
# old aisle ends there); its rails and the fences by it (0xAF0 - 0xAF5) out.
SPEC[1] = dict(s=0.92, d=(0, 0, 770), z1=560,
    keep=[(0x586, 0x831, ("after", 0xAFB)), (0x582, 0x829, "end")],
    drop={0xAF0, 0xAF1, 0xAF2, 0xAF3, 0xAF4, 0xAF5,                          # (the aisle's rails, the fences by it)
          0xB16, 0xB18, 0xB1A, 0xB1C, 0xB1E, 0xB20, 0xB0A, 0xB0C, 0xB0E, 0xB10, 0xB12, 0xB14,   # (the arches' 2nd layer)
          0xAFE, 0xB00, 0xB02, 0xB04},                                      # (the logo's dark copy behind it)
    clip=[({0xAFC}, 2, ">=", 789)],
    front={0xE86, 0xE88, 0xE82, 0xE84, 0xE78},
    repic={0xE7C: 0xE7A},
    pack=[(64, 64, [(0xE6C, 0, 0, 32, 21), (0xE6A, 32, 0, 32, 21), (0xE68, 0, 21, 32, 21), (0xE66, 32, 21, 32, 21),
                    (0xE6E, 0, 42, 32, 22), (0xE64, 32, 42, 32, 22)], None),          # the floors
          (64, 64, [(0xE58, 0, 0, 32, 32), (0xE56, 32, 0, 32, 32), (0xE54, 0, 32, 32, 32), (0xE5A, 32, 32, 32, 32)], None),  # the walls, doorway
          (64, 64, [(0xE70, 0, 0, 64, 32), (0xE72, 0, 32, 64, 32)], None),        # the WWF logo: silver, red
          (64, 64, [(0xE80, 0, 0, 64, 32), (0xE7A, 0, 32, 32, 32), (0xE7E, 32, 32, 32, 32)], None)],   # the ledge, lights
    screen=0xB07)

# SMACKDOWN (venue 2): the close set's shell (a flat picture there too, in 8 pieces), the oval screen on it, its blue
# tube arches, the halo of lights over the stage at 0.97 x, 180 up (the match's deck is 180 high, the close scene's
# stage floor at 0) and 967 further along z (place 385 added; matched: the shell 0x594 / 0x592 x +-740, y 180..1260,
# z 1370 <- 0xB56 / 0xB51.. +-750, 0..1120, 30; the small screen 0x595 y 200..400 <- 0xB79 44..220). The close scene's
# stage stands 280 over its floor and reaches 370 further toward the ring than the match's: its floor is cut at the
# match deck's front (z 930) and its face and walkway / ramp left out -- the old deck's front face 0x599, the ramp 0x598
# and the floor round the ring 0x59A kept; the old sign beside the stage (0x596 / 0x597) kept (the close scene's big
# oval sign stands where the match's stands are).
SPEC[2] = dict(s=0.97, d=(0, 180, 967), z1=880,
    keep=[(0x599, 0x855, ("after", 0xB63)), (((0x596, 0x84F), (0x597, 0x851)), None, ("after", 0xB63)),
          (((0x58E, 0x841), (0x58F, 0x841)), None, "end"), (0x59A, 0x857, "end"), (0x598, 0x853, "end")],
    drop={0xB65, 0xB66, 0xB67, 0xB68} | set(range(0xB6A, 0xB78)) | set(range(0xB8F, 0xB96)) | set(range(0xB96, 0xBAC)),
    clip=[({0xB59, 0xB5A, 0xB5B, 0xB5C, 0xB5D, 0xB5E, 0xB5F, 0xB60, 0xB61, 0xB62, 0xB63, 0xB64}, 2, ">=", 930)],
    front={0xEB4},
    pack=[(64, 64, [(0xE8A, 0, 0, 32, 32), (0xE8C, 32, 0, 32, 32), (0xE8E, 0, 32, 32, 32), (0xE90, 32, 32, 32, 32)], None),   # the shell's middle
          (64, 64, [(0xE92, 0, 0, 32, 32), (0xE94, 32, 0, 32, 32), (0xE96, 0, 32, 32, 32), (0xE98, 32, 32, 32, 32)], None),   # its sides
          (64, 64, [(0xE9A, 0, 0, 32, 32), (0xE9C, 32, 0, 32, 32), (0xEA0, 0, 32, 32, 16), (0xE9E, 32, 32, 32, 16),
                    (0xEA2, 0, 48, 16, 16), (0xEA4, 16, 48, 16, 16), (0xEA6, 32, 48, 32, 16)], None),                       # the deck's floor
          (64, 64, [(0x84F, 0, 0, 32, 64), (0x851, 32, 0, 32, 64)], None)],                                             # the old sign
    alone={0xB79}, order={0xB79: 0xB7A},
    screen=0xB79)

# KING OF THE RING (venue 3): the close set at the match's size (1 x) and 380 further along z (matched: the backdrop
# 0x59B x -910..930, y 0..950, z 1110 <- 0xBAC the same at z 730; the screen 0x5A8 / 0x5A9 x +-430, y 910..1530 <- its
# truss 0xC10.. the same; the two rows of lights 0x59F / 0x5A0 z 740 / 540 <- 0xBBF.. 350..390 / 0xBCB.. 150..190).
# The letters, crown and swords stand 100-150 nearer the backdrop than the old flat ones. Kept: the old floor 0x59D
# (under the close floors, which end at z 1020, before the backdrop), the aisle 0x59E (the close floors cut at its
# end, z 70), the floor round the ring 0x59C; the close aisle's rails / fences (0xBAE - 0xBB3) out.
SPEC[3] = dict(s=1.0, d=(0, 0, 380), z1=40,
    keep=[(0x59E, 0x85F, ("after", 0xBB7)),
          (((0x59F, 0x863), (0x5A0, 0x861)), None, ("after", 0xBB7)), (0x59C, 0x85B, "end")],
    drop=set(range(0xBAE, 0xBB4)) | set(range(0xBBF, 0xBD7)) | {0xBE2, 0xBE3, 0xBE4, 0xBE5},
    clip=[({0xBB4, 0xBB5, 0xBB6, 0xBB7, 0xBB8}, 2, ">=", 70)],
    solid={0xECA, 0xEE8, 0xEEA},
    cos={0xEE8: 0.6, 0xEEA: 0.6, 0xECA: 0.45, 0xEEE: 0.45, 0xEF0: 0.45, 0xEEC: 0.45, 0xED8: 0.6, 0xEDA: 0.6},
    repic={0xEFA: 0xEF8, 0xEDA: 0xED8},
    pack=[(128, 32, [(0xEF8, 0, 0, 128, 16), (0xEF4, 0, 16, 128, 16)], None),                     # the screen's truss, the lights under it
          (64, 64, [(0xED6, 0, 0, 32, 32), (0xED4, 32, 0, 32, 64)], None),                                   # the floors
          (64, 64, [(0xECC, 0, 0, 64, 32), (0xEFC, 0, 32, 64, 32)], None),                                    # the doorway, its logo
          (128, 32, [(0x863, 0, 0, 128, 16), (0x861, 0, 16, 128, 16)], None),                     # the old rows of lights
          (64, 64, [(0xEEE, 0, 0, 64, 32), (0xEF0, 0, 32, 32, 32), (0xEEC, 32, 32, 32, 32)], None)],   # the crown, swords
    order={0xBB4: 0xBAC, 0xBB5: 0xBAC, 0xBB6: 0xBAC, 0xBB7: 0xBAC, 0xBB8: 0xBAC},
    screen=0xBF7)

# SUMMERSLAM (venue 4): the close set at the match's size, 150 further along z (place -325 added; matched: the backdrop
# 0x5B5.. x -730..630, y 0..950, z 870 <- 0xC72 - 0xC74 x +-700, 0..950, 635..775; the old set's two layers of
# flat cranes, z 570 / 290 <- the close scene's back two rows, 0xC46.. 515..705 / 0xC3E.. 215..425). Its third row, in
# front (z -115..135: on the old aisle, before the stage's front at 140), and the aisle's fences are left out: the
# old stage has two. The floors and side walls cut at the stage's front (z 140); the aisle 0x5AC and the floor round
# the ring 0x5AA kept.
SPEC[4] = dict(s=1.0, d=(0, 0, 150), z1=110,
    keep=[(0x5AC, 0x891, ("after", 0xC1E)), (0x5AA, 0x877, "end")],
    drop=set(range(0xC21, 0xC35)) | {0xC5B, 0xC5C, 0xC6A, 0xC64, 0xC69, 0xC63} | set(range(0xC15, 0xC1B)),
    clip=[({0xC20, 0xC1F, 0xC1E, 0xC1D, 0xC1C, 0xC1B, 0xC76, 0xC77}, 2, ">=", 140)],
    cos={0xF16: 0.5, 0xF18: 0.4}, order={0xC1F: 0xC20, 0xC1E: 0xC20},
    pack=[(64, 64, [(0xF04, 0, 0, 32, 64), (0xF06, 32, 0, 32, 64)], None),                                  # the floors
          (64, 64, [(0xF16, 0, 0, 32, 32), (0xF18, 32, 0, 32, 32), (0xF14, 0, 32, 32, 32), (0xF1C, 32, 32, 32, 16)], None),   # wheels, lamps
          (64, 64, [(0xF12, 0, 0, 64, 32), (0xF0E, 0, 32, 64, 32)], None)],                                # the cranes' bodies, bases
    screen=None)

# SURVIVOR SERIES (venue 5): the close set at the match's size, 80 further along z (place -255, then 335: matched:
# the old set's three flat hexagon frames, z 140 / 530 / 910, x +-530, y 0..850 <- the close scene's three 3D ones,
# 0xC86.. / 0xC8B.. / 0xC90.. z 30..100 / 420..490 / 800..870, +-480, 0..830; the stage floor's front z 70 <- 0xC7E..
# z 0). The close floors' part before the stage (its aisle, z < 80: 0xC81 - 0xC83), the aisle's fences and the two
# lights by it left out; the aisle 0x5BA and the floor round the ring 0x5BB kept. The round screen (0xCBF, the
# animated piece's first frame): no Titantron in the match (the match's set had no screen of its own -- tron.c's rows
# for it are the close-up scene's animated ones, which match only there).
SPEC[5] = dict(s=1.0, d=(0, 0, 335), z1=40,
    keep=[(0x5BA, 0x8A9, ("after", 0xC80)), (0x5BB, 0x895, "end")],
    drop={0xC81, 0xC82, 0xC83, 0xCA9, 0xCAF} | set(range(0xC78, 0xC7E)) | set(range(0xCAA, 0xCAF)) | set(range(0xCB0, 0xCB5))
         | set(range(0xCB5, 0xCBA)),
    bands=(350, 750),
    repic={0xF4A: 0xF40, 0xF48: 0xF40, 0xF46: 0xF40, 0xF44: 0xF40, 0xF42: 0xF40, 0xF3E: 0xF40, 0xF3C: 0xF40, 0xF3A: 0xF40},
    pack=[(64, 64, [(0xF2E, 0, 0, 32, 64), (0xF30, 32, 0, 32, 64)], None),                                  # the floors
          (64, 64, [(0xF40, 0, 0, 64, 32), (0xF52, 0, 32, 32, 32)], None),                                  # the frames' lattice, side panels
          (64, 64, [(0xF34, 0, 0, 64, 32), (0xF38, 0, 32, 32, 32), (0xF54, 32, 32, 16, 16),
                    (0xF50, 32, 48, 32, 8), (0xF4E, 32, 56, 32, 8)], None)],                                # emblem, lamps, tubes
    screen=None)

# ROYAL RUMBLE (venue 6): the close set at 1.1 x, 391 further along z (place 410 added; matched: the garage door
# 0x5C5 - 0x5C8 x +-240, y 0..400, z 930 <- 0xCE5 - 0xCE8 +-200, 0..370, 80; the taxi over it 0x5C9 y 370..710,
# z 810..1000 <- the 3D taxi, the extra list 0xCFA.. (its offset 0, 480, 300)). The close scene's crowd stands over and
# behind the door (the old set's three low rows beside the road, z 400..890, aren't in it), its alley walls along the
# road cut where the old rows began (z 400); its road 0xCEB out -- the old road 0x5C4 and the floor round the ring
# 0x5CF kept. 35 pictures: the garage door's two and the taxi's sign (0xF82) as they are, the others in 64 x 64s
# (32 x 32 each); two dark slivers of posts at the cut (0xCED / 0xCEE) out.
SPEC[6] = dict(s=1.1, d=(0, 0, 391), z1=350,
    keep=[(0x5C4, 0x8AB, "start"), (0x5CF, 0x8BF, "end")],
    drop={0xCEB, 0xCED, 0xCEE},
    clip=[("all", 2, ">=", 400)],
    pack=[grid(0xF6E, 0xF6C, 0xF72, 0xF70),                                   # the crowd's rows
          (64, 64, [(0xF88, 0, 0, 64, 32), (0xF78, 0, 32, 32, 32), (0xF74, 32, 32, 32, 16), (0xF7C, 32, 48, 16, 8)], None),  # the garage's frame, lights, rail
          grid(0xF8C, 0xF8E, 0xF92, 0xF94),                                  # the taxi, its sign:
          (64, 64, [(0xF90, 0, 0, 64, 32), (0xF96, 0, 32, 32, 32), (0xF98, 32, 32, 32, 16), (0xFCA, 32, 48, 16, 16)], None),
          (64, 64, [(0xFA0, 0, 0, 32, 32), (0xFBC, 32, 0, 32, 32), (0xF7A, 0, 32, 32, 16), (0xF9C, 0, 48, 32, 16),
                    (0xFBA, 32, 32, 32, 32)], None),                                   # the alley: bins, posts, ...
          grid(0xFAE, 0xFB0, 0xFB2, 0xFB4), grid(0xFC2, 0xFC4, 0xFC6, 0xFC8)],  # ... its walls' pictures
    screen=None)

# WRESTLEMANIA (venue 7; also the base venue of the arcade's Revenge arenas: arena.c writes its entries only while
# WrestleMania itself is the arena): the close set at 0.95 x, 795 further along z (place 310 added; matched: the
# screen 0x5D6 / 0x5D7 x +-290, y 0..390, z 1160 <- 0xD50 / 0xD51 +-300, 0..410, 70; the side screens 0x5D3 x
# 530..1020, y 100..290, z 740..1020 <- 0xD42 560..1080, 110..310, -370..-70; the back wall 0x5D8 +-1200, 0..380,
# z 790..1270 <- 0xD52 +-1260, 0..400, -320..180; the stage floor 0x5D2 z 470.. <- 0xD35 z -650..: all within 1-2%).
# The close aisle's part on the stage kept (cut at the stage floor's front, z 470), the old aisle 0x5D1 over it as
# it was, the floor round the ring 0x5D0 kept; the aisle's rails (0xD2A - 0xD2F) out.
SPEC[7] = dict(s=0.95, d=(0, 0, 795), z1=440,
    keep=[(0x5D1, 0x8CF, ("after", 0xD35)), (0x5D0, 0x8C1, "end")],
    drop=set(range(0xD2A, 0xD30)),
    clip=[({0xD34}, 2, ">=", 470)],
    repic={0xFD4: 0xFD6},
    pack=[grid(0xFCE, 0xFCC, 0xFD2, 0xFD0),                                   # the crowd over the back wall
          grid(0xFE8, 0xFE6, 0xFE4, 0xFE2),                                   # the floors, the back wall
          (64, 32, [(0xFEE, 0, 0, 32, 32), (0xFF0, 32, 0, 32, 32)], None)],   # under the screen, the lights (the side
                                                                                # screens 0xFF4 their own: one palette with
                                                                                # these lost their red, QA 2026-10-31)
    alone={0xD50}, order={0xD51: 0xD42},                                    # (the side screens over their truss)
    screen=0xD50)

# BACKLASH (venue 8): the close set at the match's size, 575 further along z (matched: the truss over the stage
# 0x5E2 x -410..420, y 870..900, z 650..1200 <- 0xDBA.. +-420, 860..890, 60..640; the curved arms' three flat layers
# 0x5DD / 0x5DF / 0x5E1 z 740 / 900 / 1050 <- the close scene's three, 0xD72.. / 0xD82.. / 0xD96.. z 160..220 /
# 320..380 / 470..530). Its big screen (0xDB9 +-300, y 450..790) stands where the old one's picture (+-430, its truss
# painted round it) was, in its own 3D truss (+-360). The close floors cut at the aisle's end (z 70); the old stage
# floor 0x5DA under them (wider: +-750), the aisle 0x5DB and the floor round the ring 0x5D9 kept; the aisle's rails /
# fences (0xD57 - 0xD5C) out. The arms' and the screen's truss: their faces toward the ring; the arms' six lattice
# pictures one (0x1024's), each layer its own parts (bands: drawn back to front).
SPEC[8] = dict(s=1.0, d=(0, 0, 575), z1=40,
    keep=[(0x5DA, 0x8EB, "start"), (0x5DB, 0x8E9, ("after", 0xD63)), (0x5D9, 0x8D1, "end")],
    drop=set(range(0xD57, 0xD5D)),
    clip=[({0xD5E, 0xD5F, 0xD60, 0xD61, 0xD62, 0xD63}, 2, ">=", 70)],
    front={0x1014, 0x102A, 0x1024, 0x1022, 0x1028, 0x1026, 0x1016},
    repic={0x1014: 0x1024, 0x102A: 0x1024, 0x1022: 0x1024, 0x1028: 0x1024, 0x1026: 0x1024},
    bands=(850, 1000), band_pals={0x1024},
    pack=[(64, 64, [(0x101E, 0, 0, 32, 64), (0x1020, 32, 0, 32, 32), (0x1036, 32, 32, 32, 32)], None),   # floors, back wall
          (64, 64, [(0x102E, 0, 0, 64, 32), (0x102C, 0, 32, 64, 32)], None)],                           # the lamps
    alone={0xDB9},
    screen=0xDB9)

# ARMAGEDDON (venue 9): the close set at 0.92 x, 155 further along z (matched: the curtain 0x5E7 x +-320, y 300..770,
# z 1180..1260 <- 0xDC0 -360..350, 330..860, 1110..1200; the back panel 0x5E8 -1090..1070, 0..310, z 1150 <- 0xDC1
# -1370..920, 0..340, 1080; the sign 0x5EF +-185, 240..380, 1140 <- 0xDF0 +-205, 270..420, 1070; the tower 0x5F0,
# the tank 0x5F1, the sandbags 0x5EE <- 0xDF1.., 0xDF9.., 0xDE1..). The close scene has no big screen: the old one
# (0x5F4, tron.c's row) and its see-through truss (0x5F2) kept where they were, over the curtain; the close floors
# cut at the stage's front (z 180), its aisle (0xDCD / 0xDD0 / 0xDD1), the aisle's fences and the crates by its edge
# (0xDC2 - 0xDC4) out -- the old aisle 0x5EC and the floor round the ring 0x5EA kept.
SPEC[9] = dict(s=0.92, d=(0, 0, 155), z1=150,
    keep=[(0x5EC, 0x8F7, ("after", 0xDD2)), (0x5F2, 0x905, ("after", 0xDF0)), (0x5F4, 0x903, ("after", 0xDF0)),
          (0x5EA, 0x8F3, "end")],
    drop={0xDCD, 0xDD0, 0xDD1, 0xDC2, 0xDC3, 0xDC4} | set(range(0xDC7, 0xDCD)),
    clip=[({0xDCE, 0xDCF, 0xDD2}, 2, ">=", 180)],
    cos={"all": 0.35},
    pack=[(64, 64, [(0x1092, 0, 0, 32, 32), (0x1084, 32, 0, 32, 32), (0x107C, 0, 32, 64, 32)], None),   # curtain, back, sign
          grid(0x1054, 0x1056, 0x1058, 0x104C),                               # the floors, tracks
          grid(0x1082, 0x1080, 0x107E, 0x107A),                               # the tower, its lights
          grid(0x1090, 0x108C, 0x108E, 0x1066),                               # the tank
          grid(0x1070, 0x109C, 0x109E, 0x10A0, 0x1098, 0x1094, 0x1096, 0x109A, 0x1088, 0x105A, 0x1086, 0x104A),   # the vehicles
          grid(0x106C, 0x106E, 0x1072, 0x1074, 0x1076, 0x1078, 0x105C, 0x105E, 0x1060, 0x1062, 0x1064, 0x1068,
               0x106A, 0x108A)],                                              # the sandbags, the helicopter
    screen=None)

# ---- 2026-11-01: the stage set's parts are src/arena.c's (ARENA_SET_MAX: the game's array 0x8007B8A0 had room for 15;
# mod/NOTES.md "Arenas restructured") -- what the 15 places cut, back:
#   - every picture its own again (the game's file as it is): no packs (four to a 64 x 64, one median-cut palette), no
#     repic (a lattice drawn with another's picture), no solid (a one-colour picture made the vertex colours) -- but
#     SmackDown's old sign, its two halves one picture (tron_panels_all.h's TP_MASK row for it is that picture's);
#   - the trusses / lattices whole (the faces any match camera sees, as every other piece: no "front only");
#   - NO MERCY: the arches' second layer and the logo's dark copy behind it; SMACKDOWN: the close halo of lights over
#     the stage and its lamps (1300 triangles: the old flat halo stood in for it) and the four lamps by the shell
#     (not the two far out in the stands); KING OF THE RING: the close rows of lights (24 truss pieces: the old flat
#     rows stood in for them) and the small floor lights; SURVIVOR SERIES: the disc's second ring and the floor
#     lights by the sides. (Left out as before, by design: aisles, ramps, rails / fences on the old aisle, pieces in
#     the stands.)
for _v, _sp in SPEC.items():
    for _k in ("repic", "solid", "front"): _sp.pop(_k, None)
    _sp["pack"] = [p for p in _sp.get("pack", ()) if _v == 2 and p[2][0][0] == 0x84F]
SPEC[1]["drop"] -= {0xB16, 0xB18, 0xB1A, 0xB1C, 0xB1E, 0xB20, 0xB0A, 0xB0C, 0xB0E, 0xB10, 0xB12, 0xB14, 0xAFE, 0xB00, 0xB02, 0xB04}
SPEC[2]["drop"] -= set(range(0xB96, 0xBAC)) | {0xB8F, 0xB90, 0xB91, 0xB92, 0xB93}
SPEC[2]["keep"] = [k for k in SPEC[2]["keep"] if not (isinstance(k[0], tuple) and k[0][0][0] == 0x58E)]   # (the old halo)
SPEC[3]["drop"] -= set(range(0xBBF, 0xBD7)) | {0xBE2, 0xBE3, 0xBE4, 0xBE5}
SPEC[3]["keep"] = [k for k in SPEC[3]["keep"] if not (isinstance(k[0], tuple) and k[0][0][0] == 0x59F)]   # (the old rows)
SPEC[5]["drop"] -= set(range(0xCB5, 0xCBA)) | set(range(0xCAA, 0xCAF)) | set(range(0xCB0, 0xCB5))
SPEC[8]["band_pals"] = {0x1014, 0x102A, 0x1024, 0x1022, 0x1028, 0x1026}   # (the arms' lattice pictures, each its own now)
# SURVIVOR SERIES' Titantron (arenas_stages.py NEW, 2026-11-01): the new screen a part of its own (tron.c finds it by its
# extent: XSTAGE_SCREEN5_*)
SPEC[5]["alone"] = {0x7F00}
SPEC[5]["screen"] = 0x7F00
