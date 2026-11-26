// Entry point for WWF No Mercy: Recompiled.
// Minimal launcher: sets up SDL + RT64 (via recompui) + the runtime callbacks, registers
// the game, auto-loads the ROM (no UI launcher yet), and calls recomp::start.
// Adapted from Bomberman Hero: Recompiled's src/main/main.cpp.
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <array>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <fstream>
#include <thread>
#include <set>
#include <chrono>
#include <cctype>

#define SDL_MAIN_HANDLED
#include "SDL.h"
#ifdef _WIN32
#include "SDL_syswm.h"
#endif

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "arcade_host.h"
#include "arcade_startup_status.h"   // (startup 2026-11-19)
#include "arcade_platform.h"
#include "nm_replay.h"                     // [replay 2026-11-14]
#include "ultramodern/lockstep.hpp"        // [replay 2026-11-14]
#include "recompinput/recompinput.h"
#include "recompinput/profiles.h"
#include "recompinput/input_events.h"
#include "recompinput/input_state.h"
#include "recompinput/players.h"
#include "librecomp/game.hpp"
#include "librecomp/rsp.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "librecomp/mods.hpp"   // (write_hook_counts, as a test run ends)
#include "librecomp/files.hpp"  // (mods.json written with its backup, as librecomp does: repair_bundled_mods_config)
#include "json/json.hpp"
#include <iomanip>
#include <span>
#include "ovl_patches.hpp"
#ifdef _WIN32
#include "renderdoc_app.h" // [wcw] RenderDoc in-app capture API (diagnostic)
#endif

// (the game has stopped: a thread caught in the runtime's own teardown faults now and then -- nothing is left to
// lose, so the process just ends, no report)
static std::atomic<bool> g_game_stopped{false};
extern std::atomic_int wcw_viswaps_total;   // (ultramodern events.cpp: every game frame since boot)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib") // timeBeginPeriod
#include <io.h>
#include <fcntl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#ifndef _MSC_VER
#include <guiddef.h>
#include <dxguids.h> // (MinGW: the D3D12 interfaces' IDs for __uuidof)
#endif
#include <dbghelp.h>
#include <exception>
#include <cstdlib>
#pragma comment(lib, "dbghelp.lib")

// DIAGNOSTIC: print one resolved stack frame (module!symbol+off or module+off).
static void wcw_print_frame(HANDLE proc, int i, DWORD64 addr) {
    char symbuf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    char modname[MAX_PATH] = "?";
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(mod, full, MAX_PATH)) {
            const char* b = strrchr(full, '\\'); strncpy(modname, b ? b + 1 : full, MAX_PATH - 1);
        }
    }
    DWORD64 disp = 0;
    if (SymFromAddr(proc, addr, &disp, sym)) {
        fprintf(stderr, "  #%2d  %-20s  %s+0x%llX  (0x%llX)\n", i, modname, sym->Name, (unsigned long long)disp, (unsigned long long)addr);
    } else {
        DWORD64 modbase = mod ? (DWORD64)mod : 0;
        fprintf(stderr, "  #%2d  %-20s  +0x%llX  (0x%llX)\n", i, modname, (unsigned long long)(addr - modbase), (unsigned long long)addr);
    }
}

// DIAGNOSTIC: dump a backtrace + in-flight C++ exception when std::terminate fires.
static void wcw_terminate_handler() {
    fprintf(stderr, "\n==== [wcw] std::terminate called ====\n");
    if (auto ep = std::current_exception()) {
        try { std::rethrow_exception(ep); }
        catch (const std::exception& e) { fprintf(stderr, "[wcw] in-flight exception: %s: %s\n", typeid(e).name(), e.what()); }
        catch (...) { fprintf(stderr, "[wcw] in-flight exception: non-std type\n"); }
    } else {
        fprintf(stderr, "[wcw] no in-flight C++ exception (pure-virtual call / explicit abort?)\n");
    }
    void* frames[64];
    USHORT n = RtlCaptureStackBackTrace(0, 64, frames, nullptr);
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    for (USHORT i = 0; i < n; i++) wcw_print_frame(proc, i, (DWORD64)frames[i]);
    fprintf(stderr, "==== [wcw] end backtrace ====\n");
    fflush(stderr);
    _exit(2);
}

// DIAGNOSTIC: walk the faulting thread's stack for hard faults (access violations etc.),
// which don't go through std::terminate.
void crash_report_crash(EXCEPTION_POINTERS* ep);
void crash_report_init(const std::filesystem::path& app_folder);
void crash_report_set_rdram(uint8_t* rdram);
void crash_report_shutdown();
void crash_report_qa_snapshot();
static LONG WINAPI wcw_unhandled_filter(EXCEPTION_POINTERS* ep) {
    if (g_game_stopped.load()) { fflush(stderr); TerminateProcess(GetCurrentProcess(), 0); }
    crash_report_crash(ep);   // [arcade] crash_reports\crash_<date>_<time>.txt + the game's RAM
    fprintf(stderr, "\n==== [wcw] unhandled exception 0x%08lX at %p ====\n",
        (unsigned long)ep->ExceptionRecord->ExceptionCode, (void*)ep->ExceptionRecord->ExceptionAddress);
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        fprintf(stderr, "[wcw]   access %s address 0x%llX\n",
            ep->ExceptionRecord->ExceptionInformation[0] ? "WRITE" : "READ",
            (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }
    HANDLE proc = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(proc, nullptr, TRUE);
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 64; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        if (sf.AddrPC.Offset == 0) break;
        wcw_print_frame(proc, i, sf.AddrPC.Offset);
    }
    fprintf(stderr, "==== [wcw] end backtrace ====\n");
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// ---- the six-man engine (No Mercy six-man build) ---------------------------------------------------------------
// Its rewritten functions and stubs are recompiled from the six-man ROM's "rwblob" section (vram 0x80780000); the
// blob's DATA (tables, the match's wrestler count, ...) must also be in RDRAM there, and the match overlay's BSS
// grows (the overlay table's ovl_e entry: BSS end 0x8016B9F0 -> SIX_OVLE_BSS_END, the moved per-wrestler arrays). The
// player's ROM is the stock one, so both go into RDRAM after the boot load, before the game's entrypoint runs.
#include "six_blob.h"
#include "six_rompatch.h"
#include "six_layout.h"   // (SIX_OVLE_BSS_END: 0x80176000 for six slots, more for a wider layout)
#include "arcade_audio_filter.h"   // [audiotable 2026-11-18]
#include "arcade_vi_mode.h"        // [relclean 2026-11-19]

// [audiotable 2026-11-18] The sound resampler's filter (arcade_audio.cpp) is read from the player's ROM, not carried in the
// program: No Mercy (USA) Rev 1 or v1.0, its 512 bytes checked (arcade_audio_filter.cpp). A ROM without it stops the game
// here with a message: the sound would be wrong, and a filter of zeros would be silence.
static void arcade_audio_filter_or_quit(std::span<const uint8_t> rom) {
    std::string why;
    uint32_t at = 0;
    if (arcade_audio_filter_from_rom(rom.data(), rom.size(), why, &at)) {
        fprintf(stderr, "[audio] the resampler's filter read from the ROM at 0x%X\n", at);
        return;
    }
    fprintf(stderr, "[audio] the resampler's filter: %s\n", why.c_str());
    fflush(stderr);
    const std::string text = "The sound filter could not be read from the WWF No Mercy ROM: " + why +
        ".\n\nThis needs the WWF No Mercy (USA) ROM (Rev 1), unchanged. Put it in the game's roms folder and start it again.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ultimate No Mercy", text.c_str(), nullptr);
    exit(1);
}

// [relclean 2026-11-19] The runtime's stand-in video mode (the picture before the game starts: libultra's NTSC LAN1 mode, the
// SDK's data) is read from the player's ROM, not carried in the program: No Mercy (USA) Rev 1 or v1.0, its 80 bytes checked
// (arcade_vi_mode.cpp), given to the runtime before it starts (ultramodern set_dummy_vi_mode). The ROM chosen above is loaded
// for it now (the runtime loads it again when the game starts). A ROM without it stops the game here with a message.
static void arcade_vi_mode_or_quit(std::u8string& game_id) {
    std::string why;
    uint32_t at = 0;
    OSViMode mode{};
    if (!recomp::load_stored_rom(game_id)) why = "the ROM could not be loaded";
    else if (arcade_vi_mode_from_rom(recomp::get_rom().data(), recomp::get_rom().size(), mode, why, &at)) {
        ultramodern::set_dummy_vi_mode(mode);
        fprintf(stderr, "[vi] the stand-in video mode read from the ROM at 0x%X\n", at);
        return;
    }
    fprintf(stderr, "[vi] the stand-in video mode: %s\n", why.c_str());
    fflush(stderr);
    const std::string text = "The video mode could not be read from the WWF No Mercy ROM: " + why +
        ".\n\nThis needs the WWF No Mercy (USA) ROM (Rev 1), unchanged. Put it in the game's roms folder and start it again.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ultimate No Mercy", text.c_str(), nullptr);
    exit(1);
}

static void six_engine_init(uint8_t* rdram, recomp_context* ctx) {
    (void)ctx;
#ifdef _WIN32
    crash_report_set_rdram(rdram);
#endif
    arcade_audio_filter_or_quit(recomp::get_rom());   // [audiotable 2026-11-18] (before anything here changes the ROM)
    // The ROM the runtime holds becomes the six-man game's: mods' hooked functions are recompiled live from it (they
    // must be the six-man code, not the stock code) and the overlays are read from it. Only a stock ROM is changed.
    {
        std::span<const uint8_t> cur = recomp::get_rom();
        std::vector<uint8_t> rom(cur.begin(), cur.end());
        size_t applied = 0, mismatched = 0;
        // [portable 2026-11-15] The table holds the six-man words only, no stock word (that is the ROM's own data, and nothing the
        // game ships may hold any: the user, 2026-11-14). Each word the six-man engine changes is read from the player's ROM, and
        // a hash of them all (six_rom_stock_fnv: FNV-1a 64 over the stock words in table order -- a number, no ROM bytes) says
        // whether it is the stock ROM the table was made against. The stock ROM: every word applied, as before. Any other ROM: a
        // word is changed only where it already is the six-man word's (it was: where it was neither, it was left), so a ROM
        // that is not the stock one is still never patched blind. (The runtime takes only the Rev 1 ROM anyway: select_rom.)
        auto rom_word = [&](uint32_t off) {
            const uint8_t* b = cur.data() + off;
            return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
        };
        uint64_t fnv = 0xCBF29CE484222325ull;
        bool fits = true;
        for (const auto& p : six_rom_patch) {
            if (p[0] + 4 > cur.size()) { fits = false; break; }
            const uint32_t w = rom_word(p[0]);
            for (int k = 3; k >= 0; k--) { fnv ^= (w >> (8 * k)) & 0xFF; fnv *= 0x100000001B3ull; }
        }
        const bool stock = fits && fnv == six_rom_stock_fnv;
        for (const auto& p : six_rom_patch) {
            if (p[0] + 4 > rom.size()) { mismatched++; continue; }
            uint8_t* b = rom.data() + p[0];
            uint32_t w = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
            if (!stock && w != p[1]) { mismatched++; continue; }
            b[0] = (uint8_t)(p[1] >> 24); b[1] = (uint8_t)(p[1] >> 16); b[2] = (uint8_t)(p[1] >> 8); b[3] = (uint8_t)p[1];
            applied++;
        }
        // The boot load (ROM 0x1000, 1 MB, to 0x80000400) already ran from the stock ROM: the main segment's data in
        // RDRAM gets the same changes (as the six-man game's boot would have loaded it).
        size_t boot_words = 0;
        for (const auto& p : six_rom_patch) {
            if (p[0] < 0x1000 || p[0] >= 0x101000) continue;
            uint32_t* w = reinterpret_cast<uint32_t*>(rdram + (0x80000400u + (p[0] - 0x1000) - 0x80000000u));
            // ([portable 2026-11-15] the stock word: the player's ROM's own at that place, read before the ROM is changed)
            if (*w == rom_word(p[0]) || *w == p[1]) { *w = p[1]; boot_words++; }
        }
        fprintf(stderr, "[six] ROM: %zu words of the six-man engine, %zu not as expected; boot image %zu words\n",
            applied, mismatched, boot_words);
        recomp::set_rom_contents(std::move(rom));
    }
    for (size_t i = 0; i < sizeof(six_blob_words) / sizeof(six_blob_words[0]); i++)
        *reinterpret_cast<uint32_t*>(rdram + (six_blob_vram - 0x80000000u) + i * 4) = six_blob_words[i];
    uint32_t* bss_end = reinterpret_cast<uint32_t*>(rdram + (0x80052E2Cu - 0x80000000u));
    if (*bss_end == 0x8016B9F0u) *bss_end = SIX_OVLE_BSS_END;
    // The blob's functions must be in the runtime's function map like a loaded overlay's: code the runtime
    // recompiles live (mods' hooked functions) reaches them through a lookup by address ("Failed to find function").
    // The game never DMAs it (it is put in RDRAM here), so it is registered here, at its ROM and RAM place.
    load_overlays(0x01FF0000u, (int32_t)six_blob_vram, (uint32_t)(sizeof(six_blob_words)));
}

