// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// LoopNative's bridge to the frontend (org.yuzu.yuzu_emu.loopnative.LoopNativeJni): the phone's
// tuning, a game's start plan and its session settings, Prepare (the ExeFS analysis behind the
// pack manifest) and the thermal/memory governor that runs while a game plays.

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include <jni.h>

#include "common/android/android_common.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/zbic_compression.h"
#include "core/core.h"
#include "core/file_sys/card_image.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nup_container.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/submission_package.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/loader/loader.h"
#include "jni/native.h"
#include "loopnative/device.h"
#include "loopnative/governor.h"
#include "loopnative/nso_analyzer.h"
#include "loopnative/report.h"
#include "loopnative/title_profiles.h"

namespace {

std::mutex state_mutex;
LoopNative::DeviceInfo device_info;
LoopNative::DeviceTuning device_tuning = LoopNative::ClassifyDevice(device_info);

struct GovernorState {
    std::optional<LoopNative::ThermalGovernor> thermal;
    LoopNative::MemoryGovernor memory;
    // What fps_limit and frame_skip were before the governor touched them.
    u32 saved_fps_limit = 0;
    int saved_frame_skip = 0;
    bool saved_fps_limit_global = true;
    bool saved_frame_skip_global = true;
    bool applied = false;
};
GovernorState governor;

std::string ReadSmallFile(const char* path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

/// Session values live in the per-game layer, so a save of the global config never keeps them.
template <typename T, bool ranged>
void SetSessionValue(Settings::SwitchableSetting<T, ranged>& setting, const T& value) {
    setting.SetGlobal(false);
    setting.SetValue(value);
}

bool Decompress(LoopNative::NsoCodec codec, std::span<const u8> src, std::span<u8> dst) {
    switch (codec) {
    case LoopNative::NsoCodec::Lz4:
        return LoopNative::DecompressLz4Block(src, dst);
    case LoopNative::NsoCodec::Zbic: {
        const int written = Common::Compression::DecompressDataZBIC(dst, src);
        return written > 0 && static_cast<size_t>(written) == dst.size();
    }
    case LoopNative::NsoCodec::None:
        break;
    }
    return false;
}

/// A NUP container's ExeFS, chosen as the loader does (core/loader/nup.cpp): the newest update's
/// when it has one, else the base package's.
FileSys::VirtualDir OpenNupExeFs(FileSys::NupContainer& nup) {
    if (nup.GetStatus() != Loader::ResultStatus::Success) {
        return nullptr;
    }
    const auto base = nup.FindBasePackage();
    const auto update = nup.FindBestUpdatePackage();
    if (update) {
        if (auto exefs = nup.GetExeFSDir(*update)) {
            return exefs;
        }
    }
    return base ? nup.GetExeFSDir(*base) : nullptr;
}

/// The game's ExeFS (with its installed update applied), whatever container holds it. A NUP
/// container is kept in `nup` for as long as its files are read.
FileSys::VirtualDir OpenExeFs(Core::System& system, const std::string& path, u64 program_id,
                              std::unique_ptr<FileSys::NupContainer>& nup, std::string& error) {
    const auto file = Core::GetGameFileFromPath(system.GetFilesystem(), path);
    if (file == nullptr) {
        error = "the game file could not be opened";
        return nullptr;
    }
    FileSys::VirtualDir exefs;
    switch (Loader::IdentifyFile(file)) {
    case Loader::FileType::NSP:
    case Loader::FileType::NSZ:
        exefs = FileSys::NSP(file, program_id).GetExeFS();
        break;
    case Loader::FileType::XCI:
    case Loader::FileType::XCZ:
        if (const auto nsp = FileSys::XCI(file, program_id).GetSecurePartitionNSP()) {
            exefs = nsp->GetExeFS();
        }
        break;
    case Loader::FileType::NCA:
        exefs = FileSys::NCA(file).GetExeFS();
        break;
    case Loader::FileType::DeconstructedRomDirectory:
        exefs = file->GetContainingDirectory();
        break;
    case Loader::FileType::NUP:
        nup = std::make_unique<FileSys::NupContainer>(file);
        exefs = OpenNupExeFs(*nup);
        break;
    default:
        error = "LoopNative cannot read this kind of game file";
        return nullptr;
    }
    if (exefs == nullptr) {
        error = "no ExeFS (are the keys installed?)";
        return nullptr;
    }
    if (program_id != 0) {
        const FileSys::PatchManager patches{program_id, system.GetFileSystemController(),
                                            system.GetContentProvider()};
        if (auto patched = patches.PatchExeFS(exefs)) {
            exefs = std::move(patched);
        }
    }
    return exefs;
}

} // namespace

extern "C" {

void Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_setDevice(
    JNIEnv* env, jobject, jstring jmanufacturer, jstring jmodel, jstring jdevice, jstring jsoc,
    jstring jgpu, jlong total_ram, jint sdk_int) {
    LoopNative::DeviceInfo info;
    info.manufacturer = Common::Android::GetJString(env, jmanufacturer);
    info.model = Common::Android::GetJString(env, jmodel);
    info.device = Common::Android::GetJString(env, jdevice);
    info.soc_model = Common::Android::GetJString(env, jsoc);
    info.gpu_renderer = Common::Android::GetJString(env, jgpu);
    info.total_ram_bytes = static_cast<u64>(total_ram);
    info.sdk_int = sdk_int;

    std::scoped_lock lock{state_mutex};
    device_info = std::move(info);
    device_tuning = LoopNative::ClassifyDevice(device_info);
    LOG_INFO(Frontend, "LoopNative: {} ({}), {} tier, GPU '{}'", device_tuning.display_name,
             LoopNative::ToString(device_tuning.device_class),
             LoopNative::ToString(device_tuning.ram_tier), device_info.gpu_renderer);
}

jstring Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_deviceTuning(JNIEnv* env, jobject) {
    std::scoped_lock lock{state_mutex};
    return Common::Android::ToJString(env, LoopNative::ToJson(device_tuning));
}

jstring Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_sessionPlan(JNIEnv* env, jobject,
                                                                       jstring jprogram_id) {
    const auto program_id =
        LoopNative::ParseProgramId(Common::Android::GetJString(env, jprogram_id)).value_or(0);
    std::scoped_lock lock{state_mutex};
    const auto plan = LoopNative::PlanSession(device_tuning, program_id);
    return Common::Android::ToJString(env, LoopNative::ToJson(device_tuning, plan));
}

jboolean Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_hasProfile(JNIEnv* env, jobject,
                                                                       jstring jprogram_id) {
    const auto program_id = LoopNative::ParseProgramId(Common::Android::GetJString(env, jprogram_id));
    return program_id && LoopNative::FindTitleProfile(*program_id) != nullptr;
}

/// Puts the session's settings in the per-game layer; returns how many it found.
jint Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_applySession(JNIEnv* env, jobject,
                                                                     jobjectArray jkeys,
                                                                     jobjectArray jvalues) {
    const jsize count = env->GetArrayLength(jkeys);
    if (env->GetArrayLength(jvalues) != count) {
        return 0;
    }
    jint applied = 0;
    for (jsize i = 0; i < count; ++i) {
        auto* jkey = static_cast<jstring>(env->GetObjectArrayElement(jkeys, i));
        auto* jvalue = static_cast<jstring>(env->GetObjectArrayElement(jvalues, i));
        const auto key = Common::Android::GetJString(env, jkey);
        const auto value = Common::Android::GetJString(env, jvalue);
        env->DeleteLocalRef(jkey);
        env->DeleteLocalRef(jvalue);

        const auto it = Settings::values.linkage.by_key.find(key);
        if (it == Settings::values.linkage.by_key.end() || it->second == nullptr) {
            LOG_WARNING(Frontend, "LoopNative: no setting named {}", key);
            continue;
        }
        auto* setting = it->second;
        if (setting->Switchable()) {
            setting->SetGlobal(false);
        }
        setting->LoadString(value);
        LOG_INFO(Frontend, "LoopNative: {} = {}", key, setting->ToString());
        ++applied;
    }
    return applied;
}

/// Session end: the settings the session put in the per-game layer follow the global ones again.
void Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_restoreSession(JNIEnv* env, jobject,
                                                                     jobjectArray jkeys) {
    const jsize count = env->GetArrayLength(jkeys);
    for (jsize i = 0; i < count; ++i) {
        auto* jkey = static_cast<jstring>(env->GetObjectArrayElement(jkeys, i));
        const auto key = Common::Android::GetJString(env, jkey);
        env->DeleteLocalRef(jkey);
        const auto it = Settings::values.linkage.by_key.find(key);
        if (it != Settings::values.linkage.by_key.end() && it->second != nullptr &&
            it->second->Switchable()) {
            it->second->SetGlobal(true);
        }
    }
}

/// Prepare: reads every module of the game's ExeFS, writes the pack manifest to manifest_path
/// and returns it.
jstring Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_prepare(JNIEnv* env, jobject,
                                                                 jstring jpath, jstring jprogram_id,
                                                                 jstring jtitle,
                                                                 jstring jmanifest_path) {
    const auto path = Common::Android::GetJString(env, jpath);
    const auto program_id =
        LoopNative::ParseProgramId(Common::Android::GetJString(env, jprogram_id)).value_or(0);
    const auto manifest_path = Common::Android::GetJString(env, jmanifest_path);

    LoopNative::PackReport pack;
    pack.program_id = program_id;
    pack.title = Common::Android::GetJString(env, jtitle);

    std::string error;
    std::unique_ptr<FileSys::NupContainer> nup;
    auto& system = EmulationSession::GetInstance().System();
    const auto exefs = OpenExeFs(system, path, program_id, nup, error);
    if (exefs == nullptr) {
        LoopNative::NsoReport missing;
        missing.file_name = "exefs";
        missing.error = error;
        pack.modules.push_back(std::move(missing));
    } else {
        for (const char* name : LoopNative::ExeFsModuleNames()) {
            const auto module = exefs->GetFile(name);
            if (module == nullptr) {
                continue;
            }
            const auto bytes = module->ReadAllBytes();
            pack.modules.push_back(LoopNative::AnalyzeNso(name, bytes, Decompress));
        }
    }

    const auto json = LoopNative::ToJson(pack);
    if (!manifest_path.empty()) {
        std::ofstream out(manifest_path, std::ios::trunc);
        out << json;
        if (!out) {
            LOG_WARNING(Frontend, "LoopNative: could not write {}", manifest_path);
        }
    }
    LOG_INFO(Frontend, "LoopNative: prepared {:016X} ({} modules)", program_id, pack.modules.size());
    return Common::Android::ToJString(env, json);
}

/// Starts the governor for a session whose own frame cap is base_fps_limit (0: the game's rate).
void Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_governorStart(JNIEnv*, jobject,
                                                                    jint base_fps_limit) {
    std::scoped_lock lock{state_mutex};
    governor.thermal.emplace(device_tuning, static_cast<u32>(base_fps_limit));
    governor.memory = LoopNative::MemoryGovernor{};
    governor.saved_fps_limit = Settings::values.fps_limit.GetValue();
    governor.saved_fps_limit_global = Settings::values.fps_limit.UsingGlobal();
    governor.saved_frame_skip = Settings::values.frame_skip.GetValue();
    governor.saved_frame_skip_global = Settings::values.frame_skip.UsingGlobal();
    governor.applied = false;
}

/// One governor step; applies a new frame cap when the thermal level changes. Returns the decision.
jstring Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_governorTick(JNIEnv* env, jobject,
                                                                      jfloat headroom,
                                                                      jint thermal_status,
                                                                      jboolean low_memory,
                                                                      jdouble seconds) {
    LoopNative::MemorySample memory;
    if (const auto info = LoopNative::ParseMemInfo(ReadSmallFile("/proc/meminfo"))) {
        memory.info = *info;
    }
    // Apps may not read PSI on every Android build: then the governor goes by meminfo alone.
    memory.psi = LoopNative::ParseMemoryPsi(ReadSmallFile("/proc/pressure/memory"));
    memory.android_low_memory = low_memory;

    std::scoped_lock lock{state_mutex};
    if (!governor.thermal) {
        return Common::Android::ToJString(env, "{}");
    }
    const auto pressure = governor.memory.Update(memory);
    const auto decision =
        governor.thermal->Update({seconds, headroom, static_cast<int>(thermal_status)});
    if (decision.changed) {
        SetSessionValue(Settings::values.fps_limit, decision.fps_limit);
        SetSessionValue(Settings::values.frame_skip, decision.frame_skip);
        governor.applied = true;
        LOG_INFO(Frontend, "LoopNative: thermal {} (headroom {:.2f}, status {}): fps_limit {}, frame_skip {}",
                 LoopNative::ToString(decision.level), headroom, thermal_status,
                 decision.fps_limit, decision.frame_skip);
    }
    return Common::Android::ToJString(env, LoopNative::ToJson(decision, pressure));
}

void Java_org_yuzu_yuzu_1emu_loopnative_LoopNativeJni_governorStop(JNIEnv*, jobject) {
    std::scoped_lock lock{state_mutex};
    if (governor.applied) {
        Settings::values.fps_limit.SetValue(governor.saved_fps_limit);
        Settings::values.fps_limit.SetGlobal(governor.saved_fps_limit_global);
        Settings::values.frame_skip.SetValue(governor.saved_frame_skip);
        Settings::values.frame_skip.SetGlobal(governor.saved_frame_skip_global);
    }
    governor.thermal.reset();
    governor.applied = false;
}

} // extern "C"
