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
//   override in gs_async_install.cpp installs this one when the ELF loads.
//   This file only needs the GS backend headers, so the offline replay
//   tool (tools/gs_replay) builds it too.
//   BEN10_GS_SYNC=1    keep the original synchronous backend (comparisons).
//   BEN10_GS_THREADS=N draw with N row bands (default: 6, or fewer on a
//   machine with fewer than 8 hardware threads; 1 below 4).
//
// WHERE THE TIME GOES (the [gs-async] log line, every 10 s of drawing)
//   To tell whether the rasteriser is what holds the rest of the port
//   (findings/20 s.21), the dispatcher counts, per 10 seconds:
//   - draws (primitive batches) and the parallel segments they ran in, and
//     why each segment ended: a texture or CLUT read from a page written
//     in the segment ("texture", "CLUT"), a transfer that runs on the
//     main backend ("transfer"), the length limit ("length"), or the queue
//     running empty ("idle");
//   - how long band 0 (the dispatcher's own band) drew, and how long it
//     then waited for the slowest other band (the cost of bands that
//     finish at different times);
//   - how often and how long producers (the game thread, the VIF1 worker)
//     were held by the queue limit, and how long readers waited in
//     drain() (clears, VRAM reads, resets, readback syncs).
//   The counters cost two clock reads per segment.
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
//   - A row of pixels is one band's only while every draw of the segment
//     sees that memory through the same layout (base page, FBW, pixel
//     format): a draw whose frame or Z pages the segment already used
//     through another layout (a Z buffer redrawn as a colour buffer; the
//     PSMZ and PSMCT block orders differ) first lets every band finish
//     (layoutConflict, findings/20 s.21).
//   - Image transfers run on the main backend on the dispatcher thread,
//     while no band is drawing. One that would write pages a draw or CLUT
//     load of the unfinished segment reads or writes first lets every band
//     finish; one that touches none of them (the usual texture upload into
//     a free area) runs at once, ahead of those draws, which do not see
//     its pages either way (findings/20 s.21). Local-to-host and
//     local-to-local transfers always wait.
//   - One difference remains with N > 1: each band has its own copy of the
//     GS texture page buffer (GSCpuBackend's one-page TexturePageCache), so
//     a game that samples a page it rendered to without a TEXFLUSH may see
//     different stale data. Games must TEXFLUSH after rendering to a
//     texture; BEN10_GS_THREADS=1 avoids even that.
//   Nothing else reads GS memory: PS2Memory's VRAM buffer is only handed to
//   the GS at start-up (checked in the runtime sources).

#include "gs_async_backend.h"

