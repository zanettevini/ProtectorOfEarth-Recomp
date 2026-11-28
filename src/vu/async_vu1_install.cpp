// async_vu1_install.cpp: runs VIF1 and VU1 on a thread of their own (on by default).
//
// What it is for (findings/21 s.21-22)
//   Every game frame the game kicks two VIF1 DMA transfers from its VBlank
//   handler (FUN_0012E5F8). Each one runs the VU1 programs that transform
//   the frame's models, about 24 ms of game-thread CPU on the Linux test
//   machine, and the EE then works about 17 ms more before it next looks
//   at that transfer. With the runtime's default model the transfer is
//   done at once, inside the kick, so the EE waits for all of it.
//
// What it does
//   Unless the environment variable BEN10_ASYNC_VU1=0 is set: before the
//   game runs, PS2Memory::setAsyncVif1 (patch 0034) is turned on. It was
//   opt-in (BEN10_ASYNC_VU1=1) until the Windows measurement of
//   findings/20 s.41 (the first level at the game's own 30 fps, a fight
//   included); it became the default by the maintainer's decision (D15). A VIF1
//   kick then hands the transfer to a worker thread and the EE goes on.
//   The transfer ends for the EE (D_STAT, CHCR, the game's DMAC handler
//   FUN_0012E670) at a fixed point of the EE's own execution: an access to
//   VIF1/GIF/DMA/VU1/GS-control state, the EE going idle, the next VBlank
//   start (so the handler still runs before the VBlank handler that kicks
//   the next transfer), or, if BEN10_ASYNC_VU1_CYCLES is set, that many EE
//   cycles after the kick. That is later in EE time than the default model
//   (as on the console), but it does not depend on which thread is faster.
//   With BEN10_ASYNC_VU1=0 the runtime's default model runs: each transfer
//   is done inside its kick, on the game thread.

#include "ps2_runtime.h"
#include "game_overrides.h"

#include <cstdio>
#include <cstdlib>

namespace ben10::async_vu1
{
    // No EE-cycle limit by default: the next VBlank start ends a transfer
    // in flight. In the first level the EE runs about 47 million cycles
    // between a kick and its next look at the transfer (its idle thread
    // included), and a limit of 1 million made it wait for the worker
    // almost every time (findings/21 s.22).
    constexpr uint64_t kDefaultDeliveryCycles = 0u;

    void apply(PS2Runtime &runtime)
    {
        // On unless BEN10_ASYNC_VU1 starts with '0' (D15).
        const char *on = std::getenv("BEN10_ASYNC_VU1");
        if (on != nullptr && on[0] == '0')
        {
            std::fprintf(stderr, "[vif1-async] off (BEN10_ASYNC_VU1=0): VIF1/VU1 run inside each kick on the game thread\n");
            return;
        }
        uint64_t cycles = kDefaultDeliveryCycles;
        if (const char *c = std::getenv("BEN10_ASYNC_VU1_CYCLES"); c != nullptr && c[0] != '\0')
            cycles = std::strtoull(c, nullptr, 10);
        runtime.memory().setAsyncVif1(true, cycles);
        if (cycles != 0u)
            std::fprintf(stderr, "[vif1-async] VIF1/VU1 on a worker thread (default; BEN10_ASYNC_VU1=0 turns it off), "
                                 "completion by the next VBlank or %llu EE cycles after a kick\n",
                         static_cast<unsigned long long>(cycles));
        else
            std::fprintf(stderr, "[vif1-async] VIF1/VU1 on a worker thread (default; BEN10_ASYNC_VU1=0 turns it off), "
                                 "completion by the next VBlank\n");
    }
}

// (The registration's name comes from its line number: in a unity build this
// file and src/gs/gs_async_install.cpp share a translation unit, so the two
// registrations must not sit on the same line.)
PS2_REGISTER_GAME_OVERRIDE("Async VIF1/VU1 (BEN10_ASYNC_VU1)", "SLUS_216.61", 0x00100008u, 0u, &ben10::async_vu1::apply);
