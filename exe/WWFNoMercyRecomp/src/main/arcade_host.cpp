// [arcade] Where the game's things are, and the settings of the input and the sound. The operator menu (F2) sets them (recomp_arcade_host); before the mod's first word, what was saved in general.json and sound.json, as the old settings screens left them.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include "arcade_platform.h"     // ([xplat 2026-11-12] the program's folder, the settings folder, the message box: one call each, per system)
#include "json/json.hpp"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"
#include "recompinput/host.h"
#include "arcade_host.h"

int arcade_view_toggle_window();
namespace {
std::atomic<int> volume_{ 100 }, rumble_{ 25 }, deadzone_{ 5 }, mouse_{ 0 }, gyro_{ 0 };
int number_of(const nlohmann::json& j, const char* k, int fallback) { auto it = j.find(k); return it != j.end() && it->is_number() ? int(it->get<double>()) : fallback; }
nlohmann::json read_json(const std::filesystem::path& p) { std::ifstream f(p); if (!f.good()) return nlohmann::json::object(); return nlohmann::json::parse(f, nullptr, false); }
// recomp_arcade_host(volume, rumble, deadzone): the operator menu's
void host_set(uint8_t* rdram, recomp_context* ctx) { volume_ = _arg<0, int>(rdram, ctx); rumble_ = _arg<1, int>(rdram, ctx); deadzone_ = _arg<2, int>(rdram, ctx); }
}
std::filesystem::path arcade::program_folder() {
    return platform::executable_path().parent_path();
}
std::filesystem::path arcade::app_folder() {
    if (std::filesystem::exists("portable.txt")) return std::filesystem::current_path();
    if (std::filesystem::exists(program_folder() / "portable.txt")) return program_folder();
    const std::filesystem::path known = platform::local_data_folder();
    return known.empty() ? known : known / "NoMercyRecompiled";
}
std::string arcade::shown(const std::filesystem::path& p) {
    if (p.empty() || p.is_relative()) return platform::utf8(p);     // (already relative: as it is)
    // (by path elements, not text: "...\NoMercy" isn't inside "...\No")
    const auto under = [&](const std::filesystem::path& base, std::string& out) {
        if (base.empty()) return false;
        const std::filesystem::path rel = p.lexically_normal().lexically_relative(base.lexically_normal());
        if (rel.empty() || *rel.begin() == "..") return false;
        out = platform::utf8(rel);
        return true;
    };
    std::string rel;
    if (under(program_folder(), rel)) return rel;
    if (under(app_folder(), rel)) return platform::utf8(std::filesystem::path("settings") / rel);   // (%LOCALAPPDATA%\... without portable.txt)
    return platform::utf8(p.filename());
}
int arcade::main_volume() { return volume_.load(); }
void arcade::load_settings() {
    const nlohmann::json g = read_json(app_folder() / "general.json"), so = read_json(app_folder() / "sound.json");
    if (g.is_object()) { rumble_ = number_of(g, "rumble_strength", rumble_); deadzone_ = number_of(g, "joystick_deadzone", deadzone_); mouse_ = number_of(g, "mouse_sensitivity", 0); gyro_ = number_of(g, "gyro_sensitivity", 0); }
    if (so.is_object()) volume_ = number_of(so, "main_volume", volume_);
}
void arcade::message_box(const char* text) { fprintf(stderr, "[message] %s\n", text); fflush(stderr); platform::message_box("No Mercy", text); }
double recompinput::host::rumble_strength() { return rumble_.load(); }
double recompinput::host::gyro_sensitivity() { return gyro_.load(); }
double recompinput::host::mouse_sensitivity() { return mouse_.load(); }
double recompinput::host::joystick_deadzone() { return deadzone_.load(); }
void recompinput::host::toggle_fullscreen() { arcade_view_toggle_window(); }
static void heap_test(uint8_t* rdram, recomp_context* ctx) { const char* v = getenv("WCW_HEAP_TEST"); _return<int32_t>(ctx, v ? atoi(v) : 0); }
static void heap_note(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t low = _arg<0, uint32_t>(rdram, ctx), high = _arg<1, uint32_t>(rdram, ctx), big = _arg<2, uint32_t>(rdram, ctx);
    fprintf(stderr, "[heap] first region: %u KB free (largest %u KB); second: %u KB in use\n", low / 64u, big / 64u, 0xFFFFFFu > high ? (0xFFFFFFu - high) / 64u : 0u);
    fflush(stderr);
}
static void scene_dropped(uint8_t* rdram, recomp_context* ctx) { static int said; if (said++ < 8) { fprintf(stderr, "[entrance] a pending scene request became scene %d: dropped\n", _arg<0, int32_t>(rdram, ctx)); fflush(stderr); } }
void arcade_host_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_scene_dropped", scene_dropped);
    recomp::overlays::register_base_export("recomp_arcade_host", host_set);
    recomp::overlays::register_base_export("recomp_arcade_heap_test", heap_test);
    recomp::overlays::register_base_export("recomp_arcade_heap_note", heap_note);
}