#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <algorithm>
#include <bitset>
#include <chrono>
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
    // inside the scissor (FBP and ZBP count pages). The first row is
    // rounded down to a multiple of 64: addRows counts 32-row pages, and
    // for a 16-bit frame or Z buffer (64x64-pixel pages) starting from
    // row y0 / 32 would skip the page that holds rows 32-63 when the
    // scissor starts there (findings/20 s.21).
    // The frame and Z sets are kept apart for the layout rule below.
    struct WriteSets
    {
        PageSet frame;   // frame buffer rows in the scissor
        PageSet z;       // Z buffer rows in the scissor (whether or not Z is written)
        PageSet written; // frame, plus Z unless ZMSK is set
    };
    WriteSets writtenPages(const GSDrawState &state)
    {
        WriteSets sets;
        const GSContext &ctx = state.context;
        const uint32_t y0 = static_cast<uint32_t>(ctx.scissor.y0) & ~63u;
        addRows(sets.frame, ctx.frame.fbp, ctx.frame.fbw, y0, ctx.scissor.y1);
        addRows(sets.z, ctx.zbuf.zbp, ctx.frame.fbw, y0, ctx.scissor.y1); // Z uses FRAME's FBW
        sets.written = sets.frame;
        if (!ctx.zbuf.zmask)
            sets.written |= sets.z;
        return sets;
    }

    // Whether a primitive touches its Z buffer at all: it writes Z (ZMSK
    // clear) or tests it (TEST.ZTE, bit 16, with ZTST, bits 17-18, GEQUAL
    // or GREATER; ALWAYS and NEVER read nothing).
    bool touchesZ(const GSDrawState &state)
    {
        const GSContext &ctx = state.context;
        if (!ctx.zbuf.zmask)
            return true;
        return ((ctx.test >> 16) & 1u) != 0u && ((ctx.test >> 17) & 3u) >= 2u;
    }

    // The memory layout of a frame or Z buffer: which pixel (x, y) each
    // byte belongs to depends on the base page, the width (FBW) and the
    // pixel format, whose swizzle differs between colour and Z formats even
    // at the same depth. Two draws with the same layout put a pixel row in
    // the same band; two with different layouts may not (findings/20 s.21).
    uint32_t bufferLayout(uint32_t basePage, uint32_t fbw, uint32_t psm)
    {
        return (basePage & 0xFFFu) | ((fbw & 0xFFu) << 12) | ((psm & 0xFFu) << 20);
    }

    // Pages a CLUT load reads: the page of CBP, and the next one when the
    // 1 KB of a 256-entry 32-bit CLUT (4 blocks) can run past its end.
    PageSet clutPages(const GSTex0Reg &tex0)
    {
        PageSet pages;
        const uint32_t cbp = static_cast<uint32_t>(tex0.cbp);
        pages.set((cbp / kBlocksPerPage) % kPages);
        if (cbp % kBlocksPerPage > kBlocksPerPage - 4u)
            pages.set((cbp / kBlocksPerPage + 1u) % kPages);
        return pages;
    }

    // Pages a host-to-local image transfer writes (BITBLTBUF DBP/DBW/DPSM,
    // TRXPOS DSAX/DSAY, TRXREG RRW/RRH). Page sizes by pixel format (GS
    // manual, "Page" tables): 64x32 for the 32-bit layouts (PSMCT32,
    // PSMCT24, PSMT8H, PSMT4HL, PSMT4HH, PSMZ32, PSMZ24), 64x64 for the
    // 16-bit ones, 128x64 for PSMT8, 128x128 for PSMT4. The set runs from
    // the first page of the first row of pages to the last page column
    // the rectangle reaches in its last row of pages, plus one page when
    // DBP is not page-aligned. Any other format, or a rectangle that
    // wraps past row 2047, gives every page (the transfer then waits for
    // all bands, as every transfer did before).
    PageSet transferWritePages(const GSTransferCommand &command)
    {
        PageSet pages;
        uint32_t pageW = 0, pageH = 0;
        switch (command.bitbltbuf.dpsm)
        {
        case 0x00: case 0x01: case 0x1B: case 0x24: case 0x2C: case 0x30: case 0x31:
            pageW = 64u; pageH = 32u; break;
        case 0x02: case 0x0A: case 0x32: case 0x3A:
            pageW = 64u; pageH = 64u; break;
        case 0x13:
            pageW = 128u; pageH = 64u; break;
        case 0x14:
            pageW = 128u; pageH = 128u; break;
        default:
            pages.set();
            return pages;
        }
        const uint32_t rrw = command.trxreg.rrw, rrh = command.trxreg.rrh;
        if (rrw == 0u || rrh == 0u)
            return pages; // nothing is written
        const uint32_t x0 = command.trxpos.dsax, y0 = command.trxpos.dsay;
        const uint32_t x1 = x0 + rrw - 1u, y1 = y0 + rrh - 1u;
        if (y1 >= 2048u)
        {
            pages.set();
            return pages;
        }
        const uint32_t widthPixels = std::max<uint32_t>(command.bitbltbuf.dbw, 1u) * 64u;
        const uint32_t perRow = (widthPixels + pageW - 1u) / pageW; // pages in one row of pages
        const uint32_t firstPage = command.bitbltbuf.dbp / kBlocksPerPage;
        const uint32_t from = firstPage + (y0 / pageH) * perRow;
        uint32_t to = firstPage + (y1 / pageH) * perRow + x1 / pageW + 1u; // exclusive
        if (command.bitbltbuf.dbp % kBlocksPerPage != 0u)
            ++to;
        if (to - from >= kPages)
        {
            pages.set();
            return pages;
        }
        for (uint32_t page = from; page < to; ++page)
            pages.set(page % kPages);
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

    using Clock = std::chrono::steady_clock;

    // Why a parallel segment ended (the [gs-async] counts).
    enum class Barrier : size_t
    {
        Texture,  // a texture read from a page written in the segment
        Clut,     // a CLUT load from such a page
        Transfer, // an image transfer, run on the main backend
        Length,   // kSegmentLimit draws
        Idle,     // the queue ran empty
        Layout,   // frame or Z memory used through two layouts (layoutConflict)
    };

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
                    const auto start = Clock::now();
                    m_drained.wait(lock, [this]()
                                   { return m_submitted - m_completed < kMaxPending; });
                    ++m_heldCount; // under m_mutex, read by the dispatcher's report
                    m_heldTime += Clock::now() - start;
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
            if (m_completed >= target)
                return;
            const auto start = Clock::now();
            m_drained.wait(lock, [this, target]()
                           { return m_completed >= target; });
            ++m_drainCount; // under m_mutex, like the producer counters
            m_drainTime += Clock::now() - start;
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
        void runSegment(std::vector<Command *> &segment, Barrier reason)
        {
            if (segment.empty())
                return;
            ++m_stats.segments;
            ++m_stats.barriers[static_cast<size_t>(reason)];
            const auto start = Clock::now();
            if (m_bands.size() == 1u)
            {
                for (Command *command : segment)
                    runOnBand(*m_bands[0], *command);
                m_stats.band0 += Clock::now() - start;
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
                const auto drawn = Clock::now();
                m_stats.band0 += drawn - start;
                std::unique_lock<std::mutex> lock(m_bandMutex);
                m_bandDone.wait(lock, [this]()
                                { return m_bandPending == 0u; });
                m_bandSegment = nullptr;
                m_stats.otherBands += Clock::now() - drawn;
            }
            reportDone(segment.size());
            segment.clear();
            m_segmentWrites.reset();
            m_segmentReads.reset();
            m_segmentLayouts.clear();
            m_layoutMemoValid = false;
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
                        ++m_stats.draws;
                        if (banded && state.prim.tme)
                        {
                            const PageSet &texture = cachedTexturePages(state);
                            if ((texture & m_segmentWrites).any())
                                runSegment(segment, Barrier::Texture); // the texture may be in an unfinished band
                            m_segmentReads |= texture;
                        }
                        if (banded)
                        {
                            const WriteSets &sets = cachedWrittenPages(state);
                            if (layoutConflict(state, sets))
                                runSegment(segment, Barrier::Layout);
                            recordLayouts(state, sets);
                            m_segmentWrites |= sets.written;
                        }
                        segment.push_back(&command);
                    }
                    else if (auto *clut = std::get_if<LoadClutCommand>(&command))
                    {
                        if (banded)
                        {
                            const PageSet pages = clutPages(clut->tex0);
                            if ((pages & m_segmentWrites).any())
                                runSegment(segment, Barrier::Clut);
                            m_segmentReads |= pages;
                        }
                        segment.push_back(&command);
                    }
                    else if (std::holds_alternative<TextureFlushCommand>(command))
                    {
                        segment.push_back(&command);
                    }
                    else
                    {
                        // Transfers run on the main backend, on this thread,
                        // while the bands are idle (a segment only runs inside
                        // runSegment). A host-to-local transfer whose pages
                        // no queued draw or CLUT load of the segment reads or
                        // writes can run at once, ahead of those draws: they
                        // see the same memory either way, and the draws after
                        // it see its data. Anything else (local-to-host,
                        // local-to-local, pages in use) first lets every band
                        // finish the segment, as all transfers used to.
                        auto *transfer = std::get_if<BeginTransferCommand>(&command);
                        if (transfer)
                        {
                            m_transferPages = transfer->command.direction == 0u ? transferWritePages(transfer->command)
                                              : transfer->command.direction == 3u ? PageSet{}
                                                                                  : PageSet{}.set();
                        }
                        if (!banded || ((m_segmentWrites | m_segmentReads) & m_transferPages).any())
                            runSegment(segment, Barrier::Transfer);
                        else
                            ++m_stats.transfersAhead;
                        if (transfer)
                            m_main->BeginTransfer(transfer->command);
                        else if (auto *image = std::get_if<UploadImageCommand>(&command))
                            m_main->UploadImage(image->data.data(), static_cast<uint32_t>(image->data.size()));
                        reportDone(1u);
                    }
                    // Long runs of draws: report progress so a producer held
                    // by the limit can go on.
                    if (segment.size() >= kSegmentLimit)
                        runSegment(segment, Barrier::Length);
                }
                runSegment(segment, Barrier::Idle);
                batch.clear();
                reportIfDue();
            }
        }

        // The band rule (each pixel row drawn by one band) keeps every byte
        // of GS memory with one band only while all the segment's draws see
        // that memory through the same layout (bufferLayout). The game also
        // uses one area as a colour buffer and as a Z buffer in the same
        // stretch of draws (seen in the first level: sprites drawn with
        // FRAME at the page the triangles before them use as their 24-bit
        // Z buffer). PSMZ swizzles blocks differently from PSMCT, so rows
        // 0-15 of one are rows 16-31 of the other: two bands drew the same
        // bytes, in an order that changed from run to run (findings/20
        // s.21). So a draw whose frame or Z pages were already used in the
        // segment through another layout first lets the bands finish.
        // layoutConflict says whether this draw needs that barrier;
        // recordLayouts then adds its pages to the segment's records
        // (runSegment clears them).
        bool layoutConflict(const GSDrawState &state, const WriteSets &sets)
        {
            const GSContext &ctx = state.context;
            const uint32_t frameLayout = bufferLayout(ctx.frame.fbp, ctx.frame.fbw, ctx.frame.psm);
            const bool z = touchesZ(state);
            const uint32_t zLayout = z ? bufferLayout(ctx.zbuf.zbp, ctx.frame.fbw, ctx.zbuf.psm) : frameLayout;
            // Same layouts and pages as the previous draw of the segment:
            // already recorded and checked.
            if (m_layoutMemoValid && m_layoutMemoVersion == m_writeSetsVersion &&
                m_layoutMemoFrame == frameLayout && m_layoutMemoZ == zLayout && m_layoutMemoTouchesZ == z)
                return false;
            for (const SegmentLayout &used : m_segmentLayouts)
            {
                if ((used.layout != frameLayout && (used.pages & sets.frame).any()) ||
                    (z && used.layout != zLayout && (used.pages & sets.z).any()))
                    return true;
            }
            return false;
        }
        void recordLayouts(const GSDrawState &state, const WriteSets &sets)
        {
            const GSContext &ctx = state.context;
            const uint32_t frameLayout = bufferLayout(ctx.frame.fbp, ctx.frame.fbw, ctx.frame.psm);
            const bool z = touchesZ(state);
            const uint32_t zLayout = z ? bufferLayout(ctx.zbuf.zbp, ctx.frame.fbw, ctx.zbuf.psm) : frameLayout;
            if (m_layoutMemoValid && m_layoutMemoVersion == m_writeSetsVersion &&
                m_layoutMemoFrame == frameLayout && m_layoutMemoZ == zLayout && m_layoutMemoTouchesZ == z)
                return; // the previous draw recorded the same pages
            recordLayout(frameLayout, sets.frame);
            if (z)
                recordLayout(zLayout, sets.z);
            m_layoutMemoValid = true;
            m_layoutMemoVersion = m_writeSetsVersion;
            m_layoutMemoFrame = frameLayout;
            m_layoutMemoZ = zLayout;
            m_layoutMemoTouchesZ = z;
        }
        void recordLayout(uint32_t layout, const PageSet &pages)
        {
            for (SegmentLayout &used : m_segmentLayouts)
            {
                if (used.layout == layout)
                {
                    used.pages |= pages;
                    return;
                }
            }
            m_segmentLayouts.push_back({layout, pages});
        }
        struct SegmentLayout
        {
            uint32_t layout;
            PageSet pages;
        };
        std::vector<SegmentLayout> m_segmentLayouts; // frame/Z layouts used since the last barrier
        bool m_layoutMemoValid = false;
        uint64_t m_layoutMemoVersion = 0;
        uint32_t m_layoutMemoFrame = 0, m_layoutMemoZ = 0;
        bool m_layoutMemoTouchesZ = false;

        // Prints the [gs-async] line (see the header) once 10 seconds have
        // passed since the last one, then starts the counts again. Called by
        // the dispatcher between batches, so the log has no line while
        // nothing is drawn.
        void reportIfDue()
        {
            const auto now = Clock::now();
            if (m_statsStart == Clock::time_point{})
                m_statsStart = now;
            const auto elapsed = now - m_statsStart;
            if (elapsed < std::chrono::seconds(10))
                return;
            uint64_t held = 0, drains = 0;
            Clock::duration heldTime{}, drainTime{};
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                held = m_heldCount;
                drains = m_drainCount;
                heldTime = m_heldTime;
                drainTime = m_drainTime;
                m_heldCount = m_drainCount = 0;
                m_heldTime = m_drainTime = {};
            }
            const auto seconds = [](Clock::duration d)
            { return std::chrono::duration<double>(d).count(); };
            const auto &b = m_stats.barriers;
            std::fprintf(stderr,
                         "[gs-async] last %.1f s: %llu draws in %llu segments (ended by texture %llu, CLUT %llu, "
                         "transfer %llu, length %llu, idle %llu, layout %llu; %llu transfer commands ran ahead of the segment); band 0 drew %.2f s and waited %.2f s for the "
                         "other bands; producers held by the queue limit %llu times (%.2f s), drains %llu (%.2f s)\n",
                         seconds(elapsed), static_cast<unsigned long long>(m_stats.draws),
                         static_cast<unsigned long long>(m_stats.segments),
                         static_cast<unsigned long long>(b[0]), static_cast<unsigned long long>(b[1]),
                         static_cast<unsigned long long>(b[2]), static_cast<unsigned long long>(b[3]),
                         static_cast<unsigned long long>(b[4]), static_cast<unsigned long long>(b[5]),
                         static_cast<unsigned long long>(m_stats.transfersAhead),
                         seconds(m_stats.band0), seconds(m_stats.otherBands),
                         static_cast<unsigned long long>(held), seconds(heldTime),
                         static_cast<unsigned long long>(drains), seconds(drainTime));
            m_stats = {};
            m_statsStart = now;
        }

        // writtenPages/texturePages for the last inputs seen: nearly every
        // primitive of a frame has the same frame/Z buffer and scissor, and
        // runs of them the same texture, while building a set costs a few
        // hundred bit operations (findings/20 s.20). Only the dispatcher
        // thread uses these.
        struct WriteKey
        {
            uint32_t fbp, fbw, zbp, zmask, y0, y1;
            bool operator==(const WriteKey &) const = default;
        };
        struct TextureKey
        {
            uint32_t tbp0, tbw, height, wrapV;
            bool operator==(const TextureKey &) const = default;
        };
        const WriteSets &cachedWrittenPages(const GSDrawState &state)
        {
            const GSContext &ctx = state.context;
            const WriteKey key{static_cast<uint32_t>(ctx.frame.fbp), static_cast<uint32_t>(ctx.frame.fbw),
                               static_cast<uint32_t>(ctx.zbuf.zbp), static_cast<uint32_t>(ctx.zbuf.zmask ? 1u : 0u),
                               static_cast<uint32_t>(ctx.scissor.y0), static_cast<uint32_t>(ctx.scissor.y1)};
            if (!m_writeCacheValid || !(key == m_writeKey))
            {
                m_writePages = writtenPages(state);
                m_writeKey = key;
                m_writeCacheValid = true;
                ++m_writeSetsVersion;
            }
            return m_writePages;
        }
        const PageSet &cachedTexturePages(const GSDrawState &state)
        {
            const GSContext &ctx = state.context;
            const TextureKey key{static_cast<uint32_t>(ctx.tex0.tbp0), static_cast<uint32_t>(ctx.tex0.tbw),
                                 static_cast<uint32_t>(state.textureHeight), static_cast<uint32_t>((ctx.clamp >> 2) & 3u)};
            if (!m_textureCacheValid || !(key == m_textureKey))
            {
                m_texturePages = texturePages(state);
                m_textureKey = key;
                m_textureCacheValid = true;
            }
            return m_texturePages;
        }
        WriteKey m_writeKey{};
        TextureKey m_textureKey{};
        WriteSets m_writePages;
        uint64_t m_writeSetsVersion = 0; // changes whenever m_writePages is rebuilt
        PageSet m_texturePages;
        bool m_writeCacheValid = false;
        bool m_textureCacheValid = false;

        // Dispatcher-only counters for the [gs-async] line; the producer
        // and drain counters below them are guarded by m_mutex.
        struct DispatchStats
        {
            uint64_t draws = 0;
            uint64_t segments = 0;
            uint64_t barriers[6] = {}; // indexed by Barrier
            uint64_t transfersAhead = 0; // transfer commands run without a barrier
            Clock::duration band0{};
            Clock::duration otherBands{};
        };
        DispatchStats m_stats;
        Clock::time_point m_statsStart{};

        // About half a frame's worth of the first level's primitives.
        static constexpr uint64_t kMaxPending = 8192;
        static constexpr size_t kSegmentLimit = 512;

        std::unique_ptr<GSCpuBackend> m_main;               // transfers, clears, readers
        std::vector<std::unique_ptr<GSCpuBackend>> m_bands; // drawing, one per row band
        PageSet m_segmentWrites;                            // pages written since the last barrier
        PageSet m_segmentReads;                             // texture and CLUT pages read since then
        PageSet m_transferPages;                            // pages the current transfer writes

        mutable std::mutex m_mutex;
        std::condition_variable m_workAvailable;
        mutable std::condition_variable m_drained;
        std::deque<Command> m_queue;
        uint64_t m_submitted = 0;
        uint64_t m_completed = 0;
        bool m_workerIdle = false;
        bool m_stop = false;
        uint64_t m_heldCount = 0;                // producers held by kMaxPending
        Clock::duration m_heldTime{};
        mutable uint64_t m_drainCount = 0;       // drain() calls that waited
        mutable Clock::duration m_drainTime{};

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

    std::unique_ptr<GSRasterBackend> makeAsyncBackend(uint32_t bandCount)
    {
        return std::make_unique<AsyncBackend>(bandCount);
    }
}
