// gs_replay.cpp: replays a recorded GS command stream on a raster backend (findings/20 s.21).
//
// A recording (src/gs/gs_record.h, BEN10_GS_RECORD) is GS memory at one
// moment and every backend call the game made after it. This tool loads GS
// memory, makes the calls again on the backend chosen on the command line,
// and prints a hash of GS memory every N records and at the end, so two
// backends, or two versions of one, can be checked to draw the same thing;
// the last line is the time the replay took, to compare speed on the same
// work.
//
//   ben10_gs_replay FILE [--sync | --bands N] [--every N] [--reps N]
//     --sync     PS2Recomp's GSCpuBackend, on this thread (the reference)
//     --bands N  the port's async backend with N row bands (default 1)
//     --row-band K N
//                GSCpuBackend on this thread drawing only row band K of N
//                (what one band thread does), to time one band's share:
//                its hashes differ from the others by design
//     --every N  a hash line every N records (default 20000)
//     --reps N   replay N times, from the same start; hashes of the first
//                replay only (default 1)
//     --dump N F write GS memory after record N to file F (raw 4 MB), to
//                find which pixels two backends draw differently
//     --limit N  stop after record N (as if the recording ended there), for
//                quick runs and instruction counts under callgrind
//     --list A B print records A to B (numbered from 1) on stderr: draws
//                with their frame, Z, texture and scissor; transfers; CLUT
//                loads
//
// What a replay does not have: the CLUT buffer and texture page caches the
// game's backend held when the recording started (only GS memory is saved),
// so until the game loads a CLUT again the replay may draw differently from
// the game. Every backend replays from the same start, so they can still be
// compared with each other.

#include "gs_async_backend.h"
#include "gs_record.h"

