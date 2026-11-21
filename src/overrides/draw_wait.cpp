// draw_wait.cpp: the game's "wait for the frame's DMA" loop, with a longer time step (findings/20 s.8).
//
// WHAT IT IS FOR
//   FUN_0012e580(state) waits, by spinning, until the VIF1 DMA end handler
//   (FUN_0012E670) clears bit 0x10 of the flags at state+0x08, or until ten
//   VBlanks have passed (the VBlank handler FUN_0012E5F8 counts them at
//   state+0x0C; state is 0x00268540). On timeout it calls 0x001BF478.
//   In the generated code every turn of the 3-instruction loop counts 32 EE
//   cycles at a checkpoint, and each checkpoint also steps the IOP
//   interpreter by those 32 cycles. While the game waits for the frame, the
//   PS2's clock (which decides when the next VBlank and DMA-end interrupts
//   come) therefore advances in 32-cycle steps, each paying the
//   checkpoint's fixed cost: in the first level about 15% of the game
//   thread's samples were this loop and the IOP steps it drove (findings/20 s.6).
//
// WHAT IT DOES
//   The same loop, natively, with one change: each turn counts
//   kWaitStepCycles (1024) EE cycles instead of 32. The loop only reads
//   two words, so nothing on the EE side can change between turns; the
//   events that end it (DMA end, VBlank) are taken at the first checkpoint
//   after they are due, as before, at most 1024 cycles (3.5 microseconds of
//   PS2 time) late instead of at most 32. EE timer interrupts that fall
//   inside a step are likewise taken at its end.
//
// HOW IT HOOKS IN
//   PS2Runtime::replaceFunction on 0x0012E580 (FUN_0012e580, entered at its
//   start) and 0x0012E590 (entry_0012e590, the loop head the scheduler
//   resumes at after a checkpoint). Register results are the same as the
//   generated code's: v0, v1, sp and the saved ra on the stack. When the
//   loop ends, ctx->pc = 0x0012E5B0 and the generated entry_0012e5b0 runs
//   the rest (the timeout test, the call, the return), as it already does
//   after a checkpoint. BEN10_DRAW_WAIT_STEP=32 restores the original step.

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "game_overrides.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ben10::draw_wait
{
    constexpr uint32_t kWaitFunction = 0x0012E580u; // FUN_0012e580
    constexpr uint32_t kLoopHead = 0x0012E590u;     // entry_0012e590
    constexpr uint32_t kAfterLoop = 0x0012E5B0u;    // entry_0012e5b0

    uint32_t g_stepCycles = 1024u;
    PS2Runtime::RecompiledFunction g_originalWait = nullptr;

    // 0x12E590-0x12E5AC: while ((state->flags & 0x10) && state->vblanks < limit).
    // a0 = state, v1 = limit (start count + 10).
    void spin(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        for (;;)
        {
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 8)));   // lw v0, 8(a0)
            SET_GPR_U64(ctx, 2, GPR_U64(ctx, 2) & (uint64_t)(uint16_t)16);     // andi v0, v0, 0x10
            if (GPR_U64(ctx, 2) == 0u)                                          // beqz v0, 0x12e5b0
                break;
            SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 12)));  // lw v0, 0xc(a0)
            SET_GPR_U64(ctx, 2, ((int64_t)GPR_S64(ctx, 2) < (int64_t)GPR_S64(ctx, 3)) ? 1 : 0); // slt v0, v0, v1
            if (GPR_U64(ctx, 2) == 0u)                                          // bnez v0, 0x12e590
                break;
            ctx->pc = kLoopHead;
            if (runtime->eeCheckpointDue(g_stepCycles))
                return; // the scheduler resumes at kLoopHead (loopHead below)
        }
        ctx->pc = kAfterLoop;
    }

    void waitFunction(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        // Entered anywhere but its start (the generated function can resume
        // at its labels): leave it to the generated code.
        if (ctx->pc != kWaitFunction && ctx->pc != 0u)
        {
            g_originalWait(rdram, ctx, runtime);
            return;
        }
        SET_GPR_S32(ctx, 29, (int32_t)ADD32(GPR_U32(ctx, 29), 4294967280)); // addiu sp, sp, -16
        WRITE64(ADD32(GPR_U32(ctx, 29), 0), GPR_U64(ctx, 31));              // sd ra, 0(sp)
        SET_GPR_S32(ctx, 2, (int32_t)READ32(ADD32(GPR_U32(ctx, 4), 12)));   // lw v0, 0xc(a0)
        SET_GPR_S32(ctx, 3, (int32_t)ADD32(GPR_U32(ctx, 2), 10));           // addiu v1, v0, 10
        spin(rdram, ctx, runtime);
    }

    void loopHead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        spin(rdram, ctx, runtime);
    }

    void apply(PS2Runtime &runtime)
    {
        if (const char *step = std::getenv("BEN10_DRAW_WAIT_STEP"); step && *step)
        {
            const int value = std::atoi(step);
            if (value >= 1 && value <= 65536)
                g_stepCycles = static_cast<uint32_t>(value);
        }
        g_originalWait = runtime.lookupFunction(kWaitFunction);
        if (!g_originalWait || !runtime.lookupFunction(kLoopHead))
        {
            std::fprintf(stderr, "[draw-wait] functions not found, override not installed\n");
            return;
        }
        runtime.replaceFunction(kWaitFunction, &waitFunction);
        runtime.replaceFunction(kLoopHead, &loopHead);
        std::fprintf(stderr, "[draw-wait] frame wait loop native, %u EE cycles per turn (BEN10_DRAW_WAIT_STEP)\n", g_stepCycles);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Frame wait loop with a longer time step", "SLUS_216.61", 0x00100008u, 0u, &ben10::draw_wait::apply);
