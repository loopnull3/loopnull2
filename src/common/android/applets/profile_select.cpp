// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <mutex>
#include <optional>
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
#include "core/hle/service/service.h"

namespace Common::Android::ProfileSelect {

namespace {

using Callback = Core::Frontend::ProfileSelectApplet::SelectProfileCallback;

// A selection whose list is up: the answer is the person's to give.
struct Pending {
    int request{};
    Callback callback;
    std::vector<Common::UUID> users;
};

std::mutex state_mutex;
std::optional<Pending> pending;
int last_request{};
// The users this selection gave the game since it started: who is taken to be playing already
// when nobody can be asked.
std::vector<Common::UUID> given;

// Shows the list. False when it did not come up (no activity, or the UI thread does not answer
// in time): then no answer follows.
bool ShowList(const std::vector<std::string>& names, int request) {
    // On a thread of its own: this is called from where no JNI environment can be taken.
    return RunJNIOnFiber<bool>([&names, request](JNIEnv* env) -> bool {
        const jclass native_library = GetNativeLibraryClass();
        if (env == nullptr || native_library == nullptr) {
            return false;
        }
        const jmethodID select_user =
            env->GetStaticMethodID(native_library, "selectUser", "([Ljava/lang/String;I)Z");
        if (select_user == nullptr) {
            env->ExceptionClear();
            return false;
        }
        const jobjectArray j_names = ToJStringArray(env, names);
        const jboolean shown = env->CallStaticBooleanMethod(native_library, select_user, j_names,
                                                           static_cast<jint>(request));
        env->DeleteLocalRef(j_names);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        return shown != JNI_FALSE;
    });
}

void Give(const Callback& callback, const Common::UUID& user) {
    {
        std::scoped_lock lock{state_mutex};
        if (std::ranges::find(given, user) == given.end()) {
            given.push_back(user);
        }
    }
    // What the game does with the user it was given goes to the log for a while.
    Service::TraceNextRequests(300);
    callback(user);
}

} // Anonymous namespace

AndroidProfileSelect::AndroidProfileSelect() {
    std::scoped_lock lock{state_mutex};
    pending.reset();
    given.clear();
}

AndroidProfileSelect::~AndroidProfileSelect() {
    std::scoped_lock lock{state_mutex};
    pending.reset();
}

void AndroidProfileSelect::Close() const {
    // The game took its request back: an answer that still comes finds nothing waiting.
    std::scoped_lock lock{state_mutex};
    pending.reset();
}

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

    if (parameters.mode != Service::AM::Frontend::UiMode::UserSelector) {
        // Not a selection (an account to make or to link, a name or an icon to edit): nothing
        // here does those, and the answer stays what it was, the current user.
        describe("not a selection, the current user");
        callback(current);
        return;
    }
    if (users.empty()) {
        // Nobody is left to pick: the person is told, and the game hears a cancel.
        ShowList(names, 0);
        describe("nobody is left to pick, a second local player needs a second user (Profile "
                 "Manager): cancelled");
        callback(std::nullopt);
        return;
    }
    if (users.size() == 1) {
        describe("the only user that can be picked");
        Give(callback, users.front());
        return;
    }

    // Several users: the person says who plays, as on the console. A game adding a local player
    // asks here, and it must not be given the user who already plays (Minecraft was, eight times
    // over). The game is not held meanwhile: its request returns and the answer completes it.
    int request{};
    {
        std::scoped_lock lock{state_mutex};
        request = ++last_request;
        pending = Pending{request, callback, users};
    }
    describe(fmt::format("the person is asked (request {})", request));
    if (ShowList(names, request)) {
        return;
    }

    // No list came up, so nobody can be asked. The user the game was started with, as before,
    // unless this selection gave it already: then the first user not given yet. (A game that
    // names the playing users has them out of the list, and gets another at once.)
    Common::UUID pick = users.front();
    {
        std::scoped_lock lock{state_mutex};
        if (!pending || pending->request != request) {
            return;
        }
        pending.reset();
        const auto is_given = [](const Common::UUID& user) {
            return std::ranges::find(given, user) != given.end();
        };
        const auto fresh =
            std::ranges::find_if(users, [&](const Common::UUID& user) { return !is_given(user); });
        if (std::ranges::find(users, current) != users.end() && !is_given(current)) {
            pick = current;
        } else if (fresh != users.end()) {
            pick = *fresh;
        }
    }
    const auto index = std::ranges::find(users, pick) - users.begin();
    describe(fmt::format("no list could be shown: user {} of the list", index + 1));
    Give(callback, pick);
}

void UserSelected(int request, int index) {
    std::optional<Pending> answered;
    {
        std::scoped_lock lock{state_mutex};
        if (!pending || pending->request != request) {
            LOG_INFO(Service_ACC, "User selection: an answer to request {} that nothing waits for",
                     request);
            return;
        }
        answered = std::move(pending);
        pending.reset();
    }
    if (index >= 0 && static_cast<size_t>(index) < answered->users.size()) {
        LOG_INFO(Service_ACC, "User selection: the person picked user {} of the list (request {})",
                 index + 1, request);
        Give(answered->callback, answered->users[static_cast<size_t>(index)]);
        return;
    }
    LOG_INFO(Service_ACC, "User selection: the person closed the list (request {}): cancelled",
             request);
    answered->callback(std::nullopt);
}

} // namespace Common::Android::ProfileSelect
