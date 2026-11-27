// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

#include "common/logging.h"
#include <ranges>
#include "core/crypto/aes_util.h"
#include "core/crypto/ctr_encryption_layer.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/partition_filesystem.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/loader/loader.h"

#include "core/file_sys/fssystem/fssystem_compression_configuration.h"
#include "core/file_sys/fssystem/fssystem_crypto_configuration.h"
#include "core/file_sys/fssystem/fssystem_nca_file_system_driver.h"

namespace FileSys {

static u8 MasterKeyIdForKeyGeneration(u8 key_generation) {
    return std::max<u8>(key_generation, 1) - 1;
}

NCA::NCA(VirtualFile file_, const NCA* base_nca, bool allow_missing_base)
    : file(std::move(file_)) {
    if (file == nullptr) {
        status = Loader::ResultStatus::ErrorNullFile;
        return;
    }

    {
        Core::Crypto::KeyManager::ScopedKeySet scope{nullptr};
        status = Open(Core::Crypto::KeyManager::Instance(), base_nca, allow_missing_base);
    }

    // Content of another console (a Switch 2's system titles beside a Switch's keys, or the other
    // way round) does not open with the main keys: its header does not decrypt, or its key
    // generation is missing. Try the other key files' sets before giving up.
    const auto needs_other_keys = [](Loader::ResultStatus s) {
        return s == Loader::ResultStatus::ErrorBadNCAHeader ||
               s == Loader::ResultStatus::ErrorMissingKeyAreaKey ||
               s == Loader::ResultStatus::ErrorMissingTitlekey ||
               s == Loader::ResultStatus::ErrorMissingTitlekek;
    };
    if (!needs_other_keys(status)) {
        return;
    }
    const auto first_status = status;
    for (const auto& set : Core::Crypto::KeyManager::Alternates()) {
        Core::Crypto::KeyManager::ScopedKeySet scope{set.get()};
        sections.clear();
        is_update = false;
        const auto with_set = Open(*set, base_nca, allow_missing_base);
        if (!needs_other_keys(with_set)) {
            key_set = set;
            status = with_set;
            return;
        }
    }
    sections.clear();
    is_update = false;
    status = first_status;
}

Loader::ResultStatus NCA::Open(const Core::Crypto::KeyManager& keys, const NCA* base_nca,
                               bool allow_missing_base) {
    reader = std::make_shared<NcaReader>();
    if (Result rc = reader->Initialize(file, GetCryptoConfiguration(), GetNcaCompressionConfiguration()); R_FAILED(rc)) {
        return Loader::ResultStatus::ErrorBadNCAHeader;
    }

    // Ensure we have the proper key area keys to continue.
    const u8 master_key_id = MasterKeyIdForKeyGeneration(reader->GetKeyGeneration());
    if (!keys.HasKey(Core::Crypto::S128KeyType::KeyArea, master_key_id, reader->GetKeyIndex())) {
        LOG_ERROR(Loader, "Cannot decrypt {}: keys for gen 0x{:02X}+ required.", file->GetName(), reader->GetKeyGeneration());
        return Loader::ResultStatus::ErrorMissingKeyAreaKey;
    }

    RightsId rights_id{};
    reader->GetRightsId(rights_id.data(), rights_id.size());
    if (rights_id != RightsId{}) {
        // External decryption key required; provide it here.
        u128 rights_id_u128;
        std::memcpy(rights_id_u128.data(), rights_id.data(), sizeof(rights_id));

        // (Title keys read from tickets are kept by the main set.)
        auto titlekey =
            keys.GetKey(Core::Crypto::S128KeyType::Titlekey, rights_id_u128[1], rights_id_u128[0]);
        if (titlekey == Core::Crypto::Key128{}) {
            titlekey = Core::Crypto::KeyManager::Instance().GetKey(
                Core::Crypto::S128KeyType::Titlekey, rights_id_u128[1], rights_id_u128[0]);
        }
        if (titlekey == Core::Crypto::Key128{}) {
            return Loader::ResultStatus::ErrorMissingTitlekey;
        }

        if (!keys.HasKey(Core::Crypto::S128KeyType::Titlekek, master_key_id)) {
            return Loader::ResultStatus::ErrorMissingTitlekek;
        }

        auto titlekek = keys.GetKey(Core::Crypto::S128KeyType::Titlekek, master_key_id);
        Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(titlekek, Core::Crypto::Mode::ECB);
        cipher.Transcode(titlekey.data(), titlekey.size(), titlekey.data(),
                         Core::Crypto::Op::Decrypt);

        reader->SetExternalDecryptionKey(titlekey.data(), titlekey.size());
    }

    base_reader = base_nca ? base_nca->reader : nullptr;
    const s32 fs_count = reader->GetFsCount();
    for (s32 i = 0; i < fs_count; i++) {
        NcaFsHeaderReader header_reader;
        if (!reader->HasFsInfo(i) || R_FAILED(header_reader.Initialize(*reader, i))) {
            return Loader::ResultStatus::ErrorBadNCAHeader;
        }

        auto& section = sections.emplace_back();
        section.index = i;
        section.partition = header_reader.GetFsType() == NcaFsHeader::FsType::PartitionFs;

        if (header_reader.GetEncryptionType() == NcaFsHeader::EncryptionType::AesCtrEx) {
            is_update = true;
        }
    }

    if (is_update && base_nca == nullptr && !allow_missing_base) {
        return Loader::ResultStatus::ErrorMissingBKTRBaseRomFS;
    }
    return Loader::ResultStatus::Success;
}

NCA::~NCA() = default;

Loader::ResultStatus NCA::GetStatus() const {
    return status;
}

void NCA::OpenSection(Section& section) const {
    if (section.opened) {
        return;
    }
    section.opened = true;
    Core::Crypto::KeyManager::ScopedKeySet scope{key_set.get()};
    NcaFileSystemDriver fs(base_reader, reader);
    NcaFsHeaderReader header_reader;
    if (R_FAILED(fs.OpenStorage(&section.storage, &header_reader, section.index))) {
        LOG_ERROR(Loader, "Cannot open section {} of {}", section.index, file->GetName());
        section.storage = nullptr;
        return;
    }
    if (section.partition) {
        auto pfs = std::make_shared<PartitionFilesystem>(section.storage);
        if (pfs->GetStatus() == Loader::ResultStatus::Success) {
            section.partition_fs = std::move(pfs);
        }
    }
}

std::vector<VirtualFile> NCA::GetFiles() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    std::scoped_lock lock{sections_mutex};
    std::vector<VirtualFile> files;
    for (auto& section : sections) {
        if (section.partition) {
            continue;
        }
        OpenSection(section);
        if (section.storage) {
            files.push_back(section.storage);
        }
    }
    return files;
}

