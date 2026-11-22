// gs_record.h: recording the GS command stream for offline replay (findings/20 s.21).
//
// WHAT IT IS FOR
//   Changes to the GS backends (row bands, barriers, the rasteriser itself)
//   must not change the picture. The game's frames are not reproducible run
//   to run (thread timing), so instead the backend's input is recorded once:
//   every call the GS frontend makes on the raster backend, in order, after
//   a copy of GS memory. tools/gs_replay feeds the same calls to any backend
//   and prints a hash of GS memory at regular points, so two backends (or
//   two versions of one) can be compared exactly, and timed on the same
//   work.
//
// HOW IT HOOKS IN
//   gs_async_install.cpp wraps the game's backend in a RecordingBackend when
//   BEN10_GS_RECORD=<file> is set:
//     BEN10_GS_RECORD_AFTER=S    start S seconds after the backend is made
//                                (default 0)
//     BEN10_GS_RECORD_SECONDS=S  record for S seconds (default 10)
//   A recording holds the game's textures and frames: it is game data and
//   stays in user/, never in the repository (AGENTS.md s.2).
//
// FORMAT (version 1; native byte order, and the structs are written as raw
//   bytes, so a file is only read by a build of the same compiler and
//   target as the one that wrote it)
//   header: "B10GSREC", u32 version, u32 size of GS memory, GS memory
//   then records, each one tag byte (Tag) and its arguments:
//     Submit         GSPrimitiveBatch
//     LoadClut       GSTex0Reg, GSTexClutReg
//     BeginTransfer  GSTransferCommand
//     UploadImage    u32 byte count, the bytes
//     TextureFlush   -
//     Clear          GSContext, u32 rgba (ClearFramebuffer)
//     WriteVram      u32 psm, base, bw, x, y, value
//     Reset          -
//     Sync           u8 GSSyncReason
//     End            - (last record)
//   Reads that return data (ReadVram, ConsumeLocalToHostBytes, snapshots)
//   change nothing and are not recorded.

#pragma once

#include "runtime/gs/gs_backend.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace ben10::gs_record
{
    constexpr char kMagic[8] = {'B', '1', '0', 'G', 'S', 'R', 'E', 'C'};
    constexpr uint32_t kVersion = 1u;

    enum class Tag : uint8_t
    {
        Submit = 1,
        LoadClut,
        BeginTransfer,
        UploadImage,
        TextureFlush,
        Clear,
        WriteVram,
        Reset,
        Sync,
        End,
    };

    // Raw-byte records need structs without pointers or padding traps.
    static_assert(std::is_trivially_copyable_v<GSPrimitiveBatch>);
    static_assert(std::is_trivially_copyable_v<GSTex0Reg>);
    static_assert(std::is_trivially_copyable_v<GSTexClutReg>);
    static_assert(std::is_trivially_copyable_v<GSTransferCommand>);
    static_assert(std::is_trivially_copyable_v<GSContext>);

    // Forwards every call to `inner`; while recording, also writes it to
    // the file. The frontend already calls a backend from one thread at a
    // time (its state mutex); the recorder has its own lock anyway.
    class RecordingBackend final : public GSRasterBackend
    {
    public:
        RecordingBackend(std::unique_ptr<GSRasterBackend> inner, std::string path,
                         double startSeconds, double lengthSeconds);
        ~RecordingBackend() override;

        void Initialize(uint8_t *vram, uint32_t vramSize) override;
        void Reset() override;
        void Submit(const GSPrimitiveBatch &batch) override;
        void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override;
        void BeginTransfer(const GSTransferCommand &command) override;
        void UploadImage(const uint8_t *data, uint32_t sizeBytes) override;
        void Flush() override { m_inner->Flush(); }
        void TextureFlush() override;
        void Sync(GSSyncReason reason) override;
        PresentationFrame Present(const GSPresentationRequest &request) override { return m_inner->Present(request); }
        bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override;
        uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override
        {
            return m_inner->ConsumeLocalToHostBytes(dst, maxBytes);
        }
        uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override
        {
            return m_inner->ReadVram(psm, base, bw, x, y);
        }
        void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
        void SnapshotVram(std::vector<uint8_t> &out) const override { m_inner->SnapshotVram(out); }
        GSTransferSnapshot GetTransferSnapshot() const override { return m_inner->GetTransferSnapshot(); }

    private:
        // Opens the file and writes the header when the start time has come;
        // closes it when the length has passed. Returns whether to record
        // the call being made. Called with m_mutex held.
        bool active();
        void put(const void *data, size_t size);
        template <typename T>
        void put(const T &value) { put(&value, sizeof(T)); }
        void tag(Tag t) { put(static_cast<uint8_t>(t)); }
        void finish();

        std::unique_ptr<GSRasterBackend> m_inner;
        std::string m_path;
        double m_startSeconds;
        double m_lengthSeconds;
        double m_createdAt;
        double m_startedAt = 0.0;
        bool m_done = false;
        uint64_t m_records = 0;
        std::FILE *m_file = nullptr;
        std::mutex m_mutex;
    };
}
