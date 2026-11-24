// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "loopnative/device.h"

namespace LoopNative {

/// /proc/meminfo, the fields LoopNative reads (bytes).
struct MemInfo {
    std::uint64_t total = 0;
    std::uint64_t available = 0;
    std::uint64_t swap_total = 0;
    std::uint64_t swap_free = 0;
};

/// /proc/pressure/memory: share of the last 10 s some/all tasks stalled on memory (percent).
struct MemoryPsi {
    float some_avg10 = 0.0f;
    float full_avg10 = 0.0f;
};

[[nodiscard]] std::optional<MemInfo> ParseMemInfo(std::string_view text);
[[nodiscard]] std::optional<MemoryPsi> ParseMemoryPsi(std::string_view text);

enum class MemoryPressure {
    Normal,
    Elevated, ///< reclaim is working hard: zram filling, tasks stalling now and then
    Critical, ///< lmkd is about to close something, possibly the game
};

struct MemorySample {
    MemInfo info;
    std::optional<MemoryPsi> psi;
    bool android_low_memory = false; ///< ActivityManager.MemoryInfo.lowMemory
};

/// Classifies memory pressure, with hysteresis: it rises at once and falls only after
/// `calm_samples_to_fall` calm samples in a row.
class MemoryGovernor {
public:
    explicit MemoryGovernor(int calm_samples_to_fall_ = 3) : calm_samples_to_fall{calm_samples_to_fall_} {}

    MemoryPressure Update(const MemorySample& sample);
    [[nodiscard]] MemoryPressure Level() const {
        return level;
    }

    [[nodiscard]] static MemoryPressure Classify(const MemorySample& sample);

private:
    MemoryPressure level = MemoryPressure::Normal;
    int calm_samples = 0;
    int calm_samples_to_fall;
};

/// android.os.PowerManager THERMAL_STATUS_* values.
enum ThermalStatus : int {
    ThermalNone = 0,
    ThermalLight = 1,
    ThermalModerate = 2,
    ThermalSevere = 3,
    ThermalCritical = 4,
    ThermalEmergency = 5,
    ThermalShutdown = 6,
};

struct ThermalSample {
    double seconds = 0.0;   ///< a monotonic clock
    float headroom = -1.0f; ///< PowerManager.getThermalHeadroom forecast; negative: unknown
    int status = ThermalNone;
};

enum class ThermalLevel {
    Full,     ///< the session's own frame cap
    Capped30, ///< 30 fps cap
    Skipping, ///< 30 fps cap and every other frame left undrawn (game speed kept)
};

struct ThermalDecision {
    ThermalLevel level = ThermalLevel::Full;
    std::uint32_t fps_limit = 0; ///< Settings fps_limit (0: the game's own rate)
    int frame_skip = 0;          ///< Settings frame_skip
    bool changed = false;
};

/// Trades frame rate for temperature before Android's own throttling halves the clocks: the
/// Flip5 drops to a fraction of its GPU clock once its skin limit is reached, which costs far
/// more than a steady 30 fps.
class ThermalGovernor {
public:
    ThermalGovernor(const DeviceTuning& tuning, std::uint32_t base_fps_limit);

    ThermalDecision Update(const ThermalSample& sample);
    [[nodiscard]] ThermalDecision Current() const;

private:
    [[nodiscard]] ThermalDecision DecisionFor(ThermalLevel level, bool changed) const;

    float step_down;
    float step_up;
    double cool_seconds;
    std::uint32_t base_fps_limit;
    ThermalLevel level = ThermalLevel::Full;
    double last_change = -1e18;
    std::optional<double> cool_since;
};

[[nodiscard]] std::string_view ToString(MemoryPressure pressure);
[[nodiscard]] std::string_view ToString(ThermalLevel level);

} // namespace LoopNative
