// gs_synth.cpp: writes a synthetic GS recording with random draws, for randomized backend checks (findings/20 s.24).
//
// A recording from the game (src/gs/gs_record.h) only covers the states the
// game happened to use in those seconds. This tool writes a recording in the
// same format with random GS memory and random draws over the states the
// GS backends handle: triangles, sprites, points and lines; colour and Z
// formats; textures in every format with every wrap mode, point and
// bilinear filtering, S/T/Q and U/V coordinates; alpha test, destination
// alpha test, Z tests, blending, fog, FBMSK, FBA; CLUT loads, image
// uploads and TEXFLUSH. Replaying it (ben10_gs_replay) with two versions of
// a backend and comparing the hashes checks a change on cases the game's
// own recording may not reach. It holds no game data.
//
//   ben10_gs_synth OUT [--seed N] [--records N] [--any-texture 1]
//
// By default the recording follows the rule the row bands rely on
// (gs_async_backend.cpp): a TEXFLUSH before every textured draw, and no
// draw samples the frame or Z buffer it is drawing into (on the GS that
// result is undefined; a band could read pixels another band is writing).
// Textures may still sample pages earlier draws wrote. --any-texture 1
// drops both rules, for comparing two versions of GSCpuBackend alone.

#include "gs_record.h"

#include "runtime/gs/gs_types.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

namespace
{
    using ben10::gs_record::Tag;

    class Writer
    {
    public:
        explicit Writer(std::FILE *file) : m_file(file) {}
        template <typename T>
        void put(const T &value) { std::fwrite(&value, sizeof(T), 1, m_file); }
        void bytes(const void *data, size_t size) { std::fwrite(data, 1, size, m_file); }
        void tag(Tag t) { put(static_cast<uint8_t>(t)); }

    private:
        std::FILE *m_file;
    };

    class Random
    {
    public:
        explicit Random(uint64_t seed) : m_rng(seed) {}
        uint32_t below(uint32_t n) { return n == 0u ? 0u : static_cast<uint32_t>(m_rng() % n); }
        bool chance(uint32_t percent) { return below(100u) < percent; }
        float real(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(m_rng() % 1000003u) / 1000003.0f; }
        uint64_t bits() { return m_rng(); }
        template <typename T, size_t N>
        T pick(const T (&values)[N]) { return values[below(N)]; }

    private:
        std::mt19937_64 m_rng;
    };

    constexpr uint8_t kFramePsms[] = {GS_PSM_CT32, GS_PSM_CT32, GS_PSM_CT24, GS_PSM_CT16, GS_PSM_CT16S};
    constexpr uint8_t kZPsms[] = {GS_PSM_Z32, GS_PSM_Z24, GS_PSM_Z24, GS_PSM_Z16, GS_PSM_Z16S};
    constexpr uint8_t kTexPsms[] = {GS_PSM_CT32, GS_PSM_CT24, GS_PSM_CT16, GS_PSM_CT16S, GS_PSM_T8, GS_PSM_T8,
                                    GS_PSM_T4, GS_PSM_T4, GS_PSM_T8H, GS_PSM_T4HL, GS_PSM_T4HH, GS_PSM_Z32};
    constexpr uint8_t kClutPsms[] = {GS_PSM_CT32, GS_PSM_CT16, GS_PSM_CT16S};

