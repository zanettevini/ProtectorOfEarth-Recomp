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
//
// VU0 (findings/21 s.19): the VU0 microprograms (VCALLMS) are in the same
// tables. resolveVu0 hands the VU0 interpreter only the pairs marked as
// decoding the same on VU0 (the translator checks this at build time),
// since a few lower instructions are reserved there.

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
        bool vu0;       // decodes the same as VU0 code: may also run on VU0
    };

    struct NativeTable
    {
        const NativePair *pairs;
        size_t count;
    };

    // A translated block (findings/21 s.24): `count` consecutive pairs from
    // `pc`, whose words are words[2 * i] (lower) and words[2 * i + 1]
    // (upper). The resolver puts `fn` at the block's first pair only when
    // every pair of the block is in code memory as translated.
    struct NativeBlock
    {
        uint32_t pc;
        uint32_t count;
        const uint32_t *words;
        VU1Interpreter::NativePairFn fn;
        bool vu0; // every pair decodes the same as VU0 code
    };

    struct NativeBlockTable
    {
        const NativeBlock *blocks;
        size_t count;
    };

    // Defined by the generated index (generated/vu1/vu1_native_index.cpp).
    extern const NativeTable kTables[];
    extern const size_t kTableCount;
    extern const NativeBlockTable kBlockTables[];
    extern const size_t kBlockTableCount;

    // Whether the resolvers hand out blocks (default on). Off, every pair
    // runs through its own function as before (comparisons:
    // BEN10_VU1_BLOCKS=0, ben10_vu1_replay --no-blocks).
    void setBlocksEnabled(bool enabled);
    size_t blockCount();

    // VU1Interpreter::NativeResolver.
    void resolve(const uint8_t *vuCode, uint32_t codeSize,
                 VU1Interpreter::NativePairFn *table, uint32_t entries);

    // The same for the VU0 instance: only pairs marked vu0 (the VU0
    // skinning microprograms are overlay group B, findings/21 s.19).
    void resolveVu0(const uint8_t *vuCode, uint32_t codeSize,
                    VU1Interpreter::NativePairFn *table, uint32_t entries);

    // Number of translated pairs (all tables), and of those valid on VU0.
    size_t pairCount();
    size_t vu0PairCount();
}
