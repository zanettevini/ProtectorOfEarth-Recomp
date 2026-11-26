// vu1_replay.cpp: replays captured VU1 (and VU0) workloads offline
// (findings/20 s.2, findings/21 s.19).
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
//   Always: total VU cycles, wall time, VU cycles per second, and how many
//   calls went to translated code and how many pairs to the interpreter.
//   --native: the translated VU1 code (src/vu/vu1_native.h, findings/21)
//   runs every pair it has; the sums must not change. Translated blocks
//   (findings/21 s.24) are on with it; --no-blocks runs pair by pair.
//
// VU0 (--vu0): the game's VU0 microprograms (the skinning, started with
//   VCALLMS, findings/21 s.19), saved by capture_vu0.gdb when
//   PS2Runtime::executeVU0Microprogram calls VU1Interpreter::execute:
//   NNN_code.bin   VU0 micro memory, 4 KB
//   NNN_data.bin   VU0 data memory, 4 KB
//   NNN_state.bin  the VU0 instance's VU1State at the call (after the
//                  runtime's reset and the copy of the EE's VU0 registers)
//   NNN_args.bin   two uint32: the start PC and ITOP of the call
//   Each one runs as the runtime runs it: reset, the saved state, then
//   execute() from the start PC with a budget of 4096 cycles. The sum is the
//   same as above without the GS memory (VU0 has no XGKICK); with --native
//   the pairs valid on VU0 run translated (resolveVu0).
//
// USAGE
//   cmake --build --preset linux-debug --target ben10_vu1_replay
//   out/build/linux-debug/ben10_vu1_replay user/vu1_captures [--sum] [--reps N] [--native]
//   out/build/linux-debug/ben10_vu1_replay user/vu0_captures --vu0 [--sum] [--reps N] [--native]
//   Profiling: valgrind --tool=callgrind out/build/.../ben10_vu1_replay ...
//
// The data files are the game's: they stay in user/ and are never versioned.

#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#if defined(BEN10_HAVE_VU1_NATIVE)
#include "vu/vu1_native.h"
#endif

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
        uint32_t startPC = 0; // VU0 only: the VCALLMS address
        uint32_t itop = 0;    // VU0 only
    };

    // One checksum of everything a run produced (see OUTPUT above).
    uint64_t stateSum(const VU1State &s, uint64_t cycles, const std::vector<uint8_t> &data)
    {
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
        return fnv1a(data.data(), data.size(), h);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <capture folder> [--vu0] [--sum] [--reps N] [--native [--no-blocks]]\n", argv[0]);
        return 2;
    }
    const std::string folder = argv[1];
    bool printSums = false;
    int reps = 1;
    bool vu0Mode = false;
    bool native = false;
    for (int i = 2; i < argc; ++i)
        if (std::strcmp(argv[i], "--vu0") == 0)
            vu0Mode = true;
#if defined(BEN10_HAVE_VU1_NATIVE)
        else if (std::strcmp(argv[i], "--no-blocks") == 0)
            ben10::vu1_native::setBlocksEnabled(false);
