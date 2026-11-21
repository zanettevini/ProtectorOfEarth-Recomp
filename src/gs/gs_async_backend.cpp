// gs_async_backend.cpp: runs the GS rasteriser on its own thread (findings/20 s.4).
//
// WHAT IT IS FOR
//   PS2Recomp's GS frontend hands every primitive to a raster backend; the
//   only one it has, GSCpuBackend, draws each pixel on the CPU, on the
//   thread that submitted it. In Ben 10 that is the game thread, so the
//   game, VU1 and the IOP all wait for every triangle (findings/19 s.5, s.8:
//   the rasteriser was 80% of the game thread's time in the menus and 30%
//   in the first level). This backend wraps a GSCpuBackend and gives it a
//   thread of its own: drawing overlaps with the rest of the frame.
//
// HOW IT HOOKS IN
//   GS::setRasterBackend() (a public PS2Recomp API) swaps the backend; a
//   game override installs this one when the ELF loads. BEN10_GS_SYNC=1
//   keeps the original synchronous backend (for comparisons).
//
// WHY THE PICTURE IS THE SAME
//   - One worker thread runs every command in submission order, on the
//     same GSCpuBackend code: the VRAM contents after the same commands are
//     the same bytes.
//   - Everything that writes or changes GS state goes into the queue:
//     primitives, CLUT loads, image transfers (BeginTransfer/UploadImage,
//     the data is copied), TEXFLUSH.
//   - Everything that reads GS memory for the game or returns a result first
//     waits for the queue to drain, then runs the call: ReadVram, WriteVram,
//     SnapshotVram, ConsumeLocalToHostBytes (local-to-host transfers),
//     ClearFramebuffer (it returns whether it cleared), Reset, and Sync for
//     readback, local-to-host and reset.
//   - Not drained: Sync(Finish) (the GS FINISH event only sets a flag),
//     GetTransferSnapshot (debug fields only) and presentation (Present and
//     Sync(Presentation)): the window copies the frame between two commands,
//     as it did before. The picture shown can be up to kMaxPending commands
//     behind the game; the game's own view of GS memory is not.
//   Nothing else reads the GS memory: PS2Memory's VRAM buffer is only
//   handed to the GS at start-up (checked in the runtime sources).

#include "ps2_runtime.h"
#include "game_overrides.h"
#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"

