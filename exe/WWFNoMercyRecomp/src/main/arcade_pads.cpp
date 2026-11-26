// [arcade] Controllers for the No Mercy Arcade mod (its operator menu, F2), the way the arcade
// ports have them (Terrordrome's operator menu: CONTROLS):
//   - known layouts: SDL's own, recompcontrollerdb.txt, gamecontrollerdb.txt (the community database,
//     about 2,000 controllers) and controller_map.txt (the ones set up in the operator menu), all next
//     to the exe;
//   - anything else (arcade encoders, odd pads): CONTROLLER SETUP, each control pressed in turn; the
//     result is an SDL layout for that controller (its GUID), saved to controller_map.txt;
//   - player 1 = the first controller, player 2 the second (SWAP CONTROLLERS 1/2);
//   - the pad's BACK / SELECT is the player's coin.
// The mod calls these through RECOMP_IMPORT (recomp_arcade_*). Strings go in and out of the game's
// memory a byte at a time (MEM_B: the game's byte order).
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>

#include "SDL.h"
#include "json/json.hpp"
#include "ultramodern/ultramodern.hpp"
#include "arcade_host.h"
#include "recompinput/recompinput.h"
#include "recompinput/players.h"
#include "recompinput/profiles.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"

namespace {

std::filesystem::path program_folder() { return arcade::program_folder(); }

// The controller a player has: the one recompinput gave the player (a known layout), else one of the
// controllers without a layout (raw joysticks: opened here), in the order they're plugged in.
std::vector<SDL_Joystick*> raw_joys;    // (opened raw joysticks, by device order)

void refresh_raw() {
    // close the ones gone or that have a layout now (recompinput has them as controllers)
    for (size_t i = 0; i < raw_joys.size();) {
        SDL_Joystick* j = raw_joys[i];
        bool gone = !SDL_JoystickGetAttached(j);
        SDL_JoystickID id = SDL_JoystickInstanceID(j);
        bool now_known = false;
        for (int d = 0; d < SDL_NumJoysticks(); d++)
            if (SDL_JoystickGetDeviceInstanceID(d) == id && SDL_IsGameController(d)) now_known = true;
        if (gone || now_known) {
            SDL_JoystickClose(j);
            raw_joys.erase(raw_joys.begin() + i);
        } else {
            i++;
        }
    }
    for (int d = 0; d < SDL_NumJoysticks(); d++) {
        if (SDL_IsGameController(d) || SDL_JoystickIsVirtual(d)) continue;
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(d);
        bool have = false;
        for (SDL_Joystick* j : raw_joys) if (SDL_JoystickInstanceID(j) == id) have = true;
        if (!have) {
            SDL_Joystick* j = SDL_JoystickOpen(d);
            if (j != nullptr) raw_joys.push_back(j);
        }
    }
}

SDL_GameController* player_controller(int player) {
    if (player < 0 || player > 1) return nullptr;
    if (!recompinput::players::get_player_is_assigned(player)) return nullptr;
    return recompinput::players::get_player(player).controller;
}

// the joystick behind a player's controls (a known controller's, or a raw one)
SDL_Joystick* player_joystick(int player) {
    if (SDL_GameController* c = player_controller(player)) return SDL_GameControllerGetJoystick(c);
    refresh_raw();
    // raw joysticks go to the players without a controller, in order
    int k = 0;
    for (int p = 0; p < 2; p++) {
        if (player_controller(p) != nullptr) continue;
        if (p == player) return k < (int)raw_joys.size() ? raw_joys[k] : nullptr;
        k++;
    }
    return nullptr;
}

void put_string(uint8_t* rdram, gpr addr, int len, const std::string& s) {
    int n = 0;
    for (; n < (int)s.size() && n < len - 1; n++) {
        char c = s[n];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c < 32 || c > 126) c = '?';
        MEM_B(n, addr) = (uint8_t)c;
    }
    MEM_B(n, addr) = 0;
}

std::string get_string(uint8_t* rdram, gpr addr, int max) {
    std::string s;
    for (int i = 0; i < max; i++) {
        char c = (char)MEM_B(i, addr);
        if (!c) break;
        s += c;
    }
    return s;
}

// ---- controller setup ---------------------------------------------------------------------------
Sint16 rest[2][16];
int settle[2];

std::string learn_read(int player) {
    SDL_Joystick* j = player_joystick(player);
    if (j == nullptr) return "";
    if (settle[player] > 0) {                       // (the axes' resting values, the first few reads)
        settle[player]--;
        for (int a = 0; a < 16 && a < SDL_JoystickNumAxes(j); a++) rest[player][a] = SDL_JoystickGetAxis(j, a);
        return "?";
    }
    char sz[16];
    for (int b = 0; b < SDL_JoystickNumButtons(j) && b < 64; b++)
        if (SDL_JoystickGetButton(j, b)) { snprintf(sz, sizeof sz, "b%d", b); return sz; }
    for (int h = 0; h < SDL_JoystickNumHats(j) && h < 4; h++) {
        Uint8 v = SDL_JoystickGetHat(j, h);
        if (v == SDL_HAT_UP || v == SDL_HAT_DOWN || v == SDL_HAT_LEFT || v == SDL_HAT_RIGHT) {
            snprintf(sz, sizeof sz, "h%d.%d", h, v);
            return sz;
        }
    }
    for (int a = 0; a < SDL_JoystickNumAxes(j) && a < 16; a++) {
        int v = SDL_JoystickGetAxis(j, a), r = rest[player][a];
        if (abs(v - r) < 20000) continue;
        if (r < -16000) snprintf(sz, sizeof sz, "a%d", a);            // (a trigger resting at one end)
        else if (r > 16000) snprintf(sz, sizeof sz, "a%d~", a);
        else snprintf(sz, sizeof sz, "%ca%d", v > r ? '+' : '-', a);
        return sz;
    }
    return "";
}

// The layout's targets, in the order the mod asks for the controls (its CONTROLLER SETUP): the pad
// buttons the game's default bindings put the N64's controls on (main.cpp's defaults: move on the
// d-pad / left stick, A south, B west, Z left stick click, L / R the triggers, C up / down north /
// east, C left / right the shoulders), START, and BACK for the coin.
const char* const targets[] = { "dpup", "dpdown", "dpleft", "dpright", "a", "x", "leftstick", "lefttrigger",
    "righttrigger", "y", "b", "leftshoulder", "rightshoulder", "start", "back" };
constexpr int n_targets = sizeof(targets) / sizeof(targets[0]);

