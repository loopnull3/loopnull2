// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <common/fs/fs.h>
#include <common/fs/path_util.h>
#include <common/settings.h>
#include <common/string_util.h>
#include <hid_core/hid_types.h>
#include <jni.h>

#include "android_config.h"
#include "common/android/android_common.h"
#include "common/android/id_cache.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "input_common/drivers/android.h"
#include "input_common/drivers/touch_screen.h"
#include "input_common/drivers/virtual_amiibo.h"
#include "input_common/drivers/virtual_gamepad.h"
#include "native.h"

::Common::unordered_map<std::string, std::unique_ptr<AndroidConfig>> map_profiles;

bool IsHandheldOnly() {
    const auto npad_style_set =
        EmulationSession::GetInstance().System().HIDCore().GetSupportedStyleTag();

    if (npad_style_set.fullkey == 1) {
        return false;
    }

    if (npad_style_set.handheld == 0) {
        return false;
    }

    return !Settings::IsDockedMode();
}

std::filesystem::path GetNameWithoutExtension(std::filesystem::path filename) {
    return filename.replace_extension();
}

bool IsProfileNameValid(std::string_view profile_name) {
    return profile_name.find_first_of("<>:;\"/\\|,.!?*") == std::string::npos;
}

bool ProfileExistsInFilesystem(std::string_view profile_name) {
    return Common::FS::Exists(Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "input" /
                              fmt::format("{}.ini", profile_name));
}

bool ProfileExistsInMap(const std::string& profile_name) {
    return map_profiles.find(profile_name) != map_profiles.end();
}

bool SaveProfile(const std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    map_profiles[profile_name]->SaveAndroidControlPlayerValues(player_index);
    return true;
}

bool LoadProfile(std::string& profile_name, std::size_t player_index) {
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name)) {
        map_profiles.erase(profile_name);
        return false;
    }

    LOG_INFO(Config, "Loading input profile `{}`", profile_name);

    Settings::values.players.GetValue()[player_index].profile_name = profile_name;
    map_profiles[profile_name]->ReadAndroidControlPlayerValues(player_index);
    return true;
}

void ApplyControllerConfig(size_t player_index,
                           const std::function<void(Core::HID::EmulatedController*)>& apply) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    if (player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        handheld->EnableConfiguration();
        player_one->EnableConfiguration();
        apply(handheld);
        apply(player_one);
        handheld->DisableConfiguration();
        player_one->DisableConfiguration();
        handheld->SaveCurrentConfig();
        player_one->SaveCurrentConfig();
    } else {
        auto* controller = hid_core.GetEmulatedControllerByIndex(player_index);
        controller->EnableConfiguration();
        apply(controller);
        controller->DisableConfiguration();
        controller->SaveCurrentConfig();
    }
}

std::vector<s32> GetSupportedStyles(int player_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    const auto npad_style_set = hid_core.GetSupportedStyleTag();
    std::vector<s32> supported_indexes;
    if (npad_style_set.fullkey == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::Fullkey));
    }

    if (npad_style_set.joycon_dual == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconDual));
    }

    if (npad_style_set.joycon_left == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconLeft));
    }

    if (npad_style_set.joycon_right == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::JoyconRight));
    }

    if (player_index == 0 && npad_style_set.handheld == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::Handheld));
    }

    if (npad_style_set.gamecube == 1) {
        supported_indexes.push_back(static_cast<s32>(Core::HID::NpadStyleIndex::GameCube));
    }

    return supported_indexes;
}

void ConnectController(size_t player_index, bool connected) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    ApplyControllerConfig(player_index, [&](Core::HID::EmulatedController* controller) {
        auto supported_styles = GetSupportedStyles(player_index);
        auto controller_style = controller->GetNpadStyleIndex(true);
        auto style = std::find(supported_styles.begin(), supported_styles.end(),
                               static_cast<int>(controller_style));
        if (style == supported_styles.end() && !supported_styles.empty()) {
            controller->SetNpadStyleIndex(
                static_cast<Core::HID::NpadStyleIndex>(supported_styles[0]));
        }
    });

    if (player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        handheld->EnableConfiguration();
        player_one->EnableConfiguration();
        if (player_one->GetNpadStyleIndex(true) == Core::HID::NpadStyleIndex::Handheld) {
            if (connected) {
                handheld->Connect();
            } else {
                handheld->Disconnect();
            }
            player_one->Disconnect();
        } else {
            if (connected) {
                player_one->Connect();
            } else {
                player_one->Disconnect();
            }
            handheld->Disconnect();
        }
        handheld->DisableConfiguration();
        player_one->DisableConfiguration();
        handheld->SaveCurrentConfig();
        player_one->SaveCurrentConfig();
    } else {
        auto* controller = hid_core.GetEmulatedControllerByIndex(player_index);
        controller->EnableConfiguration();
        if (connected) {
            controller->Connect();
        } else {
            controller->Disconnect();
        }
        controller->DisableConfiguration();
        controller->SaveCurrentConfig();
    }
}

namespace {

// hid_core's order: player 1 to 8, then the handheld controller
constexpr size_t NUM_PLAYERS = 8;
constexpr size_t HANDHELD_PLAYER = 8;
// The touch overlay's port (InputHandler)
constexpr int OVERLAY_PORT = 100;

// InputHandler's GUID is the product ID then the vendor ID, 16 hex digits each
constexpr std::string_view JOYCON_LEFT_GUID = "0000000000002006000000000000057e";
constexpr std::string_view JOYCON_RIGHT_GUID = "0000000000002007000000000000057e";

/// An Android controller as a mapping names it: its model and its port
struct AndroidPad {
    std::string guid;
    int port{};

    bool operator==(const AndroidPad&) const = default;
};

/// A connected controller, as InputHandler listed it last
struct ConnectedPad {
    AndroidPad pad;
    /// Its descriptor, which tells two controllers of one model apart: the log gets its start only
    std::string key;
    std::string name;

