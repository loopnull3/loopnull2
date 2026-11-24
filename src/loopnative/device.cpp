// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cctype>

#include "loopnative/device.h"

namespace LoopNative {

namespace {

constexpr std::uint64_t GiB = 1ULL << 30;

std::string Lower(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool Contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

// Carrier models that don't follow the SM-F731 pattern (au, docomo).
constexpr std::array FLIP5_CARRIER_MODELS{std::string_view{"scg23"}, std::string_view{"sc-54d"}};
constexpr std::array FOLD5_CARRIER_MODELS{std::string_view{"scg22"}, std::string_view{"sc-55d"}};

bool IsGalaxyZFold5(const DeviceInfo& info) {
    const auto model = Lower(info.model);
    const auto device = Lower(info.device);
    return StartsWith(model, "sm-f946") || device == "q5q" ||
           std::ranges::find(FOLD5_CARRIER_MODELS, model) != FOLD5_CARRIER_MODELS.end();
}

bool IsSnapdragon8Gen2(const DeviceInfo& info) {
    const auto soc = Lower(info.soc_model);
    return soc == "sm8550" || Contains(soc, "kalama");
}

} // namespace

bool IsGalaxyZFlip5(const DeviceInfo& info) {
    const auto model = Lower(info.model);
    const auto device = Lower(info.device);
    return StartsWith(model, "sm-f731") || device == "b5q" ||
           std::ranges::find(FLIP5_CARRIER_MODELS, model) != FLIP5_CARRIER_MODELS.end();
}

GpuVendor GpuVendorOf(std::string_view renderer) {
    const auto name = Lower(renderer);
    if (Contains(name, "adreno") || Contains(name, "turnip")) {
        return GpuVendor::Adreno;
    }
    if (Contains(name, "xclipse") || Contains(name, "radv")) {
        return GpuVendor::Xclipse;
    }
    if (Contains(name, "mali") || Contains(name, "immortalis") || Contains(name, "panvk")) {
        return GpuVendor::Mali;
    }
    return GpuVendor::Other;
}

RamTier RamTierOf(std::uint64_t total_ram_bytes) {
    // Android reports less than the marketed size (the kernel and carve-outs keep some): an 8 GB
    // phone shows about 7.3-7.6 GiB.
    if (total_ram_bytes == 0) {
        return RamTier::Gb8;
    }
    if (total_ram_bytes <= GiB * 13 / 2) {
        return RamTier::Gb6OrLess;
    }
    if (total_ram_bytes <= GiB * 21 / 2) {
        return RamTier::Gb8;
    }
    if (total_ram_bytes <= GiB * 29 / 2) {
        return RamTier::Gb12;
    }
    return RamTier::Gb16Plus;
}

DeviceTuning ClassifyDevice(const DeviceInfo& info) {
    DeviceTuning tuning;
    tuning.ram_tier = RamTierOf(info.total_ram_bytes);
    tuning.gpu_vendor = GpuVendorOf(info.gpu_renderer);
    tuning.turnip_driver = Contains(Lower(info.gpu_renderer), "turnip");
    tuning.display_name = info.model.empty() ? std::string{"Unknown phone"} : info.model;

    if (IsGalaxyZFlip5(info)) {
        tuning.device_class = DeviceClass::GalaxyZFlip5;
        tuning.display_name = "Galaxy Z Flip5";
    } else if (IsGalaxyZFold5(info)) {
        tuning.device_class = DeviceClass::GalaxyZFold5;
        tuning.display_name = "Galaxy Z Fold5";
    } else if (IsSnapdragon8Gen2(info)) {
        tuning.device_class = DeviceClass::Snapdragon8Gen2;
    } else {
        switch (tuning.gpu_vendor) {
        case GpuVendor::Adreno:
            tuning.device_class = DeviceClass::Adreno;
            break;
        case GpuVendor::Xclipse:
            tuning.device_class = DeviceClass::Xclipse;
            break;
        case GpuVendor::Mali:
            tuning.device_class = DeviceClass::Mali;
            break;
        case GpuVendor::Other:
            tuning.device_class = DeviceClass::Unknown;
            break;
        }
    }

    // What Android leaves a foreground game before lmkd starts closing it, by tier.
    std::uint64_t reserve = GiB * 3;
    switch (tuning.ram_tier) {
    case RamTier::Gb6OrLess:
        reserve = GiB * 5 / 2;
        tuning.resolution_setup = 0;
        break;
    case RamTier::Gb8:
        reserve = GiB * 11 / 4;
        tuning.resolution_setup = 1;
        break;
    case RamTier::Gb12:
        reserve = GiB * 13 / 4;
        tuning.resolution_setup = 1;
        break;
    case RamTier::Gb16Plus:
        reserve = GiB * 4;
        tuning.resolution_setup = 3;
        break;
    }
    tuning.usable_ram_bytes =
        info.total_ram_bytes > reserve ? info.total_ram_bytes - reserve : info.total_ram_bytes / 2;

    tuning.recommend_turnip = tuning.gpu_vendor == GpuVendor::Adreno && !tuning.turnip_driver;

    switch (tuning.device_class) {
    case DeviceClass::GalaxyZFlip5:
        // The Flip5's hinge leaves little room to spread heat: it reaches its skin limit in a
        // few minutes of full GPU load. Step down early, wait longer before stepping back up.
        tuning.headroom_step_down = 0.80f;
        tuning.headroom_step_up = 0.60f;
        tuning.cool_seconds_before_step_up = 60;
        tuning.resolution_setup = 1;
        break;
    case DeviceClass::GalaxyZFold5:
        tuning.headroom_step_down = 0.85f;
        tuning.headroom_step_up = 0.65f;
        tuning.cool_seconds_before_step_up = 45;
        break;
    default:
        break;
    }
    return tuning;
}

std::string_view ToString(DeviceClass device_class) {
    switch (device_class) {
    case DeviceClass::GalaxyZFlip5:
        return "galaxy_z_flip5";
    case DeviceClass::GalaxyZFold5:
        return "galaxy_z_fold5";
    case DeviceClass::Snapdragon8Gen2:
        return "snapdragon_8_gen_2";
    case DeviceClass::Adreno:
        return "adreno";
    case DeviceClass::Xclipse:
        return "xclipse";
    case DeviceClass::Mali:
        return "mali";
    case DeviceClass::Unknown:
        break;
    }
    return "unknown";
}

std::string_view ToString(RamTier tier) {
    switch (tier) {
    case RamTier::Gb6OrLess:
        return "6gb_or_less";
    case RamTier::Gb8:
        return "8gb";
    case RamTier::Gb12:
        return "12gb";
    case RamTier::Gb16Plus:
        return "16gb_plus";
    }
    return "unknown";
}

} // namespace LoopNative