bool learn_apply(int player, const std::vector<std::string>& el) {
    SDL_Joystick* j = player_joystick(player);
    if (j == nullptr) return false;
    bool any = false;
    for (const std::string& e : el) if (!e.empty()) any = true;
    if (!any) return false;                         // (all skipped: the layout stays as it was)
    char guid[40];
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof guid);
    std::string name = SDL_JoystickName(j) ? SDL_JoystickName(j) : "";
    for (char& c : name) if (c == ',') c = ' ';
    if (name.empty()) name = "Arcade controller";
    std::string map = std::string(guid) + "," + name + ",";
    for (int k = 0; k < n_targets && k < (int)el.size(); k++) {
        if (el[k].empty()) continue;
        // (2026-11-09) A button on a whole axis (`a4`) counts as pressed from the middle of the axis up, so one that reads 0 at rest is
        // held for good: a Switch Pro Controller's ZL / ZR before their first press showed C-right stuck on. A button gets the half of
        // the axis it moves into. The triggers are axes in the layout and keep the whole range.
        std::string e = el[k];
        bool trigger = strcmp(targets[k], "lefttrigger") == 0 || strcmp(targets[k], "righttrigger") == 0;
        if (!trigger && e[0] == 'a') e = e.back() == '~' ? "-" + e.substr(0, e.size() - 1) : "+" + e;
        map += std::string(targets[k]) + ":" + e + ",";
    }
    map += "platform:Windows,";
    int r = SDL_GameControllerAddMapping(map.c_str());
    fprintf(stderr, "[arcade] controller setup (player %d): %s -> %d %s\n", player + 1, map.c_str() + std::string(guid).size() + 1 + name.size() + 1, r, r < 0 ? SDL_GetError() : "");   // (privacy 2026-11-13: the layout without the controller's ID and name)
    // controller_map.txt: this GUID's line replaced
    std::filesystem::path file = program_folder() / "controller_map.txt";
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
            if (!line.empty() && line.compare(0, strlen(guid), guid) != 0) lines.push_back(line);
    }
    lines.push_back(map);
    std::ofstream out(file, std::ios::trunc);
    for (const std::string& l : lines) out << l << "\n";
    return r >= 0;
}

// ---- the exports ---------------------------------------------------------------------------------
// recomp_arcade_pad_name(player, buf, len) -> 1 when the player has a controller (its name in buf; "NONE")
void pad_name(uint8_t* rdram, recomp_context* ctx) {
    int player = _arg<0, int>(rdram, ctx);
    gpr buf = ctx->r5;
    int len = _arg<2, int>(rdram, ctx);
    std::string name = "NONE";
    int ret = 0;
    if (SDL_GameController* c = player_controller(player)) {
        const char* n = SDL_GameControllerName(c);
        name = n ? n : "CONTROLLER";
        ret = 1;
    } else if (SDL_Joystick* j = player_joystick(player)) {
        const char* n = SDL_JoystickName(j);
        name = std::string(n ? n : "CONTROLLER") + " (NO LAYOUT)";
        ret = 2;
    }
    if (len > 0) put_string(rdram, buf, len, name);
    _return(ctx, ret);
}

// recomp_arcade_pad_buttons(player) -> bit 0: BACK (coin), bit 1: START, bit 2: SOUTH, bit 3: EAST,
// bits 4-7: d-pad up, down, left, right (the left stick counts), for the menus and the input test
void pad_buttons(uint8_t* rdram, recomp_context* ctx) {
    int player = _arg<0, int>(rdram, ctx);
    uint32_t bits = 0;
    if (!recompinput::game_input_disabled()) {
        if (SDL_GameController* c = player_controller(player)) {
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK)) bits |= 1;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START)) bits |= 2;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A)) bits |= 4;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B)) bits |= 8;
            int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -16000) bits |= 0x10;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > 16000) bits |= 0x20;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -16000) bits |= 0x40;
            if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > 16000) bits |= 0x80;
        }
    }
    _return(ctx, bits);
}

// recomp_arcade_pad_learn_start(player): the controls at rest (the axes' resting values)
void pad_learn_start(uint8_t* rdram, recomp_context* ctx) {
    int player = _arg<0, int>(rdram, ctx) & 1;
    memset(rest[player], 0, sizeof rest[player]);
    settle[player] = 15;
    _return(ctx, player_joystick(player) != nullptr ? 1 : 0);
}

// recomp_arcade_pad_learn_read(player, buf, len) -> 1 while a control is held (its SDL layout element in buf)
void pad_learn_read(uint8_t* rdram, recomp_context* ctx) {
    int player = _arg<0, int>(rdram, ctx) & 1;
    gpr buf = ctx->r5;
    int len = _arg<2, int>(rdram, ctx);
    std::string e = learn_read(player);
    if (len > 0) put_string(rdram, buf, len, e == "?" ? "" : e);
    _return(ctx, e.empty() ? 0 : 1);
}

// recomp_arcade_pad_learn_apply(player, elements): elements = 15 x 16 bytes (strings, "" = skipped),
// in the order of `targets` -> 1 done
void pad_learn_apply(uint8_t* rdram, recomp_context* ctx) {
    int player = _arg<0, int>(rdram, ctx) & 1;
    gpr buf = ctx->r5;
    std::vector<std::string> el;
    for (int k = 0; k < n_targets; k++) {
        std::string s = get_string(rdram, buf + k * 16, 15);
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);   // (the mod's buffer may be upper case)
        el.push_back(s);
    }
    _return(ctx, learn_apply(player, el) ? 1 : 0);
}

// recomp_arcade_pad_swap(): players 1 and 2 trade controllers
void pad_swap(uint8_t* rdram, recomp_context* ctx) {
    recompinput::players::swap_first_two();
}

// recomp_arcade_quit(): the operator menu's QUIT GAME
void arcade_quit(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::quit();
}

// ---- the N64 controls, each one bindable (2026-11-19) ----------------------------------------------------------------
// The operator menu's CONTROLS pages: every input of the N64 pad on a key or a controller's button / stick / trigger.
// The sets ("devices"): 0 the keyboard (player 1's keys); 1 the controllers' default ("CONTROLLER BUTTONS (ALL)"); 2..5
// players 1..4's own (round 2, the user: per player, with the default's row wherever the player hasn't set his own). The
// rows: the buttons, the d-pad (moving), the analog stick's four ways, and SPECIAL: while held, the analog stick pushed
// all the way the d-pad is held (straight up with no direction) -- the specials and taunts the game puts on the stick, on
// a button (factory: R3 on a pad, U on the keyboard). A player's set follows the PLAYER, whatever pad is his: each player
// has a recompinput profile of ours, put back on him whenever recompinput gives him his pad's own (plug order, SWAP
// CONTROLLERS, a pad plugged in again: arcade_pads_player_profile, each read of his controller). SPECIAL is ours
// (arcade_pads_special). Saved in controls_n64.json beside the exe: the keyboard's and the default's rows changed from the
// factory's, each player's own rows -- SDL's names, no controller IDs (controls.json's profiles are keyed by the pad's
// serial number: not that file). Round 1's file (no "players") is read as it was: its "controllers" is the default.
using recompinput::GameInput;
using recompinput::InputDevice;
using recompinput::InputField;
using recompinput::InputType;
using json = nlohmann::json;

