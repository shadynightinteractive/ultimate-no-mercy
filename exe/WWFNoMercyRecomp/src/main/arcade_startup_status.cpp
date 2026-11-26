// arcade_startup_status.cpp (startup 2026-11-19): the start-up status window (arcade_startup_status.h says what and why).
//
// Drawn with SDL alone, before the game has a window of its own: SDL's video is started for the time the window is up and
// stopped again after, so the game's own window is made exactly as before (create_gfx, with the same DPI setting). The
// picture is a plain 32-bit image drawn here -- text in the project's 8x8 font (mod/src/font8x8.h, public domain), doubled
// -- and copied to the window with SDL's surface calls: no renderer, no GPU, nothing that could disagree with the game's.
#include "arcade_startup_status.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "SDL.h"

#include "../../../../mod/src/font8x8.h"   // (the mod's 8x8 font, characters 32-127: the same one its menus use)

namespace startup_status {
namespace {

struct Line { Kind kind; std::string text; };

std::mutex mu;
std::condition_variable cv;
std::vector<Line> lines;          // (everything said during run(), in order)
std::string heading;              // the last Making line: what is being made now
int total = 0, done = 0;          // the progress bar's steps
bool running = false;             // inside run()
bool wanted = false;              // something was said: the window is wanted
bool finished = false;            // run()'s work is done

const char* tag(Kind k) {
    switch (k) {
        case Kind::Found: return "FOUND";
        case Kind::Making: return "MAKING";
        case Kind::Added: return "ADDED";
        case Kind::Removed: return "REMOVED";
        default: return "NOTE";
    }
}

// ---- the picture ------------------------------------------------------------------------------------------------------
constexpr int W = 1024, H = 640, MARGIN = 40;
constexpr uint32_t BG = 0xFF0F1320, PANEL = 0xFF171C30, WHITE = 0xFFF2F4F8, GREY = 0xFF9AA3B8, AMBER = 0xFFF2C14E,
                   BAR_BG = 0xFF262C44, BAR = 0xFFD8352A, BLUE = 0xFF5AA9E6, GREEN = 0xFF6CCB5F, RED = 0xFFE5645A;

uint32_t colour(Kind k) {
    switch (k) {
        case Kind::Found: return BLUE;
        case Kind::Making: return AMBER;
        case Kind::Added: return GREEN;
        case Kind::Removed: return RED;
        default: return GREY;
    }
}

struct Canvas {
    uint32_t* px; int pitch;      // (pixels, in 32-bit words per row)
    void fill(int x, int y, int w, int h, uint32_t c) {
        for (int j = std::max(0, y); j < std::min(H, y + h); j++)
            for (int i = std::max(0, x); i < std::min(W, x + w); i++) px[j * pitch + i] = c;
    }
    // text at (x, y), each font pixel `s` x `s`; characters outside 32-127 drawn as '?'
    void text(int x, int y, int s, uint32_t c, const std::string& t) {
        for (unsigned char ch : t) {
            const int g = (ch >= 32 && ch < 128) ? ch - 32 : '?' - 32;
            for (int r = 0; r < 8; r++)
                for (int b = 0; b < 8; b++)
                    if (font8x8[g][r] & (1 << b)) fill(x + b * s, y + r * s, s, s, c);
            x += 8 * s;
        }
    }
};

// `t` cut into pieces of at most `n` characters, at spaces where it can be
std::vector<std::string> wrap(const std::string& t, size_t n) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < t.size()) {
        size_t len = std::min(n, t.size() - at);
        if (at + len < t.size()) {
            const size_t space = t.rfind(' ', at + len);
            if (space != std::string::npos && space > at) len = space - at;
        }
        out.push_back(t.substr(at, len));
        at += len;
        while (at < t.size() && t[at] == ' ') at++;
    }
    if (out.empty()) out.push_back("");
    return out;
}

void draw(Canvas& c, int frame, bool all_done) {
    std::vector<Line> ls;
    std::string head;
    int n = 0, d = 0;
    {
        std::lock_guard<std::mutex> lk(mu);
        ls = lines; head = heading; n = total; d = done;
    }
    c.fill(0, 0, W, H, BG);
    c.text(MARGIN, 30, 4, WHITE, "ULTIMATE NO MERCY");
    c.text(MARGIN, 72, 2, GREY, "Getting the game ready");
    // the heading: what is being made now, with dots that move (the game is working even while one step takes long)
    std::string now = all_done ? std::string("Done -- starting the game") : head.empty() ? std::string("Looking at your game files") : head;
    if (!all_done) now += std::string(size_t(frame / 8 % 4), '.');
    c.text(MARGIN, 118, 2, AMBER, wrap(now, size_t((W - 2 * MARGIN) / 16))[0]);
    // the bar
    const int bx = MARGIN, by = 148, bw = W - 2 * MARGIN - 80, bh = 22;
    c.fill(bx, by, bw, bh, BAR_BG);
    if (all_done || (n > 0)) {
        const double f = all_done ? 1.0 : std::min(1.0, double(d) / double(n));
        c.fill(bx, by, int(bw * f), bh, BAR);
        char pct[16];
        snprintf(pct, sizeof pct, "%3d%%", int(f * 100.0 + 0.5));
        c.text(bx + bw + 14, by + 3, 2, WHITE, pct);
    } else {
        const int blk = bw / 5, x = (frame * 12) % (bw + blk) - blk;     // (no total yet: a block that moves)
        c.fill(bx + std::max(0, x), by, std::min(blk, bw - std::max(0, x)) - std::max(0, -x), bh, BAR);
    }
    // the list: the newest lines that fit, oldest first
    const int top = 196, line_h = 22, bottom = H - 56, tag_w = 9 * 16;
    c.fill(MARGIN - 12, top - 10, W - 2 * MARGIN + 24, bottom - top + 14, PANEL);
    const size_t cols = size_t((W - 2 * MARGIN - tag_w) / 16);
    std::vector<std::pair<int, std::string>> rows;      // (line index, a piece of its text)
    for (size_t i = 0; i < ls.size(); i++)
        for (const std::string& piece : wrap(ls[i].text, cols)) rows.push_back({ int(i), piece });
    const size_t fit = size_t((bottom - top) / line_h);
    const size_t first = rows.size() > fit ? rows.size() - fit : 0;
    int y = top;
    for (size_t r = first; r < rows.size(); r++) {
        const Line& l = ls[size_t(rows[r].first)];
        if (r == first || rows[r - 1].first != rows[r].first) c.text(MARGIN, y, 2, colour(l.kind), tag(l.kind));
        c.text(MARGIN + tag_w, y, 2, WHITE, rows[r].second);
        y += line_h;
    }
    c.text(MARGIN, H - 38, 2, GREY, "Made once and kept: next time the game starts at once.");
}

