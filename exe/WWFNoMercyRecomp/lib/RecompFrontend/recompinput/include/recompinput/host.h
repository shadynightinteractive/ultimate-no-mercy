#pragma once
// What the input needs to ask of the program it's in (arcade_host.cpp).
namespace recompinput::host {
    double rumble_strength();      // 0-100
    double gyro_sensitivity();     // 0-100
    double mouse_sensitivity();    // 0-100
    double joystick_deadzone();    // 0-100
    void toggle_fullscreen();      // (F11, Alt + Enter)
}
