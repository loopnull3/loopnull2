// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware_manager.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>
#include <fmt/format.h>
#include <common/fs/fs_paths.h>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"

#include "common/logging.h"

#include "core/crypto/key_manager.h"
#include "frontend_common/content_manager.h"

#ifdef __ANDROID__
#include <jni.h>
#include <common/android/id_cache.h>
#include <common/android/android_common.h>
#endif

#ifdef __ANDROID__
namespace {

std::vector<char> ReadKeyFile(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// A key file named prod.keys was copied over the installed one, which waits as `before`. The keys
// of a second console (a Switch 2's beside a Switch's) are both kept: content the main set cannot
// open is tried with the other key files (Core::Crypto::KeyManager::Alternates). The file with
// the base keys stays prod.keys, the new one when both or neither have them, and the other is
// kept as prod.<hash of its bytes>.keys.
void KeepOtherKeySet(const std::filesystem::path& prod, const std::filesystem::path& before) {
    std::error_code ec;
    if (!std::filesystem::exists(prod, ec)) {
        // The new file has another name: it is a second set under that name already.
        std::filesystem::rename(before, prod, ec);
        return;
    }
    const auto new_bytes = ReadKeyFile(prod);
    const auto old_bytes = ReadKeyFile(before);
    if (new_bytes == old_bytes) {
        std::filesystem::remove(before, ec);
        return;
    }
    const bool keep_old_main = !Core::Crypto::KeyManager::FileHasBaseKeys(prod)
                               && Core::Crypto::KeyManager::FileHasBaseKeys(before);
    u32 hash = 2166136261u;
    for (const char c : keep_old_main ? new_bytes : old_bytes) {
        hash = (hash ^ static_cast<u8>(c)) * 16777619u;
    }
    const auto other = prod.parent_path() / fmt::format("prod.{:08x}.keys", hash);
    if (keep_old_main) {
        std::filesystem::rename(prod, other, ec);
        std::filesystem::rename(before, prod, ec);
    } else {
        std::filesystem::rename(before, other, ec);
    }
    LOG_INFO(Frontend, "Two key sets: the {} file is prod.keys, the other is kept as {}",
             keep_old_main ? "installed" : "new", other.filename().string());
}

} // Anonymous namespace
#endif

FirmwareManager::KeyInstallResult
FirmwareManager::InstallKeys(std::string location, std::string extension) {
    LOG_INFO(Frontend, "Installing key files from {}", location);

    const auto keys_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::KeysDir);

#ifdef __ANDROID__
    JNIEnv *env = Common::Android::GetEnvForThread();

    jstring jsrc = Common::Android::ToJString(env, location);

    jclass native = Common::Android::GetNativeLibraryClass();
    jmethodID getExtension = Common::Android::GetFileExtension();

    jstring jext = static_cast<jstring>(env->CallStaticObjectMethod(
        native,
        getExtension,
        jsrc
    ));

    std::string ext = Common::Android::GetJString(env, jext);

    if (ext != extension) {
        return ErrorWrongFilename;
    }

    // (The installed prod.keys is set aside while the new file is copied: KeepOtherKeySet.)
    const auto prod_keys = keys_dir / "prod.keys";
    const auto prod_before = keys_dir / "prod.keys.before";
    std::error_code ec;
    if (std::filesystem::exists(prod_before, ec)) {
        // Left by an install that did not finish.
        if (std::filesystem::exists(prod_keys, ec)) {
            std::filesystem::remove(prod_before, ec);
        } else {
            std::filesystem::rename(prod_before, prod_keys, ec);
        }
    }
    bool had_prod_keys = std::filesystem::exists(prod_keys, ec);
    if (had_prod_keys) {
        std::filesystem::rename(prod_keys, prod_before, ec);
        had_prod_keys = !ec;
    }

    jmethodID copyToStorage = Common::Android::GetCopyToStorage();
    jstring jdest = Common::Android::ToJString(env, keys_dir.string() + "/");

    jboolean copyResult = env->CallStaticBooleanMethod(
        native,
        copyToStorage,
        jsrc,
        jdest
    );

    if (!copyResult) {
        if (had_prod_keys) {
            std::filesystem::remove(prod_keys, ec);
            std::filesystem::rename(prod_before, prod_keys, ec);
        }
        return ErrorFailedCopy;
    }
    if (had_prod_keys) {
        KeepOtherKeySet(prod_keys, prod_before);
    }
#else
    if (!location.ends_with(extension)) {
        return ErrorWrongFilename;
    }

    bool prod_keys_found = false;

    const std::filesystem::path prod_key_path = location;
    const std::filesystem::path key_source_path = prod_key_path.parent_path();

    if (!Common::FS::IsDir(key_source_path)) {
        return InvalidDir;
    }

    std::vector<std::filesystem::path> source_key_files;

    if (Common::FS::Exists(prod_key_path)) {
        prod_keys_found = true;
        source_key_files.emplace_back(prod_key_path);
    }

    if (Common::FS::Exists(key_source_path / "title.keys")) {
        source_key_files.emplace_back(key_source_path / "title.keys");
    }

    if (Common::FS::Exists(key_source_path / "key_retail.bin")) {
        source_key_files.emplace_back(key_source_path / "key_retail.bin");
    }

    if (source_key_files.empty() || !prod_keys_found) {
        return ErrorWrongFilename;
    }

    for (const auto &key_file : source_key_files) {
        std::filesystem::path destination_key_file = keys_dir / key_file.filename();
        if (!std::filesystem::copy_file(key_file,
                                        destination_key_file,
                                        std::filesystem::copy_options::overwrite_existing)) {
            LOG_ERROR(Frontend,
                      "Failed to copy file {} to {}",
                      key_file.string(),
                      destination_key_file.string());
            return ErrorFailedCopy;
        }
    }
#endif

    // Reinitialize the key manager
    Core::Crypto::KeyManager::Instance().ReloadKeys();

    if (ContentManager::AreKeysPresent()) {
        return Success;
    }

    // Let the frontend handle everything else
    return ErrorFailedInit;
}

FirmwareManager::FirmwareCheckResult FirmwareManager::VerifyFirmware(Core::System &system) {
    if (!CheckFirmwarePresence(system)) {
        return ErrorFirmwareMissing;
    } else {
        const auto pair = GetFirmwareVersion(system);
        const auto result = pair.second;

        if (result.IsError()) {
            LOG_INFO(Frontend, "Unable to read firmware");
            return ErrorFirmwareCorrupted;
        }
    }

    return FirmwareGood;
}
