// [commentary2 2026-11-18] The commentary pack made from the player's own WWE '13 (Wii) disc: every clip of the recipe
// found on the disc (Wwise packages, .pck), decoded (wwise_decode), trimmed and encoded (comm_encode) exactly as the
// developer's tools did (tools\commentary\rebuild.py), then the pack written (comm_pack). No Python, no other program.
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "comm_pack.h"

namespace comm_build {

// Where the disc's Wwise media are: a disc image (.rvz / .wia, through wii_disc) or a folder of the disc's files.
class Media {
public:
    // false + why when it can't be read, or holds none of the recipe's media
    static std::unique_ptr<Media> open(const std::filesystem::path& source, std::string& why);
    virtual ~Media() = default;
    virtual bool has(uint32_t id) const = 0;
    // the .wem's bytes (thread-safe: each call reads on its own)
    virtual bool read(uint32_t id, std::vector<uint8_t>& out, std::string& why) const = 0;
    virtual size_t count() const = 0;           // media found
    virtual std::string game_id() const = 0;    // the disc's 6-character id ("" for a folder)
};

struct BuildReport {
    size_t identical = 0;    // the same bytes as the developer's clip
    size_t equivalent = 0;   // the same line, not the same bytes (length within 2 Vorbis blocks, loudness within 1 dB)
    size_t wrong = 0;        // made, but not that line: left out
    size_t missing = 0;      // not on this disc / not placed in the recipe yet: left out
    size_t clips = 0, lines = 0;
    std::string pack_sha1;
    bool same_as_target = false;
    std::vector<std::string> notes;   // per clip that isn't identical (a few), and anything else worth saying
};

// How many of the recipe's media this source has (to tell a WWE '13 disc from another).
size_t recipe_media_found(const Recipe& r, const Media& m, size_t& needed);

// Makes the pack. threads: workers (>= 1); progress(done, total) from any thread; stop: set to give up;
// thread_start: run first on each worker thread (the game: low priority).
bool build_pack(const Media& media, const Recipe& r, std::vector<uint8_t>& pack, BuildReport& rep, std::string& why,
                int threads, const std::function<void(size_t, size_t)>& progress, const std::atomic<bool>* stop,
                void (*thread_start)() = nullptr);

// One clip (exposed for the tests): its bytes and how it came out ("identical", "equivalent", "wrong", "missing").
std::string make_clip(const Media& media, const Recipe& r, const RecipeClip& c, std::vector<uint8_t>& out, std::string& note);

}  // namespace comm_build