    GSDrawState randomState(Random &rnd, GSPrimType type)
    {
        GSDrawState st{};
        GSContext &c = st.context;
        c.frame.fbp = rnd.below(400u);
        c.frame.fbw = 1u + rnd.below(10u);
        c.frame.psm = rnd.pick(kFramePsms);
        c.frame.fbmsk = rnd.chance(15) ? static_cast<uint32_t>(rnd.bits()) : 0u;
        c.scissor.x0 = static_cast<uint16_t>(rnd.below(200u));
        c.scissor.x1 = static_cast<uint16_t>(c.scissor.x0 + rnd.below(500u));
        c.scissor.y0 = static_cast<uint16_t>(rnd.below(150u));
        c.scissor.y1 = static_cast<uint16_t>(c.scissor.y0 + rnd.below(350u));
        c.tex0.tbp0 = rnd.below(512u * 32u);
        c.tex0.tbw = static_cast<uint8_t>(1u + rnd.below(10u));
        c.tex0.psm = rnd.pick(kTexPsms);
        c.tex0.tw = static_cast<uint8_t>(2u + rnd.below(9u));
        c.tex0.th = static_cast<uint8_t>(2u + rnd.below(9u));
        c.tex0.tcc = static_cast<uint8_t>(rnd.below(2u));
        c.tex0.tfx = static_cast<uint8_t>(rnd.below(4u));
        c.tex0.cbp = rnd.below(512u * 32u);
        c.tex0.cpsm = rnd.pick(kClutPsms);
        c.tex0.csm = static_cast<uint8_t>(rnd.below(2u));
        c.tex0.csa = static_cast<uint8_t>(rnd.below(32u));
        c.xyoffset.ofx = rnd.chance(50) ? 0u : static_cast<uint16_t>(rnd.below(64u) << 4);
        c.xyoffset.ofy = rnd.chance(50) ? 0u : static_cast<uint16_t>(rnd.below(64u) << 4);
        c.zbuf.zbp = rnd.below(400u);
        c.zbuf.psm = rnd.pick(kZPsms);
        c.zbuf.zmask = rnd.chance(30);
        c.clamp = rnd.bits() & 0x3FFFFFFFFFull; // WMS, WMT, MINU, MAXU, MINV, MAXV
        c.alpha = (rnd.bits() & 0xFFull) | (static_cast<uint64_t>(rnd.below(256u)) << 32);
        c.test = rnd.bits() & 0x7FFFFull;          // ATE..ZTST
        if (rnd.chance(60))
            c.test &= ~0x1ull;                     // alpha test off more often
        c.fba = rnd.below(2u);
        st.prim.type = type;
        st.prim.iip = rnd.chance(70);
        st.prim.tme = rnd.chance(70);
        st.prim.fge = rnd.chance(20);
        st.prim.abe = rnd.chance(50);
        st.prim.fst = rnd.chance(40);
        st.texa.ta0 = static_cast<uint8_t>(rnd.below(256u));
        st.texa.aem = rnd.chance(50);
        st.texa.ta1 = static_cast<uint8_t>(rnd.below(256u));
        st.pabe = rnd.chance(20);
        st.fogR = static_cast<uint8_t>(rnd.below(256u));
        st.fogG = static_cast<uint8_t>(rnd.below(256u));
        st.fogB = static_cast<uint8_t>(rnd.below(256u));
        st.textureWidth = static_cast<uint16_t>(1u << c.tex0.tw);
        st.textureHeight = static_cast<uint16_t>(1u << c.tex0.th);
        st.linearFilter = rnd.chance(60);
        return st;
    }

    // Rough page ranges (8 KB pages) of a buffer and a texture, generous on
    // purpose: used only to keep a draw from sampling its own frame or Z.
    bool overlaps(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1)
    {
        return a0 <= b1 && b0 <= a1;
    }
    bool samplesOwnBuffers(const GSDrawState &st)
    {
        const GSContext &c = st.context;
        const uint32_t t0 = c.tex0.tbp0 / 32u;
        const uint32_t t1 = t0 + (1024u / 32u + 2u) * c.tex0.tbw * 2u + 2u;
        const uint32_t rows = (static_cast<uint32_t>(c.scissor.y1) / 32u + 2u) * c.frame.fbw * 2u + 2u;
        return overlaps(t0, t1, c.frame.fbp, c.frame.fbp + rows) || overlaps(t0, t1, c.zbuf.zbp, c.zbuf.zbp + rows) ||
               t1 >= 512u; // wrapping past the end of GS memory
    }

    GSVertex randomVertex(Random &rnd, const GSDrawState &st, float cx, float cy, float size)
    {
        GSVertex v{};
        const float ofx = static_cast<float>(st.context.xyoffset.ofx >> 4);
        const float ofy = static_cast<float>(st.context.xyoffset.ofy >> 4);
        v.x = cx + rnd.real(-size, size) + ofx;
        v.y = cy + rnd.real(-size, size) + ofy;
        v.z = rnd.chance(50) ? static_cast<double>(rnd.below(1u << 24)) : static_cast<double>(rnd.bits() & 0xFFFFFFFFull);
        v.r = static_cast<uint8_t>(rnd.below(256u));
        v.g = static_cast<uint8_t>(rnd.below(256u));
        v.b = static_cast<uint8_t>(rnd.below(256u));
        v.a = static_cast<uint8_t>(rnd.below(256u));
        v.q = rnd.chance(5) ? rnd.real(-1.0f, 1.0f) : rnd.real(0.2f, 4.0f);
        v.s = rnd.real(-0.5f, 1.5f) * v.q;
        v.t = rnd.real(-0.5f, 1.5f) * v.q;
        const float texW = static_cast<float>(st.textureWidth), texH = static_cast<float>(st.textureHeight);
        v.u = static_cast<uint16_t>(rnd.real(0.0f, texW * 1.5f) * 16.0f);
        v.v = static_cast<uint16_t>(rnd.real(0.0f, texH * 1.5f) * 16.0f);
        v.fog = static_cast<uint8_t>(rnd.below(256u));
        return v;
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s OUT [--seed N] [--records N]\n", argv[0]);
        return 2;
    }
    uint64_t seed = 1;
    long records = 60000;
    bool anyTexture = false;
    for (int i = 2; i + 1 < argc; i += 2)
    {
        const std::string arg = argv[i];
        if (arg == "--seed")
            seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (arg == "--records")
            records = std::atol(argv[i + 1]);
        else if (arg == "--any-texture")
            anyTexture = std::atoi(argv[i + 1]) != 0;
    }
    std::FILE *file = std::fopen(argv[1], "wb");
    if (!file)
    {
        std::fprintf(stderr, "gs_synth: cannot write %s\n", argv[1]);
        return 1;
    }
    Writer out(file);
    Random rnd(seed);

