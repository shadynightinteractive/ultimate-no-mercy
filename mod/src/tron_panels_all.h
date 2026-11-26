// The arenas' screens (tron.c), all of No Mercy's that have one (data/tron_all/TRON_ALL.md: how they were found, and
// checked in the N64 rig's RAM -- each texture found as a material in a match / an entrance close-up at its venue, its
// fold there equal to the row's, its part's vertices inside the row's extent). Same struct as tron_panels.h: venue;
// the screen's extent in its model (x from gx0 to gx1, y from gy0 at the bottom to gy1 at the top: the whole picture,
// TRON_W x TRON_H, goes across all of it); the texture folded (sum, rotating xor); flags (the old "framed"):
//   1        framed: the arena's picture has a frame of its own painted round it (ours then gets it, panels_draw)
//   TP_TWIN  the picture is also at -x (x from -gx1 to -gx0), each copy the whole picture, read left to right
//   TP_ANIM  one frame of an animated piece of the entrances' close-up scene (its node's other frames: tron.c holds
//            the one found while ours shows)
//   TP_MASK  the arena's picture has see-through texels round its shape (an oval sign): ours is cut to that shape --
//            tron.c mask_compose: the piece's own picture, laid flat, gives our picture's alpha (the piece is drawn
//            with the arena's own render mode, which leaves see-through texels out)
// Two scenes per venue: the match's stage set (meshes 0x575.., the stage-set tables 0x800507FC..) and the entrances'
// close-up scene (venue + 10: tables 0x80052280.., shown in the close shots, entrance steps 5 / 11).
// Rows under TRON_PANEL_EXT need tron.c's changes (TRON_ALL.md section 4: the fold over the texture's own size, the
// extent check, 32 vertices, the twin and animated pieces); TRON_SIDE_SCREENS: the second screens (optional:
// Backlash's lower one).
// Which screen of an arena: the user's choice where there are several (2026-11-01, Titantron Placement QA\To Fix).
// No screen (left out): SUMMERSLAM (4: light cranes, a sign), ROYAL RUMBLE (6: the taxi, the garage), ARMAGEDDON's
// close-up scene, and the arcade's MONDAY NITRO, HALLOWEEN HAVOC (its crypts' "screens" are painted into their fronts).
// SURVIVOR SERIES' round disc keeps its own logo (the user's word): its screen is a Titantron of its own (2026-11-01).
#ifndef TP_TWIN
#define TP_TWIN 2
#define TP_ANIM 4
#define TP_MASK 8
#endif
static const TronPanel tron_panels[] = {
    // RAW IS WAR (venue 0): the big screen in the middle -- only it: the side screens (the fist, 0x0816 / 0x0818) keep
    // the arena's own picture. In the match it's three columns (x -500..-170, -170..170, 170..500) of two rows (y 340..600,
    // 600..870), its picture a quarter mirrored into all of them (2026-10-27: the panels' own S / T, scaled, showed
    // ours so -- in flipped tiles); up close (the entrances' model of it) one quad
    // (2026-10-31: the match's set is the close set now -- gen/arenas_raw.py, arena.c arena_raw_tables: its screen the
    // close one's 0xAA7 at 0.8 x, one quad with its own 3D truss round it; the two rows below then meet nothing)
    { 0, XRAW_SCREEN_X0, XRAW_SCREEN_X1, XRAW_SCREEN_Y0, XRAW_SCREEN_Y1, 0x83D84FF5u, 0x7DBC91F9u, 0 },   // match: the big screen (texture 0x0E33 = 0x081E's texels; was 0x57B's -460..460, 370..830)
    { 0, -500, 500, 340, 870, 0x6918BBE2u, 0x8AF2B28Du, 1 },   // 0x081C (the outer columns; the truss painted round its picture)
    { 0, -500, 500, 340, 870, 0x14213D2Fu, 0x1F492BE9u, 1 },   // 0x081A (the middle column)
    // (2026-10-31, the rig: the first is mesh 0x57B, texture 0x081E -- the logo picture behind the far screen's truss in
    // the match's stage set; 0x081A is mesh 0x57C, the outer columns, 0x081C mesh 0x57D, the middle; the truss pieces see-
    // through in the middle. The close-up scene's screen 0xAA7 has the same picture (0x0E33 = 0x081E's texels): without
    // the extent check the first row takes it too, its picture then laid over -460..460 of its -580..580)
#if TRON_PANEL_EXT
    { 0, -580, 580, 260, 840, 0x83D84FF5u, 0x7DBC91F9u, 0 },   // close-up: mesh 0xAA7, texture 0x0E33 (64x64) -- the close set's big screen (scene 10, the entrances' close shots: its own 3D truss round it)
#endif
    // RAW IS WAR WM 2000 (the arcade's arena 13 -- gen/arenas.py ARENAS' fourth: arena_current() 10 + 3): WrestleMania 2000's
    // 3D entrance set's big screen, in the match's stage set and in the close scene (gen/arenas_wm.py tron_rows: its mesh's
    // extent, its RAW IS WAR picture's fold -- regenerate these two rows if the set's placement changes)
    // (2026-11-01 evening, arenas-wm: generated -- src/arena_screen_files.h XWM_TRON_ROWS, every WrestleMania 2000 venue
    // with a screen: RAW IS WAR WM 2000 (13) and SUNDAY NIGHT HEAT, match and close-up, each its own picture's fold)
#ifdef XWM_TRON_ROWS
    XWM_TRON_ROWS
#endif
    // BASH AT THE BEACH (the arcade's arena 12): its picture board under the hut (Revenge's slideshow wall: scene 0x3C
    // models 13-16, a 2 x 2 wall of 64 x 32 CI8 photos on the diagonal by the boardwalk; gen/arenas_stage3d.py BOARD) --
    // four tiles, a part each, the picture once across the whole wall (each tile's row the wall's extent). The extents and
    // folds: src/arena_screen_files.h (gen/arenas_stages.py write_screens), regenerated with the sets.
#if TRON_PANEL_EXT && defined(XBOARD_ARENA)
    { XBOARD_ARENA, XBOARD_MATCH_X0, XBOARD_MATCH_X1, XBOARD_MATCH_Y0, XBOARD_MATCH_Y1, XBOARD_MATCH_SIG0_0, XBOARD_MATCH_SIG1_0, 0 },   // match: tile 1 (model 13)
    { XBOARD_ARENA, XBOARD_MATCH_X0, XBOARD_MATCH_X1, XBOARD_MATCH_Y0, XBOARD_MATCH_Y1, XBOARD_MATCH_SIG0_1, XBOARD_MATCH_SIG1_1, 0 },   //   ... tile 2 (model 14)
    { XBOARD_ARENA, XBOARD_MATCH_X0, XBOARD_MATCH_X1, XBOARD_MATCH_Y0, XBOARD_MATCH_Y1, XBOARD_MATCH_SIG0_2, XBOARD_MATCH_SIG1_2, 0 },   //   ... tile 3 (model 15)
    { XBOARD_ARENA, XBOARD_MATCH_X0, XBOARD_MATCH_X1, XBOARD_MATCH_Y0, XBOARD_MATCH_Y1, XBOARD_MATCH_SIG0_3, XBOARD_MATCH_SIG1_3, 0 },   //   ... tile 4 (model 16)
    { XBOARD_ARENA, XBOARD_CLOSE_X0, XBOARD_CLOSE_X1, XBOARD_CLOSE_Y0, XBOARD_CLOSE_Y1, XBOARD_CLOSE_SIG0_0, XBOARD_CLOSE_SIG1_0, 0 },   // close-up: tile 1
    { XBOARD_ARENA, XBOARD_CLOSE_X0, XBOARD_CLOSE_X1, XBOARD_CLOSE_Y0, XBOARD_CLOSE_Y1, XBOARD_CLOSE_SIG0_1, XBOARD_CLOSE_SIG1_1, 0 },   //   ... tile 2
    { XBOARD_ARENA, XBOARD_CLOSE_X0, XBOARD_CLOSE_X1, XBOARD_CLOSE_Y0, XBOARD_CLOSE_Y1, XBOARD_CLOSE_SIG0_2, XBOARD_CLOSE_SIG1_2, 0 },   //   ... tile 3
    { XBOARD_ARENA, XBOARD_CLOSE_X0, XBOARD_CLOSE_X1, XBOARD_CLOSE_Y0, XBOARD_CLOSE_Y1, XBOARD_CLOSE_SIG0_3, XBOARD_CLOSE_SIG1_3, 0 },   //   ... tile 4
#endif
    // SURVIVOR SERIES (venue 5): its own Titantron over the stage (gen/arenas_stages.py NEW: a screen in a truss frame of
    // the arena's lattice beam, the emblem its picture until ours shows) -- the round disc keeps its logo, no rows.
#if TRON_PANEL_EXT && defined(XSCREEN5_SIG0)
    { 5, XSTAGE_SCREEN5_X0, XSTAGE_SCREEN5_X1, XSTAGE_SCREEN5_Y0, XSTAGE_SCREEN5_Y1, XSCREEN5_SIG0, XSCREEN5_SIG1, 0 },   // match: the new screen (texture 0x0F35)
    { 5, XSCREEN5_CLOSE_X0, XSCREEN5_CLOSE_X1, XSCREEN5_CLOSE_Y0, XSCREEN5_CLOSE_Y1, XSCREEN5_SIG0, XSCREEN5_SIG1, 0 },   // close-up: the same, in its close scene's extra list
#endif
    // NO MERCY (venue 1)
#if TRON_PANEL_EXT
    // (2026-10-31: the match's set is the close set now -- gen/arenas_stages.py, arena.c arena_stage_tables: its screen the
    // close one's 0xB07 at 0.92 x, its 3D truss round it; was mesh 0x589, texture 0x0838, -520..520, 240..750, EEEEEE00 / 0)
    { 1, XSTAGE_SCREEN1_X0, XSTAGE_SCREEN1_X1, XSTAGE_SCREEN1_Y0, XSTAGE_SCREEN1_Y1, 0x00000000u, 0x00000000u, 0 },   // match: texture 0x0E75 (64x64) -- the close set's big screen (blank); texture all colour 0
    { 1, -550, 550, 260, 840, 0x00000000u, 0x00000000u, 0 },   // close-up: mesh 0xB07, texture 0x0E75 (64x64) -- the close set's big screen (blank); texture all colour 0
#endif
    // SMACKDOWN (venue 2): the "SMACKDOWN" oval sign at the right of the shell (the user, 2026-11-01: not the oval
    // doorway in the shell's mouth -- mesh 0xB79, the close set's animated logo, and its copy in the match's set, which
    // show the arena's own picture again). One sign, no twin on the left (the old set has only the one at -x, the close
    // set's extra list only the one pair of halves). The picture cut to the oval (TP_MASK): the sign is a flat quad in
    // the match (its oval is the texture's see-through corners), an oval fan up close.
#if TRON_PANEL_EXT
    // match: the old set's sign kept in the new set (gen/arenas_stages.py SPEC[2] keep: meshes 0x596 + 0x597 made one
    // part, 12 points, x -1240..-740 (angled: z 500..1368), y 200..700; textures 0x0850 / 0x0852 packed into one 64x64
    // CI4, colour 0 see-through). Extent and fold: data/tron_all/stage_rows.py -- rerun it if that part changes.
    { 2, -1240, -740, 200, 700, 0xDDFA708Au, 0x7F5CA24Eu, TP_MASK },   // match: the oval sign (old 0x596 + 0x597, its packed 64x64 picture)
    // close-up: the close set's big sign, its extra list (gen/arenas_stages_tab.py CLOSE[2] x1): two halves of one oval,
    // in the plane x = z, each its own 64x64 CI4 (colour 15 see-through) -- one extent for both, as KOTR's halves
    { 2, -450, 450, 40, 800, 0xB8CFCE55u, 0x622290CFu, TP_MASK },   // close-up: mesh 0xB6A, texture 0x0EAF (64x64) -- the sign's +x half
    { 2, -450, 450, 40, 800, 0xC02D4DB7u, 0x451C4DC4u, TP_MASK },   // close-up: mesh 0xB6B, texture 0x0EB1 (64x64) -- its -x half
#endif
    // KING OF THE RING (venue 3)
    // (2026-10-31: the match's set is the close set now, at the match's size: its screen 0xBF7 with its 3D truss; was
    // meshes 0x5A8 / 0x5A9, textures 0x0874 / 0x0876, framed: 84CC199E / C8051309, C966AA84 / 25BC11E1)
    { 3, XSTAGE_SCREEN3_X0, XSTAGE_SCREEN3_X1, XSTAGE_SCREEN3_Y0, XSTAGE_SCREEN3_Y1, 0xB65A1FE9u, 0x4D7292E1u, 0 },   // match: texture 0x0EF7 (64x64) -- the close set's big screen
    { 3, -360, 360, 1020, 1460, 0xB65A1FE9u, 0x4D7292E1u, 0 },   // close-up: mesh 0xBF7, texture 0x0EF7 (64x64) -- the close set's big screen (high: y 1020..1460, its 3D truss round it)
    // SURVIVOR SERIES (venue 5): none for now (the user, 2026-11-01: the round disc in the hexagon -- mesh 0xCBF,
    // the close set's animated logo, textures 0x0F57..0x0F6B, and its first frame in the match's set -- stays the
    // arena's own logo, in the close shots too; its own Titantron over the stage is to come as a new piece of the set).
    // WRESTLEMANIA (venue 7): the two big side screens on their truss over the crowd, left and right (the user,
    // 2026-11-01: not the panel in the middle at the ramp's end -- mesh 0xD50, texture 0x0FED, and its copy in the
    // match's set, which show the arena's own picture again). A mirrored pair: one TP_TWIN row, each the whole picture.
    // (WrestleMania's own set only: the arcade's arenas in venue 7 are arena_current() 10.., no rows.)
#if TRON_PANEL_EXT
    // match: the close set's side screens in the match's set (gen/arenas_stages.py SPEC[7]: 0xD42 + 0xD43, their first
    // frame 0x0FF5 (64x32), made one part at 0.95 x: 18 points, |x| 528..1024, y 112..288 on its grid; data/tron_all/stage_rows.py).
    // First: its points are outside the close rows' extent and the close rows' outside its own, but a TP_ANIM row met
    // first would end the search (panel_patch drops a match that isn't the close scene's animated piece)
    { 7, 528, 1024, 112, 288, 0x4584A02Eu, 0xBB58908Au, TP_TWIN },   // match: the side screens (0xD42 + 0xD43 in one part, texture 0x0FF5)
    { 7, 560, 1080, 110, 310, 0x4584A02Eu, 0xBB58908Au, TP_TWIN | TP_ANIM },   // close-up: mesh 0xD42, texture 0x0FF5 (64x32) -- the close set's side screens (0xD42 right, 0xD43 left: animated, 16 frames, 64x32)
    { 7, 560, 1080, 110, 310, 0xC51E542Eu, 0x70FCFB76u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x0FF7
    { 7, 560, 1080, 110, 310, 0x35AD8741u, 0xC2BB043Eu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x0FF9
    { 7, 560, 1080, 110, 310, 0x9E3AD0A8u, 0x0993FEBFu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x0FFB
    { 7, 560, 1080, 110, 310, 0x0D261BFDu, 0x66EB1F24u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x0FFD
    { 7, 560, 1080, 110, 310, 0xDF53DB90u, 0x6159471Bu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x0FFF
    { 7, 560, 1080, 110, 310, 0xDB5FF378u, 0xBDF15082u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1001
    { 7, 560, 1080, 110, 310, 0xDB3DB392u, 0xEDCB1922u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1003
    { 7, 560, 1080, 110, 310, 0xA1B687C5u, 0x88D9456Fu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1005
    { 7, 560, 1080, 110, 310, 0xCC9116F4u, 0x6190A541u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1007
    { 7, 560, 1080, 110, 310, 0xA2EDD6A3u, 0x760EBF7Fu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1009
    { 7, 560, 1080, 110, 310, 0x58B28423u, 0xE9CBBDE5u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x100B
    { 7, 560, 1080, 110, 310, 0xA08D8F9Au, 0xE391E090u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x100D
    { 7, 560, 1080, 110, 310, 0x16C78556u, 0x2D2DA041u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x100F
    { 7, 560, 1080, 110, 310, 0x22A988E7u, 0xD878BD9Bu, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1011
    { 7, 560, 1080, 110, 310, 0xC2017F5Au, 0xF8360E48u, TP_TWIN | TP_ANIM },   //   ... frame texture 0x1013
#endif
    // (the old set's side screens, mesh 0x5D3, texture 0x08C8 64x32, x 530..1020 mirrored, y 100..290, 431FEEFA /
    // 4D8455BD: not in the match's set any more -- its close set is)
    // BACKLASH (venue 8)
#if TRON_PANEL_EXT
    // (2026-10-31: the match's set is the close set now, at the match's size: its upper screen 0xDB9 with its 3D truss;
    // was meshes 0x5E3 / 0x5E4, textures 0x08E6 / 0x08E8, framed: 628D5BB3 / C39B9027, 65F920D2 / 282BB463)
    { 8, XSTAGE_SCREEN8_X0, XSTAGE_SCREEN8_X1, XSTAGE_SCREEN8_Y0, XSTAGE_SCREEN8_Y1, 0x00000000u, 0x00000000u, 0 },   // match: texture 0x1033 (64x64) -- the close set's upper big screen (blank)
    { 8, -300, 300, 450, 790, 0x00000000u, 0x00000000u, 0 },   // close-up: mesh 0xDB9, texture 0x1033 (64x64) -- the close set's upper big screen (blank); texture all colour 0
#endif
#if TRON_PANEL_EXT && TRON_SIDE_SCREENS
    { 8, -430, 430, 0, 450, 0xA637DB7Bu, 0xE9795B81u, 1 },   // match: mesh 0x5E5, texture 0x08E2 (64x64) -- the lower screen, right half (the BACKLASH logo; truss painted all round)
    { 8, -430, 430, 0, 450, 0x9D605A69u, 0xEB495E65u, 1 },   // match: mesh 0x5E6, texture 0x08E4 (64x64) -- the lower screen, left half
    { 8, -300, 300, 60, 390, 0xF715BB34u, 0x050C4410u, TP_ANIM },   // close-up: mesh 0xDB8, texture 0x1039 (64x64) -- the close set's lower screen (animated BACKLASH logo, 17 frames / 8 pictures)
    { 8, -300, 300, 60, 390, 0x17E74816u, 0xAAE999CCu, TP_ANIM },   //   ... frame texture 0x103B
    { 8, -300, 300, 60, 390, 0x756AC376u, 0x9D58F0C9u, TP_ANIM },   //   ... frame texture 0x103D
    { 8, -300, 300, 60, 390, 0xB18A756Eu, 0x5824E977u, TP_ANIM },   //   ... frame texture 0x103F
    { 8, -300, 300, 60, 390, 0x79481124u, 0x693D088Du, TP_ANIM },   //   ... frame texture 0x1041
    { 8, -300, 300, 60, 390, 0xA6FC63DFu, 0x0408BF62u, TP_ANIM },   //   ... frame texture 0x1043
    { 8, -300, 300, 60, 390, 0xC7FB7BA2u, 0xE3048F33u, TP_ANIM },   //   ... frame texture 0x1045
    { 8, -300, 300, 60, 390, 0xAD96604Au, 0xB5ACA565u, TP_ANIM },   //   ... frame texture 0x1047
#endif
    // ARMAGEDDON (venue 9)
#if TRON_PANEL_EXT
    { 9, -420, 420, 740, 1100, 0x00000000u, 0x00000000u, 0 },   // match: mesh 0x5F4, texture 0x0904 (64x64) -- the big screen (blank; the truss 0x5F2 in front of it, see-through in the middle); texture all colour 0
#endif
};
