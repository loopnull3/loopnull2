// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <string>
#include <vector>

#include <jni.h>

#include "common/android/android_common.h"
#include "common/android/applets/profile_select.h"
#include "common/android/id_cache.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/hle/service/acc/profile_manager.h"

namespace Common::Android::ProfileSelect {

namespace {

// What NativeLibrary.selectUser answers besides a user's index.
constexpr jint Cancelled = -1;
constexpr jint NoDialog = -2;

jint AskPerson(const std::vector<std::string>& names) {
    // On a thread of its own: this is called from where no JNI environment can be taken.
    return RunJNIOnFiber<jint>([&names](JNIEnv* env) -> jint {
        const jclass native_library = GetNativeLibraryClass();
        if (env == nullptr || native_library == nullptr) {
            return NoDialog;
        }
        const jmethodID select_user =
            env->GetStaticMethodID(native_library, "selectUser", "([Ljava/lang/String;)I");
        if (select_user == nullptr) {
            env->ExceptionClear();
            return NoDialog;
        }
        const jobjectArray j_names = ToJStringArray(env, names);
        const jint picked = env->CallStaticIntMethod(native_library, select_user, j_names);
        env->DeleteLocalRef(j_names);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return NoDialog;
        }
        return picked;
    });
}

} // Anonymous namespace

AndroidProfileSelect::~AndroidProfileSelect() = default;

void AndroidProfileSelect::Close() const {}

void AndroidProfileSelect::SelectProfile(
    SelectProfileCallback callback,
    const Core::Frontend::ProfileSelectParameters& parameters) const {
    const Service::Account::ProfileManager manager;
    const auto current =
        manager.GetUser(Settings::values.current_user.GetValue()).value_or(Common::UUID{});

    // The users the game does not let be picked (those already playing, for a game that says).
    const auto& invalid = parameters.invalid_uid_list;
    const auto is_excluded = [&invalid](const Common::UUID& uuid) {
        return std::ranges::find(invalid, uuid) != invalid.end();
    };
    const auto excluded =
        std::ranges::count_if(invalid, [](const Common::UUID& uuid) { return uuid.IsValid(); });

    std::vector<Common::UUID> users;
    std::vector<std::string> names;
    for (const Common::UUID& user : manager.GetAllUsers()) {
        if (!user.IsValid() || is_excluded(user)) {
            continue;
        }
        Service::Account::ProfileBase base{};
        std::string name;
        if (manager.GetProfileBase(user, base)) {
            name = Common::StringFromFixedZeroTerminatedBuffer(
                reinterpret_cast<const char*>(base.username.data()), base.username.size());
        }
        if (name.empty()) {
            name = fmt::format("User {}", users.size() + 1);
        }
        users.push_back(user);
        names.push_back(std::move(name));
    }

    const auto describe = [&](std::string_view outcome) {
        LOG_INFO(Service_ACC,
                 "User selection: {}. Mode {}, purpose {}; {} users can be picked, the game "
                 "excludes {}",
                 outcome, static_cast<u32>(parameters.mode), static_cast<u32>(parameters.purpose),
                 users.size(), excluded);
    };

    if (users.empty()) {
        // Nobody is left to pick: the person is told, and the game hears a cancel.
        AskPerson(names);
        describe("nobody is left to pick, a second local player needs a second user (Profile "
                 "Manager): cancelled");
        callback(std::nullopt);
        return;
    }
    if (users.size() == 1) {
        describe("the only user that can be picked");
        callback(users.front());
        return;
    }

    // Several users: the person says who plays, as on the console. A game adding a local player
    // asks here, and it must not be given the user who already plays (Minecraft was, eight times
    // over).
    const jint picked = AskPerson(names);
    if (picked >= 0 && static_cast<size_t>(picked) < users.size()) {
        describe(fmt::format("the person picked user {} of the list", picked + 1));
        callback(users[static_cast<size_t>(picked)]);
        return;
    }
    if (picked == Cancelled) {
        describe("the person closed the list: cancelled");
        callback(std::nullopt);
        return;
    }
    // No dialog could be shown: the current user if it can be picked, else the first that can.
    const auto it = std::ranges::find(users, current);
    describe(it != users.end() ? "no dialog, the current user" : "no dialog, the first user");
    callback(it != users.end() ? *it : users.front());
}

} // namespace Common::Android::ProfileSelect
