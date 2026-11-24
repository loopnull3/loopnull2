// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "loopnative/arm64_scan.h"

namespace LoopNative {

enum class NsoCodec {
    None,
    Lz4,
    Zbic, ///< Switch 2 modules: a zstd variant (flags bit 7)
};

/// Decompresses one segment into exactly dst.size() bytes; false on failure. LoopNative decodes
/// LZ4 itself; the frontend supplies ZBIC (common/zbic_compression.h on Android).
using NsoDecompressor =
    std::function<bool(NsoCodec codec, std::span<const std::uint8_t> src, std::span<std::uint8_t> dst)>;

/// LZ4 block format, the whole output: false unless it decodes to exactly dst.size() bytes.
[[nodiscard]] bool DecompressLz4Block(std::span<const std::uint8_t> src, std::span<std::uint8_t> dst);

/// The default decompressor: LZ4 only.
[[nodiscard]] bool DecompressLz4Only(NsoCodec codec, std::span<const std::uint8_t> src,
                                     std::span<std::uint8_t> dst);

struct NsoSegment {
    std::uint32_t file_offset = 0;
    std::uint32_t memory_offset = 0;
    std::uint32_t size = 0;
    std::uint32_t compressed_size = 0;
    bool compressed = false;
};

/// One module of a game's ExeFS (rtld, main, subsdk*, sdk) as LoopNative's Prepare sees it.
struct NsoReport {
    std::string file_name;
    bool valid = false;
    std::string error;

    std::string build_id; ///< 40 hex digits, as patch files name modules
    std::string module_path; ///< the build's path to the module, from the start of .rodata
    NsoCodec codec = NsoCodec::None;
    NsoSegment text, rodata, data;
    std::uint32_t bss_size = 0;
    std::uint64_t image_size = 0;

    bool has_mod0 = false;
    std::uint64_t dynamic_entries = 0;
    std::uint64_t symbols = 0;
    std::uint64_t exported = 0;
    std::uint64_t imported = 0;

    /// NVN entry points the module defines (an NVN driver lives in the sdk module), and where.
    std::vector<std::pair<std::string, std::uint64_t>> nvn_exports;
    std::uint64_t nvn_imports = 0;
    std::optional<std::uint64_t> nvn_bootstrap_loader;

    std::vector<std::string> sdk_versions; ///< api_info entries ("SDK MW+Nintendo+...")
    Arm64ScanResult scan;
};

[[nodiscard]] NsoReport AnalyzeNso(std::string file_name, std::span<const std::uint8_t> file,
                                   const NsoDecompressor& decompress = DecompressLz4Only);

/// The ExeFS module names, in load order.
[[nodiscard]] std::span<const char* const> ExeFsModuleNames();

/// A Prepare report: every module of one game, for the pack manifest.
struct PackReport {
    std::uint64_t program_id = 0;
    std::string title;
    std::vector<NsoReport> modules;
};

[[nodiscard]] std::string ToJson(const NsoReport& report);
[[nodiscard]] std::string ToJson(const PackReport& report);

} // namespace LoopNative