constexpr int n_bind_rows = 19, row_special = 18, n_bind_players = 4, n_bind_sets = 2 + n_bind_players;
struct BindRow { const char* id; GameInput input; };
const BindRow bind_rows[n_bind_rows] = {
    { "A", GameInput::A }, { "B", GameInput::B }, { "Z", GameInput::Z }, { "L", GameInput::L }, { "R", GameInput::R },
    { "START", GameInput::START }, { "C_UP", GameInput::C_UP }, { "C_DOWN", GameInput::C_DOWN },
    { "C_LEFT", GameInput::C_LEFT }, { "C_RIGHT", GameInput::C_RIGHT },
    { "UP", GameInput::DPAD_UP }, { "DOWN", GameInput::DPAD_DOWN }, { "LEFT", GameInput::DPAD_LEFT }, { "RIGHT", GameInput::DPAD_RIGHT },
    { "STICK_UP", GameInput::Y_AXIS_POS }, { "STICK_DOWN", GameInput::Y_AXIS_NEG },
    { "STICK_LEFT", GameInput::X_AXIS_NEG }, { "STICK_RIGHT", GameInput::X_AXIS_POS },
    { "SPECIAL", GameInput::COUNT },                         // (not recompinput's: ours)
};
using Binding = std::array<InputField, recompinput::num_bindings_per_input>;
Binding bind_factory[2][n_bind_rows], bind_now[2][n_bind_rows];    // [0 keyboard, 1 the controllers' default][row]
Binding bind_pl[n_bind_players][n_bind_rows];                       // a player's own row ...
bool bind_own[n_bind_players][n_bind_rows];                         // ... when he has one (else the default's)
int pl_profile[n_bind_players] = { -1, -1, -1, -1 };               // each player's recompinput profile (ours)
constexpr const char* bind_file_name = "controls_n64.json";

// The analog stick as one (2026-11-20, controls -- the user: "I still don't see the analog stick map-able"): the menu's
// row for the four STICK rows at once. On a controller it puts the N64 stick on a pad's left or right stick (moved in the
// capture, or LEFT / RIGHT on the row); on the keyboard the mod asks for the four keys in turn (one capture a row).
// The N64 ports' menus have the stick the same way (Zelda64Recomp: the stick's up, down, left and right, each a key or a
// stick's half); none of them has a "walk" key, so neither do we -- a key pushes the stick all the way.
constexpr int row_stick_up = 14, row_stick_all = n_bind_rows;
const char* row_id(int row) { return row == row_stick_all ? "STICK (ALL 4)" : row >= 0 && row < n_bind_rows ? bind_rows[row].id : "?"; }
// the stick halves of the four rows, in their order (up, down, left, right; SDL's y is down): which 0 the left stick, 1 the right
InputField stick_half(int which, int k) {
    SDL_GameControllerAxis x = which ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX;
    SDL_GameControllerAxis y = which ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY;
    return k < 2 ? InputField::controller_analog(y, k == 1) : InputField::controller_analog(x, k == 3);
}

InputDevice device_of(int set) { return set == 0 ? InputDevice::Keyboard : InputDevice::Controller; }
std::string set_word(int set) {
    if (set == 0) return "keyboard";
    if (set == 1) return "controllers (the default)";
    return "player " + std::to_string(set - 1);
}
// a row as the set has it in play: a player's own, else the default's
const Binding& effective(int set, int row) {
    if (set >= 2 && !bind_own[set - 2][row]) return bind_now[1][row];
    return set >= 2 ? bind_pl[set - 2][row] : bind_now[set][row];
}

// the bound fields first, no field twice
void compact(Binding& b) {
    Binding out{};
    size_t k = 0;
    for (const InputField& f : b) {
        if (f.is_empty()) continue;
        bool dup = false;
        for (size_t i = 0; i < k; i++) if (out[i] == f) dup = true;
        if (!dup) out[k++] = f;
    }
    b = out;
}

// SDL's name of a field, as the file has it: keyboard "I", "Left Shift"; controller "a", "rightstick", "+lefttrigger",
// "-righty" (the right stick up: SDL's y is down)
std::string field_name(const InputField& f) {
    if (f.input_type == InputType::Keyboard) {
        const char* n = SDL_GetScancodeName((SDL_Scancode)f.input_id);
        return n && n[0] ? std::string(n) : "#" + std::to_string(f.input_id);
    }
    if (f.input_type == InputType::ControllerDigital) {
        const char* n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)f.input_id);
        return n ? std::string(n) : "";
    }
    if (f.input_type == InputType::ControllerAnalog) {
        const char* n = SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)(abs(f.input_id) - 1));
        return n ? std::string(f.input_id > 0 ? "+" : "-") + n : "";
    }
    return "";
}

InputField field_from_name(bool keyboard, const std::string& s) {
    if (s.empty()) return {};
    if (keyboard) {
        if (s[0] == '#') { int sc = atoi(s.c_str() + 1); return sc > 0 && sc < SDL_NUM_SCANCODES ? InputField::keyboard((SDL_Scancode)sc) : InputField{}; }
        SDL_Scancode sc = SDL_GetScancodeFromName(s.c_str());
        return sc != SDL_SCANCODE_UNKNOWN ? InputField::keyboard(sc) : InputField{};
    }
    if (s[0] == '+' || s[0] == '-') {
        SDL_GameControllerAxis a = SDL_GameControllerGetAxisFromString(s.c_str() + 1);
        return a != SDL_CONTROLLER_AXIS_INVALID ? InputField::controller_analog(a, s[0] == '+') : InputField{};
    }
    SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(s.c_str());
    return b != SDL_CONTROLLER_BUTTON_INVALID ? InputField::controller_digital(b) : InputField{};
}

