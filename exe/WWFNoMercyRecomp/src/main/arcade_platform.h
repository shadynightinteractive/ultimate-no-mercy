#pragma once
// [xplat 2026-11-12] The few things the port asks of the operating system, behind one small interface, so the rest of
// the port's code is the same on every system. Each system has one file with the answers:
//   arcade_platform_win32.cpp   Windows (the calls the port always made, moved here unchanged)
//   arcade_platform_posix.cpp   Linux, the Steam Deck and the Mac
// CMakeLists.txt builds the one that fits. Nothing here is about the picture or the sound: those have their own
// interfaces (the renderer's backend, SDL's audio).
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace arcade::platform {
    std::filesystem::path executable_path();      // the running program's own file
    std::filesystem::path local_data_folder();    // where a program keeps its settings: %LOCALAPPDATA%, ~/.local/share, ~/Library/Application Support (empty: unknown)
    void message_box(const char* title, const char* text);     // an error the player must see (UTF-8), waits for OK
    uint64_t ticks();                             // a steady clock, for timing stretches of work
    uint64_t ticks_per_second();
    std::string utf8(const std::filesystem::path& p);          // a path as UTF-8 text, for the log (one way on every system)
    // [portable 2026-11-15] another program run, no console window, waited for: its exit code (-1: it could not be started).
    // For the first start's builder (main.cpp arcade_portable_first_run); args are UTF-8.
    int run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args);
}
