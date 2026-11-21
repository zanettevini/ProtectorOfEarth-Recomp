// vu1_native_resolver.cpp: picks, for each pair of the VU1 code memory, the
// translated function that matches it (see vu1_native.h).
//
// Built into the ben10_vu1_native library (with the generated tables), not
// into ben10_poe's own sources, so the offline replay (tools/vu1_replay) can
// use it too.

#include "vu/vu1_native.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace ben10::vu1_native
{
    namespace
    {
        // Candidates per pair slot (pc / 8): the overlay groups overlap in
        // VU memory, so one address has several possible pairs.
        std::vector<std::vector<const NativePair *>> g_byPc;
        std::once_flag g_once;

        void buildIndex()
        {
            g_byPc.assign(0x4000u / 8u, {});
            for (size_t t = 0; t < kTableCount; ++t)
                for (size_t i = 0; i < kTables[t].count; ++i)
                {
                    const NativePair &p = kTables[t].pairs[i];
                    if ((p.pc >> 3) < g_byPc.size())
                        g_byPc[p.pc >> 3].push_back(&p);
                }
        }
    }

    size_t pairCount()
    {
        size_t n = 0;
        for (size_t t = 0; t < kTableCount; ++t)
            n += kTables[t].count;
        return n;
    }

    void resolve(const uint8_t *vuCode, uint32_t codeSize,
                 VU1Interpreter::NativePairFn *table, uint32_t entries)
    {
        std::call_once(g_once, buildIndex);
        for (uint32_t slot = 0; slot < entries && slot < g_byPc.size(); ++slot)
        {
            const uint32_t pc = slot * 8u;
            if (pc + 8u > codeSize)
                break;
            uint32_t lower = 0, upper = 0;
            std::memcpy(&lower, vuCode + pc, 4);
            std::memcpy(&upper, vuCode + pc + 4, 4);
            for (const NativePair *p : g_byPc[slot])
                if (p->lower == lower && p->upper == upper)
                {
                    table[slot] = p->fn;
                    break;
                }
        }
    }
}