#ifdef _WIN32
#include <tlhelp32.h>

// DIAGNOSTIC: after a delay, suspend every other thread and print its stack, to find where
// the game threads are parked when the boot stalls (no crash, no progress).
static void wcw_sample_all_threads() {
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    DWORD me = GetCurrentThreadId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    DWORD pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
            HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (!th) continue;
            SuspendThread(th);
            CONTEXT ctx{}; ctx.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(th, &ctx)) {
                STACKFRAME64 sf{};
                sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
                sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
                sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
                fprintf(stderr, "[wcw][sample] thread %lu:\n", (unsigned long)te.th32ThreadID);
                for (int i = 0; i < 16; i++) {
                    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, th, &sf, &ctx, nullptr,
                                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                    if (sf.AddrPC.Offset == 0) break;
                    wcw_print_frame(proc, i, sf.AddrPC.Offset);
                }
            }
            ResumeThread(th);
            CloseHandle(th);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    fprintf(stderr, "[wcw][sample] done\n");
    fflush(stderr);
}
#endif

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

static const recomp::Version version { 0, 2, 0, "" };

template <typename... Ts>
[[noreturn]] static void exit_error(const char* str, Ts... args) {
    fprintf(stderr, str, args...);
    assert(false);
    ultramodern::error_handling::quick_exit(__FILE__, __LINE__, __FUNCTION__);
}

// (firstrun 2026-11-17) The first start's builder is native now: arcade_firstrun.cpp, called in main before the bundled mods
// are installed. (portable's 2026-11-15 hook ran a bundled Python's firstrun\firstrun.py when firstrun\plan.json was there;
// that builder was never brought into the project, and no player needs Python.)
void arcade_firstrun_build();
bool arcade_commentary_will_build();   // (startup 2026-11-19: commentary/arcade_commentary_build.cpp)
// The six-man engine's words (six_rompatch.h), for the first start's builder: the mod's patched copies are of the six-man ROM.
std::span<const uint32_t[2]> arcade_six_rom_patch() { return six_rom_patch; }

#if NOMERCY_STANDALONE
// [standalone] The ROM, without the launcher's "Load ROM": the copy stored on an earlier run (wwf.nomercy.us.z64 in the
// app folder) or a ROM file put in the game's folder (any .z64 / .n64 / .v64; it's validated and stored the first time,
// as the launcher's Load ROM does). Without one: a message saying where to put it, and the program ends.
static void standalone_select_rom(std::u8string& game_id) {
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path app_folder = arcade::app_folder();
    const std::filesystem::path stored = app_folder / (game_id + u8".z64");
    std::error_code ec;
    if (std::filesystem::is_regular_file(stored, ec)) {
        candidates.push_back(stored);
    }
    // ([portable 2026-11-15] and the roms folder beside the program, where the portable game's README says to put it)
    for (const std::filesystem::path& folder : { arcade::program_folder(), arcade::program_folder() / "roms", app_folder }) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
            std::u8string ext = entry.path().extension().u8string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char8_t c) { return (char8_t)std::tolower((unsigned char)c); });
            if (!entry.is_regular_file(ec) || (ext != u8".z64" && ext != u8".n64" && ext != u8".v64")) {
                continue;
            }
            if (std::find(candidates.begin(), candidates.end(), entry.path()) == candidates.end()) {
                candidates.push_back(entry.path());
            }
        }
    }
    bool wrong_version = false;
    for (const std::filesystem::path& rom : candidates) {
        const recomp::RomValidationError err = recomp::select_rom(rom, game_id);
        fprintf(stderr, "[standalone] ROM %s: %d\n", arcade::shown(rom).c_str(), (int)err);   // (privacy 2026-11-13: relative to the game folder)
        if (err == recomp::RomValidationError::Good) {
            return;
        }
        wrong_version |= (err == recomp::RomValidationError::IncorrectVersion);
    }
    const std::wstring folder = arcade::program_folder().wstring();
    const std::wstring text = (wrong_version
        ? std::wstring(L"The WWF No Mercy ROM in this folder isn't the version this needs: WWF No Mercy (USA) (Rev 1).\n\n")
        : std::wstring(L"This needs the WWF No Mercy (USA) (Rev 1) ROM.\n\n")) +
        L"Put the ROM file (.z64) in the game's roms folder (or the game's folder) and start it again:\n" + folder;
#ifdef _WIN32
    MessageBoxW(nullptr, text.c_str(), L"Ultimate No Mercy", MB_OK | MB_ICONINFORMATION);   // ([portable 2026-11-15] the name the player sees)
#else
    // ([xplat 2026-11-12] elsewhere the same words through SDL's box: the folder as UTF-8)
    const std::string utext = std::string(wrong_version
        ? "The WWF No Mercy ROM in this folder isn't the version this needs: WWF No Mercy (USA) (Rev 1).\n\n"
        : "This needs the WWF No Mercy (USA) (Rev 1) ROM.\n\n") +
        "Put the ROM file (.z64) in the game's roms folder (or the game's folder) and start it again:\n" + arcade::platform::utf8(arcade::program_folder());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Ultimate No Mercy", utext.c_str(), nullptr);
#endif
    fprintf(stderr, "[standalone] no ROM\n");
    exit(1);
}
#endif

// ---------------- gfx / window ----------------
// These globals are referenced by recompui/recompinput, so they must be non-static.
SDL_Window* window = nullptr;
extern "C" int n64recomp_live_symbolize(uintptr_t a, char* out, size_t sz);   // (N64Recomp live_generator.cpp)
uint8_t* g_ww_rdram = nullptr;   // rdram base for the [nomercy][ww] write-watch diagnostic
std::atomic_bool g_mw_stop{ false }, g_mw_reading{ false };   // (the QA write-watch's thread: stopped before the RAM is freed)
std::vector<recomp::GameEntry> supported_games;
void arcade_host_register_exports();
std::unique_ptr<ultramodern::renderer::RendererContext> arcade_view_make_renderer();

ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) > 0) {
        exit_error("Failed to initialize SDL2: %s\n", SDL_GetError());
    }
    fprintf(stdout, "SDL Video Driver: %s\n", SDL_GetCurrentVideoDriver());
    return {};
}

ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    fprintf(stderr, "[wcw] create_window\n");
    uint32_t flags = SDL_WINDOW_RESIZABLE;
#if defined(RT64_SDL_WINDOW_VULKAN) || defined(ARCADE_VIEW_VULKAN)
    flags |= SDL_WINDOW_VULKAN;      // [xplat 2026-11-12] ARCADE_VIEW_VULKAN (CMake, Linux): the picture is drawn into this window by Vulkan (arcade_view_vulkan.cpp)
#endif
    if (nm_headless()) flags |= SDL_WINDOW_HIDDEN;     // [replay 2026-11-14] the no-graphics mode: the window is there (its events, the pads) but never shown
#if NOMERCY_STANDALONE
    window = SDL_CreateWindow("Ultimate No Mercy",   // ([portable 2026-11-15] the name the player sees)
#else
    window = SDL_CreateWindow("WWF No Mercy: Recompiled",
#endif
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960, flags);
    if (window == nullptr) {
        exit_error("Failed to create window: %s\n", SDL_GetError());
    }
#if defined(_WIN32)
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#else
    return ultramodern::renderer::WindowHandle{ window };
#endif
}

// Test aid: WCW_VPADS=<n> attaches n SDL virtual gamepads that each pulse one distinct
// button (pad 1: A/South, pad 2: B/East, pad 3: X/West, pad 4: Y/North) every 2 seconds.
// Combined with WCW_INPUT_LOG=1 (librecomp si.cpp) this verifies per-player input
// separation end to end without physical controllers.
//
// WCW_VPADS_SCRIPT=<file> replaces the pulses with scripted presses, for reproducible multi-player
// runs (e.g. two players into a match). One press per line: `<seconds> <pad 1-4> <button> [hold ms]`,
// seconds counted from the first frame; buttons are SDL game controller names (a, b, x, y, start,
// back, dpup, dpdown, dpleft, dpright, leftshoulder, rightshoulder, ...; lefttrigger / righttrigger -- the N64's
// L / R -- pulled all the way); `#` starts a comment.
// A line `<seconds> repeat <period seconds>` loops the presses after <seconds> from there on.
// Hot-plug: `<seconds> plug <pad>` / `<seconds> unplug <pad>` attach / detach a pad then (a pad
// whose first such line is a plug starts unplugged), like a player connecting or pulling a controller mid-session.
// [arcade] The tests' fast-forward (qa.ps1 -Speed): WCW_SPEED=n runs a match n times as fast, the rest of the game
// (start-up, the menus, entrances before the bell) at WCW_SPEED_MENUS (2 unless set) -- those screens cost more to draw,
// and above about 2x they ran slower than at 1x, erratically (2026-11-06). A match: the game's mode is a match (-2) or
// an attract demo (-6, a QA singles match) and its frame counter is running. Run every VI (events_callbacks). The game's
// clock changes speed without a jump (ultramodern timer.cpp). Players never set WCW_SPEED: speed 1, nothing done.
static uint32_t fast_forward_speed() {
    static const uint32_t v = [] { const char* s = getenv("WCW_SPEED"); long n = s ? strtol(s, nullptr, 10) : 1; return (uint32_t)(n < 1 ? 1 : n > 16 ? 16 : n); }();
    return v;
}
static void arcade_speed_policy() {
    const uint32_t fast = fast_forward_speed();
    if (fast <= 1) return;
    static const uint32_t menus = [fast] { const char* s = getenv("WCW_SPEED_MENUS"); long n = s ? strtol(s, nullptr, 10) : 2; n = n < 1 ? 1 : n; return (uint32_t)(n > (long)fast ? fast : n); }();
    extern uint8_t* g_ww_rdram;
    uint32_t want = menus;
    if (g_ww_rdram) {
        const int32_t mode = *reinterpret_cast<const int32_t*>(g_ww_rdram + (0x800457A0u - 0x80000000u));        // NM_MODE
        const uint16_t frames = *reinterpret_cast<const uint16_t*>(g_ww_rdram + ((0x801532C0u ^ 2u) - 0x80000000u));   // NM_MATCH_FRAMES
        if ((mode == -2 || mode == -6) && frames != 0xFFFF) want = fast;
    }
    static uint32_t last = 0;
    if (want != last) {
        static int logged = 0;
        if (logged++ < 40) fprintf(stderr, "[speed] x%u (game mode %d, match frames %d)\n", want,
            g_ww_rdram ? *reinterpret_cast<const int32_t*>(g_ww_rdram + (0x800457A0u - 0x80000000u)) : 99,
            g_ww_rdram ? (int)*reinterpret_cast<const uint16_t*>(g_ww_rdram + ((0x801532C0u ^ 2u) - 0x80000000u)) : -1);
        last = want;
    }
    ultramodern::set_speed_multiplier(want);
}
// [qa12x 2026-11-17] The tests' pace line (WCW_QA_PERF=1): every 5 s of the wall clock, how fast the game went and where its
// time went -- its pictures and retraces a second over the speed asked for, the retraces the VI thread let go by, and the share
// of the time the game's threads ran (ultramodern perf: the idle thread's waits taken out). Only counts and prints; players never set it.
#include "ultramodern/perf.hpp"
extern std::atomic<uint64_t> wcw_vi_given, wcw_vi_skipped;     // (ultramodern events.cpp)
static std::atomic<int64_t> qa_busy_ns{ 0 }, qa_frame_ns{ 0 };
static void qa_perf_sink(const char* phase, int64_t start_ns, int64_t end_ns) {
    if (!strcmp(phase, "game_busy")) qa_busy_ns.fetch_add(end_ns - start_ns, std::memory_order_relaxed);
    else if (!strcmp(phase, "game_frame")) qa_frame_ns.fetch_add(end_ns - start_ns, std::memory_order_relaxed);
}
static void qa_perf_start() {
    const char* e = getenv("WCW_QA_PERF");
    if (!e || e[0] != '1') return;
    ultramodern::perf::set_phase_sink(qa_perf_sink);
    std::thread([] {
        int pics0 = wcw_viswaps_total.load(); uint64_t given0 = wcw_vi_given.load(), skipped0 = wcw_vi_skipped.load(), ret0 = ultramodern::lockstep::retraces();
        auto t0 = std::chrono::steady_clock::now();
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            const auto t1 = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(t1 - t0).count();
            const int pics = wcw_viswaps_total.load(); const uint64_t given = wcw_vi_given.load(), skipped = wcw_vi_skipped.load(), ret = ultramodern::lockstep::retraces();
            const int64_t busy = qa_busy_ns.exchange(0), frame = qa_frame_ns.exchange(0);
            const double vis = ultramodern::lockstep::on() ? double(ret - ret0) : double(given - given0);
            fprintf(stderr, "[qa-perf] x%u asked: pictures %.2fx, retraces %.2fx (%.0f let go), the game's threads busy %.0f%%%s\n",
                ultramodern::get_speed_multiplier(), (pics - pics0) / 30.0 / dt, vis / 60.0 / dt, double(skipped - skipped0),
                frame > 0 ? 100.0 * double(busy) / double(frame) : 0.0, ultramodern::lockstep::on() ? " (lockstep)" : "");
            fflush(stderr);
            pics0 = pics; given0 = given; skipped0 = skipped; ret0 = ret; t0 = t1;
        }
    }).detach();
}
struct VpadPress { double at; int pad; SDL_GameControllerButton button; uint32_t hold_ms; };
struct VpadPlug { double at; int pad; bool plug; };
static std::vector<VpadPlug> g_vpad_plugs;
// (2026-11-20, controls) a key held by the script -- "12.0 key J 300" (SDL's key name; "#13" a scancode): put in SDL's own key
// state, which the game's keyboard bindings and the mod's keys read (QA only: WCW_VPADS_SCRIPT)
struct VkeyPress { double at; SDL_Scancode key; uint32_t hold_ms; };
static std::vector<VkeyPress> g_vkey_presses;
// a script's stick half ("-lefty": the left stick up, "+rightx": the right stick right), after the buttons and the axes pushed
// positive (the triggers' place): the axes pushed negative
constexpr int vpad_neg_axis = SDL_CONTROLLER_BUTTON_MAX + SDL_CONTROLLER_AXIS_MAX, vpad_slots = vpad_neg_axis + SDL_CONTROLLER_AXIS_MAX;

