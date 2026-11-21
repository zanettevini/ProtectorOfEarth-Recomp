// vu1_native.h: translated VU1 microcode (decision D14, findings/21).
//
// The game's VU microprograms are a closed list stored in the ELF
// (.vutext, described by .DVP.ovlytab, findings/11). tools/vu1_translate
// turns every instruction pair of every overlay chunk into a C++ function
// that runs exactly that pair: VU1Interpreter::execPair, the interpreter's
// own per-pair step, with the pair decoded at build time as a constant, so
// the compiler folds away every decision the interpreter takes per pair on
// the decoded fields. The generated files (generated/vu1/, never versioned:
// they are the game's code) define the tables declared here.
//
// At run time the resolver (vu1_native_resolver.cpp) gives the VU1
// interpreter, for each pair of its current code memory, the function whose
// pair has the same address and the same 64 bits, or nothing; the
// interpreter runs the pairs it gets no function for itself. A pair's
// decoding depends only on its 64 bits, so a function picked this way does
// exactly what the interpreter would do.

#pragma once

#include "runtime/ps2_vu1.h"

#include <cstddef>
#include <cstdint>

namespace ben10::vu1_native
{
    struct NativePair
    {
        uint32_t pc;    // byte address in VU1 micro memory (the chunk's VMA + offset)
        uint32_t lower; // the pair's lower word (first in memory)
        uint32_t upper; // the pair's upper word
        VU1Interpreter::NativePairFn fn;
    };

    struct NativeTable
    {
        const NativePair *pairs;
        size_t count;
    };

    // Defined by the generated index (generated/vu1/vu1_native_index.cpp).
    extern const NativeTable kTables[];
    extern const size_t kTableCount;

    // VU1Interpreter::NativeResolver.
    void resolve(const uint8_t *vuCode, uint32_t codeSize,
                 VU1Interpreter::NativePairFn *table, uint32_t entries);

    // Number of translated pairs (all tables).
    size_t pairCount();
}
