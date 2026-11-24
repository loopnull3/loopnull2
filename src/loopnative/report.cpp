// SPDX-FileCopyrightText: Copyright 2026 LoopNative contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>

#include "loopnative/json_writer.h"
#include "loopnative/report.h"

namespace LoopNative {

namespace {

void WriteTuning(JsonWriter& json, const DeviceTuning& tuning) {
    json.BeginObject();
    json.Key("device_class").String(ToString(tuning.device_class));
    json.Key("display_name").String(tuning.display_name);
    json.Key("ram_tier").String(ToString(tuning.ram_tier));
    json.Key("usable_ram_bytes").Number(tuning.usable_ram_bytes);
    json.Key("turnip_driver").Bool(tuning.turnip_driver);
    json.Key("recommend_turnip").Bool(tuning.recommend_turnip);
    json.Key("resolution_setup").Number(tuning.resolution_setup);
    json.Key("start_fps_limit").Number(tuning.start_fps_limit);
    json.Key("headroom_step_down").Number(static_cast<double>(tuning.headroom_step_down));
    json.Key("headroom_step_up").Number(static_cast<double>(tuning.headroom_step_up));
    json.Key("cool_seconds").Number(tuning.cool_seconds_before_step_up);
    json.EndObject();
}

} // namespace

std::string ToJson(const DeviceTuning& tuning) {
    JsonWriter json;
    WriteTuning(json, tuning);
    return json.Str();
}

std::string ToJson(const DeviceTuning& tuning, const SessionPlan& plan) {
    JsonWriter json;
    json.BeginObject();
    json.Key("device");
    WriteTuning(json, tuning);
    json.Key("profile");
    if (plan.profile != nullptr) {
        char program_id[17];
        std::snprintf(program_id, sizeof(program_id), "%016llX",
                      static_cast<unsigned long long>(plan.profile->program_id));
        json.BeginObject()
            .Key("program_id").String(program_id)
            .Key("name").String(plan.profile->name)
            .Key("ram_need_gib").Number(static_cast<double>(plan.profile->ram_need_gib))
            .Key("reported_on_8gb").Bool(plan.profile->reported_on_8gb)
            .Key("note").String(plan.profile->note)
            .EndObject();
    } else {
        json.Null();
    }
    json.Key("memory_fit").String(ToString(plan.memory_fit));
    json.Key("storage_backed_memory").Bool(plan.storage_backed_memory);
    json.Key("start_fps_limit").Number(plan.start_fps_limit);
    json.Key("overrides").BeginArray();
    for (const auto& [key, value] : plan.overrides) {
        json.BeginArray().String(key).String(value).EndArray();
    }
    json.EndArray();
    json.EndObject();
    return json.Str();
}

std::string ToJson(const ThermalDecision& decision, MemoryPressure pressure) {
    JsonWriter json;
    json.BeginObject()
        .Key("thermal_level").String(ToString(decision.level))
        .Key("fps_limit").Number(decision.fps_limit)
        .Key("frame_skip").Number(decision.frame_skip)
        .Key("changed").Bool(decision.changed)
        .Key("memory_pressure").String(ToString(pressure))
        .EndObject();
    return json.Str();
}

} // namespace LoopNative
