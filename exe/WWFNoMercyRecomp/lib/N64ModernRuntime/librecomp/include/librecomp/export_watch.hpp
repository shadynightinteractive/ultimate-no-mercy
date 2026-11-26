// [replay 2026-11-14] A watch on what the host's exports hand back to the mods (a QA tool, nm_replay.cpp): with an
// observer set before the mods load, every export a mod imports is handed to it through a small stand-in that calls the
// export and then tells the observer which one it was and what it returned. A replay that goes another way from its
// recording usually does so first where the host answered differently (a clock, the commentary, a file), so this says where.
// With no observer set, the mods get the exports themselves, as ever.
#pragma once
#include "recomp.h"

namespace recomp::overlays {
using export_observer_t = void(int index, const char* name, uint8_t* rdram, recomp_context* ctx);
void set_export_observer(export_observer_t* observer);
}
