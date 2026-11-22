// vu1_native_install.cpp: turns on the translated VU1 code (vu1_native.h) when the game starts.
//
// A game override: before the game runs, VU1Interpreter::setNativeResolver
// gets ben10::vu1_native::resolve, so every VU1 program run uses the
// translated function of each pair whose address and 64 bits match the
// generated tables, and the interpreter for the rest (findings/21).
// BEN10_VU1_NATIVE=0 leaves everything to the interpreter (comparisons).
// The VU0 instance gets ben10::vu1_native::resolveVu0 the same way, for the
// pairs that also decode the same on VU0 (findings/21 s.19);
// BEN10_VU0_NATIVE=0 turns off only that part. Translated blocks
// (straight-line runs of pairs, findings/21 s.24) are on unless
// BEN10_VU1_BLOCKS=0.

#include "ps2_runtime.h"
#include "game_overrides.h"
#include "vu/vu1_native.h"

#include <cstdio>
#include <cstdlib>

namespace ben10::vu1_native_install
{
    void apply(PS2Runtime &)
    {
        if (const char *v = std::getenv("BEN10_VU1_NATIVE"); v != nullptr && v[0] == '0')
        {
            std::fprintf(stderr, "[vu1] BEN10_VU1_NATIVE=0: VU1 programs run in the interpreter\n");
            return;
        }
        // Translated blocks (findings/21 s.24): on unless BEN10_VU1_BLOCKS=0.
        if (const char *b = std::getenv("BEN10_VU1_BLOCKS"); b != nullptr && b[0] == '0')
        {
            ben10::vu1_native::setBlocksEnabled(false);
            std::fprintf(stderr, "[vu1] BEN10_VU1_BLOCKS=0: translated pairs run one by one\n");
        }
        else
            std::fprintf(stderr, "[vu1] translated blocks on: %zu (BEN10_VU1_BLOCKS=0 turns them off)\n",
                         ben10::vu1_native::blockCount());
        VU1Interpreter::setNativeResolver(&ben10::vu1_native::resolve);
        std::fprintf(stderr, "[vu1] translated VU1 code on: %zu pairs (BEN10_VU1_NATIVE=0 turns it off)\n",
                     ben10::vu1_native::pairCount());
        // The VU0 microprograms (the skinning, findings/21 s.19) are in the
        // same .vutext tables; pairs that decode the same on VU0 run on the
        // VU0 instance too.
        if (const char *v0 = std::getenv("BEN10_VU0_NATIVE"); v0 != nullptr && v0[0] == '0')
        {
            std::fprintf(stderr, "[vu0] BEN10_VU0_NATIVE=0: VU0 microprograms run in the interpreter\n");
            return;
        }
        VU1Interpreter::setNativeResolverVu0(&ben10::vu1_native::resolveVu0);
        std::fprintf(stderr, "[vu0] translated VU0 code on: %zu pairs (BEN10_VU0_NATIVE=0 turns it off)\n",
                     ben10::vu1_native::vu0PairCount());
    }
}

PS2_REGISTER_GAME_OVERRIDE("Translated VU1 code", "SLUS_216.61", 0x00100008u, 0u, &ben10::vu1_native_install::apply);
