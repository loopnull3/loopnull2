// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// "Build all shaders now" for the emulator engines (org.yuzu.yuzu_emu.utils.ShaderPrecache): the
// shader precache of video_core/shader_warm, which the desktop app has in its game list. Build
// reads the game's files once and writes the precache file; a start with it builds its programs
// and the graphics pipelines saved from earlier play on the loading screen.

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>

#include <jni.h>

#include "common/android/android_common.h"
#include "common/logging.h"
#include "common/settings.h"
#include "core/core.h"
#include "jni/native.h"
#include "loopnative/json_writer.h"
#include "video_core/shader_warm/shader_warm_builder.h"

namespace {

namespace ShaderWarm = VideoCommon::ShaderWarm;

std::mutex build_mutex;
std::optional<std::stop_source> build_stop;

bool HasPrecache(u64 title_id) {
    std::error_code ec;
    return title_id != 0 && std::filesystem::exists(ShaderWarm::ComputeWarmPath(title_id), ec);
}

} // namespace

extern "C" {

/// Builds the game's precache file on the calling thread, reporting to the object's
/// onBuildProgress(String, long, long). Returns the result as JSON.
jstring Java_org_yuzu_yuzu_1emu_utils_ShaderPrecache_nativeBuild(JNIEnv* env, jobject thiz,
                                                                  jstring jpath,
                                                                  jstring jprogram_id) {
    const auto path = Common::Android::GetJString(env, jpath);
    const u64 title_id = EmulationSession::GetProgramId(env, jprogram_id);

    std::stop_source stop;
    {
        std::scoped_lock lock{build_mutex};
        build_stop = stop;
    }

    const jmethodID on_progress = env->GetMethodID(env->GetObjectClass(thiz), "onBuildProgress",
                                                   "(Ljava/lang/String;JJ)V");
    std::string last_stage;
    auto last_report = std::chrono::steady_clock::time_point{};
    const auto progress = [&](const ShaderWarm::Progress& p) {
        // The builder reports every item: the dialog needs a few updates a second.
        const auto now = std::chrono::steady_clock::now();
        const bool last_item = p.total != 0 && p.done == p.total;
        if (on_progress == nullptr || (p.stage == last_stage && !last_item &&
                                       now - last_report < std::chrono::milliseconds{150})) {
            return;
        }
        last_stage = p.stage;
        last_report = now;
        jstring jstage = Common::Android::ToJString(env, p.stage);
        env->CallVoidMethod(thiz, on_progress, jstage, static_cast<jlong>(p.done),
                            static_cast<jlong>(p.total));
        env->DeleteLocalRef(jstage);
    };

    ShaderWarm::Result result;
    if (title_id == 0) {
        result.error = "This game has no title ID";
    } else {
        result = ShaderWarm::BuildWarmFiles(EmulationSession::GetInstance().System(), path,
                                            title_id, progress, stop.get_token());
    }
    {
        std::scoped_lock lock{build_mutex};
        build_stop.reset();
    }
    LOG_INFO(Frontend,
             "Shader precache build for {:016X}: ok {}, {} archives, {} programs, {} skipped{}{}",
             title_id, result.ok, result.archives, result.compute_programs, result.skipped,
             result.error.empty() ? "" : ": ", result.error);

    LoopNative::JsonWriter json;
    json.BeginObject()
        .Key("ok").Bool(result.ok)
        .Key("stopped").Bool(stop.stop_requested())
        .Key("error").String(result.error)
        .Key("archives").Number(static_cast<u64>(result.archives))
        .Key("programs").Number(static_cast<u64>(result.compute_programs))
        .Key("skipped").Number(static_cast<u64>(result.skipped))
        .EndObject();
    return Common::Android::ToJString(env, json.Str());
}

/// Stops the build that runs now after its current items.
void Java_org_yuzu_yuzu_1emu_utils_ShaderPrecache_cancelBuild(JNIEnv*, jobject) {
    std::scoped_lock lock{build_mutex};
    if (build_stop) {
        build_stop->request_stop();
    }
}

jboolean Java_org_yuzu_yuzu_1emu_utils_ShaderPrecache_exists(JNIEnv* env, jobject,
                                                               jstring jprogram_id) {
    return HasPrecache(EmulationSession::GetProgramId(env, jprogram_id));
}

/// Before a game boots: with a precache file and the disk shader cache on, this start precaches
/// before the game runs (the renderer skips what an earlier precache built for this driver).
/// Otherwise the start is as the settings say.
jboolean Java_org_yuzu_yuzu_1emu_utils_ShaderPrecache_requestBeforeStart(JNIEnv* env, jobject,
                                                                           jstring jprogram_id) {
    const u64 title_id = EmulationSession::GetProgramId(env, jprogram_id);
    const bool precache =
        HasPrecache(title_id) && Settings::values.use_disk_shader_cache.GetValue();
    if (precache &&
        Settings::values.precache_shaders.GetValue() == Settings::PrecacheShaders::Off) {
        // The session's value, in the per-game layer: restoreAfterStop gives the setting back.
        Settings::values.precache_shaders.SetGlobal(false);
        Settings::values.precache_shaders.SetValue(Settings::PrecacheShaders::Ask);
    }
    ShaderWarm::RequestWarmBeforeStart(precache);
    if (precache) {
        LOG_INFO(Frontend, "Shader precache before the game starts: {:016X}", title_id);
    }
    return precache;
}

/// After the game stopped: the precache setting follows the global one again.
void Java_org_yuzu_yuzu_1emu_utils_ShaderPrecache_restoreAfterStop(JNIEnv*, jobject) {
    Settings::values.precache_shaders.SetGlobal(true);
    ShaderWarm::RequestWarmBeforeStart(false);
}

} // extern "C"
