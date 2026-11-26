// Crash and freeze reports (the arcade build). When the game crashes, or stops making frames for a while
// (a freeze), a report goes into the "crash_reports" folder next to the exe:
//   <kind>_<date>_<time>.txt         what happened: the exception, and every thread's stack (exe offsets;
//                                     recompiled functions are named func_<N64 address>, so the stack is the
//                                     game's own call chain)
//   <kind>_<date>_<time>_rdram.bin   the game's 8 MB of RAM at that moment (the arcade mod keeps a log of what
//                                     it's been doing at 0x807F2000 in it), then the mods' memory (0x81000000..)
// The newest few are kept. Nothing is sent anywhere: the files stay on the machine.
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <psapi.h>
#include <dxgi1_4.h>
#include "arcade_host.h"   // (arcade::shown: paths for the log, privacy 2026-11-13)

extern std::atomic_int wcw_viswaps_total;
extern std::atomic_bool exited;   // (the runtime's: the game is stopping -- librecomp recomp.cpp)
extern "C" int n64recomp_live_symbolize(uintptr_t a, char* out, size_t sz);   // (N64Recomp live_generator.cpp)   // ultramodern events.cpp: every osViSwapBuffer (a game frame)

namespace {
void mem_status(char* out, size_t n);
uint8_t* g_rdram = nullptr;
std::filesystem::path g_dir;
std::mutex g_lock;
std::atomic_bool g_writing{ false };
std::atomic_bool g_stop{ false }, g_qa_reading{ false }, g_wd_reading{ false };   // (shutdown: the game's RAM is about to go)
DWORD g_watchdog_tid = 0;

void frame_line(FILE* f, HANDLE proc, int i, DWORD64 addr) {
    char modname[MAX_PATH] = "?";
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(mod, full, MAX_PATH)) {
            const char* b = strrchr(full, '\\');
            strncpy(modname, b ? b + 1 : full, MAX_PATH - 1);
        }
    }
    char symbuf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    unsigned long long off = mod ? (unsigned long long)(addr - (DWORD64)mod) : (unsigned long long)addr;
    char live[256];
    if (!mod && n64recomp_live_symbolize((uintptr_t)addr, live, sizeof(live)))
        fprintf(f, "  #%2d  %s\n", i, live);
    else if (SymFromAddr(proc, addr, &disp, sym))
        fprintf(f, "  #%2d  %s+0x%llX  %s+0x%llX\n", i, modname, off, sym->Name, (unsigned long long)disp);
    else
        fprintf(f, "  #%2d  %s+0x%llX\n", i, modname, off);
}