    // Header and random GS memory.
    const uint32_t vramSize = 4u * 1024u * 1024u;
    out.bytes(ben10::gs_record::kMagic, sizeof(ben10::gs_record::kMagic));
    out.put(ben10::gs_record::kVersion);
    out.put(vramSize);
    std::vector<uint8_t> vram(vramSize);
    for (uint8_t &b : vram)
        b = static_cast<uint8_t>(rnd.bits());
    out.bytes(vram.data(), vram.size());

    constexpr GSPrimType kTypes[] = {GS_PRIM_TRIANGLE, GS_PRIM_TRISTRIP, GS_PRIM_TRIFAN, GS_PRIM_TRIANGLE,
                                     GS_PRIM_SPRITE, GS_PRIM_SPRITE, GS_PRIM_POINT, GS_PRIM_LINE};
    for (long n = 0; n < records; ++n)
    {
        const uint32_t kind = rnd.below(100u);
        if (kind < 2u)
        {
            GSTex0Reg tex0{};
            tex0.cbp = rnd.below(512u * 32u);
            tex0.cpsm = rnd.pick(kClutPsms);
            tex0.csm = static_cast<uint8_t>(rnd.below(2u));
            tex0.csa = static_cast<uint8_t>(rnd.below(32u));
            tex0.cld = 1;
            tex0.psm = rnd.chance(50) ? GS_PSM_T8 : GS_PSM_T4;
            GSTexClutReg texclut{};
            texclut.cbw = static_cast<uint8_t>(1u + rnd.below(4u));
            texclut.cou = static_cast<uint8_t>(rnd.below(4u));
            texclut.cov = static_cast<uint16_t>(rnd.below(64u));
            out.tag(Tag::LoadClut);
            out.put(tex0);
            out.put(texclut);
        }
        else if (kind < 4u)
        {
            GSTransferCommand t{};
            constexpr uint8_t kUploadPsms[] = {GS_PSM_CT32, GS_PSM_CT16, GS_PSM_T8, GS_PSM_T4};
            t.bitbltbuf.dbp = rnd.below(512u * 32u);
            t.bitbltbuf.dbw = static_cast<uint8_t>(1u + rnd.below(8u));
            t.bitbltbuf.dpsm = rnd.pick(kUploadPsms);
            t.trxpos.dsax = static_cast<uint16_t>(rnd.below(64u));
            t.trxpos.dsay = static_cast<uint16_t>(rnd.below(64u));
            t.trxreg.rrw = static_cast<uint16_t>(8u * (1u + rnd.below(8u)));
            t.trxreg.rrh = static_cast<uint16_t>(1u + rnd.below(64u));
            t.direction = 0u;
            const uint32_t bits = t.bitbltbuf.dpsm == GS_PSM_CT32 ? 32u : t.bitbltbuf.dpsm == GS_PSM_CT16 ? 16u
                                                                 : t.bitbltbuf.dpsm == GS_PSM_T8    ? 8u
                                                                                                    : 4u;
            const uint32_t size = static_cast<uint32_t>(t.trxreg.rrw) * t.trxreg.rrh * bits / 8u;
            std::vector<uint8_t> data(size);
            for (uint8_t &b : data)
                b = static_cast<uint8_t>(rnd.bits());
            out.tag(Tag::BeginTransfer);
            out.put(t);
            out.tag(Tag::UploadImage);
            out.put(size);
            out.bytes(data.data(), data.size());
        }
        else if (kind < 5u)
        {
            out.tag(Tag::TextureFlush);
        }
        else
        {
            const GSPrimType type = rnd.pick(kTypes);
            GSPrimitiveBatch batch{};
            batch.state = randomState(rnd, type);
            if (!anyTexture && batch.state.prim.tme)
            {
                // Frames and Z buffers stay in the lower pages; textures
                // move until they miss this draw's own buffers.
                int tries = 0;
                while (samplesOwnBuffers(batch.state) && tries++ < 50)
                    batch.state.context.tex0.tbp0 = rnd.below(512u * 32u);
                if (samplesOwnBuffers(batch.state))
                    batch.state.prim.tme = false;
                else
                    out.tag(Tag::TextureFlush);
            }
            const float size = rnd.chance(10) ? 300.0f : rnd.real(2.0f, 60.0f);
            const float cx = rnd.real(-20.0f, 660.0f), cy = rnd.real(-20.0f, 470.0f);
            batch.vertexCount = type == GS_PRIM_POINT ? 1 : (type == GS_PRIM_LINE || type == GS_PRIM_SPRITE) ? 2 : 3;
            for (uint32_t i = 0; i < batch.vertexCount; ++i)
                batch.vertices[i] = randomVertex(rnd, batch.state, cx, cy, size);
            out.tag(Tag::Submit);
            out.put(batch);
        }
    }
    out.tag(Tag::End);
    std::fclose(file);
    return 0;
}
