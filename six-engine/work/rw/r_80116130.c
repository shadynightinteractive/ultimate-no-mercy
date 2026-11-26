/* func_80116130 (ovl_e): the camera's slot mask, u16. The original packs, for 4 slots:
 *   bit i (0x1 << i)  slot i in play (+0xA0 != -1, +0x94 bit 0 clear) and in the camera's view (+0xE9 & 0x10)
 *   0x10 / 0x20       flags: one in view has +0x80 & 0xC0 / +0x82 & 4
 *   0x40 << i         slot i out of play (+0x94 bit 0)
 * For 6 slots the layout is: bits 0-5 slots, 0x40 << i out of play (0xFC0), flags 0x1000 / 0x2000; the slots past
 * the match's count are out of play (so that `(m ^ 0xFC0) >> 6` -- the consumers' "in play" -- has no bits past
 * the count, as the original's `(m ^ 0x3C0) >> 6` with 4). The consumers' masks: build_rom.MASK_IMMS.
 * Past six (2026-11-06, patch_cam_wide.py): the u16 has slots 0-5 only (the HUD and the tag-type matches read it), and
 * the camera's per-slot readers take two 32-bit sets kept here: rw_cam_view (in play, in view) and rw_cam_play (not
 * out of play -- an empty slot counts, as in the old `(m ^ 0xFC0) >> 6`). */
#include "six.h"
u32 rw_cam_view, rw_cam_play;

REWRITE(80116130, u16, (void))
{
    s32 n = nslots(), i;
    u32 m = 0, view = 0, play = 0;
    for (i = 0; i < n; i++) {
        u8 *w = WR(i);
        if (*(volatile s16 *)(w + 0xA0) == -1) { play |= 1u << i; continue; }
        if (V16(w + 0x94) & 1) { if (i < 6) m |= 0x40u << i; continue; }
        play |= 1u << i;
        if (!(V8(w + 0xE9) & 0x10)) continue;
        view |= 1u << i;
        if (i < 6) m |= 1u << i;
        if (V16(w + 0x80) & 0xC0) m |= 0x1000;
        if (V16(w + 0x82) & 4) m |= 0x2000;
    }
    for (; i < 6; i++) m |= 0x40u << i;
    rw_cam_view = view;
    rw_cam_play = play;
    return (u16)m;
}
