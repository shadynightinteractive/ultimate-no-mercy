// [arcade] The controllers, straight from the PC's input (step 3 of the native port). The game's library asks for them
// through six functions; mod/recomp/hooks_recomp.c patches those to these. Before this they went by way of a stand-in
// for the N64's controller chip: 64-byte command blocks, packed by the game's translated library, unpacked by the
// runtime, answered, packed again, unpacked again.
//
// No Controller Pak: there's nowhere for one to plug in. Rumble works on every controller, where before it was the
// first alone (the stand-in only answered there). The game's pak and microphone code is still in there until step 4,
// and asks through __osSiRawStartDma: librecomp si.cpp answers "nothing's plugged in".
#include <cstdint>
#include <cstdio>
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "librecomp/helpers.hpp"
#include "nm_replay.h"

namespace {
constexpr int PADS = 4;
constexpr uint8_t NO_RESPONSE = 8;     // (CHNL_ERR_NORESP >> 4: what the library leaves in a pad's err_no)

// What the controllers say now. The first is always there, as it was: a game with no pad at all still runs.
void read_pads(OSContPad* pads) {
    for (int c = 0; c < PADS; c++) { pads[c].button = 0; pads[c].stick_x = 0; pads[c].stick_y = 0; pads[c].err_no = 0; }
    osContGetReadData(pads);
    pads[0].err_no = 0;
    nm_replay_pads(pads);      // [replay 2026-11-14] recorded, or the recording's instead (nm_replay.cpp; nothing unless asked for)
}

// recomp_arcade_cont_ask(): func_800344F0 and func_80034590, the start of a read and of a query. The game waits for the
// serial event after each.
void cont_ask(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
    ultramodern::input::poll_input();
    ultramodern::send_si_message();
}

// recomp_arcade_cont_read(data): func_80034618, OSContPad data[4] (6 bytes each). As the library does: a pad that didn't
// answer gets its error and keeps what it had.
void cont_read(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) data = _arg<0, PTR(void)>(rdram, ctx);
    OSContPad pads[PADS];
    read_pads(pads);
    for (int c = 0; c < PADS; c++) {
        MEM_B(6 * c + 4, data) = pads[c].err_no ? NO_RESPONSE : 0;
        if (pads[c].err_no) continue;
        MEM_H(6 * c + 0, data) = pads[c].button;
        MEM_B(6 * c + 2, data) = pads[c].stick_x;
        MEM_B(6 * c + 3, data) = pads[c].stick_y;
    }
}

// recomp_arcade_cont_status(data): func_80034570, OSContStatus data[4] (4 bytes each): type, something plugged in, error.
void cont_status(uint8_t* rdram, recomp_context* ctx) {
    const PTR(void) data = _arg<0, PTR(void)>(rdram, ctx);
    OSContPad pads[PADS];
    read_pads(pads);
    for (int c = 0; c < PADS; c++) {
        MEM_B(4 * c + 3, data) = pads[c].err_no ? NO_RESPONSE : 0;
        if (pads[c].err_no) continue;
        MEM_H(4 * c + 0, data) = 0x0005;   // (a standard controller)
        MEM_B(4 * c + 2, data) = 1;        // (a pak: the motor)
    }
}

// recomp_arcade_cont_motor(channel, on): func_80039890
void cont_motor(uint8_t* rdram, recomp_context* ctx) {
    const int channel = _arg<0, int32_t>(rdram, ctx), on = _arg<1, int32_t>(rdram, ctx);
    static bool said = false;
    if (on && !said) { said = true; fprintf(stderr, "[cont] first rumble, controller %d\n", channel + 1); }
    ultramodern::input::set_rumble(channel, on != 0);
}
}  // namespace

void arcade_cont_register_exports() {
    recomp::overlays::register_base_export("recomp_arcade_cont_ask", cont_ask);
    recomp::overlays::register_base_export("recomp_arcade_cont_read", cont_read);
    recomp::overlays::register_base_export("recomp_arcade_cont_status", cont_status);
    recomp::overlays::register_base_export("recomp_arcade_cont_motor", cont_motor);
}