#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace ben10::gs_async
{
    // One queued GS operation. The data each one needs is copied in.
    struct SubmitCommand
    {
        GSPrimitiveBatch batch;
    };
    struct LoadClutCommand
    {
        GSTex0Reg tex0;
        GSTexClutReg texclut;
    };
    struct BeginTransferCommand
    {
        GSTransferCommand command;
    };
    struct UploadImageCommand
    {
        std::vector<uint8_t> data;
    };
    struct TextureFlushCommand
    {
    };
    using Command = std::variant<SubmitCommand, LoadClutCommand, BeginTransferCommand,
                                 UploadImageCommand, TextureFlushCommand>;

    class AsyncBackend final : public GSRasterBackend
    {
    public:
        AsyncBackend()
            : m_inner(std::make_unique<GSCpuBackend>()),
              m_worker([this]()
                       { run(); })
        {
        }

        ~AsyncBackend() override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_workAvailable.notify_one();
            m_worker.join();
        }

        void Initialize(uint8_t *vram, uint32_t vramSize) override
        {
            drain();
            m_inner->Initialize(vram, vramSize);
        }

        void Reset() override
        {
            drain();
            m_inner->Reset();
        }

        void Submit(const GSPrimitiveBatch &batch) override { push(SubmitCommand{batch}); }
        void LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut) override { push(LoadClutCommand{tex0, texclut}); }
        void BeginTransfer(const GSTransferCommand &command) override { push(BeginTransferCommand{command}); }
        void UploadImage(const uint8_t *data, uint32_t sizeBytes) override
        {
            push(UploadImageCommand{std::vector<uint8_t>(data, data + sizeBytes)});
        }
        void TextureFlush() override { push(TextureFlushCommand{}); }

        // The CPU backend's Flush does nothing; the queue keeps running.
        void Flush() override {}

        void Sync(GSSyncReason reason) override
        {
            // FINISH only raises a flag in the frontend, and presentation
            // only copies the frame (see Present): neither waits. Readback,
            // local-to-host and reset read or replace GS memory: they wait.
            if (reason != GSSyncReason::Finish && reason != GSSyncReason::Presentation)
                drain();
            m_inner->Sync(reason);
        }

        // Presentation does not wait for the queue: GSCpuBackend copies the
        // frame under its own lock, so it sees GS memory between two
        // commands, as it did when the game thread drew (the window thread
        // never waited for the game thread then either). Waiting here made
        // the window thread spend its time blocked, and key presses shorter
        // than its loop were lost.
        PresentationFrame Present(const GSPresentationRequest &request) override
        {
            return m_inner->Present(request);
        }

        bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override
        {
            drain();
            return m_inner->ClearFramebuffer(context, rgba);
        }

        uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override
        {
            drain();
            return m_inner->ConsumeLocalToHostBytes(dst, maxBytes);
        }

        uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override
        {
            drain();
            return m_inner->ReadVram(psm, base, bw, x, y);
        }

        void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override
        {
            drain();
            m_inner->WriteVram(psm, base, bw, x, y, value);
        }

        void SnapshotVram(std::vector<uint8_t> &out) const override
        {
            drain();
            m_inner->SnapshotVram(out);
        }

        // Debug-only fields (transfer direction and pixel count): no wait.
        GSTransferSnapshot GetTransferSnapshot() const override { return m_inner->GetTransferSnapshot(); }

    private:
        void push(Command &&command)
        {
            bool wake = false;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // Back-pressure: the game may run ahead of the rasteriser
                // by at most kMaxPending commands. Without a limit the queue
                // grows without end whenever drawing is the slower side.
                if (m_submitted - m_completed >= kMaxPending)
                {
                    if (m_workerIdle)
                        m_workAvailable.notify_one();
                    m_drained.wait(lock, [this]()
                                   { return m_submitted - m_completed < kMaxPending; });
                }
                m_queue.push_back(std::move(command));
                ++m_submitted;
                wake = m_workerIdle;
            }
            if (wake)
                m_workAvailable.notify_one();
        }

        // Waits until every command queued before this call has run.
        void drain() const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const uint64_t target = m_submitted;
            m_drained.wait(lock, [this, target]()
                           { return m_completed >= target; });
        }

        void execute(Command &command)
        {
            if (auto *submit = std::get_if<SubmitCommand>(&command))
                m_inner->Submit(submit->batch);
            else if (auto *clut = std::get_if<LoadClutCommand>(&command))
                m_inner->LoadClut(clut->tex0, clut->texclut);
            else if (auto *transfer = std::get_if<BeginTransferCommand>(&command))
                m_inner->BeginTransfer(transfer->command);
            else if (auto *image = std::get_if<UploadImageCommand>(&command))
                m_inner->UploadImage(image->data.data(), static_cast<uint32_t>(image->data.size()));
            else if (std::holds_alternative<TextureFlushCommand>(command))
                m_inner->TextureFlush();
        }

        void run()
        {
            std::deque<Command> batch;
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_workerIdle = true;
                    m_workAvailable.wait(lock, [this]()
                                         { return m_stop || !m_queue.empty(); });
                    m_workerIdle = false;
                    if (m_queue.empty() && m_stop)
                        return;
                    // Take everything queued so far in one go.
                    batch.swap(m_queue);
                }
                // Report progress every kProgressStep commands, so a
                // producer held by the limit can go on before the whole
                // batch is done.
                uint64_t done = 0;
                for (Command &command : batch)
                {
                    execute(command);
                    if (++done == kProgressStep)
                    {
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_completed += done;
                        }
                        done = 0;
                        m_drained.notify_all();
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_completed += done;
                }
                batch.clear();
                m_drained.notify_all();
            }
        }

        // About a frame's worth of primitives of the first level.
        static constexpr uint64_t kMaxPending = 8192;
        static constexpr uint64_t kProgressStep = 256;

        std::unique_ptr<GSCpuBackend> m_inner;
        mutable std::mutex m_mutex;
        std::condition_variable m_workAvailable;
        mutable std::condition_variable m_drained;
        std::deque<Command> m_queue;
        uint64_t m_submitted = 0;
        uint64_t m_completed = 0;
        bool m_workerIdle = false;
        bool m_stop = false;
        std::thread m_worker; // last: starts after the members it uses exist
    };

    void apply(PS2Runtime &runtime)
    {
        if (const char *sync = std::getenv("BEN10_GS_SYNC"); sync && *sync && *sync != '0')
        {
            std::fprintf(stderr, "[gs] BEN10_GS_SYNC set: the rasteriser stays on the submitting thread\n");
            return;
        }
        runtime.gs().setRasterBackend(std::make_unique<AsyncBackend>());
        std::fprintf(stderr, "[gs] rasteriser on its own thread (set BEN10_GS_SYNC=1 to turn off)\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("GS rasteriser on its own thread", "SLUS_216.61", 0x00100008u, 0u, &ben10::gs_async::apply);
