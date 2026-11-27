// SPDX-License-Identifier: GPL-3.0-or-later

// Runs the NCE patcher (src/core/arm/nce/patcher.cpp) on made-up programs the way the two passes
// of AppLoader_DeconstructedRomDirectory::Load and AppLoader_NSO::LoadModule do. Any host will
// do: the patcher only writes instruction words. Checks that every patched instruction branches
// to patch code that branches back after it, or is in the trap table, and that the load pass
// stays inside what the layout pass reserved.
//
// Build it with the include directories of a core source file and oaknut's, HAS_NCE defined and
// ARCHITECTURE_x86_64 not (the patcher reads the arm64 wall clock's fields):
//   c++ -std=c++20 -O1 -DFMT_HEADER_ONLY -DHAS_NCE=1 -I src -I externals -I <fmt>/include
//       -I <oaknut>/include -I <boost>/libs/<each>/include
//       tools/nx2/nce_patch_layout_probe.cpp src/core/arm/nce/patcher.cpp
// A letter as the argument runs that program alone. The exit status is the number of programs
// that came out otherwise than expected.
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <fmt/format.h>
#include <oaknut/oaknut_exception.hpp>

#include "common/cpu_features.h"
#include "core/arm/nce/patcher.h"

void AssertFailSoftImpl() {
    std::puts("  soft assert failed");
}
namespace Common {
WallClock::WallClock(bool, u64) noexcept {}
void WallClock::SetGuestCNTFRQ(u64 f) {
    guest_cntfrq.store(f);
}
WallClock g_wall_clock{false, 1};
} // namespace Common
namespace Common::Log {
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view format, const fmt::format_args& args) {
    if (level < Level::Info) {
        return;
    }
    fmt::print("  log: {}\n", fmt::vformat(format, args));
}
} // namespace Common::Log

using Core::NCE::PatchMode;
using Core::NCE::Patcher;

