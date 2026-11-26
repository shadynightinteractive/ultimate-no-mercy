// arcade_startup_status.h (startup 2026-11-19): the window that says what the game is building before it can start.
//
// The user (2026-11-19): "Every build at start (first run, an added ROM, a change) shows a status window listing what was
// found, added and removed." Whenever a start has something to make first -- the first start, a ROM added or taken away,
// content\reimport.txt, a changed input, a new commentary disc -- a plain window opens at once with a progress bar and a
// running list: the ROMs found (each by its game's name), what is being made, what is added (wrestlers per game, themes,
// Titantrons, arenas, commentary) and what is removed or left out (a source that went missing). A start with nothing to make
// opens nothing and costs nothing (the builder runs as before; the window exists only once something is said).
//
// Every line also goes to the game's log as "[status] <KIND>: <text>" (and the progress as "[status] progress d/n"), so a
// test reads the same list without a person. NM_STATUS_SHOT=<file.bmp> saves the window's last picture when it closes.
#pragma once
#include <functional>
#include <string>

namespace startup_status {

enum class Kind {
    Found,      // a source the game found: "WCW/nWo Revenge" (a ROM), "Revenge Redux patch", "WWE '13 disc image"
    Making,     // what is being made now (also the heading over the bar)
    Added,      // what was added: "52 wrestlers from WCW/nWo Revenge", "themes and Titantron videos"
    Removed,    // what was taken away or left out: a pack whose source ROM is gone, extras that need a missing game
    Note,       // anything else worth a line
};

// A line in the list (and in the log). The first line said during run() opens the window.
void say(Kind kind, const std::string& text);
// The progress bar: `more` steps added to the total; one step done. (No total yet: the bar moves on its own.)
void plan(int more);
void step();

// Runs `work` (the first-start builders) on a worker thread while this thread -- the program's main thread, where the window
// belongs on every system -- keeps the status window drawn and answering, but only once `work` has said something. Returns
// when `work` is done. Nothing said: no window was made, and nothing but a thread was started.
void run(const std::function<void()>& work);

}  // namespace startup_status
