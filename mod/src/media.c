// No Mercy Plus V3's entrance themes and Titantron videos, in the game's own music engine and video
// player. Their data is the content pack (build/content.bin, made by gen/mkcontent.py; where things
// are: content_index.h), read with plat_content_read.
//
// THEMES. A music slot's entry in the entrance table names a song; the song table says which sound of
// the music bank it plays; a sound is a short script that plays one instrument (a long ADPCM sample in
// the ROM, streamed through the game's audio DMA cache). No Mercy Plus re-did every theme and gave the
// empty slots (Big Show's among them) their own. Its instruments are added to the bank the game loaded
// (the instrument tables copied with them added), the sounds of songs 23..66 play its scripts, and the
// songs the game left without a sound get one (each song s has the sound s - 23, silent where empty).
// The samples aren't in the ROM: they're given ROM addresses past the cartridge's end (THEME_ROM) and
// the audio DMA callback (func_8002B0C0, replaced) reads that range from the pack.
//
// VIDEOS. Video v's frame list is the entrance table's word at 0x800F50CC + 12 v: (frame file, time)
// pairs. No Mercy Plus made every video 72 frames of footage. Each video's list here names the same 72
// of the game's own frame files (tv_carriers.h); as one is read while a video of ours is the one being
// shown, its picture is swapped for that video's frame from the pack. The sprite cache keeps frames by
// file: when a video starts, the 72 are let go (func_800DF5DC), so none is left from another video.
//
// Big Show's theme (song 0x18) and video (5) are there while he is (bigshow_frame); the rest while the
// option is on. Off, everything is put back as the game had it.
//
// THE ADDED THEMES' SCRIPT (content2 2026-11-19, the user: our own, not No Mercy Plus's). Every theme the arcade adds --
// the built-in ports' (Revenge's song 0x214, Revenge Redux's songs), Jeff Jarrett's and The Blue Meanie's (WrestleMania
// 2000's samples), a wrestler folder's theme.bin, and the silent song below -- is one long sample played by one sound
// script of the game's own format, theme_script below: written here from the game's script interpreter (exe lifted.cpp:
// run_channel_script and its command table; docs\THEME_SCRIPT.md), not taken from any game. The importer writes the same
// bytes into content_index.h for the content pack's added themes (exe src\romimport\ri_content.cpp, mod\gen\mkcontent.py).
// No Mercy Plus's own songs keep the scripts its ROM gives them.
//
// The ported Superstars' videos (WrestleMania 2000's: wmport_files.h) are videos past the game's 57
// (WMP_VIDEO0 on, their records' number): the game's function that picks a video (func_8010C998) is
// replaced by one that knows them (media_video_select), their lists here, their frames swapped in the
// same way from their files.
#include "arcade.h"
#include "content_index.h"
#include "gamefn.h"
#include "tv_carriers.h"
#include "wmport_files.h"

#define NM_MUSIC_BANK  0x80062BFCu   // u32 -> the music bank: sound [i] script at +0x18 + 8 i
#define NM_SONGS       ((u32)media_songs)   // s16 per song: the sound it plays (or a file, >= 0x2C0; -1 none):
                                     // the game's table (0x800480D8) made longer (content_index.h; gen/patches.py)
#define NM_ENTRANCES   0x800F50D0u   // 12 bytes per music slot: s16 song, s16, u32 effects, u32 -
#define NM_VIDEO_LIST(v) (0x800F50CCu + 12u * (u32)(v))  // the entrance table's word for video v
#define BANK_SOUND(b, i) ((b) + 0x18u + 8u * (u32)(i))
#define BANK_MAPS      0x08          // u32 number of instrument numbers a script can use
#define BANK_CTL       0x10          // u32 -> the instruments ("N64 PtrTablesV2")
#define BANK_MAP       0x14          // u32 -> u16 [maps]: a script's instrument number -> the instrument
#define CTL_COUNT      0x20          // u32 number of instruments
#define CTL_LEVEL      0x24          // u32 -> u8 [count]
#define CTL_TUNE       0x28          // u32 -> f32 [count]
#define CTL_WAVES      0x2C          // u32 -> u32 [count]: each instrument's wave table
#define WAVE_BASE      0x00          // u32 ROM address of the sample
#define WAVE_LOOP      0x0C          // u32 -> loop
#define WAVE_BOOK      0x10          // u32 -> ADPCM book
#define WAVE_SIZE      0xD0

// the game's audio DMA cache (libultra's sample scheme): buffers in a list kept in address order
#define AUD_DMA_OFF    0x80098954u   // bit 0: no reads now
#define AUD_DMA_USED   0x8008D2E0u   // u32 -> first buffer in use
#define AUD_DMA_FREE   0x8008D2E4u   // u32 -> first free buffer
#define AUD_DMA_LEN    0x8008D2F4u   // u32 bytes per buffer
#define DMA_PREV 0x0
#define DMA_NEXT 0x4
#define DMA_AGE  0x8                 // frames left unused before it's freed
#define DMA_ADDR 0xC                 // ROM address it holds
#define DMA_BUF  0x10                // -> the bytes

#define SONG_FIRST     23            // songs 23..66: music slots 1..44's themes
#define SONG_LAST      66
#define SONG_BIG_SHOW  0x18
#define SLOT_BIG_SHOW  2
#define BIG_SHOW_FX    0x405u        // his entrance's effects, as No Mercy Plus has them
#define VIDEO_BIG_SHOW 5
#define THEME_ROM      0x04000000u   // past the 32 MB cartridge
// How many instruments the sound bank can hold, the game's and ours. A song's script names its instrument with one byte,
// through the bank's map, so 256 is the most a script can reach.
#define MAX_INSTRUMENTS 256

// The instruments' wave tables (with their ADPCM books and loops) have to be below 16 MB, because the sound commands
// hold 24-bit addresses (exe src/main/arcade_audio.cpp). They used to sit in 16 KB at 0x807C0000, which held 78 of them,
// with nothing to stop more running into the menu's display list above. THEME_WAVE_RAM (arcade.h) is 1 MB set aside for
// them at the start of the middle memory region, enough for about 5,000.

static u8 lvl[MAX_INSTRUMENTS];
static u32 tune[MAX_INSTRUMENTS], waves[MAX_INSTRUMENTS];
static u16 map[MAX_INSTRUMENTS];
static u8 scripts[CT_SONGS + FOLDER_THEMES + 1][24];   // (+ 1: the silent song, below)