// The window, on this thread, until run()'s work is done (and a moment more, so the last lines can be read).
void show() {
    const auto t0 = std::chrono::steady_clock::now();
    auto wait_done = [] { std::unique_lock<std::mutex> lk(mu); cv.wait(lk, [] { return finished; }); };
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");    // (as create_gfx sets it before the game's window)
#endif
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[status] no window (%s): the list is in this log only\n", SDL_GetError());
        wait_done();
        return;
    }
    const char* headless = getenv("NM_HEADLESS");
    const Uint32 flags = (headless && headless[0] == '1') ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    SDL_Window* win = SDL_CreateWindow("Ultimate No Mercy", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, flags);
    SDL_Surface* img = win ? SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888) : nullptr;
    if (!win || !img) {
        fprintf(stderr, "[status] no window (%s): the list is in this log only\n", SDL_GetError());
        if (img) SDL_FreeSurface(img);
        if (win) SDL_DestroyWindow(win);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        wait_done();
        return;
    }
    fprintf(stderr, "[status] the status window is open\n");
    bool asked_to_close = false;
    std::chrono::steady_clock::time_point done_at{};
    for (int frame = 0;; frame++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const bool close = e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE);
            if (close && !asked_to_close) {
                // (stopping half way would only make the game do it all again next time: it goes on, and says so)
                asked_to_close = true;
                say(Kind::Note, "Please wait: the game finishes this first, then starts.");
            }
        }
        bool all = false;
        {
            std::lock_guard<std::mutex> lk(mu);
            all = finished;
        }
        const auto now = std::chrono::steady_clock::now();
        if (all && done_at == std::chrono::steady_clock::time_point{}) done_at = now;
        Canvas c{ static_cast<uint32_t*>(img->pixels), img->pitch / 4 };
        SDL_LockSurface(img);
        draw(c, frame, all);
        SDL_UnlockSurface(img);
        if (SDL_Surface* screen = SDL_GetWindowSurface(win)) {
            SDL_BlitSurface(img, nullptr, screen, nullptr);
            SDL_UpdateWindowSurface(win);
        }
        // done: the last picture held a second, and the window up two at least (a short build can still be read)
        if (all && now - done_at >= std::chrono::milliseconds(1000) && now - t0 >= std::chrono::milliseconds(2000)) break;
        SDL_Delay(33);
    }
    if (const char* shot = getenv("NM_STATUS_SHOT"); shot && *shot) {
        if (SDL_SaveBMP(img, shot) == 0) fprintf(stderr, "[status] its picture saved: %s\n", shot);
    }
    SDL_FreeSurface(img);
    SDL_DestroyWindow(win);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);       // (the game starts SDL's video again for its own window, as it always did)
    fprintf(stderr, "[status] the status window is closed\n");
}

}  // namespace

void say(Kind kind, const std::string& text) {
    fprintf(stderr, "[status] %s: %s\n", tag(kind), text.c_str());
    fflush(stderr);
    std::lock_guard<std::mutex> lk(mu);
    if (!running) return;                    // (after the start-up: the log only)
    lines.push_back({ kind, text });
    if (kind == Kind::Making) heading = text;
    // (only real work opens the window: a start that finds its ROMs again and makes nothing -- an input whose date changed --
    //  shows nothing; the Found lines wait in the list for a window that something else opens)
    if (!wanted && kind != Kind::Found && kind != Kind::Note) { wanted = true; cv.notify_all(); }
}

void plan(int more) {
    std::lock_guard<std::mutex> lk(mu);
    total += more;
    if (running) fprintf(stderr, "[status] progress %d/%d\n", done, total);
}

void step() {
    std::lock_guard<std::mutex> lk(mu);
    done++;                                  // (the bar shows at most full: draw)
    if (running) fprintf(stderr, "[status] progress %d/%d\n", done, total);
}

void run(const std::function<void()>& work) {
    {
        std::lock_guard<std::mutex> lk(mu);
        running = true; wanted = false; finished = false;
        lines.clear(); heading.clear(); total = done = 0;
    }
    std::thread worker([&work] {
        work();
        std::lock_guard<std::mutex> lk(mu);
        finished = true;
        cv.notify_all();
    });
    bool show_it = false;
    {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait(lk, [] { return finished || wanted; });
        show_it = wanted;
    }
    if (show_it) show();
    worker.join();
    std::lock_guard<std::mutex> lk(mu);
    running = false;
}

}  // namespace startup_status