#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_cpu_backend.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using ben10::gs_record::Tag;

    // FNV-1a, 64-bit: enough to tell two pictures apart.
    uint64_t hashBytes(const std::vector<uint8_t> &bytes)
    {
        uint64_t h = 1469598103934665603ull;
        for (uint8_t b : bytes)
        {
            h ^= b;
            h *= 1099511628211ull;
        }
        return h;
    }

    struct Recording
    {
        std::vector<uint8_t> vram;    // GS memory at the start
        std::vector<uint8_t> records; // everything after the header
    };

    bool load(const char *path, Recording &out)
    {
        std::FILE *file = std::fopen(path, "rb");
        if (!file)
        {
            std::fprintf(stderr, "gs_replay: cannot open %s\n", path);
            return false;
        }
        char magic[8];
        uint32_t version = 0, size = 0;
        bool ok = std::fread(magic, 1, 8, file) == 8 && std::memcmp(magic, ben10::gs_record::kMagic, 8) == 0 &&
                  std::fread(&version, 4, 1, file) == 1 && version == ben10::gs_record::kVersion &&
                  std::fread(&size, 4, 1, file) == 1;
        if (ok)
        {
            out.vram.resize(size);
            ok = std::fread(out.vram.data(), 1, size, file) == size;
        }
        if (ok)
        {
            std::vector<uint8_t> chunk(1u << 20);
            size_t got = 0;
            while ((got = std::fread(chunk.data(), 1, chunk.size(), file)) != 0u)
                out.records.insert(out.records.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(got));
        }
        std::fclose(file);
        if (!ok)
            std::fprintf(stderr, "gs_replay: %s is not a version-%u GS recording\n", path, ben10::gs_record::kVersion);
        return ok;
    }

    // Reads the records in order; false at the end or on a short record.
    class Reader
    {
    public:
        explicit Reader(const std::vector<uint8_t> &bytes) : m_bytes(bytes) {}
        template <typename T>
        bool get(T &value)
        {
            return getBytes(&value, sizeof(T));
        }
        bool getBytes(void *out, size_t size)
        {
            if (m_bytes.size() - m_pos < size)
                return false;
            std::memcpy(out, m_bytes.data() + m_pos, size);
            m_pos += size;
            return true;
        }

    private:
        const std::vector<uint8_t> &m_bytes;
        size_t m_pos = 0;
    };

    // One replay. Returns the number of records, or -1 on a broken file.
    long long g_dumpAt = -1;     // --dump
    const char *g_dumpPath = nullptr;
    long long g_listFrom = 0, g_listTo = -1; // --list
    long long g_limit = -1;                  // --limit

    void listDraw(long long n, const GSPrimitiveBatch &batch)
    {
        const GSDrawState &st = batch.state;
        const GSContext &c = st.context;
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (uint32_t i = 0; i < batch.vertexCount && i < 3u; ++i)
        {
            x0 = std::min(x0, batch.vertices[i].x);
            x1 = std::max(x1, batch.vertices[i].x);
            y0 = std::min(y0, batch.vertices[i].y);
            y1 = std::max(y1, batch.vertices[i].y);
        }
        std::fprintf(stderr,
                     "%lld draw type %d frame fbp %u fbw %u psm %02x | z zbp %u psm %02x mask %d | scissor x %u-%u y %u-%u"
                     " | abe %d | tex %s tbp0 %u tbw %u psm %02x tw %u th %u | xy %.1f-%.1f, %.1f-%.1f\n",
                     n, static_cast<int>(st.prim.type), c.frame.fbp, c.frame.fbw, c.frame.psm, c.zbuf.zbp, c.zbuf.psm,
                     c.zbuf.zmask ? 1 : 0, c.scissor.x0, c.scissor.x1, c.scissor.y0, c.scissor.y1, st.prim.abe ? 1 : 0,
                     st.prim.tme ? "on" : "off", c.tex0.tbp0, c.tex0.tbw, c.tex0.psm, c.tex0.tw, c.tex0.th, x0, x1, y0, y1);
    }
    bool listing(long long n) { return n >= g_listFrom && n <= g_listTo; }

    long long replay(const Recording &recording, GSRasterBackend &backend, std::vector<uint8_t> &vram,
                     long long every, bool printHashes, uint64_t &draws)
    {
        vram = recording.vram;
        backend.Initialize(vram.data(), static_cast<uint32_t>(vram.size()));
        Reader reader(recording.records);
        std::vector<uint8_t> image, snapshot;
        long long count = 0;
        draws = 0;
        for (;;)
        {
            uint8_t tag = 0;
            if (!reader.get(tag))
                return -1;
            if (g_limit >= 0 && count >= g_limit)
                tag = static_cast<uint8_t>(Tag::End);
            bool ok = true;
            switch (static_cast<Tag>(tag))
            {
            case Tag::Submit:
            {
                GSPrimitiveBatch batch{};
                ok = reader.get(batch);
                if (ok && listing(count + 1))
                    listDraw(count + 1, batch);
                if (ok)
                {
                    backend.Submit(batch);
                    ++draws;
                }
                break;
            }
            case Tag::LoadClut:
            {
                GSTex0Reg tex0{};
                GSTexClutReg texclut{};
                ok = reader.get(tex0) && reader.get(texclut);
                if (ok && listing(count + 1))
                    std::fprintf(stderr, "%lld CLUT cbp %u cpsm %02x\n", count + 1, tex0.cbp, tex0.cpsm);
                if (ok)
                    backend.LoadClut(tex0, texclut);
                break;
            }
            case Tag::BeginTransfer:
            {
                GSTransferCommand command{};
                ok = reader.get(command);
                if (ok && listing(count + 1))
                    std::fprintf(stderr, "%lld transfer dir %u dbp %u dbw %u psm %02x at %u,%u size %ux%u\n", count + 1,
                                 command.direction, command.bitbltbuf.dbp, command.bitbltbuf.dbw, command.bitbltbuf.dpsm,
                                 command.trxpos.dsax, command.trxpos.dsay, command.trxreg.rrw, command.trxreg.rrh);
                if (ok)
                    backend.BeginTransfer(command);
                break;
            }
            case Tag::UploadImage:
            {
                uint32_t size = 0;
                ok = reader.get(size);
                if (ok)
                {
                    image.resize(size);
                    ok = reader.getBytes(image.data(), size);
                }
                if (ok)
                    backend.UploadImage(image.data(), static_cast<uint32_t>(image.size()));
                break;
            }
            case Tag::TextureFlush:
                if (listing(count + 1))
                    std::fprintf(stderr, "%lld TEXFLUSH\n", count + 1);
                backend.TextureFlush();
                break;
            case Tag::Clear:
            {
                GSContext context{};
                uint32_t rgba = 0;
                ok = reader.get(context) && reader.get(rgba);
                if (ok)
                    backend.ClearFramebuffer(context, rgba);
                break;
            }
            case Tag::WriteVram:
            {
                uint32_t a[6] = {};
                ok = reader.getBytes(a, sizeof(a));
                if (ok)
                    backend.WriteVram(a[0], a[1], a[2], a[3], a[4], a[5]);
                break;
            }
            case Tag::Reset:
                backend.Reset();
                break;
            case Tag::Sync:
            {
                uint8_t reason = 0;
                ok = reader.get(reason);
                if (ok)
                    backend.Sync(static_cast<GSSyncReason>(reason));
                break;
            }
            case Tag::End:
                backend.SnapshotVram(snapshot); // waits for everything queued
                if (printHashes)
                    std::printf("end after %lld records: %016llx\n", count,
                                static_cast<unsigned long long>(hashBytes(snapshot)));
                return count;
            default:
                ok = false;
                break;
            }
            if (!ok)
                return -1;
            ++count;
            if (printHashes && count == g_dumpAt && g_dumpPath)
            {
                backend.SnapshotVram(snapshot);
                if (std::FILE *dump = std::fopen(g_dumpPath, "wb"))
                {
                    std::fwrite(snapshot.data(), 1, snapshot.size(), dump);
                    std::fclose(dump);
                }
            }
            if (printHashes && every > 0 && count % every == 0)
            {
                backend.SnapshotVram(snapshot);
                std::printf("after %lld records: %016llx\n", count,
                            static_cast<unsigned long long>(hashBytes(snapshot)));
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s FILE [--sync | --bands N] [--every N] [--reps N] [--row-band K N] [--dump N FILE] [--list A B] [--limit N]\n", argv[0]);
        return 2;
    }
    bool sync = false;
    uint32_t bands = 1;
    int rowBand = -1;
    uint32_t rowBandCount = 1;
    long long every = 20000;
    int reps = 1;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--sync")
            sync = true;
        else if (arg == "--row-band" && i + 2 < argc)
        {
            sync = true;
            rowBand = std::atoi(argv[++i]);
            rowBandCount = static_cast<uint32_t>(std::max(1, std::atoi(argv[++i])));
        }
        else if (arg == "--bands" && i + 1 < argc)
            bands = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (arg == "--every" && i + 1 < argc)
            every = std::atoll(argv[++i]);
        else if (arg == "--dump" && i + 2 < argc)
        {
            g_dumpAt = std::atoll(argv[++i]);
            g_dumpPath = argv[++i];
        }
        else if (arg == "--limit" && i + 1 < argc)
            g_limit = std::atoll(argv[++i]);
        else if (arg == "--list" && i + 2 < argc)
        {
            g_listFrom = std::atoll(argv[++i]);
            g_listTo = std::atoll(argv[++i]);
        }
        else if (arg == "--reps" && i + 1 < argc)
            reps = std::max(1, std::atoi(argv[++i]));
        else
        {
            std::fprintf(stderr, "gs_replay: unknown option %s\n", arg.c_str());
            return 2;
        }
    }
    Recording recording;
    if (!load(argv[1], recording))
        return 1;

    std::vector<uint8_t> vram;
    uint64_t draws = 0;
    long long records = 0;
    double seconds = 0.0;
    for (int rep = 0; rep < reps; ++rep)
    {
        // A new backend each time: no CLUT or cache state carried over.
        std::unique_ptr<GSRasterBackend> backend;
        if (sync)
        {
            auto cpu = std::make_unique<GSCpuBackend>();
            if (rowBand >= 0)
                cpu->SetRowBand(static_cast<uint32_t>(rowBand), rowBandCount);
            backend = std::move(cpu);
        }
        else
            backend = ben10::gs_async::makeAsyncBackend(bands);
        const auto start = std::chrono::steady_clock::now();
        records = replay(recording, *backend, vram, every, rep == 0, draws);
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (records < 0)
        {
            std::fprintf(stderr, "gs_replay: the recording ends in the middle of a record\n");
            return 1;
        }
    }
    std::printf("%lld records (%llu draws) x %d on %s in %.3f s\n", records,
                static_cast<unsigned long long>(draws), reps,
                rowBand >= 0 ? ("GSCpuBackend, row band " + std::to_string(rowBand) + " of " + std::to_string(rowBandCount)).c_str()
                : sync     ? "GSCpuBackend"
                           : (std::to_string(bands) + " band(s)").c_str(),
                seconds);
    return 0;
}
