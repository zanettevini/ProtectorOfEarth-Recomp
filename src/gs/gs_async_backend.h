// gs_async_backend.h: the GS raster backend that draws off the game thread, in row bands.
//
// The implementation and the rules that keep the picture the same are in
// gs_async_backend.cpp (findings/20 s.4, s.7, s.18, s.20-21). The game
// installs it from gs_async_install.cpp; tools/gs_replay builds it to replay
// recorded GS command streams (gs_record.h).

#pragma once

#include "runtime/gs/gs_backend.h"

#include <cstdint>
#include <memory>

namespace ben10::gs_async
{
    // A backend that queues every GS operation and draws it on a dispatcher
    // thread plus bandCount - 1 band threads (clamped to 1..16).
    std::unique_ptr<GSRasterBackend> makeAsyncBackend(uint32_t bandCount);
}
