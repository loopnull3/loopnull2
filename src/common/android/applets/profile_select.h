// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/frontend/applets/profile_select.h"

namespace Common::Android::ProfileSelect {

/// A game's user selection, asked of the person: a second local player picks their own user.
class AndroidProfileSelect final : public Core::Frontend::ProfileSelectApplet {
public:
    ~AndroidProfileSelect() override;

    void Close() const override;
    void SelectProfile(SelectProfileCallback callback,
                       const Core::Frontend::ProfileSelectParameters& parameters) const override;
};

} // namespace Common::Android::ProfileSelect
