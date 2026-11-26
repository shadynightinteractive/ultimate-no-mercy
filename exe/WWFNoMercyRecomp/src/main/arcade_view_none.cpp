// [xplat 2026-11-12] The picture backend that draws nothing: for a build with no GPU layer yet (the Linux link test) or a
// machine where none can start. Each finished frame is taken off the reader as the real backends do, so the reader's
// hand-over keeps moving, and then dropped; the game runs, silent of picture. (This is what xplat's make_view_headless.py
// made from a copy of the whole file; now it is just this backend.)
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include "arcade_view_shared.h"
#include "arcade_view_gpu.h"

namespace arcade_view {
void backend_run() {
    for (;;) {
        { std::lock_guard<std::mutex> l(mtx_); if (have_ready_) { ready_ = Frame{}; have_ready_ = false; } }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}
void backend_status(char* out, size_t size) { snprintf(out, size, "no renderer (headless)"); }
}