    bool operator==(const ConnectedPad&) const = default;
};

std::mutex pads_mutex;
std::vector<ConnectedPad> connected_pads;
/// The port every controller seen had last, by key
std::vector<std::pair<std::string, int>> last_ports;
/// Controllers the log has a line for already, about a press that made them no player
std::vector<AndroidPad> refused_pads;

bool Contains(const std::vector<AndroidPad>& pads, const AndroidPad& pad) {
    return std::find(pads.begin(), pads.end(), pad) != pads.end();
}

/// Adds the Android controller a parameter names, if it names one
void AddPad(std::vector<AndroidPad>& pads, const Common::ParamPackage& param) {
    const std::string engine = param.Get("engine", "");
    if (engine == "analog_from_button") {
        // A stick made of buttons: each direction is a parameter of its own
        for (const char* direction : {"up", "down", "left", "right", "modifier"}) {
            const std::string button = param.Get(direction, "");
            if (!button.empty()) {
                AddPad(pads, Common::ParamPackage(button));
            }
        }
        return;
    }
    if (engine != "android" || !param.Has("guid") || param.Get("port", -1) < 0) {
        return;
    }
    AndroidPad pad{Common::ToLower(param.Get("guid", "")), param.Get("port", 0)};
    if (!Contains(pads, pad)) {
        pads.push_back(std::move(pad));
    }
}

/// The Android controllers a player's mapping names, as the player has it now
std::vector<AndroidPad> MappedPads(const Core::HID::EmulatedController& controller) {
    std::vector<AndroidPad> pads;
    for (int id = 0; id < Settings::NativeButton::NumButtons; ++id) {
        AddPad(pads, controller.GetButtonParam(static_cast<size_t>(id)));
    }
    for (int id = 0; id < Settings::NativeAnalog::NumAnalogs; ++id) {
        AddPad(pads, controller.GetStickParam(static_cast<size_t>(id)));
    }
    return pads;
}

/// The same, as the settings have it
std::vector<AndroidPad> SavedPads(size_t player_index) {
    std::vector<AndroidPad> pads;
    const auto& player = Settings::values.players.GetValue()[player_index];
    for (const auto& button : player.buttons) {
        if (!button.empty()) {
            AddPad(pads, Common::ParamPackage(button));
        }
    }
    for (const auto& analog : player.analogs) {
        if (!analog.empty()) {
            AddPad(pads, Common::ParamPackage(analog));
        }
    }
    return pads;
}

/// The Android controllers a player's mapping names. In a game that is what the player has now,
/// which a press may have changed. With no game running it is what the settings have: the same,
/// and the players themselves are not read then, since a starting game reloads them from the
/// settings on its own threads.
std::vector<AndroidPad> PlayerPads(size_t player_index) {
    auto& session = EmulationSession::GetInstance();
    if (!session.IsRunning()) {
        return SavedPads(player_index);
    }
    return MappedPads(*session.System().HIDCore().GetEmulatedControllerByIndex(player_index));
}

bool NamesPad(const Core::HID::EmulatedController& controller, const AndroidPad& pad) {
    std::vector<AndroidPad> pads;
    for (int id = 0; id < Settings::NativeButton::NumButtons; ++id) {
        AddPad(pads, controller.GetButtonParam(static_cast<size_t>(id)));
        if (Contains(pads, pad)) {
            return true;
        }
    }
    for (int id = 0; id < Settings::NativeAnalog::NumAnalogs; ++id) {
        AddPad(pads, controller.GetStickParam(static_cast<size_t>(id)));
    }
    return Contains(pads, pad);
}

bool HasMapping(const Core::HID::EmulatedController& controller) {
    for (int id = 0; id < Settings::NativeButton::NumButtons; ++id) {
        if (controller.GetButtonParam(static_cast<size_t>(id)).Has("engine")) {
            return true;
        }
    }
    for (int id = 0; id < Settings::NativeAnalog::NumAnalogs; ++id) {
        if (controller.GetStickParam(static_cast<size_t>(id)).Has("engine")) {
            return true;
        }
    }
    return false;
}

bool IsJoyconHalf(const std::string& guid) {
    return guid == JOYCON_LEFT_GUID || guid == JOYCON_RIGHT_GUID;
}

/// "057e:2009" (vendor:product) from a GUID
std::string ModelText(const std::string& guid) {
    if (guid.size() != 32) {
        return "?";
    }
    return fmt::format("{}:{}", guid.substr(28, 4), guid.substr(12, 4));
}

std::string PlayerText(size_t index) {
    return index == HANDHELD_PLAYER ? std::string{"Handheld"} : fmt::format("Player {}", index + 1);
}

const char* StyleText(Core::HID::NpadStyleIndex style) {
    switch (style) {
    case Core::HID::NpadStyleIndex::None:
        return "no controller type";
    case Core::HID::NpadStyleIndex::Fullkey:
        return "Pro Controller";
    case Core::HID::NpadStyleIndex::Handheld:
        return "Handheld";
    case Core::HID::NpadStyleIndex::JoyconDual:
        return "Dual Joycons";
    case Core::HID::NpadStyleIndex::JoyconLeft:
        return "Left Joycon";
    case Core::HID::NpadStyleIndex::JoyconRight:
        return "Right Joycon";
    case Core::HID::NpadStyleIndex::GameCube:
        return "GameCube Controller";
    default:
        return "another controller type";
    }
}

std::string StylesText(Core::HID::NpadStyleTag styles) {
    if (styles.raw == Core::HID::NpadStyleSet::All) {
        return "any controller type (no game has said)";
    }
    std::string text;
    const auto add = [&text](bool taken, std::string_view name) {
        if (taken) {
            text += text.empty() ? "" : ", ";
            text += name;
        }
    };
    add(styles.fullkey != 0, "Pro Controller");
    add(styles.handheld != 0, "Handheld");
    add(styles.joycon_dual != 0, "Dual Joycons");
    add(styles.joycon_left != 0, "Left Joycon");
    add(styles.joycon_right != 0, "Right Joycon");
    add(styles.gamecube != 0, "GameCube Controller");
    return text.empty() ? std::string{"none of the controller types of Settings"} : text;
}

/// The connected controllers, each with the players it drives and how, then the players that are
/// connected or have a mapping. Called with pads_mutex held.
void LogControllers(std::string_view reason) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    std::array<std::vector<AndroidPad>, HANDHELD_PLAYER + 1> mapped;
    for (size_t index = 0; index <= HANDHELD_PLAYER; ++index) {
        mapped[index] = PlayerPads(index);
    }

