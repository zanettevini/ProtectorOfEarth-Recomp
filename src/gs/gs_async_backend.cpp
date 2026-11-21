// gs_async_backend.cpp: runs the GS rasteriser off the game thread, in row bands (findings/20 s.4, s.7).
//
// WHAT IT IS FOR
//   PS2Recomp's GS frontend hands every primitive to a raster backend; the
//   only one it has, GSCpuBackend, draws each pixel on the CPU, on the
//   thread that submitted it. In Ben 10 that is the game thread, so the
//   game, VU1 and the IOP all waited for every triangle (findings/19 s.5,
//   s.8). This backend queues the GS work and runs it on other threads:
//   one dispatcher, plus optional band threads that each draw a share of
//   the rows of the same picture.
//
// HOW IT HOOKS IN
//   GS::setRasterBackend() (a public PS2Recomp API) swaps the backend; a game
//   override installs this one when the ELF loads.
//   BEN10_GS_SYNC=1    keep the original synchronous backend (comparisons).
//   BEN10_GS_THREADS=N draw with N row bands (default 1: one GS thread).
//
// WHY THE PICTURE IS THE SAME
//   - Everything that writes or changes GS state is queued and run in
//     submission order: primitives, CLUT loads, image transfers
//     (BeginTransfer/UploadImage, data copied), TEXFLUSH.
//   - Everything that reads GS memory for the game or returns a result
//     first waits for the queue to drain: ReadVram, WriteVram,
//     SnapshotVram, ConsumeLocalToHostBytes (local-to-host transfers),
//     ClearFramebuffer (it returns whether it cleared), Reset, and Sync for
//     readback, local-to-host and reset.
//   - Not drained: Sync(Finish) (the FINISH event only sets a flag),
//     GetTransferSnapshot (debug fields) and presentation (Present,
//     Sync(Presentation)): the window copies the frame while drawing goes
//     on, as it did when the game thread drew. The picture shown can be up
//     to kMaxPending commands behind the game; the game's own view of GS
//     memory is not.
//   - Row bands (patch 0014, GSCpuBackend::SetRowBand): band k draws only
//     the rows y with ((y >> 4) % N) == k. Every band sees every primitive,
//     CLUT load and TEXFLUSH in the same order, so each pixel gets the same
//     writes in the same order as with one backend (ps2xTest case "GS CPU
//     backends drawing row bands..."). What a band must never do is read
//     GS memory that another band has not finished writing: a texture or
//     CLUT read from a page written by a primitive queued since the last
//     barrier ends the parallel segment first (all bands catch up). Pages
//     are 8 KB; the page sets are over-estimates (frame and Z rows rounded
//     to 32-row pages, the texture's whole page rectangle).
//   - One difference remains with N > 1: each band has its own copy of the
//     GS texture page buffer (GSCpuBackend's one-page TexturePageCache), so
//     a game that samples a page it rendered to without a TEXFLUSH may see
//     different stale data. Games must TEXFLUSH after rendering to a
//     texture; BEN10_GS_THREADS=1 avoids even that.
//   Nothing else reads GS memory: PS2Memory's VRAM buffer is only handed to
//   the GS at start-up (checked in the runtime sources).

#include "ps2_runtime.h"
#include "game_overrides.h"
#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_frontend.h"

