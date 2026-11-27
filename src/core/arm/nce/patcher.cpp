// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <numeric>
#include <algorithm>
#include <limits>
#include <optional>
#include <bit>
#include "common/cpu_features.h"
#include "common/alignment.h"
#include "common/literals.h"
#include "core/arm/nce/arm_nce.h"
#include "core/arm/nce/guest_context.h"
#include "core/arm/nce/instructions.h"
#include "core/arm/nce/patcher.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/svc.h"
#include "core/memory.h"
#include "core/hle/kernel/k_thread.h"

namespace Core::NCE {

using namespace Common::Literals;
using namespace oaknut::util;

using NativeExecutionParameters = Kernel::KThread::NativeExecutionParameters;

constexpr size_t MaxRelativeBranch = 128_MiB;
constexpr u32 ModuleCodeIndex = 0x24 / sizeof(u32);
// What a patched instruction becomes when no patch section is in reach: udf #0.
constexpr u32 TrapInstruction = 0x00000000;

namespace {

bool IsPatchSite(u32 inst) {
    if (SVC{inst}.Verify()) {
        return true;
    }
    if (auto mrs = MRS{inst}; mrs.Verify()) {
        const u32 reg = mrs.GetSystemReg();
        return reg == TpidrroEl0 || reg == TpidrEl0 || reg == CntpctEl0 || reg == CntfrqEl0;
    }
    auto msr = MSR{inst};
    return msr.Verify() && msr.GetSystemReg() == TpidrEl0;
}

bool InBranchRange(ptrdiff_t offset) {
    return offset % 4 == 0 && offset >= -static_cast<ptrdiff_t>(MaxRelativeBranch) &&
           offset < static_cast<ptrdiff_t>(MaxRelativeBranch);
}

// Where a module and its patch sections lie, for the branches between them. Offsets are from
// the start of the text and of the patch section.
struct BranchLayout {
    PatchMode mode;
    ptrdiff_t patch_size;     ///< The section after the data (before the text in PreText mode).
    ptrdiff_t pre_patch_size; ///< The section before the text of a split module.
    ptrdiff_t image_size;     ///< The module, without the section before its text.
    ptrdiff_t total_size;     ///< From the start of the module to the section after the data.

    ptrdiff_t ToPatch(ptrdiff_t patch_offset, ptrdiff_t module_offset) const {
        switch (mode) {
        case PatchMode::PreText:
            return patch_offset - patch_size - module_offset;
        case PatchMode::Split:
            return image_size + patch_offset - module_offset;
        default:
            return total_size - module_offset + patch_offset;
        }
    }

    ptrdiff_t ToPrePatch(ptrdiff_t patch_offset, ptrdiff_t module_offset) const {
        return patch_offset - pre_patch_size - module_offset;
    }

    ptrdiff_t FromPatch(ptrdiff_t patch_offset, ptrdiff_t module_offset) const {
        switch (mode) {
        case PatchMode::PreText:
            return patch_size - patch_offset + module_offset;
        case PatchMode::Split:
            return module_offset - image_size - patch_offset;
        default:
            return module_offset - total_size - patch_offset;
        }
    }

