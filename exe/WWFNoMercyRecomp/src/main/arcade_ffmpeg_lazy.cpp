// [xplat 2026-11-12] FFmpeg loaded on first use on Linux, the Steam Deck and the Mac: see arcade_ffmpeg_lazy.h for why and how.
// This file is the only one that sees FFmpeg's real function names; everything else calls through the table.
#if defined(ARCADE_FFMPEG_LAZY) && !defined(_WIN32)
#define ARCADE_FFMPEG_NO_REDIRECT
#include <dlfcn.h>
#include <cstdio>
#include <filesystem>
#include <mutex>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/version.h>
#include <libavcodec/version.h>
#include <libavformat/version.h>
#include <libswresample/version.h>
#include <libswscale/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include "arcade_ffmpeg_lazy.h"
#include "arcade_platform.h"

namespace arcade_ffmpeg_lazy {
Table table;

namespace {
// A library's file name the way this system writes it, with the version number FFmpeg's own headers (the ones this build
// was compiled against) give it: libavutil.so.60 on Linux, libavutil.60.dylib on the Mac.
std::string file_name(const char* base, const char* major) {
#if defined(__APPLE__)
    return std::string("lib") + base + "." + major + ".dylib";
#else
    return std::string("lib") + base + ".so." + major;
#endif
}

// Beside the game first (lib/, as the release tarball lays it out), then wherever the system's loader looks (the game's
// RPATH, LD_LIBRARY_PATH, /usr/lib ...). The first failure's message is kept.
void* open_one(const std::string& name, std::string& why) {
    std::error_code ec;
    const auto beside = arcade::platform::executable_path().parent_path() / "lib" / name;
    if (std::filesystem::exists(beside, ec)) {
        if (void* h = dlopen(beside.c_str(), RTLD_NOW | RTLD_LOCAL)) return h;
        const char* e = dlerror(); why = e ? e : name;
        return nullptr;
    }
    if (void* h = dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL)) return h;
    const char* e = dlerror(); why = e ? e : name;
    return nullptr;
}
}  // namespace

bool load(std::string& why) {
    // (in the order they need each other: a library finds the ones it needs already loaded, by name)
    const std::string names[] = {
        file_name("avutil", AV_STRINGIFY(LIBAVUTIL_VERSION_MAJOR)), file_name("swresample", AV_STRINGIFY(LIBSWRESAMPLE_VERSION_MAJOR)),
        file_name("swscale", AV_STRINGIFY(LIBSWSCALE_VERSION_MAJOR)), file_name("avcodec", AV_STRINGIFY(LIBAVCODEC_VERSION_MAJOR)),
        file_name("avformat", AV_STRINGIFY(LIBAVFORMAT_VERSION_MAJOR)) };
    void* handles[5] = {};
    for (int i = 0; i < 5; i++) {
        handles[i] = open_one(names[i], why);
        if (!handles[i]) { why = names[i] + " isn't in lib/ beside the game or won't load (" + why + ")"; return false; }
    }
    // Each function is looked up in the five (a name lives in one of them; asking all keeps this list free of which).
    std::string missing;
#define ARCADE_FFMPEG_LOOKUP(name) { void* p = nullptr; for (void* h : handles) if ((p = dlsym(h, #name))) break; \
        table.name = reinterpret_cast<decltype(table.name)>(p); if (!p) missing += std::string(missing.empty() ? "" : ", ") + #name; }
    ARCADE_FFMPEG_FUNCTIONS(ARCADE_FFMPEG_LOOKUP)
#undef ARCADE_FFMPEG_LOOKUP
    if (!missing.empty()) { why = "these libraries lack " + missing + " (a different FFmpeg than the game was built for)"; table = Table{}; return false; }
    return true;
}
}  // namespace arcade_ffmpeg_lazy
#endif