namespace {
constexpr size_t MiB = 1024 * 1024;
constexpr u32 Nop = 0xD503201F;
constexpr u32 MrsTpidr = 0xD53BD048;   // mrs x8, tpidr_el0
constexpr u32 MrsTpidrro = 0xD53BD069; // mrs x9, tpidrro_el0
constexpr u32 MrsCntpct = 0xD53BE020;  // mrs x0, cntpct_el0
constexpr u32 MrsCntfrq = 0xD53BE001;  // mrs x1, cntfrq_el0
constexpr u32 MsrTpidr = 0xD51BD043;   // msr tpidr_el0, x3
constexpr u32 Svc = 0xD40003E1;        // svc #0x1f
constexpr u32 Ldxr = 0xC85F7C00;       // ldxr x0, [x0]
constexpr u64 LoadBase = 0x176b5bb000;

size_t PageAlign(size_t v) {
    return (v + 0xFFF) & ~size_t{0xFFF};
}

struct Site {
    size_t offset;
    u32 word;
};

struct Module {
    size_t text;
    size_t rest; // rodata + data
    size_t bss;
    std::vector<Site> sites;
};

const char* ModeName(PatchMode m) {
    switch (m) {
    case PatchMode::PreText:
        return "PreText";
    case PatchMode::PostData:
        return "PostData";
    case PatchMode::Split:
        return "Split";
    default:
        return "None";
    }
}

void Fill(std::vector<u8>& image, size_t start, const Module& m) {
    for (size_t i = 0; i < m.text; i += 4) {
        std::memcpy(image.data() + start + i, &Nop, 4);
    }
    for (const auto& s : m.sites) {
        std::memcpy(image.data() + start + s.offset, &s.word, 4);
    }
    // An exclusive load, to see it made ordered.
    std::memcpy(image.data() + start + 0x40, &Ldxr, 4);
}

u32 Word(const std::vector<u8>& mem, size_t at) {
    u32 w;
    std::memcpy(&w, mem.data() + at, 4);
    return w;
}

s64 BranchTarget(const std::vector<u8>& mem, size_t at) {
    const u32 w = Word(mem, at);
    if ((w & 0xFC000000) != 0x14000000) {
        return -1;
    }
    s64 imm = w & 0x03FFFFFF;
    if (imm & 0x02000000) {
        imm -= 0x04000000;
    }
    return static_cast<s64>(at) + imm * 4;
}

u64 Hash(const std::vector<u8>& mem) {
    u64 h = 0xcbf29ce484222325ull;
    const size_t words = mem.size() / 8;
    for (size_t i = 0; i < words; i++) {
        u64 v;
        std::memcpy(&v, mem.data() + i * 8, 8);
        h = (h ^ v) * 0x100000001b3ull;
    }
    return h;
}

enum class Outcome {
    Native,   ///< Laid out, every branch and trap right.
    Dynarmic, ///< The patcher says it cannot write the branches.
    Wrong,
};

Outcome Run(const char* name, const std::vector<Module>& program) {
    fmt::print("{}\n", name);
    std::vector<Patcher> patchers;
    patchers.emplace_back();
    std::vector<size_t> index(program.size());

    // Layout pass.
    size_t code_size = 0;
    for (size_t i = 0; i < program.size(); i++) {
        const Module& m = program[i];
        std::vector<u8> image(m.text + m.rest);
        Fill(image, 0, m);
        const size_t image_size = PageAlign(image.size() + m.bss);
        image.resize(image_size);
        Kernel::CodeSet::Segment code{};
        code.size = static_cast<u32>(PageAlign(m.text));
        Patcher* patch = &patchers.back();
        while (!patch->PatchText(image, code)) {
            patch = &patchers.emplace_back();
        }
        index[i] = patchers.size() - 1;
        const auto mode = patch->GetPatchMode();
        fmt::print("  module {}: text {} MiB, image {} MiB, {} sites, patcher {}, mode {}\n", i,
                   m.text / MiB, image_size / MiB, m.sites.size(), index[i], ModeName(mode));
        if (mode == PatchMode::Split) {
            code_size += patch->GetPreSectionSize() + image_size + patch->GetSectionSize();
        } else {
            code_size += image_size + patch->GetSectionSize();
        }
    }
    for (const auto& patcher : patchers) {
        code_size += patcher.GetSectionSize();
    }
    for (size_t p = 0; p < patchers.size(); p++) {
        if (!patchers[p].CanRelocate()) {
            fmt::print("  patcher {} cannot relocate: the loader runs the program on Dynarmic\n",
                       p);
            return Outcome::Dynarmic;
        }
    }

    // Load pass, into one flat copy of the code region.
    std::vector<u8> mem;
    std::vector<size_t> text_start(program.size());
    Core::NCE::EntryTrampolines trampolines;
    Core::NCE::TrappedInstructions traps;
    for (size_t i = 0; i < program.size(); i++) {
        const Module& m = program[i];
        Patcher* patch = &patchers[index[i]];
        const auto mode = patch->GetPatchMode();
        const size_t pre = mode == PatchMode::Split
                               ? patch->GetPreSectionSize()
                               : (mode == PatchMode::PreText ? patch->GetSectionSize() : 0);
        std::vector<u8> image(pre + m.text + m.rest);
        Fill(image, pre, m);
        image.resize(PageAlign(image.size() + m.bss));
        Kernel::CodeSet::Segment code{};
        code.offset = pre;
        code.addr = pre;
        code.size = static_cast<u32>(PageAlign(m.text));
        const size_t load = mem.size();
        try {
            patch->RelocateAndCopy(LoadBase + load, code, image, &trampolines, &traps);
        } catch (const oaknut::OaknutException& e) {
            fmt::print("  module {}: RelocateAndCopy threw \"{}\" (nothing catches it: the app "
                       "closes)\n",
                       i, e.what());
            return Outcome::Wrong;
        }
        text_start[i] = load + pre;
        mem.insert(mem.end(), image.begin(), image.end());
        fmt::print("  module {}: loaded at +{:#x}, {:#x} bytes (section before the text {:#x})\n",
                   i, load, image.size(), pre);
    }
    if (mem.size() > code_size) {
        fmt::print("  the load pass used {:#x} bytes, the layout pass reserved {:#x}\n",
                   mem.size(), code_size);
        return Outcome::Wrong;
    }

    // Every site branches to patch code that branches back to the next instruction, or traps.
    size_t bad = 0, branched = 0, trapped = 0, svcs = 0;
    for (size_t i = 0; i < program.size(); i++) {
        const Module& m = program[i];
        const size_t text_end = text_start[i] + m.text;
        if (Word(mem, text_start[i] + 0x40) != (Ldxr | 0x8000)) {
            bad++;
            fmt::print("  module {}: the exclusive load was not made ordered\n", i);
        }
        for (const auto& s : m.sites) {
            const size_t at = text_start[i] + s.offset;
            if (const auto it = traps.find(LoadBase + at); it != traps.end()) {
                if (Word(mem, at) != 0 || it->second != s.word) {
                    if (bad++ < 3) {
                        fmt::print("  site {:#x}: wrong trap\n", s.offset);
                    }
                }
                trapped++;
                continue;
            }
            const s64 target = BranchTarget(mem, at);
            // Patch code is outside every module's text.
            bool in_text = target < 0 || static_cast<size_t>(target) >= mem.size();
            for (size_t j = 0; j < program.size() && !in_text; j++) {
                in_text = static_cast<size_t>(target) >= text_start[j] &&
                          static_cast<size_t>(target) < text_start[j] + program[j].text +
                                                            program[j].rest + program[j].bss;
            }
            if (in_text) {
                if (bad++ < 3) {
                    fmt::print("  site {:#x}: no branch to patch code (target {:#x})\n", s.offset,
                               target);
                }
                continue;
            }
            branched++;
            bool returns = false;
            for (size_t w = 0; w < 160 && target + w * 4 + 4 <= mem.size(); w++) {
                if (BranchTarget(mem, target + w * 4) == static_cast<s64>(at + 4)) {
                    returns = true;
                    break;
                }
            }
            if (!returns && bad++ < 3) {
                fmt::print("  site {:#x}: its patch code does not return after it\n", s.offset);
            }
            if ((s.word & 0xFFE0001F) == 0xD4000001) {
                // The handler run after the call is in patch code too.
                svcs++;
                const auto it = trampolines.find(LoadBase + at + 4);
                if (it == trampolines.end() || it->second < LoadBase ||
                    it->second >= LoadBase + mem.size() ||
                    (it->second >= LoadBase + text_start[i] && it->second < LoadBase + text_end)) {
                    if (bad++ < 3) {
                        fmt::print("  site {:#x}: no handler after the call\n", s.offset);
                    }
                }
            }
        }
    }
    fmt::print("  {} sites branch to patch code ({} calls), {} trap, {} wrong; hash {:016x}\n",
               branched, svcs, trapped, bad, Hash(mem));
    return bad == 0 ? Outcome::Native : Outcome::Wrong;
}

u32 Pick(size_t n) {
    static constexpr u32 Kinds[] = {MrsTpidr, MrsTpidrro, MsrTpidr, MrsCntpct, MrsCntfrq};
    return n % 97 == 0 ? Svc : Kinds[n % 5];
}

Module Make(size_t text_mib, size_t rest_mib, size_t bss_mib, size_t spacing,
            std::vector<Site> extra = {}) {
    Module m{text_mib * MiB, rest_mib * MiB, bss_mib * MiB, {}};
    size_t n = 0;
    for (size_t off = 0x1000; off + 8 < m.text; off += spacing, n++) {
        m.sites.push_back({off, Pick(n)});
    }
    // An extra site replaces a regular one at the same place.
    std::map<size_t, u32> by_offset;
    for (const auto& e : m.sites) {
        by_offset[e.offset] = e.word;
    }
    for (const auto& e : extra) {
        by_offset[e.offset] = e.word;
    }
    m.sites.clear();
    for (const auto& [offset, word] : by_offset) {
        m.sites.push_back({offset, word});
    }
    return m;
}

// Sites on every word of [from, to).
std::vector<Site> Dense(size_t from, size_t to) {
    std::vector<Site> sites;
    for (size_t off = from, n = 0; off < to; off += 4, n++) {
        sites.push_back({off, Pick(n)});
    }
    return sites;
}

Module Small(size_t text_kib, size_t spacing = 0x400) {
    Module m{text_kib * 1024, 0x3000, 0x2000, {}};
    size_t n = 0;
    for (size_t off = 0x100; off + 8 < m.text; off += spacing, n++) {
        m.sites.push_back({off, Pick(n)});
    }
    return m;
}
} // namespace