static std::vector<VpadPress> load_vpad_script(const char* path, double& repeat_from, double& repeat_period) {
    std::vector<VpadPress> presses;
    repeat_from = -1.0;
    repeat_period = 0.0;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        char word[64] = {};
        double at = 0.0, period = 0.0;
        int pad = 0;
        unsigned hold = 150;
        if (sscanf(line.c_str(), "%lf repeat %lf", &at, &period) == 2) {
            repeat_from = at;
            repeat_period = period;
            continue;
        }
        char verb[16] = {};
        if ((sscanf(line.c_str(), "%lf %15s %63s %u", &at, verb, word, &hold) >= 3) && !strcmp(verb, "key")) {
            SDL_Scancode sc = word[0] == '#' ? (SDL_Scancode)atoi(word + 1) : SDL_GetScancodeFromName(word);
            if ((sc <= SDL_SCANCODE_UNKNOWN) || (sc >= SDL_NUM_SCANCODES)) { fprintf(stderr, "[wcw] vpad script: bad key '%s'\n", line.c_str()); continue; }
            g_vkey_presses.push_back({ at, sc, hold });
            continue;
        }
        hold = 150;
        if ((sscanf(line.c_str(), "%lf %15s %d", &at, verb, &pad) == 3) && (!strcmp(verb, "plug") || !strcmp(verb, "unplug"))) {
            if ((pad >= 1) && (pad <= 4)) g_vpad_plugs.push_back({ at, pad - 1, verb[0] == 'p' });
            continue;
        }
        const int fields = sscanf(line.c_str(), "%lf %d %63s %u", &at, &pad, word, &hold);
        if (fields < 3) continue;
        SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(word);
        // (the triggers -- the N64's L and R -- are axes: "lefttrigger" / "righttrigger", pulled all the way)
        const SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(word);
        if ((button == SDL_CONTROLLER_BUTTON_INVALID) && ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) || (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)))
            button = (SDL_GameControllerButton)(SDL_CONTROLLER_BUTTON_MAX + axis);
        // (the sticks: "-lefty" / "+leftx" ..., pushed all the way that way)
        const SDL_GameControllerAxis half = ((word[0] == '+') || (word[0] == '-')) ? SDL_GameControllerGetAxisFromString(word + 1) : SDL_CONTROLLER_AXIS_INVALID;
        if ((button == SDL_CONTROLLER_BUTTON_INVALID) && (half >= SDL_CONTROLLER_AXIS_LEFTX) && (half <= SDL_CONTROLLER_AXIS_RIGHTY))
            button = (SDL_GameControllerButton)((word[0] == '+' ? SDL_CONTROLLER_BUTTON_MAX : vpad_neg_axis) + half);
        if ((pad < 1) || (pad > 4) || (button == SDL_CONTROLLER_BUTTON_INVALID)) {
            fprintf(stderr, "[wcw] vpad script: bad line '%s'\n", line.c_str());
            continue;
        }
        presses.push_back({ at, pad - 1, button, hold });
    }
    fprintf(stderr, "[wcw] vpad script %s: %zu presses, %zu keys, repeat from %.1f every %.1f s\n", arcade::shown(path).c_str(), presses.size(), g_vkey_presses.size(), repeat_from, repeat_period);
    return presses;
}

static void update_virtual_pads() {
    static int vpad_count = -1;
    static SDL_Joystick* vpads[4] = {};
    static std::vector<VpadPress> script;
    static bool scripted = false;
    static double repeat_from = -1.0, repeat_period = 0.0;
    static uint32_t start_ticks = 0;
    static size_t next_plug = 0;
    auto attach = [](int i) {
        int dev = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                            SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
        if (dev >= 0) vpads[i] = SDL_JoystickOpen(dev);
        fprintf(stderr, "[wcw] virtual pad %d attached (dev=%d joy=%p)\n", i + 1, dev, (void*)vpads[i]);
    };
    if (vpad_count < 0) {
        const char* env = getenv("WCW_VPADS");
        vpad_count = env ? std::clamp(atoi(env), 0, 4) : 0;
        if (const char* path = getenv("WCW_VPADS_SCRIPT")) {
            script = load_vpad_script(path, repeat_from, repeat_period);
            std::stable_sort(g_vpad_plugs.begin(), g_vpad_plugs.end(), [](const VpadPlug& a, const VpadPlug& b) { return a.at < b.at; });
            scripted = true;
        }
        for (int i = 0; i < vpad_count; i++) {
            // A pad whose first hot-plug event is a plug starts unplugged.
            const auto first = std::find_if(g_vpad_plugs.begin(), g_vpad_plugs.end(), [i](const VpadPlug& p) { return p.pad == i; });
            if ((first == g_vpad_plugs.end()) || !first->plug) attach(i);
        }
        start_ticks = SDL_GetTicks();
    }
    if (vpad_count == 0) return;
    uint32_t t = SDL_GetTicks();
    if (scripted) {
        // (in the game's time: the script's seconds land at the same moment of the game in fast-forward, WCW_SPEED --
        //  the game's clock since the script began, whatever speeds it ran at)
        static const uint64_t script_start_ticks = ultramodern::game_ticks_now();
        // ([replay 2026-11-14] and in lockstep, whose clock is the game's own, run fast or not)
        double now = (fast_forward_speed() > 1 || ultramodern::lockstep::on()) ? (double)(ultramodern::game_ticks_now() - script_start_ticks) / 46'875'000.0
                                              : (t - start_ticks) / 1000.0;
        while ((next_plug < g_vpad_plugs.size()) && (now >= g_vpad_plugs[next_plug].at)) {
            const VpadPlug& p = g_vpad_plugs[next_plug++];
            if (p.pad >= vpad_count) continue;
            if (p.plug && (vpads[p.pad] == nullptr)) {
                attach(p.pad);
            }
            else if (!p.plug && (vpads[p.pad] != nullptr)) {
                const SDL_JoystickID id = SDL_JoystickInstanceID(vpads[p.pad]);
                int device = -1;
                for (int d = 0; d < SDL_NumJoysticks(); d++) {
                    if (SDL_JoystickGetDeviceInstanceID(d) == id) device = d;
                }
                SDL_JoystickClose(vpads[p.pad]);
                vpads[p.pad] = nullptr;
                if (device >= 0) SDL_JoystickDetachVirtual(device);
                fprintf(stderr, "[wcw] virtual pad %d detached (dev=%d)\n", p.pad + 1, device);
            }
        }
        if ((repeat_from >= 0.0) && (repeat_period > 0.0) && (now >= repeat_from + repeat_period)) {
            now = repeat_from + std::fmod(now - repeat_from, repeat_period);
        }
        bool down[4][vpad_slots] = {};
        for (const VpadPress& p : script) {
            if ((now >= p.at) && (now < p.at + p.hold_ms / 1000.0)) down[p.pad][p.button] = true;
        }
        for (int i = 0; i < vpad_count; i++) {
            if (vpads[i] == nullptr) continue;
            for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
                SDL_JoystickSetVirtualButton(vpads[i], b, down[i][b] ? SDL_PRESSED : SDL_RELEASED);
            }
            for (int a : { (int)SDL_CONTROLLER_AXIS_TRIGGERLEFT, (int)SDL_CONTROLLER_AXIS_TRIGGERRIGHT })
                SDL_JoystickSetVirtualAxis(vpads[i], a, down[i][SDL_CONTROLLER_BUTTON_MAX + a] ? 32767 : 0);
            for (int a = SDL_CONTROLLER_AXIS_LEFTX; a <= SDL_CONTROLLER_AXIS_RIGHTY; a++)
                SDL_JoystickSetVirtualAxis(vpads[i], a, down[i][SDL_CONTROLLER_BUTTON_MAX + a] ? 32767 : down[i][vpad_neg_axis + a] ? -32768 : 0);
        }
        if (!g_vkey_presses.empty()) {
            int count = 0;
            Uint8* keys = const_cast<Uint8*>(SDL_GetKeyboardState(&count));   // (SDL's own state: what every reader of the keyboard sees)
            for (const VkeyPress& k : g_vkey_presses) if (keys != nullptr && (int)k.key < count) keys[k.key] = 0;
            for (const VkeyPress& k : g_vkey_presses)
                if (keys != nullptr && (int)k.key < count && now >= k.at && now < k.at + k.hold_ms / 1000.0) keys[k.key] = 1;
        }
        return;
    }
    for (int i = 0; i < vpad_count; i++) {
        if (vpads[i] == nullptr) continue;
        // 2 s period, 500 ms press, staggered 250 ms per pad.
        bool pressed = ((t + i * 250) % 2000) < 500;
        SDL_JoystickSetVirtualButton(vpads[i], SDL_CONTROLLER_BUTTON_A + i, pressed ? SDL_PRESSED : SDL_RELEASED);
    }
}

int arcade_view_window_mode();
void update_gfx(void*) {
    { static int was = 0; const int now = arcade_view_window_mode();      // (the operator menu's PICTURE page: a window, or all of the screen)
      if (now != was && window && !nm_headless()) { was = now; SDL_SetWindowFullscreen(window, now ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); } }
    update_virtual_pads();
    recompinput::handle_events();
}

ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    // All four ports report a controller so the game considers every player slot joinable
    // (unassigned players read as neutral input, like an idle plugged-in pad). Only port 1
    // has a pak: the emulated hybrid Controller/Rumble Pak in librecomp si.cpp.
    return { .connected_device = ultramodern::input::Device::Controller,
             .connected_pak = (controller_num == 0) ? ultramodern::input::Pak::RumblePak
                                                    : ultramodern::input::Pak::None };
}

// ---------------- audio (minimal: queue as-is, 16-bit stereo) ----------------
static SDL_AudioDeviceID g_audio_device = 0;
static uint32_t g_sample_rate = 32000;
static uint32_t g_requested_rate = 0;
constexpr uint32_t input_channels = 2;
constexpr uint32_t bytes_per_frame = input_channels * sizeof(int16_t);

// With no audio endpoint (headless/unattended session) queued audio used to vanish:
// get_frames_remaining()==0 forever, so the game's AI-full throttle never engages and the
// audio task loop free-runs (~500/s, bringup-plan session 4). Model a device that drains
// the queue in real time so pacing works endpoint or not.
static uint64_t g_virtual_frames = 0;
static uint64_t g_virtual_last_ms = 0;
static void virtual_drain() {
    uint64_t now = SDL_GetTicks64();
    if (g_virtual_last_ms) {
        // (as a speaker would play them -- n times as fast in fast-forward, WCW_SPEED)
        uint64_t drained = (now - g_virtual_last_ms) * g_sample_rate * ultramodern::get_speed_multiplier() / 1000;
        g_virtual_frames = drained >= g_virtual_frames ? 0 : g_virtual_frames - drained;
    }
    g_virtual_last_ms = now;
}