const char* prot_name(DWORD p) {
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return "noaccess"; case PAGE_READONLY: return "r"; case PAGE_READWRITE: return "rw";
    case PAGE_WRITECOPY: return "wc"; case PAGE_EXECUTE: return "x"; case PAGE_EXECUTE_READ: return "rx";
    case PAGE_EXECUTE_READWRITE: return "rwx"; case PAGE_EXECUTE_WRITECOPY: return "xwc"; default: return "?";
    }
}
void mem_line(FILE* f, const char* what, uintptr_t a) {
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery((void*)a, &m, sizeof(m))) { fprintf(f, "%s %p: (no memory info)\n", what, (void*)a); return; }
    fprintf(f, "%s %p: region %p+0x%llX (allocation %p), %s, %s, %s\n", what, (void*)a, m.BaseAddress,
            (unsigned long long)m.RegionSize, m.AllocationBase,
            m.State == MEM_COMMIT ? "committed" : m.State == MEM_FREE ? "FREE" : "reserved",
            prot_name(m.Protect), m.Type == MEM_IMAGE ? "image" : m.Type == MEM_PRIVATE ? "private" : "mapped");
}
bool is_code(uintptr_t v) {
    MEMORY_BASIC_INFORMATION m{};
    if (v < 0x10000 || !VirtualQuery((void*)v, &m, sizeof(m)) || m.State != MEM_COMMIT) return false;
    return (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
// the crashed thread's stack, raw: every value on it that points into code (return addresses, mostly) --
// when the walk can't follow the frames (live-recompiled mod code has no unwind info), these still show the way
void stack_scan(FILE* f, HANDLE proc, const CONTEXT& c) {
    fprintf(f, "registers: rip %p rsp %p rbp %p rax %p rbx %p rcx %p rdx %p rsi %p rdi %p\n"
               "           r8 %p r9 %p r10 %p r11 %p r12 %p r13 %p r14 %p r15 %p\n",
            (void*)c.Rip, (void*)c.Rsp, (void*)c.Rbp, (void*)c.Rax, (void*)c.Rbx, (void*)c.Rcx, (void*)c.Rdx, (void*)c.Rsi,
            (void*)c.Rdi, (void*)c.R8, (void*)c.R9, (void*)c.R10, (void*)c.R11, (void*)c.R12, (void*)c.R13, (void*)c.R14,
            (void*)c.R15);
    if (g_rdram) fprintf(f, "rdram at %p\n", (void*)g_rdram);
    mem_line(f, "rip", c.Rip);
    fprintf(f, "code addresses on the stack (from rsp):\n");
    int shown = 0;
    for (int k = 0; k < 1024 && shown < 40; k++) {
        uintptr_t at = c.Rsp + 8 * k;
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery((void*)at, &m, sizeof(m)) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD))) break;
        uintptr_t v = *(uintptr_t*)at;
        if (!is_code(v)) {
            char live[256];
            if (v > 0x10000 && n64recomp_live_symbolize(v, live, sizeof(live))) { fprintf(f, "  [rsp+0x%X] %s (NOT executable now)\n", 8 * k, live); shown++; }
            continue;
        }
        fprintf(f, "  [rsp+0x%X]", 8 * k);
        frame_line(f, proc, shown++, v);
    }
    fflush(f);
}

void walk(FILE* f, HANDLE proc, HANDLE thread, CONTEXT ctx) {
    STACKFRAME64 sf{};
    sf.AddrPC.Offset = ctx.Rip;    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp; sf.AddrStack.Mode = AddrModeFlat;
    for (int i = 0; i < 48; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &sf, &ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        if (sf.AddrPC.Offset == 0) break;
        frame_line(f, proc, i, sf.AddrPC.Offset);
    }
}

// every thread but this one (and the faulting one, walked from its own context)
void walk_others(FILE* f, HANDLE proc, DWORD skip) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{ sizeof(te) };
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me || te.th32ThreadID == skip) continue;
        HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != (DWORD)-1) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(t, &c)) {
                fprintf(f, "\nthread %lu:\n", (unsigned long)te.th32ThreadID);
                walk(f, proc, t, c);
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
}

