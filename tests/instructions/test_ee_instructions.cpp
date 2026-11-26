// test_ee_instructions.cpp: end-to-end per-instruction tests for the EE.
//
// How it works (tests/CMakeLists.txt):
//   1. tests/tools/make_test_elf.py builds a PS2 ELF with one small function
//      per case (no game data).
//   2. The REAL ps2_recomp (the same one that translates the game) writes
//      the C++.
//   3. This file sets up the registers, calls the generated function and
//      compares with the expected result.
//
// Where the expected values come from: PS2 behaviour as described in the
// R5900 manual and implemented in PCSX2's interpreter (commit 15afef6),
// cited case by case as "PCSX2 <file> <function>". The master prompt asks to
// "compare against a trusted reference"; PCSX2 is the project's reference.
// Where PCSX2 and the runtime disagree and there is no way to decide without
// hardware, the case does NOT go here (it is noted in docs/findings/14).
//
// The cases come from the categories the game uses most and that are most
// likely to go wrong (docs/findings/09 s.5): 64-bit, LQ/SQ, "likely"
// branches, FPU, MMI and VU0 macro mode.

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_recompiled_functions.h"
#include "test_cases.h"

namespace {

// MIPS register numbers, as in make_test_elf.py.
constexpr int V0 = 2, V1 = 3, A0 = 4, A1 = 5, A2 = 6, A3 = 7, RA = 31;

// Any return address: the "jr ra" at the end of each case just copies this
// into ctx->pc and returns.
constexpr uint32_t kReturnAddress = 0x00200000u;

struct U128 {
    uint64_t lo = 0, hi = 0;
    bool operator==(const U128&) const = default;
};

std::ostream& operator<<(std::ostream& os, const U128& v) {
    return os << std::hex << "{hi=0x" << v.hi << ", lo=0x" << v.lo << "}" << std::dec;
}

class EeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 32 MB of guest RAM, zeroed. The runtime (PS2Runtime) is not
        // created: these cases only touch registers and plain RAM, and the
        // macros only use the runtime for special addresses (hardware
        // registers), which the tests do not access.
        rdram.assign(PS2_RAM_SIZE, 0);
        ctx = std::make_unique<R5900Context>();
        set64(RA, kReturnAddress);
    }

    void set128(int reg, uint64_t hi, uint64_t lo) {
        ctx->r[reg] = _mm_set_epi64x(static_cast<long long>(hi), static_cast<long long>(lo));
    }
    void set64(int reg, uint64_t v) { set128(reg, 0, v); }
    void setf(int reg, float f) { set64(reg, std::bit_cast<uint32_t>(f)); }
    void setbits(int reg, uint32_t bits) { set64(reg, bits); }

    U128 get128(int reg) const {
        alignas(16) uint64_t v[2];
        _mm_store_si128(reinterpret_cast<__m128i*>(v), ctx->r[reg]);
        return {v[0], v[1]};
    }
    uint64_t get64(int reg) const { return get128(reg).lo; }
    uint32_t get32(int reg) const { return static_cast<uint32_t>(get64(reg)); }
    float getf(int reg) const { return std::bit_cast<float>(get32(reg)); }

    template <typename Fn>
    void run(Fn fn) {
        fn(rdram.data(), ctx.get(), nullptr);
        ASSERT_EQ(ctx->pc, kReturnAddress) << "the function did not return through jr ra";
    }

    std::vector<uint8_t> rdram;
    std::unique_ptr<R5900Context> ctx;
};

// ---------------------------------------------------------------------------
// Integers: 32-bit operations sign-extend to 64 bits
// ---------------------------------------------------------------------------

// PCSX2 R5900OpcodeImpl.cpp ADDU: UD[0] = u64(s64(s32(rs + rt))).
TEST_F(EeTest, AdduSignExtendsTo64Bits) {
    set64(A0, 0x7FFFFFFF);
    set64(A1, 1);
    run(CASE_t_addu_sign_extends);
    EXPECT_EQ(get64(V0), 0xFFFFFFFF80000000ull);
}

// PCSX2 DADDU: full 64-bit add (the carry goes from bit 31 into bit 32).
TEST_F(EeTest, DadduCarriesInto64Bits) {
    set64(A0, 0x00000000FFFFFFFFull);
    set64(A1, 1);
    run(CASE_t_daddu_64bit);
    EXPECT_EQ(get64(V0), 0x0000000100000000ull);
}

// PCSX2 DSRA32: SD[0] = rt.SD[0] >> (sa + 32), arithmetic shift.
TEST_F(EeTest, Dsra32IsArithmetic) {
    set64(A0, 0x8000000000000000ull);
    run(CASE_t_dsra32);  // sa = 4 -> 36 bits
    EXPECT_EQ(get64(V0), 0xFFFFFFFFF8000000ull);
}

// ---------------------------------------------------------------------------
// LQ / SQ: the address is silently aligned to 16 bytes
// ---------------------------------------------------------------------------

// PCSX2 R5900OpcodeImpl.cpp LQ: memRead128(addr & ~0xf). No unaligned
// address exception.
TEST_F(EeTest, LqIgnoresLowFourBits) {
    const uint32_t base = 0x00010000;
    const uint64_t lo = 0x1122334455667788ull, hi = 0x99AABBCCDDEEFF00ull;
    std::memcpy(&rdram[base], &lo, 8);
    std::memcpy(&rdram[base + 8], &hi, 8);
    set64(A0, base);  // the case reads from base + 7
    run(CASE_t_lq_ignores_low_bits);
    EXPECT_EQ(get128(V0), (U128{lo, hi}));
}

// PCSX2 SQ: memWrite128(addr & ~0xf).
TEST_F(EeTest, SqIgnoresLowFourBits) {
    const uint32_t base = 0x00020000;
    set64(A0, base);  // the case writes to base + 9
    set128(A1, 0x0F0E0D0C0B0A0908ull, 0x0706050403020100ull);
    run(CASE_t_sq_ignores_low_bits);
    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, &rdram[base], 8);
    std::memcpy(&hi, &rdram[base + 8], 8);
    EXPECT_EQ(lo, 0x0706050403020100ull);
    EXPECT_EQ(hi, 0x0F0E0D0C0B0A0908ull);
}

// ---------------------------------------------------------------------------
// Branches: the delay slot
// ---------------------------------------------------------------------------
// The cases: v0 = 0; branch; delay slot "v0 += 1"; "v0 += 0x10" only on the
// not-taken path. So: taken = 1, not taken = 0x11 (or 0x10 for "likely").

// Plain BEQ: the delay slot ALWAYS runs.
TEST_F(EeTest, BeqTakenRunsDelaySlot) {
    set64(A0, 5); set64(A1, 5);
    run(CASE_t_beq_delay_slot);
    EXPECT_EQ(get64(V0), 1u);
}
TEST_F(EeTest, BeqNotTakenRunsDelaySlot) {
    set64(A0, 5); set64(A1, 6);
    run(CASE_t_beq_delay_slot);
    EXPECT_EQ(get64(V0), 0x11u);
}