static bool reset_audio(uint32_t freq) {
    // Diagnostic: every open/close drops the whole SDL queue, so frequent calls here
    // zero osAiGetLength and free-run the game's audio generator (bringup-plan session 4).
    { static int n = 0; fprintf(stderr, "[audio] reset_audio #%d freq=%u driver=%s (had_device=%d)\n",
        n++, freq, SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none", g_audio_device != 0); }
    SDL_AudioSpec want{}, have{};
    want.freq = (int)freq;
    want.format = AUDIO_S16SYS;
    want.channels = (Uint8)input_channels;
    // 512-frame chunks (23ms @ 22050) — with 1024 (46ms) the device drained more per pull
    // than the game's ~25ms audio bursts could cover, guaranteeing periodic underruns.
    want.samples = 512;
    want.callback = nullptr;
    if (g_audio_device) { SDL_CloseAudioDevice(g_audio_device); g_audio_device = 0; }
    // [arcade] fast-forward (WCW_SPEED > 1, the tests): no sound device -- the game makes its sound n times as fast as a
    // speaker plays it, and SDL's queue would grow without end. The virtual queue below drains at the game's speed
    // instead, so the game sees its sound go out as it would at 1x.
    if (fast_forward_speed() > 1) {
        g_sample_rate = freq;
        g_virtual_frames = 0;
        fprintf(stderr, "[audio] fast-forward x%u: no sound device (a virtual one, drained at the game's speed)\n",
            fast_forward_speed());
        return false;
    }
    g_audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio_device == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        g_sample_rate = freq; // keep the virtual-drain model at the game's real rate
        return false;
    }
    g_sample_rate = (uint32_t)have.freq;
    g_virtual_frames = 0;
    fprintf(stderr, "[audio] device opened id=%u have.freq=%d have.samples=%u\n",
        (unsigned)g_audio_device, have.freq, (unsigned)have.samples);
    SDL_PauseAudioDevice(g_audio_device, 0);
    return true;
}

void set_frequency(uint32_t freq) {
    // Reopening the device discards SDL's entire queued buffer, so skip redundant calls
    // (WM2000 re-sets 28800 on menu transitions — each one was an audible drop).
    if (g_audio_device && freq == g_requested_rate) return;
    g_requested_rate = freq;
    reset_audio(freq);
}

size_t get_frames_remaining();
void arcade_commentary_mix(int16_t* buf, size_t frames, uint32_t rate, float main_volume);
void arcade_media_mix(int16_t* buf, size_t frames, uint32_t rate, float main_volume);     // (arcade_media.cpp)

void queue_samples(int16_t* audio_data, size_t sample_count) {
    // audio_data is interleaved stereo; swap L/R to correct for endianness handling, and apply
    // Settings > Sound > Main Volume (0-100, linear like BMHero's; read per batch so the slider
    // takes effect immediately).
    static std::vector<int16_t> buf;
    buf.resize(sample_count);
    const float volume = std::clamp(static_cast<float>(arcade::main_volume()) / 100.0f, 0.0f, 1.0f);
    if (volume >= 1.0f) {
        for (size_t i = 0; i + 1 < sample_count; i += 2) {
            buf[i + 0] = audio_data[i + 1];
            buf[i + 1] = audio_data[i + 0];
        }
    }
    else {
        for (size_t i = 0; i + 1 < sample_count; i += 2) {
            buf[i + 0] = static_cast<int16_t>(std::lround(audio_data[i + 1] * volume));
            buf[i + 1] = static_cast<int16_t>(std::lround(audio_data[i + 0] * volume));
        }
    }
    // [arcade] the commentary's voice mixed in (Main Volume applies to it too)
    arcade_commentary_mix(buf.data(), sample_count / 2, g_sample_rate, volume);
    // [arcade] 2026-11-10: a folder wrestler's theme (or his video's own sound) from a common-format file, mixed in the same
    // way. Not in the tests' fast-forward: the game makes its sound n times as fast there and nothing plays it.
    if (fast_forward_speed() <= 1) arcade_media_mix(buf.data(), sample_count / 2, g_sample_rate, volume);
    // QA: WCW_AUDIO_RECORD=file keeps everything that goes to the speakers, as raw 16-bit stereo, so a test can check that
    // a sound really played.
    {
        static const char* record_path = getenv("WCW_AUDIO_RECORD");
        static FILE* record_file = record_path ? fopen(record_path, "wb") : nullptr;
        if (record_file) fwrite(buf.data(), sizeof(int16_t), sample_count, record_file);
    }
    uint32_t queued_frames_before = (uint32_t)get_frames_remaining();
    // [replay 2026-11-14] In lockstep run as fast as it goes (a replay, the no-graphics mode) the game makes its sound many
    // times faster than a speaker plays it: none goes to the device, whose queue would only grow. (The game's driver
    // doesn't ask the device in lockstep: ultramodern audio.cpp.)
    if (ultramodern::lockstep::on() && !ultramodern::lockstep::paced()) {
    } else if (g_audio_device) {
        SDL_QueueAudio(g_audio_device, buf.data(), (Uint32)(sample_count * sizeof(int16_t)));
    } else {
        virtual_drain();
        g_virtual_frames += sample_count / input_channels;
    }
    // Diagnostic (1/s): audio health. queued_frames_before==0 means the device ran dry and
    // SDL fed silence (an audible pop); minlvl is the low-water mark in frames; maxgap the
    // longest interval between batches (starvation if it exceeds the buffered duration).
    static uint64_t last_ms = 0, report_ms = 0; static uint64_t max_gap = 0;
    static uint32_t underruns = 0, batches = 0, min_lvl = UINT32_MAX; static int peak = 0;
    uint64_t now = SDL_GetTicks64();
    if (last_ms && now - last_ms > max_gap) max_gap = now - last_ms;
    last_ms = now;
    if (queued_frames_before == 0) underruns++;
    if (queued_frames_before < min_lvl) min_lvl = queued_frames_before;
    batches++;
    for (size_t i = 0; i < sample_count; i++) { int v = audio_data[i] < 0 ? -audio_data[i] : audio_data[i]; if (v > peak) peak = v; }
    static const bool audio_log = getenv("WCW_AUDIO_LOG") != nullptr;
    if (now - report_ms >= 1000) {
        if (audio_log) {
            fprintf(stderr, "[audio] t=%llus batches=%u minlvl=%u underruns=%u maxgap=%llums peak=%d rate=%u\n",
                (unsigned long long)(now / 1000), batches, min_lvl, underruns, (unsigned long long)max_gap, peak, g_sample_rate);
        }
        report_ms = now; underruns = 0; min_lvl = UINT32_MAX; max_gap = 0; peak = 0; batches = 0;
    }
}

size_t get_frames_remaining() {
    if (!g_audio_device) {
        virtual_drain();
        return (size_t)g_virtual_frames;
    }
    return SDL_GetQueuedAudioSize(g_audio_device) / bytes_per_frame;
}

// ---------------- RSP ucode (audio) ----------------
// The audio ucode (located at runtime via the OSTask log: ucode=0x8002BD10 ->
// ROM 0x2C910, ucode_data=0x8003A5C0 -> ROM 0x3B1C0) is recompiled by RSPRecomp
// (rsp/revenge_audio.toml -> rsp/revenge_audio.cpp). It is NOT WT's aspMain (newer
// revision, text 0xC54); its jump table was extracted from the ucode's own data
// section. Never return nullptr here: that makes recomp::rsp::run_task return false,
// which task_thread_func (events.cpp) treats as FATAL (ULTRAMODERN_QUICK_EXIT). For
// unexpected task types, hand back a no-op ucode that reports a clean RSP break so
// the task "completes" and the game keeps running.
static RspExitReason wcw_null_ucode(uint8_t* rdram, uint32_t ucode_addr) {
    (void)rdram; (void)ucode_addr;
    return RspExitReason::Broke; // pretend the task finished so the runtime continues
}

extern RspUcodeFunc arcade_audio_ucode;      // (src/main/arcade_audio.cpp: the game's sound commands)
void arcade_audio_task(const void* task);

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    static int n = 0;
    if (n++ < 4) {
        fprintf(stderr, "[rsp] task type=%u ucode=0x%08X ucode_data=0x%08X ucode_size=0x%X\n",
            (unsigned)task->t.type, (unsigned)task->t.ucode, (unsigned)task->t.ucode_data,
            (unsigned)task->t.ucode_size);
    }
    // Only non-gfx tasks reach here — gfx tasks go through the renderer path.
    if (task->t.type == M_AUDTASK) {
        arcade_audio_task(task);
        return &arcade_audio_ucode;
    }
    fprintf(stderr, "[rsp] unknown task type=%u — running no-op ucode\n", (unsigned)task->t.type);
    return &wcw_null_ucode;
}

#ifdef _WIN32
// [wcw] C4: release builds link as /SUBSYSTEM:WINDOWS (no console window). When launched
// without usable stdio (double-click), route stdout+stderr to a log file in the app folder
// so bug reports have something to attach (the terminate/SEH backtraces land there too).
// --show-console allocates a console instead. If the parent provided std handles (a dev
// console, or cycle.ps1's `> bt.txt` redirection), stdio is left untouched.
static void wcw_setup_logging(bool show_console) {
    if (show_console) {
        if (AllocConsole()) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
        return;
    }
    // [arcade] QA (WCW_LOG_FILE, set by qa.ps1): the log straight to that file, whatever handles the parent gave.
    // PowerShell's Start-Process -RedirectStandardError hands the game a pipe it copies to the file itself; when the
    // copying fell behind, the pipe filled and the game's own writes stopped it -- the renderer's thread held in
    // WriteFile for 12 s and more (a 16-man probe's "freeze", a Rumble's win held 39 s, the "[read]" spikes),
    // 2026-11-06. A file of the game's own never waits on anyone.
    const char* qa_log = getenv("WCW_LOG_FILE");
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (!qa_log && ((out != nullptr && out != INVALID_HANDLE_VALUE) || (err != nullptr && err != INVALID_HANDLE_VALUE))) {
        return;
    }
    std::filesystem::path app_folder = arcade::app_folder();
    std::error_code ec;
    std::filesystem::create_directories(app_folder, ec);
    std::filesystem::path log_path = app_folder / "NoMercyRecompiled.log";
    if (qa_log) {
        log_path = std::filesystem::u8path(qa_log);
    } else {
        // [arcade] the last session's log is kept (a crash's backtrace is in it)
        std::filesystem::rename(log_path, app_folder / "NoMercyRecompiled.prev.log", ec);
    }
    // Open with read sharing (freopen would deny it) so the log can be copied/viewed while
    // the game is running; route both streams through dups of the same fd (shared offset).
    HANDLE log_handle = CreateFileW(log_path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_handle == INVALID_HANDLE_VALUE) {
        return;
    }
    int log_fd = _open_osfhandle(reinterpret_cast<intptr_t>(log_handle), _O_WRONLY);
    if (log_fd == -1) {
        CloseHandle(log_handle);
        return;
    }
    // In a handle-less GUI launch the CRT streams have no fd (-2); freopen to NUL first to
    // materialize one, then dup the log fd over it.
    FILE* f = nullptr;
    freopen_s(&f, "NUL", "w", stderr);
    _dup2(log_fd, _fileno(stderr));
    freopen_s(&f, "NUL", "w", stdout);
    _dup2(log_fd, _fileno(stdout));
    _close(log_fd);
}
#endif

// Generated from RecompiledFuncs/repaired_functions.txt (CMakeLists.txt).
void wcw_register_repaired_functions();

// Bundled mods (assets/mods/*.nrm, built in the AkiMods repo) ship in every release zip and
// AKI Launcher install, but librecomp only scans <config>/mods. Copy each one there when it's
// missing or differs from the bundled copy, so a new release updates it. librecomp enables a
// newly found mod by default (manifest enabled_by_default; RecompModTool always leaves it true),
// so every bundled mod must do nothing until configured. A deleted one comes back on the next
// launch, and one mods.json lists as switched off is switched back on (repair_bundled_mods_config:
// the game has no Mods menu, 2026-11-13).
static void install_bundled_mods(const std::filesystem::path& app_folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src_dir = arcade::program_folder() / "assets" / "mods";
    if (!fs::is_directory(src_dir, ec)) {
        return;
    }
    const fs::path dst_dir = app_folder / "mods";
    fs::create_directories(dst_dir, ec);
    auto read_all = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    };
    for (const auto& entry : fs::directory_iterator(src_dir, ec)) {
        if (entry.path().extension() != ".nrm") {
            continue;
        }
        const fs::path dst = dst_dir / entry.path().filename();
        if (fs::exists(dst, ec) && read_all(dst) == read_all(entry.path())) {
            continue;
        }
        fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing, ec);
        fprintf(stderr, "[wcw] bundled mod %s: %s\n", entry.path().filename().string().c_str(),
            ec ? ec.message().c_str() : "installed");
    }
}