void dump_memory(const std::filesystem::path& path) {
    if (!g_rdram) return;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fwrite(g_rdram, 1, 0x800000, f);                        // the game's RAM, 0x80000000..0x807FFFFF
    // the mods' memory, as far as it's there (0x81000000.., page by page): the arcade's data sits past its ~31 MB of
    // packs (0x82E7.. in 0.36.x), so up to 0x84000000
    for (size_t off = 0x1000000; off < 0x4000000; off += 0x1000) {
        MEMORY_BASIC_INFORMATION mbi{};
        static const uint8_t zero[0x1000] = {};
        bool there = VirtualQuery(g_rdram + off, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
                     !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
        fwrite(there ? g_rdram + off : zero, 1, 0x1000, f);
    }
    fclose(f);
}

void prune(const wchar_t* kind_prefix) {
    std::error_code ec;
    std::vector<std::filesystem::path> txt;
    for (auto& e : std::filesystem::directory_iterator(g_dir, ec)) {
        auto n = e.path().filename().wstring();
        if (n.rfind(kind_prefix, 0) == 0 && e.path().extension() == L".txt") txt.push_back(e.path());
    }
    std::sort(txt.begin(), txt.end());
    while (txt.size() > 4) {
        auto p = txt.front(); txt.erase(txt.begin());
        std::filesystem::remove(p, ec);
        auto bin = p; bin.replace_extension(); bin += L"_rdram.bin";
        std::filesystem::remove(bin, ec);
    }
}

// The arcade mod's log of what it and the game were doing (N64 0x807F2000, logic.c flight_*), read out.
uint32_t rd(uint32_t n64) { return g_rdram ? *reinterpret_cast<uint32_t*>(g_rdram + (n64 - 0x80000000u)) : 0; }
void arcade_log(FILE* f) {
    const uint32_t F = 0x807F2000u;
    if (!g_rdram || rd(F) != 0x4152434Cu) { fprintf(f, "(no arcade log in the game's RAM)\n"); return; }
    fprintf(f, "arcade: frame %u, phase %u, game mode %u, menus mode %d screen %d, Royal Rumble state %u, credits %u\n",
            rd(F + 4), rd(F + 8), rd(F + 0x24), (int)rd(F + 0x1C), (int)rd(F + 0x20), rd(F + 0xC), rd(F + 0x2C));
    fprintf(f, "match frames %u; draw list %u (pool %u); heap: largest free block %u KB, %u free blocks\n",
            rd(F + 0x4C) & 0xFFFF, rd(F + 0x44) >> 16, rd(F + 0x44) & 0xFFFF, rd(F + 0x48) >> 16, rd(F + 0x48) & 0xFFFF);
    fprintf(f, "match slots (Superstar ID / state):");
    for (int s = 0; s < 6; s++) fprintf(f, " %04X/%X", rd(F + 0x50 + 4 * s) >> 16, rd(F + 0x50 + 4 * s) & 0xFFFF);
    fprintf(f, "\nlast frames (frame: match frames, draws/pool, heap KB/blocks, slots' states):\n");
    const uint32_t n = rd(F + 0x68);
    for (uint32_t i = (n > 16 ? n - 16 : 0); i < n; i++) {
        const uint32_t h = F + 0x480 + (i % 128) * 16;
        fprintf(f, "  %u: %u, %u/%u, %u/%u, %06X\n", rd(h) >> 16, rd(h) & 0xFFFF, rd(h + 4) >> 16, rd(h + 4) & 0xFFFF,
                rd(h + 8) >> 16, rd(h + 8) & 0xFFFF, rd(h + 12));
    }
    fprintf(f, "bad matrices put right: %u (the last: node 0x%08X, retrace %u)\n", rd(F + 0x6C), rd(F + 0x70), rd(F + 0x74));
    fprintf(f, "arcade events (frame, kind, a, b):");
    const uint32_t ne = rd(F + 0x7C);
    for (uint32_t i = (ne > 12 ? ne - 12 : 0); i < ne; i++) {
        const uint32_t e = F + 0x80 + (i % 64) * 16;
        fprintf(f, " [%u %u %X %X]", rd(e), rd(e + 4), rd(e + 8), rd(e + 12));
    }
    fprintf(f, "\n");
}

extern "C" void wcw_gpu_status(char* out, size_t size);     // (plume_d3d12.cpp)
void write_report(const char* kind, EXCEPTION_POINTERS* ep) {
    bool expected = false;
    if (!g_writing.compare_exchange_strong(expected, true)) return;
    std::error_code ec;
    std::filesystem::create_directories(g_dir, ec);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[96];
    swprintf(name, 96, L"%hs_%04u%02u%02u_%02u%02u%02u", kind, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    auto base = g_dir / name;
    FILE* f = _wfopen((base.wstring() + L".txt").c_str(), L"w");
    HANDLE proc = GetCurrentProcess();
    bool dumped = false;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    if (f) {
        fprintf(f, "Ultimate No Mercy %s report, %04u-%02u-%02u %02u:%02u:%02u\n", kind, st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
        fprintf(f, "exe base %p, game frames so far %d\n", (void*)GetModuleHandleA(nullptr), wcw_viswaps_total.load());
        char gpu[256] = {};
        wcw_gpu_status(gpu, sizeof(gpu));
        fprintf(f, "%s\n", gpu);
        { char m[200]; mem_status(m, sizeof(m)); fprintf(f, "%s\n", m); }
        arcade_log(f);
        if (ep) {
            fprintf(f, "exception 0x%08lX at %p", (unsigned long)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
            if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
                unsigned long long a = (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1];
                const auto kind_ = ep->ExceptionRecord->ExceptionInformation[0];
                fprintf(f, ", %s 0x%llX", kind_ == 8 ? "EXECUTING (DEP) at" : kind_ ? "write of" : "read of", a);
                if (g_rdram && a >= (unsigned long long)(uintptr_t)g_rdram && a < (unsigned long long)(uintptr_t)g_rdram + 0x100000000ull)
                    fprintf(f, " (N64 address 0x%08llX)", 0x80000000ull + (a - (unsigned long long)(uintptr_t)g_rdram));
            }
            fprintf(f, "\n");
            {
                char live[256];
                if (n64recomp_live_symbolize((uintptr_t)ep->ExceptionRecord->ExceptionAddress, live, sizeof(live)))
                    fprintf(f, "the crash address is in %s\n", live);
            }
            stack_scan(f, proc, *ep->ContextRecord);
            dump_memory(base.wstring() + L"_rdram.bin"); dumped = true;
            fprintf(f, "\nthread %lu (the one that crashed):\n", (unsigned long)GetCurrentThreadId());
            fflush(f);
            walk(f, proc, GetCurrentThread(), *ep->ContextRecord);
            walk_others(f, proc, GetCurrentThreadId());
        } else {
            fprintf(f, "the game made no new frame for %d seconds\n", 12);
            walk_others(f, proc, 0);
        }
        fclose(f);
    }
    if (!dumped) dump_memory(base.wstring() + L"_rdram.bin");
    prune(ep ? L"crash_" : L"freeze_");
    fprintf(stderr, "[arcade] %s report written: %s.txt\n", kind, arcade::shown(base).c_str());   // (privacy 2026-11-13: relative)
    fflush(stderr);
    g_writing = false;
}

// The program's memory and what it uses of the graphics card's, now: "ws 812 MB, private 1350 MB, card 3100 MB"
// (privacy 2026-11-13: not the card's budget, which says how much memory it has)
IDXGIAdapter3* g_adapter = nullptr;
void mem_status(char* out, size_t n) {
    PROCESS_MEMORY_COUNTERS_EX pm{}; pm.cb = sizeof(pm);
    K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof(pm));
    if (!g_adapter) {
        IDXGIFactory1* f = nullptr;
        if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f)) && f) {
            IDXGIAdapter1* a = nullptr; SIZE_T best = 0;
            for (UINT i = 0; f->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
                DXGI_ADAPTER_DESC1 d{}; a->GetDesc1(&d);
                IDXGIAdapter3* a3 = nullptr;
                if (d.DedicatedVideoMemory > best && SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3))) {
                    if (g_adapter) g_adapter->Release();
                    g_adapter = a3; best = d.DedicatedVideoMemory;
                }
                a->Release();
            }
            f->Release();
        }
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO vm{};
    if (g_adapter) g_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &vm);
    snprintf(out, n, "memory: working set %llu MB, private %llu MB; graphics card %llu MB",
             (unsigned long long)(pm.WorkingSetSize >> 20), (unsigned long long)(pm.PrivateUsage >> 20),
             (unsigned long long)(vm.CurrentUsage >> 20));
}

