// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <charconv>

#include "loopnative/title_profiles.h"

namespace LoopNative {

namespace {

constexpr double GiB = 1024.0 * 1024.0 * 1024.0;

// Setting values are the enums' numeric values (common/settings_enums.h):
// ResolutionSetup Res1_4X=0 Res1_2X=1 Res3_4X=2 Res1X=3; GpuAccuracy Low=0;
// VramUsageMode Conservative=0; ConsoleMode Handheld=0; SceneMemoryCap On=2;
// VSyncMode FifoRelaxed=3; ScalingFilter ScaleForce=5; AntiAliasing Fxaa=1.

// The Legend of Zelda: Ocarina of Time. Upstream (Hokkaido 0.7.1 README): playable on the
// emulator core at 0.5x, 16 GB recommended, 12 GB phones can run out of memory.
constexpr std::array OCARINA_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"gpu_accuracy", "0"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"vram_usage_mode", "0"},
    SettingOverride{"scene_memory_cap", "2"},
    SettingOverride{"use_docked_mode", "0"},
};

// Mario Kart World. Upstream: Hokkaido's own recommended preset (common/game_presets.h); Banana
// Native's profile reports 14 GiB in use on its engine and calls for a 16 GB phone. LoopNative
// adds 0.5x and the game's fixed 30 fps mode (mkw_30fps), which halves the GPU's work.
constexpr std::array MARIO_KART_WORLD_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"gpu_accuracy", "0"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"vram_usage_mode", "0"},
    SettingOverride{"use_reactive_flushing", "false"},
    SettingOverride{"use_vsync", "3"},
    SettingOverride{"use_docked_mode", "0"},
    SettingOverride{"scaling_filter", "5"},
    SettingOverride{"anti_aliasing", "1"},
    SettingOverride{"mkw_30fps", "true"},
    SettingOverride{"mkw_dynamic_fps", "false"},
};

// Pokemon Pokopia. Upstream: playable on the emulator core, also on 8 GB phones.
constexpr std::array POKOPIA_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"use_docked_mode", "0"},
};

// Fire Emblem: Fortune's Weave. Upstream: playable at 0.5x, "Present a frame ahead" on Auto.
constexpr std::array FIRE_EMBLEM_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"gpu_accuracy", "0"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"vram_usage_mode", "0"},
    SettingOverride{"use_docked_mode", "0"},
};

// Donkey Kong Bananza. Upstream (Banana Native profile): keeps 6-8 GB in use while playing.
constexpr std::array BANANZA_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"gpu_accuracy", "0"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"vram_usage_mode", "0"},
    SettingOverride{"use_docked_mode", "0"},
};

// 12 GB phones (the Galaxy S26 with Exynos 2600 above all, Hokkaido's own test phone): a
// 0.75x render where upstream reports the game running well there, then FSR to the screen
// (DeviceTuning::upscale_filter). Ocarina of Time, Fire Emblem and Bananza stay at the 0.5x
// upstream recommends on the S26.
constexpr std::array THREE_QUARTER_RENDER{
    SettingOverride{"resolution_setup", "2"},
};

constexpr std::array STAR_FOX_OVERRIDES{
    SettingOverride{"resolution_setup", "1"},
    SettingOverride{"use_asynchronous_shaders", "true"},
    SettingOverride{"vram_usage_mode", "0"},
    SettingOverride{"use_docked_mode", "0"},
};

constexpr std::array TITLE_PROFILES{
    TitleProfile{0x0400486024DEC000ULL, "The Legend of Zelda: Ocarina of Time", 9.5f, false,
                 OCARINA_OVERRIDES,
                 "Runs on the emulator core at 0.5x on 12-16 GB phones; scene loads need the most "
                 "memory."},
    TitleProfile{0x0400C3F00006E000ULL, "Mario Kart World", 12.0f, false,
                 MARIO_KART_WORLD_OVERRIDES,
                 "Its upstream preset plus 0.5x and the game's 30 fps mode.",
                 THREE_QUARTER_RENDER},
    TitleProfile{0x0400A3A01EE4C000ULL, "Pokemon Pokopia", 4.5f, true, POKOPIA_OVERRIDES,
                 "Reported playable on 8 GB phones.", THREE_QUARTER_RENDER},
    TitleProfile{0x04007EF01F8AE000ULL, "Fire Emblem: Fortune's Weave", 6.0f, false,
                 FIRE_EMBLEM_OVERRIDES, "Playable at 0.5x upstream."},
    TitleProfile{0x040074A01BF12000ULL, "Donkey Kong Bananza", 8.0f, false, BANANZA_OVERRIDES,
                 "Keeps 6-8 GB in use while playing."},
    TitleProfile{0x0400E88025B1A000ULL, "Star Fox", 6.0f, false, STAR_FOX_OVERRIDES,
                 "Memory need not measured upstream; estimate.", THREE_QUARTER_RENDER},
};

