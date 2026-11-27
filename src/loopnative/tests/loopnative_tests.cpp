// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Host tests for LoopNative's portable core. No test framework: each CHECK prints its failure and
// the process exits non-zero when any failed.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "loopnative/device.h"
#include "loopnative/governor.h"
#include "loopnative/nso_analyzer.h"
#include "loopnative/report.h"
#include "loopnative/title_profiles.h"

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                          \
    do {                                                                                          \
        ++checks;                                                                                 \
        if (!(condition)) {                                                                       \
            ++failures;                                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                                         \
    } while (0)

constexpr std::uint64_t GiB = 1ULL << 30;

using namespace LoopNative;

DeviceInfo Flip5() {
    DeviceInfo info;
    info.manufacturer = "samsung";
    info.model = "SM-F731N";
    info.device = "b5q";
    info.soc_model = "SM8550";
    info.gpu_renderer = "Adreno (TM) 740";
    info.total_ram_bytes = GiB * 15 / 2; // what an 8 GB phone reports
    info.sdk_int = 35;
    return info;
}

void TestDevice() {
    CHECK(IsGalaxyZFlip5(Flip5()));
    DeviceInfo carrier;
    carrier.model = "SCG23";
    CHECK(IsGalaxyZFlip5(carrier));
    DeviceInfo by_codename;
    by_codename.model = "unknown";
    by_codename.device = "B5Q";
    CHECK(IsGalaxyZFlip5(by_codename));
    DeviceInfo s24;
    s24.model = "SM-S921B";
    CHECK(!IsGalaxyZFlip5(s24));

    CHECK(RamTierOf(GiB * 15 / 2) == RamTier::Gb8);
    CHECK(RamTierOf(GiB * 11) == RamTier::Gb12);
    CHECK(RamTierOf(GiB * 15) == RamTier::Gb16Plus);
    CHECK(RamTierOf(GiB * 6) == RamTier::Gb6OrLess);

    CHECK(GpuVendorOf("Adreno (TM) 740") == GpuVendor::Adreno);
    CHECK(GpuVendorOf("Turnip Adreno (TM) 740") == GpuVendor::Adreno);
    CHECK(GpuVendorOf("Samsung Xclipse 940") == GpuVendor::Xclipse);
    CHECK(GpuVendorOf("Mali-G715") == GpuVendor::Mali);

    const auto tuning = ClassifyDevice(Flip5());
    CHECK(tuning.device_class == DeviceClass::GalaxyZFlip5);
    CHECK(tuning.display_name == "Galaxy Z Flip5");
    CHECK(tuning.ram_tier == RamTier::Gb8);
    CHECK(tuning.resolution_setup == 1);
    CHECK(tuning.recommend_turnip);
    CHECK(!tuning.turnip_driver);
    CHECK(tuning.usable_ram_bytes == GiB * 15 / 2 - GiB * 11 / 4);
    CHECK(tuning.headroom_step_down < 0.85f);

    auto turnip = Flip5();
    turnip.gpu_renderer = "Turnip Adreno (TM) 740";
    const auto with_turnip = ClassifyDevice(turnip);
    CHECK(with_turnip.turnip_driver);
    CHECK(!with_turnip.recommend_turnip);

    DeviceInfo fold;
    fold.model = "SM-F946B";
    fold.soc_model = "SM8550";
    fold.total_ram_bytes = GiB * 11;
    CHECK(ClassifyDevice(fold).device_class == DeviceClass::GalaxyZFold5);

    DeviceInfo other_8g2;
    other_8g2.model = "SM-S911B";
    other_8g2.soc_model = "SM8550";
    other_8g2.total_ram_bytes = GiB * 15 / 2;
    CHECK(ClassifyDevice(other_8g2).device_class == DeviceClass::Snapdragon8Gen2);
}

bool HasOverride(const SessionPlan& plan, std::string_view key, std::string_view value) {
    for (const auto& [k, v] : plan.overrides) {
        if (k == key) {
            return v == value;
        }
    }
    return false;
}

bool HasKey(const SessionPlan& plan, std::string_view key) {
    for (const auto& entry : plan.overrides) {
        if (entry.first == key) {
            return true;
        }
    }
    return false;
}

void TestProfiles() {
    const auto tuning = ClassifyDevice(Flip5());

    const auto ocarina = PlanSession(tuning, 0x0400486024DEC000ULL);
    CHECK(ocarina.profile != nullptr);
    CHECK(ocarina.memory_fit == MemoryFit::Unlikely);
    CHECK(ocarina.storage_backed_memory);
    CHECK(HasOverride(ocarina, "scene_memory_cap", "2"));
    CHECK(HasOverride(ocarina, "resolution_setup", "1"));

    const auto kart = PlanSession(tuning, 0x0400C3F00006E000ULL);
    CHECK(kart.profile != nullptr);
    CHECK(HasOverride(kart, "mkw_30fps", "true"));
    CHECK(HasOverride(kart, "use_vsync", "3"));
    CHECK(kart.memory_fit == MemoryFit::Unlikely);

    const auto pokopia = PlanSession(tuning, 0x0400A3A01EE4C000ULL);
    CHECK(pokopia.memory_fit == MemoryFit::Fits);
    CHECK(!pokopia.storage_backed_memory);

    const auto unknown = PlanSession(tuning, 0x0400000000010000ULL);
    CHECK(unknown.profile == nullptr);
    CHECK(HasOverride(unknown, "resolution_setup", "1"));
    CHECK(!unknown.storage_backed_memory);

    // A Switch game (Minecraft) keeps the user's settings.
    const auto switch_game = PlanSession(tuning, 0x0100D71004694000ULL);
    CHECK(switch_game.profile == nullptr);
    CHECK(switch_game.overrides.empty());
    CHECK(!switch_game.storage_backed_memory);

    // No key appears twice: the title's value replaces the device default.
    for (std::size_t i = 0; i < kart.overrides.size(); ++i) {
        for (std::size_t j = i + 1; j < kart.overrides.size(); ++j) {
            CHECK(kart.overrides[i].first != kart.overrides[j].first);
        }
    }

    DeviceInfo big;
    big.model = "SM-S948B";
    big.gpu_renderer = "Adreno (TM) 830";
    big.total_ram_bytes = GiB * 15;
    const auto big_ocarina = PlanSession(ClassifyDevice(big), 0x0400486024DEC000ULL);
    CHECK(big_ocarina.memory_fit == MemoryFit::Fits);
    CHECK(!HasKey(big_ocarina, "resolution_setup"));

    CHECK(FitOf(4.0f, GiB * 5) == MemoryFit::Fits);
    CHECK(FitOf(7.0f, GiB * 5) == MemoryFit::Tight);
    CHECK(FitOf(9.0f, GiB * 5) == MemoryFit::Unlikely);

    CHECK(ParseProgramId("0400486024DEC000") == 0x0400486024DEC000ULL);
    CHECK(ParseProgramId("0x0400c3f00006e000") == 0x0400C3F00006E000ULL);
    CHECK(!ParseProgramId("xyz"));
    CHECK(!ParseProgramId(""));

    const auto json = ToJson(tuning, ocarina);
    CHECK(json.find("\"memory_fit\":\"unlikely\"") != std::string::npos);
    CHECK(json.find("\"device_class\":\"galaxy_z_flip5\"") != std::string::npos);
    CHECK(json.find("[\"scene_memory_cap\",\"2\"]") != std::string::npos);
}

DeviceInfo S26Exynos() {
    DeviceInfo info;
    info.manufacturer = "samsung";
    info.model = "SM-S942N";
    info.device = "s26";
    info.soc_model = "s5e9965";
    info.gpu_renderer = "Samsung Xclipse 960";
    info.total_ram_bytes = GiB * 45 / 4; // what a 12 GB phone reports
    info.sdk_int = 37;
    return info;
}

void TestS26() {
    const auto tuning = ClassifyDevice(S26Exynos());
    CHECK(tuning.device_class == DeviceClass::GalaxyS26Exynos);
    CHECK(tuning.display_name == "Galaxy S26 (Exynos 2600)");
    CHECK(tuning.ram_tier == RamTier::Gb12);
    CHECK(tuning.gpu_vendor == GpuVendor::Xclipse);
    CHECK(tuning.recommend_radv);
    CHECK(!tuning.recommend_turnip);
    CHECK(tuning.upscale_filter == 6);
    CHECK(tuning.headroom_step_down > 0.85f);
    CHECK(tuning.cool_seconds_before_step_up == 30);

    auto plus = S26Exynos();
    plus.model = "SM-S947B";
    CHECK(ClassifyDevice(plus).display_name == "Galaxy S26+ (Exynos 2600)");

    // Before Vulkan names the GPU, the SoC alone makes it the S26 Exynos.
    auto unnamed = S26Exynos();
    unnamed.gpu_renderer.clear();
    const auto early = ClassifyDevice(unnamed);
    CHECK(early.device_class == DeviceClass::GalaxyS26Exynos);
    CHECK(early.upscale_filter == 6);
    CHECK(early.recommend_radv);

    // A custom Mesa RADV build is what upstream recommends there.
    auto radv = S26Exynos();
    radv.gpu_renderer = "AMD RADV JUNO RADV-Xclipse-27.0.zip";
    const auto with_radv = ClassifyDevice(radv);
    CHECK(with_radv.radv_driver);
    CHECK(!with_radv.recommend_radv);
    CHECK(with_radv.gpu_vendor == GpuVendor::Xclipse);

    // The Snapdragon S26 of other regions is not this class.
    auto snapdragon = S26Exynos();
    snapdragon.model = "SM-S942U";
    snapdragon.soc_model = "SM8850";
    snapdragon.gpu_renderer = "Adreno (TM) 840";
    const auto us = ClassifyDevice(snapdragon);
    CHECK(us.device_class == DeviceClass::Adreno);
    CHECK(us.upscale_filter == 13);
    CHECK(us.recommend_turnip);

    // Ocarina of Time: upstream's 0.5x, FSR to the screen, guest memory on storage (it can run
    // out of memory on 12 GB phones).
    const auto ocarina = PlanSession(tuning, 0x0400486024DEC000ULL);
    CHECK(HasOverride(ocarina, "resolution_setup", "1"));
    CHECK(HasOverride(ocarina, "scaling_filter", "6"));
    CHECK(ocarina.memory_fit == MemoryFit::Tight);
    CHECK(ocarina.storage_backed_memory);

    // Mario Kart World: 0.75x on 12 GB; FSR replaces the preset's ScaleForce; still 30 fps mode.
    const auto kart = PlanSession(tuning, 0x0400C3F00006E000ULL);
    CHECK(HasOverride(kart, "resolution_setup", "2"));
    CHECK(HasOverride(kart, "scaling_filter", "6"));
    CHECK(HasOverride(kart, "mkw_30fps", "true"));
    CHECK(kart.memory_fit == MemoryFit::Tight);

    const auto pokopia = PlanSession(tuning, 0x0400A3A01EE4C000ULL);
    CHECK(HasOverride(pokopia, "resolution_setup", "2"));
    CHECK(pokopia.memory_fit == MemoryFit::Fits);
    CHECK(!pokopia.storage_backed_memory);

    const auto emblem = PlanSession(tuning, 0x04007EF01F8AE000ULL);
    CHECK(HasOverride(emblem, "resolution_setup", "1"));
    CHECK(emblem.memory_fit == MemoryFit::Fits);

    const auto unknown = PlanSession(tuning, 0x0400000000010000ULL);
    CHECK(HasOverride(unknown, "resolution_setup", "1"));
    CHECK(HasOverride(unknown, "scaling_filter", "6"));

    const auto switch_game = PlanSession(tuning, 0x0100D71004694000ULL);
    CHECK(switch_game.overrides.empty());

    const auto json = ToJson(tuning, kart);
    CHECK(json.find("\"device_class\":\"galaxy_s26_exynos\"") != std::string::npos);
    CHECK(json.find("\"recommend_radv\":true") != std::string::npos);
    CHECK(json.find("\"upscale_filter\":6") != std::string::npos);

    // The S26 caps later and lifts sooner than the Flip5.
    ThermalGovernor governor(tuning, 0);
    CHECK(governor.Update({0.0, 0.85f, ThermalNone}).level == ThermalLevel::Full);
    CHECK(governor.Update({2.0, 0.91f, ThermalNone}).level == ThermalLevel::Capped30);
    CHECK(governor.Update({10.0, 0.5f, ThermalNone}).level == ThermalLevel::Capped30);
    CHECK(governor.Update({41.0, 0.5f, ThermalNone}).level == ThermalLevel::Full);
}

void TestProcParsing() {
    const auto info = ParseMemInfo("MemTotal:        7864320 kB\n"
                                   "MemFree:          102400 kB\n"
                                   "MemAvailable:    1048576 kB\n"
                                   "SwapTotal:       4194304 kB\n"
                                   "SwapFree:        2097152 kB\n");
    CHECK(info.has_value());
    CHECK(info->total == 7864320ULL * 1024);
    CHECK(info->available == 1048576ULL * 1024);
    CHECK(info->swap_total == 4194304ULL * 1024);
    CHECK(info->swap_free == 2097152ULL * 1024);
    CHECK(!ParseMemInfo("garbage"));

    const auto psi = ParseMemoryPsi("some avg10=12.50 avg60=3.00 avg300=1.00 total=123\n"
                                    "full avg10=4.25 avg60=1.00 avg300=0.10 total=45\n");
    CHECK(psi.has_value());
    CHECK(std::fabs(psi->some_avg10 - 12.5f) < 1e-4f);
    CHECK(std::fabs(psi->full_avg10 - 4.25f) < 1e-4f);
    CHECK(!ParseMemoryPsi(""));
}

void TestMemoryGovernor() {
    MemorySample calm;
    calm.info = {GiB * 8, GiB * 3, GiB * 4, GiB * 4};
    MemorySample tight = calm;
    tight.info.available = GiB * 8 / 10; // 10%
    MemorySample critical = calm;
    critical.psi = MemoryPsi{40.0f, 15.0f};

    CHECK(MemoryGovernor::Classify(calm) == MemoryPressure::Normal);
    CHECK(MemoryGovernor::Classify(tight) == MemoryPressure::Elevated);
    CHECK(MemoryGovernor::Classify(critical) == MemoryPressure::Critical);
    MemorySample low_flag = calm;
    low_flag.android_low_memory = true;
    CHECK(MemoryGovernor::Classify(low_flag) == MemoryPressure::Critical);

    MemoryGovernor governor(3);
    CHECK(governor.Update(critical) == MemoryPressure::Critical);
    CHECK(governor.Update(calm) == MemoryPressure::Critical);
    CHECK(governor.Update(calm) == MemoryPressure::Critical);
    CHECK(governor.Update(calm) == MemoryPressure::Elevated);
    CHECK(governor.Update(tight) == MemoryPressure::Elevated);
    CHECK(governor.Update(calm) == MemoryPressure::Elevated);
    CHECK(governor.Update(calm) == MemoryPressure::Elevated);
    CHECK(governor.Update(calm) == MemoryPressure::Normal);
}

void TestThermalGovernor() {
    const auto tuning = ClassifyDevice(Flip5()); // steps down at 0.80, up below 0.60 after 60 s
    ThermalGovernor governor(tuning, 0);
    CHECK(governor.Current().level == ThermalLevel::Full);
    CHECK(governor.Current().fps_limit == 0);

    auto decision = governor.Update({0.0, 0.5f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Full);
    CHECK(!decision.changed);

    decision = governor.Update({2.0, 0.82f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Capped30);
    CHECK(decision.changed);
    CHECK(decision.fps_limit == 30);
    CHECK(decision.frame_skip == 0);

    decision = governor.Update({4.0, 0.9f, ThermalSevere});
    CHECK(decision.level == ThermalLevel::Skipping);
    CHECK(decision.frame_skip == 1);

    // Between the thresholds: hold.
    decision = governor.Update({10.0, 0.7f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Skipping);

    // Cool from 20 s: one level up after 60 s, the next after 60 s more.
    decision = governor.Update({20.0, 0.4f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Skipping);
    decision = governor.Update({70.0, 0.4f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Skipping);
    decision = governor.Update({81.0, 0.4f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Capped30);
    CHECK(decision.changed);
    decision = governor.Update({120.0, 0.4f, ThermalLight});
    CHECK(decision.level == ThermalLevel::Capped30);
    decision = governor.Update({142.0, 0.4f, ThermalNone});
    CHECK(decision.level == ThermalLevel::Full);
    CHECK(decision.fps_limit == 0);

    // Status alone (no headroom forecast on this phone).
    ThermalGovernor status_only(tuning, 60);
    decision = status_only.Update({0.0, -1.0f, ThermalModerate});
    CHECK(decision.level == ThermalLevel::Capped30);
    CHECK(decision.fps_limit == 30);

    // A session already capped at 20 stays at 20.
    ThermalGovernor low_cap(tuning, 20);
    decision = low_cap.Update({0.0, 0.95f, ThermalNone});
    CHECK(decision.fps_limit == 20);

    const auto json = ToJson(decision, MemoryPressure::Elevated);
    CHECK(json.find("\"memory_pressure\":\"elevated\"") != std::string::npos);
}

// --- NSO ---

/// A greedy LZ4 block encoder, enough to give the decoder matches and overlapping copies.
std::vector<std::uint8_t> EncodeLz4(const std::vector<std::uint8_t>& in) {
    std::vector<std::uint8_t> out;
    const auto put_length = [&](std::size_t length) {
        while (length >= 255) {
            out.push_back(255);
            length -= 255;
        }
        out.push_back(static_cast<std::uint8_t>(length));
    };
    std::size_t anchor = 0;
    std::size_t pos = 0;
    // LZ4 keeps the last 5 bytes literal and starts no match in the last 12.
    while (in.size() >= 12 && pos + 12 <= in.size()) {
        std::size_t best_len = 0;
        std::size_t best_offset = 0;
        for (std::size_t from = pos > 65535 ? pos - 65535 : 0; from < pos; ++from) {
            std::size_t len = 0;
            while (pos + len + 5 < in.size() && in[from + len] == in[pos + len]) {
                ++len;
            }
            if (len > best_len) {
                best_len = len;
                best_offset = pos - from;
            }
        }
        if (best_len < 4) {
            ++pos;
            continue;
        }
        const std::size_t literals = pos - anchor;
        const std::size_t match = best_len - 4;
        out.push_back(static_cast<std::uint8_t>(((literals >= 15 ? 15 : literals) << 4) |
                                                (match >= 15 ? 15 : match)));
        if (literals >= 15) {
            put_length(literals - 15);
        }
        out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(anchor),
                   in.begin() + static_cast<std::ptrdiff_t>(pos));
        out.push_back(static_cast<std::uint8_t>(best_offset & 0xFF));
        out.push_back(static_cast<std::uint8_t>(best_offset >> 8));
        if (match >= 15) {
            put_length(match - 15);
        }
        pos += best_len;
        anchor = pos;
    }
    const std::size_t literals = in.size() - anchor;
    out.push_back(static_cast<std::uint8_t>((literals >= 15 ? 15 : literals) << 4));
    if (literals >= 15) {
        put_length(literals - 15);
    }
    out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(anchor), in.end());
    return out;
}

template <typename T>
void Put(std::vector<std::uint8_t>& bytes, std::size_t offset, T value) {
    if (bytes.size() < offset + sizeof(T)) {
        bytes.resize(offset + sizeof(T));
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void PutString(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view text) {
    if (bytes.size() < offset + text.size() + 1) {
        bytes.resize(offset + text.size() + 1);
    }
    std::memcpy(bytes.data() + offset, text.data(), text.size());
    bytes[offset + text.size()] = 0;
}

struct SyntheticNso {
    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> text;
};

/// A small module: SVCs and system register reads in .text, the build path, api_info, symbols
/// (nvnBootstrapLoader exported, two imports) in .rodata, MOD0 and a .dynamic in .data.
SyntheticNso BuildNso(bool compress, bool zbic_flag) {
    constexpr std::uint32_t TEXT = 0x000, TEXT_SIZE = 0x100;
    constexpr std::uint32_t RODATA = 0x100, RODATA_SIZE = 0x200;
    constexpr std::uint32_t DATA = 0x300, DATA_SIZE = 0x40;

    std::vector<std::uint8_t> text(TEXT_SIZE);
    for (std::size_t i = 0; i < TEXT_SIZE / 4; ++i) {
        Put<std::uint32_t>(text, i * 4, 0xD503201FU); // nop
    }
    Put<std::uint32_t>(text, 0, 0x14000002U);   // b +8
    Put<std::uint32_t>(text, 4, DATA - TEXT);   // MOD0 offset
    Put<std::uint32_t>(text, 0x10, 0xD4000021U); // svc #0x1
    Put<std::uint32_t>(text, 0x14, 0xD40000E1U); // svc #0x7
    Put<std::uint32_t>(text, 0x18, 0xD40000E1U); // svc #0x7
    Put<std::uint32_t>(text, 0x1C, 0xD53BD060U); // mrs x0, tpidrro_el0
    Put<std::uint32_t>(text, 0x20, 0xD53BE021U); // mrs x1, cntpct_el0
    Put<std::uint32_t>(text, 0x24, 0xD51BD042U); // msr tpidr_el0, x2
    Put<std::uint32_t>(text, 0x28, 0xD53BE003U); // mrs x3, cntfrq_el0
    Put<std::uint32_t>(text, 0x2C, 0xD53B0024U); // mrs x4, ctr_el0

    std::vector<std::uint8_t> rodata(RODATA_SIZE);
    const std::string_view path = "C:/build/main.nss";
    Put<std::uint32_t>(rodata, 0, 0);
    Put<std::uint32_t>(rodata, 4, static_cast<std::uint32_t>(path.size()));
    PutString(rodata, 8, path);
    const std::string api = std::string("SDK MW+Nintendo+NintendoSdk_nnSdk-20.5.4-Release") + '\0' +
                            "SDK MW+Nintendo+NintendoSdk_nvn-55.1" + '\0' + "Middleware X" + '\0';
    std::memcpy(rodata.data() + 0x40, api.data(), api.size());
    const std::string dynstr = std::string("\0nvnBootstrapLoader\0nvnQueueSubmit\0_ZN2nn4initEv\0", 49);
    std::memcpy(rodata.data() + 0xC0, dynstr.data(), dynstr.size());
    // Elf64_Sym: name, info, other, shndx, value, size
    const auto symbol = [&](std::size_t index, std::uint32_t name, std::uint8_t info,
                            std::uint16_t shndx, std::uint64_t value) {
        const std::size_t at = 0x100 + index * 24;
        Put<std::uint32_t>(rodata, at, name);
        Put<std::uint8_t>(rodata, at + 4, info);
        Put<std::uint8_t>(rodata, at + 5, 0);
        Put<std::uint16_t>(rodata, at + 6, shndx);
        Put<std::uint64_t>(rodata, at + 8, value);
        Put<std::uint64_t>(rodata, at + 16, 0);
    };
    symbol(0, 0, 0, 0, 0);
    symbol(1, 1, 0x12, 1, 0x40);  // GLOBAL FUNC, defined
    symbol(2, 20, 0x12, 0, 0);    // nvnQueueSubmit, import
    symbol(3, 35, 0x22, 0, 0);    // WEAK, import
    rodata.resize(RODATA_SIZE);

    std::vector<std::uint8_t> data(DATA_SIZE);
    Put<std::uint32_t>(data, 0, 0x30444F4DU); // MOD0
    Put<std::int32_t>(data, 4, 0x20);         // .dynamic
    Put<std::int64_t>(data, 0x20, 5);         // DT_STRTAB
    Put<std::uint64_t>(data, 0x28, RODATA + 0xC0);
    Put<std::int64_t>(data, 0x30, 0); // DT_NULL
    Put<std::uint64_t>(data, 0x38, 0);
    data.resize(DATA_SIZE);

    const auto text_stored = compress ? EncodeLz4(text) : text;
    const auto data_stored = compress ? EncodeLz4(data) : data;

    std::vector<std::uint8_t> file(0x100);
    Put<std::uint32_t>(file, 0, 0x304F534EU);
    std::uint32_t flags = compress ? 0b101 : 0;
    if (zbic_flag) {
        flags |= 1U << 7;
    }
    Put<std::uint32_t>(file, 0x0C, flags);
    std::uint32_t offset = 0x100;
    const auto segment = [&](std::size_t index, std::uint32_t location, std::uint32_t size,
                             const std::vector<std::uint8_t>& stored, std::uint32_t extra) {
        Put<std::uint32_t>(file, 0x10 + index * 0x10, offset);
        Put<std::uint32_t>(file, 0x14 + index * 0x10, location);
        Put<std::uint32_t>(file, 0x18 + index * 0x10, size);
        Put<std::uint32_t>(file, 0x1C + index * 0x10, extra);
        Put<std::uint32_t>(file, 0x60 + index * 4, static_cast<std::uint32_t>(stored.size()));
        file.insert(file.end(), stored.begin(), stored.end());
        offset += static_cast<std::uint32_t>(stored.size());
    };
    for (std::size_t i = 0; i < 0x20; ++i) {
        Put<std::uint8_t>(file, 0x40 + i, static_cast<std::uint8_t>(0xA0 + i));
    }
    segment(0, TEXT, TEXT_SIZE, text_stored, 0);
    segment(1, RODATA, RODATA_SIZE, rodata, 0);
    segment(2, DATA, DATA_SIZE, data_stored, 0x20);
    Put<std::uint32_t>(file, 0x88, 0x40);
    Put<std::uint32_t>(file, 0x8C, static_cast<std::uint32_t>(api.size()));
    Put<std::uint32_t>(file, 0x90, 0xC0);
    Put<std::uint32_t>(file, 0x94, static_cast<std::uint32_t>(dynstr.size()));
    Put<std::uint32_t>(file, 0x98, 0x100);
    Put<std::uint32_t>(file, 0x9C, 4 * 24);
    return {file, text};
}

void CheckSyntheticReport(const NsoReport& report) {
    CHECK(report.valid);
    CHECK(report.error.empty());
    CHECK(report.build_id == "A0A1A2A3A4A5A6A7A8A9AAABACADAEAFB0B1B2B3");
    CHECK(report.module_path == "C:/build/main.nss");
    CHECK(report.image_size == 0x340 + 0x20);
    CHECK(report.has_mod0);
    CHECK(report.dynamic_entries == 2);
    CHECK(report.symbols == 4);
    CHECK(report.exported == 1);
    CHECK(report.imported == 2);
    CHECK(report.nvn_imports == 1);
    CHECK(report.nvn_bootstrap_loader == 0x40u);
    CHECK(report.nvn_exports.size() == 1);
    CHECK(report.sdk_versions.size() == 2);
    CHECK(report.scan.instructions == 0x40);
    CHECK(report.scan.svc_sites == 3);
    CHECK(report.scan.svc_by_id[1] == 1);
    CHECK(report.scan.svc_by_id[7] == 2);
    CHECK(report.scan.mrs_tpidrro_el0 == 1);
    CHECK(report.scan.mrs_cntpct_el0 == 1);
    CHECK(report.scan.msr_tpidr_el0 == 1);
    CHECK(report.scan.mrs_cntfrq_el0 == 1);
    CHECK(report.scan.mrs_ctr_el0 == 1);
    CHECK(report.scan.PatchSites() == 8);
}

void TestLz4() {
    std::vector<std::uint8_t> input;
    for (int i = 0; i < 300; ++i) {
        input.push_back(static_cast<std::uint8_t>(i % 7 == 0 ? 0xAA : i % 3));
    }
    input.insert(input.end(), 400, 0x55); // a long run: overlapping copies, 255-byte length bytes
    const auto encoded = EncodeLz4(input);
    CHECK(encoded.size() < input.size());
    std::vector<std::uint8_t> output(input.size());
    CHECK(DecompressLz4Block(encoded, output));
    CHECK(output == input);

    std::vector<std::uint8_t> too_small(input.size() - 1);
    CHECK(!DecompressLz4Block(encoded, too_small));
    std::vector<std::uint8_t> too_big(input.size() + 1);
    CHECK(!DecompressLz4Block(encoded, too_big));

    auto truncated = encoded;
    truncated.resize(truncated.size() / 2);
    CHECK(!DecompressLz4Block(truncated, output));

    const std::vector<std::uint8_t> bad_offset{0x10, 0x41, 0x05, 0x00};
    std::vector<std::uint8_t> small(8);
    CHECK(!DecompressLz4Block(bad_offset, small));
}

void TestNso() {
    const auto plain = BuildNso(false, false);
    CheckSyntheticReport(AnalyzeNso("main", plain.file));

    const auto compressed = BuildNso(true, false);
    CHECK(compressed.file.size() < plain.file.size());
    const auto lz4 = AnalyzeNso("main", compressed.file);
    CHECK(lz4.codec == NsoCodec::Lz4);
    CheckSyntheticReport(lz4);

    // ZBIC needs the frontend's decompressor; without it Prepare reports the module, not a crash.
    const auto zbic = BuildNso(true, true);
    const auto missing = AnalyzeNso("main", zbic.file);
    CHECK(!missing.valid);
    CHECK(missing.codec == NsoCodec::Zbic);
    CHECK(missing.error.find("zbic") != std::string::npos);
    // A stand-in ZBIC codec (here: LZ4 underneath) is used when supplied.
    const auto supplied = AnalyzeNso("main", zbic.file, [](NsoCodec codec, auto src, auto dst) {
        return codec == NsoCodec::Zbic && DecompressLz4Block(src, dst);
    });
    CheckSyntheticReport(supplied);

    CHECK(!AnalyzeNso("x", std::vector<std::uint8_t>(16)).valid);
    auto truncated = plain.file;
    truncated.resize(0x180);
    const auto cut = AnalyzeNso("main", truncated);
    CHECK(!cut.valid);
    CHECK(cut.error.find("past the end") != std::string::npos);

    PackReport pack;
    pack.program_id = 0x0400486024DEC000ULL;
    pack.title = "Test \"Title\"";
    pack.modules.push_back(AnalyzeNso("main", plain.file));
    pack.modules.push_back(cut);
    const auto json = ToJson(pack);
    CHECK(json.find("\"program_id\":\"0400486024DEC000\"") != std::string::npos);
    CHECK(json.find("\"patch_sites\":8") != std::string::npos);
    CHECK(json.find("\"nvn_module\":\"main\"") != std::string::npos);
    CHECK(json.find("Test \\\"Title\\\"") != std::string::npos);
    CHECK(json.find("\"svc_ids\":{\"0x01\":1,\"0x07\":2}") != std::string::npos);
    CHECK(json.find("NintendoSdk_nnSdk-20.5.4") != std::string::npos);

    CHECK(ExeFsModuleNames().size() == 13);
    CHECK(std::string_view(ExeFsModuleNames().front()) == "rtld");
    CHECK(std::string_view(ExeFsModuleNames().back()) == "sdk");
}

} // namespace

int main() {
    TestDevice();
    TestProfiles();
    TestS26();
    TestProcParsing();
    TestMemoryGovernor();
    TestThermalGovernor();
    TestLz4();
    TestNso();
    std::printf("loopnative_tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
