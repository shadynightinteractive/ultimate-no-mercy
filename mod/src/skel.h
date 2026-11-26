// A model's skeleton (skel.c): whose way its parts are joined.
#ifndef SKEL_H
#define SKEL_H
// A model's descriptor is 0x270 bytes; No Mercy uses none of 0x260-0x26B. A model from WrestleMania 2000
// keeps there what its own descriptor had past No Mercy's fields (gen/wmport.py makes these descriptors):
#define SKEL_WM_ATTACH 0x25A        // s16[3]: its attachments' codes (WM2000's 0x24A; No Mercy's own place)
#define SKEL_WM_PLACE  0x260        // s16: where its attachments sit (WM2000's 0x250)
#define SKEL_WM_HANDS  0x262        // u8: its hands' kind (WM2000's 0x80041CC4[the looks' first byte])
#define SKEL_WM_FLAGS  0x264        // u32: WM2000's 0x244 (head offsets)
#define SKEL_MAGIC_AT  0x268        // u32: SKEL_MAGIC_WM
#define SKEL_WM_REV   0x80000000u   // (in SKEL_WM_FLAGS) a WCW/nWo Revenge model (gen/wmport.py)
#define SKEL_WM_NMHANDS 0x40000000u // (in SKEL_WM_FLAGS) its hands No Mercy's (parts 9 10 17 18: hands.c draws them No Mercy's way)
#define SKEL_WM_HD 0x20000000u      // (in SKEL_WM_FLAGS) drawn whole, by src/hd.c
#define SKEL_WM_OWNHANDS 0x10000000u // (in SKEL_WM_FLAGS, with SKEL_WM_REV) its hands its own game's, as modelled: never
                             // WrestleMania 2000's pose meshes (a Revenge model made without WM2000: hands.c)
#define SKEL_WM_HD_MODEL 0x0F000000u // (and which man's pack: hd.c hd_models)
#define SKEL_WM_HD_MODEL_AT 24
#define SKEL_MAGIC_WM  0x574D324Bu  // "WM2K"
enum { SKEL_NM, SKEL_WM };
int skel_origin(u32 desc);
void skel_setup(u32 desc, s32 slot);
void skel_visible(s32 slot);
void skel_show(s32 slot, s32 flag);
void skel_textures(u32 desc, s32 slot, s32 phase);
void hands_frame(void);
void hands_wm_load(void);
void hands_wm_keep(s32 slot);
void pal_group_guard(u32 part, s32 tex, s32 pal, s32 at, s32 n, s32 shift, s32 z);   // func_8000CAF8 where the palette has room
#endif
