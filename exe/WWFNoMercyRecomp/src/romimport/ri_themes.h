// ri_themes.h (phase3, 2026-11-19): entrance themes for wrestler folders and the content pack -- the C++ twin of
// tools\handlers\handlers\revenge\theme.py (encode, theme_files, redux_files) and mod\gen\reduxmusic.py's tables
// (PORTS, theme_list, song_for, song_of, code_of_folder). The sound itself is revplay.cpp's (Revenge's own sequencer and
// synthesizer, Redux's clips) and ri_vadpcm's (VADPCM as the SDK tools make it).
#pragma once
#include "ri_roms.h"
#include <optional>

namespace revplay {
typedef std::vector<uint8_t> Bytes;
struct Result { std::vector<int16_t> pcm; int64_t loop_start = 0; double rate = 0; int64_t S = 0, U = 0, tempo = 0, Lr = 0; };
bool theme(const Bytes& rom, int file, Result& res, std::string& err);       // Revenge's song file (0x214 ...)
bool clip_theme(const Bytes& rom, int song, Result& res, std::string& err);  // Revenge Redux's song (0x18 .. 0x41)
}

namespace ri {

// A folder theme: theme.bin (the VADPCM data), theme.meta (the 0xD0-byte wave record, level 0, loops, 2 spare, tune
// 0xC2400000), its length in samples, and the report's words for it.
struct ThemeFiles { Bytes bin, meta; uint32_t samples = 0; std::string about; };
// theme.py encode: a theme sample (PCM, loop start, rate) as a folder's two files
ThemeFiles theme_encode(const std::vector<int16_t>& pcm, int64_t loop_start, double rate);
ThemeFiles revenge_theme_files(const Bytes& revenge_rom, int song_file = 0x214);   // theme.py theme_files
ThemeFiles redux_theme_files(const Bytes& redux_rom, int song);                    // theme.py redux_files

// reduxmusic.py's tables
struct ReduxPort { const char* name; std::vector<const char*> folder_ids; uint32_t code; int attire; };
const std::vector<ReduxPort>& redux_ports();
std::string norm_name(const std::string& s);                  // mp3theme.norm: lower case, letters and digits only
std::optional<int> redux_song_for(const Bytes& redux_rom, uint32_t code);          // song_for
std::optional<int> redux_song_of(const Bytes& redux_rom, const std::string& name); // song_of
std::optional<uint32_t> redux_code_of_folder(const std::string& fid, const std::string& source_code);   // code_of_folder

}  // namespace ri
