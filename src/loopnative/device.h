// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace LoopNative {

/// What the frontend knows about the phone (android.os.Build, ActivityManager, the Vulkan driver).
struct DeviceInfo {
    std::string manufacturer; ///< Build.MANUFACTURER, e.g. "samsung"
    std::string model;        ///< Build.MODEL, e.g. "SM-F731N"
    std::string device;       ///< Build.DEVICE (codename), e.g. "b5q"
    std::string soc_model;    ///< Build.SOC_MODEL (API 31+), e.g. "SM8550"
    std::string gpu_renderer; ///< Vulkan deviceName, e.g. "Adreno (TM) 740" or "Turnip Adreno (TM) 740"
    std::uint64_t total_ram_bytes = 0; ///< ActivityManager.MemoryInfo.totalMem
    int sdk_int = 0;                   ///< Build.VERSION.SDK_INT
};

enum class DeviceClass {
    GalaxyZFlip5,   ///< SM-F731*, b5q: Snapdragon 8 Gen 2 for Galaxy, Adreno 740, 8 GB
    GalaxyZFold5,   ///< SM-F946*, q5q: same SoC, 12 GB
    Snapdragon8Gen2, ///< any other SM8550 phone
    Adreno,         ///< other Adreno phones
    Xclipse,        ///< Samsung Exynos with an AMD RDNA GPU
    Mali,
    Unknown,
};

enum class RamTier {
    Gb6OrLess,
    Gb8,
    Gb12,
    Gb16Plus,
};

enum class GpuVendor {
    Adreno,
    Xclipse,
    Mali,
    Other,
};

/// Defaults LoopNative starts a game with on this phone, before a title profile refines them.
struct DeviceTuning {
    DeviceClass device_class = DeviceClass::Unknown;
    RamTier ram_tier = RamTier::Gb8;
    GpuVendor gpu_vendor = GpuVendor::Other;
    bool turnip_driver = false;  ///< the renderer string names Mesa's Turnip
    std::string display_name;    ///< "Galaxy Z Flip5", or the model

    /// ResolutionSetup index (0 = 1/4x, 1 = 1/2x, 2 = 3/4x, 3 = 1x).
    int resolution_setup = 1;
    /// Frame cap the thermal governor starts from (0: the game's own rate).
    std::uint32_t start_fps_limit = 0;
    /// Recommend Mesa Turnip over the stock Qualcomm driver (BC textures, fewer driver bugs).
    bool recommend_turnip = false;
    /// RAM the app can count on for a game, leaving Android, the launcher and the GPU driver room.
    std::uint64_t usable_ram_bytes = 0;

    /// Thermal governor thresholds (PowerManager.getThermalHeadroom forecast, 1.0 = severe).
    float headroom_step_down = 0.85f;
    float headroom_step_up = 0.65f;
    /// Seconds the phone has to stay cool before the governor lifts a frame cap again.
    int cool_seconds_before_step_up = 45;
};

[[nodiscard]] bool IsGalaxyZFlip5(const DeviceInfo& info);
[[nodiscard]] GpuVendor GpuVendorOf(std::string_view renderer);
[[nodiscard]] RamTier RamTierOf(std::uint64_t total_ram_bytes);
[[nodiscard]] DeviceTuning ClassifyDevice(const DeviceInfo& info);
[[nodiscard]] std::string_view ToString(DeviceClass device_class);
[[nodiscard]] std::string_view ToString(RamTier tier);

} // namespace LoopNative
