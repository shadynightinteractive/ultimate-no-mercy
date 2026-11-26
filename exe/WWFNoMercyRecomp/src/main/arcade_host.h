#pragma once
#include <filesystem>
#include <string>
namespace arcade {
    std::filesystem::path program_folder();     // beside the exe: the game's own files
    std::filesystem::path app_folder();         // settings and saves: beside the exe with portable.txt, else %LOCALAPPDATA%\\NoMercyRecompiled
    int main_volume();                          // 0-100
    void load_settings();                       // what the operator menu hasn't set yet: general.json, sound.json
    void message_box(const char* text);
    // [privacy 2026-11-13] A path as the log, the crash reports and the messages show it: relative to the game's folder
    // ("content\hd.bin"), or to the settings folder ("settings\crash_reports\..."), never the folders above them -- the
    // player's user name and the PC's own folders stay out of what the game writes. Anything else shows its name alone.
    std::string shown(const std::filesystem::path& p);
}
