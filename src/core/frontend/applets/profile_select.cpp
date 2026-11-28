// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>

#include "common/logging.h"
#include "common/settings.h"
#include "core/frontend/applets/profile_select.h"
#include "core/hle/service/acc/profile_manager.h"

namespace Core::Frontend {

ProfileSelectApplet::~ProfileSelectApplet() = default;

void DefaultProfileSelectApplet::Close() const {}

void DefaultProfileSelectApplet::SelectProfile(SelectProfileCallback callback,
                                               const ProfileSelectParameters& parameters) const {
    Service::Account::ProfileManager manager;
    auto user = manager.GetUser(Settings::values.current_user.GetValue()).value_or(Common::UUID{});
    // The users the game does not let be picked. A game that adds a local player names the users
    // already playing: Minecraft asked eight times for its second player's user and was given
    // the first player's each time.
    const auto& invalid = parameters.invalid_uid_list;
    const auto is_excluded = [&invalid](const Common::UUID& uuid) {
        return uuid.IsValid() && std::ranges::find(invalid, uuid) != invalid.end();
    };
    const auto excluded =
        std::ranges::count_if(invalid, [](const Common::UUID& uuid) { return uuid.IsValid(); });
    const bool current_excluded = is_excluded(user);
    bool another_user = false;
    if (current_excluded) {
        // With nobody to ask, the first user the game does let be picked stands in.
        for (const Common::UUID& candidate : manager.GetAllUsers()) {
            if (candidate.IsValid() && !is_excluded(candidate)) {
                user = candidate;
                another_user = true;
                break;
            }
        }
    }
    callback(user);
    LOG_INFO(Service_ACC,
             "called, selecting {} instead of prompting... Mode {}, purpose {}; the game "
             "excludes {} users{}",
             another_user ? "another user" : "current user", static_cast<u32>(parameters.mode),
             static_cast<u32>(parameters.purpose), excluded,
             !current_excluded ? ""
             : another_user    ? ", the current user among them"
                               : ", the current user among them, and there is no other user: "
                                 "a second local player needs a second user (Profile Manager)");
}

} // namespace Core::Frontend
