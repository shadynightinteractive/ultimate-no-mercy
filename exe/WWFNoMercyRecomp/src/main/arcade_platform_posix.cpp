// [xplat 2026-11-12] arcade_platform.h on Linux, the Steam Deck and the Mac. Plain POSIX, with SDL for the message box (SDL
// is in the program already and shows one on X11, Wayland and the Mac alike).
#include <chrono>
#include <cstdlib>
#include <climits>
#include <unistd.h>
#include <sys/wait.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "SDL.h"
#include "arcade_platform.h"

std::filesystem::path arcade::platform::executable_path() {
#if defined(__APPLE__)
    char here[PATH_MAX] = {}; uint32_t size = sizeof here;
    if (_NSGetExecutablePath(here, &size) != 0) return {};
    std::error_code ec; const auto real = std::filesystem::canonical(here, ec);      // (it may be a link inside the .app)
    return ec ? std::filesystem::path(here) : real;
#else
    std::error_code ec; const auto here = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : here;
#endif
}
std::filesystem::path arcade::platform::local_data_folder() {
    const char* home = getenv("HOME");
#if defined(__APPLE__)
    return home && home[0] ? std::filesystem::path(home) / "Library" / "Application Support" : std::filesystem::path();
#else
    // (the XDG rule: $XDG_DATA_HOME when it's set and absolute, else ~/.local/share)
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0] == '/') return std::filesystem::path(xdg);
    return home && home[0] ? std::filesystem::path(home) / ".local" / "share" : std::filesystem::path();
#endif
}
void arcade::platform::message_box(const char* title, const char* text) { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, text, nullptr); }
uint64_t arcade::platform::ticks() { return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
uint64_t arcade::platform::ticks_per_second() { return 1'000'000'000ull; }
std::string arcade::platform::utf8(const std::filesystem::path& p) { const auto u = p.u8string(); return std::string(u.begin(), u.end()); }
// [portable 2026-11-15] fork + execv, waited for
int arcade::platform::run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args) {
    const std::string prog = utf8(program);
    std::vector<std::string> all{ prog }; all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv; for (std::string& a : all) argv.push_back(a.data()); argv.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) { execv(prog.c_str(), argv.data()); _exit(127); }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