// (2026-11-13, smallfix) mods.json repair. The standalone game has no Mods menu, so a bundled mod that mods.json lists as
// switched off can never be switched back on by the player -- and that has happened without the player doing anything: a save
// made before librecomp had read mods.json (the start-up race fixed in librecomp mods.cpp for 0.81) wrote "these mods, none
// enabled", and every later launch read that back as the player's choice and started without them. So before recomp::start
// reads mods.json, each bundled mod (assets/mods/*.nrm) that mods.json knows (it is in "mod_order") but has not among
// "enabled_mods" is put back among them, and the log says so in one line. A mod the player added themselves is left as
// mods.json has it, and a bundled mod mods.json doesn't know yet needs nothing: librecomp switches a new mod on by itself.
static void repair_bundled_mods_config(const std::filesystem::path& app_folder) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src_dir = arcade::program_folder() / "assets" / "mods";
    const fs::path config_path = app_folder / "mods.json";
    if (!fs::is_directory(src_dir, ec) || !fs::exists(config_path, ec)) {
        return;
    }
    // mods.json read the way librecomp reads it (parse_mods_config): the file, or its backup if the file doesn't parse.
    nlohmann::json config;
    auto parse = [&config](std::ifstream input) {
        if (!input.good()) {
            return false;
        }
        try {
            input >> config;
        } catch (const nlohmann::json::exception&) {
            return false;
        }
        return config.is_object();
    };
    if (!parse(std::ifstream{config_path}) && !parse(recomp::open_input_backup_file(config_path))) {
        return;
    }
    nlohmann::json& enabled = config["enabled_mods"];
    const auto order = config.find("mod_order");
    if (!enabled.is_array() && !enabled.is_null()) {
        return;   // (not a list: librecomp ignores it too, and enables nothing from it)
    }
    if (order == config.end() || !order->is_array()) {
        return;   // (no order: librecomp treats every mod as new and switches it on)
    }
    auto listed = [](const nlohmann::json& list, const std::string& id) {
        for (const auto& entry : list) {
            if (entry.is_string() && entry.get<std::string>() == id) {
                return true;
            }
        }
        return false;
    };
    // Each bundled mod's id, from the mod.json inside it (the id librecomp files it under, not the file's name).
    std::vector<std::string> repaired;
    for (const auto& entry : fs::directory_iterator(src_dir, ec)) {
        if (entry.path().extension() != ".nrm") {
            continue;
        }
        recomp::mods::ModOpenError open_error = recomp::mods::ModOpenError::Good;
        recomp::mods::ZipModFileHandle mod_file(entry.path(), open_error);
        if (open_error != recomp::mods::ModOpenError::Good) {
            continue;
        }
        bool exists = false;
        const std::vector<char> manifest_bytes = mod_file.read_file("mod.json", exists);
        if (!exists) {
            continue;
        }
        const nlohmann::json manifest = nlohmann::json::parse(manifest_bytes.begin(), manifest_bytes.end(), nullptr, false);
        const auto id = manifest.is_object() ? manifest.find("id") : manifest.end();
        if (!manifest.is_object() || id == manifest.end() || !id->is_string()) {
            continue;
        }
        const std::string mod_id = id->get<std::string>();
        if (listed(*order, mod_id) && !(enabled.is_array() && listed(enabled, mod_id))) {
            enabled.push_back(mod_id);
            repaired.push_back(mod_id);
        }
    }
    if (repaired.empty()) {
        return;   // (the usual case: nothing to say)
    }
    std::sort(repaired.begin(), repaired.end());
    std::string names;
    for (const std::string& mod_id : repaired) {
        names += (names.empty() ? "" : ", ") + mod_id;
    }
    // Written the way librecomp writes it (save_mods_config): a temporary file, then the old one kept as mods.json.bak.
    bool saved = false;
    {
        std::ofstream output = recomp::open_output_file_with_backup(config_path);
        if (output.good()) {
            output << std::setw(4) << config;
            output.close();
            saved = output.good() && recomp::finalize_output_file_with_backup(config_path);
        }
    }
    fprintf(stderr, "[wcw] mods.json: bundled mods listed as switched off, switched back on (the game has no Mods menu): %s%s\n",
        names.c_str(), saved ? "" : " -- but mods.json could not be written, so they stay off");
}

// Right-stick access for mods (AkiMods camera). The N64 has no right stick; by default it
// drives the C buttons, so a mod that takes it over also suppresses that mapping (the C
// buttons stay on their digital bindings). Player 1's stick, -1..1, deadzone applied.
static void mod_get_right_analog_x(uint8_t* rdram, recomp_context* ctx) {
    float x, y;
    recompinput::get_right_analog(0, &x, &y);
    _return(ctx, x);
}

static void mod_get_right_analog_y(uint8_t* rdram, recomp_context* ctx) {
    float x, y;
    recompinput::get_right_analog(0, &x, &y);
    _return(ctx, y);
}

static void mod_set_right_analog_suppressed(uint8_t* rdram, recomp_context* ctx) {
    recompinput::set_right_analog_suppressed(_arg<0, uint32_t>(rdram, ctx) != 0);
}

// Right stick click (R3), player 1. Note R3 is also bound to C-up by default.
static void mod_get_right_stick_click(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, uint32_t(recompinput::get_input_digital(0, recompinput::InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) ? 1 : 0));
}

// Camera-relative movement for mods (AkiMods camera): a rotation (radians) applied to the
// players' movement before the game sees it. AKI games move on the N64 d-pad, so the
// d-pad direction is rotated and snapped back to the nearest of its 8 directions. 0 = off.
static std::atomic<float> mod_movement_rotation{0.0f};

static void mod_set_movement_rotation(uint8_t* rdram, recomp_context* ctx) {
    mod_movement_rotation.store(_arg<0, float>(rdram, ctx));
}

void arcade_pads_special(int player, uint16_t buttons, float* x, float* y);   // (arcade_pads.cpp: the bindable SPECIAL)
void arcade_pads_player_profile(int player);                                    // (arcade_pads.cpp: a player's bindings on his pad)
void arcade_pads_snapback(int player, uint16_t* buttons);                       // (arcade_pads.cpp: a stick's spring-back)
static bool get_n64_input_for_mods(int controller_num, uint16_t* buttons, float* x, float* y) {
    arcade_pads_player_profile(controller_num);   // [arcade 2026-11-19] the player's own bindings, whatever pad is his
    const bool ok = recompinput::profiles::get_n64_input(controller_num, buttons, x, y);
    // [arcade 2026-11-23] a stick springing back past its centre is not a press the other way (arcade_pads.cpp)
    if (ok) arcade_pads_snapback(controller_num, buttons);
    // [arcade 2026-11-19] SPECIAL (the operator menu's CONTROLS pages): while its key / button is held, the analog stick
    // pushed the way the d-pad is held -- before the movement's rotation below, from the d-pad as the player holds it
    if (ok) arcade_pads_special(controller_num, *buttons, x, y);
    const float rotation = mod_movement_rotation.load();
    if (!ok || rotation == 0.0f) {
        return ok;
    }
    constexpr uint16_t up = 0x0800, down = 0x0400, left = 0x0200, right = 0x0100;
    const int dx = ((*buttons & right) ? 1 : 0) - ((*buttons & left) ? 1 : 0);
    const int dy = ((*buttons & up) ? 1 : 0) - ((*buttons & down) ? 1 : 0);
    if (dx == 0 && dy == 0) {
        return ok;
    }
    // Octant 0 = right, counter-clockwise in 45-degree steps.
    static constexpr int dirs[8][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1} };
    const float angle = std::atan2(float(dy), float(dx)) + rotation;
    const int octant = int(std::lround(angle / 0.785398163f)) & 7;
    *buttons &= ~(up | down | left | right);
    *buttons |= dirs[octant][0] > 0 ? right : dirs[octant][0] < 0 ? left : 0;
    *buttons |= dirs[octant][1] > 0 ? up : dirs[octant][1] < 0 ? down : 0;
    return ok;
}

// Keyboard and mouse for mods (AkiMods camera free-fly mode). Keys are read straight from SDL,
// so a mod still sees them while recomp_set_keyboard_suppressed keeps the game's keyboard
// bindings quiet. Mouse motion and wheel are raw (unscaled) and only collected while game input
// is enabled (not while a menu is open); recomp_take_mouse_motion latches what accumulated since
// the last call for the three getters.
static float mod_mouse_dx = 0.0f;
static float mod_mouse_dy = 0.0f;
static int32_t mod_mouse_wheel = 0;

static void mod_get_key_down(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t scancode = _arg<0, uint32_t>(rdram, ctx);
    int count = 0;
    const Uint8* keys = SDL_GetKeyboardState(&count);
    // Nothing while a menu has the input, so typing in it doesn't drive the mod.
    const bool down = !recompinput::game_input_disabled() && (keys != nullptr) && (scancode < uint32_t(count)) && keys[scancode];
    _return(ctx, uint32_t(down ? 1 : 0));
}

static void mod_take_mouse_motion(uint8_t* rdram, recomp_context* ctx) {
    recompinput::take_raw_mouse_motion(&mod_mouse_dx, &mod_mouse_dy, &mod_mouse_wheel);
}

static void mod_get_mouse_dx(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, mod_mouse_dx);
}

static void mod_get_mouse_dy(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, mod_mouse_dy);
}

static void mod_get_mouse_wheel(uint8_t* rdram, recomp_context* ctx) {
    _return(ctx, int32_t(mod_mouse_wheel));
}

static void mod_set_mouse_captured(uint8_t* rdram, recomp_context* ctx) {
    recompinput::set_mouse_capture_requested(_arg<0, uint32_t>(rdram, ctx) != 0);
}

static void mod_set_keyboard_suppressed(uint8_t* rdram, recomp_context* ctx) {
    recompinput::set_keyboard_suppressed(_arg<0, uint32_t>(rdram, ctx) != 0);
}

void arcade_pads_load_layouts();
void arcade_pads_register_exports();
void arcade_pads_bindings_init();     // [arcade 2026-11-19] every N64 input bindable: controls_n64.json
void arcade_pads_bindings_apply();
void arcade_stories_register_exports();
void arcade_pages_register_exports();
void arcade_hd_register_exports();
void arcade_scene_register_exports();
void arcade_view_register_exports();
void arcade_bones_register_exports();
void arcade_commentary_init();
void arcade_commentary_build();   // [commentary2 2026-11-18]
void arcade_commentary_register_exports();
static void register_mod_input_exports() {
    arcade_pads_register_exports();
    arcade_stories_register_exports();
    arcade_pages_register_exports();
    arcade_hd_register_exports();
    arcade_scene_register_exports();
    arcade_view_register_exports();
    arcade_bones_register_exports();
    arcade_host_register_exports();
    { extern void arcade_cont_register_exports(); arcade_cont_register_exports(); } { extern void arcade_packs_register_exports(); arcade_packs_register_exports(); } { extern void arcade_wrestlers_register_exports(); arcade_wrestlers_register_exports(); } { extern void arcade_arenas_register_exports(); arcade_arenas_register_exports(); }
    arcade_commentary_register_exports();
    { extern void arcade_media_register_exports(); arcade_media_register_exports(); }     // (2026-11-10: themes and Titantron videos from common files)
    recomp::overlays::register_base_export("recomp_get_key_down", mod_get_key_down);
    recomp::overlays::register_base_export("recomp_take_mouse_motion", mod_take_mouse_motion);
    recomp::overlays::register_base_export("recomp_get_mouse_dx", mod_get_mouse_dx);
    recomp::overlays::register_base_export("recomp_get_mouse_dy", mod_get_mouse_dy);
    recomp::overlays::register_base_export("recomp_get_mouse_wheel", mod_get_mouse_wheel);
    recomp::overlays::register_base_export("recomp_set_mouse_captured", mod_set_mouse_captured);
    recomp::overlays::register_base_export("recomp_set_keyboard_suppressed", mod_set_keyboard_suppressed);
    recomp::overlays::register_base_export("recomp_get_right_analog_x", mod_get_right_analog_x);
    recomp::overlays::register_base_export("recomp_get_right_analog_y", mod_get_right_analog_y);
    recomp::overlays::register_base_export("recomp_set_right_analog_suppressed", mod_set_right_analog_suppressed);
    recomp::overlays::register_base_export("recomp_get_right_stick_click", mod_get_right_stick_click);
    recomp::overlays::register_base_export("recomp_set_movement_rotation", mod_set_movement_rotation);
}

// [wcw] Texture packs (aki-roadmap#48). A pack is a folder or an .rtz (a zip) with rt64.json at its root, in the mods
// folder or installed from the Mods menu; it shows up there and can be switched on and off while playing (the
// Zelda64Recomp registration). --dump-textures (or WCW_TEX_DUMP=<folder>) saves every texture the game draws as
// <rt64 hash>.png in <app folder>/texture_dump/pack: edit the PNGs you want to change, delete the rest, and copy the
// pack folder (any name) into mods.
// (2026-11-13, final082) The build's start-up canaries: lifted.cpp's and the translated game's compile options checked, and
// said loudly in the log if they are wrong (src/game/lift_canary.cpp).
extern "C" void lift_canaries();

int main(int argc, char** argv) {
    lift_canaries();
    bool show_console = false;
    bool dump_textures = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--show-console") == 0) {
            show_console = true;
        }
        else if (strcmp(argv[i], "--dump-textures") == 0) {
            dump_textures = true;
        }
    }
    (void)show_console;
#if NOMERCY_STANDALONE
    // [standalone] The game alone: no launcher, menus or prompts (recompui's standalone mode).