// A glitch report (the Royal Rumble's "King frozen in the ring, everything slowing down", 2026-10-27): the scene's
// matrices going bad by the hundred in a second (the arcade log's count, gameplay.c), or the arcade's own frame
// code no longer running while the game goes on making frames -- the game's RAM and every thread's stack at that
// moment, as a crash report has them (one of each kind a run).
bool g_glitch_bad = false, g_glitch_stall = false, g_glitch_pose = false;
uint32_t g_last_badmtx = 0, g_last_modframe = 0;
int g_last_swaps = 0, g_modframe_still = 0;
void glitch_watch() {
    if (!g_rdram || rd(0x807F2000u) != 0x4152434Cu) return;   // ('ARCL': the arcade's log is there)
    uint32_t bad = rd(0x807F206Cu), modframe = rd(0x807F2004u);
    int swaps = wcw_viswaps_total.load();
    if (!g_glitch_bad && bad - g_last_badmtx > 100) {
        g_glitch_bad = true;
        fprintf(stderr, "[arcade] glitch: %u bad matrices in a second (last node 0x%08X)\n", bad - g_last_badmtx, rd(0x807F2070u)); fflush(stderr);
        write_report("glitch", nullptr);
    }
    // the announcers' pose not a number the first time (announce.c pose_kept, 'ADIA' at 0x807F3240: what went into
    // it): the RAM then, the arcade's own memory with it, before whatever made it goes further
    if (!g_glitch_pose && rd(0x807F3240u) == 0x41444941u) {
        g_glitch_pose = true;
        fprintf(stderr, "[arcade] glitch: an announcer's pose wasn't numbers (his last good one used)\n"); fflush(stderr);
        write_report("pose", nullptr);
    }
    if (modframe == g_last_modframe && swaps - g_last_swaps > 20) g_modframe_still++; else g_modframe_still = 0;
    if (!g_glitch_stall && g_modframe_still >= 3) {
        g_glitch_stall = true;
        fprintf(stderr, "[arcade] glitch: the arcade's frames stopped (frame %u) while the game made %d\n", modframe, swaps - g_last_swaps); fflush(stderr);
        write_report("stall", nullptr);
    }
    g_last_badmtx = bad; g_last_modframe = modframe; g_last_swaps = swaps;
}