    LOG_INFO(Input, "Controllers ({}): {} connected, {} mode, the game takes {}", reason,
             connected_pads.size(), Settings::IsDockedMode() ? "docked" : "handheld",
             StylesText(hid_core.GetSupportedStyleTag()));
    for (const auto& connected : connected_pads) {
        std::string players;
        for (size_t index = 0; index < NUM_PLAYERS; ++index) {
            if (!Contains(mapped[index], connected.pad)) {
                continue;
            }
            players += players.empty() ? "" : "; ";
            players += fmt::format("{}, {}", PlayerText(index),
                                   Contains(SavedPads(index), connected.pad)
                                       ? "saved mapping"
                                       : "default mapping of this session (not saved)");
            if (mapped[index].size() > 1) {
                players +=
                    fmt::format(", dual mapping (one of {} controllers)", mapped[index].size());
            }
        }
        if (players.empty()) {
            players = IsJoyconHalf(connected.pad.guid)
                          ? "no player (one Joy-Con of a pair: Auto-map two Joy-Cons maps both)"
                          : "no player (a button press on it makes it the next free player)";
        }
        LOG_INFO(Input, "Controller {} [{}, port {}, id {}]: {}", connected.name,
                 ModelText(connected.pad.guid), connected.pad.port, connected.key.substr(0, 8),
                 players);
    }
    for (size_t index = 0; index <= HANDHELD_PLAYER; ++index) {
        const auto* controller = hid_core.GetEmulatedControllerByIndex(index);
        const bool is_connected = controller->IsConnected();
        if (!is_connected && (index == HANDHELD_PLAYER || mapped[index].empty())) {
            continue;
        }
        std::string mapping;
        for (const auto& pad : mapped[index]) {
            const bool present =
                std::any_of(connected_pads.begin(), connected_pads.end(),
                            [&pad](const ConnectedPad& connected) { return connected.pad == pad; });
            mapping +=
                fmt::format("{}{} port {}{}", mapping.empty() ? "mapping names " : ", ",
                            ModelText(pad.guid), pad.port, present ? "" : " (not connected)");
        }
        LOG_INFO(Input, "{}: {}, {}, {}", PlayerText(index),
                 is_connected ? "connected" : "not connected",
                 StyleText(controller->GetNpadStyleIndex()),
                 mapping.empty() ? std::string{"no controller in its mapping"} : mapping);
    }
}

