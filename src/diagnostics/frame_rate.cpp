// frame_rate.cpp: optional frame-rate log for measuring the port's speed.
//
// Why
//   The port's speed has to be measured, on Linux and on Windows, without a
//   debugger attached. The host window always presents at 60 Hz, so the
//   number that matters is how many frames the GAME finishes per second.
//
// How the game marks a finished frame (findings/15 s.12)
//   The game's state struct is the global 0x00268540:
//     +0x08 flags: bit 0x08 = a frame was kicked to the GS (VIF1 DMA),
//           bit 0x10 = waiting for that DMA to end
//     +0x0C VBlank counter: the VBlank handler FUN_0012E5F8 adds 1 to it
//   The VIF1 DMA end handler FUN_0012E670 (registered with
//   AddDmacHandler(1, ...)) clears bits 0x08 and 0x10 when a frame's DMA is
//   done. So "FUN_0012E670 called with bit 0x08 set" = one frame finished.
//
// What this file does
//   Only when the environment variable BEN10_FPS_LOG is set (to anything),
//   it wraps those two handlers: each wrapper counts, then calls the
//   original recompiled function unchanged. About once per second (host
//   time) it prints to stderr:
//     [fps] game frames 24 in 1.00 s, vblanks 48; frames up to 34 ms 20, to 50 ms 3, to 100 ms 1, longer 0 (longest 81 ms)
//   The second part is the frame pacing (findings/20 s.26): the host time
//   between two finished frames. The game shows a new picture at most
//   every second VBlank (30 per second, 33.3 ms), so a frame up to 34 ms
//   is on time, one to 50 ms a hitch, longer ones visible stalls. Host
//   time, not VBlanks: the port's VBlanks follow the EE's clock, which
//   slows down with the port, so when the port is slow the game runs
//   slower (vblanks below 60 in a second) instead of skipping VBlanks.
//   Without the variable nothing is installed and nothing changes.

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "game_overrides.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Named namespace: the port sources are compiled as one unity batch, where
// two anonymous-namespace "apply" functions would collide.
namespace ben10::frame_rate_log
{
    constexpr uint32_t kGameState = 0x00268540u;      // the game's state struct (findings/15 s.12)
    constexpr uint32_t kVif1DmaEndHandler = 0x0012E670u;
    constexpr uint32_t kVBlankHandler = 0x0012E5F8u;
    constexpr uint32_t kFrameKickedBit = 0x08u;       // state+0x08: a frame's DMA was kicked

    PS2Runtime::RecompiledFunction g_originalDmaEnd = nullptr;
    PS2Runtime::RecompiledFunction g_originalVBlank = nullptr;
    std::atomic<uint32_t> g_frames{0};
    std::atomic<uint32_t> g_vblanks{0};
    std::chrono::steady_clock::time_point g_lastReport{};

    // Frame pacing. Both handlers run on the game thread, so plain
    // variables do.
    std::chrono::steady_clock::time_point g_lastFrameTime{};
    uint32_t g_frameLengths[4] = {}; // finished frames of up to 34, 50, 100 ms, and longer
    double g_longestMs = 0.0;

    // Prints and resets the counters once at least a second has passed.
    // Called from the game thread only (both handlers run there).
    void maybeReport()
    {
        const auto now = std::chrono::steady_clock::now();
        if (g_lastReport.time_since_epoch().count() == 0)
        {
            g_lastReport = now;
            return;
        }
        const double seconds = std::chrono::duration<double>(now - g_lastReport).count();
        if (seconds < 1.0)
            return;
        std::fprintf(stderr, "[fps] game frames %u in %.2f s, vblanks %u; frames up to 34 ms %u, to 50 ms %u, to 100 ms %u, longer %u (longest %.0f ms)\n",
                     g_frames.exchange(0), seconds, g_vblanks.exchange(0), g_frameLengths[0], g_frameLengths[1],
                     g_frameLengths[2], g_frameLengths[3], g_longestMs);
        for (uint32_t &n : g_frameLengths)
            n = 0u;
        g_longestMs = 0.0;
        std::fflush(stderr);
        g_lastReport = now;
    }

    void countedDmaEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Read the flags before the original handler clears them.
        if ((READ32(kGameState + 0x08u) & kFrameKickedBit) != 0u)
        {
            g_frames.fetch_add(1, std::memory_order_relaxed);
            const auto now = std::chrono::steady_clock::now();
            if (g_lastFrameTime.time_since_epoch().count() != 0)
            {
                const double ms = std::chrono::duration<double, std::milli>(now - g_lastFrameTime).count();
                ++g_frameLengths[ms <= 34.0 ? 0 : ms <= 50.0 ? 1 : ms <= 100.0 ? 2 : 3];
                g_longestMs = std::max(g_longestMs, ms);
            }
            g_lastFrameTime = now;
        }
        maybeReport();
        g_originalDmaEnd(rdram, ctx, runtime);
    }

    void countedVBlank(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_vblanks.fetch_add(1, std::memory_order_relaxed);
        g_originalVBlank(rdram, ctx, runtime);
    }

    void apply(PS2Runtime &runtime)
    {
        if (!std::getenv("BEN10_FPS_LOG"))
            return;
        g_originalDmaEnd = runtime.lookupFunction(kVif1DmaEndHandler);
        g_originalVBlank = runtime.lookupFunction(kVBlankHandler);
        if (!g_originalDmaEnd || !g_originalVBlank)
        {
            std::fprintf(stderr, "[fps] handlers not found, frame-rate log disabled\n");
            return;
        }
        runtime.replaceFunction(kVif1DmaEndHandler, &countedDmaEnd);
        runtime.replaceFunction(kVBlankHandler, &countedVBlank);
        std::fprintf(stderr, "[fps] frame-rate log on (BEN10_FPS_LOG)\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("Frame-rate log (BEN10_FPS_LOG)", "SLUS_216.61", 0x00100008u, 0u, &ben10::frame_rate_log::apply);