// ---- the silent song (2026-11-10) ------------------------------------------------------------------------
// A folder wrestler whose theme is a common-format file (theme.mp3 ...; the exe plays it: arcade_media.cpp) still has
// the game start and stop "a song" for his entrance, so the game's music state and the entrance's timing are what they
// always were. That song is this one: the added themes' own script, playing an instrument of its own whose level is 0
// and whose sample data is all zeros (SILENT_ROM: themes_audio_dma fills its buffers with zeros, which decode to
// silence). Nothing the rest of the music uses is changed: not the bank's other instruments, not their levels.
// It is one more instrument and one more sound after the folder themes', there while the bank is set up (install).
#define SILENT_ROM    0x0F000000u    // made-up sample addresses, 16 MB of them, between the pack's and the folders'
static int silent_on;                // the bank has it

// ---- themes from wrestler folders ----------------------------------------------------------------------
// Each is one more instrument, song and sound after all the built-in ones, added while the bank is set up (install).
// Its sound is a content pack of its own. The game asks for sample data by address through the audio DMA callback
// (themes_audio_dma), so each theme gets its own 16 MB of made-up addresses from FOLDER_THEME_ROM.
// The record the exe fills: wrestler_theme_get in arcade_wrestlers.cpp.
typedef struct { u32 id, pack; u8 wave[0xD0]; u8 level, loop, spare[2]; u32 tune; } FolderTheme;
#define FOLDER_THEME_ROM  0x10000000u
#define FOLDER_SONG0      SONGS_N
#define FOLDER_SOUND0     (SOUND_EXT0 + EXT_SLOTS_N)
extern int plat_wrestler_theme_count(void);
extern void plat_wrestler_theme_get(int i, u32 dst);
static FolderTheme folder_themes[FOLDER_THEMES];
static int folder_themes_read = -1;
static int folder_theme_count(void) {
    if (folder_themes_read < 0) {
        int n = plat_wrestler_theme_count();
        if (n > FOLDER_THEMES) n = FOLDER_THEMES;
        for (int i = 0; i < n; i++) plat_wrestler_theme_get(i, (u32)&folder_themes[i]);
        folder_themes_read = n;
    }
    // (2026-11-14) 0..FOLDER_THEMES in the game (the lines above); kept to it where it is read, so that no index made from it
    //  runs past folder_themes -- undefined in C, which the compiler took its ranges from. Nothing changes in the game.
    return folder_themes_read < FOLDER_THEMES ? folder_themes_read : FOLDER_THEMES;
}
static int folder_themes_on;     // How many are in the bank. Fewer than the folders hold if the bank ran out of room.

static u32 inst_bank, inst_ctl;      // the bank it's installed in (0: none)
static u32 orig_count, orig_level, orig_tune, orig_waves, orig_maps, orig_map;
static u32 orig_sound[CT_SONGS];     // the sounds' own scripts (0: not redirected)
static s16 orig_song[CT_SONGS];
static u32 orig_fx;
static u32 tv_orig[CT_VIDEOS + 1];
static u16 tv_list[CT_VIDEOS + 1][CT_VIDEO_FRAMES + 1][2];
static int active_video;             // the video being shown (as the game last picked one)
static u16 pv_list[WMP_VIDEOS_N][WMP_VIDEO_LEN][2];   // the ported Superstars' videos' lists (carriers)
static int port_video(int v) { return v >= WMP_VIDEO0 && v < WMP_VIDEO0 + WMP_VIDEOS_N; }

// ---- Titantron videos from wrestler folders ------------------------------------------------------------
// Video numbers after the built-in ones. His frames are a content pack, read straight from memory. The small panel some
// screens show plays through the game's own frame files (tv_carrier), so it only reaches the first TV_CARRIERS frames.
typedef struct { u32 id, pack, frames; } FolderVideo;
#define FOLDER_VIDEO0 (WMP_VIDEO0 + WMP_VIDEOS_N)
extern int plat_wrestler_video_count(void);
extern void plat_wrestler_video_get(int i, u32 dst);
static FolderVideo folder_videos[FOLDER_VIDEOS];
static u16 fv_list[FOLDER_VIDEOS][TV_CARRIERS + 1][2];
static int folder_videos_read = -1;
static int folder_video_count(void) {
    if (folder_videos_read < 0) {
        int n = plat_wrestler_video_count();
        if (n > FOLDER_VIDEOS) n = FOLDER_VIDEOS;
        for (int i = 0; i < n; i++) plat_wrestler_video_get(i, (u32)&folder_videos[i]);
        folder_videos_read = n;
    }
    // (2026-11-14: 0..FOLDER_VIDEOS in the game; kept to it where it is read, as folder_theme_count's. Nothing changes in the game.)
    return folder_videos_read < FOLDER_VIDEOS ? folder_videos_read : FOLDER_VIDEOS;
}
static int is_folder_video(int v) { return v >= FOLDER_VIDEO0 && v < FOLDER_VIDEO0 + folder_video_count(); }
// The video number of this wrestler's own video, or -1 (roster.c).
int media_folder_video(u32 id) {
    for (int i = 0, n = folder_video_count(); i < n; i++) if (folder_videos[i].id == id) return FOLDER_VIDEO0 + i;
    return -1;
}
// Frame k of folder video v into dest.
static void folder_video_frame(int v, int k, u32 dest) {
    const FolderVideo* fv = &folder_videos[v - FOLDER_VIDEO0];
    const u8* from = (const u8*)plat_pack((int)fv->pack) + (u32)k * CT_FRAME_SIZE;
    for (u32 i = 0; i < CT_FRAME_SIZE; i++) RD8(dest + i) = from[i];
}
static int want_bs, want_media;
static u32 want_late;                // the themes (and videos) of Jarrett, Droz, Mideon, the Meanie: bit each
u32 dbg_theme_installs, dbg_theme_reads, dbg_tv_swaps, dbg_theme_last_off, dbg_tv_dest, dbg_tv_frame;

static int ram_ptr(u32 p) { return GAME_PTR(p); }
// a song's script wanted: No Mercy Plus's (with its media, Big Show's with him); the Superstars' after
// Benoit (Jarrett's, Droz's, Mideon's, the Meanie's) while each is on, over No Mercy Plus's for its song
static int song_wanted(int i) {
    if (!ct_song[i].len) return 0;       // (nmplus: a row whose source game the first start hadn't)
    int s = ct_song[i].song;
    if (i >= CT_SONG_LATE0) return (want_late >> (i - CT_SONG_LATE0)) & 1u;
    for (int k = 0; k < CT_SONG_LATE_N; k++)
        if (((want_late >> k) & 1u) && ct_song[CT_SONG_LATE0 + k].song == s) return 0;
    return want_media || (want_bs && s == SONG_BIG_SHOW);
}
#define VIDEO_NMPLUS0 44
// (videos 44 on: No Mercy Plus's alone, for its Superstars that only the new ones name -- always theirs)
static int video_wanted(int v) {
    return want_media || (want_bs && v == VIDEO_BIG_SHOW) || (v >= VIDEO_NMPLUS0 && arcade_extras_has(EXTRAS_NMPLUS));
}

