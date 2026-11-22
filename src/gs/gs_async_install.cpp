// gs_async_install.cpp: installs the GS backend that draws off the game thread (findings/20 s.4, s.18).
//
// What it does
//   A game override, run when the ELF loads, swaps the GS frontend's raster
//   backend (GS::setRasterBackend, a public PS2Recomp API) for the one in
//   gs_async_backend.cpp, with this many row bands:
//     default            6 when the machine has the threads for them, keeping
//                        two for the game thread and the window
//                        (findings/20 s.18: on an 8-core desktop 6 bands drew
//                        the menus about 4 times as fast as 1, and 4 or 6 gave
//                        the same frame rate in the first level); one band on
//                        machines with fewer than 4 hardware threads
//     BEN10_GS_THREADS=N N bands (1..16)
//     BEN10_GS_SYNC=1    nothing is swapped: the original synchronous backend
//   With BEN10_GS_RECORD=<file> the backend is wrapped in the GS command
//   recorder (gs_record.h) for tools/gs_replay.

#include "ps2_runtime.h"
#include "game_overrides.h"
#include "runtime/gs/gs_frontend.h"

#include "gs_async_backend.h"
#include "gs_record.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

namespace ben10::gs_async
{
    namespace
    {
        double envSeconds(const char *name, double fallback)
        {
            const char *text = std::getenv(name);
            return (text && *text) ? std::atof(text) : fallback;
        }
    }

    void apply(PS2Runtime &runtime)
    {
        if (const char *sync = std::getenv("BEN10_GS_SYNC"); sync && *sync && *sync != '0')
        {
            std::fprintf(stderr, "[gs] BEN10_GS_SYNC set: the rasteriser stays on the submitting thread\n");
            return;
        }
        const unsigned hw = std::thread::hardware_concurrency();
        uint32_t bands = hw >= 4u ? std::min(6u, hw - 2u) : 1u;
        if (const char *threads = std::getenv("BEN10_GS_THREADS"); threads && *threads)
            bands = static_cast<uint32_t>(std::clamp(std::atoi(threads), 1, 16));
        std::unique_ptr<GSRasterBackend> backend = makeAsyncBackend(bands);
        if (const char *record = std::getenv("BEN10_GS_RECORD"); record && *record)
        {
            backend = std::make_unique<gs_record::RecordingBackend>(
                std::move(backend), record, envSeconds("BEN10_GS_RECORD_AFTER", 0.0),
                envSeconds("BEN10_GS_RECORD_SECONDS", 10.0));
            std::fprintf(stderr, "[gs-record] will record into %s (BEN10_GS_RECORD_AFTER, BEN10_GS_RECORD_SECONDS)\n", record);
        }
        runtime.gs().setRasterBackend(std::move(backend));
        std::fprintf(stderr, "[gs] rasteriser off the game thread, %u row band(s) (BEN10_GS_THREADS; BEN10_GS_SYNC=1 turns it off)\n", bands);
    }
}

PS2_REGISTER_GAME_OVERRIDE("GS rasteriser off the game thread", "SLUS_216.61", 0x00100008u, 0u, &ben10::gs_async::apply);
