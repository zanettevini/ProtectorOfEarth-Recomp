// sample_profiler.cpp: optional sampling profiler of the game thread (Windows).
//
// Why
//   On Windows the first level is limited by the game thread (one core at
//   100%, findings/21 s.7), but the Release build has no debug symbols, so
//   the usual profilers can't say where its time goes. This file samples
//   the thread's call stack from inside the process; the build writes a
//   linker map (ben10_poe.map, CMakeLists.txt) that tools/perf/win_profile.py
//   uses to turn the addresses into function names.
//
// What this file does
//   Only when the environment variable BEN10_PROFILE is set: its value is
//   the output file ("1" means profile.txt in the working directory). A
//   host thread finds the thread named GameThread (or the name in
//   BEN10_PROFILE_THREAD) and, about every millisecond:
//     - suspends it, reads its registers and walks its stack with the x64
//       unwind tables (RtlLookupFunctionEntry / RtlVirtualUnwind), up to
//       kMaxFrames return addresses, into a fixed buffer;
//     - resumes it, and only then counts the stack (nothing is allocated
//       while the thread is suspended: it may hold the heap lock).
//   Every 10 s it appends one window to the file: the time span and each
//   distinct stack with its count, each address written as
//   module-index:offset-from-module-base (hex), leaf first. Module lines
//   give the index -> file name.
//   Without the variable nothing is started. On other platforms the
//   variable only prints that the profiler is not available (on Linux,
//   gdb sampling is used instead: tools/perf).
//
// Cost and risk
//   A suspend/resume and a stack walk per millisecond: a few percent of the
//   game thread. If the game thread is suspended while it is itself inside
//   the unwinder (C++ exception handling), RtlLookupFunctionEntry could
//   wait for a lock that thread holds; the game does not throw in normal
//   play, and this is a diagnostic switched on by hand.

// Includes only game_overrides.h (PS2Runtime is forward-declared there):
// <windows.h> clashes with raylib and with the runtime's own Win32
// declarations (ThreadNaming.h), so CMakeLists.txt keeps this file out of
// the unity build and the precompiled headers.
#include "game_overrides.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <tlhelp32.h>
#include <psapi.h>

#include <chrono>
#include <map>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif

namespace ben10::sample_profiler
{
    constexpr int kMaxFrames = 48;
    constexpr DWORD kWindowMs = 10000;

