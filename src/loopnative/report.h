// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include "loopnative/device.h"
#include "loopnative/governor.h"
#include "loopnative/title_profiles.h"

namespace LoopNative {

/// The JSON the Android frontend reads (LoopNativeJni): the phone, and one game's start plan.
[[nodiscard]] std::string ToJson(const DeviceTuning& tuning);
[[nodiscard]] std::string ToJson(const DeviceTuning& tuning, const SessionPlan& plan);
[[nodiscard]] std::string ToJson(const ThermalDecision& decision, MemoryPressure pressure);

} // namespace LoopNative
