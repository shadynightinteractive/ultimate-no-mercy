// ri_wmmedia.h (phase3 p3wmmedia, 2026-11-19): a WrestleMania 2000 (or Badd Blood) man's entrance theme and Titantron as
// folder files -- the C++ twin of tools\handlers\handlers\baddblood\game.py theme / theme_sound / theme_rate / theme_folder /
// video_list / video (which the WrestleMania 2000 handler, tools\handlers\wm2k_import.py write_theme_video, and imports2.py's
// Badd Blood men both call), giving the same bytes.
//
// Inputs, all from the player's ROM: the ROM itself (the music bank's samples), its AKI files (the video frames) and the
// game's RAM at its Superstar Select (ri_images make_image("wm_sel1"): the music bank's records and the entrance table).
//
// The theme is the game's own sound re-made for the PC player, which plays every theme.bin at 10787 samples a second:
// decoded (ri_vadpcm), resampled (scipy.signal.resample_poly with a Kaiser window, ported here exactly: the FIR design,
// the polyphase filter and the trimming, in float64 in the same order), levelled (mod\gen\mp3theme.py loudness / limit)
// and encoded again with a codebook of its own (ri_themes theme_encode = tools\wrestler_theme.py's way).
#pragma once
#include "ri_roms.h"
#include "ri_json.h"

namespace ri {

// ---- the theme ------------------------------------------------------------------------------------------------------
// game.py theme_sound: the music bank sound the entrance of music slot `slot` plays (the entrance table at 0x8010206C in
// the select screen's RAM, s16 at +8 of the slot's 12 bytes). < 0: the slot plays no sound (the folder then has no theme).
int wm_theme_sound(const Bytes& sel_ram, int slot);

struct FolderTheme {
    Bytes bin, meta;     // theme.bin (VADPCM) and theme.meta (the 0xD0-byte wave record + level, loops, tune)
    ojson about;         // the dict wrestler.json's "source"."theme" holds (theme_folder's about, keys in its order)
};

// What theme_folder computes on the way (for the developer's step-by-step checks only; the game passes nullptr).
struct WmThemeSteps {
    std::vector<double> pcm;       // the decoded sound, cut at its loop end
    std::vector<double> h;         // firwin's filter (before resample_poly's gain and padding)
    std::vector<double> y;         // resampled and cut to whole 16-sample frames
    std::vector<double> y2;        // levelled and limited
    double src_rate = 0, med = 0;
    int up = 0, down = 0, ls = 0;
};

// game.py theme_folder(rom, ram, slot): the theme of music slot `slot` (a record's music byte) as a folder's files. Error
// when the slot plays no sound (check wm_theme_sound first, as write_theme_video does).
FolderTheme wm_theme_folder(const Bytes& rom, const Bytes& sel_ram, int slot, WmThemeSteps* steps = nullptr);

// wm2k_import.py write_theme_video's text for "source"."theme" when the slot plays no sound
std::string wm_theme_none(int slot);

// ---- the video ------------------------------------------------------------------------------------------------------
struct FolderVideo {
    Bytes bin;           // video.bin: 64 x 64 CI8 TEX frames (16 + 512 + 4096 bytes each), 7.5 a second
    ojson about;         // the dict wrestler.json's "source"."video" holds
};
// game.py video(files, ram, v): video v (a record's video byte, not 0) from its frame list in the select screen's RAM.
FolderVideo wm_video(const RomFiles& files, const Bytes& sel_ram, int video);

// wm2k_import.py write_theme_video's text for "source"."video" when the record's video is 0
std::string wm_video_none();

// ---- the numeric twins (exposed for the checks) ---------------------------------------------------------------------
// numpy's pairwise sum of n doubles (np.sum / np.add.reduce along a contiguous axis)
double np_pairwise_sum(const double* a, size_t n);
// the exp the reference's scipy.special.i0 calls (MinGW-w64's x87 exp, precision control 53 bits), in software
double mingw_exp(double x);
double mingw_exp_soft(double x);      // (its software steps: the path off x86-64)
// scipy.signal.firwin(numtaps, cutoff, window=("kaiser", beta)) for a low-pass (pass_zero, scale): the filter's taps
std::vector<double> sp_firwin_kaiser(int numtaps, double cutoff, double beta);
// scipy.signal.resample_poly(x, up, down, window=("kaiser", beta)) (padtype "constant", cval 0) on float64 x
std::vector<double> sp_resample_poly_kaiser(const std::vector<double>& x, int64_t up, int64_t down, double beta);

}  // namespace ri