// ---- the added themes' sound script (content2 2026-11-19; docs\THEME_SCRIPT.md) -----------------------------------
// A bank sound starts on a freshly reset channel (exe lifted.cpp reset_sound_channel): fixed velocity 0x7F, no level byte with
// a note (velocity_per_note 0), the default envelope (time scale 1, start 0, 1 frame to 0x7F, 255 frames to 0x7F, released
// over 15), transpose 0. The script, a command at a time (bytes >= 0x80 through the game's command table; a byte below
// 0x80 is a note):
//   0x84 t s a p d u r   the envelope (script_set_envelope): time scale t, start level s, a frames to the peak p, d frames to
//                        the sustain level u, r frames to silence once released
//   0x81 i               the instrument i (one byte below 0x80; install() writes the bank's number there)
//   0xA3 n b             the transpose: a random number below n, plus b (script_random_transpose: it steps the sound
//                        player's random register, *0x8008D2A4, eight times whatever n is)
//   note                 the note (< 0x60): the pitch is note + transpose + the instrument's tuning
//   length               one byte (< 0x80) or two (the first with bit 7: 15 bits); 0x7FFF holds the note for ever (no
//                        release: start_channel_envelope)
//   0x80                 the end
// Ours: the sample at full level from its first frame (1 frame from 0 to the peak 0xFF, held there: the decay's 1 frame goes
// from 0xFF to 0xFF), released over 15 frames when the game stops it; pitch 31 + the instrument's tuning (the samples are
// made for that pitch); a random transpose of range 0 (none), so that the sound player's random numbers run on exactly as
// they did when every added theme played No Mercy Plus's script (which has one); the note held for ever (the game stops the
// theme). 17 bytes: the length the content index's rows have always had (firstrun\hdrfill.bin's layout counts a row's
// leaves).
#define THEME_SCRIPT_LEN 17
#define THEME_SCRIPT_AT 9                // (the instrument byte)
static const u8 theme_script[THEME_SCRIPT_LEN] = {
    0x84, 0x01, 0x00, 0x01, 0xFF, 0x01, 0xFF, 0x0F,   // envelope: scale 1, from 0 over 1 frame to 0xFF, 1 frame to 0xFF, release 15
    0x81, 0x00,                                       // instrument (set at install)
    0xA3, 0x00, 0x00,                                 // transpose: random below 0, plus 0
    0x1F,                                             // note 31
    0xFF, 0xFF,                                       // length 0x7FFF: held
    0x80,                                             // end
};
// The silent song's wave record when the content pack has no instrument to lend it (a first start without the sources it is
// made from): ours -- an ADPCM wave of order 2 with one all-zero predictor, looping for ever over its SILENT_WAVE_LEN bytes
// (the sample data the audio DMA gives it is all zeros: SILENT_ROM), tuned as the added themes are.
#define SILENT_WAVE_LEN   0x10002u       // bytes of sample data: 7282 frames of 9 bytes (16 samples each)
#define SILENT_TUNE       0xC2400000u    // -48.0, the added themes' tuning
static void silent_wave(u32 wave) {
    for (u32 i = 0; i < WAVE_SIZE; i++) RD8(wave + i) = 0;
    RD32(wave + 0x04) = SILENT_WAVE_LEN;                    // length
    RD8(wave + 0x09) = 1;                                   // (relocated: the game's mark)
    RD32(wave + 0x18) = 2; RD32(wave + 0x1C) = 1;           // the book: order 2, one predictor (its coefficients zero)
    RD32(wave + 0xA0) = 0;                                  // the loop: from the start
    RD32(wave + 0xA4) = SILENT_WAVE_LEN / 9u * 16u;         // ... to the end (samples)
    RD32(wave + 0xA8) = 0xFFFFFFFFu;                        // ... for ever
}

// ---- the music bank ---------------------------------------------------------------------------
static int bank_ours(u32 bank) {
    return inst_bank && bank == inst_bank && RD32(inst_bank + BANK_CTL) == inst_ctl &&
           RD32(inst_ctl + CTL_WAVES) == (u32)waves;
}

static void sounds_restore(void) {
    for (int i = 0; i < CT_SONG_EXT0; i++) {
        if (!orig_sound[i]) continue;
        u32 at = BANK_SOUND(inst_bank, ct_song[i].song - SONG_FIRST);
        if (RD32(at) == (u32)scripts[i]) RD32(at) = orig_sound[i];
        orig_sound[i] = 0;
        RD16(NM_SONGS + 2u * ct_song[i].song) = (u16)orig_song[i];
    }
}

static void uninstall(void) {
    if (!inst_bank) return;
    if (bank_ours(RD32(NM_MUSIC_BANK))) {
        sounds_restore();
        RD32(inst_ctl + CTL_COUNT) = orig_count;
        RD32(inst_ctl + CTL_LEVEL) = orig_level;
        RD32(inst_ctl + CTL_TUNE) = orig_tune;
        RD32(inst_ctl + CTL_WAVES) = orig_waves;
        RD32(inst_bank + BANK_MAPS) = orig_maps;
        RD32(inst_bank + BANK_MAP) = orig_map;
    }
    for (int i = 0; i < CT_SONGS; i++) orig_sound[i] = 0;
    inst_bank = 0;
}

