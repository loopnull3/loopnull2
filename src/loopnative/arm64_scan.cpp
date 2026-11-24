// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstring>

#include "loopnative/arm64_scan.h"

namespace LoopNative {

Arm64ScanResult ScanArm64(std::span<const std::uint8_t> code) {
    Arm64ScanResult result;
    const std::size_t count = code.size() / sizeof(std::uint32_t);
    result.instructions = count;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t insn;
        std::memcpy(&insn, code.data() + i * sizeof(insn), sizeof(insn));

        // SVC #imm16: 1101 0100 000 imm16 000 01
        if ((insn & 0xFFE0001FU) == 0xD4000001U) {
            const std::uint32_t imm = (insn >> 5) & 0xFFFFU;
            ++result.svc_sites;
            if (imm < result.svc_by_id.size()) {
                ++result.svc_by_id[imm];
            }
            continue;
        }

        // MRS Xt, <sysreg>: 1101 0101 0011 ...; MSR <sysreg>, Xt: 1101 0101 0001 ...
        const bool mrs = (insn & 0xFFF00000U) == 0xD5300000U;
        const bool msr = (insn & 0xFFF00000U) == 0xD5100000U;
        if (!mrs && !msr) {
            continue;
        }
        const std::uint32_t sysreg = (insn >> 5) & 0x7FFFU;
        if (msr) {
            if (sysreg == SysReg::TPIDR_EL0) {
                ++result.msr_tpidr_el0;
            }
            continue;
        }
        switch (sysreg) {
        case SysReg::TPIDRRO_EL0:
            ++result.mrs_tpidrro_el0;
            break;
        case SysReg::TPIDR_EL0:
            ++result.mrs_tpidr_el0;
            break;
        case SysReg::CNTPCT_EL0:
            ++result.mrs_cntpct_el0;
            break;
        case SysReg::CNTVCT_EL0:
            ++result.mrs_cntvct_el0;
            break;
        case SysReg::CNTFRQ_EL0:
            ++result.mrs_cntfrq_el0;
            break;
        case SysReg::CTR_EL0:
            ++result.mrs_ctr_el0;
            break;
        case SysReg::DCZID_EL0:
            ++result.mrs_dczid_el0;
            break;
        default:
            break;
        }
    }
    return result;
}

} // namespace LoopNative
