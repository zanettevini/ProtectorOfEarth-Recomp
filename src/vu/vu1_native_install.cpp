// vu1_native_install.cpp: turns on the translated VU1 code (vu1_native.h) when the game starts.
//
// A game override: before the game runs, VU1Interpreter::setNativeResolver
// gets ben10::vu1_native::resolve, so every VU1 program run uses the
// translated function of each pair whose address and 64 bits match the
// generated tables, and the interpreter for the rest (findings/21).
// BEN10_VU1_NATIVE=0 leaves everything to the interpreter (comparisons).

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
        VU1Interpreter::setNativeResolver(&ben10::vu1_native::resolve);
        std::fprintf(stderr, "[vu1] translated VU1 code on: %zu pairs (BEN10_VU1_NATIVE=0 turns it off)\n",
                     ben10::vu1_native::pairCount());
    }
}

PS2_REGISTER_GAME_OVERRIDE("Translated VU1 code", "SLUS_216.61", 0x00100008u, 0u, &ben10::vu1_native_install::apply);