static void install(u32 bank) {
    u32 ctl = RD32(bank + BANK_CTL);
    if (!ram_ptr(ctl)) return;
    u32 n = RD32(ctl + CTL_COUNT), nm = RD32(bank + BANK_MAPS);
    u32 l = RD32(ctl + CTL_LEVEL), t = RD32(ctl + CTL_TUNE), w = RD32(ctl + CTL_WAVES), m = RD32(bank + BANK_MAP);
    if (n == 0 || n + CT_INSTRUMENTS > MAX_INSTRUMENTS || nm == 0 || nm + CT_INSTRUMENTS > MAX_INSTRUMENTS) return;
    int nf = folder_theme_count();       // As many as the bank's table has room for.
    if (nf > MAX_INSTRUMENTS - (int)(n + CT_INSTRUMENTS)) nf = MAX_INSTRUMENTS - (int)(n + CT_INSTRUMENTS);
    if (nf > MAX_INSTRUMENTS - (int)(nm + CT_INSTRUMENTS)) nf = MAX_INSTRUMENTS - (int)(nm + CT_INSTRUMENTS);
    folder_themes_on = nf;
    // (2026-11-10: the silent song's instrument, after them, if there's room for one more)
    const int ns = (n + CT_INSTRUMENTS + (u32)nf < MAX_INSTRUMENTS && nm + CT_INSTRUMENTS + (u32)nf < MAX_INSTRUMENTS) ? 1 : 0;
    silent_on = 0;
    if (!ram_ptr(l) || !ram_ptr(t) || !ram_ptr(w) || !ram_ptr(m)) return;
    for (u32 i = 0; i < n; i++) { lvl[i] = RD8(l + i); tune[i] = RD32(t + 4 * i); waves[i] = RD32(w + 4 * i); }
    for (u32 i = 0; i < nm; i++) map[i] = RD16(m + 2 * i);
    for (u32 j = 0; j < CT_INSTRUMENTS; j++) {
        const CtInstrument* ci = &ct_inst[j];
        u32 wave = THEME_WAVE_RAM + WAVE_SIZE * j;
        for (u32 i = 0; i < WAVE_SIZE; i++) RD8(wave + i) = ci->wave[i];
        RD32(wave + WAVE_BASE) = THEME_ROM + ci->off;
        RD32(wave + WAVE_LOOP) = ci->loop ? wave + 0xA0 : 0;
        RD32(wave + WAVE_BOOK) = wave + 0x18;
        lvl[n + j] = ci->level; tune[n + j] = ci->tune; waves[n + j] = wave;
        map[nm + j] = (u16)(n + j);
    }
    for (int i = 0; i < CT_SONGS; i++) {
        for (int k = 0; k < ct_song[i].len; k++) scripts[i][k] = ct_song[i].script[k];
        if (ct_song[i].len) scripts[i][ct_song[i].at] = (u8)(nm + ct_song[i].inst);   // (nmplus: a row without its source game: none)
        orig_sound[i] = 0;
    }
    // The themes from folders. Each plays through the added themes' script (theme_script, ours), with its own instrument in it.
    for (int j = 0; j < nf; j++) {
        const FolderTheme* ft = &folder_themes[j];
        const u32 at = CT_INSTRUMENTS + (u32)j, wave = THEME_WAVE_RAM + WAVE_SIZE * at;
        for (u32 i = 0; i < WAVE_SIZE; i++) RD8(wave + i) = ft->wave[i];
        RD32(wave + WAVE_BASE) = FOLDER_THEME_ROM + ((u32)j << 24);
        RD32(wave + WAVE_LOOP) = ft->loop ? wave + 0xA0 : 0;
        RD32(wave + WAVE_BOOK) = wave + 0x18;
        lvl[n + at] = ft->level; tune[n + at] = ft->tune; waves[n + at] = wave;
        map[nm + at] = (u16)(n + at);
        u8* script = scripts[CT_SONGS + j];
        for (int k = 0; k < THEME_SCRIPT_LEN; k++) script[k] = theme_script[k];
        script[THEME_SCRIPT_AT] = (u8)(nm + at);
        RD16(NM_SONGS + 2u * (u32)(FOLDER_SONG0 + j)) = (u16)(FOLDER_SOUND0 + j);
    }
    if (ns) {       // The silent song: the first added theme's wave record (its ADPCM book, its loop), at level 0, its samples zeros.
        const u32 at = CT_INSTRUMENTS + (u32)nf, wave = THEME_WAVE_RAM + WAVE_SIZE * at;
        // (content2: read, not folded -- the first start fills ct_inst; with no instrument in the pack, our own record)
        const int lend = *(volatile const u32*)&ct_inst[0].len != 0;
        if (lend) for (u32 i = 0; i < WAVE_SIZE; i++) RD8(wave + i) = ct_inst[0].wave[i];
        else silent_wave(wave);
        RD32(wave + WAVE_BASE) = SILENT_ROM;
        RD32(wave + WAVE_LOOP) = !lend || ct_inst[0].loop ? wave + 0xA0 : 0;
        RD32(wave + WAVE_BOOK) = wave + 0x18;
        lvl[n + at] = 0; tune[n + at] = lend ? ct_inst[0].tune : SILENT_TUNE; waves[n + at] = wave;
        map[nm + at] = (u16)(n + at);
        u8* script = scripts[CT_SONGS + FOLDER_THEMES];
        for (int k = 0; k < THEME_SCRIPT_LEN; k++) script[k] = theme_script[k];
        script[THEME_SCRIPT_AT] = (u8)(nm + at);
        silent_on = 1;
    }
    orig_count = n; orig_level = l; orig_tune = t; orig_waves = w; orig_maps = nm; orig_map = m;
    RD32(ctl + CTL_LEVEL) = (u32)lvl;
    RD32(ctl + CTL_TUNE) = (u32)tune;
    RD32(ctl + CTL_WAVES) = (u32)waves;
    RD32(ctl + CTL_COUNT) = n + CT_INSTRUMENTS + (u32)nf + (u32)ns;
    RD32(bank + BANK_MAP) = (u32)map;
    RD32(bank + BANK_MAPS) = nm + CT_INSTRUMENTS + (u32)nf + (u32)ns;
    inst_bank = bank; inst_ctl = ctl;
    dbg_theme_installs++;
}

// The songs: each wanted one plays its script from the pack, the others their own. (Two of the pack's can
// be for one song, a late Superstar's: the ones no longer wanted are put back first.)
static void sounds_update(void) {
    for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < CT_SONG_EXT0; i++) {     // (the made slots' songs play from their own sounds)
        int s = ct_song[i].song;
        u32 at = BANK_SOUND(inst_bank, s - SONG_FIRST), song = NM_SONGS + 2u * (u32)s;
        if (song_wanted(i)) {
            if (!pass) continue;
            if (!orig_sound[i]) { orig_sound[i] = RD32(at); orig_song[i] = (s16)RD16(song); }
            if (RD32(at) != (u32)scripts[i]) RD32(at) = (u32)scripts[i];
            if (RD16(song) != (u16)(s - SONG_FIRST)) RD16(song) = (u16)(s - SONG_FIRST);
        } else if (orig_sound[i] && !pass) {
            if (RD32(at) == (u32)scripts[i]) RD32(at) = orig_sound[i];
            RD16(song) = (u16)orig_song[i];
            orig_sound[i] = 0;
        }
    }
}

// ---- the entrance table (an overlay's data) -----------------------------------------------------
// (It's there when its first slots are: songs 0x16-0x19, each with its -3600 word.)
int entrance_table_loaded(void) {
    for (u32 i = 0; i < 4; i++)
        if (RD16(NM_ENTRANCES + 12u * i) != 0x16u + i || RD16(NM_ENTRANCES + 12u * i + 2) != 0xF1F0u) return 0;
    return 1;
}

static void lists_init(void) {
    if (tv_list[1][0][0]) return;
    for (int v = 1; v <= CT_VIDEOS; v++) {
        for (int i = 0; i < CT_VIDEO_FRAMES; i++) { tv_list[v][i][0] = tv_carrier[i]; tv_list[v][i][1] = i == 1 ? 0x0004 : 0x8004; }
        tv_list[v][CT_VIDEO_FRAMES][0] = 0xFFFF; tv_list[v][CT_VIDEO_FRAMES][1] = 0;
    }
}