// QA's guard (WCW_GUARD_MOD, set by qa.ps1): the arcade's packs made read-only once it says where they are ('GARD' at
// 0x807F2F00: first, end -- logic.c). Nothing writes them; in a Royal Rumble (2026-10-27) something wrote 3.9 MB of
// zeros from the start of the arcade's memory on, its numbers with it (every sum after it not a number). The next
// write there is a crash report, the thread that did it on top of its stack.
bool g_guarded = false;
uint8_t* g_guard_at = nullptr;
size_t g_guard_size = 0;
// (the game stopping -- the runtime's `exited` -- its memory can be written as it's taken down: the guard is lifted then,
// not a crash on closing the game)
LONG CALLBACK guard_lift(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_guard_at || !exited.load()) return EXCEPTION_CONTINUE_SEARCH;
    uint8_t* at = (uint8_t*)ep->ExceptionRecord->ExceptionInformation[1];
    if (at < g_guard_at || at >= g_guard_at + g_guard_size) return EXCEPTION_CONTINUE_SEARCH;
    DWORD was = 0;
    VirtualProtect(g_guard_at, g_guard_size, PAGE_READWRITE, &was);
    g_guard_at = nullptr;
    return EXCEPTION_CONTINUE_EXECUTION;
}
void guard_packs() {
    if (g_guarded || !g_rdram || !getenv("WCW_GUARD_MOD") || rd(0x807F2F00u) != 0x47415244u) return;   // 'GARD'
    g_guarded = true;
    uint32_t a = (rd(0x807F2F04u) + 0xFFFu) & ~0xFFFu, b = rd(0x807F2F08u) & ~0xFFFu;
    DWORD was = 0;
    BOOL ok = a >= 0x81000000u && b > a && VirtualProtect(g_rdram + (a - 0x80000000u), b - a, PAGE_READONLY, &was);
    if (ok) { g_guard_at = g_rdram + (a - 0x80000000u); g_guard_size = b - a; AddVectoredExceptionHandler(1, guard_lift); }
    fprintf(stderr, "[arcade] guard: 0x%08X..0x%08X read-only: %s\n", a, b, ok ? "yes" : "NO"); fflush(stderr);
}