#endif
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--sum") == 0)
            printSums = true;
        else if (std::strcmp(argv[i], "--reps") == 0 && i + 1 < argc)
            reps = std::max(1, std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--native") == 0)
        {
#if defined(BEN10_HAVE_VU1_NATIVE)
            native = true;
            if (vu0Mode)
            {
                VU1Interpreter::setNativeResolverVu0(&ben10::vu1_native::resolveVu0);
                std::fprintf(stderr, "translated VU0 code: %zu pairs\n", ben10::vu1_native::vu0PairCount());
            }
            else
            {
                VU1Interpreter::setNativeResolver(&ben10::vu1_native::resolve);
                std::fprintf(stderr, "translated VU1 code: %zu pairs, %zu blocks\n", ben10::vu1_native::pairCount(),
                             ben10::vu1_native::blockCount());
            }
#else
            std::fprintf(stderr, "--native: this build has no translated VU1 code (no game ELF at configure time)\n");
            return 2;
#endif
        }
    }

    (void)native;
    // Load every NNN_* set, from 000 up to the first missing one.
    const size_t memSize = vu0Mode ? 4096u : 16384u;
    std::vector<Workload> workloads;
    for (int n = 0;; ++n)
    {
        char prefix[32];
        std::snprintf(prefix, sizeof(prefix), "/%03d_", n);
        Workload w;
        w.code = readFile(folder + prefix + "code.bin");
        w.data = readFile(folder + prefix + "data.bin");
        const std::vector<uint8_t> state = readFile(folder + prefix + "state.bin");
        if (w.code.size() != memSize || w.data.size() != memSize || state.size() != sizeof(VU1State))
            break;
        std::memcpy(&w.state, state.data(), sizeof(VU1State));
        if (vu0Mode)
        {
            const std::vector<uint8_t> args = readFile(folder + prefix + "args.bin");
            if (args.size() != 8u)
                break;
            std::memcpy(&w.startPC, args.data(), 4);
            std::memcpy(&w.itop, args.data() + 4, 4);
        }
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
    VU1Interpreter vu(vu0Mode ? VU1Interpreter::Unit::VU0 : VU1Interpreter::Unit::VU1);

    uint64_t totalCycles = 0;
    uint64_t lastCycles = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int rep = 0; rep < reps; ++rep)
    {
        for (size_t i = 0; i < workloads.size(); ++i)
        {
            std::vector<uint8_t> code = workloads[i].code;
            std::vector<uint8_t> data = workloads[i].data;
            uint64_t cycles = 0;
            if (vu0Mode)
            {
                // As PS2Runtime::executeVU0Microprogram: reset, the EE's VU0
                // registers (here the saved state), execute from the VCALLMS
                // address with 4096 cycles. reset() zeroes the cycle count.
                vu.reset();
                vu.state() = workloads[i].state;
                vu.execute(code.data(), 4096u, data.data(), 4096u, gs, nullptr,
                           workloads[i].startPC, 0u, workloads[i].itop, 4096u);
                cycles = vu.state().cycles;
            }
            else
            {
                vu.state() = workloads[i].state;
                // MSCNT: resume at the saved PC with the saved TOP/ITOP.
                vu.resume(code.data(), 16384u, data.data(), 16384u, gs, nullptr,
                          vu.state().top, vu.state().itop, 65536u);
                // VU1State::cycles is the interpreter's own running cycle count.
                cycles = vu.state().cycles - lastCycles;
                lastCycles = vu.state().cycles;
            }
            totalCycles += cycles;

            if (printSums && rep == 0)
            {
                uint64_t h = stateSum(vu.state(), cycles, data);
                if (!vu0Mode)
                    h = fnv1a(vram.data(), vram.size(), h);
                std::printf("%03zu %016llx\n", i, static_cast<unsigned long long>(h));
            }
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("workloads %zu x %d, VU cycles %llu, %.3f s, %.2f M VU cycles/s\n",
                workloads.size(), reps, static_cast<unsigned long long>(totalCycles), seconds,
                seconds > 0.0 ? static_cast<double>(totalCycles) / seconds / 1e6 : 0.0);
    // How much ran translated (VU1Interpreter::nativeCoverage, findings/21
    // s.27): with --native every pair of the game's programs should.
    const auto &coverage = vu.nativeCoverage();
    std::printf("translated calls %llu, pairs interpreted %llu (no table %llu, no pair %llu)\n",
                static_cast<unsigned long long>(coverage.nativeCalls.load()),
                static_cast<unsigned long long>(coverage.interpretedNoTable.load() + coverage.interpretedNoPair.load()),
                static_cast<unsigned long long>(coverage.interpretedNoTable.load()),
                static_cast<unsigned long long>(coverage.interpretedNoPair.load()));
    return 0;
}