int main(int argc, char** argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const Module rtld = Small(20);
    const Module subsdk = Small(900);
    const Module sdk = Small(6000);
    const auto game = [&](Module main) { return std::vector<Module>{rtld, main, subsdk, sdk}; };

    struct Program {
        const char* name;
        std::function<std::vector<Module>()> make;
        Outcome expected = Outcome::Native;
    };
    // Unless said otherwise a main has a site every 64 KiB, none of them in the last 4 KiB
    // before 128 MiB.
    const Program programs[] = {
        {"a: 150 MiB of code in a 208 MiB main",
         [&] { return game(Make(150, 38, 20, 0x10000)); }},
        {"b: the same with one TLS read 8 bytes before 128 MiB",
         [&] { return game(Make(150, 38, 20, 0x10000, {{128 * MiB - 8, MrsTpidr}})); }},
        {"c: 180 MiB of code in a 270 MiB main",
         [&] { return game(Make(180, 60, 30, 0x10000)); }},
        {"d: 120 MiB of code in a 300 MiB main",
         [&] { return game(Make(120, 100, 80, 0x10000)); }},
        {"e: 60 MiB of code in a 90 MiB main", [&] { return game(Make(60, 20, 10, 0x10000)); }},
        {"f: 200 MiB of code in a 252 MiB main, a site every 4 KiB",
         [&] { return game(Make(200, 40, 12, 0x1000)); }},
        {"g: 300 MiB of code in a 400 MiB main",
         [&] { return game(Make(300, 60, 40, 0x10000)); }},
        {"h: 150 MiB of code in a 208 MiB main, every word a site for 64 KiB on each side of "
         "128 MiB",
         [&] {
             return game(Make(150, 38, 20, 0x10000,
                              Dense(128 * MiB - 0x10000, 128 * MiB + 0x10000)));
         }},
        {"i: 100 MiB of code in a 129 MiB main",
         [&] { return game(Make(100, 19, 10, 0x10000)); }},
        {"j: 200 MiB of code in a 256 MiB main, a site every 4 KiB",
         [&] { return game(Make(200, 40, 16, 0x1000)); }},
        {"k: 127 MiB of code in a 128 MiB main, alone in its patcher",
         [&] { return std::vector<Module>{Make(127, 1, 0, 0x1000)}; }},
        {"l: three modules of 40 MiB sharing a patcher",
         [&] {
             return std::vector<Module>{Make(30, 8, 2, 0x2000), Make(30, 8, 2, 0x2000),
                                        Make(30, 8, 2, 0x2000)};
         }},
        {"m: 250 MiB of code in a 262 MiB main, every word a site from 127 MiB to 136 MiB",
         [&] { return game(Make(250, 8, 4, 0x10000, Dense(127 * MiB, 136 * MiB))); }},
        // The patch code of the first module's read lies 0x50 bytes past what a branch reaches.
        {"n: two 64 MiB modules sharing a patcher, a TLS read at 0x100 of the first",
         [&] {
             return std::vector<Module>{Make(60, 3, 1, 0x10000, {{0x100, MrsTpidr}}),
                                        Make(60, 3, 1, 0x10000)};
         },
         Outcome::Dynarmic},
        {"o: 230 MiB of code in a 253 MiB main, a site every 1 KiB",
         [&] { return game(Make(230, 15, 8, 0x400)); }},
    };

    int unexpected = 0;
    for (const Program& program : programs) {
        if (!only.empty() && only[0] != program.name[0]) {
            continue;
        }
        if (Run(program.name, program.make()) != program.expected) {
            fmt::print("  NOT AS EXPECTED\n");
            unexpected++;
        }
    }
    fmt::print("{} program(s) came out otherwise than expected\n", unexpected);
    return unexpected;
}
