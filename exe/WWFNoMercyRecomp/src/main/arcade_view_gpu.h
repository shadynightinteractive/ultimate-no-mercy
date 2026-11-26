#pragma once
// [xplat 2026-11-12] The picture backend's doorway: the two calls the display-list reader (arcade_view.cpp) makes into whichever
// backend this build has. One file defines them:
//   arcade_view_d3d12.cpp   Windows: Direct3D 12 (the default there)
//   arcade_view_vulkan.cpp  Linux, the Steam Deck (and Windows with WCW_NATIVE_GPU=vulkan, to compare pictures on one machine)
//   arcade_view_none.cpp    a build with no picture (links and runs the game headless; a stand-in, nothing is drawn)
// The backend runs the picture's thread: the window, the device, and a loop that takes each finished Frame off the reader
// (arcade_view_shared.h: mtx_, ready_, have_ready_), blends it between the last two game updates and presents it.
// (A tidier interface of a dozen calls -- PLAN.md step 4 -- can come later; the first cut is this mechanical one on purpose,
//  so the Direct3D 12 code could move unchanged and be checked alone.)
#include <cstddef>

namespace arcade_view {
void backend_run();                            // the picture's thread body: returns only if the backend can't start (then nothing is drawn)
void backend_status(char* out, size_t size);   // one line for a crash report: the device and its state
#ifdef ARCADE_VIEW_VULKAN_TOO
// [vkwin 2026-11-12] A Windows build with the Vulkan backend beside Direct3D 12 (CMake ARCADE_VIEW_BACKEND=vulkan): its two calls
// under their own names, so arcade_view_d3d12.cpp's backend_run / backend_status stay as they are and stay the default.
// WCW_NATIVE_GPU=vulkan at start-up picks these (arcade_view.cpp).
void vulkan_backend_run();
void vulkan_backend_status(char* out, size_t size);
#endif
}