void watchdog() {
    int last = -1, still = 0;
    bool reported = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        g_wd_reading = true;
        if (g_stop) { g_wd_reading = false; return; }
        struct Done { ~Done() { g_wd_reading = false; } } done;   // (every way out of this pass)
        guard_packs();
        glitch_watch();
        static int tick = 0;
        if (++tick % 15 == 0) {      // (the log: how the memory goes, every 15 s)
            char m[200]; mem_status(m, sizeof(m));
            fprintf(stderr, "[mem] %s; bad matrices %u\n", m, rd(0x807F206Cu)); fflush(stderr);
        }
        int n = wcw_viswaps_total.load();
        if (n != last) { last = n; still = 0; reported = false; continue; }
        if (n < 300) continue;                               // (not running yet)
        if (++still >= 12 && !reported) { reported = true; write_report("freeze", nullptr); }
    }
}
// QA (WCW_QA_STATE=<file.csv>, Development\qa.ps1): the arcade mod's QA state (N64 0x807F3000, logic.c "QA
// scenarios") as a CSV row 20 times a second while it changes: what a test checks the game did.
void qa_state_writer(std::string path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    // (bad_mtx: the scene's matrices put right so far, gameplay.c; bad_owner: whose the last was -- 1 JR, 2 King,
    // 3 / 4 their chairs, 0x10 + k slot k's bones, 0x20 the table, 0 other)
    fprintf(f, "ms,swaps,frame,stage,matches,mode,screen,match_frames,match_state,entrance_step,venue,flags,clock,slots,"
               "bad_mtx,bad_owner,bad_frame,forced,jr_bones,jr_x,jr_y,jr_z,king_bones,king_x,king_y,king_z,bad_node,bad_parent,eye_z,ann_draw,king_pic0,king_sum0,king_pic1,king_sum1,king_hips_x,king_hips_y,king_hips_z,king_head_x,king_head_y,king_head_z,ann_fallbacks,bs_tries,bs_grabs,bs_step,pal_wm_skipped,pal_room_skipped,pal_calls,g_part,g_pal,g_room,g_at_n,g_slot,rr_put_out,rr_kept_out");
    // (2026-11-12, agent "names4", noted by final081: "s<k>_state" is the wrestler's PREVIOUS state (his record's +0x1C) and
    //  "s<k>_phase" his state (+0x14): mod src/logic.c reads W + 0x14 and W + 0x0C, and W is the record + 8. The labels are kept:
    //  37 scenarios' checks were written against these values as they are, so their verdicts stay what they meant.)
    for (int s = 0; s < 6; s++)
        fprintf(f, ",s%d_id,s%d_costume,s%d_pad,s%d_x,s%d_y,s%d_z,s%d_spirit,s%d_stamina,s%d_move,s%d_state,s%d_phase,"
                   "s%d_flags,s%d_e1,s%d_ring,s%d_elim,s%d_facing", s, s, s, s, s, s, s, s, s, s, s, s, s, s, s, s);
    // (the 32-slot work: across every slot in play, how many hold a Superstar and how many are in the ring -- logic.c)
    fprintf(f, ",present,in_ring,ann_listed,rr_next");
    // (New feature (Ultimate No Mercy) -- lumberjack2, 2026-11-21: the Lumberjack match's eight words, mod src/lumberjack.c lumberjack_qa2)
    fprintf(f, ",lj_a,lj_b,lj_c,lj_d,lj_e,lj_f,lj_g,lj_h,lj_i,lj_j,lj_k,lj_l,lj_m,lj_n,lj_o,lj_p,lj_q,lj_r,lj_s,lj_t,lj_u,lj_v,lj_w,lj_x,lj_y,lj_z");   // (lj_i .. lj_m: lumberjack3; lj_n, lj_o: lumberjack4; lj_p .. lj_s: ljtwitch, the jitter measure; lj_t .. lj_w: ljround8, aggression / escape loop / intro stops; lj_x / lj_y: ljround9, the lumberjacks' grapple moves outside, the aggression over the run)
    fprintf(f, "\n"); fflush(f);
    const auto t0 = std::chrono::steady_clock::now();
    uint32_t last = 0xFFFFFFFFu;
    auto fl = [](uint32_t v) { float x; memcpy(&x, &v, 4); return x; };
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        g_qa_reading = true;
        if (g_stop) { g_qa_reading = false; break; }
        struct Done { ~Done() { g_qa_reading = false; } } done;   // (every way out of this pass)
        const uint32_t Q = 0x807F3000u;
        if (!g_rdram || rd(Q) != 0x51415354u) continue;   // 'QAST'
        uint32_t frame = rd(Q + 4);
        if (frame == last) continue;
        last = frame;
        long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        fprintf(f, "%lld,%d,%u,%u,%u,%d,%d,%u,%d,%d,%u,0x%08X,%d,%u", ms, wcw_viswaps_total.load(), frame, rd(Q + 8),
                rd(Q + 0xC), (int)rd(Q + 0x10), (int)rd(Q + 0x14), rd(Q + 0x18), (int)rd(Q + 0x1C), (int)rd(Q + 0x20),
                rd(Q + 0x24), rd(Q + 0x28), (int)rd(Q + 0x2C), rd(Q + 0x30));
        fprintf(f, ",%u,0x%02X,%u,%u", rd(Q + 0x34), rd(Q + 0x38), rd(Q + 0x3C), rd(Q + 0x1C0));
        for (uint32_t i = 0; i < 2; i++)   // JR's, King's: their bones (heap), root x y z
            fprintf(f, ",0x%08X,%.1f,%.1f,%.1f", rd(Q + 0x1C4 + 16 * i), fl(rd(Q + 0x1C8 + 16 * i)), fl(rd(Q + 0x1CC + 16 * i)), fl(rd(Q + 0x1D0 + 16 * i)));
        fprintf(f, ",0x%08X,0x%08X", rd(Q + 0x1E4), rd(Q + 0x1E8));
        // the camera's eye z; the announcers' draw flags (JR | King << 8; 0x80 not drawn; 0xFFFF not built)
        fprintf(f, ",%.1f,0x%04X", fl(rd(Q + 0x1EC)), rd(Q + 0x1F0));
        // King's head pictures: where, and their checksum (a change: written over)
        fprintf(f, ",0x%08X,0x%08X,0x%08X,0x%08X", rd(Q + 0x1F4), rd(Q + 0x1F8), rd(Q + 0x1FC), rd(Q + 0x200));
        // where King's body is drawn: hips, head (world)
        for (uint32_t a = 0x204; a < 0x21C; a += 4) fprintf(f, ",%.1f", fl(rd(Q + a)));
        // the announcers' poses that weren't numbers, replaced by their last good ones (announce.c pose_kept, 0x807F32C0)
        fprintf(f, ",%u", rd(Q + 0x2C0));
        // the scripted back suplex (logic.c qa_bsuplex): tries, back grapples got, its step
        fprintf(f, ",%u,%u,%u", rd(Q + 0x220), rd(Q + 0x224), rd(Q + 0x228));
        fprintf(f, ",%u,%u,%u", rd(Q + 0x22C) & 0xFF, (rd(Q + 0x22C) >> 8) & 0xFF, rd(Q + 0x22C) >> 16);
        // (skel.c pal_group_guard's last call: part, palette, room, at | n << 16, slot | its flag << 8)
        fprintf(f, ",0x%08X,0x%08X,0x%08X,0x%08X,0x%X", rd(0x807F2F30u), rd(0x807F2F34u), rd(0x807F2F38u), rd(0x807F2F3Cu), rd(0x807F2F48u));
        // (the Rumble: CPU Superstars QA put outside (qa_rr_out); eliminated men kept out, no entrant for them -- logic.c)
        fprintf(f, ",%u,%u", rd(Q + 0x230) & 0xFFFF, rd(Q + 0x230) >> 16);   // palette groups skel.c's pal_group_guard didn't load (no room), of all
        for (uint32_t s = 0; s < 6; s++) {
            uint32_t q = Q + 0x40 + 0x40 * s, id = rd(q), sp = rd(q + 0x14), mv = rd(q + 0x18), fg = rd(q + 0x20);
            fprintf(f, ",0x%04X,%u,%d,%.1f,%.1f,%.1f,%d,%d,%u,%u,%d,0x%04X,0x%02X,%u,%d,%d", id & 0xFFFF, (id >> 16) & 0xF,
                    (int)rd(q + 4), fl(rd(q + 8)), fl(rd(q + 0xC)), fl(rd(q + 0x10)), (int)(int16_t)(sp & 0xFFFF),
                    (int)(int16_t)(sp >> 16), mv >> 16, mv & 0xFFFF, (int)rd(q + 0x1C), fg & 0xFFFF, (fg >> 16) & 0xFF,
                    fg >> 24, (int)rd(q + 0x24), (int)rd(q + 0x28));
        }
        fprintf(f, ",%u,%u,%u", rd(Q + 0x238) & 0xFF, (rd(Q + 0x238) >> 8) & 0xFF, rd(Q + 0x23C));
        fprintf(f, ",0x%08X", rd(Q + 0x240));   // (+0x240: the Rumble results table's automatic Next -- what it gave | tries << 16)   // (+0x23C: the announcers' draw-list entries)
        for (uint32_t a = 0x244; a <= 0x2A8; a += 4) fprintf(f, ",%u", rd(Q + a));   // (lumberjack2: lj_a .. lj_h; lumberjack3: lj_i .. lj_m; lumberjack4: lj_n, lj_o; ljtwitch: lj_p .. lj_s; ljround8: lj_t .. lj_w; ljround9: lj_x, lj_y; ljround10: lj_z)
        fprintf(f, "\n"); fflush(f);
    }
    fclose(f);
}
}  // namespace

