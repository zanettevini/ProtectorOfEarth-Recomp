// vu1_replay.cpp: replays captured VU1 workloads offline (findings/20 s.2).
//
// WHAT IT IS FOR
//   The VU1 microprograms are the largest cost in the first level
//   (findings/19 s.8). To work on them (speed up the runtime's VU1
//   interpreter now, check native VU1 code later) we need the same work
//   again and again, outside the game, with a way to tell whether the
//   results changed. capture_vu1.gdb saves workloads from the running game;
//   this program runs each one through PS2Recomp's VU1Interpreter (the
//   reference) and prints how fast, and, with --sum, one checksum per
//   workload of everything the program produced.
//
// INPUT (in the folder given, written by capture_vu1.gdb, from user/):
//   NNN_code.bin   VU1 micro memory, 16 KB
//   NNN_data.bin   VU1 data memory, 16 KB
//   NNN_state.bin  the interpreter's VU1State (registers, PC, TOP/ITOP...)
//   The program resumes at the saved PC, as VIF1's MSCNT does.
//
// OUTPUT
//   --sum: "NNN <64-bit FNV-1a>" per workload, over VF, VI, ACC, Q, P, I, R,
//   PC, MAC, clip, status, the number of VU cycles the run took, the VU1
//   data memory and the GS memory (XGKICK packets are drawn by the
//   runtime's GS into a private 4 MB VRAM). Two builds that print the same
//   lines computed the same thing.
//   Always: total VU cycles, wall time, VU cycles per second.
//
// USAGE
//   cmake --build --preset linux-debug --target ben10_vu1_replay
//   out/build/linux-debug/ben10_vu1_replay user/vu1_captures [--sum] [--reps N]
//   Profiling: valgrind --tool=callgrind out/build/.../ben10_vu1_replay ...
//
// The data files are the game's: they stay in user/ and are never versioned.

#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    std::vector<uint8_t> readFile(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    }

    uint64_t fnv1a(const void *data, size_t size, uint64_t hash = 1469598103934665603ull)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
        return hash;
    }

    struct Workload
    {
        std::vector<uint8_t> code;
        std::vector<uint8_t> data;
        VU1State state{};
    };
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <capture folder> [--sum] [--reps N]\n", argv[0]);
        return 2;
    }
    const std::string folder = argv[1];
    bool printSums = false;
    int reps = 1;
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--sum") == 0)
            printSums = true;
        else if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc)
            reps = std::max(1, std::atoi(argv[++i]));
    }

    // Load every NNN_* triple, from 000 up to the first missing one.
    std::vector<Workload> workloads;
    for (int n = 0;; ++n)
    {
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "/%03d_", n);
        Workload w;
        w.code = readFile(folder + prefix + "code.bin");
        w.data = readFile(folder + prefix + "data.bin");
        const std::vector<uint8_t> state = readFile(folder + prefix + "state.bin");
        if (w.code.size() != 16384u || w.data.size() != 16384u || state.size() != sizeof(VU1State))
            break;
        std::memcpy(&w.state, state.data(), sizeof(VU1State));
        workloads.push_back(std::move(w));
    }
    if (workloads.empty())
    {
        std::fprintf(stderr, "no workloads in %s (NNN_code.bin, NNN_data.bin, NNN_state.bin)\n", folder.c_str());
        return 1;
    }

    std::vector<uint8_t> vram(4u * 1024u * 1024u, 0u);
    GS gs;
    gs.init(vram.data(), static_cast<uint32_t>(vram.size()), nullptr);
    VU1Interpreter vu(VU1Interpreter::Unit::VU1);

    uint64_t totalCycles = 0;
    uint64_t lastCycles = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int rep = 0; rep < reps; ++rep)
    {
        for (size_t i = 0; i < workloads.size(); ++i)
        {
            std::vector<uint8_t> code = workloads[i].code;
            std::vector<uint8_t> data = workloads[i].data;
            vu.state() = workloads[i].state;
            // MSCNT: resume at the saved PC with the saved TOP/ITOP.
            vu.resume(code.data(), 16384u, data.data(), 16384u, gs, nullptr,
                      vu.state().top, vu.state().itop, 65536u);
            // VU1State::cycles is the interpreter's own running cycle count.
            const uint64_t cycles = vu.state().cycles - lastCycles;
            lastCycles = vu.state().cycles;
            totalCycles += cycles;

            if (printSums && rep == 0)
            {
                const VU1State &s = vu.state();
                uint64_t h = fnv1a(s.vf, sizeof(s.vf));
                h = fnv1a(s.vi, sizeof(s.vi), h);
                h = fnv1a(s.acc, sizeof(s.acc), h);
                h = fnv1a(&s.q, sizeof(s.q), h);
                h = fnv1a(&s.p, sizeof(s.p), h);
                h = fnv1a(&s.i, sizeof(s.i), h);
                h = fnv1a(&s.r, sizeof(s.r), h);
                h = fnv1a(&s.pc, sizeof(s.pc), h);
                h = fnv1a(&s.mac, sizeof(s.mac), h);
                h = fnv1a(&s.clip, sizeof(s.clip), h);
                h = fnv1a(&s.status, sizeof(s.status), h);
                h = fnv1a(&cycles, sizeof(cycles), h);
                h = fnv1a(data.data(), data.size(), h);
                h = fnv1a(vram.data(), vram.size(), h);
                std::printf("%03zu %016llx\n", i, static_cast<unsigned long long>(h));
            }
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("workloads %zu x %d, VU cycles %llu, %.3f s, %.2f M VU cycles/s\n",
                workloads.size(), reps, static_cast<unsigned long long>(totalCycles), seconds,
                seconds > 0.0 ? static_cast<double>(totalCycles) / seconds / 1e6 : 0.0);
    return 0;
}