// the menu's name of a field (put_string makes it capitals)
std::string field_text(const InputField& f) {
    std::string n = field_name(f);
    if (f.input_type == InputType::Keyboard) return n == "Return" ? "ENTER" : n;
    static const char* const map[][2] = {
        { "a", "A" }, { "b", "B" }, { "x", "X" }, { "y", "Y" }, { "back", "BACK" }, { "guide", "GUIDE" }, { "start", "START" },
        { "leftstick", "L3" }, { "rightstick", "R3" }, { "leftshoulder", "LB" }, { "rightshoulder", "RB" },
        { "dpup", "D-PAD UP" }, { "dpdown", "D-PAD DOWN" }, { "dpleft", "D-PAD LEFT" }, { "dpright", "D-PAD RIGHT" },
        { "misc1", "MISC" }, { "paddle1", "PADDLE 1" }, { "paddle2", "PADDLE 2" }, { "paddle3", "PADDLE 3" }, { "paddle4", "PADDLE 4" },
        { "touchpad", "TOUCHPAD" }, { "+lefttrigger", "LT" }, { "+righttrigger", "RT" }, { "-lefttrigger", "LT (RELEASED)" },
        { "-righttrigger", "RT (RELEASED)" },
        { "-leftx", "LEFT STICK LEFT" }, { "+leftx", "LEFT STICK RIGHT" }, { "-lefty", "LEFT STICK UP" }, { "+lefty", "LEFT STICK DOWN" },
        { "-rightx", "RIGHT STICK LEFT" }, { "+rightx", "RIGHT STICK RIGHT" }, { "-righty", "RIGHT STICK UP" }, { "+righty", "RIGHT STICK DOWN" },
    };
    for (const auto& m : map) if (n == m[0]) return m[1];
    return n;
}

std::string binding_text(const Binding& b, const char* none) {
    std::string s;
    for (const InputField& f : b) {
        if (f.is_empty()) continue;
        if (!s.empty()) s += " / ";
        s += field_text(f);
    }
    return s.empty() ? std::string(none) : s;
}

Binding from_vector(const std::vector<InputField>& v) {
    Binding b{};
    for (size_t i = 0; i < v.size() && i < b.size(); i++) b[i] = v[i];
    compact(b);
    return b;
}

bool is_player_profile(int i) {
    for (int p = 0; p < n_bind_players; p++) if (pl_profile[p] == i) return true;
    return false;
}
void put_row(int profile, int row, const Binding& b) {
    for (size_t k = 0; k < b.size(); k++) recompinput::profiles::set_input_binding(profile, bind_rows[row].input, k, b[k]);
}

// one row of a set as it is now, into recompinput:
//  keyboard: its default and player 1's keyboard profiles (the single-player one and the multiplayer one of player 1; the
//    other players' keyboard profiles are empty on purpose, recompinput's [wcw fix]);
//  the default: recompinput's default (pads' own profiles, made from it), every pad's profile, and each player's profile
//    where he has no row of his own;
//  a player's: his profile.
void apply_row(int set, int row) {
    if (row == row_special) return;
    GameInput input = bind_rows[row].input;
    if (set >= 2) {
        int p = set - 2;
        if (pl_profile[p] >= 0) put_row(pl_profile[p], row, effective(set, row));
        return;
    }
    const Binding& b = bind_now[set][row];
    std::vector<InputField> v;
    for (const InputField& f : b) if (!f.is_empty()) v.push_back(f);
    recompinput::replace_default_mapping(device_of(set), input, v);
    const std::string p1_mp_keyboard = recompinput::profiles::get_mp_keyboard_profile_key(0);
    for (int i = 0; i < recompinput::profiles::get_input_profile_count(); i++) {
        if (recompinput::profiles::get_input_profile_device(i) != device_of(set) || is_player_profile(i)) continue;
        if (set == 0) {
            const std::string& key = recompinput::profiles::get_input_profile_key(i);
            if (i != recompinput::profiles::get_sp_keyboard_profile_index() && key != p1_mp_keyboard) continue;
        }
        put_row(i, row, b);
    }
    if (set == 1)
        for (int p = 0; p < n_bind_players; p++)
            if (!bind_own[p][row] && pl_profile[p] >= 0) put_row(pl_profile[p], row, b);
}

json rows_json(const Binding* rows, const bool* include) {
    json o = json::object();
    for (int r = 0; r < n_bind_rows; r++) {
        if (!include[r]) continue;
        json a = json::array();
        for (const InputField& f : rows[r]) if (!f.is_empty()) a.push_back(field_name(f));
        o[bind_rows[r].id] = a;
    }
    return o;
}

bool bind_save() {
    json j;
    j["about"] = "The N64 controls as set in the operator menu (F2: CONTROLS), saved as soon as one is set. \"keyboard\": player 1's "
                 "keys; \"controllers\": the default for every player's controller (CONTROLLER BUTTONS (ALL)); \"players\": a "
                 "player's own rows (P1..P4 CONTROLLER BUTTONS), his other rows are the default's. Only the keyboard's and the "
                 "default's rows changed from the factory's are here. A row is a list of up to two SDL names (keyboard: key "
                 "names; controllers: button names, or an axis with + / -: \"-righty\" is the right stick up); an empty list is no "
                 "binding. Rows: A B Z L R START C_UP C_DOWN C_LEFT C_RIGHT, UP DOWN LEFT RIGHT (the d-pad: moving), STICK_UP "
                 "STICK_DOWN STICK_LEFT STICK_RIGHT (the analog stick: \"-lefty\" \"+lefty\" \"-leftx\" \"+leftx\" is the left stick; a row left out is the factory's), SPECIAL (while held: the stick pushed the way the d-pad is "
                 "held, up with none). Delete the file for the factory bindings.";
    for (int set = 0; set < 2; set++) {
        bool changed[n_bind_rows];
        for (int r = 0; r < n_bind_rows; r++) changed[r] = bind_now[set][r] != bind_factory[set][r];
        j[set == 0 ? "keyboard" : "controllers"] = rows_json(bind_now[set], changed);
    }
    json pl = json::object();
    for (int p = 0; p < n_bind_players; p++) {
        bool any = false;
        for (int r = 0; r < n_bind_rows; r++) any |= bind_own[p][r];
        if (any) pl[std::to_string(p + 1)] = rows_json(bind_pl[p], bind_own[p]);
    }
    j["players"] = pl;
    std::ofstream out(program_folder() / bind_file_name, std::ios::trunc);
    out << j.dump(2) << "\n";
    return out.good();
}

void bind_log(int set, int row, bool saved) {
    fprintf(stderr, "[pads] %s: %s = %s%s (%s %s)\n", set_word(set).c_str(), bind_rows[row].id,
        binding_text(effective(set, row), "(none)").c_str(), set >= 2 && !bind_own[set - 2][row] ? " (the default's)" : "",
        saved ? "saved in" : "NOT saved: could not write", bind_file_name);
}
// a set's rows in play, in the log (a player's: his profile's, as the game reads his controller)
void log_in_use(int set) {
    int p = set == 0 ? recompinput::profiles::get_sp_keyboard_profile_index() : set == 1 ? recompinput::profiles::get_sp_controller_profile_index() : pl_profile[set - 2];
    std::string s;
    for (int r = 0; r < n_bind_rows; r++) {
        Binding b{};
        if (r == row_special) b = effective(set, r);
        else if (p >= 0) for (size_t k = 0; k < b.size(); k++) b[k] = recompinput::profiles::get_input_binding(p, bind_rows[r].input, k);
        s += std::string(r ? ", " : "") + bind_rows[r].id + "=" + binding_text(b, "-");
        if (set >= 2 && bind_own[set - 2][r]) s += " (own)";
    }
    fprintf(stderr, "[pads] %s in use: %s\n", set_word(set).c_str(), s.c_str());
}