    ptrdiff_t FromPrePatch(ptrdiff_t patch_offset, ptrdiff_t module_offset) const {
        return pre_patch_size + module_offset - patch_offset;
    }
};

} // namespace

// Switch 2 titles are built with pointer authentication. Under NCE the guest runs natively on
// the host, whose kernel enables real PAC keys (and FEAT_FPAC traps on a failed AUT*). Guest
// pointers that never went through the host's signing (HLE-written contexts, relocations,
// thread entry points, ...) then fail authentication and the process dies with SIGILL.
// Rewrite every PAC instruction into its key-disabled equivalent so signing and checking are
// both no-ops, which is consistent for the whole guest.
static std::optional<u32> RewritePointerAuth(u32 inst) {
    constexpr u32 Nop = 0xD503201F;
    const u32 rn = (inst >> 5) & 0x1F;
    const u32 rd = inst & 0x1F;

    // HINT space: XPACLRI, PACIx1716, AUTIx1716, PACIxZ, PACIxSP, AUTIxZ, AUTIxSP.
    if ((inst & 0xFFFFF01F) == 0xD503201F) {
        const u32 imm = (inst >> 5) & 0x7F;
        if (imm == 7 || imm == 8 || imm == 10 || imm == 12 || imm == 14 ||
            (imm >= 24 && imm <= 31)) {
            return Nop;
        }
        return std::nullopt;
    }

    // PACIA/PACIB/PACDA/PACDB/AUTIA/AUTIB/AUTDA/AUTDB and their Z variants (Xd modified in place).
    if ((inst & 0xFFFFC000) == 0xDAC10000) {
        return Nop;
    }
    // XPACI/XPACD.
    if ((inst & 0xFFFFFBE0) == 0xDAC143E0) {
        return Nop;
    }

    // RETAA/RETAB -> RET.
    if (inst == 0xD65F0BFF || inst == 0xD65F0FFF) {
        return 0xD65F03C0;
    }
    // BRAAZ/BRABZ/BLRAAZ/BLRABZ -> BR/BLR Xn.
    if ((inst & 0xFFDFF81F) == 0xD61F081F) {
        return ((inst & (1U << 21)) ? 0xD63F0000U : 0xD61F0000U) | (rn << 5);
    }
    // BRAA/BRAB/BLRAA/BLRAB Xn, Xm -> BR/BLR Xn.
    if ((inst & 0xFFDFF800) == 0xD71F0800) {
        return ((inst & (1U << 21)) ? 0xD63F0000U : 0xD61F0000U) | (rn << 5);
    }

    // LDRAA/LDRAB Xt, [Xn, #simm10*8]{!} -> LDR/LDUR with the same address.
    if ((inst & 0xFF200400) == 0xF8200400) {
        const u32 s = (inst >> 22) & 1;
        const u32 imm9 = (inst >> 12) & 0x1FF;
        const bool writeback = (inst >> 11) & 1;
        s32 offset = static_cast<s32>((s << 9) | imm9);
        if (offset & 0x200) {
            offset -= 0x400;
        }
        offset *= 8;
        if (writeback) {
            if (offset >= -256 && offset <= 255) {
                // LDR Xt, [Xn, #simm9]!
                return 0xF8400C00U | ((static_cast<u32>(offset) & 0x1FF) << 12) | (rn << 5) | rd;
            }
        } else if (offset >= 0) {
            // LDR Xt, [Xn, #uimm12*8]
            return 0xF9400000U | ((static_cast<u32>(offset) / 8) << 10) | (rn << 5) | rd;
        } else if (offset >= -256) {
            // LDUR Xt, [Xn, #simm9]
            return 0xF8400000U | ((static_cast<u32>(offset) & 0x1FF) << 12) | (rn << 5) | rd;
        }
        LOG_WARNING(Core_ARM, "NCE: cannot rewrite LDRAA/LDRAB {:08X} (offset {})", inst, offset);
        return std::nullopt;
    }

    return std::nullopt;
}

Patcher::Patcher() : c(m_patch_instructions), c_pre(m_patch_instructions_pre) {
    // The first word of the patch section is always a branch to the first instruction of the
    // module.
    c.dw(0);
    c_pre.dw(0);

    // Write save context helper function.
    c.l(m_save_context);
    WriteSaveContext();
    c_pre.l(m_save_context_pre);
    WriteSaveContext(c_pre);

    // Write load context helper function.
    c.l(m_load_context);
    WriteLoadContext();
    c_pre.l(m_load_context_pre);
    WriteLoadContext(c_pre);
}

Patcher::Patcher(std::span<const u8> text) : c(m_patch_instructions), c_pre(m_patch_instructions_pre) {
    modules.emplace_back();
    curr_patch = &modules.back();
    const auto words = std::span<const u32>{reinterpret_cast<const u32*>(text.data()), text.size() / sizeof(u32)};
    for (u32 i = ModuleCodeIndex; i < static_cast<u32>(words.size()); ++i) {
        const uintptr_t offset = i * sizeof(u32);
        if (auto mrs = MRS{words[i]}; mrs.Verify() && mrs.GetRt() != 31 && (mrs.GetSystemReg() == TpidrEl0 || mrs.GetSystemReg() == TpidrroEl0)) {
            BranchToPatch(offset);
            const auto src_reg = mrs.GetSystemReg() == TpidrroEl0 ? oaknut::SystemReg::TPIDRRO_EL0 : oaknut::SystemReg::TPIDR_EL0;
            WriteMrsHandler(offset + sizeof(u32), oaknut::XReg{static_cast<int>(mrs.GetRt())}, src_reg);
        } else if (auto msr = MSR{words[i]}; msr.Verify() && msr.GetSystemReg() == TpidrEl0) {
            BranchToPatch(offset);
            WriteMsrHandler(offset + sizeof(u32), oaknut::XReg{static_cast<int>(msr.GetRt())});
        }
    }
}

Patcher::~Patcher() = default;

bool Patcher::PatchText(std::span<const u8> program_image, const Kernel::CodeSet::Segment& code) {
    // If we have patched modules but cannot reach the new module, then it needs its own patcher.
    const size_t image_size = program_image.size();

    // Check if we need split mode for large modules. A64 max takes 128MB
    // tests showed that, with update, some are larger. (In this case 208MB)
    bool use_split = false;
    if (image_size > MaxRelativeBranch) {
        if (total_program_size > 0) {
            return false;
        }
        use_split = true;
    } else if (total_program_size + image_size > MaxRelativeBranch && total_program_size > 0) {
        return false;
    }

    // Add a new module patch to our list
    modules.emplace_back();
    curr_patch = &modules.back();
    curr_patch->m_image_size = image_size;

    // The first word of the patch section is always a branch to the first instruction of the
    // module.
    if (use_split) {
        // curr_patch->m_branch_to_module_relocations.push_back({0, 0});
        curr_patch->m_branch_to_module_relocations_pre.push_back({0, 0});
    } else {
        curr_patch->m_branch_to_module_relocations.push_back({0, 0});
    }

    // Retrieve text segment data.
    const auto text = std::span{program_image}.subspan(code.offset, code.size);
    const auto text_words =
        std::span<const u32>{reinterpret_cast<const u32*>(text.data()), text.size() / sizeof(u32)};

    // Real code can start before ModuleCodeIndex (e.g. Switch 2 main has a function at +0x20).
    // Neutralize PAC there too, skipping only the entry branch and the MOD0 offset word, or a
    // PACIBSP left there signs LR while its rewritten AUTIBSP no longer strips it.
    for (u32 i = 2; i < std::min<u32>(ModuleCodeIndex, static_cast<u32>(text_words.size())); i++) {
        if (const auto pac = RewritePointerAuth(text_words[i])) {
            curr_patch->m_pac_rewrites.emplace_back(i, *pac);
        }
    }

    // Loop through instructions, finding the ones to patch.
    std::vector<PatchSite> sites;
    for (u32 i = ModuleCodeIndex; i < static_cast<u32>(text_words.size()); i++) {
        const u32 inst = text_words[i];

        // SVC, MRS Xn, TPIDR_EL0/TPIDRRO_EL0/CNTPCT_EL0/CNTFRQ_EL0, MSR TPIDR_EL0, Xn
        if (IsPatchSite(inst)) {
            sites.push_back({i, inst});
            continue;
        }

        if (auto exclusive = Exclusive{inst}; exclusive.Verify()) {
            curr_patch->m_exclusives.push_back(i);
            continue;
        }

        if (const auto pac = RewritePointerAuth(inst)) {
            curr_patch->m_pac_rewrites.emplace_back(i, *pac);
        }
    }

    if (use_split) {
        EmitSplit(sites, image_size);
    } else {
        for (const PatchSite& site : sites) {
            EmitSite(site, false);
        }
    }

    if (!curr_patch->m_pac_rewrites.empty()) {
        LOG_INFO(Core_ARM, "NCE: neutralized {} pointer-authentication instructions",
                 curr_patch->m_pac_rewrites.size());
    }

    // Determine patching mode for the final relocation step
    total_program_size += image_size;
    if (use_split) {
        this->mode = PatchMode::Split;
    } else {
        this->mode = image_size > MaxRelativeBranch ? PatchMode::PreText : PatchMode::PostData;
    }
    return true;
}

void Patcher::EmitSite(const PatchSite& site, bool pre) {
    const uintptr_t this_offset = site.index * sizeof(u32);
    // Relocate from patch to next instruction.
    const uintptr_t ret = this_offset + sizeof(u32);
    auto& cg = pre ? c_pre : c;

    // Relocate to pre- or post-patch
    if (pre) {
        this->BranchToPatchPre(this_offset);
    } else {
        this->BranchToPatch(this_offset);
    }

    if (auto svc = SVC{site.inst}; svc.Verify()) {
        if (pre) {
            WriteSvcTrampoline(ret, svc.GetValue(), c_pre, m_save_context_pre, m_load_context_pre);
        } else {
            WriteSvcTrampoline(ret, svc.GetValue(), c, m_save_context, m_load_context);
        }
    } else if (auto mrs = MRS{site.inst}; mrs.Verify()) {
        const auto dest_reg = oaknut::XReg{static_cast<int>(mrs.GetRt())};
        switch (mrs.GetSystemReg()) {
        case TpidrroEl0:
            WriteMrsHandler(ret, dest_reg, oaknut::SystemReg::TPIDRRO_EL0, cg);
            break;
        case TpidrEl0:
            WriteMrsHandler(ret, dest_reg, oaknut::SystemReg::TPIDR_EL0, cg);
            break;
        case CntpctEl0:
            WriteCntpctHandler(ret, dest_reg, cg);
            break;
        default:
            WriteCntfrqHandler(ret, dest_reg, cg);
            break;
        }
    } else {
        WriteMsrHandler(ret, oaknut::XReg{static_cast<int>(MSR{site.inst}.GetRt())}, cg);
    }
}

// A branch reaches 128 MiB, less than such a module is long, so it has a patch section before
// its text and one after its data. Patch code follows the order of the instructions in both. An
// instruction that reaches neither section is left to trap: with 180 MiB of code in a 270 MiB
// module those are the ones between 128 MiB and 142 MiB.
void Patcher::EmitSplit(std::span<const PatchSite> sites, size_t image_size) {
    // The branch back lies inside the patch code, up to two instructions from where the rules
    // below measure.
    constexpr ptrdiff_t Reach = static_cast<ptrdiff_t>(MaxRelativeBranch - 2 * sizeof(u32));
    const auto offset_of = [](const PatchSite& site) {
        return static_cast<ptrdiff_t>(site.index * sizeof(u32));
    };
    const auto drop_from = [](auto& list, ptrdiff_t cut) {
        std::erase_if(list, [cut](const auto& rel) { return rel.patch_offset >= cut; });
    };

    // Before the text: an instruction is as far from its patch code as it is into the text,
    // plus the patch code of the instructions after it.
    std::vector<ptrdiff_t> starts;
    size_t pre_count = 0;
    while (pre_count < sites.size() &&
           offset_of(sites[pre_count]) < static_cast<ptrdiff_t>(MaxRelativeBranch)) {
        starts.push_back(c_pre.offset());
        EmitSite(sites[pre_count++], true);
    }
    starts.push_back(c_pre.offset());

    size_t fits = 0;
    ptrdiff_t farthest = std::numeric_limits<ptrdiff_t>::min();
    for (size_t n = 1; n <= pre_count; n++) {
        farthest = std::max(farthest, offset_of(sites[n - 1]) - starts[n - 1]);
        const auto section = static_cast<ptrdiff_t>(
            Common::AlignUp(static_cast<size_t>(starts[n]), Core::Memory::YUZU_PAGESIZE));
        if (farthest + section > Reach) {
            break;
        }
        fits = n;
    }
    if (fits < pre_count) {
        const ptrdiff_t cut = starts[fits];
        m_patch_instructions_pre.resize(static_cast<size_t>(cut) / sizeof(u32));
        drop_from(curr_patch->m_branch_to_pre_patch_relocations, cut);
        drop_from(curr_patch->m_branch_to_module_relocations_pre, cut);
        drop_from(curr_patch->m_write_module_pc_relocations_pre, cut);
        drop_from(curr_patch->m_trampolines_pre, cut);
    }

    // After the data: an instruction is as far from the end of its patch code as the rest of
    // the module is long, plus the patch code up to there.
    const ptrdiff_t base = c.offset();
    std::vector<ptrdiff_t> ends;
    const auto emit_from = [&](size_t first) {
        ends.clear();
        for (size_t k = first; k < sites.size(); k++) {
            EmitSite(sites[k], false);
            ends.push_back(c.offset());
        }
    };
    emit_from(fits);

    const size_t rest = sites.size() - fits;
    std::vector<ptrdiff_t> farthest_after(rest + 1, std::numeric_limits<ptrdiff_t>::min());
    for (size_t i = rest; i-- > 0;) {
        farthest_after[i] =
            std::max(farthest_after[i + 1], ends[i] - offset_of(sites[fits + i]));
    }
    // Leaving the first of them out brings the patch code of the others closer.
    size_t trapped = 0;
    while (trapped < rest) {
        const ptrdiff_t saved = (trapped == 0 ? base : ends[trapped - 1]) - base;
        if (static_cast<ptrdiff_t>(image_size) + farthest_after[trapped] - saved <= Reach) {
            break;
        }
        trapped++;
    }
    if (trapped != 0) {
        m_patch_instructions.resize(static_cast<size_t>(base) / sizeof(u32));
        drop_from(curr_patch->m_branch_to_patch_relocations, base);
        drop_from(curr_patch->m_branch_to_module_relocations, base);
        drop_from(curr_patch->m_write_module_pc_relocations, base);
        drop_from(curr_patch->m_trampolines, base);
        emit_from(fits + trapped);
        for (size_t k = fits; k < fits + trapped; k++) {
            curr_patch->m_traps.emplace_back(offset_of(sites[k]), sites[k].inst);
        }
    }

    LOG_INFO(Core_ARM,
             "NCE: module of {:#x} bytes: {} patched instructions use the section before the "
             "text, {} the one after the data, {} trap",
             image_size, fits, rest - trapped, trapped);
}

bool Patcher::CanRelocate() const {
    BranchLayout layout{mode, static_cast<ptrdiff_t>(GetSectionSize()),
                        static_cast<ptrdiff_t>(GetPreSectionSize()), 0,
                        static_cast<ptrdiff_t>(total_program_size)};
    const auto to_patch = [&](const Relocation& rel) {
        return InBranchRange(layout.ToPatch(rel.patch_offset, rel.module_offset));
    };
    const auto from_patch = [&](const Relocation& rel) {
        return InBranchRange(layout.FromPatch(rel.patch_offset, rel.module_offset));
    };
    const auto to_pre_patch = [&](const Relocation& rel) {
        return InBranchRange(layout.ToPrePatch(rel.patch_offset, rel.module_offset));
    };
    const auto from_pre_patch = [&](const Relocation& rel) {
        return InBranchRange(layout.FromPrePatch(rel.patch_offset, rel.module_offset));
    };
    for (const ModulePatch& patch : modules) {
        layout.image_size = static_cast<ptrdiff_t>(patch.m_image_size);
        if (!std::ranges::all_of(patch.m_branch_to_patch_relocations, to_patch) ||
            !std::ranges::all_of(patch.m_branch_to_module_relocations, from_patch)) {
            return false;
        }
        if (mode == PatchMode::Split &&
            (!std::ranges::all_of(patch.m_branch_to_pre_patch_relocations, to_pre_patch) ||
             !std::ranges::all_of(patch.m_branch_to_module_relocations_pre, from_pre_patch))) {
            return false;
        }
        layout.total_size -= layout.image_size;
    }
    return true;
}

std::span<const u32> Patcher::RelocateRuntimeTls(
    s64 patch_offset, std::vector<InstructionPatch>& out_patches) {
    const auto& patch = modules.front();
    const auto can_branch = [](s64 offset) {
        return offset % 4 == 0 && offset >= -static_cast<s64>(MaxRelativeBranch) && offset < static_cast<s64>(MaxRelativeBranch);
    };
    out_patches.clear();
    out_patches.reserve(patch.m_branch_to_patch_relocations.size());
    for (const auto& rel : patch.m_branch_to_patch_relocations) {
        const s64 offset = patch_offset + rel.patch_offset - static_cast<s64>(rel.module_offset);
        if (!can_branch(offset)) {return {};}
        u32 replacement;
        oaknut::CodeGenerator rc{&replacement};
        rc.B(offset);
        out_patches.push_back({rel.module_offset, replacement});
    }
    for (const auto& rel : patch.m_branch_to_module_relocations) {
        const s64 offset = static_cast<s64>(rel.module_offset) - patch_offset - rel.patch_offset;
        if (!can_branch(offset)) {return {};}
        oaknut::CodeGenerator rc{m_patch_instructions.data() + rel.patch_offset / sizeof(u32)};
        rc.B(offset);
    }
    return m_patch_instructions;
}

bool Patcher::RelocateAndCopy(Common::ProcessAddress load_base, const Kernel::CodeSet::Segment& code, std::vector<u8>& program_image, EntryTrampolines* out_trampolines, TrappedInstructions* out_traps) {
    const size_t patch_size = GetSectionSize();
    const size_t pre_patch_size = GetPreSectionSize();

    const size_t image_size = (mode == PatchMode::Split) ? program_image.size() - pre_patch_size : program_image.size();

    // Retrieve text segment data.
    const auto text = std::span{program_image}.subspan(code.offset, code.size);
    const auto text_words =
        std::span<u32>{reinterpret_cast<u32*>(text.data()), text.size() / sizeof(u32)};

    // CanRelocate checks these same branches.
    const BranchLayout layout{mode, static_cast<ptrdiff_t>(patch_size),
                              static_cast<ptrdiff_t>(pre_patch_size),
                              static_cast<ptrdiff_t>(image_size),
                              static_cast<ptrdiff_t>(total_program_size)};

    const auto ApplyBranchToPatchRelocation = [&](u32* target, const Relocation& rel) {
        oaknut::CodeGenerator rc{target};
        rc.B(layout.ToPatch(rel.patch_offset, rel.module_offset));
    };

    const auto ApplyBranchToPatchRelocationPre = [&](u32* target, const Relocation& rel) {
        oaknut::CodeGenerator rc{target};
        rc.B(layout.ToPrePatch(rel.patch_offset, rel.module_offset));
    };

    const auto ApplyBranchToModuleRelocation = [&](u32* target, const Relocation& rel) {
        oaknut::CodeGenerator rc{target};
        rc.B(layout.FromPatch(rel.patch_offset, rel.module_offset));
    };

    const auto ApplyBranchToModuleRelocationPre = [&](u32* target, const Relocation& rel) {
        oaknut::CodeGenerator rc{target};
        rc.B(layout.FromPrePatch(rel.patch_offset, rel.module_offset));
    };

    const auto RebasePatch = [&](ptrdiff_t patch_offset) {
        if (mode == PatchMode::PreText) {
            return GetInteger(load_base) + patch_offset;
        } else {
            return GetInteger(load_base) + total_program_size + patch_offset;
        }
    };

    const auto RebasePc = [&](uintptr_t module_offset) {
        if (mode == PatchMode::PreText) {
            return GetInteger(load_base) + patch_size + module_offset;
        }
        if (mode == PatchMode::Split) {
            return GetInteger(load_base) + pre_patch_size + module_offset;
        }

        return GetInteger(load_base) + module_offset;
    };

    // We are now ready to relocate!
    auto& patch = modules[m_relocate_module_index++];

    if (mode == PatchMode::Split) {
        for (const Relocation& rel : patch.m_branch_to_pre_patch_relocations) {
            ApplyBranchToPatchRelocationPre(text_words.data() + rel.module_offset / sizeof(u32), rel);
        }
        LOG_DEBUG(Core_ARM, "applied Pre: {}", patch.m_branch_to_pre_patch_relocations.size());

        for (const Relocation& rel : patch.m_branch_to_patch_relocations) {
            ApplyBranchToPatchRelocation(text_words.data() + rel.module_offset / sizeof(u32), rel);
        }
        LOG_DEBUG(Core_ARM, "applied Post: {}", patch.m_branch_to_patch_relocations.size());

        for (const Relocation& rel : patch.m_branch_to_module_relocations_pre) {
            ApplyBranchToModuleRelocationPre(m_patch_instructions_pre.data() + rel.patch_offset / sizeof(u32), rel);
        }
        LOG_DEBUG(Core_ARM, "aplied Pre-module {}", patch.m_branch_to_module_relocations_pre.size());

        for (const Relocation& rel : patch.m_branch_to_module_relocations) {
            ApplyBranchToModuleRelocation(m_patch_instructions.data() + rel.patch_offset / sizeof(u32), rel);
        }
        LOG_DEBUG(Core_ARM, "applied Post-module {}", patch.m_branch_to_module_relocations.size());

        // Pre
        for (const Relocation& rel : patch.m_write_module_pc_relocations_pre) {
            oaknut::CodeGenerator rc{m_patch_instructions_pre.data() + rel.patch_offset / sizeof(u32)};
            rc.dx(RebasePc(rel.module_offset));
        }
        // Post
        for (const Relocation& rel : patch.m_write_module_pc_relocations) {
            oaknut::CodeGenerator rc{m_patch_instructions.data() + rel.patch_offset / sizeof(u32)};
            rc.dx(RebasePc(rel.module_offset));
        }

        // Trampolines (split pre + post)
        for (const Trampoline& rel : patch.m_trampolines_pre) {
            out_trampolines->insert({RebasePc(rel.module_offset),
                                     GetInteger(load_base) + rel.patch_offset});
        }
        for (const Trampoline& rel : patch.m_trampolines) {
            out_trampolines->insert({RebasePc(rel.module_offset),
                                     GetInteger(load_base) + pre_patch_size + image_size + rel.patch_offset});
        }

        if (!m_patch_instructions_pre.empty()) {
            u32 insn = m_patch_instructions_pre[0];
            if ((insn & 0xFC000000) == 0x14000000) {
                s32 imm26 = insn & 0x3FFFFFF;
                // Sign extend
                if (imm26 & 0x2000000) imm26 |= 0xFC000000;
            }
        }
    } else {
        for (const Relocation& rel : patch.m_branch_to_patch_relocations) {
            ApplyBranchToPatchRelocation(text_words.data() + rel.module_offset / sizeof(u32), rel);
        }
        for (const Relocation& rel : patch.m_branch_to_module_relocations) {
            ApplyBranchToModuleRelocation(m_patch_instructions.data() + rel.patch_offset / sizeof(u32),
                                          rel);
        }

        // Rewrite PC constants
        for (const Relocation& rel : patch.m_write_module_pc_relocations) {
            oaknut::CodeGenerator rc{m_patch_instructions.data() + rel.patch_offset / sizeof(u32)};
            rc.dx(RebasePc(rel.module_offset));
        }
    }

    if (mode != PatchMode::Split) {
        for (const Trampoline& rel : patch.m_trampolines) {
            out_trampolines->insert({RebasePc(rel.module_offset), RebasePatch(rel.patch_offset)});
        }
    }

    // Cortex-A57 seems to treat all exclusives as ordered, but newer processors do not.
    // Convert to ordered to preserve this assumption.
    for (const ModuleTextAddress i : patch.m_exclusives) {
        auto exclusive = Exclusive{text_words[i]};
        text_words[i] = exclusive.AsOrdered();
    }

    for (const auto& [i, word] : patch.m_pac_rewrites) {
        text_words[i] = word;
    }

    for (const auto& [module_offset, inst] : patch.m_traps) {
        text_words[module_offset / sizeof(u32)] = TrapInstruction;
        if (out_traps) {
            out_traps->insert({RebasePc(module_offset), inst});
        }
    }

    // Remove the patched module size from the total. This is done so total_program_size
    // always represents the distance from the currently patched module to the patch section.
    total_program_size -= image_size;

    // Only copy to the program image of the last module
    if (m_relocate_module_index == modules.size()) {
        if (this->mode == PatchMode::PreText) {
            ASSERT(image_size == total_program_size);
            std::memcpy(program_image.data(), m_patch_instructions.data(),
                        m_patch_instructions.size() * sizeof(u32));
        } else if (this->mode == PatchMode::Split) {
            const size_t current_size = program_image.size();
            program_image.resize(current_size + patch_size);
            // Copy pre-patch buffer to the beginning
            std::memcpy(program_image.data(), m_patch_instructions_pre.data(),
                        m_patch_instructions_pre.size() * sizeof(u32));
            // Same for post-patch buffer to the end
            std::memcpy(program_image.data() + current_size, m_patch_instructions.data(),
                        m_patch_instructions.size() * sizeof(u32));
        } else {
            program_image.resize(image_size + patch_size);
            std::memcpy(program_image.data() + image_size, m_patch_instructions.data(),
                        m_patch_instructions.size() * sizeof(u32));
        }
        return true;
    }

    return false;
}

size_t Patcher::GetSectionSize() const noexcept {
    return Common::AlignUp(m_patch_instructions.size() * sizeof(u32), Core::Memory::YUZU_PAGESIZE);
}

size_t Patcher::GetPreSectionSize() const noexcept {
    return Common::AlignUp(m_patch_instructions_pre.size() * sizeof(u32), Core::Memory::YUZU_PAGESIZE);
}

void Patcher::WriteLoadContext(oaknut::VectorCodeGenerator& cg) {
    // This function was called, which modifies X30, so use that as a scratch register.
    // SP contains the guest X30, so save our return X30 to SP + 8, since we have allocated 16 bytes
    // of stack.
    cg.STR(X30, SP, 8);
    cg.MRS(X30, oaknut::SystemReg::TPIDR_EL0);
    cg.LDR(X30, X30, offsetof(NativeExecutionParameters, native_context));

    // Load system registers.
    cg.LDR(W0, X30, offsetof(GuestContext, fpsr));
    cg.MSR(oaknut::SystemReg::FPSR, X0);
    cg.LDR(W0, X30, offsetof(GuestContext, fpcr));
    cg.MSR(oaknut::SystemReg::FPCR, X0);
    cg.LDR(W0, X30, offsetof(GuestContext, nzcv));
    cg.MSR(oaknut::SystemReg::NZCV, X0);

    // Load all vector registers.
    static constexpr size_t VEC_OFF = offsetof(GuestContext, vector_registers);
    for (int i = 0; i <= 30; i += 2) {
        cg.LDP(oaknut::QReg{i}, oaknut::QReg{i + 1}, X30, VEC_OFF + 16 * i);
    }

    // Load all general-purpose registers except X30.
    for (int i = 0; i <= 28; i += 2) {
        cg.LDP(oaknut::XReg{i}, oaknut::XReg{i + 1}, X30, 8 * i);
    }

    // Reload our return X30 from the stack and return.
    // The patch code will reload the guest X30 for us.
    cg.LDR(X30, SP, 8);
    cg.RET();
}

void Patcher::WriteSaveContext(oaknut::VectorCodeGenerator& cg) {
    // This function was called, which modifies X30, so use that as a scratch register.
    // SP contains the guest X30, so save our X30 to SP + 8, since we have allocated 16 bytes of
    // stack.
    cg.STR(X30, SP, 8);
    cg.MRS(X30, oaknut::SystemReg::TPIDR_EL0);
    cg.LDR(X30, X30, offsetof(NativeExecutionParameters, native_context));

    // Store all general-purpose registers except X30.
    for (int i = 0; i <= 28; i += 2) {
        cg.STP(oaknut::XReg{i}, oaknut::XReg{i + 1}, X30, 8 * i);
    }

    // Store all vector registers.
    static constexpr size_t VEC_OFF = offsetof(GuestContext, vector_registers);
    for (int i = 0; i <= 30; i += 2) {
        cg.STP(oaknut::QReg{i}, oaknut::QReg{i + 1}, X30, VEC_OFF + 16 * i);
    }

    // Store guest system registers, X30 and SP, using X0 as a scratch register.
    cg.STR(X0, SP, PRE_INDEXED, -16);
    cg.LDR(X0, SP, 16);
    cg.STR(X0, X30, 8 * 30);
    cg.ADD(X0, SP, 32);
    cg.STR(X0, X30, offsetof(GuestContext, sp));
    cg.MRS(X0, oaknut::SystemReg::FPSR);
    cg.STR(W0, X30, offsetof(GuestContext, fpsr));
    cg.MRS(X0, oaknut::SystemReg::FPCR);
    cg.STR(W0, X30, offsetof(GuestContext, fpcr));
    cg.MRS(X0, oaknut::SystemReg::NZCV);
    cg.STR(W0, X30, offsetof(GuestContext, nzcv));
    cg.LDR(X0, SP, POST_INDEXED, 16);

    // Reload our return X30 from the stack, and return.
    cg.LDR(X30, SP, 8);
    cg.RET();
}

void Patcher::WriteSvcTrampoline(ModuleDestLabel module_dest, u32 svc_id, oaknut::VectorCodeGenerator& cg, oaknut::Label& save_ctx, oaknut::Label& load_ctx) {
    // Determine if we're writing to the pre-patch buffer
    const bool is_pre = (&cg == &c_pre);

    // We are about to start saving state, so we need to lock the context.
    this->LockContext(cg);

    // Store guest X30 to the stack. Then, save the context and restore the stack.
    // This will save all registers except PC, but we know PC at patch time.
    cg.STR(X30, SP, PRE_INDEXED, -16);
    cg.BL(save_ctx);
    cg.LDR(X30, SP, POST_INDEXED, 16);

    // Now that we've saved all registers, we can use any registers as scratch.
    // Store PC + 4 to arm interface, since we know the instruction offset from the entry point.
    oaknut::Label pc_after_svc;
    cg.MRS(X1, oaknut::SystemReg::TPIDR_EL0);
    cg.LDR(X1, X1, offsetof(NativeExecutionParameters, native_context));
    cg.LDR(X2, pc_after_svc);
    cg.STR(X2, X1, offsetof(GuestContext, pc));

    // Store SVC number to execute when we return
    cg.MOV(X2, svc_id);
    cg.STR(W2, X1, offsetof(GuestContext, svc));

    // We are calling a SVC. Clear esr_el1 and return it.
    static_assert(std::is_same_v<std::underlying_type_t<HaltReason>, u64>);
    oaknut::Label retry;
    cg.ADD(X2, X1, offsetof(GuestContext, esr_el1));
    cg.l(retry);
    cg.LDAXR(X0, X2);
    cg.STLXR(W3, XZR, X2);
    cg.CBNZ(W3, retry);

    // Add "calling SVC" flag. Since this is X0, this is now our return value.
    cg.ORR(X0, X0, static_cast<u64>(HaltReason::SupervisorCall));

    // Offset the GuestContext pointer to the HostContext member.
    // STP has limited range of [-512, 504] which we can't reach otherwise
    // NB: Due to this all offsets below are from the start of HostContext.
    cg.ADD(X1, X1, offsetof(GuestContext, host_ctx));

    // Reload host TPIDR_EL0 and SP.
    static_assert(offsetof(HostContext, host_sp) + 8 == offsetof(HostContext, host_tpidr_el0));
    cg.LDP(X2, X3, X1, offsetof(HostContext, host_sp));
    cg.MOV(SP, X2);
    cg.MSR(oaknut::SystemReg::TPIDR_EL0, X3);

    // Load callee-saved host registers and return to host.
    static constexpr size_t HOST_REGS_OFF = offsetof(HostContext, host_saved_regs);
    static constexpr size_t HOST_VREGS_OFF = offsetof(HostContext, host_saved_vregs);
    cg.LDP(X19, X20, X1, HOST_REGS_OFF);
    cg.LDP(X21, X22, X1, HOST_REGS_OFF + 2 * sizeof(u64));
    cg.LDP(X23, X24, X1, HOST_REGS_OFF + 4 * sizeof(u64));
    cg.LDP(X25, X26, X1, HOST_REGS_OFF + 6 * sizeof(u64));
    cg.LDP(X27, X28, X1, HOST_REGS_OFF + 8 * sizeof(u64));
    cg.LDP(X29, X30, X1, HOST_REGS_OFF + 10 * sizeof(u64));
    cg.LDP(Q8, Q9, X1, HOST_VREGS_OFF);
    cg.LDP(Q10, Q11, X1, HOST_VREGS_OFF + 2 * sizeof(u128));
    cg.LDP(Q12, Q13, X1, HOST_VREGS_OFF + 4 * sizeof(u128));
    cg.LDP(Q14, Q15, X1, HOST_VREGS_OFF + 6 * sizeof(u128));
    cg.RET();

    // Write the post-SVC trampoline address, which will jump back to the guest after restoring its
    // state.
    if (is_pre) {
        curr_patch->m_trampolines_pre.push_back({cg.offset(), module_dest});
    } else {
        curr_patch->m_trampolines.push_back({cg.offset(), module_dest});
    }

    // Host called this location. Save the return address so we can
    // unwind the stack properly when jumping back.
    cg.MRS(X2, oaknut::SystemReg::TPIDR_EL0);
    cg.LDR(X2, X2, offsetof(NativeExecutionParameters, native_context));
    cg.ADD(X0, X2, offsetof(GuestContext, host_ctx));
    cg.STR(X30, X0, offsetof(HostContext, host_saved_regs) + 11 * sizeof(u64));

    // Reload all guest registers except X30 and PC.
    // The function also expects 16 bytes of stack already allocated.
    cg.STR(X30, SP, PRE_INDEXED, -16);
    cg.BL(load_ctx);
    cg.LDR(X30, SP, POST_INDEXED, 16);

    // Use X1 as a scratch register to restore X30.
    cg.STR(X1, SP, PRE_INDEXED, -16);
    cg.MRS(X1, oaknut::SystemReg::TPIDR_EL0);
    cg.LDR(X1, X1, offsetof(NativeExecutionParameters, native_context));
    cg.LDR(X30, X1, offsetof(GuestContext, cpu_registers) + sizeof(u64) * 30);
    cg.LDR(X1, SP, POST_INDEXED, 16);

    // Unlock the context.
    this->UnlockContext(cg);

    // Jump back to the instruction after the emulated SVC.
    if (&cg == &c_pre)
        this->BranchToModulePre(module_dest);
    else
        this->BranchToModule(module_dest);

    // Store PC after call.
    cg.l(pc_after_svc);
    if (&cg == &c_pre)
        this->WriteModulePcPre(module_dest);
    else
        this->WriteModulePc(module_dest);
}

void Patcher::WriteMrsHandler(ModuleDestLabel module_dest, oaknut::XReg dest_reg,
                              oaknut::SystemReg src_reg, oaknut::VectorCodeGenerator& cg) {
    // Retrieve emulated TLS register from GuestContext.
    cg.MRS(dest_reg, oaknut::SystemReg::TPIDR_EL0);
    if (src_reg == oaknut::SystemReg::TPIDRRO_EL0) {
        cg.LDR(dest_reg, dest_reg, offsetof(NativeExecutionParameters, tpidrro_el0));
    } else {
        cg.LDR(dest_reg, dest_reg, offsetof(NativeExecutionParameters, tpidr_el0));
    }

    // Jump back to the instruction after the emulated MRS.
    if (&cg == &c_pre)
        this->BranchToModulePre(module_dest);
    else
        this->BranchToModule(module_dest);
}

void Patcher::WriteMsrHandler(ModuleDestLabel module_dest, oaknut::XReg src_reg, oaknut::VectorCodeGenerator& cg) {
    const auto scratch_reg = src_reg.index() == 0 ? X1 : X0;
    cg.STR(scratch_reg, SP, PRE_INDEXED, -16);

    // Save guest value to NativeExecutionParameters::tpidr_el0.
    cg.MRS(scratch_reg, oaknut::SystemReg::TPIDR_EL0);
    cg.STR(src_reg, scratch_reg, offsetof(NativeExecutionParameters, tpidr_el0));

    // Restore scratch register.
    cg.LDR(scratch_reg, SP, POST_INDEXED, 16);

    // Jump back to the instruction after the emulated MSR.
    if (&cg == &c_pre)
        this->BranchToModulePre(module_dest);
    else
        this->BranchToModule(module_dest);
}

void Patcher::WriteCntfrqHandler(ModuleDestLabel module_dest, oaknut::XReg dest_reg, oaknut::VectorCodeGenerator& cg) {
    cg.MOV(dest_reg, Common::g_wall_clock.GetGuestCNTFRQ());

    // Jump back to the instruction after the emulated MRS.
    if (&cg == &c_pre)
        this->BranchToModulePre(module_dest);
    else
        this->BranchToModule(module_dest);
}

void Patcher::WriteCntpctHandler(ModuleDestLabel module_dest, oaknut::XReg dest_reg, oaknut::VectorCodeGenerator& cg) {
#if defined(HAS_NCE)
    static Common::WallClock clock(false, 1);
#else
    static Common::WallClock clock(true, 1);
#endif
    clock.SetGuestCNTFRQ(Common::g_wall_clock.GetGuestCNTFRQ());
    const auto factor = clock.GetGuestCNTFRQFactor();
    const auto raw_factor = std::bit_cast<std::array<u64, 2>>(factor);

    const auto use_x2_x3 = dest_reg.index() == 0 || dest_reg.index() == 1;
    oaknut::XReg scratch0 = use_x2_x3 ? X2 : X0;
    oaknut::XReg scratch1 = use_x2_x3 ? X3 : X1;

    oaknut::Label factorlo;
    oaknut::Label factorhi;

    // Save scratches.
    cg.STP(scratch0, scratch1, SP, PRE_INDEXED, -16);

    // Load counter value.
    cg.MRS(dest_reg, oaknut::SystemReg::CNTVCT_EL0);

    // Load scaling factor.
    cg.LDR(scratch0, factorlo);
    cg.LDR(scratch1, factorhi);

    // Multiply low bits and get result.
    cg.UMULH(scratch0, dest_reg, scratch0);

    // Multiply high bits and add low bit result.
    cg.MADD(dest_reg, dest_reg, scratch1, scratch0);

    // Reload scratches.
    cg.LDP(scratch0, scratch1, SP, POST_INDEXED, 16);

    // Jump back to the instruction after the emulated MRS.
    if (&cg == &c_pre)
        this->BranchToModulePre(module_dest);
    else
        this->BranchToModule(module_dest);

    // Scaling factor constant values.
    cg.l(factorlo);
    cg.dx(raw_factor[0]);
    cg.l(factorhi);
    cg.dx(raw_factor[1]);
}

void Patcher::LockContext(oaknut::VectorCodeGenerator& cg) {
    oaknut::Label retry;

    // Save scratches.
    cg.STP(X0, X1, SP, PRE_INDEXED, -16);

    // Reload lock pointer.
    cg.l(retry);
    cg.CLREX();
    cg.MRS(X0, oaknut::SystemReg::TPIDR_EL0);
    cg.ADD(X0, X0, offsetof(NativeExecutionParameters, lock));

    static_assert(SpinLockLocked == 0);

    // Load-linked with acquire ordering.
    cg.LDAXR(W1, X0);

    // If the value was SpinLockLocked, clear monitor and retry.
    cg.CBZ(W1, retry);

    // Store-conditional SpinLockLocked with relaxed ordering.
    cg.STXR(W1, WZR, X0);

    // If we failed to store, retry.
    cg.CBNZ(W1, retry);

    // We succeeded! Reload scratches.
    cg.LDP(X0, X1, SP, POST_INDEXED, 16);
}

void Patcher::UnlockContext(oaknut::VectorCodeGenerator& cg) {
    // Save scratches.
    cg.STP(X0, X1, SP, PRE_INDEXED, -16);

    // Load lock pointer.
    cg.MRS(X0, oaknut::SystemReg::TPIDR_EL0);
    cg.ADD(X0, X0, offsetof(NativeExecutionParameters, lock));

    // Load SpinLockUnlocked.
    cg.MOV(W1, SpinLockUnlocked);

    // Store value with release ordering.
    cg.STLR(W1, X0);

    // Load scratches.
    cg.LDP(X0, X1, SP, POST_INDEXED, 16);
}

} // namespace Core::NCE