/// Gives every connected controller its port. A mapping names a controller by its model and its
/// port, so a controller that a mapping names sits where the mapping says, whatever else is
/// connected. (The port used to be the controller's place among the connected ones: connecting
/// another controller could move one off its mapping.) In order:
/// - the port it had last, while a mapping names it there;
/// - its place in the list, when a mapping names its model there (how ports were given before);
/// - the first port a mapping names for its model that no connected controller of the model has;
/// - in no mapping: the number it had last, else the lowest one that no connected controller has
///   and no mapping names.
/// `named` is what the mappings name, in the order of the players. Called with pads_mutex held.
void GivePorts(std::vector<ConnectedPad>& pads, const std::vector<AndroidPad>& named) {
    const size_t count = pads.size();
    std::vector<bool> done(count, false);
    const auto taken = [&](const AndroidPad& pad) {
        for (size_t i = 0; i < count; ++i) {
            if (done[i] && pads[i].pad == pad) {
                return true;
            }
        }
        return false;
    };
    const auto number_free = [&](int port) {
        if (port == OVERLAY_PORT) {
            return false;
        }
        for (size_t i = 0; i < count; ++i) {
            if (done[i] && pads[i].pad.port == port) {
                return false;
            }
        }
        return true;
    };
    const auto last_port = [](const std::string& key) {
        for (const auto& [last_key, port] : last_ports) {
            if (last_key == key) {
                return port;
            }
        }
        return -1;
    };
    const auto give = [&](size_t i, int port) {
        pads[i].pad.port = port;
        done[i] = true;
    };

    for (size_t i = 0; i < count; ++i) {
        const AndroidPad last{pads[i].pad.guid, last_port(pads[i].key)};
        if (last.port >= 0 && Contains(named, last) && !taken(last)) {
            give(i, last.port);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const AndroidPad in_place{pads[i].pad.guid, static_cast<int>(i)};
        if (!done[i] && Contains(named, in_place) && !taken(in_place)) {
            give(i, in_place.port);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (done[i]) {
            continue;
        }
        for (const auto& pad : named) {
            if (pad.guid == pads[i].pad.guid && !taken(pad)) {
                give(i, pad.port);
                break;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const int last = last_port(pads[i].key);
        if (!done[i] && last >= 0 && number_free(last)) {
            give(i, last);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (done[i]) {
            continue;
        }
        int port = 0;
        while (!number_free(port) ||
               std::any_of(named.begin(), named.end(),
                           [port](const AndroidPad& pad) { return pad.port == port; })) {
            ++port;
        }
        give(i, port);
    }

    for (const auto& pad : pads) {
        const auto last =
            std::find_if(last_ports.begin(), last_ports.end(),
                         [&pad](const auto& entry) { return entry.first == pad.key; });
        if (last != last_ports.end()) {
            last->second = pad.pad.port;
        } else {
            last_ports.emplace_back(pad.key, pad.pad.port);
        }
    }
}

/// GivePorts with what the mappings name: each player's as it is now, then as the settings have it
void ResolvePorts(std::vector<ConnectedPad>& pads) {
    std::vector<AndroidPad> named;
    for (size_t index = 0; index <= HANDHELD_PLAYER; ++index) {
        for (auto& pad : PlayerPads(index)) {
            if (!Contains(named, pad)) {
                named.push_back(std::move(pad));
            }
        }
    }
    for (size_t index = 0; index <= HANDHELD_PLAYER; ++index) {
        for (auto& pad : SavedPads(index)) {
            if (!Contains(named, pad)) {
                named.push_back(std::move(pad));
            }
        }
    }
    GivePorts(pads, named);
}

/// A controller that no player's mapping names becomes the next free player when a button is
/// pressed on it, with its own default mapping (what Auto-map gives): player 1 when player 1 has
/// no mapping, else the first player that is not connected. Nothing is saved, so the settings keep
/// what the user set; reloading the input devices (ReloadFromSettings: when emulation starts and
/// when the settings close) undoes it until the next press, which is why it is done at a press.
/// Returns the player's index, or -1 when the press made no player. Called with pads_mutex held.
int JoinUnmappedController(const AndroidPad& pad) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    // Why a press makes no player is said once for a controller
    const auto first_refusal = [&pad] {
        if (Contains(refused_pads, pad)) {
            return false;
        }
        refused_pads.push_back(pad);
        return true;
    };
    for (size_t index = 0; index <= HANDHELD_PLAYER; ++index) {
        const auto* controller = hid_core.GetEmulatedControllerByIndex(index);
        if (!NamesPad(*controller, pad)) {
            continue;
        }
        // Player 1 may be on the handheld controller instead, so only players 2 to 8 are checked
        if (index > 0 && index < NUM_PLAYERS && !controller->IsConnected() && first_refusal()) {
            LOG_INFO(Input,
                     "A button was pressed on a controller [{}, port {}] that the mapping of {} "
                     "names, and {} is not connected (by its setting, or the game disconnected "
                     "it): nothing done",
                     ModelText(pad.guid), pad.port, PlayerText(index), PlayerText(index));
        }
        return -1;
    }
    if (IsJoyconHalf(pad.guid)) {
        if (first_refusal()) {
            LOG_INFO(Input,
                     "A button was pressed on a Joy-Con [{}, port {}] that is in no mapping. One "
                     "Joy-Con is half a controller and is not made a player: Settings > Controls "
                     "> a player > Auto-map two Joy-Cons maps the pair",
                     ModelText(pad.guid), pad.port);
        }
        return -1;
    }

    auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
    auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    size_t slot = NUM_PLAYERS;
    if (!HasMapping(*player_one)) {
        slot = 0;
    } else {
        for (size_t index = 1; index < NUM_PLAYERS; ++index) {
            if (!hid_core.GetEmulatedControllerByIndex(index)->IsConnected()) {
                slot = index;
                break;
            }
        }
    }
    if (slot == NUM_PLAYERS) {
        if (first_refusal()) {
            LOG_INFO(Input,
                     "A button was pressed on a controller [{}, port {}] that is in no mapping, "
                     "and every player is connected: not made a player",
                     ModelText(pad.guid), pad.port);
        }
        return -1;
    }

    // Only a controller the engine knows may be asked for its default mapping
    auto& input_subsystem = EmulationSession::GetInstance().GetInputSubsystem();
    Common::ParamPackage device;
    for (const auto& registered : input_subsystem.GetInputDevices()) {
        if (registered.Get("engine", "") == "android" && registered.Get("port", -1) == pad.port &&
            Common::ToLower(registered.Get("guid", "")) == pad.guid) {
            device = registered;
            break;
        }
    }
    if (!device.Has("engine")) {
        return -1;
    }
    const std::string name = device.Get("display", "");
    const auto button_mapping = input_subsystem.GetButtonMappingForDevice(device);
    const auto analog_mapping = input_subsystem.GetAnalogMappingForDevice(device);
    if (!button_mapping.contains(Settings::NativeButton::A) &&
        !button_mapping.contains(Settings::NativeButton::B)) {
        if (first_refusal()) {
            LOG_INFO(Input,
                     "A button was pressed on {} [{}, port {}], which is in no mapping. Its "
                     "default mapping (buttons: {}, sticks: {}) has no A or B: not made a player",
                     name, ModelText(pad.guid), pad.port, button_mapping.size(),
                     analog_mapping.size());
        }
        return -1;
    }

    std::array<Common::ParamPackage, Settings::NativeButton::NumButtons> buttons{};
    std::array<Common::ParamPackage, Settings::NativeAnalog::NumAnalogs> sticks{};
    for (const auto& [id, param] : button_mapping) {
        buttons[static_cast<size_t>(id)] = param;
        buttons[static_cast<size_t>(id)].Set("display", name);
    }
    for (const auto& [id, param] : analog_mapping) {
        sticks[static_cast<size_t>(id)] = param;
        sticks[static_cast<size_t>(id)].Set("display", name);
    }

    // Player 1's mapping is the handheld controller's too, as in ApplyControllerConfig
    std::vector<Core::HID::EmulatedController*> targets;
    auto* joined = hid_core.GetEmulatedControllerByIndex(slot);
    if (slot == 0) {
        targets = {handheld, player_one};
        if (player_one->GetNpadStyleIndex() == Core::HID::NpadStyleIndex::Handheld) {
            joined = handheld;
        }
    } else {
        targets = {joined};
    }
    const auto is_connected = [&targets] {
        return std::any_of(targets.begin(), targets.end(),
                           [](const auto* controller) { return controller->IsConnected(); });
    };
    if (!is_connected()) {
        // The controller type its settings give it, or the first one the game takes
        const auto styles = GetSupportedStyles(static_cast<int>(slot));
        if (!styles.empty() &&
            std::find(styles.begin(), styles.end(),
                      static_cast<s32>(joined->GetNpadStyleIndex())) == styles.end()) {
            joined->SetNpadStyleIndex(static_cast<Core::HID::NpadStyleIndex>(styles[0]));
        }
        joined->Connect();
        if (!is_connected()) {
            if (first_refusal()) {
                LOG_INFO(Input,
                         "A button was pressed on {} [{}, port {}], which is in no mapping, but "
                         "{} could not be connected as {}: the game takes {}",
                         name, ModelText(pad.guid), pad.port, PlayerText(slot),
                         StyleText(joined->GetNpadStyleIndex()),
                         StylesText(hid_core.GetSupportedStyleTag()));
            }
            return -1;
        }
    }
    for (auto* controller : targets) {
        for (size_t id = 0; id < buttons.size(); ++id) {
            controller->SetButtonParam(id, buttons[id]);
        }
        for (size_t id = 0; id < sticks.size(); ++id) {
            controller->SetStickParam(id, sticks[id]);
        }
    }

    LOG_INFO(Input,
             "A button was pressed on {} [{}, port {}], which is in no mapping: it is now {} "
             "with its default mapping (buttons: {}, sticks: {}), {}. Not saved: Settings > "
             "Controls gives a player its controller for good",
             name, ModelText(pad.guid), pad.port, PlayerText(slot), button_mapping.size(),
             analog_mapping.size(), StyleText(joined->GetNpadStyleIndex()));
    LogControllers("a controller became a player");
    return static_cast<int>(slot);
}

} // namespace

extern "C" {

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_isHandheldOnly(JNIEnv* env,
                                                                           jobject j_obj) {
    return IsHandheldOnly();
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onGamePadButtonEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jint j_button_id, jint j_action) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetButtonState(
        Common::Android::GetJString(env, j_guid), j_port, j_button_id, j_action != 0);
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onGamePadAxisEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jint j_stick_id, jfloat j_value) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetAxisPosition(
        Common::Android::GetJString(env, j_guid), j_port, j_stick_id, j_value);
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onGamePadMotionEvent(
    JNIEnv* env, jobject j_obj, jstring j_guid, jint j_port, jlong j_delta_timestamp,
    jfloat j_x_gyro, jfloat j_y_gyro, jfloat j_z_gyro, jfloat j_x_accel, jfloat j_y_accel,
    jfloat j_z_accel) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->SetMotionState(
        Common::Android::GetJString(env, j_guid), j_port, j_delta_timestamp, j_x_gyro, j_y_gyro,
        j_z_gyro, j_x_accel, j_y_accel, j_z_accel);
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onReadNfcTag(JNIEnv* env, jobject j_obj,
                                                                     jbyteArray j_data) {
    jboolean isCopy{false};
    std::span<u8> data(reinterpret_cast<u8*>(env->GetByteArrayElements(j_data, &isCopy)),
                       static_cast<size_t>(env->GetArrayLength(j_data)));

    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualAmiibo()->LoadAmiibo(data);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onRemoveNfcTag(JNIEnv* env, jobject j_obj) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualAmiibo()->CloseAmiibo();
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onTouchPressed(JNIEnv* env, jobject j_obj,
                                                                       jint j_id, jfloat j_x_axis,
                                                                       jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchPressed(j_id, j_x_axis, j_y_axis);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onTouchMoved(JNIEnv* env, jobject j_obj,
                                                                     jint j_id, jfloat j_x_axis,
                                                                     jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchMoved(j_id, j_x_axis, j_y_axis);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onTouchReleased(JNIEnv* env, jobject j_obj,
                                                                        jint j_id) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().Window().OnTouchReleased(j_id);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onOverlayButtonEventImpl(
    JNIEnv* env, jobject j_obj, jint j_port, jint j_button_id, jint j_action) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetButtonState(
            j_port, j_button_id, j_action == 1);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onOverlayJoystickEventImpl(
    JNIEnv* env, jobject j_obj, jint j_port, jint j_stick_id, jfloat j_x_axis, jfloat j_y_axis) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetStickPosition(
            j_port, j_stick_id, j_x_axis, j_y_axis);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_onDeviceMotionEvent(
    JNIEnv* env, jobject j_obj, jint j_port, jlong j_delta_timestamp, jfloat j_x_gyro,
    jfloat j_y_gyro, jfloat j_z_gyro, jfloat j_x_accel, jfloat j_y_accel, jfloat j_z_accel) {
    if (EmulationSession::GetInstance().IsRunning()) {
        EmulationSession::GetInstance().GetInputSubsystem().GetVirtualGamepad()->SetMotionState(
            j_port, j_delta_timestamp, j_x_gyro, j_y_gyro, j_z_gyro, j_x_accel, j_y_accel,
            j_z_accel);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_reloadInputDevices(JNIEnv* env,
                                                                           jobject j_obj) {
    EmulationSession::GetInstance().System().HIDCore().ReloadInputDevices();
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_registerController(JNIEnv* env,
                                                                           jobject j_obj,
                                                                           jobject j_device) {
    EmulationSession::GetInstance().GetInputSubsystem().GetAndroid()->RegisterController(j_device);
}

jintArray Java_org_yuzu_yuzu_1emu_features_input_NativeInput_resolvePorts(
    JNIEnv* env, jobject j_obj, jobjectArray j_guids, jobjectArray j_descriptors,
    jobjectArray j_names) {
    const auto element = [env](jobjectArray j_array, jsize index) {
        const auto j_string = static_cast<jstring>(env->GetObjectArrayElement(j_array, index));
        std::string text = Common::Android::GetJString(env, j_string);
        env->DeleteLocalRef(j_string);
        return text;
    };
    const jsize count =
        (std::min)({env->GetArrayLength(j_guids), env->GetArrayLength(j_descriptors),
                    env->GetArrayLength(j_names)});
    std::vector<ConnectedPad> pads(static_cast<size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto& pad = pads[static_cast<size_t>(i)];
        pad.pad.guid = Common::ToLower(element(j_guids, i));
        pad.key = element(j_descriptors, i);
        pad.name = element(j_names, i);
    }

    std::vector<jint> ports;
    {
        std::scoped_lock lock{pads_mutex};
        ResolvePorts(pads);
        for (const auto& pad : pads) {
            ports.push_back(pad.pad.port);
        }
        if (pads != connected_pads) {
            connected_pads = std::move(pads);
            LogControllers("the connected controllers changed");
        }
    }
    jintArray j_ports = env->NewIntArray(count);
    env->SetIntArrayRegion(j_ports, 0, count, ports.data());
    return j_ports;
}

jint Java_org_yuzu_yuzu_1emu_features_input_NativeInput_joinUnmappedController(JNIEnv* env,
                                                                               jobject j_obj,
                                                                               jstring j_guid,
                                                                               jint j_port) {
    if (!EmulationSession::GetInstance().IsRunning()) {
        return -1;
    }
    std::scoped_lock lock{pads_mutex};
    return JoinUnmappedController(
        AndroidPad{Common::ToLower(Common::Android::GetJString(env, j_guid)), j_port});
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_logControllers(JNIEnv* env, jobject j_obj,
                                                                       jstring j_reason) {
    std::scoped_lock lock{pads_mutex};
    LogControllers(Common::Android::GetJString(env, j_reason));
}

jobjectArray Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getInputDevices(JNIEnv* env,
                                                                                jobject j_obj) {
    auto devices = EmulationSession::GetInstance().GetInputSubsystem().GetInputDevices();
    jobjectArray jdevices = env->NewObjectArray(devices.size(), Common::Android::GetStringClass(),
                                                Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < devices.size(); ++i) {
        env->SetObjectArrayElement(jdevices, i,
                                   Common::Android::ToJString(env, devices[i].Serialize()));
    }
    return jdevices;
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_loadInputProfiles(JNIEnv* env,
                                                                          jobject j_obj) {
    map_profiles.clear();
    const auto input_profile_loc =
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "input";

    if (Common::FS::IsDir(input_profile_loc)) {
        Common::FS::IterateDirEntries(
            input_profile_loc,
            [&](const std::filesystem::path& full_path) {
                const auto filename = full_path.filename();
                const auto name_without_ext =
                    Common::FS::PathToUTF8String(GetNameWithoutExtension(filename));

                if (filename.extension() == ".ini" && IsProfileNameValid(name_without_ext)) {
                    map_profiles.insert_or_assign(
                        name_without_ext, std::make_unique<AndroidConfig>(
                                              name_without_ext, Config::ConfigType::InputProfile));
                }

                return true;
            },
            Common::FS::DirEntryFilter::File);
    }
}

jobjectArray Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getInputProfileNames(
    JNIEnv* env, jobject j_obj) {
    std::vector<std::string> profile_names;
    profile_names.reserve(map_profiles.size());

    auto it = map_profiles.cbegin();
    while (it != map_profiles.cend()) {
        const auto& [profile_name, config] = *it;
        if (!ProfileExistsInFilesystem(profile_name)) {
            it = map_profiles.erase(it);
            continue;
        }

        profile_names.push_back(profile_name);
        ++it;
    }

    std::stable_sort(profile_names.begin(), profile_names.end());

    jobjectArray j_profile_names =
        env->NewObjectArray(profile_names.size(), Common::Android::GetStringClass(),
                            Common::Android::ToJString(env, ""));
    for (size_t i = 0; i < profile_names.size(); ++i) {
        env->SetObjectArrayElement(j_profile_names, i,
                                   Common::Android::ToJString(env, profile_names[i]));
    }

    return j_profile_names;
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_isProfileNameValid(JNIEnv* env,
                                                                               jobject j_obj,
                                                                               jstring j_name) {
    return Common::Android::GetJString(env, j_name).find_first_of("<>:;\"/\\|,.!?*") ==
           std::string::npos;
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_createProfile(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_name,
                                                                          jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    if (ProfileExistsInMap(profile_name)) {
        return false;
    }

    map_profiles.insert_or_assign(
        profile_name,
        std::make_unique<AndroidConfig>(profile_name, Config::ConfigType::InputProfile));

    return SaveProfile(profile_name, j_player_index);
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_deleteProfile(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_name,
                                                                          jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    if (!ProfileExistsInMap(profile_name)) {
        return false;
    }

    if (!ProfileExistsInFilesystem(profile_name) ||
        Common::FS::RemoveFile(map_profiles[profile_name]->GetConfigFilePath())) {
        map_profiles.erase(profile_name);
    }

    Settings::values.players.GetValue()[j_player_index].profile_name = "";
    return !ProfileExistsInMap(profile_name) && !ProfileExistsInFilesystem(profile_name);
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_loadProfile(JNIEnv* env, jobject j_obj,
                                                                        jstring j_name,
                                                                        jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    return LoadProfile(profile_name, j_player_index);
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_saveProfile(JNIEnv* env, jobject j_obj,
                                                                        jstring j_name,
                                                                        jint j_player_index) {
    auto profile_name = Common::Android::GetJString(env, j_name);
    return SaveProfile(profile_name, j_player_index);
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_loadPerGameConfiguration(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_selected_index,
    jstring j_selected_profile_name) {
    static constexpr size_t HANDHELD_INDEX = 8;

    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    Settings::values.players.SetGlobal(false);

    auto profile_name = Common::Android::GetJString(env, j_selected_profile_name);
    auto* emulated_controller = hid_core.GetEmulatedControllerByIndex(j_player_index);

    if (j_selected_index == 0) {
        Settings::values.players.GetValue()[j_player_index].profile_name = "";
        if (j_player_index == 0) {
            Settings::values.players.GetValue()[HANDHELD_INDEX] = {};
        }
        Settings::values.players.SetGlobal(true);
        emulated_controller->ReloadFromSettings();
        return;
    }
    if (profile_name.empty()) {
        return;
    }
    auto& player = Settings::values.players.GetValue()[j_player_index];
    auto& global_player = Settings::values.players.GetValue(true)[j_player_index];
    player.profile_name = profile_name;
    global_player.profile_name = profile_name;
    // Read from the profile into the custom player settings
    LoadProfile(profile_name, j_player_index);
    // Make sure the controller is connected
    player.connected = true;

    emulated_controller->ReloadFromSettings();

    if (j_player_index > 0) {
        return;
    }
    // Handle Handheld cases
    auto& handheld_player = Settings::values.players.GetValue()[HANDHELD_INDEX];
    auto* handheld_controller = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
    if (player.controller_type == Settings::ControllerType::Handheld) {
        handheld_player = player;
    } else {
        handheld_player = {};
    }
    handheld_controller->ReloadFromSettings();
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_beginMapping(JNIEnv* env, jobject j_obj,
                                                                     jint jtype) {
    EmulationSession::GetInstance().GetInputSubsystem().BeginMapping(
        static_cast<InputCommon::Polling::InputType>(jtype));
}

jstring Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getNextInput(JNIEnv* env,
                                                                        jobject j_obj) {
    return Common::Android::ToJString(
        env, EmulationSession::GetInstance().GetInputSubsystem().GetNextInput().Serialize());
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_stopMapping(JNIEnv* env, jobject j_obj) {
    EmulationSession::GetInstance().GetInputSubsystem().StopMapping();
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_updateMappingsWithDefaultImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jstring j_device_params,
    jstring j_display_name) {
    auto& input_subsystem = EmulationSession::GetInstance().GetInputSubsystem();

    // Clear all previous mappings
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(button_id, {});
        });
    }
    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(analog_id, {});
        });
    }

    // Apply new mappings
    auto device = Common::ParamPackage(Common::Android::GetJString(env, j_device_params));
    auto button_mappings = input_subsystem.GetButtonMappingForDevice(device);
    auto analog_mappings = input_subsystem.GetAnalogMappingForDevice(device);
    auto display_name = Common::Android::GetJString(env, j_display_name);
    for (const auto& button_mapping : button_mappings) {
        const std::size_t index = button_mapping.first;
        auto named_mapping = button_mapping.second;
        named_mapping.Set("display", display_name);
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(index, named_mapping);
        });
    }
    for (const auto& analog_mapping : analog_mappings) {
        const std::size_t index = analog_mapping.first;
        auto named_mapping = analog_mapping.second;
        named_mapping.Set("display", display_name);
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(index, named_mapping);
        });
    }
}

/// One player's controls from two devices held as a pair (two Joy-Cons, which Android lists as
/// two controllers): the left device gives the left half, the right device the right half. Each
/// half is the device's own default mapping, with what a lone Joy-Con reports differently made
/// up for:
/// - a Joy-Con's stick may be its X/Y axes whichever side it is: the other stick's axes stand in;
/// - the left Joy-Con's direction buttons reach the app by scan code only (InputHandler), so the
///   device does not list them: they are mapped all the same;
/// - a lone Joy-Con's SL and SR are its other shoulder pair (hid-nintendo).
void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_updateMappingsWithDualDevicesImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jstring j_left_params, jstring j_right_params,
    jstring j_left_name, jstring j_right_name) {
    namespace NativeButton = Settings::NativeButton;
    namespace NativeAnalog = Settings::NativeAnalog;
    auto& input_subsystem = EmulationSession::GetInstance().GetInputSubsystem();

    const Common::ParamPackage left(Common::Android::GetJString(env, j_left_params));
    const Common::ParamPackage right(Common::Android::GetJString(env, j_right_params));
    const auto left_name = Common::Android::GetJString(env, j_left_name);
    const auto right_name = Common::Android::GetJString(env, j_right_name);
    auto left_buttons = input_subsystem.GetButtonMappingForDevice(left);
    auto right_buttons = input_subsystem.GetButtonMappingForDevice(right);
    auto left_analogs = input_subsystem.GetAnalogMappingForDevice(left);
    auto right_analogs = input_subsystem.GetAnalogMappingForDevice(right);

    std::array<Common::ParamPackage, NativeButton::NumButtons> buttons{};
    std::array<Common::ParamPackage, NativeAnalog::NumAnalogs> sticks{};
    const auto take = [](auto& from, int from_id, auto& to, int to_id, const std::string& name) {
        using Key = typename std::decay_t<decltype(from)>::key_type;
        const auto it = from.find(static_cast<Key>(from_id));
        if (it == from.end() || !it->second.Has("engine")) {
            return false;
        }
        to[static_cast<size_t>(to_id)] = it->second;
        to[static_cast<size_t>(to_id)].Set("display", name);
        return true;
    };
    const auto button_of = [](const Common::ParamPackage& device, int keycode,
                              const std::string& name) {
        Common::ParamPackage param;
        param.Set("engine", device.Get("engine", "android"));
        param.Set("port", device.Get("port", 0));
        param.Set("guid", device.Get("guid", ""));
        param.Set("button", keycode);
        param.Set("display", name);
        return param;
    };

    // The left half
    for (const int id : {NativeButton::LStick, NativeButton::L, NativeButton::ZL,
                         NativeButton::Minus, NativeButton::DUp, NativeButton::DDown,
                         NativeButton::DLeft, NativeButton::DRight}) {
        take(left_buttons, id, buttons, id, left_name);
    }
    constexpr int KEYCODE_DPAD_UP = 19;
    for (int i = 0; i < 4; ++i) {
        // DUp, DDown, DLeft, DRight beside KEYCODE_DPAD_UP, _DOWN, _LEFT, _RIGHT
        constexpr std::array dpad{NativeButton::DUp, NativeButton::DDown, NativeButton::DLeft,
                                  NativeButton::DRight};
        auto& param = buttons[static_cast<size_t>(dpad[static_cast<size_t>(i)])];
        if (!param.Has("engine") && left.Has("guid")) {
            param = button_of(left, KEYCODE_DPAD_UP + i, left_name);
        }
    }
    if (!take(left_analogs, NativeAnalog::LStick, sticks, NativeAnalog::LStick, left_name)) {
        take(left_analogs, NativeAnalog::RStick, sticks, NativeAnalog::LStick, left_name);
    }
    take(left_buttons, NativeButton::R, buttons, NativeButton::SLLeft, left_name);
    take(left_buttons, NativeButton::ZR, buttons, NativeButton::SRLeft, left_name);

    // The right half
    for (const int id : {NativeButton::A, NativeButton::B, NativeButton::X, NativeButton::Y,
                         NativeButton::RStick, NativeButton::R, NativeButton::ZR,
                         NativeButton::Plus}) {
        take(right_buttons, id, buttons, id, right_name);
    }
    if (!take(right_analogs, NativeAnalog::RStick, sticks, NativeAnalog::RStick, right_name)) {
        take(right_analogs, NativeAnalog::LStick, sticks, NativeAnalog::RStick, right_name);
    }
    if (!buttons[NativeButton::RStick].Has("engine")) {
        // Its stick's click may be the left stick's button when it reports the left stick's axes
        take(right_buttons, NativeButton::LStick, buttons, NativeButton::RStick, right_name);
    }
    take(right_buttons, NativeButton::L, buttons, NativeButton::SLRight, right_name);
    take(right_buttons, NativeButton::ZL, buttons, NativeButton::SRRight, right_name);

    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        for (int id = 0; id < NativeButton::NumButtons; ++id) {
            controller->SetButtonParam(static_cast<size_t>(id), buttons[static_cast<size_t>(id)]);
        }
        for (int id = 0; id < NativeAnalog::NumAnalogs; ++id) {
            controller->SetStickParam(static_cast<size_t>(id), sticks[static_cast<size_t>(id)]);
        }
    });
}

jstring Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getButtonParamImpl(JNIEnv* env,
                                                                              jobject j_obj,
                                                                              jint j_player_index,
                                                                              jint j_button) {
    return Common::Android::ToJString(env, EmulationSession::GetInstance()
                                               .System()
                                               .HIDCore()
                                               .GetEmulatedControllerByIndex(j_player_index)
                                               ->GetButtonParam(j_button)
                                               .Serialize());
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_setButtonParamImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_button_id, jstring j_param) {
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetButtonParam(j_button_id,
                                   Common::ParamPackage(Common::Android::GetJString(env, j_param)));
    });
}

jstring Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getStickParamImpl(JNIEnv* env,
                                                                             jobject j_obj,
                                                                             jint j_player_index,
                                                                             jint j_stick) {
    return Common::Android::ToJString(env, EmulationSession::GetInstance()
                                               .System()
                                               .HIDCore()
                                               .GetEmulatedControllerByIndex(j_player_index)
                                               ->GetStickParam(j_stick)
                                               .Serialize());
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_setStickParamImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index, jint j_stick_id, jstring j_param) {
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetStickParam(j_stick_id,
                                  Common::ParamPackage(Common::Android::GetJString(env, j_param)));
    });
}

jint Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getButtonNameImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jstring j_param) {
    return static_cast<jint>(EmulationSession::GetInstance().GetInputSubsystem().GetButtonName(
        Common::ParamPackage(Common::Android::GetJString(env, j_param))));
}

jintArray Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getSupportedStyleTagsImpl(
    JNIEnv* env, jobject j_obj, jint j_player_index) {
    auto supported_styles = GetSupportedStyles(j_player_index);
    jintArray j_supported_indexes = env->NewIntArray(supported_styles.size());
    env->SetIntArrayRegion(j_supported_indexes, 0, supported_styles.size(),
                           supported_styles.data());
    return j_supported_indexes;
}

jint Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getStyleIndexImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jint j_player_index) {
    return static_cast<s32>(EmulationSession::GetInstance()
                                .System()
                                .HIDCore()
                                .GetEmulatedControllerByIndex(j_player_index)
                                ->GetNpadStyleIndex(true));
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_setStyleIndexImpl(JNIEnv* env,
                                                                          jobject j_obj,
                                                                          jint j_player_index,
                                                                          jint j_style_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    auto type = static_cast<Core::HID::NpadStyleIndex>(j_style_index);
    ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
        controller->SetNpadStyleIndex(type);
    });
    if (j_player_index == 0) {
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        auto* player_one = hid_core.GetEmulatedController(Core::HID::NpadIdType::Player1);
        ConnectController(j_player_index,
                          player_one->IsConnected(true) || handheld->IsConnected(true));
    }
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_isControllerImpl(JNIEnv* env,
                                                                             jobject j_obj,
                                                                             jstring jparams) {
    return static_cast<jint>(EmulationSession::GetInstance().GetInputSubsystem().IsController(
        Common::ParamPackage(Common::Android::GetJString(env, jparams))));
}