// BEQL ("likely"): the delay slot only runs if the branch is taken.
// PCSX2 R5900OpcodeImpl.cpp BEQL / intDoBranch: not taken skips the delay slot.
TEST_F(EeTest, BeqlTakenRunsDelaySlot) {
    set64(A0, 5); set64(A1, 5);
    run(CASE_t_beql_delay_slot);
    EXPECT_EQ(get64(V0), 1u);
}
TEST_F(EeTest, BeqlNotTakenSkipsDelaySlot) {
    set64(A0, 5); set64(A1, 6);
    run(CASE_t_beql_delay_slot);
    EXPECT_EQ(get64(V0), 0x10u);
}
TEST_F(EeTest, BnelNotTakenSkipsDelaySlot) {
    set64(A0, 5); set64(A1, 5);
    run(CASE_t_bnel_delay_slot);
    EXPECT_EQ(get64(V0), 0x10u);
}

// ---------------------------------------------------------------------------
// FPU (COP1)
// ---------------------------------------------------------------------------

// CVT.W.S truncates toward zero and saturates outside the int32 range.
// PCSX2 FPU.cpp CVT_W: if exponent <= 0x4E800000, (s32)float (truncation
// toward zero); otherwise 0x7FFFFFFF (positive) or 0x80000000 (negative).
TEST_F(EeTest, CvtWsTruncatesTowardZero) {
    setf(A0, 1.75f);
    run(CASE_t_cvt_w_s);
    EXPECT_EQ(static_cast<int32_t>(get32(V0)), 1);
}
TEST_F(EeTest, CvtWsTruncatesNegativeTowardZero) {
    setf(A0, -1.75f);
    run(CASE_t_cvt_w_s);
    EXPECT_EQ(static_cast<int32_t>(get32(V0)), -1);
}
TEST_F(EeTest, CvtWsSaturatesPositive) {
    setf(A0, 3.0e9f);
    run(CASE_t_cvt_w_s);
    EXPECT_EQ(get32(V0), 0x7FFFFFFFu);
}
TEST_F(EeTest, CvtWsSaturatesNegative) {
    setf(A0, -3.0e9f);
    run(CASE_t_cvt_w_s);
    EXPECT_EQ(get32(V0), 0x80000000u);
}

// The PS2 has no infinity: overflow gives the largest value (+-0x7FFFFFFF).
// PCSX2 FPU.cpp ADD_S/MUL_S + checkOverflow: Inf result -> +-Fmax.
TEST_F(EeTest, AddsOverflowGivesFmax) {
    setbits(A0, 0x7F7FFFFF);
    setbits(A1, 0x7F7FFFFF);
    run(CASE_t_add_s);
    EXPECT_EQ(get32(V0), 0x7FFFFFFFu);
}
TEST_F(EeTest, MulsOverflowGivesSignedFmax) {
    setbits(A0, 0xFF7FFFFF);  // -FLT_MAX
    setf(A1, 4.0f);
    run(CASE_t_mul_s);
    EXPECT_EQ(get32(V0), 0xFFFFFFFFu);
}

// 0x7FFFFFFF is NaN on a PC, but an ordinary number (the largest) on the
// PS2: x - x = 0. PCSX2 FPU.cpp SUB_S with fpuDouble (exponent 255 becomes a
// finite number).
TEST_F(EeTest, SubsOfPs2MaxIsZeroNotNaN) {
    setbits(A0, 0x7FFFFFFF);
    setbits(A1, 0x7FFFFFFF);
    run(CASE_t_sub_s);
    EXPECT_EQ(get32(V0), 0x00000000u);
}

// Division by zero: +-Fmax with the sign of (fs xor ft).
// PCSX2 FPU.cpp DIV_S + checkDivideByZero.
TEST_F(EeTest, DivsByZeroGivesSignedFmax) {
    setf(A0, 1.0f);
    setf(A1, 0.0f);
    run(CASE_t_div_s);
    EXPECT_EQ(get32(V0), 0x7FFFFFFFu);
}
TEST_F(EeTest, DivsNegativeByZeroGivesNegativeFmax) {
    setf(A0, -1.0f);
    setf(A1, 0.0f);
    run(CASE_t_div_s);
    EXPECT_EQ(get32(V0), 0xFFFFFFFFu);
}
TEST_F(EeTest, DivsNormal) {
    setf(A0, 1.0f);
    setf(A1, 4.0f);
    run(CASE_t_div_s);
    EXPECT_EQ(getf(V0), 0.25f);
}

// SQRT.S reads its operand from the ft field (plain MIPS would use fs).
// PCSX2 FPU.cpp SQRT_S uses _FtValUl_; the case puts the operand in $f12 (ft)
// and leaves fs = 0, as the game's compiler does (findings/14 s.5).
TEST_F(EeTest, SqrtsReadsFt) {
    setf(A0, 16.0f);  // goes to $f12
    run(CASE_t_sqrt_s);
    EXPECT_EQ(getf(V0), 4.0f);
}
// Square root of a negative: sqrt(|x|). PCSX2 FPU.cpp SQRT_S: sqrt(fabs(...)).
TEST_F(EeTest, SqrtsOfNegativeUsesAbsoluteValue) {
    setf(A0, -16.0f);
    run(CASE_t_sqrt_s);
    EXPECT_EQ(getf(V0), 4.0f);
}

// An operand with exponent 0 (a denormal on a PC) counts as zero on the PS2.
// PCSX2 FPU.cpp fpuDouble: exponent 0 -> zero with the sign.
// DISABLED on purpose: PS2Recomp's runtime itself documents that ADD, SUB
// and MUL do not do this yet (comment in ps2_runtime_macros.h, before
// PS2_FLOAT_NOINLINE). It is a known difference, not a weakened test: see
// docs/KNOWN_ISSUES.md R10.
TEST_F(EeTest, DISABLED_MulsDenormalOperandIsZero) {
    setbits(A0, 0x00000001);  // smallest denormal
    setbits(A1, 0x5D800000);  // 2^60
    run(CASE_t_mul_s);
    EXPECT_EQ(get32(V0) & 0x7FFFFFFFu, 0u);
}

// ---------------------------------------------------------------------------
// MMI (128 bits). rs = a0, rt = a1.
// ---------------------------------------------------------------------------

// PCSX2 MMI.cpp PCPYLD: rd.UD[0] = rt.UD[0]; rd.UD[1] = rs.UD[0].
TEST_F(EeTest, Pcpyld) {
    set128(A0, 0xAAAAAAAAAAAAAAAAull, 0x1111111111111111ull);
    set128(A1, 0xBBBBBBBBBBBBBBBBull, 0x2222222222222222ull);
    run(CASE_t_pcpyld);
    EXPECT_EQ(get128(V0), (U128{0x2222222222222222ull, 0x1111111111111111ull}));
}