    using GetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PWSTR *);

    // The thread's name as SetThreadDescription set it (Windows 10 1607+).
    std::wstring threadName(DWORD id, GetThreadDescriptionFn getDescription)
    {
        std::wstring name;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
        if (h == nullptr)
            return name;
        PWSTR description = nullptr;
        if (SUCCEEDED(getDescription(h, &description)) && description != nullptr)
        {
            name = description;
            LocalFree(description);
        }
        CloseHandle(h);
        return name;
    }

    // Finds a thread of this process by name; 0 when there is none yet.
    DWORD findThread(const std::wstring &wanted, GetThreadDescriptionFn getDescription)
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE)
            return 0;
        DWORD found = 0;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        const DWORD self = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snap, &entry); ok && found == 0; ok = Thread32Next(snap, &entry))
        {
            if (entry.th32OwnerProcessID == self && threadName(entry.th32ThreadID, getDescription) == wanted)
                found = entry.th32ThreadID;
        }
        CloseHandle(snap);
        return found;
    }

    // Walks the suspended thread's stack from its registers. Only reads
    // memory and the loaded modules' unwind tables: no allocation.
    int walk(CONTEXT &context, DWORD64 frames[kMaxFrames])
    {
        int count = 0;
        while (count < kMaxFrames && context.Rip != 0)
        {
            frames[count++] = context.Rip;
            DWORD64 imageBase = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
            if (function == nullptr)
            {
                // A leaf function without unwind data has its return
                // address at the top of the stack. Only trusted for the
                // innermost frame; deeper, the walk has gone wrong: stop.
                if (count != 1 || context.Rsp == 0)
                    break;
                context.Rip = *reinterpret_cast<const DWORD64 *>(context.Rsp);
                context.Rsp += 8;
                continue;
            }
            PVOID handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context,
                             &handlerData, &establisherFrame, nullptr);
        }
        return count;
    }

    struct Module
    {
        DWORD64 base = 0;
        DWORD64 size = 0;
        std::string path;
    };

    // Module index and offset for an address; adds the module on first use.
    std::string place(DWORD64 address, std::vector<Module> &modules, FILE *out)
    {
        char text[32];
        for (size_t i = 0; i < modules.size(); ++i)
        {
            if (address >= modules[i].base && address < modules[i].base + modules[i].size)
            {
                std::snprintf(text, sizeof(text), "%zu:%llx", i,
                              static_cast<unsigned long long>(address - modules[i].base));
                return text;
            }
        }
        HMODULE handle = nullptr;
        MODULEINFO info{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &handle) &&
            GetModuleInformation(GetCurrentProcess(), handle, &info, sizeof(info)))
        {
            Module module;
            module.base = reinterpret_cast<DWORD64>(info.lpBaseOfDll);
            module.size = info.SizeOfImage;
            char path[MAX_PATH] = {};
            GetModuleFileNameA(handle, path, MAX_PATH);
            module.path = path;
            modules.push_back(module);
            std::fprintf(out, "module %zu %s\n", modules.size() - 1, module.path.c_str());
            std::snprintf(text, sizeof(text), "%zu:%llx", modules.size() - 1,
                          static_cast<unsigned long long>(address - module.base));
            return text;
        }
        std::snprintf(text, sizeof(text), "?:%llx", static_cast<unsigned long long>(address));
        return text;
    }

    void run(std::string path, std::wstring wantedName)
    {
        auto getDescription = reinterpret_cast<GetThreadDescriptionFn>(reinterpret_cast<void *>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
        if (getDescription == nullptr)
        {
            std::fprintf(stderr, "[profile] GetThreadDescription missing (Windows 10 1607+ needed): off\n");
            return;
        }
        DWORD threadId = 0;
        while ((threadId = findThread(wantedName, getDescription)) == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
        if (thread == nullptr)
        {
            std::fprintf(stderr, "[profile] cannot open the thread: off\n");
            return;
        }
        FILE *out = std::fopen(path.c_str(), "w");
        if (out == nullptr)
        {
            std::fprintf(stderr, "[profile] cannot write %s: off\n", path.c_str());
            return;
        }
        std::fprintf(out, "# ben10 sample profile v1: windows of %lu ms; lines: count, then module:offset frames, leaf first\n",
                     static_cast<unsigned long>(kWindowMs));
        std::fprintf(stderr, "[profile] sampling thread %lu into %s\n", static_cast<unsigned long>(threadId), path.c_str());

        timeBeginPeriod(1); // 1 ms sleeps instead of the default ~15.6 ms
        std::vector<Module> modules;
        std::map<std::vector<DWORD64>, unsigned> stacks;
        const ULONGLONG start = GetTickCount64();
        ULONGLONG windowStart = start;
        unsigned samples = 0;
        int window = 0;
        DWORD64 frames[kMaxFrames];
        for (;;)
        {
            Sleep(1);
            if (SuspendThread(thread) == static_cast<DWORD>(-1))
                break; // the thread has ended
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            int count = 0;
            if (GetThreadContext(thread, &context))
                count = walk(context, frames);
            ResumeThread(thread);
            if (count > 0)
            {
                stacks[std::vector<DWORD64>(frames, frames + count)]++;
                ++samples;
            }

            const ULONGLONG now = GetTickCount64();
            if (now - windowStart >= kWindowMs)
            {
                std::fprintf(out, "window %d start_ms %llu end_ms %llu samples %u\n", window,
                             static_cast<unsigned long long>(windowStart - start),
                             static_cast<unsigned long long>(now - start), samples);
                for (const auto &[stack, n] : stacks)
                {
                    std::string line = std::to_string(n);
                    for (DWORD64 address : stack)
                        (line += ' ') += place(address, modules, out);
                    std::fprintf(out, "%s\n", line.c_str());
                }
                std::fflush(out);
                stacks.clear();
                samples = 0;
                windowStart = now;
                ++window;
            }
        }
        std::fclose(out);
        CloseHandle(thread);
    }

    void apply(PS2Runtime &)
    {
        const char *value = std::getenv("BEN10_PROFILE");
        if (value == nullptr)
            return;
        std::string path = (std::string(value) == "1" || *value == '\0') ? "profile.txt" : value;
        std::wstring name = L"GameThread";
        if (const char *wanted = std::getenv("BEN10_PROFILE_THREAD"))
            name.assign(wanted, wanted + std::char_traits<char>::length(wanted));
        std::thread(run, path, name).detach();
        std::fprintf(stderr, "[profile] sampling profiler on (BEN10_PROFILE): waiting for the thread\n");
    }
}

#else

namespace ben10::sample_profiler
{
    void apply(PS2Runtime &)
    {
        if (std::getenv("BEN10_PROFILE") != nullptr)
            std::fprintf(stderr, "[profile] BEN10_PROFILE: the sampling profiler is Windows only (use tools/perf on Linux)\n");
    }
}

#endif

PS2_REGISTER_GAME_OVERRIDE("Sampling profiler (BEN10_PROFILE)", "SLUS_216.61", 0x00100008u, 0u, &ben10::sample_profiler::apply);
