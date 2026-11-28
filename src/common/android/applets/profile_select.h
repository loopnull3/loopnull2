// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/frontend/applets/profile_select.h"

namespace Common::Android::ProfileSelect {

/// A game's user selection, asked of the person: a second local player picks their own user.
/// The game is not held while the list is up: the answer comes back through UserSelected.
class AndroidProfileSelect final : public Core::Frontend::ProfileSelectApplet {
public:
    AndroidProfileSelect();
    ~AndroidProfileSelect() override;

    void Close() const override;
    void SelectProfile(SelectProfileCallback callback,
                       const Core::Frontend::ProfileSelectParameters& parameters) const override;
};

/// The person's answer to the list of a request (NativeLibrary.userSelected): the index picked,
/// or -1 when the list was closed.
void UserSelected(int request, int index);

/// Drops the selection that waits for an answer, when what it would complete is going away (the
/// emulation stops): an answer that still comes finds nothing.
void Forget();

} // namespace Common::Android::ProfileSelect
