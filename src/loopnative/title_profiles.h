// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loopnative/device.h"

namespace LoopNative {

/// One setting LoopNative starts a game with: its config key (Settings linkage label) and the
/// value as Setting::LoadString takes it (enums as their numeric value, bools as true/false).
struct SettingOverride {
    std::string_view key;
    std::string_view value;
};

/// How a game's memory need compares with what this phone can give it.
enum class MemoryFit {
    Fits,     ///< within the RAM Android leaves a foreground game
    Tight,    ///< up to ~1.6x that: storage-backed guest memory makes room
    Unlikely, ///< far beyond it: expect closures even with storage-backed memory
};

struct TitleProfile {
    std::uint64_t program_id;
    std::string_view name;
    /// Peak host RAM the game takes on the emulator core, in GiB. An estimate from the upstream
    /// test reports cited in title_profiles.cpp, not a measurement on this phone.
    float ram_need_gib;
    /// Whether anyone reported the game running on an 8 GB phone.
    bool reported_on_8gb;
    std::span<const SettingOverride> overrides;
    std::string_view note;
    /// On phones with 12 GB or more (Galaxy S26 Exynos, Fold5): what changes from `overrides`.
    std::span<const SettingOverride> high_tier_overrides{};
};

/// What LoopNative starts one game with on one phone.
struct SessionPlan {
    const TitleProfile* profile = nullptr; ///< null: no title profile, device defaults only
    MemoryFit memory_fit = MemoryFit::Fits;
    bool storage_backed_memory = false;
    std::uint32_t start_fps_limit = 0;
    std::vector<std::pair<std::string, std::string>> overrides;
};

[[nodiscard]] std::span<const TitleProfile> AllTitleProfiles();
[[nodiscard]] const TitleProfile* FindTitleProfile(std::uint64_t program_id);
[[nodiscard]] MemoryFit FitOf(float ram_need_gib, std::uint64_t usable_ram_bytes);
[[nodiscard]] SessionPlan PlanSession(const DeviceTuning& tuning, std::uint64_t program_id);
[[nodiscard]] std::string_view ToString(MemoryFit fit);
[[nodiscard]] std::optional<std::uint64_t> ParseProgramId(std::string_view hex);

} // namespace LoopNative