#endif

    // Program identity. The program id names the config/save folder
    // (%LOCALAPPDATA%\NoMercyRecompiled on Windows) — keep it matched to the exe name.
    // Set before anything that resolves the app folder (logging below, config path later).
#ifdef _WIN32
    wcw_setup_logging(show_console);
    crash_report_init(arcade::app_folder());
#endif
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "[wcw] main start\n");
#ifdef _WIN32
    std::set_terminate(wcw_terminate_handler);
    SetUnhandledExceptionFilter(wcw_unhandled_filter);

    // [wcw] ultramodern paces VI/timer events with raw Sleep() (timer.cpp), whose
    // granularity is the PER-PROCESS timer resolution on Win10 2004+/Win11. Nothing
    // in-process holds 1ms otherwise, so retrace delivery can quantize to 15.6ms and
    // burst — the >=pri-80 event relay then never drains and starves the game loop
    // (frozen video, live audio, from boot). Hold 1ms for the process lifetime.
    timeBeginPeriod(1);

    // [wcw] DIAGNOSTIC: when running under RenderDoc (renderdoc.dll injected), trigger a
    // one-frame capture ~8s in (title screen rendering steadily by then). The capture is the
    // ground truth for why game draws rasterize nothing (see disasm/libultra.md deep dive).
    {
        HMODULE rdoc = GetModuleHandleA("renderdoc.dll");
        if (rdoc != nullptr) {
            pRENDERDOC_GetAPI getApi = (pRENDERDOC_GetAPI)GetProcAddress(rdoc, "RENDERDOC_GetAPI");
            static RENDERDOC_API_1_1_2* rdocApi = nullptr;
            if (getApi != nullptr && getApi(eRENDERDOC_API_Version_1_1_2, (void**)&rdocApi) == 1) {
                int rdocDelay = 8;
                if (const char* d = getenv("WCW_RDC_T")) { int v = atoi(d); if (v > 0) rdocDelay = v; }
                fprintf(stderr, "[wcw][renderdoc] in-app API active; capture will trigger at t+%ds\n", rdocDelay);
                std::thread([rdocDelay]() {
                    std::this_thread::sleep_for(std::chrono::seconds(rdocDelay));
                    fprintf(stderr, "[wcw][renderdoc] triggering capture NOW\n");
                    // Multi-frame: game workloads land in ~27 of 60 present intervals, so a
                    // single-frame capture can contain only the VI blit. 12 frames guarantees
                    // several captures with real game draws.
                    rdocApi->TriggerMultiFrameCapture(12);
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    fprintf(stderr, "[wcw][renderdoc] captures so far: %u\n", rdocApi->GetNumCaptures());
                }).detach();
            }
        }
    }
#endif
#ifdef _WIN32
    // WORKAROUND: the first DXGI factory + D3D12 device creation must happen on the main
    // thread, or they fast-fail (0xC0000409) when RT64 first touches them on the game
    // thread. (BMHero avoids this because its launcher UI creates a render context on the
    // main thread first.) Prime DXGI + D3D12 here so RT64's later calls on the game thread
    // succeed.
    {
        IDXGIFactory4* tf = nullptr;
        // (Direct3D's own checking of the native picture: before any device there is, or it takes those devices away)
        if (getenv("WCW_NATIVE_D3DDEBUG")) { ID3D12Debug* dbg = nullptr; if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) { dbg->EnableDebugLayer(); dbg->Release(); fprintf(stderr, "[wcw] Direct3D debug layer asked for\n"); } }
        fprintf(stderr, "[wcw] priming DXGI + D3D12 on main thread...\n");
        if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&tf))) && tf) {
            IDXGIAdapter1* ad = nullptr;
            for (UINT i = 0; tf->EnumAdapters1(i, &ad) != DXGI_ERROR_NOT_FOUND; i++) {
                ID3D12Device8* dev = nullptr;  // match RT64/plume: it requests ID3D12Device8
                bool ok = SUCCEEDED(D3D12CreateDevice(ad, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)));
                fprintf(stderr, "[wcw]   prime adapter %u D3D12CreateDevice(Device8) ok=%d\n", i, (int)ok);
                if (dev) dev->Release();
                ad->Release();
                if (ok) break;
            }
            tf->Release();
        }
        fprintf(stderr, "[wcw] priming done\n");
    }
#endif
#ifdef _WIN32
    timeBeginPeriod(1);
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);
#endif
    SDL_SetMainReady();

    SDL_InitSubSystem(SDL_INIT_AUDIO);
    reset_audio(32000);

    // Source controller mappings file (SDL_GameControllerDB), same as BMHero: lets SDL
    // recognize third-party pads that lack built-in mappings. Missing file is non-fatal.
    std::u8string controller_db_path = (arcade::program_folder() / "recompcontrollerdb.txt").u8string();
    if (SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char *>(controller_db_path.c_str())) < 0) {
        fprintf(stderr, "Failed to load controller mappings: %s\n", SDL_GetError());
    }
    // [arcade] the community database (gamecontrollerdb.txt) and the operator menu's controller setups
    arcade_pads_load_layouts();
    // [arcade] match commentary (commentary.pak next to the exe)
    arcade_commentary_init();

    // Register the game. The entry is stored in the global `supported_games` vector because
    // recompui's launcher reads supported_games[0] (e.g. default_launcher_init_callback) to
    // build the game-options menu; leaving it empty access-violates during boot.
    // No Mercy is documented as a FLASH-cart game (everything saves on-cart; no
    // Controller Pak save support), so the pak emulation should never touch the cart
    // save — keep the separate pak backing regardless (saves/<game id>.pak.bin,
    // [wcw2k] in librecomp si.cpp): if a pak probe does happen it can't corrupt the
    // flash image. VERIFY during bring-up (WM2000 lesson: the shared buffer would
    // have let pak writes corrupt the cart save).

    // Hardware-accurate osAiSetFrequency return (28800 -> 28805 via DAC quantization).
    // WM2000 sizes its audio-heap allocations from the return value; the unquantized
    // return shifted the audio heap onto thread 6's stack ([wcw2k] in librecomp ai.cpp).
    extern bool wcw_ai_accurate_freq;
    wcw_ai_accurate_freq = true;

    recomp::GameEntry game{};
    game.rom_hash = 0x0455F72A0F9FF778ULL;          // XXH3_64bits of the NTSC-U Rev 1 ROM
    game.internal_name = "WWF No Mercy";            // 20-byte header name, trailing spaces trimmed
    game.display_name = "WWF No Mercy";
    game.game_id = u8"wwf.nomercy.us";
    game.mod_game_id = "wwfnomercy";
    // No Mercy (NW4E) is documented as FLASH RAM 1Mbit (128 KB) — one of the few
    // flash-cart N64 games, and the reason Rev 0 had its famous save-erasure bug.
    // librecomp supports Flashram (routes the flash command protocol; see
    // librecomp pi.cpp/recomp.cpp). VERIFY during bring-up from the game's save
    // driver: find its PI handle init (WM2000 built an SRAM OSPiHandle in
    // func_80000A88, baseAddress 0xA8000000, device type 3; a flash driver instead
    // issues the 0x78xxxxxx command writes librecomp's flashram path expects).
    game.save_type = recomp::SaveType::Flashram;
    game.is_enabled = true;
    // Must be SIGN-EXTENDED: entrypoint_address is a gpr (int64) and librecomp's MEM_*
    // macros compute `rdram + (addr - 0xFFFFFFFF80000000)`. The bare literal 0x80000400 is
    // unsigned and zero-extends to 0x0000000080000400, which yields a bogus +4GB rdram offset
    // in the initial boot DMA. Cast through int32_t so it sign-extends to 0xFFFFFFFF80000400.
    game.entrypoint_address = (gpr)(int32_t)0x80000400;
    game.entrypoint = recomp_entrypoint;
    game.on_init_callback = six_engine_init;
    supported_games.push_back(game);
    recomp::register_game(supported_games[0]);

    // Config/saves/stored-ROM location: %LOCALAPPDATA%\WCWRecompiled (or next to the exe
    // if a portable.txt file exists in the working dir — handled by get_app_folder_path).
    // librecomp doesn't create the folder itself, and select_rom's stored-ROM write plus
    // the json config writes all assume it exists.
    std::filesystem::path app_folder = arcade::app_folder();
    std::error_code app_folder_ec;
    std::filesystem::create_directories(app_folder, app_folder_ec);
    recomp::register_config_path(app_folder);
    // (firstrun 2026-11-17) A release ships the mod without game data: its tables made from the games are holes, filled here
    // from the player's ROM on the first start (arcade_firstrun.cpp; NM_FIRSTRUN=0 skips it). It writes assets\mods'
    // nomercy_arcade.nrm, which install_bundled_mods copies into mods\ as ever. A developer's install (no firstrun\) is left alone.
    // (startup 2026-11-19) run under the start-up status window: it opens only when something is made (the first start, a ROM
    // added or taken away, content\reimport.txt, a changed input, a commentary disc), and says what (arcade_startup_status.h)
    startup_status::run([] {
        arcade_firstrun_build();
        if (arcade_commentary_will_build())
            startup_status::say(startup_status::Kind::Making, "the commentary pack from the disc image in roms\\ -- in the background, while you play");
    });
    if (const char* only = getenv("NM_FIRSTRUN_ONLY"); only && strcmp(only, "1") == 0) return 0;   // (QA: the first-start builder alone)
    install_bundled_mods(app_folder);
    repair_bundled_mods_config(app_folder);   // (before recomp::start reads mods.json, 2026-11-13)
    // [commentary2 2026-11-18] the commentary pack from the player's WWE '13 disc image in roms\, the first time it is
    // there (a background thread; after the first-start builder, which shares content\firstrun.json)
    arcade_commentary_build();
    arcade::load_settings();
    register_mod_input_exports();
    nm_replay_init();      // [replay 2026-11-14] recording, replay, the per-retrace hashes, the no-graphics mode (nm_replay.cpp): off unless asked for
    wcw::register_wcw_overlays();
    // [wcw romhack] The runtime warns when a ROM-patch mod changes a function the post-recompile pipeline repaired
    // (generated from RecompiledFuncs/repaired_functions.txt; see CMakeLists).
    wcw_register_repaired_functions();
    // wcw::register_wcw_patches();  // bootstrap: no patches pipeline yet (see CMakeLists)

    // Local multiplayer (WCW is a 4-player game). recompinput's multiplayer mode normally
    // requires every pad to be claimed through the player-assignment modal before it produces
    // any input; our recompinput fork adds auto-assignment ([wcw fix]) so pads claim player
    // slots in plug-in order (pad 1 -> P1, pad 2 -> P2, ...). The keyboard always drives P1
    // via the default player-0 profiles, so solo keyboard/pad play needs no ceremony. The
    // "Assign players" flow in the Controls tab remains for custom arrangements (e.g. making
    // the keyboard P2). Must run before create_controls_tab (the tab snapshots the mode).
    recompinput::players::set_single_player_mode(false);
    recompinput::players::set_player_count_range(1, 4);

    // WCW-specific default bindings (must run before the config tabs load / any profile is
    // created — recompinput forbids changing defaults after first use). WCW's in-ring scheme
    // (manual): D-PAD moves, ANALOG STICK taunts, L = duck/dodge, R = block/guard, C-Down =
    // run, C-Up = climb/flip, C-Left/Right = switch focus, Z = unused. The generic recomp
    // defaults fit badly (movement not on left stick; duck on LB while block is on RT).
    // Layout here: move = left stick + d-pad; taunt = right stick; duck = LEFT trigger and
    // block = RIGHT trigger (mirrored defensive pair); grapple = A, attack = X, run = B,
    // climb/flip = Y, focus = LB/RB, Z parked on left-stick click.
    {
        using recompinput::GameInput;
        using recompinput::InputField;
        auto pad_digital = [](SDL_GameControllerButton b) { return InputField::controller_digital(b); };
        auto pad_analog = [](SDL_GameControllerAxis a, bool positive) { return InputField::controller_analog(a, positive); };

        recompinput::set_default_mapping_for_controller(GameInput::DPAD_LEFT,  { pad_digital(SDL_CONTROLLER_BUTTON_DPAD_LEFT),  pad_analog(SDL_CONTROLLER_AXIS_LEFTX, false) });
        recompinput::set_default_mapping_for_controller(GameInput::DPAD_RIGHT, { pad_digital(SDL_CONTROLLER_BUTTON_DPAD_RIGHT), pad_analog(SDL_CONTROLLER_AXIS_LEFTX, true) });
        recompinput::set_default_mapping_for_controller(GameInput::DPAD_UP,    { pad_digital(SDL_CONTROLLER_BUTTON_DPAD_UP),    pad_analog(SDL_CONTROLLER_AXIS_LEFTY, false) });
        recompinput::set_default_mapping_for_controller(GameInput::DPAD_DOWN,  { pad_digital(SDL_CONTROLLER_BUTTON_DPAD_DOWN),  pad_analog(SDL_CONTROLLER_AXIS_LEFTY, true) });
        recompinput::set_default_mapping_for_controller(GameInput::X_AXIS_NEG, { pad_analog(SDL_CONTROLLER_AXIS_RIGHTX, false) });
        recompinput::set_default_mapping_for_controller(GameInput::X_AXIS_POS, { pad_analog(SDL_CONTROLLER_AXIS_RIGHTX, true) });
        recompinput::set_default_mapping_for_controller(GameInput::Y_AXIS_POS, { pad_analog(SDL_CONTROLLER_AXIS_RIGHTY, false) });
        recompinput::set_default_mapping_for_controller(GameInput::Y_AXIS_NEG, { pad_analog(SDL_CONTROLLER_AXIS_RIGHTY, true) });
        recompinput::set_default_mapping_for_controller(GameInput::L,          { pad_analog(SDL_CONTROLLER_AXIS_TRIGGERLEFT, true) });
        recompinput::set_default_mapping_for_controller(GameInput::R,          { pad_analog(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, true) });
        recompinput::set_default_mapping_for_controller(GameInput::Z,          { pad_digital(SDL_CONTROLLER_BUTTON_LEFTSTICK) });
        recompinput::set_default_mapping_for_controller(GameInput::C_UP,       { pad_digital(recompinput::SDL_CONTROLLER_BUTTON_NORTH) });
        recompinput::set_default_mapping_for_controller(GameInput::C_DOWN,     { pad_digital(recompinput::SDL_CONTROLLER_BUTTON_EAST) });
        recompinput::set_default_mapping_for_controller(GameInput::C_LEFT,     { pad_digital(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) });
        recompinput::set_default_mapping_for_controller(GameInput::C_RIGHT,    { pad_digital(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) });

        // Keyboard: same semantics — WASD = move (d-pad), IJKL = taunt (analog stick).
        // The generic default had these reversed for WCW (WASD was the stick = taunt).
        recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_LEFT,  { InputField::keyboard(SDL_SCANCODE_A) });
        recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_RIGHT, { InputField::keyboard(SDL_SCANCODE_D) });
        recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_UP,    { InputField::keyboard(SDL_SCANCODE_W) });
        recompinput::set_default_mapping_for_keyboard(GameInput::DPAD_DOWN,  { InputField::keyboard(SDL_SCANCODE_S) });
        recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_NEG, { InputField::keyboard(SDL_SCANCODE_J) });
        recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_POS, { InputField::keyboard(SDL_SCANCODE_L) });
        recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_POS, { InputField::keyboard(SDL_SCANCODE_I) });
        recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_NEG, { InputField::keyboard(SDL_SCANCODE_K) });
    }

    // [arcade 2026-11-19] The bindings set in the F2 menu's CONTROLS pages (controls_n64.json beside the exe): the factory's
    // above kept, the file's rows made the defaults -- before any profile is made from them.
    arcade_pads_bindings_init();
    // Button mapping (controls.json), as it was saved; the F2 menu's CONTROLS page goes on from there.
    recompinput::profiles::load_controls_config(recomp::get_config_path() / "controls.json");
    arcade_pads_bindings_apply();     // (controls_n64.json's rows over controls.json's profiles too; the bindings in use, logged)

    // ROM intake. Default: the recompui launcher menu — first run shows "Load ROM" (nfd
    // file dialog, validated against the registered hash, then stored/remembered in the
    // config path); later runs show "Start Game" (librecomp's check_all_stored_roms, run
    // inside recomp::start, revalidates the stored copy). WCW_AUTOBOOT=<path|1> bypasses
    // the launcher for dev iteration (tools/cycle.ps1, capture scripts): loads the given
    // ROM path ("1" = wcw.z64 in the working dir) and boots immediately.
    std::u8string game_id = u8"wwf.nomercy.us";
    // (the mod's game-data tables are made from the player's ROM before this: arcade_firstrun_build, above)
    const char* autoboot = getenv("WCW_AUTOBOOT");
    bool boot_now = autoboot != nullptr;
    if (autoboot != nullptr) {
        std::filesystem::path autoboot_rom = (autoboot[0] != '\0' && strcmp(autoboot, "1") != 0)
            ? std::filesystem::path(autoboot) : std::filesystem::path("nomercy.z64");
        auto rom_err = recomp::select_rom(autoboot_rom, game_id);
        if (rom_err != recomp::RomValidationError::Good) {
            exit_error("WCW_AUTOBOOT ROM validation failed (err=%d) for %s.\n", (int)rom_err, arcade::platform::utf8(autoboot_rom).c_str());
        }
    }
