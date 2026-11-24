// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

#include "loopnative/json_writer.h"
#include "loopnative/nso_analyzer.h"

namespace LoopNative {

namespace {

constexpr std::size_t HEADER_SIZE = 0x100;
constexpr std::uint32_t NSO_MAGIC = 0x304F534EU;  // "NSO0"
constexpr std::uint32_t MOD0_MAGIC = 0x30444F4DU; // "MOD0"
constexpr std::size_t SYMBOL_SIZE = 24;            // Elf64_Sym
constexpr std::uint64_t MAX_IMAGE_SIZE = 1ULL << 30;

constexpr std::array<const char*, 13> MODULE_NAMES{
    "rtld",     "main",     "subsdk0",  "subsdk1", "subsdk2", "subsdk3", "subsdk4",
    "subsdk5",  "subsdk6",  "subsdk7",  "subsdk8", "subsdk9", "sdk",
};

template <typename T>
std::optional<T> ReadAt(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
        return std::nullopt;
    }
    T value;
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::string Hex(std::span<const std::uint8_t> bytes) {
    static constexpr char DIGITS[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out += DIGITS[byte >> 4];
        out += DIGITS[byte & 0xF];
    }
    return out;
}

std::string_view CString(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
    if (offset >= bytes.size()) {
        return {};
    }
    const auto* start = reinterpret_cast<const char*>(bytes.data() + offset);
    const auto max = bytes.size() - offset;
    return std::string_view(start, strnlen(start, max));
}

std::string_view CodecName(NsoCodec codec) {
    switch (codec) {
    case NsoCodec::None:
        return "none";
    case NsoCodec::Lz4:
        return "lz4";
    case NsoCodec::Zbic:
        return "zbic";
    }
    return "unknown";
}

void WriteSegment(JsonWriter& json, const NsoSegment& segment) {
    json.BeginObject()
        .Key("memory_offset").Number(segment.memory_offset)
        .Key("size").Number(segment.size)
        .Key("compressed_size").Number(segment.compressed_size)
        .Key("compressed").Bool(segment.compressed)
        .EndObject();
}

void WriteReport(JsonWriter& json, const NsoReport& report) {
    json.BeginObject();
    json.Key("file").String(report.file_name);
    json.Key("valid").Bool(report.valid);
    if (!report.valid) {
        json.Key("error").String(report.error);
        json.EndObject();
        return;
    }
    json.Key("build_id").String(report.build_id);
    json.Key("module_path").String(report.module_path);
    json.Key("codec").String(CodecName(report.codec));
    json.Key("text");
    WriteSegment(json, report.text);
    json.Key("rodata");
    WriteSegment(json, report.rodata);
    json.Key("data");
    WriteSegment(json, report.data);
    json.Key("bss_size").Number(report.bss_size);
    json.Key("image_size").Number(report.image_size);
    json.Key("mod0").Bool(report.has_mod0);
    json.Key("dynamic_entries").Number(report.dynamic_entries);
    json.Key("symbols").Number(report.symbols);
    json.Key("exported").Number(report.exported);
    json.Key("imported").Number(report.imported);
    json.Key("nvn_imports").Number(report.nvn_imports);
    json.Key("nvn_bootstrap_loader");
    if (report.nvn_bootstrap_loader) {
        json.Number(*report.nvn_bootstrap_loader);
    } else {
        json.Null();
    }
    json.Key("nvn_exports").BeginArray();
    for (const auto& [name, offset] : report.nvn_exports) {
        json.BeginObject().Key("name").String(name).Key("offset").Number(offset).EndObject();
    }
    json.EndArray();
    json.Key("sdk_versions").BeginArray();
    for (const auto& version : report.sdk_versions) {
        json.String(version);
    }
    json.EndArray();

    const auto& scan = report.scan;
    json.Key("code").BeginObject();
    json.Key("instructions").Number(scan.instructions);
    json.Key("patch_sites").Number(scan.PatchSites());
    json.Key("svc").Number(scan.svc_sites);
    json.Key("mrs_tpidrro_el0").Number(scan.mrs_tpidrro_el0);
    json.Key("mrs_tpidr_el0").Number(scan.mrs_tpidr_el0);
    json.Key("msr_tpidr_el0").Number(scan.msr_tpidr_el0);
    json.Key("mrs_cntpct_el0").Number(scan.mrs_cntpct_el0);
    json.Key("mrs_cntvct_el0").Number(scan.mrs_cntvct_el0);
    json.Key("mrs_cntfrq_el0").Number(scan.mrs_cntfrq_el0);
    json.Key("mrs_ctr_el0").Number(scan.mrs_ctr_el0);
    json.Key("mrs_dczid_el0").Number(scan.mrs_dczid_el0);
    json.Key("svc_ids").BeginObject();
    for (std::size_t id = 0; id < scan.svc_by_id.size(); ++id) {
        if (scan.svc_by_id[id] != 0) {
            char key[24];
            std::snprintf(key, sizeof(key), "0x%02zX", id);
            json.Key(key).Number(scan.svc_by_id[id]);
        }
    }
    json.EndObject();
    json.EndObject();
    json.EndObject();
}

} // namespace

bool DecompressLz4Block(std::span<const std::uint8_t> src, std::span<std::uint8_t> dst) {
    std::size_t ip = 0;
    std::size_t op = 0;
    const std::size_t in_end = src.size();
    const std::size_t out_end = dst.size();

    const auto read_length = [&](std::size_t length) -> std::optional<std::size_t> {
        if (length != 15) {
            return length;
        }
        while (true) {
            if (ip >= in_end) {
                return std::nullopt;
            }
            const std::uint8_t byte = src[ip++];
            length += byte;
            if (byte != 255) {
                return length;
            }
        }
    };

    while (ip < in_end) {
        const std::uint8_t token = src[ip++];
        const auto literals = read_length(token >> 4);
        if (!literals || *literals > in_end - ip || *literals > out_end - op) {
            return false;
        }
        std::memcpy(dst.data() + op, src.data() + ip, *literals);
        ip += *literals;
        op += *literals;
        if (ip >= in_end) {
            break; // the last sequence carries literals only
        }
        if (in_end - ip < 2) {
            return false;
        }
        const std::size_t offset = static_cast<std::size_t>(src[ip]) | (static_cast<std::size_t>(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) {
            return false;
        }
        const auto match = read_length(token & 0xF);
        if (!match) {
            return false;
        }
        const std::size_t length = *match + 4;
        if (length > out_end - op) {
            return false;
        }
        // Byte by byte: a match may overlap the bytes it produces (offset < length).
        for (std::size_t i = 0; i < length; ++i, ++op) {
            dst[op] = dst[op - offset];
        }
    }
    return op == out_end;
}

bool DecompressLz4Only(NsoCodec codec, std::span<const std::uint8_t> src, std::span<std::uint8_t> dst) {
    return codec == NsoCodec::Lz4 && DecompressLz4Block(src, dst);
}

std::span<const char* const> ExeFsModuleNames() {
    return MODULE_NAMES;
}

NsoReport AnalyzeNso(std::string file_name, std::span<const std::uint8_t> file,
                     const NsoDecompressor& decompress) {
    NsoReport report;
    report.file_name = std::move(file_name);
    const auto fail = [&](std::string error) {
        report.valid = false;
        report.error = std::move(error);
        return report;
    };

    if (file.size() < HEADER_SIZE || ReadAt<std::uint32_t>(file, 0) != NSO_MAGIC) {
        return fail("not an NSO (no NSO0 header)");
    }
    const std::uint32_t flags = *ReadAt<std::uint32_t>(file, 0x0C);
    std::array<NsoSegment*, 3> segments{&report.text, &report.rodata, &report.data};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto& segment = *segments[i];
        segment.file_offset = *ReadAt<std::uint32_t>(file, 0x10 + i * 0x10);
        segment.memory_offset = *ReadAt<std::uint32_t>(file, 0x14 + i * 0x10);
        segment.size = *ReadAt<std::uint32_t>(file, 0x18 + i * 0x10);
        segment.compressed_size = *ReadAt<std::uint32_t>(file, 0x60 + i * 4);
        segment.compressed = ((flags >> i) & 1) != 0;
    }
    report.bss_size = *ReadAt<std::uint32_t>(file, 0x3C);
    report.build_id = Hex(file.subspan(0x40, 0x14));
    const bool zbic = ((flags >> 7) & 1) != 0;
    const bool any_compressed = report.text.compressed || report.rodata.compressed || report.data.compressed;
    report.codec = !any_compressed ? NsoCodec::None : zbic ? NsoCodec::Zbic : NsoCodec::Lz4;

    std::uint64_t image_end = 0;
    for (const auto* segment : segments) {
        image_end = std::max<std::uint64_t>(image_end, std::uint64_t{segment->memory_offset} + segment->size);
    }
    if (image_end == 0 || image_end > MAX_IMAGE_SIZE) {
        return fail("segment layout out of range");
    }
    report.image_size = image_end + report.bss_size;
    std::vector<std::uint8_t> image(image_end);

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& segment = *segments[i];
        const std::uint32_t stored = segment.compressed ? segment.compressed_size : segment.size;
        if (std::uint64_t{segment.file_offset} + stored > file.size()) {
            return fail("segment " + std::to_string(i) + " past the end of the file");
        }
        const auto source = file.subspan(segment.file_offset, stored);
        const auto target = std::span<std::uint8_t>(image).subspan(segment.memory_offset, segment.size);
        if (!segment.compressed) {
            std::memcpy(target.data(), source.data(), segment.size);
        } else if (!decompress(report.codec, source, target)) {
            return fail(std::string("segment ") + std::to_string(i) + " did not decompress (" +
                        std::string(CodecName(report.codec)) + ")");
        }
    }
    const std::span<const std::uint8_t> bytes(image);