// PCSX2 PCPYUD: rd.UD[0] = rs.UD[1]; rd.UD[1] = rt.UD[1].
TEST_F(EeTest, Pcpyud) {
    set128(A0, 0xAAAAAAAAAAAAAAAAull, 0x1111111111111111ull);
    set128(A1, 0xBBBBBBBBBBBBBBBBull, 0x2222222222222222ull);
    run(CASE_t_pcpyud);
    EXPECT_EQ(get128(V0), (U128{0xAAAAAAAAAAAAAAAAull, 0xBBBBBBBBBBBBBBBBull}));
}

// PCSX2 PEXTLW: {rt.w0, rs.w0, rt.w1, rs.w1} (word 0 = the lowest).
TEST_F(EeTest, Pextlw) {
    set128(A0, 0, 0x0000000200000001ull);   // rs: w1=2, w0=1
    set128(A1, 0, 0x0000000B0000000Aull);   // rt: w1=B, w0=A
    run(CASE_t_pextlw);
    EXPECT_EQ(get128(V0), (U128{0x000000010000000Aull, 0x000000020000000Bull}));
}

// PCSX2 _PADDSH: 16-bit add saturating at 0x7FFF / 0x8000.
TEST_F(EeTest, PaddshSaturates) {
    set128(A0, 0, 0x0001'8000'7FFF'7000ull);
    set128(A1, 0, 0x0002'FFFF'0001'1000ull);
    run(CASE_t_paddsh);
    // lanes (bottom to top): 7000+1000=8000 -> 7FFF; 7FFF+1 -> 7FFF;
    // 8000+FFFF(-1) -> 8000 (saturates at -32768); 1+2 = 3.
    EXPECT_EQ(get64(V0), 0x0003'8000'7FFF'7FFFull);
}

// PCSX2 _PSUBUH: unsigned 16-bit subtract, a result <= 0 becomes 0.
TEST_F(EeTest, PsubuhClampsAtZero) {
    set128(A0, 0, 0x0005'0001'FFFF'0010ull);
    set128(A1, 0, 0x0003'0002'0001'0020ull);
    run(CASE_t_psubuh);
    EXPECT_EQ(get64(V0), 0x0002'0000'FFFE'0000ull);
}

// PCSX2 _PADDUH: unsigned 16-bit add, above 0xFFFF becomes 0xFFFF.
TEST_F(EeTest, PadduhSaturates) {
    set128(A0, 0, 0x0001'FFFF'8000'0010ull);
    set128(A1, 0, 0x0002'0001'8000'0020ull);
    run(CASE_t_padduh);
    EXPECT_EQ(get64(V0), 0x0003'FFFF'FFFF'0030ull);
}

// PCSX2 PPACH: even halfwords of rt into the low 4, of rs into the high 4.
TEST_F(EeTest, Ppach) {
    set128(A0, 0x0017'0016'0015'0014ull, 0x0013'0012'0011'0010ull);  // rs
    set128(A1, 0x0007'0006'0005'0004ull, 0x0003'0002'0001'0000ull);  // rt
    run(CASE_t_ppach);
    EXPECT_EQ(get128(V0), (U128{0x0006'0004'0002'0000ull, 0x0016'0014'0012'0010ull}));
}

// PNOR: ~(rs | rt) over all 128 bits.
TEST_F(EeTest, Pnor) {
    set128(A0, 0xF0F0F0F0F0F0F0F0ull, 0x00000000FFFFFFFFull);
    set128(A1, 0x0F0F0F0F00000000ull, 0x0000FFFF00000000ull);
    run(CASE_t_pnor);
    EXPECT_EQ(get128(V0), (U128{0xFFFF000000000000ull, 0x000000000F0F0F0Full}));
}

// PCSX2 MMI.cpp PLZCW: CountLeadingSignBits(word) - 1, for each of the two
// low words. 1 -> 30; -1 -> 31.
TEST_F(EeTest, Plzcw) {
    set64(A0, 0xFFFFFFFF'00000001ull);
    run(CASE_t_plzcw);
    EXPECT_EQ(get64(V0), 0x0000001F'0000001Eull);
}

// Word and halfword permutes (one source, rt = a1). Expected element orders
// from the EE Core Instruction Set Manual; PROT3W, PEXEW and PEXEH also match
// QEMU's target/mips/tx79 implementation. Element 0 is the lowest.
// Words in: w0 = 0x10, w1 = 0x11, w2 = 0x12, w3 = 0x13.
// Halfwords in: h0..h7 = 0..7.
static const uint64_t kWordsLo = 0x00000011'00000010ull, kWordsHi = 0x00000013'00000012ull;
static const uint64_t kHalvesLo = 0x0003'0002'0001'0000ull, kHalvesHi = 0x0007'0006'0005'0004ull;

// PROT3W: the three low words rotate, word 3 stays -> {w1, w2, w0, w3}.
// Ben 10's software skinning (FUN_0015f198) packs vertices with it
// (findings/18 s.12).
TEST_F(EeTest, Prot3wRotatesTheLowThreeWords) {
    set128(A1, kWordsHi, kWordsLo);
    run(CASE_t_prot3w);
    EXPECT_EQ(get128(V0), (U128{0x00000012'00000011ull, 0x00000013'00000010ull}));
}

// PEXEW: words 0 and 2 swap -> {w2, w1, w0, w3}.
TEST_F(EeTest, PexewSwapsEvenWords) {
    set128(A1, kWordsHi, kWordsLo);
    run(CASE_t_pexew);
    EXPECT_EQ(get128(V0), (U128{0x00000011'00000012ull, 0x00000013'00000010ull}));
}

// PEXCW: words 1 and 2 swap -> {w0, w2, w1, w3}.
TEST_F(EeTest, PexcwSwapsCenterWords) {
    set128(A1, kWordsHi, kWordsLo);
    run(CASE_t_pexcw);
    EXPECT_EQ(get128(V0), (U128{0x00000012'00000010ull, 0x00000013'00000011ull}));
}

// PEXEH: in each 64-bit half, halfwords 0 and 2 swap -> {h2, h1, h0, h3}.
TEST_F(EeTest, PexehSwapsEvenHalfwords) {
    set128(A1, kHalvesHi, kHalvesLo);
    run(CASE_t_pexeh);
    EXPECT_EQ(get128(V0), (U128{0x0003'0000'0001'0002ull, 0x0007'0004'0005'0006ull}));
}

// PEXCH: in each 64-bit half, halfwords 1 and 2 swap -> {h0, h2, h1, h3}.
TEST_F(EeTest, PexchSwapsCenterHalfwords) {
    set128(A1, kHalvesHi, kHalvesLo);
    run(CASE_t_pexch);
    EXPECT_EQ(get128(V0), (U128{0x0003'0001'0002'0000ull, 0x0007'0005'0006'0004ull}));
}

// ---------------------------------------------------------------------------
// VU0 macro mode (COP2): VADD with a destination mask
// ---------------------------------------------------------------------------
// vf1 = a0, vf2 = a1, vf3 = a2; VADD.dest vf3, vf1, vf2; v0 = vf3.
// x is the register's lowest 32-bit word.

static uint64_t pack2(float lo, float hi) {
    return (static_cast<uint64_t>(std::bit_cast<uint32_t>(hi)) << 32) | std::bit_cast<uint32_t>(lo);
}

TEST_F(EeTest, VaddAllLanes) {
    set128(A0, pack2(3.0f, 4.0f), pack2(1.0f, 2.0f));        // x=1 y=2 z=3 w=4
    set128(A1, pack2(30.0f, 40.0f), pack2(10.0f, 20.0f));
    set128(A2, pack2(-3.0f, -4.0f), pack2(-1.0f, -2.0f));
    run(CASE_t_vadd_xyzw);
    EXPECT_EQ(get128(V0), (U128{pack2(11.0f, 22.0f), pack2(33.0f, 44.0f)}));
}

// Only x is written; vf3's y, z, w stay as they were.
TEST_F(EeTest, VaddDestMaskXOnly) {
    set128(A0, pack2(3.0f, 4.0f), pack2(1.0f, 2.0f));
    set128(A1, pack2(30.0f, 40.0f), pack2(10.0f, 20.0f));
    set128(A2, pack2(-3.0f, -4.0f), pack2(-1.0f, -2.0f));
    run(CASE_t_vadd_x_only);
    EXPECT_EQ(get128(V0), (U128{pack2(11.0f, -2.0f), pack2(-3.0f, -4.0f)}));
}

// ---------------------------------------------------------------------------
// R13 (findings/14 s.7, KNOWN_ISSUES R13): more R5900-specific
// instructions the game uses. FCR31 bits as in PCSX2 FPU.cpp: C = bit 23,
// I = 0x20000, D = 0x10000, O = 0x8000, U = 0x4000; sticky SI = 0x40,
// SD = 0x20, SO = 0x10, SU = 0x08.
// ---------------------------------------------------------------------------
constexpr uint32_t kFcrC = 0x00800000u;
constexpr uint32_t kFmax = 0x7F7FFFFFu;

// PCSX2 FPU.cpp RSQRT_S: fd = fs / sqrt(ft) for a positive ft (D and I
// cleared first).
TEST_F(EeTest, RsqrtsPositive) {
    setf(A0, 4.0f);
    setf(A1, 16.0f);
    run(CASE_t_rsqrt_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(1.0f));
    EXPECT_EQ(get32(V1), 0u);
}

// PCSX2 RSQRT_S: a negative ft sets I and SI and uses sqrt(|ft|).
// Disabled: the runtime does not keep the FCR31 cause and sticky flags
// (O/U/I/D); the result itself matches. KNOWN_ISSUES R17.
TEST_F(EeTest, DISABLED_RsqrtsNegativeSetsInvalid) {
    setf(A0, 4.0f);
    setf(A1, -16.0f);
    run(CASE_t_rsqrt_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(1.0f));
    EXPECT_EQ(get32(V1), 0x00020040u);
}

// PCSX2 RSQRT_S: ft zero (exponent 0) sets D and SD and gives ft's sign | Fmax.
// Disabled: the runtime follows its hardware-checked model (fs's sign |
// 0x7FFFFFFF, ps2_frsqrt_slow) and keeps no D flag. KNOWN_ISSUES R17, R18.
TEST_F(EeTest, DISABLED_RsqrtsByZeroGivesSignedFmax) {
    setf(A0, 4.0f);
    setbits(A1, 0x80000000u);  // -0
    run(CASE_t_rsqrt_s);
    EXPECT_EQ(get32(V0), 0x80000000u | kFmax);
    EXPECT_EQ(get32(V1), 0x00010020u);
}

// PCSX2 FPU.cpp MADD_S: fd = ACC + fs * ft (ACC set by ADDA.S a2 + 0).
TEST_F(EeTest, MaddsAddsProductToAcc) {
    setf(A0, 2.0f);
    setf(A1, 3.0f);
    setf(A2, 1.5f);
    run(CASE_t_madd_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(7.5f));
    EXPECT_EQ(get32(V1), 0u);
}

// PCSX2 MADD_S + checkOverflow: Fmax * 2 is +inf as a float, which
// fpuDouble reads as Fmax; Fmax + Fmax overflows -> Fmax, O and SO set.
// Disabled: the runtime treats exponent 255 as an ordinary exponent (its
// hardware-checked model) and saturates to 0x7FFFFFFF, with no O flag.
// The game has no MADD.S. KNOWN_ISSUES R17, R18.
TEST_F(EeTest, DISABLED_MaddsOverflowGivesFmax) {
    setbits(A0, kFmax);
    setf(A1, 2.0f);
    setbits(A2, kFmax);
    run(CASE_t_madd_s);
    EXPECT_EQ(get32(V0), kFmax);
    EXPECT_EQ(get32(V1), 0x00008010u);
}

// PCSX2 FPU.cpp MSUB_S: fd = ACC - fs * ft.
TEST_F(EeTest, MsubsSubtractsProductFromAcc) {
    setf(A0, 2.0f);
    setf(A1, 3.0f);
    setf(A2, 10.0f);
    run(CASE_t_msub_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(4.0f));
    EXPECT_EQ(get32(V1), 0u);
}

// PCSX2 FPU.cpp C_EQ (C_cond_S): fpuDouble(fs) == fpuDouble(ft) -> C.
TEST_F(EeTest, CeqsSetsAndClearsC) {
    setf(A0, 1.0f);
    setf(A1, 1.0f);
    run(CASE_t_c_eq_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
    setf(A1, 2.0f);
    run(CASE_t_c_eq_s);
    EXPECT_EQ(get32(V0) & kFcrC, 0u);
}

// PCSX2 C_LT: fs < ft. -0 is not less than +0.
TEST_F(EeTest, CltsComparesLessThan) {
    setf(A0, 1.0f);
    setf(A1, 2.0f);
    run(CASE_t_c_lt_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
    setbits(A0, 0x80000000u);
    setbits(A1, 0x00000000u);
    run(CASE_t_c_lt_s);
    EXPECT_EQ(get32(V0) & kFcrC, 0u);
}

// PCSX2 C_LE: fs <= ft.
TEST_F(EeTest, ClesComparesLessOrEqual) {
    setf(A0, 2.0f);
    setf(A1, 2.0f);
    run(CASE_t_c_le_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
    setf(A0, 3.0f);
    run(CASE_t_c_le_s);
    EXPECT_EQ(get32(V0) & kFcrC, 0u);
}

// PCSX2 C_F: clears C whatever the operands.
TEST_F(EeTest, CfsClearsC) {
    setf(A0, 1.0f);
    run(CASE_t_c_f_s);
    EXPECT_EQ(get32(V0) & kFcrC, 0u);
}

// PCSX2 C_cond_S compares fpuDouble values: exponent 0 is zero and
// exponent 255 is Fmax, so a denormal equals zero and +inf equals Fmax.
TEST_F(EeTest, CeqsDenormalEqualsZero) {
    setbits(A0, 0x00000001u);
    setbits(A1, 0x00000000u);
    run(CASE_t_c_eq_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
}

// Disabled: the runtime orders 0x7F800000 (2^128 in its hardware-checked
// model) above 0x7F7FFFFF, where PCSX2 reads both as Fmax. KNOWN_ISSUES R18.
TEST_F(EeTest, DISABLED_ClesInfinityCountsAsFmax) {
    setbits(A0, 0x7F800000u);  // +inf
    setbits(A1, kFmax);
    run(CASE_t_c_le_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
}

// x/0 leaves 0x7FFFFFFF, which the host reads as NaN; both PCSX2 (fpuDouble
// -> Fmax) and the PS2 format order it above every other positive value.
TEST_F(EeTest, CltsFmaxPatternIsLargest) {
    setf(A0, 1.0f);
    setbits(A1, 0x7FFFFFFFu);
    run(CASE_t_c_lt_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
    setbits(A0, 0xFFFFFFFFu);
    setf(A1, -1.0f);
    run(CASE_t_c_lt_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
}

// PCSX2 C_EQ: -0 and +0 are equal.
TEST_F(EeTest, CeqsNegativeZeroEqualsZero) {
    setbits(A0, 0x80000000u);
    setbits(A1, 0x00000000u);
    run(CASE_t_c_eq_s);
    EXPECT_EQ(get32(V0) & kFcrC, kFcrC);
}

// PCSX2 FPU.cpp MAX_S (fp_max): compares the bits as signed integers, the
// smaller one when both are negative; the result is one operand's bits.
TEST_F(EeTest, MaxsOrdersByBits) {
    setf(A0, 1.0f);
    setbits(A1, 0x7FFFFFFFu);
    run(CASE_t_max_s);
    EXPECT_EQ(get32(V0), 0x7FFFFFFFu);
    setf(A0, -1.0f);
    setf(A1, -2.0f);
    run(CASE_t_max_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(-1.0f));
    setbits(A0, 0x00000000u);
    setbits(A1, 0x80000000u);
    run(CASE_t_max_s);
    EXPECT_EQ(get32(V0), 0x00000000u);
}

// PCSX2 MIN_S (fp_min): the mirror of MAX_S; min(+0, -0) is -0.
TEST_F(EeTest, MinsOrdersByBits) {
    setf(A0, 1.0f);
    setbits(A1, 0x7FFFFFFFu);
    run(CASE_t_min_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(1.0f));
    setf(A0, -1.0f);
    setf(A1, -2.0f);
    run(CASE_t_min_s);
    EXPECT_EQ(get32(V0), std::bit_cast<uint32_t>(-2.0f));
    setbits(A0, 0x00000000u);
    setbits(A1, 0x80000000u);
    run(CASE_t_min_s);
    EXPECT_EQ(get32(V0), 0x80000000u);
}

// ---------------------------------------------------------------------------
// Unaligned 64-bit loads and stores. PCSX2 R5900OpcodeImpl.cpp LDL/LDR/SDL/
// SDR: shift = address & 7, the doubleword at address & ~7, and the tables
// LDL_MASK/LDL_SHIFT (shift 3: keep the low 4 bytes of rt, mem << 32),
// LDR_MASK/LDR_SHIFT (shift 5: keep the high 5 bytes, mem >> 40),
// SDL (shift 2: mem = rt >> 40 | mem & 0xffffffffff000000) and
// SDR (shift 6: mem = rt << 48 | mem & 0x0000ffffffffffff).
// ---------------------------------------------------------------------------
constexpr uint32_t kUnalignedBase = 0x00030000u;

class EeUnalignedTest : public EeTest {
protected:
    void fillBytes() {
        for (uint32_t i = 0; i < 16; ++i)
            rdram[kUnalignedBase + i] = static_cast<uint8_t>(0x10 + i);
    }
    uint64_t read64(uint32_t address) const {
        uint64_t v = 0;
        std::memcpy(&v, &rdram[address], 8);
        return v;
    }
};

TEST_F(EeUnalignedTest, LdlMergesHighBytes) {
    fillBytes();
    set64(A0, kUnalignedBase);  // address base + 3
    set64(A2, 0xAAAAAAAAAAAAAAAAull);
    run(CASE_t_ldl);
    EXPECT_EQ(get64(V0), 0x13121110AAAAAAAAull);
}

TEST_F(EeUnalignedTest, LdrMergesLowBytes) {
    fillBytes();
    set64(A0, kUnalignedBase);  // address base + 5
    set64(A2, 0xAAAAAAAAAAAAAAAAull);
    run(CASE_t_ldr);
    EXPECT_EQ(get64(V0), 0xAAAAAAAAAA171615ull);
}

// ldl v0, 7(a0); ldr v0, 0(a0) loads the 8 bytes at a0, aligned or not.
TEST_F(EeUnalignedTest, LdlLdrPairLoadsUnaligned) {
    fillBytes();
    set64(A0, kUnalignedBase + 3);
    run(CASE_t_ldl_ldr_pair);
    EXPECT_EQ(get64(V0), read64(kUnalignedBase + 3));
    EXPECT_EQ(get64(V0), 0x1A19181716151413ull);
}

TEST_F(EeUnalignedTest, SdlStoresHighBytes) {
    fillBytes();
    set64(A0, kUnalignedBase);  // address base + 2
    set64(A1, 0x8877665544332211ull);
    run(CASE_t_sdl);
    EXPECT_EQ(read64(kUnalignedBase), 0x1716151413887766ull);
    EXPECT_EQ(read64(kUnalignedBase + 8), 0x1F1E1D1C1B1A1918ull);
}

TEST_F(EeUnalignedTest, SdrStoresLowBytes) {
    fillBytes();
    set64(A0, kUnalignedBase);  // address base + 6
    set64(A1, 0x8877665544332211ull);
    run(CASE_t_sdr);
    EXPECT_EQ(read64(kUnalignedBase), 0x2211151413121110ull);
    EXPECT_EQ(read64(kUnalignedBase + 8), 0x1F1E1D1C1B1A1918ull);
}

// sdl a1, 7(a0); sdr a1, 0(a0) stores the 8 bytes of a1 at a0, and nothing else.
TEST_F(EeUnalignedTest, SdlSdrPairStoresUnaligned) {
    fillBytes();
    set64(A0, kUnalignedBase + 3);
    set64(A1, 0x8877665544332211ull);
    run(CASE_t_sdl_sdr_pair);
    EXPECT_EQ(read64(kUnalignedBase + 3), 0x8877665544332211ull);
    EXPECT_EQ(rdram[kUnalignedBase + 2], 0x12u);
    EXPECT_EQ(rdram[kUnalignedBase + 11], 0x1Bu);
}

// ---------------------------------------------------------------------------
// QFSRV and PMFHL
// ---------------------------------------------------------------------------

// The 16 bytes starting at byte `first` of the 32 bytes 0x00..0x1F.
static U128 bytesFrom(uint32_t first) {
    uint8_t b[16];
    for (uint32_t i = 0; i < 16; ++i)
        b[i] = static_cast<uint8_t>(first + i);
    U128 v;
    std::memcpy(&v.lo, b, 8);
    std::memcpy(&v.hi, b + 8, 8);
    return v;
}

// PCSX2 R5900OpcodeImpl.cpp MTSAB: SA = (rs & 0xF) ^ (imm & 0xF).
// PCSX2 MMI.cpp QFSRV: rd = the 128 bits of (rs:rt) shifted right by SA
// bytes (rt the low 128 bits); SA = 0 copies rt.
TEST_F(EeTest, QfsrvFunnelShiftsBytes) {
    const U128 rt = bytesFrom(0x00), rs = bytesFrom(0x10);
    for (uint32_t sa : {0u, 3u, 8u, 10u, 15u}) {
        set128(A1, rt.hi, rt.lo);
        set128(A0, rs.hi, rs.lo);
        set64(A2, sa);
        run(CASE_t_qfsrv);
        EXPECT_EQ(get128(V0), bytesFrom(sa)) << "SA = " << sa;
    }
}

// HI = a1, LO = a0 as four 32-bit words each (word 0 the lowest).
class EePmfhlTest : public EeTest {
protected:
    static constexpr uint32_t kLo[4] = {0x00008000u, 0xFFFF7FFFu, 0x00001234u, 0xFFFFFFFEu};
    static constexpr uint32_t kHi[4] = {0x00000005u, 0x80000000u, 0x7FFFFFFFu, 0x00000000u};
    void load() {
        set128(A0, (uint64_t(kLo[3]) << 32) | kLo[2], (uint64_t(kLo[1]) << 32) | kLo[0]);
        set128(A1, (uint64_t(kHi[3]) << 32) | kHi[2], (uint64_t(kHi[1]) << 32) | kHi[0]);
    }
    static U128 words(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
        return {(uint64_t(w1) << 32) | w0, (uint64_t(w3) << 32) | w2};
    }
    static U128 halves(const uint16_t (&h)[8]) {
        U128 v;
        std::memcpy(&v.lo, h, 8);
        std::memcpy(&v.hi, h + 4, 8);
        return v;
    }
};

// PCSX2 MMI.cpp PMFHL.LW: {LO.w0, HI.w0, LO.w2, HI.w2}.
TEST_F(EePmfhlTest, Lw) {
    load();
    run(CASE_t_pmfhl_lw);
    EXPECT_EQ(get128(V0), words(kLo[0], kHi[0], kLo[2], kHi[2]));
}

// PCSX2 PMFHL.UW: {LO.w1, HI.w1, LO.w3, HI.w3}.
TEST_F(EePmfhlTest, Uw) {
    load();
    run(CASE_t_pmfhl_uw);
    EXPECT_EQ(get128(V0), words(kLo[1], kHi[1], kLo[3], kHi[3]));
}

// PCSX2 PMFHL.SLW: per half, the 64-bit HI.w:LO.w saturated to a signed
// 32-bit value, sign-extended. Half 0: 0x00000005'00008000 is above
// 0x7FFFFFFF -> 0x7FFFFFFF; half 1: 0x7FFFFFFF'00001234 likewise.
TEST_F(EePmfhlTest, SlwSaturates) {
    load();
    run(CASE_t_pmfhl_slw);
    EXPECT_EQ(get128(V0), (U128{0x000000007FFFFFFFull, 0x000000007FFFFFFFull}));
}

// PCSX2 PMFHL.SLW with values in range: LO.w sign-extended.
TEST_F(EeTest, PmfhlSlwInRangeSignExtends) {
    // LO = {0x12345678, x, 0x80000000, x}, HI = {0, x, 0xFFFFFFFF, x}
    set128(A0, 0x0000000080000000ull, 0x0000000012345678ull);
    set128(A1, 0x00000000FFFFFFFFull, 0x0000000000000000ull);
    run(CASE_t_pmfhl_slw);
    EXPECT_EQ(get128(V0), (U128{0x0000000012345678ull, 0xFFFFFFFF80000000ull}));
}

// PCSX2 PMFHL.LH: {LO.h0, LO.h2, HI.h0, HI.h2, LO.h4, LO.h6, HI.h4, HI.h6}.
TEST_F(EePmfhlTest, Lh) {
    load();
    run(CASE_t_pmfhl_lh);
    const uint16_t h[8] = {
        static_cast<uint16_t>(kLo[0]), static_cast<uint16_t>(kLo[1]),
        static_cast<uint16_t>(kHi[0]), static_cast<uint16_t>(kHi[1]),
        static_cast<uint16_t>(kLo[2]), static_cast<uint16_t>(kLo[3]),
        static_cast<uint16_t>(kHi[2]), static_cast<uint16_t>(kHi[3])};
    EXPECT_EQ(get128(V0), halves(h));
}

// PCSX2 PMFHL.SH: the words LO.w0, LO.w1, HI.w0, HI.w1, LO.w2, LO.w3,
// HI.w2, HI.w3 as signed 32-bit values saturated to 16 bits (PMFHL_CLAMP).
TEST_F(EePmfhlTest, ShSaturates) {
    load();
    run(CASE_t_pmfhl_sh);
    // 0x8000 -> 0x7FFF; 0xFFFF7FFF (-32769) -> 0x8000; 5; 0x80000000 -> 0x8000;
    // 0x1234; 0xFFFFFFFE (-2) -> 0xFFFE; 0x7FFFFFFF -> 0x7FFF; 0.
    const uint16_t h[8] = {0x7FFF, 0x8000, 0x0005, 0x8000, 0x1234, 0xFFFE, 0x7FFF, 0x0000};
    EXPECT_EQ(get128(V0), halves(h));
}

// ---------------------------------------------------------------------------
// MMI: HI and LO are 128 bits (context: hi/lo = bits 0-63, hi1/lo1 = bits
// 64-127). Each case loads HI from a2 (PMTHI) and LO from a3 (PMTLO), then
// runs the operation on a0/a1 into v0. Expected values follow PCSX2 MMI.cpp.
// ---------------------------------------------------------------------------
class EeHiLoTest : public EeTest {
protected:
    static U128 words(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
        return {(uint64_t(w1) << 32) | w0, (uint64_t(w3) << 32) | w2};
    }
    static U128 halves(const int16_t (&h)[8]) {
        U128 v;
        std::memcpy(&v.lo, h, 8);
        std::memcpy(&v.hi, h + 4, 8);
        return v;
    }
    void setWords(int reg, uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3) {
        const U128 v = words(w0, w1, w2, w3);
        set128(reg, v.hi, v.lo);
    }
    void setHalves(int reg, const int16_t (&h)[8]) {
        const U128 v = halves(h);
        set128(reg, v.hi, v.lo);
    }
    U128 hiReg() const { return {ctx->hi, ctx->hi1}; }
    U128 loReg() const { return {ctx->lo, ctx->lo1}; }

    // Halfword operands shared by the halfword multiplies. Products:
    // 2, -6, -12, 0x3FFF0001, 0x40000000, 0, -42, -10000.
    void loadHalfOperands() {
        const int16_t s[8] = {1, -2, 3, 0x7FFF, -0x8000, 5, -6, 100};
        const int16_t t[8] = {2, 3, -4, 0x7FFF, -0x8000, 0, 7, -100};
        setHalves(A0, s);
        setHalves(A1, t);
    }
};

// PCSX2 PMULTH: product n goes to LO.w0, LO.w1, HI.w0, HI.w1, LO.w2, LO.w3,
// HI.w2, HI.w3; rd = {LO.w0, HI.w0, LO.w2, HI.w2}.
TEST_F(EeHiLoTest, PmulthKeepsEachProduct) {
    loadHalfOperands();
    setWords(A2, 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD);
    setWords(A3, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
    run(CASE_t_pmulth);
    EXPECT_EQ(loReg(), words(2, 0xFFFFFFFA, 0x40000000, 0));
    EXPECT_EQ(hiReg(), words(0xFFFFFFF4, 0x3FFF0001, 0xFFFFFFD6, 0xFFFFD8F0));
    EXPECT_EQ(get128(V0), words(2, 0xFFFFFFF4, 0x40000000, 0xFFFFFFD6));
}

// PCSX2 PMADDH: each word of HI/LO plus its product (32-bit wrap).
TEST_F(EeHiLoTest, PmaddhAddsEachProductToItsWord) {
    loadHalfOperands();
    setWords(A2, 10, 20, 30, 40);
    setWords(A3, 1, 2, 3, 4);
    run(CASE_t_pmaddh);
    EXPECT_EQ(loReg(), words(3, 0xFFFFFFFC, 0x40000003, 4));
    EXPECT_EQ(hiReg(), words(0xFFFFFFFE, 0x3FFF0015, 0xFFFFFFF4, 0xFFFFD918));
    EXPECT_EQ(get128(V0), words(3, 0xFFFFFFFE, 0x40000003, 0xFFFFFFF4));
}

// PCSX2 PMSUBH: each word of HI/LO minus its product.
TEST_F(EeHiLoTest, PmsubhSubtractsEachProductFromItsWord) {
    loadHalfOperands();
    setWords(A2, 10, 20, 30, 40);
    setWords(A3, 1, 2, 3, 4);
    run(CASE_t_pmsubh);
    EXPECT_EQ(loReg(), words(0xFFFFFFFF, 8, 0xC0000003, 4));
    EXPECT_EQ(hiReg(), words(0x16, 0xC0010013, 0x48, 0x2738));
    EXPECT_EQ(get128(V0), words(0xFFFFFFFF, 0x16, 0xC0000003, 0x48));
}

// PCSX2 PHMADH: per pair, the low word gets odd + even product and the high
// word the odd product; pairs go to LO.w0/1, HI.w0/1, LO.w2/3, HI.w2/3.
TEST_F(EeHiLoTest, PhmadhSumsPairs) {
    loadHalfOperands();
    run(CASE_t_phmadh);
    EXPECT_EQ(loReg(), words(0xFFFFFFFC, 0xFFFFFFFA, 0x40000000, 0));
    EXPECT_EQ(hiReg(), words(0x3FFEFFF5, 0x3FFF0001, 0xFFFFD8C6, 0xFFFFD8F0));
    EXPECT_EQ(get128(V0), words(0xFFFFFFFC, 0x3FFEFFF5, 0x40000000, 0xFFFFD8C6));
}

// PCSX2 PHMSBH: low word odd - even product, high word the complement of
// the odd product (PCSX2: "undocumented behaviour").
TEST_F(EeHiLoTest, PhmsbhSubtractsPairs) {
    loadHalfOperands();
    run(CASE_t_phmsbh);
    EXPECT_EQ(loReg(), words(0xFFFFFFF8, 5, 0xC0000000, 0xFFFFFFFF));
    EXPECT_EQ(hiReg(), words(0x3FFF000D, 0xC000FFFE, 0xFFFFD91A, 0x270F));
    EXPECT_EQ(get128(V0), words(0xFFFFFFF8, 0x3FFF000D, 0xC0000000, 0xFFFFD91A));
}

// PCSX2 PMULTW: words 0 and 2, signed 64-bit products; LO.dd/HI.dd get the
// sign-extended halves, rd.dd the product.
TEST_F(EeHiLoTest, PmultwMultipliesBothLanes) {
    setWords(A0, 0x7FFFFFFF, 0x11111111, 0xFFFFFFFE, 0x22222222);
    setWords(A1, 0x7FFFFFFF, 0x33333333, 3, 0x44444444);
    run(CASE_t_pmultw);
    EXPECT_EQ(loReg(), (U128{1, 0xFFFFFFFFFFFFFFFAull}));
    EXPECT_EQ(hiReg(), (U128{0x3FFFFFFF, 0xFFFFFFFFFFFFFFFFull}));
    EXPECT_EQ(get128(V0), (U128{0x3FFFFFFF00000001ull, 0xFFFFFFFFFFFFFFFAull}));
}

// PCSX2 PMULTUW: as PMULTW, unsigned.
TEST_F(EeHiLoTest, PmultuwMultipliesBothLanesUnsigned) {
    setWords(A0, 0x7FFFFFFF, 0x11111111, 0xFFFFFFFE, 0x22222222);
    setWords(A1, 0x7FFFFFFF, 0x33333333, 3, 0x44444444);
    run(CASE_t_pmultuw);
    EXPECT_EQ(loReg(), (U128{1, 0xFFFFFFFFFFFFFFFAull}));
    EXPECT_EQ(hiReg(), (U128{0x3FFFFFFF, 2}));
    EXPECT_EQ(get128(V0), (U128{0x3FFFFFFF00000001ull, 0x00000002FFFFFFFAull}));
}

// PCSX2 PMADDUW: {HI.w(ss), LO.w(ss)} + rs.w(ss) * rt.w(ss) for ss = 0, 2.
TEST_F(EeHiLoTest, PmadduwAccumulatesBothLanes) {
    setWords(A0, 0x10, 0x5555, 0xFFFFFFFF, 0x6666);
    setWords(A1, 0x10, 0x7777, 0xFFFFFFFF, 0x8888);
    setWords(A2, 1, 0x9999, 0, 0xAAAA);
    setWords(A3, 0xFFFFFFFF, 0xBBBB, 5, 0xCCCC);
    run(CASE_t_pmadduw);
    EXPECT_EQ(loReg(), (U128{0xFF, 6}));
    EXPECT_EQ(hiReg(), (U128{2, 0xFFFFFFFFFFFFFFFEull}));
    EXPECT_EQ(get128(V0), (U128{0x00000002000000FFull, 0xFFFFFFFE00000006ull}));
}

// PCSX2 PMADDW, with values where its HI quirks give the plain result:
// lane 0: 3 * 4 + {0, 5} = {0, 17}; lane 1: 0x10000^2 + {1, -1} = {2, -1}.
TEST_F(EeHiLoTest, PmaddwAccumulatesBothLanes) {
    setWords(A0, 3, 0x1111, 0x10000, 0x2222);
    setWords(A1, 4, 0x3333, 0x10000, 0x4444);
    setWords(A2, 0, 0x5555, 1, 0x6666);
    setWords(A3, 5, 0x7777, 0xFFFFFFFF, 0x8888);
    run(CASE_t_pmaddw);
    EXPECT_EQ(loReg(), (U128{0x11, 0xFFFFFFFFFFFFFFFFull}));
    EXPECT_EQ(hiReg(), (U128{0, 2}));
    EXPECT_EQ(get128(V0), words(0x11, 0, 0xFFFFFFFF, 2));
}

// PCSX2 PMSUBW: lane 0: {0, 100} - 3 * 4 = {0, 88}; lane 1: {0, 0} - 2 * -5 = {0, 10}.
TEST_F(EeHiLoTest, PmsubwSubtractsBothLanes) {
    setWords(A0, 3, 0x1111, 2, 0x2222);
    setWords(A1, 4, 0x3333, 0xFFFFFFFB, 0x4444);
    setWords(A2, 0, 0x5555, 0, 0x6666);
    setWords(A3, 100, 0x7777, 0, 0x8888);
    run(CASE_t_pmsubw);
    EXPECT_EQ(loReg(), (U128{88, 10}));
    EXPECT_EQ(hiReg(), (U128{0, 0}));
    EXPECT_EQ(get128(V0), words(88, 0, 10, 0));
}

// PCSX2 PDIVW: lanes 0 and 2; 0x80000000 / -1 gives LO = 0x80000000, HI = 0.
// rd is not written.
TEST_F(EeHiLoTest, PdivwDividesBothLanes) {
    setWords(A0, 7, 0x1111, 0x80000000, 0x2222);
    setWords(A1, 0xFFFFFFFE, 0x3333, 0xFFFFFFFF, 0x4444);
    set128(V0, 0x1234, 0x5678);
    run(CASE_t_pdivw);
    EXPECT_EQ(loReg(), (U128{0xFFFFFFFFFFFFFFFDull, 0xFFFFFFFF80000000ull}));
    EXPECT_EQ(hiReg(), (U128{1, 0}));
    EXPECT_EQ(get128(V0), (U128{0x5678, 0x1234}));
}

// PCSX2 PDIVUW: lanes 0 and 2, sign-extended results; by zero: LO = -1, HI = rs.
TEST_F(EeHiLoTest, PdivuwDividesBothLanes) {
    setWords(A0, 0xF0000000, 0x1111, 9, 0x2222);
    setWords(A1, 1, 0x3333, 0, 0x4444);
    run(CASE_t_pdivuw);
    EXPECT_EQ(loReg(), (U128{0xFFFFFFFFF0000000ull, 0xFFFFFFFFFFFFFFFFull}));
    EXPECT_EQ(hiReg(), (U128{0, 9}));
}

// PCSX2 PDIVBW: each word of rs by the signed halfword rt.h0 (-3 here).
TEST_F(EeHiLoTest, PdivbwDividesEachWord) {
    setWords(A0, 100, 0xFFFFFF9C, 0x80000000, 7);
    setWords(A1, 0x1234FFFD, 0x5555, 0x6666, 0x7777);
    run(CASE_t_pdivbw);
    EXPECT_EQ(loReg(), words(0xFFFFFFDF, 0x21, 0x2AAAAAAA, 0xFFFFFFFE));
    EXPECT_EQ(hiReg(), words(1, 0xFFFFFFFF, 0xFFFFFFFE, 1));
}

// PCSX2 PMTHI / PMFHI: all 128 bits.
TEST_F(EeHiLoTest, PmthiPmfhiMove128Bits) {
    set128(A0, 0x3333333344444444ull, 0x1111111122222222ull);
    run(CASE_t_pmthi_pmfhi);
    EXPECT_EQ(hiReg(), (U128{0x1111111122222222ull, 0x3333333344444444ull}));
    EXPECT_EQ(get128(V0), (U128{0x1111111122222222ull, 0x3333333344444444ull}));
}

// PCSX2 PMTLO / PMFLO: all 128 bits.
TEST_F(EeHiLoTest, PmtloPmfloMove128Bits) {
    set128(A0, 0x7777777788888888ull, 0x5555555566666666ull);
    run(CASE_t_pmtlo_pmflo);
    EXPECT_EQ(loReg(), (U128{0x5555555566666666ull, 0x7777777788888888ull}));
    EXPECT_EQ(get128(V0), (U128{0x5555555566666666ull, 0x7777777788888888ull}));
}

// PCSX2 PMTHL.LW: LO.w0 = rs.w0, HI.w0 = rs.w1, LO.w2 = rs.w2, HI.w2 = rs.w3;
// words 1 and 3 of HI and LO stay.
TEST_F(EeHiLoTest, PmthlLwWritesEvenWords) {
    setWords(A0, 0xA0, 0xA1, 0xA2, 0xA3);
    setWords(A2, 0xB0, 0xB1, 0xB2, 0xB3);
    setWords(A3, 0xC0, 0xC1, 0xC2, 0xC3);
    run(CASE_t_pmthl_lw);
    EXPECT_EQ(loReg(), words(0xA0, 0xC1, 0xA2, 0xC3));
    EXPECT_EQ(hiReg(), words(0xA1, 0xB1, 0xA3, 0xB3));
}

}  // namespace