#if NOMERCY_STANDALONE
    else {
        // [standalone] Straight into the game, with the ROM found (see standalone_select_rom).
        standalone_select_rom(game_id);
        boot_now = true;
    }
#endif

    // Callbacks.
    recomp::rsp::callbacks_t rsp_callbacks{ .get_rsp_microcode = get_rsp_microcode };
    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = [](uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode)
            -> std::unique_ptr<ultramodern::renderer::RendererContext> {
            extern uint8_t* g_ww_rdram;          // write-watch diag ([nomercy][ww] block below)
            g_ww_rdram = rdram;
            return arcade_view_make_renderer();
        },
    };
    ultramodern::gfx_callbacks_t gfx_callbacks{ .create_gfx = create_gfx, .create_window = create_window, .update_gfx = update_gfx };
    ultramodern::audio_callbacks_t audio_callbacks{ .queue_samples = queue_samples, .get_frames_remaining = get_frames_remaining, .set_frequency = set_frequency };
    ultramodern::input::callbacks_t input_callbacks{
        .poll_input = recompinput::poll_inputs,
        .get_input = get_n64_input_for_mods,
        .set_rumble = recompinput::set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };
    // update_rumble runs every VI to ramp/decay the host controller's rumble motor toward the
    // state last set via set_rumble (BMHero registers it the same way). [arcade] And the tests' fast-forward's speed:
    // arcade_speed_policy.
    ultramodern::events::callbacks_t events_callbacks{ .vi_callback = [] { recompinput::update_rumble(); arcade_speed_policy(); }, .gfx_init_callback = nullptr };
    ultramodern::error_handling::callbacks_t error_callbacks{ .message_box = arcade::message_box };
    ultramodern::threads::callbacks_t threads_callbacks{ .get_game_thread_name = [](const OSThread*) -> std::string { return "game"; } };

    // Dev auto-boot (and standalone): kick off the game once the runtime is up. In the normal flow the
    // launcher's Start Game option calls recomp::start_game from its click handler.
    if (boot_now) {
        std::thread starter([game_id]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::u8string gid = game_id;
            recomp::start_game(gid, "");
        });
        starter.detach();
    }

#ifdef _WIN32
    // DIAGNOSTIC watchdog (opt-in): set WCW_SAMPLE=<seconds> to dump all thread stacks that
    // many seconds in (WCW_SAMPLE=1 keeps the old ~4s default), to locate a stall.
    if (const char *wcwSampleEnv = getenv("WCW_SAMPLE")) {
        int wcwSampleDelay = atoi(wcwSampleEnv);
        if (wcwSampleDelay <= 1) wcwSampleDelay = 4;
        std::thread([wcwSampleDelay]() {
            std::this_thread::sleep_for(std::chrono::seconds(wcwSampleDelay));
            wcw_sample_all_threads();
        }).detach();
    }
#endif

    qa_perf_start();      // [qa12x 2026-11-17] (WCW_QA_PERF: the tests' pace line, above)

    // DIAGNOSTIC (opt-in): WCW_QUIT_AFTER=<seconds> requests a graceful quit (the same
    // ultramodern::quit() the quit prompt's "Quit" button calls), so scripted test runs can
    // end without the confirm dialog or a hard kill.
    if (const char *wcwQuitEnv = getenv("WCW_QUIT_AFTER")) {
        const int wcwQuitDelay = atoi(wcwQuitEnv);
        if (wcwQuitDelay > 0) {
            std::thread([wcwQuitDelay]() {
                // (counted from the game's first frame: a slow start-up -- ~85 s with the GPU busy -- ate a short run's
                // time and the quit came before the game had started, 2026-10-29)
                for (int i = 0; i < 6000 && wcw_viswaps_total.load() == 0; i++)   // (10 minutes at most)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (fast_forward_speed() > 1) {
                    // (fast-forward: the seconds are the game's -- its clock runs at more than one speed, so qa.ps1
                    //  passes the scenario's own seconds and this waits for that much of the game's time)
                    const uint64_t from = ultramodern::game_ticks_now();
                    while (ultramodern::game_ticks_now() - from < (uint64_t)wcwQuitDelay * 46'875'000ull)
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else
                    std::this_thread::sleep_for(std::chrono::seconds(wcwQuitDelay));
                fprintf(stderr, "[wcw] WCW_QUIT_AFTER=%d: quitting\n", wcwQuitDelay);
                // (how often each of the mods' hooks was reached, when NM_HOOK_COUNTS asks: here, while the game still
                //  runs, since how the process ends isn't one path. 2026-11-11)
                recomp::mods::write_hook_counts();
#ifdef _WIN32
                crash_report_qa_snapshot();   // (a QA run's RAM as it ends, while it's still there)
                g_mw_stop = true;             // (the write-watch done before the game stops: its RAM goes)
#endif
                ultramodern::quit();
            }).detach();
        }
    }