// The game has stopped: its RAM is freed next (the runtime calls this just before, wcw_rdram_release_hook). The threads
// that read it (the QA writer, the watchdog) stop reading first -- reading it after it's gone was a crash on closing
// the window (0.36.6; and ~1 quit in 4 in QA till 2026-10-29, when this still ran only after recomp::start returned).
extern std::atomic_bool g_mw_stop, g_mw_reading;   // (main.cpp: the QA write-watch; 2026-11-02: it read the freed RAM when the game quit by itself)
void crash_report_shutdown() {
    g_stop = true;
    g_mw_stop = true;
    while (g_qa_reading || g_wd_reading || g_mw_reading) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    g_rdram = nullptr;
}

// A QA run's RAM as it ends (WCW_QUIT_AFTER, before the game stops): next to its state -- the arcade's own records
// (skel.c's guard's last calls, ...) read afterwards.
void crash_report_qa_snapshot() {
    if (const char* qa = getenv("WCW_QA_STATE")) dump_memory(std::filesystem::path(qa).replace_extension(".rdram.bin"));
}
extern void (*wcw_rdram_release_hook)();   // (librecomp recomp.cpp: called before the game's RAM is freed)
void crash_report_init(const std::filesystem::path& app_folder) {
    wcw_rdram_release_hook = crash_report_shutdown;
    g_dir = app_folder / "crash_reports";
    std::thread(watchdog).detach();
    if (const char* qa = getenv("WCW_QA_STATE")) std::thread(qa_state_writer, std::string(qa)).detach();
}
void crash_report_set_rdram(uint8_t* rdram) { g_rdram = rdram; }
void crash_report_crash(EXCEPTION_POINTERS* ep) { write_report("crash", ep); }
#endif
