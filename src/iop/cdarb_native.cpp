// cdarb_native.cpp: native replacement for the EE side of the game's disc
// streaming module, CDARB (decision D12, docs/findings/17).
//
// What the original does
//   The game's IOP module CDARB.IRX serves disc reads for the EE over RPC
//   (SID 0x01212122). Two small EE functions talk to it:
//     0x0012C1F8 queue a read: RPC function 2 with 8 words (findings/17 s.5)
//     0x0012C2C8 ask whether the disc tray is open: RPC function 7
//   When a request is done, CDARB sends the EE SIF command 0 with a 0x18-byte
//   packet; the game's handler 0x0012C040 then calls
//   callback(packet+0x0C = result, packet+0x14 = argument).
//
// What this file does instead
//   It replaces those two EE functions with native code. The read is done
//   right away on the PC, straight from game/GAME.WAD into EE memory, and
//   the completion is delivered exactly as CDARB delivers it: SIF command 0
//   with the same packet, so the game's own handler runs. No RPC, no IOP
//   code: the interpreted CDARB module is no longer used for the EE's reads
//   (it still serves ALASD's audio requests until ALASD is native too).
//
// Request layout (the EE's "params" struct, a2 of 0x0012C1F8, findings/17 s.5)
//   +0x00 start sector (absolute disc LSN, in the numbering the game got from
//         sceCdSearchFile("\GAME.WAD;1"))
//   +0x04 sector count (positive)
//   +0x08 destination address (EE)
//   +0x0C written here: the request id (the RPC reply word)
//   +0x10 destination kind (CDARB .text+0x1804): 0 = EE memory, 2 and 4 =
//         SPU2 sound RAM (the EE uses 2 to load sound banks), 8 = IOP memory
//         (only ALASD uses it, through CDArb:6, not this path)
//   The callback (a3) and the params pointer itself travel back to the EE in
//   the completion packet (+0x10 and +0x14).
//
// LSN to file: the game keeps the GAME.WAD start LSN at 0x002662E8
// (findings/16 s.4), so the byte offset in GAME.WAD is
// (lsn - that LSN) * 2048. This does not depend on how the runtime numbers
// its virtual disc.

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "game_overrides.h"
#include "../audio/spu2_memory.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

// Delivers an IOP-to-EE SIF command to the EE handler the game registered,
// as the runtime does for interpreted IOP modules. Declared in the runtime's
// internal header src/lib/Kernel/Stubs/SIF.h (namespace ps2_stubs).
namespace ps2_stubs
{
    bool dispatchSifCommand(uint8_t *rdram, PS2Runtime *runtime, uint32_t commandId,
                            const void *packet, size_t packetSize) noexcept;
}

namespace
{
    constexpr uint32_t kSectorSize = 2048u;
    // Game variable: GAME.WAD start LSN, copied from the sceCdlFILE that
    // sceCdSearchFile filled (FUN_0012b7a0, findings/16 s.4).
    constexpr uint32_t kGameWadLsnAddress = 0x002662E8u;
    // SIF command CDARB sends on completion: the number at its .bss+0x2370,
    // which is 0 (never written), and the EE registers command 0 in
    // FUN_0012c068 with handler 0x0012C040 (findings/17 s.5).
    constexpr uint32_t kCompletionCommand = 0u;
    // Destination kinds of a request (CDARB .text+0x1804).
    constexpr uint32_t kDestEe = 0u;
    constexpr uint32_t kDestSpu2 = 2u;     // sceSdVoiceTrans, channel 1
    constexpr uint32_t kDestSpu2Alt = 4u;  // same transfer, another check path

    std::mutex g_mutex;
    // Set BEN10_TRACE_CDARB=1 to log every request (findings/17 s.7).
    const bool g_trace = std::getenv("BEN10_TRACE_CDARB") != nullptr;
    std::ifstream g_wad;
    uint32_t g_nextRequestId = 1u;

    // Copies into EE RAM through the runtime's own accessor (Ps2FastWrite8,
    // which masks the address to the 32 MB physical range). Hardware and
    // scratchpad addresses are refused: a disc read never targets them.
    bool writeEe(uint8_t *rdram, uint32_t address, const uint8_t *data, size_t size)
    {
        if (PS2Runtime::isSpecialAddress(address) ||
            static_cast<uint64_t>(address & PS2_RAM_MASK) + size > PS2_RAM_SIZE)
            return false;
        for (size_t i = 0; i < size; ++i)
            Ps2FastWrite8(rdram, address + static_cast<uint32_t>(i), data[i]);
        return true;
    }

    bool readWad(uint64_t offset, uint8_t *data, size_t size)
    {
        if (!g_wad.is_open())
        {
            const auto path = PS2Runtime::getIoPaths().cdRoot / "GAME.WAD";
            g_wad.open(path, std::ios::binary);
            if (!g_wad.is_open())
            {
                std::fprintf(stderr, "[CDARB:native] cannot open %s\n", path.string().c_str());
                return false;
            }
        }
        g_wad.clear();
        g_wad.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        g_wad.read(reinterpret_cast<char *>(data), static_cast<std::streamsize>(size));
        return static_cast<size_t>(g_wad.gcount()) == size;
    }