// bindings set: each field on its row's first place (a second binding stays), and off every other row of the set ("a key
// does one thing": STICK_UP on the left stick takes it off UP). On a player's page the rows touched become his own. Several
// rows at once (the whole stick): one save.
void bind_set_rows(int set, const int* rows, const InputField* fields, int count) {
    bool changed[n_bind_rows] = {};
    auto rowref = [set](int r) -> Binding& { return set >= 2 ? bind_pl[set - 2][r] : bind_now[set][r]; };
    for (int i = 0; i < count; i++) {
        const int row = rows[i];
        const InputField& f = fields[i];
        for (int r = 0; r < n_bind_rows; r++) {
            if (r == row) continue;
            Binding b = effective(set, r);
            bool hit = false;
            for (InputField& g : b) if (g == f) { g = InputField{}; hit = true; }
            if (!hit) continue;
            compact(b);
            rowref(r) = b;
            if (set >= 2) bind_own[set - 2][r] = true;
            changed[r] = true;
        }
        Binding b = effective(set, row);
        b[0] = f;
        compact(b);
        rowref(row) = b;
        if (set >= 2) bind_own[set - 2][row] = true;
        changed[row] = true;
    }
    for (int r = 0; r < n_bind_rows; r++) if (changed[r]) apply_row(set, r);
    bool saved = bind_save();
    for (int r = 0; r < n_bind_rows; r++) if (changed[r]) bind_log(set, r, saved);
    log_in_use(set);
}
void bind_set(int set, int row, const InputField& f) { bind_set_rows(set, &row, &f, 1); }

// the N64 stick on a pad's left (which 0) or right (1) stick: the four STICK rows at once (a controllers' set only)
void bind_stick_set(int set, int which) {
    int rows[4];
    InputField fields[4];
    for (int k = 0; k < 4; k++) { rows[k] = row_stick_up + k; fields[k] = stick_half(which, k); }
    fprintf(stderr, "[pads] %s: the analog stick = the %s stick\n", set_word(set).c_str(), which ? "right" : "left");
    bind_set_rows(set, rows, fields, 4);
}
// which of a pad's sticks the four STICK rows are on: 0 left, 1 right; -1 none of the rows bound, -2 something else
int stick_of(int set) {
    for (int which = 0; which < 2; which++) {
        bool all = true;
        for (int k = 0; k < 4; k++) {
            const Binding& b = effective(set, row_stick_up + k);
            all &= std::find(b.begin(), b.end(), stick_half(which, k)) != b.end();
        }
        if (all) return which;
    }
    for (int k = 0; k < 4; k++) if (!effective(set, row_stick_up + k)[0].is_empty()) return -2;
    return -1;
}

// what's held now: a key (not ESC -- cancel --, F2 -- the menu -- or F11 -- full screen), or a controller's button or a
// stick / trigger past 60% (only == nullptr: any SDL game controller; else that one, a player's own)
InputField held_key() {
    int n = 0;
    const Uint8* k = SDL_GetKeyboardState(&n);
    if (k == nullptr) return {};
    for (int sc = SDL_SCANCODE_A; sc < n && sc < SDL_NUM_SCANCODES; sc++) {
        if (!k[sc] || sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_F2 || sc == SDL_SCANCODE_F11) continue;
        return InputField::keyboard((SDL_Scancode)sc);
    }
    return {};
}
int controllers_with_layout() {
    int c = 0;
    for (int d = 0; d < SDL_NumJoysticks(); d++)
        if (SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(d)) != nullptr) c++;
    return c;
}
// (sticks_only: the whole stick's capture -- only a stick moved counts, the buttons and triggers don't)
InputField held_on(SDL_GameController* c, bool sticks_only = false) {
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX && !sticks_only; b++)
        if (SDL_GameControllerGetButton(c, (SDL_GameControllerButton)b)) return InputField::controller_digital((SDL_GameControllerButton)b);
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++) {
        int v = SDL_GameControllerGetAxis(c, (SDL_GameControllerAxis)a);
        bool trigger = a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        if (trigger && sticks_only) continue;
        if (v > 19660) return InputField::controller_analog((SDL_GameControllerAxis)a, true);
        if (!trigger && v < -19660) return InputField::controller_analog((SDL_GameControllerAxis)a, false);
    }
    return {};
}
// a player's own controller (any player 1..4; recompinput's)
SDL_GameController* player_pad(int p) {
    if (p < 0 || p >= n_bind_players || !recompinput::players::get_player_is_assigned(p)) return nullptr;
    return recompinput::players::get_player(p).controller;
}
InputField held_pad(int player, bool sticks_only = false) {
    if (player >= 0) { SDL_GameController* c = player_pad(player); return c ? held_on(c, sticks_only) : InputField{}; }
    for (int d = 0; d < SDL_NumJoysticks(); d++) {
        SDL_GameController* c = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(d));
        if (c == nullptr) continue;
        InputField f = held_on(c, sticks_only);
        if (!f.is_empty()) return f;
    }
    return {};
}

struct { int set = -1, row = 0; bool released = false; } capture;

int arg_set(uint8_t* rdram, recomp_context* ctx) { int s = _arg<0, int>(rdram, ctx); return s < 0 || s >= n_bind_sets ? 1 : s; }

// recomp_arcade_bind_name(set, row, buf, len) -> bit 0: the row is bound (as in play), bit 1: a player's own row; its text in
// buf ("D-PAD UP / LEFT STICK UP", "NONE"). Row 19 (row_stick_all), the whole stick: a pad's "LEFT STICK" / "RIGHT STICK"
// ("NONE"; "MIXED: THE 4 ROWS" when the rows are on something else), the keyboard's four keys ("I K J L")
void bind_name(uint8_t* rdram, recomp_context* ctx) {
    int set = arg_set(rdram, ctx), row = _arg<1, int>(rdram, ctx);
    gpr buf = ctx->r6;
    int len = _arg<3, int>(rdram, ctx);
    if (row == row_stick_all) {
        std::string s;
        int flags = 0;
        for (int k = 0; k < 4; k++) {
            const Binding& b = effective(set, row_stick_up + k);
            if (!b[0].is_empty()) flags |= 1;
            if (set >= 2 && bind_own[set - 2][row_stick_up + k]) flags |= 2;
            if (set == 0) s += (k ? "  " : "") + (b[0].is_empty() ? std::string("-") : field_text(b[0]));
        }
        if (set != 0) {
            int w = stick_of(set);
            s = w == 0 ? "LEFT STICK" : w == 1 ? "RIGHT STICK" : w == -1 ? "NONE" : "MIXED: THE 4 ROWS";
        }
        if (len > 0) put_string(rdram, buf, len, s);
        _return(ctx, flags);
        return;
    }
    if (row < 0 || row >= n_bind_rows) { if (len > 0) put_string(rdram, buf, len, ""); _return(ctx, 0); return; }
    const Binding& b = effective(set, row);
    if (len > 0) put_string(rdram, buf, len, binding_text(b, "NONE"));
    _return(ctx, (b[0].is_empty() ? 0 : 1) | (set >= 2 && bind_own[set - 2][row] ? 2 : 0));
}