#ifdef _WIN32
    // [nomercy diag] WCW2K_WRITEWATCH=<vram hex>: hardware watchpoint (DR0, 4-byte write) on an
    // rdram word, applied to every thread in the process by a poller. Catches the exact host
    // RIP of whoever writes the word — built for the deterministic post-attract crash where
    // func_800E2704's saved-s0 stack slot (0x80083900) is stomped with 1 by an actor every
    // indirect probe (mesg sends, RSP tasks, scheduler clients, fb clears) has acquitted.
    // Symbolize logged RIPs offline: rip - base == the NoMercyRecompiled+0x... offset used by
    // tools/annotate_stacks.py.
    // [arcade] QA's write-watch, the mod's addresses (WCW_WATCH_MOD, set by qa.ps1): up to four words the arcade
    // names at 0x807F2F10 ('WTCH', then the four, 0: none), watched with the CPU's debug registers on every thread
    // and watched anew when they change. A write to one is in the log with what wrote it (the instruction, the mod's
    // function for code of the mod's, the frames above it). For the Royal Rumble's "never-ending display list"
    // (2026-10-27): a ported model's part 1 palette, 32 bytes at +0xE0 written over -- its display list's end.
    if (getenv("WCW_WATCH_MOD")) {
        static volatile uint32_t mw_vram[4] = {};
        static uint8_t* mw_base = nullptr;
        static volatile long mw_hits = 0;
        // (the first MW_KEEP writes caught, noted by the handler and printed by the watcher thread)
        struct MwHit { volatile long ready; uint32_t addr, now, guard[6]; DWORD tid; int frames; uint64_t rip[16]; };
        static constexpr long MW_KEEP = 60;
        static MwHit mw_hit[MW_KEEP];
        // (2026-11-12, crash32: WCW2K_WRITEWATCH's word rides along here, in the last debug register, when both are on.
        //  The two watches used to share DR0: this watcher re-arms every thread with its own list whenever the mod's
        //  words change -- at a match's start -- and that cleared the other's DR0, so WCW2K_WRITEWATCH saw nothing of
        //  the match (the rumble_ring32 watch runs of 2026-11-12 14:25-14:34). Its writes are printed at once, from
        //  the thread that wrote: the one it's for is a crash a frame later, before the watcher thread would print.)
        static const uint32_t mw_fixed = [] { const char* v = getenv("WCW2K_WRITEWATCH"); return v ? (uint32_t)strtoul(v, nullptr, 16) & ~3u : 0u; }();
        AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* ep) -> LONG {
            if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
            DWORD64 dr6 = ep->ContextRecord->Dr6;
            if (!(dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
            if (g_mw_stop) { ep->ContextRecord->Dr6 = 0; ep->ContextRecord->Dr7 = 0; return EXCEPTION_CONTINUE_EXECUTION; }   // (shutting down: the RAM may be gone)
            if (mw_fixed && (dr6 & 8) && mw_base) {
                const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
                fprintf(stderr, "[nomercy][ww] watch hit: rip=NoMercyRecompiled+0x%llX tid=%lu value_now=%08X\n",
                    (unsigned long long)(ep->ContextRecord->Rip - base), GetCurrentThreadId(),
                    *(volatile uint32_t*)(mw_base + (mw_fixed - 0x80000000u)));
                fflush(stderr);
            }
            // The caught write is only noted here (no printing, no file: the game's own thread is the one usually
            // caught, and printing a stack from it, line by line, held a Royal Rumble entrant's loading up for 15-17 s
            // in the 32-slot tests, 2026-11-05); the watcher thread below prints what's noted.
            {
                const long i = InterlockedIncrement(&mw_hits) - 1;
                if (i < MW_KEEP) {
                    MwHit& h = mw_hit[i];
                    for (int k = 0; k < 4; k++) if (dr6 & (1ull << k)) { h.addr = mw_vram[k]; break; }
                    h.now = mw_base && h.addr ? *(volatile uint32_t*)(mw_base + (h.addr - 0x80000000u)) : 0;
                    h.tid = GetCurrentThreadId();
                    if (mw_base) {   // (skel.c pal_group_guard's last call: part, palette, room, at | n << 16, files, picture)
                        const volatile uint32_t* g = (const volatile uint32_t*)(mw_base + 0x7F2F30u);
                        for (int k = 0; k < 6; k++) h.guard[k] = g[k];
                    }
                    CONTEXT c = *ep->ContextRecord;
                    int n = 0;
                    for (; n < 16 && c.Rip; n++) {
                        h.rip[n] = c.Rip;
                        DWORD64 img = 0;
                        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &img, nullptr);
                        if (!fe) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; continue; }
                        PVOID hd = nullptr; DWORD64 est = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, c.Rip, fe, &c, &hd, &est, nullptr);
                    }
                    h.frames = n;
                    MemoryBarrier();
                    h.ready = 1;
                }
            }
            ep->ContextRecord->Dr6 = 0;
            // The thread resumes with this context's debug registers -- the ones it had when the write was caught. If the
            // watcher thread re-armed it meanwhile (this handler prints slowly), returning those would put the old watch
            // back, and the watcher, which counts the thread as done, would never fix it: a watch left on a stale address
            // trapped every write of the loading that followed (2026-11-05, hennig_win_taunt on the 32-slot layout: the
            // next match took 57 s to load, not 0.6). So it resumes with the watch as it stands now.
            {
                CONTEXT* c = ep->ContextRecord;
                DWORD64* dr[4] = { &c->Dr0, &c->Dr1, &c->Dr2, &c->Dr3 };
                DWORD64 dr7 = c->Dr7 & ~0xFFFF00FFull;
                for (int k = 0; k < 4; k++) {
                    const uint32_t v = mw_vram[k];
                    *dr[k] = v ? (DWORD64)(mw_base + (v - 0x80000000u)) : 0;
                    if (v) dr7 |= (1ull << (2 * k)) | (0b01ull << (16 + 4 * k)) | (0b11ull << (18 + 4 * k));   // local, write, 4 bytes
                }
                c->Dr7 = dr7;
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        });
        std::thread([]() {
            extern uint8_t* g_ww_rdram;
            while (g_ww_rdram == nullptr) Sleep(100);
            mw_base = g_ww_rdram;
            const DWORD self = GetCurrentThreadId();
            uint32_t armed_for[4] = {};
            std::set<DWORD> armed;
            long printed = 0;
            int looks_left = 0;   // (passes of the loop until the next look for threads to arm, below)
            for (;;) {
                Sleep(100);
                // what the handler noted since last time
                for (; printed < MW_KEEP && mw_hit[printed].ready; printed++) {
                    const MwHit& h = mw_hit[printed];
                    const uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
                    fprintf(stderr, "[arcade][watch] write to 0x%08X (now %08X) by thread %lu\n", h.addr, h.now, h.tid);
                    fprintf(stderr, "[arcade][watch]   guard's last: part %08X palette %08X room %08X at|n %08X files %08X picture %08X\n",
                        h.guard[0], h.guard[1], h.guard[2], h.guard[3], h.guard[4], h.guard[5]);
                    for (int i = 0; i < h.frames; i++) {
                        char live[256];
                        if (h.rip[i] >= base && h.rip[i] < base + 0x10000000ull)
                            fprintf(stderr, "[arcade][watch]   #%d exe+0x%llX\n", i, (unsigned long long)(h.rip[i] - base));
                        else if (n64recomp_live_symbolize((uintptr_t)h.rip[i], live, sizeof(live)))
                            fprintf(stderr, "[arcade][watch]   #%d %s\n", i, live);
                        else fprintf(stderr, "[arcade][watch]   #%d %p\n", i, (void*)h.rip[i]);
                    }
                    fflush(stderr);
                }
                g_mw_reading = true;
                if (g_mw_stop) { g_mw_reading = false; return; }
                const uint32_t* blk = (const uint32_t*)(mw_base + 0x7F2F10u);
                uint32_t want[4] = {};
                if (blk[0] == 0x57544348u) for (int k = 0; k < 4; k++) { uint32_t v = blk[1 + k]; want[k] = (v >= 0x80000400u && v < 0x80800000u) ? (v & ~3u) : 0; }
                g_mw_reading = false;
                if (mw_fixed) want[3] = mw_fixed;   // (WCW2K_WRITEWATCH's: the last register, from the start; see above)
                if (memcmp(want, armed_for, sizeof(want)) != 0) {
                    for (int k = 0; k < 4; k++) mw_vram[k] = want[k];
                    memcpy(armed_for, want, sizeof(want));
                    armed.clear();
                    fprintf(stderr, "[arcade][watch] watching %08X %08X %08X %08X\n", want[0], want[1], want[2], want[3]); fflush(stderr);
                    looks_left = 0;   // (new words: every thread again, now)
                }
                // (2026-11-13, agent "perf": the snapshot below is of every thread of every process on the PC, ~0.35 s of CPU
                //  a second on a busy one -- more than the game itself used, in every QA run. Taken every 2 s, not every
                //  100 ms, unless the words just changed: a thread the game starts is armed within 2 s; the hits are still
                //  printed every 100 ms.)
                if (looks_left-- > 0) continue;
                looks_left = 19;
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snap == INVALID_HANDLE_VALUE) continue;
                THREADENTRY32 te{ sizeof(te) };
                for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                    if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self || armed.count(te.th32ThreadID)) continue;
                    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                    if (!th) continue;
                    // (Nothing may allocate or print while the thread is suspended: it may hold the heap's or stderr's lock,
                    //  and this thread would wait on it for ever with the other never resumed -- the freeze of 2026-11-05,
                    //  hennig_win_taunt, the game thread stopped in the watch's fprintf. So `armed` is added to after
                    //  ResumeThread.)
                    bool set_ok = false;
                    if (SuspendThread(th) != (DWORD)-1) {
                        CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(th, &c)) {
                            DWORD64 dr7 = c.Dr7 & ~0xFFFF00FFull;
                            DWORD64* dr[4] = { &c.Dr0, &c.Dr1, &c.Dr2, &c.Dr3 };
                            for (int k = 0; k < 4; k++) {
                                *dr[k] = armed_for[k] ? (DWORD64)(mw_base + (armed_for[k] - 0x80000000u)) : 0;
                                if (armed_for[k]) dr7 |= (1ull << (2 * k)) | (0b01ull << (16 + 4 * k)) | (0b11ull << (18 + 4 * k));   // local, write, 4 bytes
                            }
                            c.Dr7 = dr7;
                            set_ok = SetThreadContext(th, &c) != 0;
                        }
                        ResumeThread(th);
                    }
                    if (set_ok) armed.insert(te.th32ThreadID);
                    CloseHandle(th);
                }
                CloseHandle(snap);
            }
        }).detach();
    }
    if (const char* ww = getenv("WCW2K_WRITEWATCH"); ww && getenv("WCW_WATCH_MOD")) {
        fprintf(stderr, "[nomercy][ww] armed, target vram %s (in the QA watch's last debug register)\n", ww);
    } else if (ww) {
        fprintf(stderr, "[nomercy][ww] armed, target vram %s\n", ww);
        static uint32_t ww_vram = (uint32_t)strtoul(ww, nullptr, 16);
        static volatile uint32_t* ww_word = nullptr;
        AddVectoredExceptionHandler(1, [](EXCEPTION_POINTERS* ep) -> LONG {
            if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
                uint64_t base = (uint64_t)GetModuleHandleA(nullptr);
                uint64_t rip = ep->ContextRecord->Rip;
                fprintf(stderr, "[nomercy][ww] watch hit: rip=NoMercyRecompiled+0x%llX tid=%lu value_now=%08X\n",
                    (unsigned long long)(rip - base), GetCurrentThreadId(), ww_word ? *ww_word : 0);
                // [nomercy diag] host backtrace for the first hits: recomp functions are real
                // host functions, so the return-address chain IS the game call chain
                // (symbolize each NoMercyRecompiled+0x offset against the .map). Needed for
                // the rope-matrix hunt: the RIP alone landed in guMtxF2L (a leaf converter);
                // the culprit is the caller that hands it an identity float matrix.
                static volatile long ww_bt_n = 0;
                // [nomercy diag] WCW2K_WW_BT=<n> raises the backtrace cap (default 12) —
                // needed when the interesting hits come late (registry-freeze hunt).
                static long ww_bt_max = [] { const char* v = getenv("WCW2K_WW_BT"); return v ? atol(v) : 12L; }();
                if (InterlockedIncrement(&ww_bt_n) <= ww_bt_max) {
                    CONTEXT c = *ep->ContextRecord;
                    for (int i = 0; i < 14 && c.Rip; i++) {
                        if (c.Rip >= base && c.Rip < base + 0x10000000ull)
                            fprintf(stderr, "[nomercy][ww]   bt[%d]=NoMercyRecompiled+0x%llX\n", i, (unsigned long long)(c.Rip - base));
                        DWORD64 imgBase = 0;
                        PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imgBase, nullptr);
                        if (fe == nullptr) { // leaf frame: return address sits at Rsp
                            c.Rip = *(DWORD64*)c.Rsp;
                            c.Rsp += 8;
                            continue;
                        }
                        PVOID handlerData = nullptr;
                        DWORD64 establisher = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imgBase, c.Rip, fe, &c, &handlerData, &establisher, nullptr);
                    }
                }
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            return EXCEPTION_CONTINUE_SEARCH;
        });
        std::thread([]() {
            // rdram pointer is stashed by the create_render_context callback.
            extern uint8_t* g_ww_rdram;
            while (g_ww_rdram == nullptr) { Sleep(100); }
            uint8_t* rdram_base = g_ww_rdram;
            uint64_t watch_addr = (uint64_t)(rdram_base + (ww_vram - 0x80000000u));
            ww_word = (volatile uint32_t*)watch_addr;
            fprintf(stderr, "[nomercy][ww] watching vram 0x%08X at host %p\n", ww_vram, (void*)watch_addr);
            std::set<DWORD> armed;
            const DWORD self = GetCurrentThreadId();
            while (true) {
                HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                if (snap != INVALID_HANDLE_VALUE) {
                    THREADENTRY32 te{ sizeof(te) };
                    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
                        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
                        if (armed.count(te.th32ThreadID)) continue;
                        HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID);
                        if (!th) continue;
                        bool set_ok = false;   // (armed is added to after ResumeThread: no allocating while it's suspended)
                        if (SuspendThread(th) != (DWORD)-1) {
                            CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                            if (GetThreadContext(th, &c)) {
                                c.Dr0 = watch_addr;
                                c.Dr7 = (c.Dr7 & ~0xF0003ull) | 0x1 | (0b01ull << 16) | (0b11ull << 18); // local DR0, write, 4 bytes
                                set_ok = SetThreadContext(th, &c) != 0;
                            }
                            ResumeThread(th);
                        }
                        if (set_ok) armed.insert(te.th32ThreadID);
                        CloseHandle(th);
                    }
                    CloseHandle(snap);
                }
                Sleep(300);
            }
        }).detach();
    }
#endif

    // NOTE: graphics-API init (both D3D12 and Vulkan) aborts in a headless/virtual-display
    // session even with a GPU present; it should initialize on a normal local desktop.
    // Leave api_option at its default (Auto -> D3D12 on Windows). To override for testing:
    //   auto gcfg = ultramodern::renderer::get_graphics_config();
    //   gcfg.api_option = ultramodern::renderer::GraphicsApi::Vulkan;  // or D3D12
    //   ultramodern::renderer::set_graphics_config(gcfg);

    arcade_vi_mode_or_quit(game_id);   // [relclean 2026-11-19] (before the runtime's threads start: the VI thread shows it)
    fprintf(stderr, "[wcw] calling recomp::start...\n");
    recomp::start(version, {}, rsp_callbacks, renderer_callbacks, audio_callbacks,
        input_callbacks, gfx_callbacks, events_callbacks, error_callbacks, threads_callbacks);
    g_game_stopped = true;
    fprintf(stderr, "[wcw] recomp::start returned\n");
    // (and again here, with the last calls in: a scenario that ends the game itself never comes by WCW_QUIT_AFTER's
    //  thread -- versus_1p left no counts, 2026-11-11. Nothing is written if the mods are unloaded by now.)
    recomp::mods::write_hook_counts();
#ifdef _WIN32
    g_mw_stop = true;
    while (g_mw_reading) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    crash_report_shutdown();
#endif

    // The game has stopped and everything it keeps is saved: the process ends here, without the teardown of
    // statics and libraries after main -- that freed the live-recompiled code while a thread of the port's could
    // still be in it, an access violation at nearly one quit in four in QA (2026-10-27/28: "recomp::start returned",
    // then the crash, its report empty).
    fflush(stdout);
    fflush(stderr);
    std::_Exit(0);
}
