// spu2_memory.cpp: the port's native SPU2 sound RAM (see spu2_memory.h).

#include "spu2_memory.h"

#include <array>
#include <cstring>
#include <mutex>

namespace ben10::audio
{
    namespace
    {
        std::mutex g_mutex;
        // Static storage: 2 MB, zero at start like the console after reset.
        std::array<uint8_t, kSpu2RamSize> g_ram{};

        bool fits(uint32_t address, size_t size)
        {
            return static_cast<uint64_t>(address) + size <= kSpu2RamSize;
        }
    }

    bool spu2Write(uint32_t address, const uint8_t *data, size_t size)
    {
        if (!data || !fits(address, size))
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::memcpy(g_ram.data() + address, data, size);
        return true;
    }

    bool spu2Read(uint32_t address, uint8_t *data, size_t size)
    {
        if (!data || !fits(address, size))
            return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::memcpy(data, g_ram.data() + address, size);
        return true;
    }
}