    // .rodata starts with the module's build path: u32 0, u32 length, the path.
    if (const auto length = ReadAt<std::uint32_t>(bytes, report.rodata.memory_offset + 4);
        ReadAt<std::uint32_t>(bytes, report.rodata.memory_offset) == 0u && length && *length > 0 &&
        *length < 0x400 && report.rodata.memory_offset + 8ULL + *length <= bytes.size()) {
        report.module_path = std::string(
            CString(bytes.first(report.rodata.memory_offset + 8 + *length), report.rodata.memory_offset + 8));
    }

    // MOD0: its offset is the second word of .text; its fields are relative to itself.
    if (const auto mod0_offset = ReadAt<std::uint32_t>(bytes, report.text.memory_offset + 4)) {
        const std::uint64_t mod0 = report.text.memory_offset + std::uint64_t{*mod0_offset};
        if (ReadAt<std::uint32_t>(bytes, mod0) == MOD0_MAGIC) {
            report.has_mod0 = true;
            const auto dynamic_rel = ReadAt<std::int32_t>(bytes, mod0 + 4);
            if (dynamic_rel) {
                std::uint64_t entry = mod0 + static_cast<std::int64_t>(*dynamic_rel);
                while (const auto tag = ReadAt<std::int64_t>(bytes, entry)) {
                    ++report.dynamic_entries;
                    if (*tag == 0 || report.dynamic_entries > 0x1000) {
                        break;
                    }
                    entry += 16;
                }
            }
        }
    }