std::vector<VirtualDir> NCA::GetSubdirectories() const {
    if (status != Loader::ResultStatus::Success) {
        return {};
    }
    std::scoped_lock lock{sections_mutex};
    std::vector<VirtualDir> dirs;
    for (auto& section : sections) {
        if (!section.partition) {
            continue;
        }
        OpenSection(section);
        if (section.partition_fs) {
            dirs.push_back(section.partition_fs);
        }
    }
    return dirs;
}

std::string NCA::GetName() const {
    return file->GetName();
}

VirtualDir NCA::GetParentDirectory() const {
    return file->GetContainingDirectory();
}

NCAContentType NCA::GetType() const {
    return static_cast<NCAContentType>(reader->GetContentType());
}

u64 NCA::GetTitleId() const {
    if (is_update) {
        return reader->GetProgramId() | 0x800;
    }
    return reader->GetProgramId();
}

RightsId NCA::GetRightsId() const {
    RightsId result;
    reader->GetRightsId(result.data(), result.size());
    return result;
}

u32 NCA::GetSDKVersion() const {
    return reader->GetSdkAddonVersion();
}

u8 NCA::GetKeyGeneration() const {
    return reader->GetKeyGeneration();
}

bool NCA::IsUpdate() const {
    return is_update;
}

VirtualFile NCA::GetRomFS() const {
    std::scoped_lock lock{sections_mutex};
    VirtualFile romfs;
    for (auto& section : sections) {
        if (section.partition) {
            continue;
        }
        OpenSection(section);
        if (section.storage) {
            romfs = section.storage;
        }
    }
    return romfs;
}

VirtualDir NCA::GetExeFS() const {
    std::scoped_lock lock{sections_mutex};
    for (auto& section : sections) {
        if (!section.partition) {
            continue;
        }
        OpenSection(section);
        if (section.partition_fs && IsDirectoryExeFS(section.partition_fs)) {
            return section.partition_fs;
        }
    }
    return nullptr;
}

VirtualFile NCA::GetBaseFile() const {
    return file;
}

VirtualDir NCA::GetLogoPartition() const {
    std::scoped_lock lock{sections_mutex};
    for (auto& section : sections) {
        if (!section.partition) {
            continue;
        }
        OpenSection(section);
        if (section.partition_fs && !IsDirectoryExeFS(section.partition_fs) &&
            IsDirectoryLogoPartition(section.partition_fs)) {
            return section.partition_fs;
        }
    }
    return nullptr;
}

} // namespace FileSys
