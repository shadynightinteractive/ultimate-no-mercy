// The arenas' screens (tron.c): venue; the screen's extent in its model (x from gx0 to gx1, y from gy0 at the bottom
// to gy1 at the top: the whole picture, TRON_W x TRON_H, goes across all of it); the texture folded (sum, rotating xor);
// framed: the arena's picture has a frame of its own painted round it (ours then gets it, tron.c panels_draw)
static const TronPanel tron_panels[] = {
    // RAW IS WAR (venue 0): the big screen in the middle -- only it: the side screens (the fist, 0x0816 / 0x0818) keep
    // the arena's own picture. In the match it's three columns (x -500..-170, -170..170, 170..500) of two rows (y 340..600,
    // 600..870), its picture a quarter mirrored into all of them (2026-10-27: the panels' own S / T, scaled, showed
    // ours so -- in flipped tiles); up close (the entrances' model of it) one quad
    { 0, -460, 460, 370, 830, 0x83D84FF5u, 0x7DBC91F9u, 0 },   // the big screen up close (one panel; the stage's own truss round it)
    { 0, -500, 500, 340, 870, 0x6918BBE2u, 0x8AF2B28Du, 1 },   // 0x081C (the outer columns; the truss painted round its picture)
    { 0, -500, 500, 340, 870, 0x14213D2Fu, 0x1F492BE9u, 1 },   // 0x081A (the middle column)
};
