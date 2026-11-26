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
//   4. Splits each chunk into blocks (findings/21 s.24): straight-line runs
//      of pairs from a leader (chunk start, static branch target, after a
//      branch's or an E bit's delay slot) up to a branch, an E/D/T pair or
//      the next leader. A block function runs its body pairs one after
//      another (VU1NativeAccess::blockPair*: no PC/branch/end tail, and a
//      hazard check only on the read lanes not settled at build time, see
//      MaskedTiming) and its last pair through the ordinary step. 32 more
//      files, vu1_native_blocks_N.cpp, with the block tables.
//   5. Marks the body pairs whose FMAC flag result (MAC and status) no flag
//      instruction can read before the next FMAC result replaces it
//      (findings/20 s.28): they run with kElide, and their flag entry is
//      left out when it could change nothing (its only sticky bits, Z and
//      S, already set). Such a block starts only when its body cannot run
//      out of the run's cycle budget (blockCanEnterWithin).
//
// USAGE
//   ben10_vu1_translate <game ELF> <output folder>
//   CMake runs it (target ben10_vu1_native) into generated/vu1/. The output
//   is the game's code, translated: never versioned.

#include "runtime/ps2_vu1.h"

#include <algorithm>
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
    // Per pair, for the blocks below: its decode and whether it is valid on VU0.
    struct PairInfo
    {
        VU1Interpreter::DecodedInstructionPair decoded;
        bool vu0 = false;
    };
    std::map<std::tuple<uint32_t, uint32_t, uint32_t>, PairInfo> pairInfo;
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
        pairInfo[{pc, lower, upper}] = PairInfo{d, vu0Same};
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

    // The block bounds (blockCanEnterWithin) assume no result takes longer
    // than kMaxPipelineLatency cycles.
    for (const auto &[key, info] : pairInfo)
    {
        const auto &d = info.decoded;
        const uint32_t latency = std::max<uint32_t>({d.upperUsage.latency, d.upperUsage.vfLatency, d.lowerUsage.latency,
                                                     d.lowerUsage.vfLatency, d.lowerUsage.viLatency});
        if (latency > VU1NativeAccess::kMaxPipelineLatency)
        {
            std::fprintf(stderr, "vu1_translate: pair at 0x%x has latency %u, above kMaxPipelineLatency\n",
                         std::get<0>(key), latency);
            return 1;
        }
    }

    // ---- Blocks (findings/21 s.24) ----------------------------------------
    // A block is a straight-line run of pairs of one overlay chunk, starting
    // at a leader: the chunk's first pair, a static branch target, or the
    // pair after a branch's delay slot or after an E bit's delay slot (where
    // the run loop next dispatches). Its body pairs (no branch, no E/D/T
    // bit, no reserved instruction) run inline one after another
    // (VU1NativeAccess::blockPair*); its last pair runs through the
    // ordinary per-pair step, which sets the PC and handles branches and
    // ends. A block needs at least one body pair.
    constexpr size_t kBlockMax = 64; // pairs per block
    struct Block
    {
        uint32_t pc = 0;
        std::vector<std::pair<uint32_t, uint32_t>> words; // (lower, upper) per pair
        bool vu0 = true;
    };
    std::map<std::pair<uint32_t, std::vector<std::pair<uint32_t, uint32_t>>>, Block> blocks;
    for (const Entry &e : entries)
    {
        auto next = lmas.upper_bound(e.lma);
        const uint32_t end = next != lmas.end() ? *next : vutextEnd;
        const uint32_t size = std::min(kChunkMax, end - e.lma);
        struct Item
        {
            uint32_t pc, lower, upper;
            const PairInfo *info;
        };
        std::vector<Item> seq;
        for (uint32_t p = 0; p + 8 <= size; p += 8)
        {
            const uint32_t pc = e.vma + p;
            if (pc + 8 > kVuCodeSize)
                break;
            const size_t off = vutext->offset + (e.lma - vutext->addr) + p;
            const uint32_t lower = u32(elf, off), upper = u32(elf, off + 4);
            seq.push_back({pc, lower, upper, &pairInfo.at({pc, lower, upper})});
        }
        const auto isBranch = [](const Item &it)
        { return it.info->decoded.lowerUsage.pipeline == VU1Interpreter::PipelineBranch; };
        const auto endsBody = [&](const Item &it)
        {
            const auto &d = it.info->decoded;
            return isBranch(it) || d.eBit || d.dBit || d.tBit || d.upperUsage.reserved || d.lowerUsage.reserved;
        };
        std::set<size_t> leaders{0u};
        for (size_t i = 0; i < seq.size(); ++i)
        {
            const auto &d = seq[i].info->decoded;
            if (isBranch(seq[i]) || d.eBit)
                leaders.insert(i + 2);
            if (d.dBit || d.tBit || d.upperUsage.reserved || d.lowerUsage.reserved)
                leaders.insert(i + 1);
            // B, BAL and the IBxx compares: PC-relative, known here (JR/JALR
            // are not). target = pc + 8 + imm11 * 8, as the lower executor.
            const uint32_t op = seq[i].lower >> 25;
            if ((seq[i].upper & 0x80000000u) == 0u && isBranch(seq[i]) && op != 0x24u && op != 0x25u)
            {
                int32_t imm = static_cast<int32_t>(seq[i].lower & 0x7FFu);
                if (imm & 0x400)
                    imm -= 0x800;
                const uint32_t target = (seq[i].pc + 8u + static_cast<uint32_t>(imm * 8)) & 0x3FFFu;
                if (target >= seq.front().pc && target <= seq.back().pc)
                    leaders.insert((target - seq.front().pc) / 8u);
            }
        }
        for (size_t l : leaders)
        {
            if (l >= seq.size())
                continue;
            // Body: l, l+1, ... while the pair can be a body pair and the
            // next one exists, is not a leader and the block is not full.
            size_t last = l;
            while (last + 1 < seq.size() && !endsBody(seq[last]) && !leaders.count(last + 1) &&
                   last + 1 - l + 1 <= kBlockMax)
                ++last;
            if (endsBody(seq[last]) && (seq[last].info->decoded.upperUsage.reserved || seq[last].info->decoded.lowerUsage.reserved))
                --last; // never run a reserved pair from a block (falls back)
            if (last <= l)
                continue; // no body pair: nothing to gain
            Block b;
            b.pc = seq[l].pc;
            for (size_t i = l; i <= last; ++i)
            {
                b.words.emplace_back(seq[i].lower, seq[i].upper);
                b.vu0 = b.vu0 && seq[i].info->vu0;
            }
            auto key = std::make_pair(b.pc, b.words);
            blocks.emplace(std::move(key), std::move(b));
        }
    }

    // Block files: the decoded pairs again as constants (each file is its
    // own translation unit), one function per block, and the block table.
    std::vector<std::ostringstream> blockFiles(kFileCount);
    std::vector<std::ostringstream> blockTables(kFileCount);
    std::vector<std::set<uint32_t>> blockUppers(kFileCount), blockLowers(kFileCount);
    std::vector<size_t> blockCounts(kFileCount, 0);
    size_t blockPairs = 0;
    size_t elidedPairs = 0;
    // Whether an upper word queues an FMAC flag entry, asked once per word
    // of the interpreter itself (VU1NativeAccess::upperQueuesFmacFlags).
    VU1Interpreter probe(VU1Interpreter::Unit::VU1);
    std::map<uint32_t, bool> queuesFmacMemo;
    const auto queuesFmac = [&](uint32_t upper)
    {
        auto it = queuesFmacMemo.find(upper);
        if (it == queuesFmacMemo.end())
            it = queuesFmacMemo.emplace(upper, VU1NativeAccess::upperQueuesFmacFlags(probe, upper)).first;
        return it->second;
    };
    size_t b = 0;
    for (const auto &[key, block] : blocks)
    {
        const int f = static_cast<int>(b % kFileCount);
        std::ostringstream &o = blockFiles[f];
        char bid[32];
        std::snprintf(bid, sizeof(bid), "b%04X_%zu", block.pc, b);
        o << "\n    // block at 0x" << std::hex << block.pc << std::dec << ", " << block.words.size() << " pairs\n";
        for (size_t i = 0; i < block.words.size(); ++i)
        {
            const uint32_t pc = block.pc + static_cast<uint32_t>(i) * 8u;
            const auto &[lower, upper] = block.words[i];
            const auto &d = pairInfo.at({pc, lower, upper}).decoded;
            o << "    constexpr Pair k_" << bid << "_" << i << "{.lower = 0x" << std::hex << d.lower << "u, .upper = 0x" << d.upper << std::dec << "u,\n"
              << "        .lowerUsage = ";
            usage(o, d.lowerUsage);
            o << ",\n        .upperUsage = ";
            usage(o, d.upperUsage);
            o << ",\n        .iBit = " << (d.iBit ? "true" : "false") << ", .eBit = " << (d.eBit ? "true" : "false")
              << ", .mBit = " << (d.mBit ? "true" : "false") << ", .dBit = " << (d.dBit ? "true" : "false")
              << ", .tBit = " << (d.tBit ? "true" : "false")
              << ", .upperVfShadowReg = " << +d.upperVfShadowReg << "u, .suppressedLowerVf = " << +d.suppressedLowerVf << "u};\n";
            blockUppers[f].insert(upper);
            if ((upper & 0x80000000u) == 0u)
                blockLowers[f].insert(lower);
        }
        o << "    const uint32_t kWords_" << bid << "[] = {";
        for (const auto &[lower, upper] : block.words)
            o << "0x" << std::hex << lower << "u, 0x" << upper << "u, " << std::dec;
        o << "};\n";
        // The ordinary per-pair step of pair i (the last pair, or the first
        // one when the block cannot be entered).
        const auto ordinaryStep = [&](size_t i)
        {
            const auto &[lower, upper] = block.words[i];
            char un[16], ln[16];
            std::snprintf(un, sizeof(un), "u%08X", upper);
            std::snprintf(ln, sizeof(ln), "l%08X", lower);
            if ((upper & 0x80000000u) != 0u)
                o << "VU1NativeAccess::execPairUpper<&ben10::vu1_native::upper::" << un << ", k_" << bid << "_" << i
                  << ">(vu, k_" << bid << "_" << i << ", frame);\n";
            else
                o << "VU1NativeAccess::execPairUpperLower<&ben10::vu1_native::upper::" << un
                  << ", &ben10::vu1_native::lower::" << ln << ", k_" << bid << "_" << i << ">(vu, k_" << bid << "_" << i << ", frame);\n";
        };
        const size_t count = block.words.size();
        // Flag results left out (findings/20 s.28). Body pair i's FMAC
        // result (MAC, status, sticky) is due 4 cycles after it issues and
        // stays in the flag registers until the next FMAC result j replaces
        // it, 4 cycles after j issues. Pairs issue at least one cycle apart,
        // so only a flag read in pairs i+1 .. j+3 can see i's MAC or status
        // (FSEQ/FSAND/FSOR, FMEQ/FMAND/FMOR; FSSET also changes the queued
        // entries, so it counts too; the clip-flag instructions do not read
        // them). If none of those pairs reads them, and they are all body
        // pairs (the block cannot stop before them: blockCanEnterWithin),
        // pair i runs with kElide: its entry is left out when the only
        // sticky bits it could add are set already (fmacFlagsFast). j is the
        // next pair whose upper word always queues an FMAC entry
        // (upperQueuesFmacFlags); if j is left out too, the same holds for
        // it with its own next one, so the registers are right whenever an
        // instruction can read them.
        std::vector<bool> elide(count, false);
        {
            const auto reads = [&](size_t k)
            {
                const auto &[lower, upper] = block.words[k];
                if ((upper & 0x80000000u) != 0u)
                    return false; // I bit: the lower word is an immediate
                const uint32_t opHi = (lower >> 25) & 0x7Fu;
                return opHi == 0x14u || opHi == 0x15u || opHi == 0x16u || opHi == 0x17u || opHi == 0x18u ||
                       opHi == 0x1Au || opHi == 0x1Bu;
            };
            for (size_t i = 0; i + 1 < count; ++i)
            {
                if (!queuesFmac(block.words[i].second) || reads(i))
                    continue;
                size_t j = i + 1;
                while (j < count && !queuesFmac(block.words[j].second))
                    ++j;
                if (j >= count || j + 3 > count - 2)
                    continue;
                bool read = false;
                for (size_t k = i + 1; k <= j + 3 && !read; ++k)
                    read = reads(k);
                if (!read)
                {
                    elide[i] = true;
                    ++elidedPairs;
                }
            }
        }
        // The body's cycle bound for blockCanEnterWithin (findings/20 s.29):
        // a body pair issues one cycle after the previous one, or when a
        // result it waits for is due, either from an earlier body pair (at
        // most that pair's latency after it issued) or from before the
        // block (at most kMaxPipelineLatency, or an XGKICK transfer:
        // kMaxXgkickWait). By induction the body ends within its pair count
        // plus that wait plus the sum of its pairs' latencies.
        uint64_t bodyBound = 0;
        bool bodyXgkick = false;
        for (size_t i = 0; i + 1 < count; ++i)
        {
            const uint32_t pc = block.pc + static_cast<uint32_t>(i) * 8u;
            const auto &[lower, upper] = block.words[i];
            const auto &d = pairInfo.at({pc, lower, upper}).decoded;
            const bool xgkick = d.lowerUsage.pipeline == VU1Interpreter::PipelineXgkick;
            bodyXgkick = bodyXgkick || xgkick;
            // (at least 1: the ACC result, forwarded to the next pair)
            const uint32_t latency = std::max<uint32_t>({1u, d.upperUsage.latency, d.upperUsage.vfLatency, d.lowerUsage.latency,
                                                         d.lowerUsage.vfLatency, d.lowerUsage.viLatency});
            bodyBound += 1u + (xgkick ? VU1NativeAccess::kMaxXgkickWait : latency);
        }
        bodyBound += bodyXgkick ? VU1NativeAccess::kMaxXgkickWait : VU1NativeAccess::kMaxPipelineLatency;
        o << "    Result " << bid << "(VU1Interpreter &vu, Frame &frame)\n    {\n"
          << "        // A branch, delay slot or end pending, or a body that could run\n"
          << "        // past the cycle budget: the first pair alone, as the run loop\n"
          << "        // would run it.\n"
          << "        if (!VU1NativeAccess::blockCanEnterWithin<" << bodyBound << "u>(vu, frame))\n";
        o << "            return ";
        ordinaryStep(0);
        // The latest write of each VF lane, VI register and ACC lane by an
        // earlier pair of this block: its index and latency. A read whose
        // latest write is at least that latency in pairs back cannot stall
        // (pairs issue at least one cycle apart) and is left out of the
        // pair's hazard check (MaskedTiming, findings/21 s.24). Reads with no
        // write in the block yet depend on the state at entry: kept.
        struct LastWrite
        {
            bool valid = false;
            size_t index = 0;
            uint32_t latency = 0;
        };
        LastWrite vfLast[32][4]{}, viLast[16]{}, accLast[4]{};
        const auto settled = [](const LastWrite &w, size_t j)
        { return w.valid && j - w.index >= w.latency; };
        const auto vfMask = [&](const VU1Interpreter::VfAccess &a, size_t j)
        {
            uint8_t keep = 0;
            for (uint32_t bit = 0; bit < 4u; ++bit) // lane bit b is component 3 - b
                if ((a.lanes & (1u << bit)) != 0u && !(a.reg == 0u || settled(vfLast[a.reg][3u - bit], j)))
                    keep |= static_cast<uint8_t>(1u << bit);
            return keep;
        };
        for (size_t i = 0; i + 1 < count; ++i)
        {
            const uint32_t pc = block.pc + static_cast<uint32_t>(i) * 8u;
            const auto &[lower, upper] = block.words[i];
            char un[16], ln[16];
            std::snprintf(un, sizeof(un), "u%08X", upper);
            std::snprintf(ln, sizeof(ln), "l%08X", lower);
            const auto &d = pairInfo.at({pc, lower, upper}).decoded;
            const uint8_t u0 = d.upperUsage.vfReadCount > 0u ? vfMask(d.upperUsage.vfRead[0], i) : 0u;
            const uint8_t u1 = d.upperUsage.vfReadCount > 1u ? vfMask(d.upperUsage.vfRead[1], i) : 0u;
            const uint8_t l0 = d.lowerUsage.vfReadCount > 0u ? vfMask(d.lowerUsage.vfRead[0], i) : 0u;
            const uint8_t l1 = d.lowerUsage.vfReadCount > 1u ? vfMask(d.lowerUsage.vfRead[1], i) : 0u;
            uint16_t vi = 0;
            for (uint32_t r = 1; r < 16u; ++r)
                if (((d.upperUsage.viRead | d.lowerUsage.viRead) & (1u << r)) != 0u && !settled(viLast[r], i))
                    vi |= static_cast<uint16_t>(1u << r);
            uint8_t acc = 0;
            for (uint32_t bit = 0; bit < 4u; ++bit)
                if (((d.upperUsage.accRead | d.lowerUsage.accRead) & (1u << bit)) != 0u && !settled(accLast[3u - bit], i))
                    acc |= static_cast<uint8_t>(1u << bit);
            // Then this pair's writes, as markPairWritesConst marks them
            // (lower first, then upper, VI, ACC).
            const auto markVf = [&](const VU1Interpreter::VfAccess &w, uint32_t latency)
            {
                for (uint32_t bit = 0; bit < 4u; ++bit)
                    if ((w.lanes & (1u << bit)) != 0u)
                        vfLast[w.reg][3u - bit] = {true, i, latency};
            };
            if (d.lowerUsage.vfWrite.reg != 0u && d.suppressedLowerVf != d.lowerUsage.vfWrite.reg)
                markVf(d.lowerUsage.vfWrite, d.lowerUsage.vfLatency != 0u ? d.lowerUsage.vfLatency : d.lowerUsage.latency);
            if (d.upperUsage.vfWrite.reg != 0u)
                markVf(d.upperUsage.vfWrite, d.upperUsage.vfLatency != 0u ? d.upperUsage.vfLatency : d.upperUsage.latency);
            for (uint32_t r = 1; r < 16u; ++r)
                if ((d.lowerUsage.viWrite & (1u << r)) != 0u)
                    viLast[r] = {true, i, d.lowerUsage.viLatency != 0u ? d.lowerUsage.viLatency : d.lowerUsage.latency};
            for (uint32_t bit = 0; bit < 4u; ++bit)
                if ((d.upperUsage.accWrite & (1u << bit)) != 0u)
                    accLast[3u - bit] = {true, i, 1u}; // kAccForwardLatency
            char masks[112];
            std::snprintf(masks, sizeof(masks), ", 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu, 0x%Xu%s", u0, u1, l0, l1, vi, acc,
                          elide[i] ? ", true" : "");
            if ((upper & 0x80000000u) != 0u)
                o << "        if (!VU1NativeAccess::blockPairUpper<&ben10::vu1_native::upper::" << un << ", k_" << bid << "_" << i
                  << ", 0x" << std::hex << pc << std::dec << "u, " << (i == 0 ? "true" : "false") << masks << ">(vu, frame))\n";
            else
                o << "        if (!VU1NativeAccess::blockPairUpperLower<&ben10::vu1_native::upper::" << un
                  << ", &ben10::vu1_native::lower::" << ln << ", k_" << bid << "_" << i
                  << ", 0x" << std::hex << pc << std::dec << "u, " << (i == 0 ? "true" : "false") << masks << ">(vu, frame))\n";
            o << "            return Result::Break;\n";
        }
        // The last pair: the run loop's checks, then the ordinary step.
        {
            const size_t i = count - 1;
            const uint32_t pc = block.pc + static_cast<uint32_t>(i) * 8u;
            const auto &[lower, upper] = block.words[i];
            char un[16], ln[16];
            std::snprintf(un, sizeof(un), "u%08X", upper);
            std::snprintf(ln, sizeof(ln), "l%08X", lower);
            (void)lower;
            (void)upper;
            (void)un;
            (void)ln;
            o << "        VU1NativeAccess::blockSetPc(vu, frame, 0x" << std::hex << pc << std::dec << "u);\n"
              << "        if (VU1NativeAccess::loopHead(vu, frame) == Result::Break)\n"
              << "            return Result::Break;\n"
              << "        return ";
            ordinaryStep(i);
        }
        o << "    }\n";
        blockTables[f] << "        {0x" << std::hex << block.pc << std::dec << "u, " << count << "u, kWords_" << bid << ", &" << bid
                       << ", " << (block.vu0 ? "true" : "false") << "},\n";
        ++blockCounts[f];
        blockPairs += count;
        ++b;
    }
    for (int f = 0; f < kFileCount; ++f)
    {
        std::ostringstream head;
        head << "// Generated by tools/vu1_translate from the game's ELF: do not edit, do not version.\n"
                "// Translated blocks (findings/21 s.24).\n"
                "#include \"vu/vu1_native.h\"\n"
                "#include \"runtime/ps2_vu1_lower_inl.h\"\n\n"
                "namespace ben10::vu1_native::upper\n{\n";
        for (uint32_t u : blockUppers[f])
        {
            char un[16];
            std::snprintf(un, sizeof(un), "u%08X", u);
            head << "    void " << un << "(VU1Interpreter &vu);\n";
        }
        head << "}\nnamespace ben10::vu1_native::lower\n{\n";
        for (uint32_t l : blockLowers[f])
        {
            char ln[16];
            std::snprintf(ln, sizeof(ln), "l%08X", l);
            head << "    void " << ln << "(VU1Interpreter &vu, uint8_t *vuData, uint32_t dataSize, GS &gs, PS2Memory *memory, uint32_t upper);\n";
        }
        head << "}\n\nnamespace\n{\n"
                "    using Pair = VU1NativeAccess::Pair;\n"
                "    using Frame = VU1NativeAccess::Frame;\n"
                "    using Result = VU1NativeAccess::Result;\n";
        std::ofstream(outDir / ("vu1_native_blocks_" + std::to_string(f) + ".cpp"))
            << head.str() << blockFiles[f].str() << "}\n\nnamespace ben10::vu1_native\n{\n    extern const NativeBlock kBlocks" << f
            << "[];\n    const NativeBlock kBlocks" << f << "[] = {\n" << blockTables[f].str()
            << (blockCounts[f] == 0 ? "        {0u, 0u, nullptr, nullptr, false},\n" : "") << "    };\n}\n";
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
    for (int f = 0; f < kFileCount; ++f)
        index << "    extern const NativeBlock kBlocks" << f << "[];\n";
    index << "    const NativeBlockTable kBlockTables[] = {\n";
    for (int f = 0; f < kFileCount; ++f)
        index << "        {kBlocks" << f << ", " << blockCounts[f] << "u},\n";
    index << "    };\n    const size_t kBlockTableCount = " << kFileCount << ";\n";
    index << "    const NativeTable kTables[] = {\n";
    for (int f = 0; f < kFileCount; ++f)
        index << "        {kPairs" << f << ", " << counts[f] << "u},\n";
    index << "    };\n    const size_t kTableCount = " << kFileCount << ";\n}\n";
    std::ofstream(outDir / "vu1_native_index.cpp") << index.str();

    std::printf("vu1_translate: %zu overlay entries, %zu distinct pairs (%zu also valid on VU0), %zu upper and %zu lower words, "
                "%zu blocks of %zu pairs (%zu with flag results left out) -> %s\n",
                entries.size(), n, vu0Pairs, upperFile.size(), lowerFile.size(), blocks.size(), blockPairs, elidedPairs,
                outDir.string().c_str());
    return 0;
}