#include <algorithm>
#include <bitset>
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

    // GS memory is 4 MB = 512 pages of 8 KB.
    constexpr uint32_t kPages = 512u;
    constexpr uint32_t kBlocksPerPage = 32u; // TBP0/CBP count 256-byte blocks
    using PageSet = std::bitset<kPages>;

    // Adds the rows y0..y1 of a buffer `widthUnits` x 64 pixels wide that
    // starts at `firstPage`. Pages are taken as 64x32 pixels, the smallest
    // page height of the formats a frame, Z buffer or texture can use
    // (32-bit); taller pages only make the set larger.
    void addRows(PageSet &set, uint32_t firstPage, uint32_t widthUnits, uint32_t y0, uint32_t y1)
    {
        const uint32_t width = std::max<uint32_t>(widthUnits, 1u);
        const uint32_t from = firstPage + (y0 / 32u) * width;
        const uint32_t to = firstPage + (y1 / 32u + 1u) * width; // exclusive
        if (to - from >= kPages)
        {
            set.set();
            return;
        }
        for (uint32_t page = from; page < to; ++page)
            set.set(page % kPages);
    }

    // Pages a primitive may write: its frame buffer and Z buffer rows
    // inside the scissor (FBP and ZBP count pages).
    PageSet writtenPages(const GSDrawState &state)
    {
        PageSet pages;
        const GSContext &ctx = state.context;
        addRows(pages, ctx.frame.fbp, ctx.frame.fbw, ctx.scissor.y0, ctx.scissor.y1);
        if (!ctx.zbuf.zmask)
            addRows(pages, ctx.zbuf.zbp, ctx.frame.fbw, ctx.scissor.y0, ctx.scissor.y1);
        return pages;
    }

    // Pages a textured primitive may read: the texture's rows (all 1024
    // when the vertical wrap mode is a region, which can reach beyond the
    // texture height), plus one page for a start that is not page-aligned.
    // GSCpuBackend reads only TEX0's base level (no mipmaps).
    PageSet texturePages(const GSDrawState &state)
    {
        PageSet pages;
        const GSContext &ctx = state.context;
        const uint32_t wrapV = static_cast<uint32_t>((ctx.clamp >> 2) & 3u);
        const uint32_t height = wrapV >= 2u ? 1024u : std::max<uint32_t>(state.textureHeight, 1u);
        const uint32_t firstPage = ctx.tex0.tbp0 / kBlocksPerPage;
        addRows(pages, firstPage, ctx.tex0.tbw, 0u, height - 1u);
        pages.set((firstPage + (height / 32u + 1u) * std::max<uint32_t>(ctx.tex0.tbw, 1u)) % kPages);
        return pages;
    }

    class AsyncBackend final : public GSRasterBackend
    {
    public:
        explicit AsyncBackend(uint32_t bandCount)
            : m_main(std::make_unique<GSCpuBackend>())
        {
            bandCount = std::clamp<uint32_t>(bandCount, 1u, 16u);
            for (uint32_t i = 0; i < bandCount; ++i)
            {
                m_bands.push_back(std::make_unique<GSCpuBackend>());
                m_bands.back()->SetRowBand(i, bandCount);
            }
            // Band 0 runs on the dispatcher thread; the others on their own.
            for (uint32_t i = 1; i < bandCount; ++i)
                m_bandThreads.emplace_back([this, i]()
                                           { bandLoop(i); });
            m_worker = std::thread([this]()
                                   { run(); });
        }

        ~AsyncBackend() override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_workAvailable.notify_one();
            m_worker.join();
            {
                std::lock_guard<std::mutex> lock(m_bandMutex);
                m_bandStop = true;
            }
            m_bandWork.notify_all();
            for (std::thread &thread : m_bandThreads)
                thread.join();
        }

        void Initialize(uint8_t *vram, uint32_t vramSize) override
        {
            drain();
            m_main->Initialize(vram, vramSize);
            for (auto &band : m_bands)
                band->Initialize(vram, vramSize);
        }

        void Reset() override
        {
            drain();
            m_main->Reset();
            for (auto &band : m_bands)
                band->Reset();
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
            if (reason != GSSyncReason::Finish && reason != GSSyncReason::Presentation)
                drain();
            m_main->Sync(reason);
        }

        // Presentation does not wait for the queue (see the header). Waiting
        // made the window thread spend its time blocked, and key presses
        // shorter than its loop were lost (findings/20 s.4).
        PresentationFrame Present(const GSPresentationRequest &request) override
        {
            return m_main->Present(request);
        }

        bool ClearFramebuffer(const GSContext &context, uint32_t rgba) override
        {
            drain();
            return m_main->ClearFramebuffer(context, rgba);
        }

        uint32_t ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes) override
        {
            drain();
            return m_main->ConsumeLocalToHostBytes(dst, maxBytes);
        }

        uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override
        {
            drain();
            return m_main->ReadVram(psm, base, bw, x, y);
        }

        void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override
        {
            drain();
            m_main->WriteVram(psm, base, bw, x, y, value);
        }

        void SnapshotVram(std::vector<uint8_t> &out) const override
        {
            drain();
            m_main->SnapshotVram(out);
        }

        // Debug-only fields (transfer direction and pixel count): no wait.
        GSTransferSnapshot GetTransferSnapshot() const override { return m_main->GetTransferSnapshot(); }

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

        void reportDone(uint64_t count)
        {
            if (count == 0u)
                return;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_completed += count;
            }
            m_drained.notify_all();
        }

        // Runs one command of a parallel segment on one band.
        static void runOnBand(GSCpuBackend &band, Command &command)
        {
            if (auto *submit = std::get_if<SubmitCommand>(&command))
                band.Submit(submit->batch);
            else if (auto *clut = std::get_if<LoadClutCommand>(&command))
                band.LoadClut(clut->tex0, clut->texclut);
            else if (std::holds_alternative<TextureFlushCommand>(command))
                band.TextureFlush();
        }

        // Every band draws the segment; returns when all are done.
        void runSegment(std::vector<Command *> &segment)
        {
            if (segment.empty())
                return;
            if (m_bands.size() == 1u)
            {
                for (Command *command : segment)
                    runOnBand(*m_bands[0], *command);
            }
            else
            {
                {
                    std::lock_guard<std::mutex> lock(m_bandMutex);
                    m_bandSegment = &segment;
                    m_bandPending = static_cast<uint32_t>(m_bands.size() - 1u);
                    ++m_bandGeneration;
                }
                m_bandWork.notify_all();
                for (Command *command : segment)
                    runOnBand(*m_bands[0], *command);
                std::unique_lock<std::mutex> lock(m_bandMutex);
                m_bandDone.wait(lock, [this]()
                                { return m_bandPending == 0u; });
                m_bandSegment = nullptr;
            }
            reportDone(segment.size());
            segment.clear();
            m_segmentWrites.reset();
        }

        void bandLoop(uint32_t index)
        {
            uint64_t seen = 0;
            for (;;)
            {
                std::vector<Command *> *segment = nullptr;
                {
                    std::unique_lock<std::mutex> lock(m_bandMutex);
                    m_bandWork.wait(lock, [this, seen]()
                                    { return m_bandStop || m_bandGeneration != seen; });
                    if (m_bandStop)
                        return;
                    seen = m_bandGeneration;
                    segment = m_bandSegment;
                }
                for (Command *command : *segment)
                    runOnBand(*m_bands[index], *command);
                bool last = false;
                {
                    std::lock_guard<std::mutex> lock(m_bandMutex);
                    last = --m_bandPending == 0u;
                }
                if (last)
                    m_bandDone.notify_one();
            }
        }

        // The dispatcher: splits the queue into parallel segments (draws,
        // CLUT loads, TEXFLUSH) and serial commands (transfers, run on the
        // main backend once every band has caught up).
        void run()
        {
            std::deque<Command> batch;
            std::vector<Command *> segment;
            const bool banded = m_bands.size() > 1u;
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
                    batch.swap(m_queue);
                }
                for (Command &command : batch)
                {
                    if (auto *submit = std::get_if<SubmitCommand>(&command))
                    {
                        const GSDrawState &state = submit->batch.state;
                        if (banded && state.prim.tme && (texturePages(state) & m_segmentWrites).any())
                            runSegment(segment); // the texture may be in an unfinished band
                        if (banded)
                            m_segmentWrites |= writtenPages(state);
                        segment.push_back(&command);
                    }
                    else if (auto *clut = std::get_if<LoadClutCommand>(&command))
                    {
                        if (banded && m_segmentWrites.test((clut->tex0.cbp / kBlocksPerPage) % kPages))
                            runSegment(segment);
                        segment.push_back(&command);
                    }
                    else if (std::holds_alternative<TextureFlushCommand>(command))
                    {
                        segment.push_back(&command);
                    }
                    else
                    {
                        runSegment(segment);
                        if (auto *transfer = std::get_if<BeginTransferCommand>(&command))
                            m_main->BeginTransfer(transfer->command);
                        else if (auto *image = std::get_if<UploadImageCommand>(&command))
                            m_main->UploadImage(image->data.data(), static_cast<uint32_t>(image->data.size()));
                        reportDone(1u);
                    }
                    // Long runs of draws: report progress so a producer held
                    // by the limit can go on.
                    if (segment.size() >= kSegmentLimit)
                        runSegment(segment);
                }
                runSegment(segment);
                batch.clear();
            }
        }

        // About half a frame's worth of the first level's primitives.
        static constexpr uint64_t kMaxPending = 8192;
        static constexpr size_t kSegmentLimit = 512;

        std::unique_ptr<GSCpuBackend> m_main;               // transfers, clears, readers
        std::vector<std::unique_ptr<GSCpuBackend>> m_bands; // drawing, one per row band
        PageSet m_segmentWrites;                            // pages written since the last barrier

        mutable std::mutex m_mutex;
        std::condition_variable m_workAvailable;
        mutable std::condition_variable m_drained;
        std::deque<Command> m_queue;
        uint64_t m_submitted = 0;
        uint64_t m_completed = 0;
        bool m_workerIdle = false;
        bool m_stop = false;

        std::mutex m_bandMutex;
        std::condition_variable m_bandWork;
        std::condition_variable m_bandDone;
        std::vector<Command *> *m_bandSegment = nullptr;
        uint64_t m_bandGeneration = 0;
        uint32_t m_bandPending = 0;
        bool m_bandStop = false;
        std::vector<std::thread> m_bandThreads;

        std::thread m_worker; // started last, in the constructor body
    };

    void apply(PS2Runtime &runtime)
    {
        if (const char *sync = std::getenv("BEN10_GS_SYNC"); sync && *sync && *sync != '0')
        {
            std::fprintf(stderr, "[gs] BEN10_GS_SYNC set: the rasteriser stays on the submitting thread\n");
            return;
        }
        uint32_t bands = 1u;
        if (const char *threads = std::getenv("BEN10_GS_THREADS"); threads && *threads)
            bands = static_cast<uint32_t>(std::clamp(std::atoi(threads), 1, 16));
        runtime.gs().setRasterBackend(std::make_unique<AsyncBackend>(bands));
        std::fprintf(stderr, "[gs] rasteriser off the game thread, %u row band(s) (BEN10_GS_THREADS; BEN10_GS_SYNC=1 turns it off)\n", bands);
    }
}

PS2_REGISTER_GAME_OVERRIDE("GS rasteriser off the game thread", "SLUS_216.61", 0x00100008u, 0u, &ben10::gs_async::apply);