    // Symbols, from the header's .dynsym/.dynstr extents (relative to .rodata).
    const std::uint64_t rodata_base = report.rodata.memory_offset;
    const auto dynstr_offset = *ReadAt<std::uint32_t>(file, 0x90);
    const auto dynstr_size = *ReadAt<std::uint32_t>(file, 0x94);
    const auto dynsym_offset = *ReadAt<std::uint32_t>(file, 0x98);
    const auto dynsym_size = *ReadAt<std::uint32_t>(file, 0x9C);
    const std::uint64_t strtab = rodata_base + dynstr_offset;
    if (dynsym_size != 0 && rodata_base + dynsym_offset + dynsym_size <= bytes.size() &&
        strtab + dynstr_size <= bytes.size()) {
        const auto strings = bytes.subspan(strtab, dynstr_size);
        const std::uint64_t count = dynsym_size / SYMBOL_SIZE;
        report.symbols = count;
        for (std::uint64_t i = 1; i < count; ++i) {
            const std::uint64_t symbol = rodata_base + dynsym_offset + i * SYMBOL_SIZE;
            const auto name_offset = *ReadAt<std::uint32_t>(bytes, symbol);
            const auto info = *ReadAt<std::uint8_t>(bytes, symbol + 4);
            const auto section = *ReadAt<std::uint16_t>(bytes, symbol + 6);
            const auto value = *ReadAt<std::uint64_t>(bytes, symbol + 8);
            const std::uint8_t binding = info >> 4;
            if (binding != 1 && binding != 2) {
                continue; // local
            }
            const auto name = CString(strings, name_offset);
            const bool defined = section != 0;
            if (defined) {
                ++report.exported;
            } else {
                ++report.imported;
            }
            if (!name.starts_with("nvn")) {
                continue;
            }
            if (!defined) {
                ++report.nvn_imports;
                continue;
            }
            report.nvn_exports.emplace_back(std::string(name), value);
            if (name == "nvnBootstrapLoader") {
                report.nvn_bootstrap_loader = value;
            }
        }
    }