// recomp_arcade_bind_start(set, row) -> 1 the capture started (bind_poll each frame); 0 no controller (the default's page:
// none at all; a player's: none for him); 2 only a controller without a layout (CONTROLLER SETUP first). Row 19 (the whole
// stick, a controllers' set only): the stick moved picks it.
void bind_start(uint8_t* rdram, recomp_context* ctx) {
    int set = arg_set(rdram, ctx), row = _arg<1, int>(rdram, ctx);
    if (row < 0 || row > n_bind_rows || (row == row_stick_all && set == 0)) { _return(ctx, 0); return; }
    if (set == 1 && controllers_with_layout() == 0) {
        refresh_raw();
        _return(ctx, raw_joys.empty() ? 0 : 2);
        return;
    }
    if (set >= 2 && player_pad(set - 2) == nullptr) {
        int p = set - 2;
        _return(ctx, p < 2 && player_joystick(p) != nullptr ? 2 : 0);   // (a raw joystick: players 1 and 2's, player_joystick)
        return;
    }
    capture.set = set; capture.row = row; capture.released = false;
    _return(ctx, 1);
}

// recomp_arcade_bind_poll() -> 0 waiting, 1 bound and saved, 2 cancelled (ESC, or no capture on). First nothing may be
// held (the Enter / A that started it), then the first key (keyboard) or button / stick / trigger (the default's page: any
// controller's; a player's: his own only). The whole stick's: the first stick moved (its half past 60%), that stick's four ways.
void bind_poll(uint8_t* rdram, recomp_context* ctx) {
    if (capture.set < 0) { _return(ctx, 2); return; }
    int n = 0;
    const Uint8* k = SDL_GetKeyboardState(&n);
    if (k != nullptr && k[SDL_SCANCODE_ESCAPE]) {
        fprintf(stderr, "[pads] %s: %s not changed (cancelled)\n", set_word(capture.set).c_str(), row_id(capture.row));
        capture.set = -1;
        _return(ctx, 2);
        return;
    }
    const bool whole_stick = capture.row == row_stick_all;
    InputField key = held_key();
    InputField pad = capture.set == 0 ? InputField{} : held_pad(capture.set >= 2 ? capture.set - 2 : -1);
    if (!capture.released) {
        capture.released = key.is_empty() && pad.is_empty();
        _return(ctx, 0);
        return;
    }
    if (whole_stick) pad = held_pad(capture.set >= 2 ? capture.set - 2 : -1, true);
    InputField f = capture.set == 0 ? key : pad;
    if (f.is_empty()) { _return(ctx, 0); return; }
    int set = capture.set;
    capture.set = -1;
    if (whole_stick) {
        const int axis = abs(f.input_id) - 1;
        bind_stick_set(set, axis == SDL_CONTROLLER_AXIS_RIGHTX || axis == SDL_CONTROLLER_AXIS_RIGHTY ? 1 : 0);
    } else bind_set(set, capture.row, f);
    _return(ctx, 1);
}

// recomp_arcade_bind_clear(set, row): the row unbound (a player's: his own row, empty) (saved); row 19: the stick's four rows
void bind_clear(uint8_t* rdram, recomp_context* ctx) {
    int set = arg_set(rdram, ctx), row = _arg<1, int>(rdram, ctx);
    if (row < 0 || row > n_bind_rows) return;
    const int first = row == row_stick_all ? row_stick_up : row, last = row == row_stick_all ? row_stick_up + 3 : row;
    for (int r = first; r <= last; r++) {
        if (set >= 2) { bind_pl[set - 2][r] = Binding{}; bind_own[set - 2][r] = true; }
        else bind_now[set][r] = Binding{};
        apply_row(set, r);
    }
    bool saved = bind_save();
    for (int r = first; r <= last; r++) bind_log(set, r, saved);
}

// recomp_arcade_bind_stick(set, which) -> 1 set and saved: the N64 stick on the pads' left (0) or right (1) stick (the
// whole stick's row, LEFT / RIGHT; a controllers' set only -- 0 for the keyboard's)
void bind_stick(uint8_t* rdram, recomp_context* ctx) {
    int set = arg_set(rdram, ctx), which = _arg<1, int>(rdram, ctx);
    if (set == 0) { _return(ctx, 0); return; }
    bind_stick_set(set, which ? 1 : 0);
    _return(ctx, 1);
}

// recomp_arcade_bind_reset(set): the keyboard / the default: the factory bindings, every row; a player: the default's,
// every row (his own ones gone) (saved)
void bind_reset(uint8_t* rdram, recomp_context* ctx) {
    int set = arg_set(rdram, ctx);
    for (int r = 0; r < n_bind_rows; r++) {
        if (set >= 2) bind_own[set - 2][r] = false;
        else bind_now[set][r] = bind_factory[set][r];
        apply_row(set, r);
    }
    bool saved = bind_save();
    fprintf(stderr, "[pads] %s: %s, every row (%s %s)\n", set_word(set).c_str(), set >= 2 ? "the default's" : "the factory bindings",
        saved ? "saved in" : "NOT saved: could not write", bind_file_name);
    log_in_use(set);
}

bool bind_from_file[2][n_bind_rows];

// a file's set of rows ({"A": ["Space"], ...}) -> the rows named, through `put`
template <typename Put> int read_rows(const json& sec, bool keyboard, const std::string& what, Put put) {
    int count = 0;
    if (!sec.is_object()) return 0;
    for (auto it = sec.begin(); it != sec.end(); ++it) {
        int r = 0;
        while (r < n_bind_rows && it.key() != bind_rows[r].id) r++;
        if (r == n_bind_rows || !it.value().is_array()) { fprintf(stderr, "[pads] %s: %s: \"%s\" is not a row (left out)\n", bind_file_name, what.c_str(), it.key().c_str()); continue; }
        Binding b{};
        size_t k = 0;
        for (const json& e : it.value()) {
            InputField f = e.is_string() ? field_from_name(keyboard, e.get<std::string>()) : InputField{};
            if (f.is_empty()) { fprintf(stderr, "[pads] %s: %s %s: \"%s\" is not a %s (left out)\n", bind_file_name, what.c_str(), bind_rows[r].id, e.is_string() ? e.get<std::string>().c_str() : "?", keyboard ? "key" : "controller input"); continue; }
            if (k < b.size()) b[k++] = f;
        }
        compact(b);
        put(r, b);
        count++;
    }
    return count;
}

} // namespace

