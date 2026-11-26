// ri_content.h (phase3, 2026-11-19): the arcade's content pack -- the C++ twin of mod\gen\mkcontent.py: content.bin (the
// entrance themes' samples, then the Titantron videos' 64 x 64 CI8 frames) and content_index.h (its index, compiled into the
// mod: its TEXT, which ri_hdrfill turns into the mod's bytes).
//
// From: No Mercy Plus V3 (its music bank's theme instruments and scripts, its entrance table, its Titantron frames), WrestleMania
// 2000 (Jeff Jarrett's and The Blue Meanie's themes from its music bank), WCW/nWo Revenge (its song 0x214, played by revplay),
// Revenge Redux (the ports' themes, when the player has the patch), the No Mercy ROM (the game's song table).
// The player's own music files (mod\music in the developer's build: gen/mp3theme.py, ffmpeg + scipy) are NOT made here: a
// player's game has no such files, so the native pack is the no-music-files configuration (the Python with an empty mod\music
// gives the same bytes).
#pragma once
#include "ri_roms.h"

namespace ri {

struct ContentPack { Bytes bin; std::string index_h; std::vector<std::string> log; };
// roster_base_json: mod\gen\roster_base.json's text (ours); roster_ext_h: the roster_ext.h text (mkroster's: the added
// Superstars' names, which the music files' and Redux songs' names are matched against); maps: tools\romimport\loadmaps.
// Error when the No Mercy ROM isn't there.
// (nmplus 2026-11-19) user_music_rows: the build's count of user_music rows (0: as many as made) -- without Revenge Redux the
// rows past the ones made are { 0xFFFF, 0 } (no one's), not zeros (Superstar 0x0000's)
// (content2 2026-11-19) Made from the sources there: No Mercy Plus V3's themes and videos with it, WrestleMania 2000's two
// themes with it, Revenge's 0x214 and Redux's songs with them; the added themes play through OUR sound script (THEME_SCRIPT,
// the mod's media.c theme_script). Without No Mercy Plus, shape gives the build's layout -- its rows (songs) and made music
// slots (ext_slots) -- so every row is where the build has it, an absent source's empty.
struct ContentShape { size_t songs = 0, ext_slots = 0; };
ContentPack make_content(Roms& R, const fs::path& maps, const std::string& roster_base_json, const std::string& roster_ext_h,
                         size_t user_music_rows = 0, const ContentShape* shape = nullptr);

}  // namespace ri
