// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <optional>
#include <span>

#include <fmt/format.h>
#include <openssl/evp.h>

#include "common/common_types.h"
#include "common/swap.h"
#include "core/crypto/aes_util.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/fssystem/fssystem_nca_header.h"
#include "core/file_sys/nca_probe.h"
#include "core/file_sys/vfs/vfs.h"

namespace FileSys {

namespace {

using Digest = std::array<u8, 0x20>;

constexpr size_t HeaderSize = 0xC00;
constexpr size_t FsHeaderOffset = 0x400;
// The most of a hash layer that is read to check it.
constexpr s64 HashLayerLimit = 8 * 1024 * 1024;

Digest DigestOf(const EVP_MD* kind, std::span<const u8> data) {
    Digest out{};
    unsigned int length = 0;
    EVP_Digest(data.data(), data.size(), out.data(), &length, kind, nullptr);
    return out;
}

struct KeySet {
    const Core::Crypto::KeyManager* keys;
    std::string name;
};

// A key that may open a file system's content, and where it comes from.
struct ContentKey {
    Core::Crypto::Key128 key;
    std::string name;
};

// The part of a file system that proves a key: a hash the header gives, over bytes of the
// section, and where the content proper starts with a known value.
struct Proof {
    s64 hash_offset{};
    s64 hash_size{};
    s64 hash_block{};
    bool pad_to_block{};
    Digest expected{};
    s64 content_offset{};
    std::array<u8, 8> content_start{};
    size_t content_start_size{};
    std::string kind;
};

std::optional<Proof> ProofOf(const NcaFsHeader& fs) {
    Proof proof{};
    switch (fs.hash_type) {
    case NcaFsHeader::HashType::HierarchicalSha256Hash:
    case NcaFsHeader::HashType::HierarchicalSha3256Hash: {
        const auto& data = fs.hash_data.hierarchical_sha256_data;
        if (data.hash_layer_count < 2 ||
            static_cast<size_t>(data.hash_layer_count) > data.hash_layer_region.size()) {
            return std::nullopt;
        }
        proof.hash_offset = data.hash_layer_region[0].offset;
        proof.hash_size = data.hash_layer_region[0].size;
        proof.hash_block = data.hash_block_size;
        proof.expected = data.fs_data_master_hash.value;
        proof.content_offset = data.hash_layer_region[data.hash_layer_count - 1].offset;
        if (fs.fs_type == NcaFsHeader::FsType::PartitionFs) {
            proof.content_start = {'P', 'F', 'S', '0'};
            proof.content_start_size = 4;
        }
        proof.kind = "hash table";
        return proof;
    }
    case NcaFsHeader::HashType::HierarchicalIntegrityHash:
    case NcaFsHeader::HashType::HierarchicalIntegritySha3Hash: {
        const auto& meta = fs.hash_data.integrity_meta_info;
        const auto& levels = meta.level_hash_info;
        if (levels.max_layers < 3 || levels.max_layers - 2 >= levels.info.size() ||
            levels.info[0].block_order < 4 || levels.info[0].block_order > 24) {
            return std::nullopt;
        }
        proof.hash_block = s64{1} << levels.info[0].block_order;
        proof.hash_offset = levels.info[0].offset;
        proof.hash_size = (std::min<s64>)(levels.info[0].size, proof.hash_block);
        proof.pad_to_block = true;
        proof.expected = meta.master_hash.value;
        proof.content_offset = levels.info[levels.max_layers - 2].offset;
        if (fs.fs_type == NcaFsHeader::FsType::RomFs) {
            // A RomFS begins with the size of its own header.
            proof.content_start = {0x50, 0, 0, 0, 0, 0, 0, 0};
            proof.content_start_size = 8;
        }
        proof.kind = "first hash level";
        return proof;
    }
    default:
        return std::nullopt;
    }
}

void Decrypt(const Core::Crypto::Key128& key, u64 upper_iv, s64 file_offset, std::span<u8> data) {
    std::array<u8, 0x10> iv{};
    const u64 upper = Common::swap64(upper_iv);
    const u64 counter = Common::swap64(static_cast<u64>(file_offset) / 0x10);
    std::memcpy(iv.data(), &upper, sizeof(upper));
    std::memcpy(iv.data() + 8, &counter, sizeof(counter));
    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(key, Core::Crypto::Mode::CTR);
    cipher.SetIV(iv);
    cipher.Transcode(data.data(), data.size(), data.data(), Core::Crypto::Op::Decrypt);
}

bool HashMatches(const Proof& proof, std::span<const u8> layer) {
    std::vector<u8> padded;
    std::span<const u8> hashed = layer;
    if (proof.pad_to_block && static_cast<s64>(layer.size()) < proof.hash_block) {
        padded.assign(layer.begin(), layer.end());
        padded.resize(static_cast<size_t>(proof.hash_block));
        hashed = padded;
    }
    return DigestOf(EVP_sha256(), hashed) == proof.expected ||
           DigestOf(EVP_sha3_256(), hashed) == proof.expected ||
           DigestOf(EVP_sha256(), layer) == proof.expected ||
           DigestOf(EVP_sha3_256(), layer) == proof.expected;
}

} // Anonymous namespace

NcaProbe ProbeNca(const VirtualFile& file) {
    NcaProbe probe;
    auto& lines = probe.lines;
    if (file == nullptr) {
        probe.verdict = "no file";
        return probe;
    }
    const u64 file_size = file->GetSize();
    std::array<u8, HeaderSize> raw{};
    if (file_size < raw.size() || file->Read(raw.data(), raw.size(), 0) != raw.size()) {
        probe.verdict = "shorter than an NCA's header";
        lines.push_back(fmt::format("{} bytes: {}", file_size, probe.verdict));
        return probe;
    }

    // Which parts are structured and which are noise: of each 0x100 bytes, how many are zero
    // and how many values occur (encrypted bytes have about 1 and 162).
    std::string regions;
    for (size_t offset = 0; offset < raw.size(); offset += 0x100) {
        std::array<bool, 0x100> seen{};
        u32 zeros = 0;
        u32 distinct = 0;
        for (size_t i = 0; i < 0x100; ++i) {
            const u8 byte = raw[offset + i];
            zeros += byte == 0 ? 1 : 0;
            if (!seen[byte]) {
                seen[byte] = true;
                ++distinct;
            }
        }
        regions += fmt::format("{}{:03x}:{}/{}", offset == 0 ? "" : " ", offset, zeros, distinct);
    }
    lines.push_back(fmt::format("{} bytes; zero bytes/values per 0x100 of the header: {}",
                                file_size, regions));

    // The fields where an NCA has them, as stored.
    NcaHeader header{};
    std::memcpy(&header, raw.data(), sizeof(header));
    const bool has_rights_id =
        std::ranges::any_of(header.rights_id, [](u8 byte) { return byte != 0; });
    const u8 generation = (std::max)(header.key_generation, header.key_generation_2);
    const bool size_matches = header.content_size == file_size;
    const u8 platform = static_cast<u8>(header.program_id >> 56);
    lines.push_back(fmt::format(
        "read as an NCA's fields: distribution {}, content type {}, key generation {:#x}/{:#x}, "
        "key index {}, content size {:#x} ({}), program id {:016X}, content index {}, sdk "
        "{:#x}, signature key {}, rights id {}",
        static_cast<u32>(header.distribution_type), static_cast<u32>(header.content_type),
        header.key_generation, header.key_generation_2, header.key_index, header.content_size,
        size_matches ? "the file's size" : "not the file's size", header.program_id,
        header.content_index, header.sdk_addon_version, header.header1_signature_key_generation,
        has_rights_id ? "set" : "none"));

    // Every key set, and every key that could be over the content: the one the header names
    // first, then each key area key of each set, then the key area's bytes as they are.
    const auto alternates = Core::Crypto::KeyManager::Alternates();
    std::vector<KeySet> key_sets;
    key_sets.push_back({&Core::Crypto::KeyManager::Instance(), "key file 1"});
    for (size_t i = 0; i < alternates.size(); ++i) {
        key_sets.push_back({alternates[i].get(), fmt::format("key file {}", i + 2)});
    }
    Core::Crypto::Key128 area_key{};
    std::memcpy(area_key.data(),
                header.encrypted_key_area.data() + NcaHeader::DecryptionKey_AesCtr * 0x10,
                area_key.size());
    const bool has_area_key = std::ranges::any_of(area_key, [](u8 byte) { return byte != 0; });
    std::vector<ContentKey> content_keys;
    const u64 named_generation = generation > 0 ? generation - 1 : 0;
    const auto add_keys = [&](bool named) {
        for (const KeySet& set : key_sets) {
            for (u64 index = 0; index < 3; ++index) {
                for (u64 key_generation = 0; key_generation < 0x40; ++key_generation) {
                    const bool is_named =
                        index == header.key_index && key_generation == named_generation;
                    if (is_named != named ||
                        !set.keys->HasKey(Core::Crypto::S128KeyType::KeyArea, key_generation,
                                          index)) {
                        continue;
                    }
                    const auto kek =
                        set.keys->GetKey(Core::Crypto::S128KeyType::KeyArea, key_generation, index);
                    ContentKey content_key{
                        .key = {},
                        .name = fmt::format("{}, key area key {} of generation {:02x}{}", set.name,
                                            index, key_generation,
                                            named ? " (the one the header names)" : ""),
                    };
                    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(
                        kek, Core::Crypto::Mode::ECB);
                    cipher.Transcode(area_key.data(), area_key.size(), content_key.key.data(),
                                     Core::Crypto::Op::Decrypt);
                    content_keys.push_back(std::move(content_key));
                }
            }
        }
    };
    if (has_area_key && !has_rights_id) {
        add_keys(true);
        add_keys(false);
        content_keys.push_back({area_key, "the key area's bytes as they are"});
    }

    u32 sections = 0;
    u32 headers_verified = 0;
    u32 contents_opened = 0;
    std::string opened_with;
    for (s32 index = 0; index < NcaHeader::FsCountMax; ++index) {
        const auto& info = header.fs_info[index];
        if (info.start_sector == 0 && info.end_sector == 0) {
            continue;
        }
        ++sections;
        const u64 start = NcaHeader::SectorToByte(info.start_sector);
        const u64 end = NcaHeader::SectorToByte(info.end_sector);
        const bool in_file = start >= HeaderSize && start < end && end <= file_size;

        // The header of the file system: its hash is in the NCA's header.
        const std::span<const u8> stored{raw.data() + FsHeaderOffset + index * NcaFsHeader::Size,
                                         NcaFsHeader::Size};
        const Digest& expected = header.fs_header_hash[index].value;
        std::array<u8, NcaFsHeader::Size> fs_bytes{};
        std::string how;
        const auto matches = [&](std::span<const u8> bytes) -> const char* {
            if (DigestOf(EVP_sha256(), bytes) == expected) {
                return "SHA-256";
            }
            if (DigestOf(EVP_sha3_256(), bytes) == expected) {
                return "SHA3-256";
            }
            return nullptr;
        };
        if (const char* hash = matches(stored); hash != nullptr) {
            std::memcpy(fs_bytes.data(), stored.data(), fs_bytes.size());
            how = fmt::format("as stored ({})", hash);
        } else {
            for (const KeySet& set : key_sets) {
                if (!set.keys->HasKey(Core::Crypto::S256KeyType::Header)) {
                    continue;
                }
                std::array<u8, NcaFsHeader::Size> decrypted{};
                Core::Crypto::AESCipher<Core::Crypto::Key256> cipher(
                    set.keys->GetKey(Core::Crypto::S256KeyType::Header), Core::Crypto::Mode::XTS);
                cipher.XTSTranscode(stored.data(), stored.size(), decrypted.data(),
                                    static_cast<size_t>(2 + index), NcaHeader::XtsBlockSize,
                                    Core::Crypto::Op::Decrypt);
                if (const char* hash_decrypted = matches(decrypted); hash_decrypted != nullptr) {
                    fs_bytes = decrypted;
                    how = fmt::format("decrypted with the header key of {} ({})", set.name,
                                      hash_decrypted);
                    break;
                }
            }
        }
        if (how.empty()) {
            lines.push_back(fmt::format(
                "file system {}: bytes {:#x} to {:#x}{}; its header matches the header's hash "
                "neither as stored nor under an installed header key",
                index, start, end, in_file ? "" : " (not inside the file)"));
            continue;
        }
        ++headers_verified;
        NcaFsHeader fs{};
        std::memcpy(&fs, fs_bytes.data(), sizeof(fs));
        lines.push_back(fmt::format(
            "file system {}: bytes {:#x} to {:#x}{}; its header verifies {}: version {}, type {}, "
            "hash type {}, encryption {}, counter {:016x}, sparse generation {}, compression {}",
            index, start, end, in_file ? "" : " (not inside the file)", how, fs.version,
            static_cast<u32>(fs.fs_type), static_cast<u32>(fs.hash_type),
            static_cast<u32>(fs.encryption_type), fs.aes_ctr_upper_iv.value,
            fs.sparse_info.generation,
            fs.compression_info.bucket.size.Get() != 0 ? "yes" : "no"));
        if (!in_file) {
            continue;
        }

        // The content: opened by the key that makes its hash layer match the header's hash.
        const auto proof = ProofOf(fs);
        if (!proof || proof->hash_size <= 0 || proof->hash_offset < 0 ||
            proof->content_offset < 0 || (proof->hash_offset % 0x10) != 0 ||
            (proof->content_offset % 0x10) != 0 ||
            start + static_cast<u64>(proof->hash_offset + proof->hash_size) > end ||
            start + static_cast<u64>(proof->content_offset) + 0x10 > end) {
            lines.push_back(fmt::format("file system {}: no hash layer to check a key with",
                                        index));
            continue;
        }
        const bool whole_layer = proof->hash_size <= HashLayerLimit;
        std::vector<u8> layer(static_cast<size_t>(whole_layer ? proof->hash_size : 0));
        const u64 layer_at = start + static_cast<u64>(proof->hash_offset);
        if (whole_layer && file->Read(layer.data(), layer.size(), layer_at) != layer.size()) {
            lines.push_back(fmt::format("file system {}: its {} cannot be read", index,
                                        proof->kind));
            continue;
        }
        std::array<u8, 0x10> content{};
        const u64 content_at = start + static_cast<u64>(proof->content_offset);
        if (file->Read(content.data(), content.size(), content_at) != content.size()) {
            continue;
        }
        const auto starts_right = [&](std::span<const u8> bytes) {
            return proof->content_start_size != 0 &&
                   std::memcmp(bytes.data(), proof->content_start.data(),
                               proof->content_start_size) == 0;
        };
        std::string outcome;
        if ((whole_layer && HashMatches(*proof, layer)) || starts_right(content)) {
            outcome = fmt::format("is stored in the clear ({}{})",
                                  whole_layer && HashMatches(*proof, layer) ? "its " : "",
                                  whole_layer && HashMatches(*proof, layer)
                                      ? proof->kind + " matches"
                                      : std::string{"its content starts as it should"});
        }
        if (outcome.empty()) {
            for (const ContentKey& candidate : content_keys) {
                // A few bytes first: all the keys are tried, the layer can be megabytes.
                bool passes = false;
                if (proof->content_start_size != 0) {
                    auto bytes = content;
                    Decrypt(candidate.key, fs.aes_ctr_upper_iv.value,
                            static_cast<s64>(content_at), bytes);
                    passes = starts_right(bytes);
                    if (!passes) {
                        continue;
                    }
                }
                bool hash_ok = false;
                if (whole_layer) {
                    auto bytes = layer;
                    Decrypt(candidate.key, fs.aes_ctr_upper_iv.value, static_cast<s64>(layer_at),
                            bytes);
                    hash_ok = HashMatches(*proof, bytes) || HashMatches(*proof, layer);
                }
                if (hash_ok || passes) {
                    outcome = fmt::format("opens with {} ({})", candidate.name,
                                          hash_ok ? "its " + proof->kind + " matches"
                                                  : "its content starts as it should");
                    opened_with = candidate.name;
                    break;
                }
            }
        }
        if (outcome.empty()) {
            outcome = fmt::format("opens with none of the {} keys tried{}", content_keys.size(),
                                  has_rights_id ? " (it names a ticket's key)"
                                  : !has_area_key ? " (its key area is empty)"
                                                  : "");
        } else {
            ++contents_opened;
        }
        lines.push_back(fmt::format("file system {}: the content {} ({} of {:#x} bytes at {:#x}, "
                                    "content at {:#x})",
                                    index, outcome, proof->kind, proof->hash_size,
                                    proof->hash_offset, proof->content_offset));
    }

    if (sections == 0 || (!size_matches && headers_verified == 0)) {
        probe.verdict = "not laid out as an NCA where one has its header";
    } else if (headers_verified == 0) {
        probe.verdict = fmt::format(
            "header fields read as an NCA's ({}), no file system header verifies",
            platform == 0x04 ? "a Switch 2 title" : platform == 0x01 ? "a Switch title" : "title");
    } else if (contents_opened == 0) {
        probe.verdict = fmt::format("laid out as an NCA, {} of {} file system headers verify, the "
                                    "installed keys open no content",
                                    headers_verified, sections);
    } else {
        probe.verdict = fmt::format(
            "laid out as an NCA, {} of {} file system headers verify, content opens{}",
            headers_verified, sections,
            opened_with.empty() ? " (stored in the clear)" : " with " + opened_with);
    }
    lines.push_back(probe.verdict);
    return probe;
}

} // namespace FileSys
