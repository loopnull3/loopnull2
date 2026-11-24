// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace LoopNative {

/// MRS/MSR system register fields (bits 5..19 of the instruction: o0:op1:CRn:CRm:op2).
namespace SysReg {
constexpr std::uint32_t Encode(std::uint32_t op0, std::uint32_t op1, std::uint32_t crn,
                               std::uint32_t crm, std::uint32_t op2) {
    return ((op0 - 2) << 14) | (op1 << 11) | (crn << 7) | (crm << 3) | op2;
}
constexpr std::uint32_t TPIDR_EL0 = Encode(3, 3, 13, 0, 2);
constexpr std::uint32_t TPIDRRO_EL0 = Encode(3, 3, 13, 0, 3);
constexpr std::uint32_t CNTFRQ_EL0 = Encode(3, 3, 14, 0, 0);
constexpr std::uint32_t CNTPCT_EL0 = Encode(3, 3, 14, 0, 1);
constexpr std::uint32_t CNTVCT_EL0 = Encode(3, 3, 14, 0, 2);
constexpr std::uint32_t CTR_EL0 = Encode(3, 3, 0, 0, 1);
constexpr std::uint32_t DCZID_EL0 = Encode(3, 3, 0, 0, 7);
} // namespace SysReg

/// What running a module's code natively has to intercept: supervisor calls into the Horizon
/// kernel and reads of registers whose host value differs from the console's (the thread
/// pointer, the counter and its frequency, cache geometry).
struct Arm64ScanResult {
    std::uint64_t instructions = 0;
    std::uint64_t svc_sites = 0;
    std::array<std::uint32_t, 0x100> svc_by_id{}; ///< SVC #imm histogram (imm < 0x100)
    std::uint64_t mrs_tpidrro_el0 = 0;
    std::uint64_t mrs_tpidr_el0 = 0;
    std::uint64_t msr_tpidr_el0 = 0;
    std::uint64_t mrs_cntpct_el0 = 0;
    std::uint64_t mrs_cntvct_el0 = 0;
    std::uint64_t mrs_cntfrq_el0 = 0;
    std::uint64_t mrs_ctr_el0 = 0;
    std::uint64_t mrs_dczid_el0 = 0;

    /// Instructions a native runner patches at load time.
    [[nodiscard]] std::uint64_t PatchSites() const {
        return svc_sites + mrs_tpidrro_el0 + mrs_tpidr_el0 + msr_tpidr_el0 + mrs_cntpct_el0 +
               mrs_cntvct_el0 + mrs_cntfrq_el0 + mrs_ctr_el0 + mrs_dczid_el0;
    }
};

[[nodiscard]] Arm64ScanResult ScanArm64(std::span<const std::uint8_t> code);

} // namespace LoopNative
