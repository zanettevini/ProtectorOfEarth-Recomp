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
//     are 8 KB; the page sets are over-estimates: whole rows of pages of
//     each buffer's own page size (frame and Z rows in the scissor), and
//     for a texture the rows its texture coordinates can reach
//     (textureRows: exact for sprites, widened for triangles), or the
//     whole height its wrap mode allows when that cannot be told
//     (findings/20 s.23).
//   - A row of pixels is one band's only while every draw of the segment
//     sees that memory through the same layout (base page, FBW, pixel
//     format): a draw whose frame or Z pages the segment already used
//     through another layout (a Z buffer redrawn as a colour buffer; the
//     PSMZ and PSMCT block orders differ) first lets every band finish
//     (layoutConflict, findings/20 s.21).
//   - A draw whose own rows share memory (past the buffer's width, frame
//     and Z on the same pages, a texture on pages it writes) is made
//     whole, in order, by the main backend after every band has finished
//     (drawsOverItself, findings/20 s.24); the main backend gets every
//     CLUT load and TEXFLUSH too, so it has the bands' CLUT.
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
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

    // Page size in pixels by pixel format (GS manual, "Page" tables): 64x32
    // for the 32-bit layouts (PSMCT32, PSMCT24, PSMT8H, PSMT4HL, PSMT4HH,
    // PSMZ32, PSMZ24), 64x64 for the 16-bit ones, 128x64 for PSMT8,
    // 128x128 for PSMT4; 0x0 for anything else.
    struct PageShape
    {
        uint32_t width, height;
    };
    PageShape pageShape(uint32_t psm)
    {
        switch (psm)
        {
        case 0x00: case 0x01: case 0x1B: case 0x24: case 0x2C: case 0x30: case 0x31:
            return {64u, 32u};
        case 0x02: case 0x0A: case 0x32: case 0x3A:
            return {64u, 64u};
        case 0x13:
            return {128u, 64u};
        case 0x14:
            return {128u, 128u};
        default:
            return {0u, 0u};
        }
    }

    // Adds the pages that hold rows y0..y1, columns 0..xEnd-1, of a buffer
    // in format `psm`, `widthUnits` x 64 pixels wide, starting at
    // `firstPage`: whole rows of pages, from the first page of the first
    // row to the last page the columns reach in the last row (columns past
    // the buffer width run into the next pages, as GS addressing does),
    // plus one page when the buffer does not start on a page boundary.
    // An unknown format, or more than all of GS memory, gives every page.
    void addRows(PageSet &set, uint32_t firstPage, uint32_t widthUnits, uint32_t psm,
                 uint32_t y0, uint32_t y1, uint32_t xEnd, bool unaligned = false)
    {
        const PageShape shape = pageShape(psm);
        if (shape.width == 0u || y1 < y0)
        {
            if (shape.width == 0u)
                set.set();
            return;
        }
        const uint32_t widthPixels = std::max<uint32_t>(widthUnits, 1u) * 64u;
        const uint32_t perRow = (widthPixels + shape.width - 1u) / shape.width;
        const uint32_t columns = std::max(perRow, (std::max(xEnd, 1u) + shape.width - 1u) / shape.width);
        const uint32_t from = firstPage + (y0 / shape.height) * perRow;
        const uint32_t to = firstPage + (y1 / shape.height) * perRow + columns + (unaligned ? 1u : 0u); // exclusive
        if (to - from >= kPages)
        {
            set.set();
            return;
        }
        for (uint32_t page = from; page < to; ++page)
            set.set(page % kPages);
    }

    // Pages a primitive may write: its frame buffer and Z buffer rows
    // inside the scissor (FBP and ZBP count pages), with each buffer's own
    // page size (findings/20 s.21: counting 32-row pages for a 16-bit
    // buffer, whose pages are 64 rows tall, missed pages when the scissor
    // did not start at row 0).
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
        const uint32_t xEnd = static_cast<uint32_t>(ctx.scissor.x1) + 1u;
        addRows(sets.frame, ctx.frame.fbp, ctx.frame.fbw, ctx.frame.psm, ctx.scissor.y0, ctx.scissor.y1, xEnd);
        addRows(sets.z, ctx.zbuf.zbp, ctx.frame.fbw, ctx.zbuf.psm, ctx.scissor.y0, ctx.scissor.y1, xEnd); // Z uses FRAME's FBW
        sets.written = sets.frame;
        if (!ctx.zbuf.zmask)
            sets.written |= sets.z;
        return sets;
    }

    // Whether a primitive touches its Z buffer at all: it writes Z (ZMSK
    // clear) or tests it (ZTST, bits 17-18 of TEST, GEQUAL or GREATER;
    // ALWAYS and NEVER read nothing). GSCpuBackend reads Z for those tests
    // whatever TEST.ZTE says, so ZTE is not looked at (findings/20 s.24:
    // looking at it let two bands race on a Z buffer read with ZTE clear).
    bool touchesZ(const GSDrawState &state)
    {
        const GSContext &ctx = state.context;
        if (!ctx.zbuf.zmask)
            return true;
        return ((ctx.test >> 17) & 3u) >= 2u;
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

    // Pages a CLUT load reads (GSCpuBackend::LoadClutUnlocked). CSM1: a
    // 16x16 block of entries at CBP, the page of CBP and the next one when
    // the 1 KB of a 256-entry 32-bit CLUT (4 blocks) can run past its end.
    // CSM2: one row of entries, row TEXCLUT.COV from column COU x 16, in a
    // buffer TEXCLUT.CBW x 64 pixels wide (findings/20 s.24: only the CSM1
    // pages were counted before).
    PageSet clutPages(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
    {
        PageSet pages;
        const uint32_t cbp = static_cast<uint32_t>(tex0.cbp);
        if (tex0.csm == 0u)
        {
            pages.set((cbp / kBlocksPerPage) % kPages);
            if (cbp % kBlocksPerPage > kBlocksPerPage - 4u)
                pages.set((cbp / kBlocksPerPage + 1u) % kPages);
            return pages;
        }
        const uint32_t row = static_cast<uint32_t>(texclut.cov);
        const uint32_t xEnd = (static_cast<uint32_t>(texclut.cou) << 4u) + 256u;
        addRows(pages, cbp / kBlocksPerPage, std::max<uint32_t>(texclut.cbw, 1u), tex0.cpsm, row, row, xEnd,
                cbp % kBlocksPerPage != 0u);
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

    // The texture rows a primitive can read, worked out from its texture
    // coordinates the way GSCpuBackend samples (findings/20 s.23): the
    // range of V over the vertices (T/Q x height, or V/16 with FST), widened
    // for the slightly negative edge weights DrawTriangle accepts (1e-4,
    // which perspective can magnify by the ratio of the Q values) and by
    // two texels for the bilinear neighbours and rounding, then put through
    // the vertical wrap mode. Anything this does not cover (points, lines,
    // Q values of mixed sign or near 0, a range that a REPEAT wrap or a
    // negative FST value would fold, REGION_REPEAT) gives the whole height
    // the wrap mode can reach, as before.
    struct RowRange
    {
        uint32_t first, last;
    };

    // Sprites: GSCpuBackend::DrawSprite gives each drawn row y the texture
    // coordinate V0 + (V1 - V0) * ((y - top + 0.5) / height), linear in y,
    // so the first and last drawn rows bound it. This repeats that float
    // arithmetic for those two rows, then turns the coordinates into texel
    // rows the way SampleTexture does (FST: rounded to 1/16; bilinear: rows
    // floor(v - 0.5) and the next; point: trunc(v)), with V nudged by
    // 1/1000 of a texel each way so that a rounding difference cannot move
    // a bound. The full-screen effects draw 32-pixel-wide strips that read
    // the frame just drawn as a texture: with the rows a texture's full
    // TEX0 height can reach (1024) every strip looked like a read of the
    // strip before it. Returns false when it cannot tell.
    bool spriteTextureRows(const GSPrimitiveBatch &batch, int &first, int &last)
    {
        const GSDrawState &state = batch.state;
        const GSContext &ctx = state.context;
        const GSVertex &a = batch.vertices[0];
        const GSVertex &b = batch.vertices[1];
        const int ofy = ctx.xyoffset.ofy >> 4;
        int y0 = static_cast<int>(a.y) - ofy;
        int y1 = static_cast<int>(b.y) - ofy;
        if (y0 > y1)
            std::swap(y0, y1);
        const int spanY = std::max(1, y1 - y0);
        const int top = y0;
        const int bottom = top + spanY - 1;
        if (bottom < ctx.scissor.y0 || top > ctx.scissor.y1)
        {
            first = 0; // nothing is drawn
            last = -1;
            return true;
        }
        const int drawTop = std::clamp<int>(top, ctx.scissor.y0, ctx.scissor.y1);
        const int drawBottom = std::clamp<int>(bottom, ctx.scissor.y0, ctx.scissor.y1);
        const float texH = static_cast<float>(std::max<int>(state.textureHeight, 1));
        float v0f, v1f;
        if (state.prim.fst)
        {
            v0f = static_cast<float>(a.v >> 4);
            v1f = static_cast<float>(b.v >> 4);
        }
        else
        {
            auto safeQ = [](float q) { return std::fabs(q) > 1.0e-8f ? q : 1.0f; }; // fabsQ in GSCpuBackend
            v0f = (a.t / safeQ(a.q)) * texH;
            v1f = (b.t / safeQ(b.q)) * texH;
        }
        const float spriteH = static_cast<float>(spanY);
        const float vTop = v0f + (v1f - v0f) * ((static_cast<float>(drawTop - top) + 0.5f) / spriteH);
        const float vBottom = v0f + (v1f - v0f) * ((static_cast<float>(drawBottom - top) + 0.5f) / spriteH);
        double lo = std::min(vTop, vBottom), hi = std::max(vTop, vBottom);
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo < -4096.0 || hi > 4096.0)
            return false;
        const double nudge = 1.0e-3;
        lo -= nudge;
        hi += nudge;
        if (state.prim.fst)
        {
            // fixed = clamp(int(v * 16 + 0.5), 0, 0xFFFF); the sampler uses fixed / 16
            auto fixed = [](double v) { return std::clamp(std::trunc(v * 16.0 + 0.5), 0.0, 65535.0) / 16.0; };
            lo = fixed(lo);
            hi = fixed(hi);
        }
        if (state.linearFilter)
        {
            first = static_cast<int>(std::floor(lo - 0.5));
            last = static_cast<int>(std::floor(hi - 0.5)) + 1;
        }
        else
        {
            first = static_cast<int>(std::trunc(lo));
            last = static_cast<int>(std::trunc(hi));
        }
        return true;
    }

    // Triangles: the texture coordinate of every pixel is a weighted mean
    // of the vertices' (T/Q x height, or V/16 with FST), with weights down
    // to -1e-4 at the edges (DrawTriangle's edge test), which perspective
    // can magnify by the ratio of the Q values; plus two texels for the
    // bilinear neighbour and rounding. Returns false when it cannot tell
    // (Q values of mixed sign or near 0, an FST range reaching below 0,
    // which the sampler turns into a large unsigned value).
    bool triangleTextureRows(const GSPrimitiveBatch &batch, int &first, int &last)
    {
        const GSDrawState &state = batch.state;
        const double height = static_cast<double>(std::max<int>(state.textureHeight, 1));
        double lo = 0.0, hi = 0.0, slack = 2.0;
        if (state.prim.fst)
        {
            for (uint32_t i = 0; i < 3u; ++i)
            {
                const double v = static_cast<double>(batch.vertices[i].v) / 16.0;
                lo = i == 0u ? v : std::min(lo, v);
                hi = i == 0u ? v : std::max(hi, v);
            }
            slack += 1.0e-3 * (hi - lo);
            if (lo - slack < 0.0 || hi + slack >= 4096.0)
                return false; // below 0 or past 0xFFFF / 16 the unsigned 16-bit V wraps
        }
        else
        {
            double qMin = 0.0, qMax = 0.0;
            const bool positive = batch.vertices[0].q > 0.0f;
            for (uint32_t i = 0; i < 3u; ++i)
            {
                const float q = batch.vertices[i].q;
                const double aq = std::fabs(static_cast<double>(q));
                if (!(aq >= 1.0e-6) || !std::isfinite(batch.vertices[i].t) || (q > 0.0f) != positive)
                    return false;
                const double v = static_cast<double>(batch.vertices[i].t) / static_cast<double>(q) * height;
                lo = i == 0u ? v : std::min(lo, v);
                hi = i == 0u ? v : std::max(hi, v);
                qMin = i == 0u ? aq : std::min(qMin, aq);
                qMax = i == 0u ? aq : std::max(qMax, aq);
            }
            slack += 1.0e-3 * (qMax / qMin) * (hi - lo) + 1.0e-5 * (std::fabs(lo) + std::fabs(hi));
        }
        if (!std::isfinite(lo) || !std::isfinite(hi) || !std::isfinite(slack) || lo < -1.0e6 || hi > 1.0e6 ||
            hi - lo > 4096.0)
            return false;
        // Bilinear reads rows floor(v - 0.5) and the one below; a point
        // sample reads row trunc(v).
        first = static_cast<int>(std::floor(lo - slack)) - 1;
        last = static_cast<int>(std::ceil(hi + slack)) + 1;
        return true;
    }

    RowRange textureRows(const GSPrimitiveBatch &batch)
    {
        const GSDrawState &state = batch.state;
        const GSContext &ctx = state.context;
        const uint32_t wrapV = static_cast<uint32_t>((ctx.clamp >> 2) & 3u);
        const int height = std::max<int>(state.textureHeight, 1);
        const int minV = static_cast<int>((ctx.clamp >> 24) & 0x3FFu);
        const int maxV = static_cast<int>((ctx.clamp >> 34) & 0x3FFu);
        const RowRange whole = wrapV >= 2u ? RowRange{0u, 1023u} : RowRange{0u, static_cast<uint32_t>(height - 1)};

        const uint32_t type = static_cast<uint32_t>(state.prim.type);
        uint32_t count = 0;
        if (type >= 3u && type <= 5u)
            count = 3; // triangles
        else if (type == 6u)
            count = 2; // sprites
        if (count == 0u || wrapV == 3u || batch.vertexCount < count)
            return whole;

        int first = 0, last = -1;
        if (count == 2u)
        {
            if (!spriteTextureRows(batch, first, last))
                return whole;
            if (last < first)
                return {1u, 0u}; // no row
        }
        else if (!triangleTextureRows(batch, first, last))
        {
            return whole;
        }
        switch (wrapV)
        {
        case 0: // REPEAT: rows are V & (height - 1); only an unfolded range keeps its bounds
            if (first < 0 || last > height - 1)
                return whole;
            break;
        case 1: // CLAMP
            first = std::clamp(first, 0, height - 1);
            last = std::clamp(last, 0, height - 1);
            break;
        case 2: // REGION_CLAMP: min(max(v, MINV), MAXV), as GSCpuBackend wraps
            first = std::min(std::max(first, minV), maxV);
            last = std::min(std::max(last, minV), maxV);
            break;
        default:
            return whole;
        }
        return {static_cast<uint32_t>(first), static_cast<uint32_t>(last)};
    }

    // Pages of those rows (every column the texture or its region can
    // reach). GSCpuBackend reads only TEX0's base level (no mipmaps).
    PageSet texturePages(const GSDrawState &state, RowRange rows)
    {
        PageSet pages;
        const GSContext &ctx = state.context;
        const uint32_t wrapU = static_cast<uint32_t>(ctx.clamp & 3u);
        const uint32_t xEnd = wrapU >= 2u ? 1024u : std::max<uint32_t>(state.textureWidth, 1u);
        addRows(pages, ctx.tex0.tbp0 / kBlocksPerPage, ctx.tex0.tbw, ctx.tex0.psm, rows.first, rows.last, xEnd,
                ctx.tex0.tbp0 % kBlocksPerPage != 0u);
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
        Serial,   // a draw whose own rows share memory (drawsOverItself)
    };

    class AsyncBackend final : public GSRasterBackend
    {
    public:
        explicit AsyncBackend(uint32_t bandCount)
            : m_main(std::make_unique<GSCpuBackend>())
        {
            m_queue.reserve(kMaxPending + kSegmentLimit);
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
            reportIfDue(true); // the last stretch, however short (the dispatcher has stopped)
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
            // Two vectors swapped between the producers and this thread,
            // each with room for the whole queue limit: after the first
            // frames no command costs an allocation. (A std::deque holds one
            // command per block at this size, so it allocated and freed one
            // block per command: about 4% of the VIF1 worker's time on
            // Linux, findings/20 s.26.)
            std::vector<Command> batch;
            batch.reserve(kMaxPending + kSegmentLimit);
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
                        if (banded)
                        {
                            const PageSet *texture = state.prim.tme ? &cachedTexturePages(submit->batch) : nullptr;
                            const WriteSets &sets = cachedWrittenPages(state);
                            if (drawsOverItself(submit->batch, sets, texture))
                            {
                                // Rows of this one draw share memory: drawn
                                // whole, in order, on the main backend.
                                runSegment(segment, Barrier::Serial);
                                m_main->Submit(submit->batch);
                                ++m_stats.serialDraws;
                                reportDone(1u);
                                continue;
                            }
                            if (texture)
                            {
                                if ((*texture & m_segmentWrites).any())
                                    runSegment(segment, Barrier::Texture); // the texture may be in an unfinished band
                                m_segmentReads |= *texture;
                            }
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
                            const PageSet pages = clutPages(clut->tex0, clut->texclut);
                            if ((pages & m_segmentWrites).any())
                                runSegment(segment, Barrier::Clut);
                            m_segmentReads |= pages;
                            // The main backend keeps the same CLUT for the
                            // draws it makes itself (drawsOverItself). Its
                            // pages are final here: no queued draw writes them.
                            m_main->LoadClut(clut->tex0, clut->texclut);
                        }
                        segment.push_back(&command);
                    }
                    else if (std::holds_alternative<TextureFlushCommand>(command))
                    {
                        if (banded)
                            m_main->TextureFlush();
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

        // The pixels a draw can reach, inside the scissor: for a sprite the
        // rectangle DrawSprite draws (truncated corners less the XYOFFSET,
        // the far edge excluded), for a triangle DrawTriangle's box (floor
        // and ceil of the vertices less the XYOFFSET), for anything else the
        // vertices' box widened by a pixel each way.
        struct PixelBox
        {
            int x0, y0, x1, y1;
        };
        static PixelBox drawBox(const GSPrimitiveBatch &batch)
        {
            const GSContext &ctx = batch.state.context;
            PixelBox box{ctx.scissor.x0, ctx.scissor.y0, ctx.scissor.x1, ctx.scissor.y1};
            const uint32_t count = std::min<uint32_t>(batch.vertexCount, 3u);
            if (count == 0u)
                return box;
            const float ofx = static_cast<float>(ctx.xyoffset.ofx >> 4);
            const float ofy = static_cast<float>(ctx.xyoffset.ofy >> 4);
            float minX = batch.vertices[0].x, maxX = minX, minY = batch.vertices[0].y, maxY = minY;
            for (uint32_t i = 1; i < count; ++i)
            {
                minX = std::min(minX, batch.vertices[i].x);
                maxX = std::max(maxX, batch.vertices[i].x);
                minY = std::min(minY, batch.vertices[i].y);
                maxY = std::max(maxY, batch.vertices[i].y);
            }
            if (!std::isfinite(minX) || !std::isfinite(maxX) || !std::isfinite(minY) || !std::isfinite(maxY) ||
                std::fabs(minX) > 1.0e6f || std::fabs(maxX) > 1.0e6f || std::fabs(minY) > 1.0e6f || std::fabs(maxY) > 1.0e6f)
                return box;
            const uint32_t type = static_cast<uint32_t>(batch.state.prim.type);
            int x0, y0, x1, y1;
            if (type == 6u && count >= 2u)
            {
                const int ox = ctx.xyoffset.ofx >> 4, oy = ctx.xyoffset.ofy >> 4;
                int ax = static_cast<int>(batch.vertices[0].x) - ox, bx = static_cast<int>(batch.vertices[1].x) - ox;
                int ay = static_cast<int>(batch.vertices[0].y) - oy, by = static_cast<int>(batch.vertices[1].y) - oy;
                if (ax > bx)
                    std::swap(ax, bx);
                if (ay > by)
                    std::swap(ay, by);
                x0 = ax;
                y0 = ay;
                x1 = ax + std::max(1, bx - ax) - 1;
                y1 = ay + std::max(1, by - ay) - 1;
            }
            else if (type >= 3u && type <= 5u && count >= 3u)
            {
                x0 = static_cast<int>(std::floor(minX - ofx));
                x1 = static_cast<int>(std::ceil(maxX - ofx));
                y0 = static_cast<int>(std::floor(minY - ofy));
                y1 = static_cast<int>(std::ceil(maxY - ofy));
            }
            else
            {
                x0 = static_cast<int>(std::floor(minX - ofx)) - 1;
                x1 = static_cast<int>(std::ceil(maxX - ofx)) + 1;
                y0 = static_cast<int>(std::floor(minY - ofy)) - 1;
                y1 = static_cast<int>(std::ceil(maxY - ofy)) + 1;
            }
            box.x0 = std::max(box.x0, x0);
            box.x1 = std::min(box.x1, x1);
            box.y0 = std::max(box.y0, y0);
            box.y1 = std::min(box.y1, y1);
            return box;
        }

        // The band rule also needs each byte of a draw's own frame and Z
        // buffer to belong to one pixel row. Not so (findings/20 s.24, found
        // with random draws, tools/gs_replay/gs_synth.cpp) when the draw
        // reaches past the buffer's width (pixels past the edge land in the
        // pages of lower rows), when its frame and the Z buffer it touches
        // share pages, when a buffer covers all of GS memory, or when it
        // samples pages it writes as a texture. Rows drawn by different
        // bands could then write (or read) the same bytes; such a draw is
        // made whole, in order, by the main backend after the bands finish.
        // The scissor's page sets (the cached ones) are tried first; when
        // they say no, that is the answer; when they say yes, the check is
        // redone with the pixels the vertices can reach (the first level
        // draws into a frame whose scissor runs 64 rows past its own Z
        // buffer, but its primitives stay above them).
        bool drawsOverItself(const GSPrimitiveBatch &batch, const WriteSets &sets, const PageSet *texture)
        {
            const GSDrawState &state = batch.state;
            if (!overlapsItself(state, sets, texture, static_cast<uint32_t>(state.context.scissor.x1)))
                return false;
            const GSContext &ctx = state.context;
            const PixelBox box = drawBox(batch);
            if (box.x1 < box.x0 || box.y1 < box.y0)
                return false; // nothing inside the scissor
            WriteSets reached;
            const uint32_t xEnd = static_cast<uint32_t>(box.x1) + 1u;
            addRows(reached.frame, ctx.frame.fbp, ctx.frame.fbw, ctx.frame.psm, box.y0, box.y1, xEnd);
            addRows(reached.z, ctx.zbuf.zbp, ctx.frame.fbw, ctx.zbuf.psm, box.y0, box.y1, xEnd);
            reached.written = reached.frame;
            if (!ctx.zbuf.zmask)
                reached.written |= reached.z;
            return overlapsItself(state, reached, texture, static_cast<uint32_t>(box.x1));
        }
        static bool overlapsItself(const GSDrawState &state, const WriteSets &sets, const PageSet *texture, uint32_t lastX)
        {
            const GSContext &ctx = state.context;
            const bool z = touchesZ(state);
            if (sets.frame.all() || (z && sets.z.all()))
                return true;
            auto spills = [&](uint32_t psm)
            {
                const PageShape shape = pageShape(psm);
                if (shape.width == 0u)
                    return true;
                const uint32_t widthPixels = std::max<uint32_t>(ctx.frame.fbw, 1u) * 64u;
                const uint32_t perRow = (widthPixels + shape.width - 1u) / shape.width;
                return lastX >= perRow * shape.width;
            };
            if (spills(ctx.frame.psm) || (z && spills(ctx.zbuf.psm)))
                return true;
            if (z && (sets.frame & sets.z).any())
                return true;
            if (texture && (*texture & sets.written).any())
                return true; // reading pages the draw only reads (a Z buffer under ZMSK) is fine
            return false;
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
        void reportIfDue(bool final = false)
        {
            const auto now = Clock::now();
            if (m_statsStart == Clock::time_point{})
                m_statsStart = now;
            const auto elapsed = now - m_statsStart;
            if (final ? m_stats.draws == 0u : elapsed < std::chrono::seconds(10))
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
                         "transfer %llu, length %llu, idle %llu, layout %llu, serial draw %llu; %llu serial draws; %llu transfer commands ran ahead of the segment); band 0 drew %.2f s and waited %.2f s for the "
                         "other bands; producers held by the queue limit %llu times (%.2f s), drains %llu (%.2f s)\n",
                         seconds(elapsed), static_cast<unsigned long long>(m_stats.draws),
                         static_cast<unsigned long long>(m_stats.segments),
                         static_cast<unsigned long long>(b[0]), static_cast<unsigned long long>(b[1]),
                         static_cast<unsigned long long>(b[2]), static_cast<unsigned long long>(b[3]),
                         static_cast<unsigned long long>(b[4]), static_cast<unsigned long long>(b[5]),
                         static_cast<unsigned long long>(b[6]), static_cast<unsigned long long>(m_stats.serialDraws),
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
            // Everything writtenPages reads: the page sizes come from the
            // formats and the columns from the scissor's right edge.
            uint32_t fbp, fbw, fpsm, zbp, zpsm, zmask, y0, y1, x1;
            bool operator==(const WriteKey &) const = default;
        };
        struct TextureKey
        {
            uint32_t tbp0, tbw, psm, pageRowFirst, pageRowLast, xEnd;
            bool operator==(const TextureKey &) const = default;
        };
        const WriteSets &cachedWrittenPages(const GSDrawState &state)
        {
            const GSContext &ctx = state.context;
            const WriteKey key{static_cast<uint32_t>(ctx.frame.fbp), static_cast<uint32_t>(ctx.frame.fbw),
                               static_cast<uint32_t>(ctx.frame.psm), static_cast<uint32_t>(ctx.zbuf.zbp),
                               static_cast<uint32_t>(ctx.zbuf.psm), static_cast<uint32_t>(ctx.zbuf.zmask ? 1u : 0u),
                               static_cast<uint32_t>(ctx.scissor.y0), static_cast<uint32_t>(ctx.scissor.y1),
                               static_cast<uint32_t>(ctx.scissor.x1)};
            if (!m_writeCacheValid || !(key == m_writeKey))
            {
                m_writePages = writtenPages(state);
                m_writeKey = key;
                m_writeCacheValid = true;
                ++m_writeSetsVersion;
            }
            return m_writePages;
        }
        const PageSet &cachedTexturePages(const GSPrimitiveBatch &batch)
        {
            // Keyed on the rows of pages the texture rows fall in: the
            // triangles of one model usually stay within the same ones.
            const GSDrawState &state = batch.state;
            const GSContext &ctx = state.context;
            const RowRange rows = textureRows(batch);
            const PageShape shape = pageShape(ctx.tex0.psm);
            const uint32_t pageHeight = std::max<uint32_t>(shape.height, 1u);
            const uint32_t wrapU = static_cast<uint32_t>(ctx.clamp & 3u);
            const TextureKey key{static_cast<uint32_t>(ctx.tex0.tbp0), static_cast<uint32_t>(ctx.tex0.tbw),
                                 static_cast<uint32_t>(ctx.tex0.psm), rows.first / pageHeight, rows.last / pageHeight,
                                 wrapU >= 2u ? 1024u : std::max<uint32_t>(state.textureWidth, 1u)};
            if (!m_textureCacheValid || !(key == m_textureKey))
            {
                // Whole page rows, so that the set depends only on the key.
                m_texturePages = texturePages(state, {key.pageRowFirst * pageHeight, key.pageRowLast * pageHeight + pageHeight - 1u});
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
            uint64_t barriers[7] = {}; // indexed by Barrier
            uint64_t serialDraws = 0;  // draws made whole on the main backend
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
        std::vector<Command> m_queue; // reserved in the constructor (see run())
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