    // api_info: NUL-separated strings naming the SDK and middleware the module was built with.
    const auto api_offset = *ReadAt<std::uint32_t>(file, 0x88);
    const auto api_size = *ReadAt<std::uint32_t>(file, 0x8C);
    if (api_size != 0 && rodata_base + api_offset + api_size <= bytes.size()) {
        const auto api = bytes.subspan(rodata_base + api_offset, api_size);
        std::uint64_t at = 0;
        while (at < api.size() && report.sdk_versions.size() < 32) {
            const auto entry = CString(api, at);
            if (entry.starts_with("SDK ")) {
                report.sdk_versions.emplace_back(entry);
            }
            at += entry.size() + 1;
        }
    }

    report.scan = ScanArm64(bytes.subspan(report.text.memory_offset, report.text.size));
    report.valid = true;
    return report;
}

std::string ToJson(const NsoReport& report) {
    JsonWriter json;
    WriteReport(json, report);
    return json.Str();
}

std::string ToJson(const PackReport& pack) {
    JsonWriter json;
    char program_id[17];
    std::snprintf(program_id, sizeof(program_id), "%016llX",
                  static_cast<unsigned long long>(pack.program_id));

    std::uint64_t patch_sites = 0;
    std::uint64_t svc_sites = 0;
    std::uint64_t image_size = 0;
    std::string nvn_module;
    std::optional<std::uint64_t> nvn_bootstrap;
    std::string sdk_version;
    for (const auto& module : pack.modules) {
        if (!module.valid) {
            continue;
        }
        patch_sites += module.scan.PatchSites();
        svc_sites += module.scan.svc_sites;
        image_size += module.image_size;
        if (module.nvn_bootstrap_loader && !nvn_bootstrap) {
            nvn_bootstrap = module.nvn_bootstrap_loader;
            nvn_module = module.file_name;
        }
        for (const auto& version : module.sdk_versions) {
            if (sdk_version.empty() && version.find("NintendoSdk_nnSdk") != std::string::npos) {
                sdk_version = version;
            }
        }
    }

    json.BeginObject();
    json.Key("format").String("loopnative-pack-manifest");
    json.Key("version").Number(1);
    json.Key("program_id").String(program_id);
    json.Key("title").String(pack.title);
    json.Key("summary").BeginObject();
    json.Key("modules").Number(static_cast<std::uint64_t>(pack.modules.size()));
    json.Key("image_size").Number(image_size);
    json.Key("patch_sites").Number(patch_sites);
    json.Key("svc_sites").Number(svc_sites);
    json.Key("nvn_module").String(nvn_module);
    json.Key("nvn_bootstrap_loader");
    if (nvn_bootstrap) {
        json.Number(*nvn_bootstrap);
    } else {
        json.Null();
    }
    json.Key("sdk").String(sdk_version);
    json.EndObject();
    json.Key("modules").BeginArray();
    for (const auto& module : pack.modules) {
        WriteReport(json, module);
    }
    json.EndArray();
    json.EndObject();
    return json.Str();
}

} // namespace LoopNative