// Start-up, right after main.cpp's defaults and before controls.json: the factory bindings kept (main.cpp's, and SPECIAL:
// R3 on a pad, U on the keyboard -- beside IJKL, the stick's keys, and no one else's: not the keyboard's defaults, the
// arcade's keys, nor the bundled camera mod's free-fly keys), controls_n64.json's keyboard and default rows made
// recompinput's defaults (so every profile made from them, pads plugged in later too, has them), its players' own rows kept.
void arcade_pads_bindings_init() {
    for (int set = 0; set < 2; set++)
        for (int r = 0; r < n_bind_rows; r++) {
            bind_factory[set][r] = r == row_special ? Binding{} : from_vector(recompinput::get_default_mapping_for_input(device_of(set), bind_rows[r].input));
            bind_now[set][r] = bind_factory[set][r];
        }
    bind_factory[0][row_special][0] = InputField::keyboard(SDL_SCANCODE_U);                          // (round 2, the user)
    bind_factory[1][row_special][0] = InputField::controller_digital(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    bind_now[0][row_special] = bind_factory[0][row_special];
    bind_now[1][row_special] = bind_factory[1][row_special];
    std::ifstream in(program_folder() / bind_file_name);
    if (!in.good()) { fprintf(stderr, "[pads] no %s: the factory bindings\n", bind_file_name); return; }
    const json j = json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { fprintf(stderr, "[pads] %s is not JSON: the factory bindings\n", bind_file_name); return; }
    int count[2] = {}, own = 0;
    for (int set = 0; set < 2; set++) {
        auto sec = j.find(set == 0 ? "keyboard" : "controllers");
        if (sec == j.end()) continue;
        count[set] = read_rows(*sec, set == 0, set_word(set), [set](int r, const Binding& b) {
            bind_now[set][r] = b;
            bind_from_file[set][r] = true;
            if (r == row_special) return;
            std::vector<InputField> v;
            for (const InputField& f : b) if (!f.is_empty()) v.push_back(f);
            recompinput::replace_default_mapping(device_of(set), bind_rows[r].input, v);
        });
    }
    auto pls = j.find("players");
    if (pls != j.end() && pls->is_object())
        for (auto it = pls->begin(); it != pls->end(); ++it) {
            int p = atoi(it.key().c_str()) - 1;
            if (p < 0 || p >= n_bind_players) { fprintf(stderr, "[pads] %s: players: \"%s\" is not a player 1-4 (left out)\n", bind_file_name, it.key().c_str()); continue; }
            own += read_rows(it.value(), false, set_word(p + 2), [p](int r, const Binding& b) { bind_pl[p][r] = b; bind_own[p][r] = true; });
        }
    fprintf(stderr, "[pads] %s: %d rows for the keyboard, %d for the controllers' default, %d of players' own\n", bind_file_name, count[0], count[1], own);
}

// After controls.json: each player's profile of ours (the default's rows, his own over them) put on him; the file's rows over
// every profile there (only those rows: with no file, nothing changes); the bindings in use, in the log
void arcade_pads_bindings_apply() {
    for (int p = 0; p < n_bind_players; p++) {
        pl_profile[p] = recompinput::profiles::add_input_profile("arcade_player_" + std::to_string(p + 1), "Player " + std::to_string(p + 1), InputDevice::Controller, false);
        recompinput::profiles::reset_profile_bindings(pl_profile[p], InputDevice::Controller);   // (the default's, menu inputs too)
        for (int r = 0; r < n_bind_rows; r++) apply_row(p + 2, r);
        recompinput::profiles::set_input_profile_for_player(p, pl_profile[p], InputDevice::Controller);
    }
    for (int set = 0; set < 2; set++)
        for (int r = 0; r < n_bind_rows; r++)
            if (bind_from_file[set][r]) apply_row(set, r);
    for (int set = 0; set < n_bind_sets; set++) log_in_use(set);
}

// Before a player's controller is read: his profile of ours on him (recompinput gives a player the pad's own profile when a
// pad is assigned to him -- plugged in, SWAP CONTROLLERS, plugged in again: his bindings follow him, not the pad)
void arcade_pads_player_profile(int player) {
    if (player < 0 || player >= n_bind_players || pl_profile[player] < 0) return;
    if (recompinput::profiles::get_input_profile_for_player(player, InputDevice::Controller) == pl_profile[player]) return;
    recompinput::profiles::set_input_profile_for_player(player, pl_profile[player], InputDevice::Controller);
    static int logged;
    if (logged++ < 40) fprintf(stderr, "[pads] player %d: a controller of his -- his bindings on it\n", player + 1);
}

// SPECIAL, for a player's N64 controller as the game is about to read it: while held, the analog stick all the way the
// d-pad is held (diagonals 0.7 each way), straight up with no direction. The keyboard's only for a player with a keyboard
// profile (player 1); the controllers' -- the player's own row, else the default's -- on the player's own pad.
void arcade_pads_special(int player, uint16_t buttons, float* x, float* y) {
    if (recompinput::game_input_disabled() || player < 0 || player >= (int)recompinput::max_num_players_supported) return;
    bool held = false;
    if (recompinput::profiles::get_input_profile_for_player(player, InputDevice::Keyboard) >= 0)
        for (const InputField& f : bind_now[0][row_special]) if (!f.is_empty() && recompinput::get_input_digital(player, f)) held = true;
    const Binding& pad = player < n_bind_players ? effective(player + 2, row_special) : bind_now[1][row_special];
    for (const InputField& f : pad) if (!f.is_empty() && recompinput::get_input_digital(player, f)) held = true;
    if (!held) return;
    constexpr uint16_t up = 0x0800, down = 0x0400, left = 0x0200, right = 0x0100;
    int dx = ((buttons & right) ? 1 : 0) - ((buttons & left) ? 1 : 0);
    int dy = ((buttons & up) ? 1 : 0) - ((buttons & down) ? 1 : 0);
    if (dx == 0 && dy == 0) dy = 1;
    float m = (dx != 0 && dy != 0) ? 0.7f : 1.0f;
    *x = dx * m;
    *y = dy * m;
    static bool logged[4];
    if (player < 4 && !logged[player]) {         // (the first time for each player: the log shows it works)
        logged[player] = true;
        fprintf(stderr, "[pads] player %d: SPECIAL held -- the stick pushed to x %.1f, y %.1f (logged once)\n", player + 1, *x, *y);
    }
}

// ---- a stick's spring-back (2026-11-23) --------------------------------------------------------------------------------
// The user: "a phantom 'left' input button press ... Noticeable on select menu mostly". A stick let go springs back PAST its
// centre for a moment (worst on a quick flick); where the stick moves the d-pad (the factory rows: LEFT = D-PAD LEFT / LEFT
// STICK LEFT; or a layout from CONTROLLER SETUP that puts the d-pad on a stick's axis, "dpleft:-a0"), that overshoot is a
// press the other way: one step back on the Superstar Select after every step. So for a direction held ONLY by a stick (no
// key, no real button or d-pad): when it comes on within 100 ms of the OPPOSITE direction being let go, it counts only once it
// has been held 50 ms -- a spring-back is over by then, a real reversal is late by 50 ms at most. A key, a d-pad or a button
// is never touched.
namespace {
constexpr int snap_rows[4] = { 10, 11, 12, 13 };                 // bind_rows: UP, DOWN, LEFT, RIGHT
constexpr uint16_t snap_bits[4] = { 0x0800, 0x0400, 0x0200, 0x0100 };
constexpr Uint64 snap_window_ms = 100, snap_hold_ms = 50;
struct SnapState { bool prev[4]; Uint64 released[4], pending[4]; bool pass[4]; };
SnapState snap[n_bind_players];
int snap_count;

// this SDL button on a stick's (or trigger's) axis in the controller's layout
bool button_on_axis(SDL_GameController* c, SDL_GameControllerButton b) {
    return c != nullptr && SDL_GameControllerGetBindForButton(c, b).bindType == SDL_CONTROLLER_BINDTYPE_AXIS;
}
bool button_on_axis_any(int player, SDL_GameControllerButton b) {
    if (!recompinput::players::is_single_player_mode()) return button_on_axis(player_pad(player), b);
    for (int d = 0; d < SDL_NumJoysticks(); d++)                  // (one player: every controller counts, as recompinput reads it)
        if (button_on_axis(SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(d)), b)) return true;
    return false;
}
} // namespace