jboolean Java_org_yuzu_yuzu_1emu_features_input_NativeInput_getIsConnected(JNIEnv* env,
                                                                           jobject j_obj,
                                                                           jint j_player_index) {
    auto& hid_core = EmulationSession::GetInstance().System().HIDCore();
    auto* controller = hid_core.GetEmulatedControllerByIndex(static_cast<size_t>(j_player_index));
    if (j_player_index == 0 &&
        controller->GetNpadStyleIndex(true) == Core::HID::NpadStyleIndex::Handheld) {
        return hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld)->IsConnected(true);
    }
    return controller->IsConnected(true);
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_connectControllersImpl(
    JNIEnv* env, jobject j_obj, jbooleanArray j_connected) {
    jboolean isCopy = false;
    auto j_connected_array_size = env->GetArrayLength(j_connected);
    jboolean* j_connected_array = env->GetBooleanArrayElements(j_connected, &isCopy);
    for (int i = 0; i < j_connected_array_size; ++i) {
        ConnectController(i, j_connected_array[i]);
    }
}

void Java_org_yuzu_yuzu_1emu_features_input_NativeInput_resetControllerMappings(
    JNIEnv* env, jobject j_obj, jint j_player_index) {
    // Clear all previous mappings
    for (int button_id = 0; button_id < Settings::NativeButton::NumButtons; ++button_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetButtonParam(button_id, {});
        });
    }
    for (int analog_id = 0; analog_id < Settings::NativeAnalog::NumAnalogs; ++analog_id) {
        ApplyControllerConfig(j_player_index, [&](Core::HID::EmulatedController* controller) {
            controller->SetStickParam(analog_id, {});
        });
    }
}

} // extern "C"
