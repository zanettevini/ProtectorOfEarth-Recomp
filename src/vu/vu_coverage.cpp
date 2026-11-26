// vu_coverage.cpp: prints how much VU work ran as translated code (see
// vu_coverage.h, findings/21 s.27). Called by the frame-rate log
// (src/diagnostics/frame_rate.cpp) every 10 seconds, so it is on whenever
// BEN10_FPS_LOG is set.

#include "vu/vu_coverage.h"

#include "ps2_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

namespace ben10::vu_coverage
{
    namespace
    {
        // The counters as last read, per unit (index 0 = VU1, 1 = VU0).
        struct Seen
        {
            uint64_t calls = 0, noTable = 0, noPair = 0;
            std::vector<uint32_t> at;
        };
        Seen g_seen[2];

        // Formats one unit's part of the line into out, and its busiest
        // interpreted PCs into busiest.
        void unitPart(const VU1Interpreter &vu, Seen &seen, const char *name, char *out, size_t size,
                      char *busiest, size_t busiestSize)
        {
            const auto &c = vu.nativeCoverage();
            const uint64_t calls = c.nativeCalls.load(std::memory_order_relaxed);
            const uint64_t noTable = c.interpretedNoTable.load(std::memory_order_relaxed);
            const uint64_t noPair = c.interpretedNoPair.load(std::memory_order_relaxed);
            const uint64_t dCalls = calls - seen.calls;
            const uint64_t dNoTable = noTable - seen.noTable;
            const uint64_t dNoPair = noPair - seen.noPair;
            std::snprintf(out, size, "%s %llu translated calls, %llu pairs interpreted (no table %llu, no pair %llu)", name,
                          static_cast<unsigned long long>(dCalls), static_cast<unsigned long long>(dNoTable + dNoPair),
                          static_cast<unsigned long long>(dNoTable), static_cast<unsigned long long>(dNoPair));
            seen.calls = calls;
            seen.noTable = noTable;
            seen.noPair = noPair;

            // The four PCs interpreted most often since the last report.
            busiest[0] = '\0';
            if (seen.at.size() != c.interpretedAt.size())
                seen.at.assign(c.interpretedAt.size(), 0u);
            std::vector<std::pair<uint32_t, uint32_t>> top; // (count, slot)
            for (size_t slot = 0; slot < c.interpretedAt.size(); ++slot)
            {
                const uint32_t now = c.interpretedAt[slot].load(std::memory_order_relaxed);
                const uint32_t delta = now - seen.at[slot];
                seen.at[slot] = now;
                if (delta != 0u)
                    top.emplace_back(delta, static_cast<uint32_t>(slot));
            }
            if (top.empty())
                return;
            std::sort(top.begin(), top.end(), [](const auto &a, const auto &b)
                      { return a.first > b.first; });
            size_t used = static_cast<size_t>(std::snprintf(busiest, busiestSize, "; %s busiest:", name));
            for (size_t i = 0; i < top.size() && i < 4u && used < busiestSize; ++i)
                used += static_cast<size_t>(std::snprintf(busiest + used, busiestSize - used, " 0x%04X (%u)",
                                                          top[i].second * 8u, top[i].first));
        }
    }

    void report(PS2Runtime &runtime, double seconds)
    {
        char vu1[160], vu0[160], busy1[160], busy0[160];
        unitPart(runtime.vu1(), g_seen[0], "VU1", vu1, sizeof(vu1), busy1, sizeof(busy1));
        unitPart(runtime.vu0(), g_seen[1], "VU0", vu0, sizeof(vu0), busy0, sizeof(busy0));
        std::fprintf(stderr, "[vu-native] %.1f s: %s; %s%s%s\n", seconds, vu1, vu0, busy1, busy0);
    }
}
