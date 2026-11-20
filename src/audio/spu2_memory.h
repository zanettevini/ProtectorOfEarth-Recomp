// spu2_memory.h: the port's native model of the SPU2's 2 MB sound RAM.
//
// What it is for
//   The PS2 sound chip (SPU2) plays samples (ADPCM) that live in its own
//   2 MB RAM. The game fills that RAM through its IOP modules: CDARB
//   streams sound banks from the disc into it (destination kinds 2 and 4,
//   findings/17 s.5), and ALASD plays from it. PS2Recomp has no SPU2 RAM,
//   so this is where the port keeps those bytes. Today only the native
//   CDARB writes here; the native sound engine (ALASD replacement, D12)
//   will read from it.
//
// Addresses are SPU2 byte addresses, 0 to 0x1FFFFF.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ben10::audio
{
    constexpr uint32_t kSpu2RamSize = 2u * 1024u * 1024u;

    // Copies size bytes to SPU2 RAM at address. Returns false (and copies
    // nothing) when the range does not fit in the 2 MB.
    bool spu2Write(uint32_t address, const uint8_t *data, size_t size);

    // Copies size bytes from SPU2 RAM at address into data.
    bool spu2Read(uint32_t address, uint8_t *data, size_t size);
}