// Each frame, and just before the game reads the table (a video picked, an entrance's effects).
void media_entrance_tables(void) {
    if (!entrance_table_loaded()) return;
    lists_init();
    u32 slot = NM_ENTRANCES + 12u * SLOT_BIG_SHOW;
    if (want_bs && RD32(slot + 4) != BIG_SHOW_FX) { orig_fx = RD32(slot + 4); RD32(slot + 4) = BIG_SHOW_FX; }
    else if (!want_bs && RD32(slot + 4) == BIG_SHOW_FX) RD32(slot + 4) = orig_fx;
    for (int v = 1; v <= CT_VIDEOS; v++) {
        u32 at = NM_VIDEO_LIST(v), ours = (u32)tv_list[v];
        if (video_wanted(v)) { if (RD32(at) != ours) { tv_orig[v] = RD32(at); RD32(at) = ours; } }
        else if (RD32(at) == ours) RD32(at) = tv_orig[v];
    }
}

// The game picks a video (func_8010C998 with v >= 0, or a menu's func_8011E488): that one's frames.
void media_video_picked(s32 v) {
    tron_video_picked(v);
    media_entrance_tables();
    if (!port_video(v) && !is_folder_video(v) &&
        (v < 1 || v > CT_VIDEOS || !entrance_table_loaded() || RD32(NM_VIDEO_LIST(v)) != (u32)tv_list[v])) { active_video = 0; return; }
    if (active_video != v)
        for (int i = 0; i < TV_CARRIERS; i++) plat_game_call(GF_SPRITE_RELEASE, tv_carrier[i], 0, 0);
    active_video = v;
}

// func_8010C998(v), the entrance's video: v >= 0 picks video v (its list from its first frame; none if
// there's no such video), v < 0 is a frame's step (the frame shown and the next, how faded); the list.
#define VID_LIST  0x80133C28u   // u32 -> the video's (frame file, time) list (0: none)
#define VID_NUM   0x80133C24u   // s32 its number
#define VID_AT    0x80133C20u   // s32 its entry shown (in halfwords)
#define VID_TIME  0x80125824u   // s32 how long it's been shown
#define VID_FILE  0x8015CD4Cu   // s32 the frame's file
#define VID_NEXT  0x8015CD50u   // s32 ... the next one's
#define VID_ALPHA 0x8015CD14u   // s32 how opaque (0xFF: no fading)
s32 media_video_select(s32 v) {
    if (v == 0x20) { s32 t = story_tron_video(); if (t > 0) v = t; }   // (a story scene's Titantron cut: the man on it's own video)
    if (v >= 0) media_video_picked(v); else media_entrance_tables();
    if (v < 0) {
        u32 list = RD32(VID_LIST);
        if (!list) return 0;
        u32 e = list + (u32)RDS32(VID_AT) * 2u;
        RDS32(VID_FILE) = RDS16(e);
        s32 n = RDS16(e + 4);
        if (n < 0) n = RDS16(list + (u32)(RDS16(e + 6) * 4));
        RDS32(VID_NEXT) = n;
        s32 t = RDS16(e + 2), alpha = 0xFF, at = RDS32(VID_TIME);
        if ((t & 0x8000) && at && (t & 0xFFF)) alpha = 0xFF - at * 0xFF / (t & 0xFFF);   // (fades out over its time)
        RDS32(VID_ALPHA) = alpha;
        return (s32)RD32(VID_LIST);
    }
    if (v >= 1 && v < 0x3A) { RDS32(VID_NUM) = v; RD32(VID_LIST) = RD32(NM_VIDEO_LIST(v)); }
    else if (port_video(v)) {
        u16 (*l)[2] = pv_list[v - WMP_VIDEO0];
        const u16 (*w)[2] = wmp_video[v - WMP_VIDEO0];
        for (int i = 0; i < WMP_VIDEO_LEN; i++) {
            l[i][0] = w[i][0] == 0xFFFF ? 0xFFFF : tv_carrier[w[i][0]]; l[i][1] = w[i][1];
            if (w[i][0] == 0xFFFF) break;
        }
        RDS32(VID_NUM) = v; RD32(VID_LIST) = (u32)l;
    } else if (is_folder_video(v)) {
        u16 (*l)[2] = fv_list[v - FOLDER_VIDEO0];
        const int shown = (int)folder_videos[v - FOLDER_VIDEO0].frames < TV_CARRIERS ? (int)folder_videos[v - FOLDER_VIDEO0].frames : TV_CARRIERS;
        for (int i = 0; i < shown; i++) { l[i][0] = tv_carrier[i]; l[i][1] = 0x8004; }
        l[shown][0] = 0xFFFF; l[shown][1] = 0;
        RDS32(VID_NUM) = v; RD32(VID_LIST) = (u32)l;
    } else { RD32(VID_LIST) = 0; RD32(0x800F5510u) = 0; RD32(VID_NUM) = 0; }
    RD32(VID_TIME) = 0; RD32(VID_AT) = 0;
    return (s32)RD32(VID_LIST);
}

// ---- frames, as the game reads their files (func_80003C94 pre / post) --------------------------
#define PENDING_MAX 8
static struct { u32 dest, age; int video, frame; } pend[PENDING_MAX];
static int n_pend;
void media_file_pre(u32 file, u32 dest) {
    if (!active_video || !ram_ptr(dest) || !ram_ptr(dest + CT_FRAME_SIZE)) return;
    int k = -1;
    for (int i = 0; i < TV_CARRIERS; i++) if (tv_carrier[i] == file) { k = i; break; }
    if (k < 0) return;
    if (n_pend == PENDING_MAX) { for (int i = 1; i < n_pend; i++) pend[i - 1] = pend[i]; n_pend--; }
    pend[n_pend].dest = dest; pend[n_pend].age = 0; pend[n_pend].video = active_video; pend[n_pend].frame = k;
    n_pend++;
}
// (a read is done when the buffer holds the file: its palette's first 32 bytes)
static int holds(u32 d, const u8* sig) { for (int i = 0; i < 32; i++) if (RD8(d + 16u + (u32)i) != sig[i]) return 0; return 1; }
void media_file_post(void) {
    for (int i = 0; i < n_pend; ) {
        int done = 0;
        if (holds(pend[i].dest, tv_carrier_sig[pend[i].frame])) {
            int v = pend[i].video, k = pend[i].frame;
            if (is_folder_video(v)) folder_video_frame(v, k, pend[i].dest);
            else if (port_video(v)) {
                u32 f = k < WMP_VIDEO_FRAMES ? wmp_video_frames[v - WMP_VIDEO0][k] : 0;
                if (f) plat_wmport_read(wmp_files[f - WMP_FILE0][0], pend[i].dest, CT_FRAME_SIZE);
            } else {
                u32 f = ct_video[v - 1][k];
                plat_content_read(CONTENT_FRAMES + f * CT_FRAME_SIZE, pend[i].dest, CT_FRAME_SIZE);
            }
            dbg_tv_swaps++; dbg_tv_dest = pend[i].dest; dbg_tv_frame = (u32)(v << 8 | k);
            done = 1;
        }
        if (done || ++pend[i].age > 64) { for (int k = i + 1; k < n_pend; k++) pend[k - 1] = pend[k]; n_pend--; }
        else i++;
    }
}

