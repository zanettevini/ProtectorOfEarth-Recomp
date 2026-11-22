// gs_record.cpp: the GS command recorder (format and use in gs_record.h, findings/20 s.21).

#include "gs_record.h"

#include <chrono>
#include <utility>

namespace ben10::gs_record
{
    namespace
    {
        double nowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }
    }

    RecordingBackend::RecordingBackend(std::unique_ptr<GSRasterBackend> inner, std::string path,
                                       double startSeconds, double lengthSeconds)
        : m_inner(std::move(inner)), m_path(std::move(path)), m_startSeconds(startSeconds),
          m_lengthSeconds(lengthSeconds), m_createdAt(nowSeconds())
    {
    }

    RecordingBackend::~RecordingBackend()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        finish();
    }

    bool RecordingBackend::active()
    {
        if (m_done)
            return false;
        const double now = nowSeconds();
        if (!m_file)
        {
            if (now - m_createdAt < m_startSeconds)
                return false;
            m_file = std::fopen(m_path.c_str(), "wb");
            if (!m_file)
            {
                std::fprintf(stderr, "[gs-record] cannot open %s; not recording\n", m_path.c_str());
                m_done = true;
                return false;
            }
            // The starting picture: GS memory once everything queued so far
            // has been drawn (the async backend's SnapshotVram drains first).
            std::vector<uint8_t> vram;
            m_inner->SnapshotVram(vram);
            put(kMagic, sizeof(kMagic));
            put(kVersion);
            put(static_cast<uint32_t>(vram.size()));
            put(vram.data(), vram.size());
            m_startedAt = now;
            std::fprintf(stderr, "[gs-record] recording the GS command stream into %s for %.0f s\n",
                         m_path.c_str(), m_lengthSeconds);
        }
        if (now - m_startedAt >= m_lengthSeconds)
        {
            finish();
            return false;
        }
        ++m_records;
        return true;
    }

    void RecordingBackend::put(const void *data, size_t size)
    {
        if (m_file && size != 0u)
            std::fwrite(data, 1, size, m_file);
    }

    void RecordingBackend::finish()
    {
        if (!m_file)
            return;
        tag(Tag::End);
        std::fclose(m_file);
        m_file = nullptr;
        m_done = true;
        std::fprintf(stderr, "[gs-record] %llu records written to %s\n",
                     static_cast<unsigned long long>(m_records), m_path.c_str());
    }

    void RecordingBackend::Initialize(uint8_t *vram, uint32_t vramSize)
    {
        m_inner->Initialize(vram, vramSize); // boot only; the recording has its own copy of GS memory
    }

    void RecordingBackend::Reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
            tag(Tag::Reset);
        m_inner->Reset();
    }

    // Each call is written before it is forwarded, under the lock, so the
    // file has the calls in the order the backend receives them.
    void RecordingBackend::Submit(const GSPrimitiveBatch &batch)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::Submit);
            put(batch);
        }
        m_inner->Submit(batch);
    }

    void RecordingBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::LoadClut);
            put(tex0);
            put(texclut);
        }
        m_inner->LoadClut(tex0, texclut);
    }

    void RecordingBackend::BeginTransfer(const GSTransferCommand &command)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::BeginTransfer);
            put(command);
        }
        m_inner->BeginTransfer(command);
    }

    void RecordingBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::UploadImage);
            put(sizeBytes);
            put(data, sizeBytes);
        }
        m_inner->UploadImage(data, sizeBytes);
    }

    void RecordingBackend::TextureFlush()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
            tag(Tag::TextureFlush);
        m_inner->TextureFlush();
    }

    // Sync is forwarded outside the lock: it only waits (or flags FINISH),
    // and the window thread calls it while the producers go on.
    void RecordingBackend::Sync(GSSyncReason reason)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (active())
            {
                tag(Tag::Sync);
                put(static_cast<uint8_t>(reason));
            }
        }
        m_inner->Sync(reason);
    }

    bool RecordingBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::Clear);
            put(context);
            put(rgba);
        }
        return m_inner->ClearFramebuffer(context, rgba);
    }

    void RecordingBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (active())
        {
            tag(Tag::WriteVram);
            const uint32_t args[6] = {psm, base, bw, x, y, value};
            put(args, sizeof(args));
        }
        m_inner->WriteVram(psm, base, bw, x, y, value);
    }
}
