// [xplat 2026-11-12] arcade_platform.h on Windows: the same calls arcade_host.cpp, arcade_hd.cpp and arcade_audio.cpp made
// before, moved here as they were, so the Windows program does exactly what it did.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include "arcade_platform.h"

std::filesystem::path arcade::platform::executable_path() {
    wchar_t here[MAX_PATH] = {}; GetModuleFileNameW(nullptr, here, MAX_PATH);
    return std::filesystem::path(here);
}
std::filesystem::path arcade::platform::local_data_folder() {
    PWSTR known = nullptr; std::filesystem::path out;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known) == S_OK) out = std::filesystem::path(known);
    CoTaskMemFree(known);
    return out;
}
void arcade::platform::message_box(const char* title, const char* text) { MessageBoxA(nullptr, text, title, MB_OK | MB_ICONERROR); }
uint64_t arcade::platform::ticks() { LARGE_INTEGER now; QueryPerformanceCounter(&now); return uint64_t(now.QuadPart); }
uint64_t arcade::platform::ticks_per_second() { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return uint64_t(f.QuadPart); }
std::string arcade::platform::utf8(const std::filesystem::path& p) { const auto u = p.u8string(); return std::string(u.begin(), u.end()); }
// [portable 2026-11-15] CreateProcessW with each argument quoted the way the C runtime splits them again
int arcade::platform::run_and_wait(const std::filesystem::path& program, const std::vector<std::string>& args) {
    auto quote = [](const std::wstring& a) {
        std::wstring q = L"\""; size_t bs = 0;
        for (wchar_t c : a) {
            if (c == L'\\') { bs++; continue; }
            if (c == L'"') { q.append(bs * 2 + 1, L'\\'); q += L'"'; bs = 0; continue; }
            q.append(bs, L'\\'); bs = 0; q += c;
        }
        q.append(bs * 2, L'\\'); q += L'"'; return q;
    };
    std::wstring cmd = quote(program.wstring());
    for (const std::string& a : args) cmd += L" " + quote(std::filesystem::path(std::u8string(a.begin(), a.end())).wstring());
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(program.wstring().c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
}