// ---- the music slots made (EXT_SLOT0 on) ------------------------------------------------------------
// Their songs (SONG_EXT0 on) play sounds SOUND_EXT0 on: the music bank's 200 and these, in a copy of the
// bank made longer (its header and sounds; the instruments, maps and the rest are the bank's own). A
// sound is played with its bank (func_800037EC: func_80027D38 then func_80027494; the voice keeps the
// bank it plays from): one of these with the copy, made from the bank as it is now.
#define BANK_SOUNDS_GAME 200
// (content2) the priority the added sounds take: the game's first theme sound's (song 23's, the bank's sound 0) -- as before,
// when it was read through ct_song[0]: song 23's row whenever the pack has No Mercy Plus's themes (without them that row is
// empty)
#define THEME_PRIORITY_SOUND 0u
static u8 ext_bank[0x18 + 8 * (BANK_SOUNDS_GAME + EXT_SLOTS_N + FOLDER_THEMES + 1)] __attribute__((aligned(8)));   // (+ 1: the silent song's sound)
#define SILENT_SOUND (FOLDER_SOUND0 + folder_themes_on)   // (2026-11-10: the silent song's sound: the one after the folder themes')
s32 media_slot_song(u32 slot) {        // a music slot's song (func_8000AAF4): -1, the game's table's
    return slot >= EXT_SLOT0 && slot < EXT_SLOT0 + EXT_SLOTS_N ? SONG_EXT0 + (s32)(slot - EXT_SLOT0) : -1;
}
s32 media_user_song(u32 id) {          // a music file's theme for him (gen_data/music): its song, else -1
    for (int i = 0, n = folder_theme_count(); i < n && i < FOLDER_THEMES; i++) if (folder_themes[i].id == id && i < folder_themes_on) return FOLDER_SONG0 + i;   // (His folder's first.)
    for (int i = 0; i < USER_MUSIC_N; i++) if (user_music[i][0] == (u16)id) return media_slot_song(user_music[i][1]);
    return -1;
}
// Two of the game's own takers of a Superstar's song (func_8000AAF4) keep only the game's songs (< 80), so a
// made slot's (SONG_EXT0 on: the ported Superstars', the music files') didn't play there: replaced, the same
// but for the songs made too.
#define SONGS_END (SONG_EXT0 + EXT_SLOTS_N + FOLDER_THEMES)
s32 roster_entrance_song(u32 id, s32 c);
// 2026-11-12: whose song the game last asked for in the ring -- a Rumble run-in's (below) or the winner's after the bell
// (media_winner_song) -- so that when it plays (media_play_sound), his own theme file plays natively there too, as it does
// on the entrance screen (tron.c tron_music). Any theme file of his: theme.mp3 ... or theme.bin, which the exe plays
// itself now, with no count of them (the mod's 64-theme bank isn't needed for them).
static s32 asked_id = -1, asked_song = -1;
static void song_asked(s32 id, s32 song) { asked_id = id; asked_song = song; }
// func_800E22D8(id, costume): a Royal Rumble entrant's song after the first two (none: -1); is there one
s32 media_entrant_song(s32 id, u32 c) {
    RD16(0x80159072u) = (u16)id;
    s32 s = id >= 0 ? roster_entrance_song((u32)id, (s32)(c & 0xFFFFu)) : -1;
    if ((u16)s >= SONGS_END) s = -1;
    if (s > 0) song_asked(id, s);
    RD16(0x80159070u) = (u16)s;
    return (s16)s > 0;
}
// func_800DBA88 (the six-man engine's, rw_800DBA88): the song after the bell, the winner's -- the last of the
// n players with his win bit (players 0-3: 0x800A10EC, 4 on: SIX_RESULT6); his Superstar and costume at
// SIXA_80076264 (s16) / SIXA_8007626C (u8) by player (func_80013F8C / func_80013FA4); 0x16 the game's own, 0xA
// (a story's match, lost)
s32 media_winner_song(s32 n) {
    u32 f = RD32(0x800A10ECu), more = RD32(SIX_RESULT6);
    if ((RD32(0x800A9950u) & 0x100000u) && !(f & 1u)) return 0xA;
    if (n <= 0) return 0x16;
    s32 w = (f & 1u) ? 0 : -1;
    for (s32 i = 1; i < n && i < 4; i++) if (f & (1u << i)) w = i;
    for (s32 i = 4; i < n && i < 36; i++) if ((more >> (i - 4)) & 1u) w = i;
    if (w < 0) return 0x16;
    s32 id = RDS16(SIXA_80076264 + 2u * (u32)w), c = RD8(SIXA_8007626C + (u32)w);
    if (id <= 0) return 0x16;
    s32 s = roster_entrance_song((u32)id, c);
    if ((u16)(s - 23) < SONGS_END - 23) { song_asked(id, s); return (s32)(s16)s; }
    return 0x16;
}
int media_slot_fx(u32 slot, u32* fx) {  // ... and its entrance effects (func_8000AB64)
    if (slot < EXT_SLOT0 || slot >= EXT_SLOT0 + EXT_SLOTS_N) return 0;
    *fx = ext_slot_fx[slot - EXT_SLOT0];
    return 1;
}
// func_800037EC(sound): a sound of the music bank played (none while 0x80043626 says no)
#define SND_HANDLE 0x80043620u
#define SND_LOCK   0x80043626u
s32 dbg_last_snd = -1, dbg_last_snd_h;
u32 dbg_silent_songs;      // (QA: how many entrances played the silent song)
s32 media_play_sound(s32 snd) {
    dbg_last_snd = (s16)snd;
    {   // (QA: the last 8 sounds asked for: 0x807F33B0.., the count at 0x807F33AC)
        u32 n = RD32(0x807F33ACu);
        RD32(0x807F33B0u + 4u * (n & 7u)) = (u32)(u16)snd;
        RD32(0x807F33ACu) = n + 1;
    }
    if (RD16(SND_LOCK)) return 0;
    func_800037BC(0);
    u32 bank = RD32(NM_MUSIC_BANK);
    snd = (s16)snd;
    // 2026-11-10: an entrance's theme: the big screen's video plays while it does (tron.c), and the exe is told who is
    // entering (tron_music -> recomp_arcade_entrance_media). If it plays a theme file of his, or his video's own sound,
    // the game's song for him is the silent one. (Moved here from after the sound's start, so the choice is made before
    // it. tron_music does nothing outside an entrance: menu and match music are never touched.)
    int native = tron_music(1);
    {   // (2026-11-12: a song asked for in the ring, playing now -- the sound is that song's: his theme file, sound only)
        const int asked = asked_id > 0 && asked_song > 0 && asked_song < SONGS_END &&
                          !(RDS16(NM_SCREEN) == 42 && RDS32(NM_ENTRANCE_STEP) >= 1 && RDS32(NM_ENTRANCE_STEP) <= 29) &&   // (not an entrance: tron_music has those)
                          RDS16(NM_SONGS + 2u * (u32)asked_song) == snd;
        if (!native && asked) native = tron_ring_song((u32)asked_id);
        asked_id = -1;
    }
    if (native & (NATIVE_THEME | NATIVE_VIDEO_SOUND)) {
        if (silent_on && inst_bank && bank_ours(bank) && RD32(bank) == BANK_SOUNDS_GAME) { snd = SILENT_SOUND; dbg_silent_songs++; }
        else tron_native_cancel();     // (no silent song to play, the bank isn't set up: his entrance the old way, all of it)
    }
    if (snd >= SOUND_EXT0) {
        if (!inst_bank || !bank_ours(bank) || RD32(bank) != BANK_SOUNDS_GAME) return 0;
        u32 b = (u32)ext_bank;
        for (u32 i = 0; i < 0x18 + 8u * BANK_SOUNDS_GAME; i += 4) RD32(b + i) = RD32(bank + i);
        for (int k = 0; k < EXT_SLOTS_N; k++) {
            int i = CT_SONG_EXT0 + k;
            RD32(b + 0x18 + 8u * (u32)(SOUND_EXT0 + k)) = (u32)scripts[i];
            RD32(b + 0x1C + 8u * (u32)(SOUND_EXT0 + k)) = RD32(bank + 0x1C + 8u * THEME_PRIORITY_SOUND);
        }
        for (int k = 0; k < folder_themes_on; k++) {
            RD32(b + 0x18 + 8u * (u32)(FOLDER_SOUND0 + k)) = (u32)scripts[CT_SONGS + k];
            RD32(b + 0x1C + 8u * (u32)(FOLDER_SOUND0 + k)) = RD32(bank + 0x1C + 8u * THEME_PRIORITY_SOUND);
        }
        if (silent_on) {
            RD32(b + 0x18 + 8u * (u32)SILENT_SOUND) = (u32)scripts[CT_SONGS + FOLDER_THEMES];
            RD32(b + 0x1C + 8u * (u32)SILENT_SOUND) = RD32(bank + 0x1C + 8u * THEME_PRIORITY_SOUND);
        }
        RD32(b) = BANK_SOUNDS_GAME + EXT_SLOTS_N + (u32)folder_themes_on + (silent_on ? 1u : 0u);
        bank = b;
    }
    func_80027D38(bank);
    s32 h = (s32)func_80027494(snd);
    RD32(SND_HANDLE) = (u32)h;
    dbg_last_snd_h = h;
    return h;
}

