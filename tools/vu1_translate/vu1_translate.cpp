// vu1_translate.cpp: translates the game's VU microprograms into C++ at build time (D14, findings/21).
//
// WHAT IT IS FOR
//   The VU1 microprograms run in PS2Recomp's VU1 interpreter (KNOWN_ISSUES
//   T2), the largest cost of the game thread in the first level
//   (findings/20 s.6, s.14). The programs are a closed list in the ELF
//   (findings/11), so they can be translated before the game runs.
//
// WHAT IT DOES (phase 1: same results as the interpreter by construction)
//   1. Reads the ELF's .DVP.ovlytab (entries of 12 bytes: name offset, LMA
//      in .vutext, VMA in VU micro memory) and .vutext. Each entry's chunk
//      runs from its LMA to the next entry's LMA, at most 0x800 bytes (the
//      8 bytes between two chunks of a group are the VIF tag of the next
//      one; a group's last chunk ends with a few bytes that are not code,
//      which are translated too and simply never match at run time).
//   2. Decodes every 64-bit instruction pair with the interpreter's own
//      decoder (VU1NativeAccess::decode), once per distinct (address, pair).
//   3. Writes, for each distinct upper word, a function that runs that one
//      upper instruction (the interpreter's upper executor, forced inline
//      with the word as a constant: only that instruction's code is kept,
//      with its FMAC result and flag logic), and, for each pair, the decoded
//      pair as a constexpr object and a function that runs the interpreter's
//      per-pair step (forced inline) with it and with its upper word's
//      function. The compiler drops every branch the step takes on the
//      decoded fields. The upper functions are shared: 6,727 pairs use
//      about 1,600 distinct upper words, which keeps the build time down.
//      Likewise one function per distinct lower word (the lower executor,
//      runtime/ps2_vu1_lower_inl.h, inlined for that word).
//      Everything is spread over kFileCount (32) files (parallel
//      compilation) plus an index file with the tables of src/vu/vu1_native.h.
//
// USAGE
//   ben10_vu1_translate <game ELF> <output folder>
//   CMake runs it (target ben10_vu1_native) into generated/vu1/. The output
//   is the game's code, translated: never versioned.