    // Sends the completion exactly as CDARB does: a 0x18-byte SIF command
    // packet (+0x00..+0x0B header, filled by the runtime) with
    // +0x0C result, +0x10 EE callback, +0x14 its argument.
    void complete(uint8_t *rdram, PS2Runtime *runtime, uint32_t result, uint32_t callback, uint32_t argument)
    {
        std::array<uint32_t, 6> packet{};
        packet[3] = result;
        packet[4] = callback;
        packet[5] = argument;
        if (!ps2_stubs::dispatchSifCommand(rdram, runtime, kCompletionCommand, packet.data(), sizeof(packet)))
            std::fprintf(stderr, "[CDARB:native] completion not delivered (no EE handler for command %u)\n",
                         kCompletionCommand);
    }

    // Replaces FUN_0012c1f8. a1 = file handle word (unused in disc mode),
    // a2 = params struct (EE), a3 = EE completion callback.
    // Returns 1 when the request was accepted, 0 otherwise (as the original:
    // "reply word != 0").
    void nativeQueueRead(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t handle = getRegU32(ctx, 5);
        const uint32_t params = getRegU32(ctx, 6);
        const uint32_t callback = getRegU32(ctx, 7);
        if (handle == 0xFFFFFFFFu)
        {
            // The original returns 0 without calling the IOP (0x0012C218).
            setReturnS32(ctx, 0);
            return;
        }

        const uint32_t lsn = READ32(params + 0x00u);
        const int32_t count = static_cast<int32_t>(READ32(params + 0x04u));
        const uint32_t destination = READ32(params + 0x08u);
        const uint32_t kind = READ32(params + 0x10u);
        const uint32_t wadLsn = READ32(kGameWadLsnAddress);

        uint32_t requestId = 0u;
        uint32_t result = 0u;
        if (g_trace)
            std::fprintf(stderr, "[CDARB:native] read handle=0x%X lsn=0x%X (wad sector %d) count=%d dest=0x%08X kind=%u callback=0x%08X\n",
                         handle, lsn, static_cast<int>(lsn - wadLsn), count, destination, kind, callback);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            requestId = g_nextRequestId++;
            if (g_nextRequestId == 0u)
                g_nextRequestId = 1u;

            const bool knownKind = kind == kDestEe || kind == kDestSpu2 || kind == kDestSpu2Alt;
            if (count <= 0 || !knownKind || lsn < wadLsn)
            {
                // Not seen from the EE so far; CDARB has a different path for
                // counts <= 0 (findings/17 s.7). Report it rather than guess.
                std::fprintf(stderr,
                             "[CDARB:native] unsupported request lsn=0x%X count=%d dest=0x%08X kind=%u wad=0x%X\n",
                             lsn, count, destination, kind, wadLsn);
            }
            else
            {
                const size_t bytes = static_cast<size_t>(count) * kSectorSize;
                std::vector<uint8_t> data(bytes);
                const uint64_t offset = static_cast<uint64_t>(lsn - wadLsn) * kSectorSize;
                bool ok = readWad(offset, data.data(), bytes);
                if (ok)
                    ok = kind == kDestEe ? writeEe(rdram, destination, data.data(), bytes)
                                         : ben10::audio::spu2Write(destination, data.data(), bytes);
                if (ok)
                    result = static_cast<uint32_t>(bytes); // CDARB reports the bytes moved (+0x18)
                else
                    std::fprintf(stderr, "[CDARB:native] read failed lsn=0x%X count=%d dest=0x%08X\n",
                                 lsn, count, destination);
            }
        }

        // The original stores the RPC reply (the request id) in params+0x0C
        // before any completion can arrive.
        WRITE32(params + 0x0Cu, requestId);
        complete(rdram, runtime, result, callback, params);
        setReturnS32(ctx, 1);
    }

    // Replaces FUN_0012c2c8 (called by ass_CheckCDDoorOpen). a1 = EE callback.
    // CDARB answers with 1 when sceCdStatus() == 1 (tray open), else 0. There
    // is no tray on the PC: always closed.
    void nativeCheckDoor(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t callback = getRegU32(ctx, 5);
        const uint32_t argument = getRegU32(ctx, 6); // second RPC word, 0 in the game
        complete(rdram, runtime, 0u, callback, argument);
        setReturnS32(ctx, 1);
    }

    void apply(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x0012C1F8u, &nativeQueueRead);
        runtime.replaceFunction(0x0012C2C8u, &nativeCheckDoor);
    }
}

PS2_REGISTER_GAME_OVERRIDE("CDARB native EE reads", "SLUS_216.61", 0x00100008u, 0u, &apply);