// Device defaults every LoopNative start gets before the title's own overrides.
void AddDeviceDefaults(const DeviceTuning& tuning, SessionPlan& plan) {
    plan.overrides.emplace_back("resolution_setup", std::to_string(tuning.resolution_setup));
    plan.overrides.emplace_back("use_asynchronous_shaders", "true");
    if (tuning.ram_tier == RamTier::Gb6OrLess || tuning.ram_tier == RamTier::Gb8) {
        plan.overrides.emplace_back("vram_usage_mode", "0");
        plan.overrides.emplace_back("use_docked_mode", "0");
    }
}

void Override(SessionPlan& plan, std::string_view key, std::string_view value) {
    const auto it = std::ranges::find_if(plan.overrides,
                                         [key](const auto& entry) { return entry.first == key; });
    if (it != plan.overrides.end()) {
        it->second = std::string(value);
    } else {
        plan.overrides.emplace_back(std::string(key), std::string(value));
    }
}

/// A render below 1x is upscaled to the screen with the phone's upscaler (FSR on Xclipse).
void ApplyUpscaler(const DeviceTuning& tuning, SessionPlan& plan) {
    if (tuning.upscale_filter < 0) {
        return;
    }
    const auto resolution = std::ranges::find_if(
        plan.overrides, [](const auto& entry) { return entry.first == "resolution_setup"; });
    if (resolution == plan.overrides.end() || resolution->second == "3") {
        return;
    }
    Override(plan, "scaling_filter", std::to_string(tuning.upscale_filter));
}

} // namespace

std::span<const TitleProfile> AllTitleProfiles() {
    return TITLE_PROFILES;
}

const TitleProfile* FindTitleProfile(std::uint64_t program_id) {
    const auto it = std::ranges::find(TITLE_PROFILES, program_id, &TitleProfile::program_id);
    return it == TITLE_PROFILES.end() ? nullptr : &*it;
}

MemoryFit FitOf(float ram_need_gib, std::uint64_t usable_ram_bytes) {
    const double usable_gib = static_cast<double>(usable_ram_bytes) / GiB;
    if (usable_gib <= 0.0 || ram_need_gib <= usable_gib) {
        return MemoryFit::Fits;
    }
    if (ram_need_gib <= usable_gib * 1.6) {
        return MemoryFit::Tight;
    }
    return MemoryFit::Unlikely;
}

SessionPlan PlanSession(const DeviceTuning& tuning, std::uint64_t program_id) {
    SessionPlan plan;
    plan.start_fps_limit = tuning.start_fps_limit;
    AddDeviceDefaults(tuning, plan);

    plan.profile = FindTitleProfile(program_id);
    if (plan.profile == nullptr) {
        ApplyUpscaler(tuning, plan);
        return plan;
    }
    for (const auto& entry : plan.profile->overrides) {
        Override(plan, entry.key, entry.value);
    }
    if (tuning.ram_tier == RamTier::Gb12 || tuning.ram_tier == RamTier::Gb16Plus) {
        for (const auto& entry : plan.profile->high_tier_overrides) {
            Override(plan, entry.key, entry.value);
        }
    }
    // A phone with room to spare keeps the user's resolution: the profiles target 8-12 GB phones.
    if (tuning.ram_tier == RamTier::Gb16Plus) {
        std::erase_if(plan.overrides,
                      [](const auto& entry) { return entry.first == "resolution_setup"; });
    }
    plan.memory_fit = FitOf(plan.profile->ram_need_gib, tuning.usable_ram_bytes);
    plan.storage_backed_memory = plan.memory_fit != MemoryFit::Fits;
    ApplyUpscaler(tuning, plan);
    return plan;
}

std::string_view ToString(MemoryFit fit) {
    switch (fit) {
    case MemoryFit::Fits:
        return "fits";
    case MemoryFit::Tight:
        return "tight";
    case MemoryFit::Unlikely:
        return "unlikely";
    }
    return "unknown";
}

std::optional<std::uint64_t> ParseProgramId(std::string_view hex) {
    if (hex.starts_with("0x") || hex.starts_with("0X")) {
        hex.remove_prefix(2);
    }
    if (hex.empty() || hex.size() > 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    if (error != std::errc{} || end != hex.data() + hex.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace LoopNative