// ---- each frame ------------------------------------------------------------------------------
// (content2) something of ours for the bank: a made music slot's song in the pack (the ports' themes), or a wrestler folder's
// theme
static int ours_to_play(void) {
    for (int i = CT_SONG_EXT0; i < CT_SONGS; i++) if (*(volatile const u8*)&ct_song[i].len) return 1;
    return folder_theme_count() > 0;
}
void media_frame(int on, int bs, int media, u32 late) {
    // (content2 2026-11-19) The content pack holds the added themes of every source game the first start had (No Mercy Plus's
    // themes and videos only with it; the ports' themes with Revenge, Redux and WrestleMania 2000), each added one playing
    // through our script (theme_script). No Mercy Plus's media and Big Show's (there only with it) need No Mercy Plus.
    if (!arcade_extras_has(EXTRAS_NMPLUS)) { bs = 0; media = 0; }
    want_bs = bs; want_media = media; want_late = late;
    u32 bank = RD32(NM_MUSIC_BANK);
    if (bs || media || late || (on && ours_to_play())) {
        if (inst_bank && !bank_ours(bank)) { inst_bank = 0; for (int i = 0; i < CT_SONGS; i++) orig_sound[i] = 0; }
        if (!inst_bank && ram_ptr(bank)) install(bank);
        if (inst_bank) sounds_update();
    } else uninstall();
    media_entrance_tables();
    media_file_post();
}

static u32 phys(u32 a) { return (a >= 0x80000000u && a < 0xC0000000u) ? (a & 0x1FFFFFFFu) : a; }

// The audio DMA callback (func_8002B0C0, replaced): the physical address of `len` bytes of sample
// data from ROM address `addr`. The game's own: the cache (func_8002B12C) finds or reads a buffer.
// The pack's themes: the same, with the buffer filled from the pack instead of a cartridge read.
u32 themes_audio_dma(u32 addr, s32 len) {
    const int folder_theme = addr >= FOLDER_THEME_ROM && addr < FOLDER_THEME_ROM + ((u32)folder_themes_on << 24) ? (int)((addr - FOLDER_THEME_ROM) >> 24) : -1;
    const int silent = silent_on && addr >= SILENT_ROM && addr < SILENT_ROM + 0x01000000u;    // (2026-11-10: the silent song's samples: zeros)
    if (folder_theme < 0 && !silent && (addr < THEME_ROM || addr >= THEME_ROM + CONTENT_THEMES_SIZE + 0x1000u)) {
        u32 b = (u32)plat_game_call(GF_AUDIO_DMA, addr, (u32)len, 0);
        if (!b) return phys(addr);
        if ((addr & 0xFF000000u) == 0xFF000000u) addr = (addr & 0x00FFFFFFu) + 0x140000u;
        return phys(RD32(b + DMA_BUF) + addr - RD32(b + DMA_ADDR));
    }
    if (RD32(AUD_DMA_OFF) & 1) return 0;
    u32 dlen = RD32(AUD_DMA_LEN), end = addr + (u32)len, prev = 0, b = RD32(AUD_DMA_USED);
    for (; b; b = RD32(b + DMA_NEXT)) {
        u32 start = RD32(b + DMA_ADDR);
        if (addr < start) break;
        if (start + dlen >= end) { RD32(b + DMA_AGE) = 2; return phys(RD32(b + DMA_BUF) + addr - start); }
        prev = b;
    }
    b = RD32(AUD_DMA_FREE);
    if (!b) { b = RD32(AUD_DMA_USED); return b ? phys(RD32(b + DMA_BUF)) : 0; }  // none left (the game's does no better)
    RD32(AUD_DMA_FREE) = RD32(b + DMA_NEXT);
    if (prev) {
        u32 nx = RD32(prev + DMA_NEXT);
        RD32(b + DMA_NEXT) = nx;
        if (nx) RD32(nx + DMA_PREV) = b;
        RD32(b + DMA_PREV) = prev;
        RD32(prev + DMA_NEXT) = b;
    } else {
        u32 first = RD32(AUD_DMA_USED);
        RD32(b + DMA_PREV) = 0;
        RD32(b + DMA_NEXT) = first;
        if (first) RD32(first + DMA_PREV) = b;
        RD32(AUD_DMA_USED) = b;
    }
    u32 start = addr & ~1u, off = start - (folder_theme >= 0 ? FOLDER_THEME_ROM + ((u32)folder_theme << 24) : THEME_ROM);
    RD32(b + DMA_ADDR) = start;
    RD32(b + DMA_AGE) = 2;
    u32 buf = RD32(b + DMA_BUF);
    // A folder theme's sound is its own pack, and the wave record holds its length (offset 4).
    const u32 size = folder_theme >= 0 ? (u32)folder_themes[folder_theme].wave[4] << 24 | (u32)folder_themes[folder_theme].wave[5] << 16 | (u32)folder_themes[folder_theme].wave[6] << 8 | folder_themes[folder_theme].wave[7] : CONTENT_THEMES_SIZE;
    u32 have = off < size ? size - off : 0;
    u32 n = dlen < have ? dlen : have;
    if (silent) n = 0;      // (nothing read: the whole buffer is zeroed below)
    if (n && folder_theme >= 0) { const u8* from = (const u8*)plat_pack((int)folder_themes[folder_theme].pack) + off; for (u32 i = 0; i < n; i++) RD8(buf + i) = from[i]; }
    else if (n) plat_content_read(off, buf, n);
    dbg_theme_last_off = off;
    for (u32 i = n; i < dlen; i++) RD8(buf + i) = 0;
    dbg_theme_reads++;
    return phys(buf + addr - start);
}