void arcade_pads_snapback(int player, uint16_t* buttons) {
    if (player < 0 || player >= n_bind_players || recompinput::game_input_disabled()) return;
    const bool sp = recompinput::players::is_single_player_mode();
    const int pc = sp ? recompinput::profiles::get_sp_controller_profile_index() : recompinput::profiles::get_input_profile_for_player(player, InputDevice::Controller);
    const int pk = sp ? recompinput::profiles::get_sp_keyboard_profile_index() : recompinput::profiles::get_input_profile_for_player(player, InputDevice::Keyboard);
    const Uint64 now = SDL_GetTicks64();
    SnapState& s = snap[player];
    bool stick[4] = {}, other[4] = {};
    for (int d = 0; d < 4; d++) {
        const GameInput input = bind_rows[snap_rows[d]].input;
        for (int prof : { pc, pk }) {
            if (prof < 0) continue;
            for (size_t k = 0; k < recompinput::num_bindings_per_input; k++) {
                const InputField f = recompinput::profiles::get_input_binding(prof, input, k);
                if (f.is_empty() || !recompinput::get_input_digital(player, f)) continue;
                bool from_stick = f.input_type == InputType::ControllerAnalog ||
                    (f.input_type == InputType::ControllerDigital && button_on_axis_any(player, (SDL_GameControllerButton)f.input_id));
                (from_stick ? stick : other)[d] = true;
            }
        }
    }
    // (the lets-go first, every way: a stick read right one time and left the next has let go of RIGHT by the time LEFT is looked at)
    for (int d = 0; d < 4; d++)
        if (!stick[d] && s.prev[d]) s.released[d] = now;
    for (int d = 0; d < 4; d++) {
        const int opp = d ^ 1;
        if (!stick[d]) {
            s.pass[d] = false;
            s.pending[d] = 0;
        } else if (!s.prev[d]) {                                  // the stick has just come on this way
            const bool after_opp = s.released[opp] != 0 && now - s.released[opp] < snap_window_ms;
            s.pass[d] = !after_opp;
            s.pending[d] = after_opp ? now : 0;
        } else if (!s.pass[d] && now - s.pending[d] >= snap_hold_ms) {
            s.pass[d] = true;                                     // held long enough: a real push
            if (snap_count < 40)
                fprintf(stderr, "[pads] player %d: %s held %d ms: a real push, it counts\n", player + 1,
                    bind_rows[snap_rows[d]].id, int(now - s.pending[d]));
        }
        s.prev[d] = stick[d];
        if (stick[d] && !other[d] && !s.pass[d] && (*buttons & snap_bits[d])) {
            *buttons &= ~snap_bits[d];
            if (s.pending[d] == now && snap_count++ < 40)          // (logged once a spring-back, the first 40)
                fprintf(stderr, "[pads] player %d: %s left out for a moment -- the stick springing back past its centre after %s\n",
                    player + 1, bind_rows[snap_rows[d]].id, bind_rows[snap_rows[opp]].id);
        }
    }
}

void arcade_pads_load_layouts() {
    for (const char* f : { "gamecontrollerdb.txt", "controller_map.txt" }) {
        std::u8string path = (program_folder() / f).u8string();
        int n = SDL_GameControllerAddMappingsFromFile(reinterpret_cast<const char*>(path.c_str()));
        fprintf(stderr, "[arcade] controller layouts: %d from %s\n", n, f);
    }
}

void arcade_pads_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_pad_name", pad_name);
    recomp::overlays::register_base_export("recomp_arcade_pad_buttons", pad_buttons);
    recomp::overlays::register_base_export("recomp_arcade_pad_learn_start", pad_learn_start);
    recomp::overlays::register_base_export("recomp_arcade_pad_learn_read", pad_learn_read);
    recomp::overlays::register_base_export("recomp_arcade_pad_learn_apply", pad_learn_apply);
    recomp::overlays::register_base_export("recomp_arcade_pad_swap", pad_swap);
    recomp::overlays::register_base_export("recomp_arcade_quit", arcade_quit);
    recomp::overlays::register_base_export("recomp_arcade_bind_name", bind_name);       // (2026-11-19: every N64 input bindable)
    recomp::overlays::register_base_export("recomp_arcade_bind_start", bind_start);
    recomp::overlays::register_base_export("recomp_arcade_bind_poll", bind_poll);
    recomp::overlays::register_base_export("recomp_arcade_bind_clear", bind_clear);
    recomp::overlays::register_base_export("recomp_arcade_bind_reset", bind_reset);
    recomp::overlays::register_base_export("recomp_arcade_bind_stick", bind_stick);     // (2026-11-20: the stick as one)
}
