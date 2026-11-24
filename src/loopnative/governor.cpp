// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <string>

#include "loopnative/governor.h"

namespace LoopNative {

namespace {

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

template <typename Callback>
void ForEachLine(std::string_view text, Callback&& callback) {
    while (!text.empty()) {
        const auto end = text.find('\n');
        callback(text.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
}

/// "MemTotal:       7654321 kB" -> bytes
std::optional<std::uint64_t> MemInfoValue(std::string_view rest) {
    rest = Trim(rest);
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(rest.data(), rest.data() + rest.size(), value);
    if (error != std::errc{}) {
        return std::nullopt;
    }
    const auto unit = Trim(rest.substr(static_cast<std::size_t>(end - rest.data())));
    if (unit == "kB") {
        value *= 1024;
    }
    return value;
}

/// "some avg10=1.23 avg60=0.50 avg300=0.10 total=12345" -> avg10
std::optional<float> PsiAvg10(std::string_view line) {
    const auto pos = line.find("avg10=");
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string number(line.substr(pos + 6, line.find(' ', pos) - (pos + 6)));
    char* end = nullptr;
    const float value = std::strtof(number.c_str(), &end);
    if (end == number.c_str()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::optional<MemInfo> ParseMemInfo(std::string_view text) {
    MemInfo info;
    bool total = false;
    bool available = false;
    ForEachLine(text, [&](std::string_view line) {
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return;
        }
        const auto key = line.substr(0, colon);
        const auto value = MemInfoValue(line.substr(colon + 1));
        if (!value) {
            return;
        }
        if (key == "MemTotal") {
            info.total = *value;
            total = true;
        } else if (key == "MemAvailable") {
            info.available = *value;
            available = true;
        } else if (key == "SwapTotal") {
            info.swap_total = *value;
        } else if (key == "SwapFree") {
            info.swap_free = *value;
        }
    });
    if (!total || !available) {
        return std::nullopt;
    }
    return info;
}

std::optional<MemoryPsi> ParseMemoryPsi(std::string_view text) {
    MemoryPsi psi;
    bool some = false;
    ForEachLine(text, [&](std::string_view line) {
        line = Trim(line);
        if (line.starts_with("some ")) {
            if (const auto value = PsiAvg10(line)) {
                psi.some_avg10 = *value;
                some = true;
            }
        } else if (line.starts_with("full ")) {
            if (const auto value = PsiAvg10(line)) {
                psi.full_avg10 = *value;
            }
        }
    });
    if (!some) {
        return std::nullopt;
    }
    return psi;
}

MemoryPressure MemoryGovernor::Classify(const MemorySample& sample) {
    const auto& info = sample.info;
    const double available_share =
        info.total == 0 ? 1.0 : static_cast<double>(info.available) / static_cast<double>(info.total);
    const double swap_free_share =
        info.swap_total == 0 ? 1.0
                             : static_cast<double>(info.swap_free) / static_cast<double>(info.swap_total);
    constexpr std::uint64_t MiB = 1024 * 1024;

    const bool critical = sample.android_low_memory || available_share < 0.05 ||
                          (info.total != 0 && info.available < 300 * MiB) ||
                          (sample.psi && sample.psi->full_avg10 >= 10.0f);
    if (critical) {
        return MemoryPressure::Critical;
    }
    const bool elevated = available_share < 0.12 || swap_free_share < 0.15 ||
                          (sample.psi && sample.psi->some_avg10 >= 20.0f);
    return elevated ? MemoryPressure::Elevated : MemoryPressure::Normal;
}

MemoryPressure MemoryGovernor::Update(const MemorySample& sample) {
    const auto now = Classify(sample);
    if (now >= level) {
        level = now;
        calm_samples = 0;
        return level;
    }
    if (++calm_samples >= calm_samples_to_fall) {
        level = static_cast<MemoryPressure>(static_cast<int>(level) - 1);
        calm_samples = 0;
    }
    return level;
}

ThermalGovernor::ThermalGovernor(const DeviceTuning& tuning, std::uint32_t base_fps_limit_)
    : step_down{tuning.headroom_step_down}, step_up{tuning.headroom_step_up},
      cool_seconds{static_cast<double>(tuning.cool_seconds_before_step_up)},
      base_fps_limit{base_fps_limit_} {}

ThermalDecision ThermalGovernor::DecisionFor(ThermalLevel at, bool changed) const {
    ThermalDecision decision;
    decision.level = at;
    decision.changed = changed;
    switch (at) {
    case ThermalLevel::Full:
        decision.fps_limit = base_fps_limit;
        break;
    case ThermalLevel::Capped30:
        decision.fps_limit = base_fps_limit != 0 ? std::min<std::uint32_t>(base_fps_limit, 30) : 30;
        break;
    case ThermalLevel::Skipping:
        decision.fps_limit = base_fps_limit != 0 ? std::min<std::uint32_t>(base_fps_limit, 30) : 30;
        decision.frame_skip = 1;
        break;
    }
    return decision;
}

ThermalDecision ThermalGovernor::Current() const {
    return DecisionFor(level, false);
}

ThermalDecision ThermalGovernor::Update(const ThermalSample& sample) {
    const bool known = sample.headroom >= 0.0f;
    const bool very_hot = (known && sample.headroom >= 1.0f) || sample.status >= ThermalSevere;
    const bool hot = very_hot || (known && sample.headroom >= step_down) ||
                     sample.status >= ThermalModerate;
    const bool cool = (!known || sample.headroom < step_up) && sample.status <= ThermalLight;

    const auto previous = level;
    if (very_hot) {
        level = ThermalLevel::Skipping;
    } else if (hot && level == ThermalLevel::Full) {
        level = ThermalLevel::Capped30;
    }

    if (hot) {
        cool_since.reset();
    } else if (cool) {
        if (!cool_since) {
            cool_since = sample.seconds;
        }
        const bool cooled = sample.seconds - *cool_since >= cool_seconds;
        const bool settled = sample.seconds - last_change >= cool_seconds;
        if (level != ThermalLevel::Full && cooled && settled) {
            level = static_cast<ThermalLevel>(static_cast<int>(level) - 1);
            cool_since = sample.seconds;
        }
    } else {
        cool_since.reset();
    }

    const bool changed = level != previous;
    if (changed) {
        last_change = sample.seconds;
    }
    return DecisionFor(level, changed);
}

std::string_view ToString(MemoryPressure pressure) {
    switch (pressure) {
    case MemoryPressure::Normal:
        return "normal";
    case MemoryPressure::Elevated:
        return "elevated";
    case MemoryPressure::Critical:
        return "critical";
    }
    return "unknown";
}

std::string_view ToString(ThermalLevel at) {
    switch (at) {
    case ThermalLevel::Full:
        return "full";
    case ThermalLevel::Capped30:
        return "capped_30";
    case ThermalLevel::Skipping:
        return "skipping";
    }
    return "unknown";
}

} // namespace LoopNative
