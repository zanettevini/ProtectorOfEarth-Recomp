// test_ee_memory_layout.cpp: where the runtime puts things in EE RAM.
//
// Why these tests exist (docs/findings/15 s.15, patch 0006):
//   The game's own newlib sbrk grows its heap until EndOfHeap. The runtime
//   used to stop the heap at 0x01F00000, keeping the top megabyte for its own
//   RPC/TLS pools and callback stacks, and handed out SIF command packets from
//   the start of the game's heap. Ben 10 needs its heap to reach 0x01F87000
//   while loading its first sound bank: with the old limit, memalign returned
//   NULL, a chunk-handler table was built at address 0 and the game ended in
//   std::terminate. Patch 0006 moves the runtime's guest data to EE kernel RAM
//   (0x00020000-0x0007FFFF, ps2_kernel_ram.h) and lets the heap reach the main
//   thread's stack.
//
// Where the expected values come from: the runtime's own definition of the
// main stack (SetupThread with stack = -1 returns sp; the stack occupies
// [sp - size, sp)) and the EE memory map (kernel RAM below 0x00080000, games
// linked at 0x00100000).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "ps2_kernel_ram.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"

namespace {

constexpr int V0 = 2, A0 = 4, A1 = 5, A2 = 6, A3 = 7, T0 = 8, SP = 29;

struct Env {
    std::vector<uint8_t> rdram = std::vector<uint8_t>(PS2_RAM_SIZE, 0);
    R5900Context ctx{};
    std::unique_ptr<PS2Runtime> runtime = std::make_unique<PS2Runtime>();

    uint32_t syscall(uint32_t number) {
        EXPECT_TRUE(ps2_syscalls::dispatchNumericSyscall(number, rdram.data(), &ctx, runtime.get()))
            << "syscall 0x" << std::hex << number;
        R5900Context *const c = &ctx;  // the GPR macros want a pointer variable
        return static_cast<uint32_t>(GPR_U32(c, V0));
    }
};

// The game's crt0 (0x001001A4): SetupThread(gp, stack = -1, size = 0x10000,
// args, root) then SetupHeap(_end = 0x00285FCC, size = -1).
TEST(EeMemoryLayout, HeapReachesTheMainStack) {
    Env env;
    SET_GPR_U32(&env.ctx, A0, 0x0025D3F0u);  // gp
    SET_GPR_U32(&env.ctx, A1, 0xFFFFFFFFu);  // stack = -1: top of RAM
    SET_GPR_U32(&env.ctx, A2, 0x00010000u);  // stack size
    SET_GPR_U32(&env.ctx, A3, 0x00271540u);  // args
    SET_GPR_U32(&env.ctx, T0, 0x00100280u);  // root function
    SET_GPR_U32(&env.ctx, SP, 0x0010FFF0u);
    const uint32_t sp = env.syscall(0x3C);  // SetupThread
    const uint32_t stackFloor = sp - 0x00010000u;

    SET_GPR_U32(&env.ctx, A0, 0x00285FCCu);  // heap base (_end)
    SET_GPR_U32(&env.ctx, A1, 0xFFFFFFFFu);  // size -1: the rest of RAM
    env.syscall(0x3D);                       // SetupHeap
    const uint32_t heapEnd = env.syscall(0x3E);  // EndOfHeap

    EXPECT_EQ(heapEnd, stackFloor) << "the heap ends where the main stack starts";
    EXPECT_GE(heapEnd, 0x01F87000u) << "Ben 10's first sound bank needs the heap up to here";
}

TEST(EeMemoryLayout, CallbackStacksAreInKernelRam) {
    Env env;
    const uint32_t top = env.runtime->reserveAsyncCallbackStack(0x4000u, 16u);
    ASSERT_NE(top, 0u);
    EXPECT_GE(top, ps2_kernel_ram::kCallbackStackFloor);
    EXPECT_LT(top, ps2_kernel_ram::kCallbackStackTop);
    EXPECT_LT(top, 0x00100000u) << "below the game's load address";
}

}  // namespace