#include "runtime/ps2_vu1.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    // Many small files: MSVC compiles each translated file slowly (every pair
    // inlines the step), so the work is spread over as many cores as there are.
    constexpr int kFileCount = 32;
    constexpr uint32_t kChunkMax = 0x800u;
    constexpr uint32_t kVuCodeSize = 0x4000u;

    struct Section
    {
        std::string name;
        uint32_t addr = 0, offset = 0, size = 0;
    };

    uint32_t u32(const std::vector<uint8_t> &d, size_t o)
    {
        uint32_t v = 0;
        std::memcpy(&v, d.data() + o, 4);
        return v;
    }
    uint16_t u16(const std::vector<uint8_t> &d, size_t o)
    {
        uint16_t v = 0;
        std::memcpy(&v, d.data() + o, 2);
        return v;
    }

    std::vector<Section> readSections(const std::vector<uint8_t> &elf)
    {
        // ELF32 header: e_shoff at 0x20, e_shentsize 0x2E, e_shnum 0x30, e_shstrndx 0x32.
        const uint32_t shoff = u32(elf, 0x20);
        const uint16_t shentsize = u16(elf, 0x2E), shnum = u16(elf, 0x30), shstrndx = u16(elf, 0x32);
        std::vector<Section> out(shnum);
        for (uint16_t i = 0; i < shnum; ++i)
        {
            const size_t h = shoff + static_cast<size_t>(i) * shentsize;
            out[i].name = std::to_string(u32(elf, h)); // name offset, resolved below
            out[i].addr = u32(elf, h + 0x0C);
            out[i].offset = u32(elf, h + 0x10);
            out[i].size = u32(elf, h + 0x14);
        }
        const uint32_t strOff = out[shstrndx].offset;
        for (auto &s : out)
        {
            const char *n = reinterpret_cast<const char *>(elf.data() + strOff + std::stoul(s.name));
            s.name = n;
        }
        return out;
    }

    const Section *find(const std::vector<Section> &secs, const char *name)
    {
        for (const auto &s : secs)
            if (s.name == name)
                return &s;
        return nullptr;
    }

    void usage(std::ostream &o, const VU1Interpreter::InstructionUsage &u)
    {
        o << "{.vfRead = {{{" << +u.vfRead[0].reg << "u, " << +u.vfRead[0].lanes << "u}, {"
          << +u.vfRead[1].reg << "u, " << +u.vfRead[1].lanes << "u}}}, "
          << ".vfWrite = {" << +u.vfWrite.reg << "u, " << +u.vfWrite.lanes << "u}, "
          << ".vfReadCount = " << +u.vfReadCount << "u, "
          << ".viRead = 0x" << std::hex << u.viRead << "u, .viWrite = 0x" << u.viWrite << std::dec << "u, "
          << ".accRead = " << +u.accRead << "u, .accWrite = " << +u.accWrite << "u, "
          << ".latency = " << +u.latency << "u, .vfLatency = " << +u.vfLatency << "u, .viLatency = " << +u.viLatency << "u, "
          << ".pipeline = static_cast<VU1Interpreter::Pipeline>(" << +static_cast<uint8_t>(u.pipeline) << "), "
          << ".waitQ = " << (u.waitQ ? "true" : "false")
          << ", .waitP = " << (u.waitP ? "true" : "false")
          << ", .readsClip = " << (u.readsClip ? "true" : "false")
          << ", .writesClip = " << (u.writesClip ? "true" : "false")
          << ", .delaysNextBranchRead = " << (u.delaysNextBranchRead ? "true" : "false")
          << ", .reserved = " << (u.reserved ? "true" : "false") << "}";
    }
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s <game ELF> <output folder>\n", argv[0]);
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in)
    {
        std::fprintf(stderr, "vu1_translate: cannot open %s\n", argv[1]);
        return 1;
    }
    const std::vector<uint8_t> elf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto secs = readSections(elf);
    const Section *tab = find(secs, ".DVP.ovlytab");
    const Section *vutext = find(secs, ".vutext");
    if (!tab || !vutext)
    {
        std::fprintf(stderr, "vu1_translate: no .DVP.ovlytab or .vutext in %s\n", argv[1]);
        return 1;
    }

    // Overlay entries: +0 name offset, +4 LMA, +8 VMA.
    struct Entry
    {
        uint32_t lma, vma;
    };
    std::vector<Entry> entries;
    std::set<uint32_t> lmas;
    for (uint32_t o = 0; o + 12 <= tab->size; o += 12)
    {
        const Entry e{u32(elf, tab->offset + o + 4), u32(elf, tab->offset + o + 8)};
        entries.push_back(e);
        lmas.insert(e.lma);
    }
    const uint32_t vutextEnd = vutext->addr + vutext->size;

    // Distinct (address, lower, upper) pairs, in a stable order.
    std::set<std::tuple<uint32_t, uint32_t, uint32_t>> pairs;
    for (const Entry &e : entries)
    {
        auto next = lmas.upper_bound(e.lma);
        const uint32_t end = next != lmas.end() ? *next : vutextEnd;
        const uint32_t size = std::min(kChunkMax, end - e.lma);
        for (uint32_t p = 0; p + 8 <= size; p += 8)
        {
            const uint32_t pc = e.vma + p;
            if (pc + 8 > kVuCodeSize)
                break;
            const size_t off = vutext->offset + (e.lma - vutext->addr) + p;
            pairs.emplace(pc, u32(elf, off), u32(elf, off + 4));
        }
    }

    const std::filesystem::path outDir(argv[2]);
    std::filesystem::create_directories(outDir);
    std::vector<std::ostringstream> files(kFileCount);
    std::vector<std::ostringstream> tables(kFileCount);
    std::vector<size_t> counts(kFileCount, 0);
    for (auto &f : files)
        f << "// Generated by tools/vu1_translate from the game's ELF: do not edit, do not version.\n"
             "#include \"vu/vu1_native.h\"\n"
             "#include \"runtime/ps2_vu1_lower_inl.h\"\n\n"
             "#if defined(_MSC_VER)\n#define BEN10_VU1_NOINLINE __declspec(noinline)\n#else\n"
             "#define BEN10_VU1_NOINLINE __attribute__((noinline))\n#endif\n\n"
             "@UPPER_DECLS@"
             "namespace\n{\n"
             "    using Pair = VU1NativeAccess::Pair;\n"
             "    using Frame = VU1NativeAccess::Frame;\n"
             "    using Result = VU1NativeAccess::Result;\n";

    // Distinct upper words, each with the file that defines its function.
    std::map<uint32_t, int> upperFile;
    for (const auto &[pc, lower, upper] : pairs)
        if (!upperFile.count(upper))
        {
            const int f = static_cast<int>(upperFile.size() % kFileCount);
            upperFile.emplace(upper, f);
        }
    std::vector<std::ostringstream> upperDecls(kFileCount);
    std::vector<std::ostringstream> lowerDecls(kFileCount);
    std::vector<std::set<uint32_t>> usedUppers(kFileCount);
    for (const auto &[upper, f] : upperFile)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "u%08X", upper);
        files[f] << "\n    // upper 0x" << std::hex << upper << std::dec << "\n"
                 << "    }\n    namespace ben10::vu1_native::upper\n    {\n"
                 << "    BEN10_VU1_NOINLINE void " << name << "(VU1Interpreter &vu) { VU1NativeAccess::execUpperWord<0x"
                 << std::hex << upper << std::dec << "u>(vu); }\n"
                 << "    }\n    namespace\n    {\n";
    }

    // Distinct lower words (pairs with the I bit hold an immediate there, not
    // an instruction), each with the file that defines its function.
    std::map<uint32_t, int> lowerFile;
    for (const auto &[pc, lower, upper] : pairs)
        if ((upper & 0x80000000u) == 0u && !lowerFile.count(lower))
        {
            const int f = static_cast<int>(lowerFile.size() % kFileCount);
            lowerFile.emplace(lower, f);
        }
    std::vector<std::set<uint32_t>> usedLowers(kFileCount);
    for (const auto &[lower, f] : lowerFile)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "l%08X", lower);
        files[f] << "\n    // lower 0x" << std::hex << lower << std::dec << "\n"
                 << "    }\n    namespace ben10::vu1_native::lower\n    {\n"
                 << "    BEN10_VU1_NOINLINE void " << name
                 << "(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upper)"
                 << " { VU1NativeLowerAccess::execLowerWord<0x" << std::hex << lower << std::dec
                 << "u>(vu, vuData, dataSize, gs, memory, upper); }\n"
                 << "    }\n    namespace\n    {\n";
    }

    VU1Interpreter vu(VU1Interpreter::Unit::VU1);
    // The same pairs decoded as VU0 code: a pair may run on VU0 only if it
    // decodes the same there (the EFU instructions, XGKICK and opcode 0x64
    // do not exist on VU0 and decode as reserved).
    VU1Interpreter vu0(VU1Interpreter::Unit::VU0);
    std::vector<uint8_t> code(kVuCodeSize, 0);
    size_t n = 0;
    size_t vu0Pairs = 0; // pairs that may also run on VU0
    for (const auto &[pc, lower, upper] : pairs)
    {
        std::memcpy(code.data() + pc, &lower, 4);
        std::memcpy(code.data() + pc + 4, &upper, 4);
        const VU1Interpreter::DecodedInstructionPair d = VU1NativeAccess::decode(vu, code.data(), pc);
        bool vu0Same = false;
        if (pc + 8u <= 0x1000u) // VU0 micro memory is 4 KB
        {
            const VU1Interpreter::DecodedInstructionPair d0 = VU1NativeAccess::decode(vu0, code.data(), pc);
            std::ostringstream a, b;
            usage(a, d.lowerUsage);
            usage(a, d.upperUsage);
            usage(b, d0.lowerUsage);
            usage(b, d0.upperUsage);
            vu0Same = a.str() == b.str() && d.iBit == d0.iBit && d.eBit == d0.eBit && d.mBit == d0.mBit &&
                      d.dBit == d0.dBit && d.tBit == d0.tBit && d.upperVfShadowReg == d0.upperVfShadowReg &&
                      d.suppressedLowerVf == d0.suppressedLowerVf;
            if (vu0Same)
                ++vu0Pairs;
        }
        const int f = static_cast<int>(n % kFileCount);
        char id[32];
        std::snprintf(id, sizeof(id), "p%04X_%zu", pc, n);
        std::ostringstream &o = files[f];
        o << "\n    // 0x" << std::hex << pc << ": lower 0x" << lower << ", upper 0x" << upper << std::dec << "\n";
        o << "    constexpr Pair k_" << id << "{.lower = 0x" << std::hex << d.lower << "u, .upper = 0x" << d.upper << std::dec << "u,\n"
          << "        .lowerUsage = ";
        usage(o, d.lowerUsage);
        o << ",\n        .upperUsage = ";
        usage(o, d.upperUsage);
        o << ",\n        .iBit = " << (d.iBit ? "true" : "false") << ", .eBit = " << (d.eBit ? "true" : "false")
          << ", .mBit = " << (d.mBit ? "true" : "false") << ", .dBit = " << (d.dBit ? "true" : "false")
          << ", .tBit = " << (d.tBit ? "true" : "false")
          << ", .upperVfShadowReg = " << +d.upperVfShadowReg << "u, .suppressedLowerVf = " << +d.suppressedLowerVf << "u};\n";
        char upperName[32];
        std::snprintf(upperName, sizeof(upperName), "u%08X", upper);
        if (usedUppers[f].insert(upper).second && upperFile[upper] != f)
            upperDecls[f] << "    void " << upperName << "(VU1Interpreter &vu);\n";
        if ((upper & 0x80000000u) != 0u)
        {
            // I bit: the lower word is an immediate, no lower instruction.
            o << "    Result " << id << "(VU1Interpreter &vu, Frame &frame) { return VU1NativeAccess::execPairUpper<&ben10::vu1_native::upper::"
              << upperName << ", k_" << id << ">(vu, k_" << id << ", frame); }\n";
        }
        else
        {
            char lowerName[32];
            std::snprintf(lowerName, sizeof(lowerName), "l%08X", lower);
            if (usedLowers[f].insert(lower).second && lowerFile[lower] != f)
                lowerDecls[f] << "    void " << lowerName
                              << "(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upper);\n";
            o << "    Result " << id << "(VU1Interpreter &vu, Frame &frame) { return VU1NativeAccess::execPairUpperLower<&ben10::vu1_native::upper::"
              << upperName << ", &ben10::vu1_native::lower::" << lowerName << ", k_" << id << ">(vu, k_" << id << ", frame); }\n";
        }
        tables[f] << "        {0x" << std::hex << pc << "u, 0x" << lower << "u, 0x" << upper << std::dec << "u, &" << id
                  << ", " << (vu0Same ? "true" : "false") << "},\n";
        ++counts[f];
        ++n;
    }

    std::ostringstream index;
    index << "// Generated by tools/vu1_translate from the game's ELF: do not edit, do not version.\n"
             "#include \"vu/vu1_native.h\"\n\nnamespace ben10::vu1_native\n{\n";
    for (int f = 0; f < kFileCount; ++f)
    {
        files[f] << "}\n\nnamespace ben10::vu1_native\n{\n    extern const NativePair kPairs" << f << "[];\n"
                 << "    const NativePair kPairs" << f << "[] = {\n" << tables[f].str() << "    };\n}\n";
        index << "    extern const NativePair kPairs" << f << "[];\n";
        std::string text = files[f].str();
        const std::string decls = "namespace ben10::vu1_native::upper\n{\n" + upperDecls[f].str() + "}\n"
                                  "namespace ben10::vu1_native::lower\n{\n" + lowerDecls[f].str() + "}\n\n";
        text.replace(text.find("@UPPER_DECLS@"), 13, decls);
        std::ofstream(outDir / ("vu1_native_" + std::to_string(f) + ".cpp")) << text;
    }
    index << "    const NativeTable kTables[] = {\n";
    for (int f = 0; f < kFileCount; ++f)
        index << "        {kPairs" << f << ", " << counts[f] << "u},\n";
    index << "    };\n    const size_t kTableCount = " << kFileCount << ";\n}\n";
    std::ofstream(outDir / "vu1_native_index.cpp") << index.str();

    std::printf("vu1_translate: %zu overlay entries, %zu distinct pairs (%zu also valid on VU0), %zu upper and %zu lower words -> %s\n",
                entries.size(), n, vu0Pairs, upperFile.size(), lowerFile.size(), outDir.string().c_str());
    return 0;
}