// ---- the arena screen's video (tron.c) -------------------------------------------------------------
// A TEX picture's header (16 bytes, then its palette, then its texels): "TEX\0", u16 width, u16 height, u8 bits per
// texel, u8 -, u16 colours in its palette.
#define TEX_MAGIC    0x54455800u     // "TEX\0"
#define TEX_WIDTH    4u
#define TEX_HEIGHT   6u
#define TEX_BITS     8u
#define TEX_COLOURS  10u
#define TEX_HEAD     16u
#define FRAME_TEXELS (64u * 64u)     // a video frame's: 64 x 64
// (tronfix 2026-11-25, the user: "No Mercy stock titantrons are glitching on the titantron" -- a garbled picture, scrambled
// with wrong colours and pieces of another, in the entrances of No Mercy's own Superstars while "No Mercy Plus themes and
// videos" is off.) The game's own videos' frames (files 0x4859-0x4992) are two kinds of 64 x 64 TEX: CI8 with 256 colours
// (4624 bytes, CT_FRAME_SIZE) and CI4 with 16 (2096 bytes: 0x4862-0x4865, 0x487E-0x4888 ... -- the game's own player reads
// each one's header). The screen draws every frame as CI8 (tron.c video_pass), so a CI4 one's texels were taken as its
// palette and the rest of the buffer -- an older frame's -- as its texels. Such a frame is made CI8 where it was read: each
// texel its own byte (the high nibble the left texel, as the RDP reads CI4), the 16 colours kept, the palette's other 240
// zero (no texel names them); its header then says CI8. A CI8 frame is left as it is.
static void frame_make_ci8(u32 d) {
    if (RD32(d) != TEX_MAGIC || RD16(d + TEX_WIDTH) != 64u || RD16(d + TEX_HEIGHT) != 64u || RD8(d + TEX_BITS) != 4u ||
        RD16(d + TEX_COLOURS) != 16u) return;
    const u32 from = d + TEX_HEAD + 16u * 2u, to = d + TEX_HEAD + 256u * 2u;   // (the texels after 16 colours, after 256)
    // (from the last byte back: each one's two texels land at or past it, on bytes already read)
    for (u32 i = FRAME_TEXELS / 2u; i-- > 0u; ) {
        const u8 b = RD8(from + i);
        RD8(to + 2u * i) = (u8)(b >> 4);
        RD8(to + 2u * i + 1u) = (u8)(b & 0x0Fu);
    }
    for (u32 a = from; a < to; a++) RD8(a) = 0;
    RD8(d + TEX_BITS) = 8u;
    RD16(d + TEX_COLOURS) = 256u;
}
// Video v's entry k: its frame (a 64 x 64 CI8 TEX, CT_FRAME_SIZE) into dest, its time (game frames); 0 past
// the list's end. The packs' frames (No Mercy Plus's, the ported Superstars'), else the game's own read from
// the ROM -- only where rom says the thread may (the main thread's file reads) -- made CI8 if it is CI4.
int media_tron_frame(int v, int k, u32 dest, int rom, int* time) {
    if (k < 0) return 0;
    if (is_folder_video(v)) {        // (The arena's big screen plays all of it.)
        if ((u32)k >= folder_videos[v - FOLDER_VIDEO0].frames) return 0;
        folder_video_frame(v, k, dest);
        *time = 4;
        return 1;
    }
    if (port_video(v)) {
        const u16 (*w)[2] = wmp_video[v - WMP_VIDEO0];
        if (k >= WMP_VIDEO_LEN || w[k][0] == 0xFFFF || w[k][0] >= WMP_VIDEO_FRAMES) return 0;
        u32 f = wmp_video_frames[v - WMP_VIDEO0][w[k][0]];
        if (!f) return 0;
        plat_wmport_read(wmp_files[f - WMP_FILE0][0], dest, CT_FRAME_SIZE);
        *time = w[k][1] & 0xFFF;
        return 1;
    }
    if (v < 1 || v > CT_VIDEOS) return 0;
    if (video_wanted(v)) {
        if (k >= CT_VIDEO_FRAMES) return 0;
        plat_content_read(CONTENT_FRAMES + (u32)ct_video[v - 1][k] * CT_FRAME_SIZE, dest, CT_FRAME_SIZE);
        *time = 4;
        return 1;
    }
    if (!entrance_table_loaded()) return 0;
    u32 list = RD32(NM_VIDEO_LIST(v));
    if (!ram_ptr(list) || list == (u32)tv_list[v]) return 0;
    s32 file = RDS16(list + 4u * (u32)k);
    if (file <= 0) return 0;
    *time = RDS16(list + 4u * (u32)k + 2) & 0xFFF;
    if (!rom) return 2;                  // (there, not read)
    u32 info[4];
    files_read((u32)info, (u32)file, dest);
    frame_make_ci8(dest);
    return 1;
}
