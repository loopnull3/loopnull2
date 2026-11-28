// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string_view>

#include "common/settings.h"
#include "common/scope_exit.h"
#include "core/hle/kernel/svc_results.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/registered_cache.h"
#include "core/hle/service/am/am_types.h"
#include "core/hle/service/am/applet.h"
#include "core/hle/service/am/applet_manager.h"
#include "core/hle/service/am/process_creation.h"
#include "core/hle/service/am/service/application_accessor.h"
#include "core/hle/service/am/service/application_creator.h"
#include "core/hle/service/am/window_system.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/loader/loader.h"
#include "core/launch_timestamp_cache.h"
#include "core/hle/api_version.h"

namespace Service::AM {

namespace {

// Where a launch from the home menu takes its program from, for the log.
std::string_view ProgramSource(const FileSys::ContentProviderUnion::ApplicationProgram& source) {
    if (source.loader_backed) {
        return "its game file";
    }
    switch (source.slot) {
    case FileSys::ContentProviderUnionSlot::SysNAND:
        return "the system NAND";
    case FileSys::ContentProviderUnionSlot::UserNAND:
        return "the user NAND";
    case FileSys::ContentProviderUnionSlot::SDMC:
        return "the SD card";
    case FileSys::ContentProviderUnionSlot::FrontendManual:
        return "the game list's content";
    case FileSys::ContentProviderUnionSlot::External:
        return "the external content";
    default:
        return "an unknown slot";
    }
}

Result CreateGuestApplication(SharedPointer<IApplicationAccessor>* out_application_accessor, Core::System& system, WindowSystem& window_system, u64 program_id) {
    const auto source = system.GetContentProviderUnion().GetApplicationProgram(program_id);
    if (source.file == nullptr) {
        LOG_WARNING(Service_AM,
                    "Application {:016X} is in no content provider and has no game file registered",
                    program_id);
        R_THROW(ResultUnknown);
    }
    const auto from = ProgramSource(source);
    if ((source.loader_backed || (program_id >> 56) == 0x04) &&
        !Settings::CurrentProgramIsSwitch2()) {
        LOG_WARNING(Service_AM,
                    "Switch 2 application {:016X} (from {}) requires Switch 2 Home Menu firmware",
                    program_id, from);
        R_THROW(ResultUnknown);
    }

    std::vector<u8> control;
    std::unique_ptr<Loader::AppLoader> loader;
    Loader::ResultStatus result{};
    auto process = CreateApplicationProcess(control, loader, result, system, source.file,
                                            program_id, 0, source.slot);
    if (!process) {
        LOG_WARNING(Service_AM, "Loading application {:016X} from {} failed with loader result {}",
                    program_id, from, static_cast<u16>(result));
        R_THROW(ResultUnknown);
    }
    LOG_INFO(Service_AM,
             "Loaded Home Menu application {:016X} from {} ({}), process program {:016X}",
             program_id, from, Loader::GetFileTypeString(loader->GetFileType()),
             process->GetProgramId());

    const auto applet = std::make_shared<Applet>(system, std::move(process), true);
    applet->owned_by_application_creator = true;
    applet->launched_from_native_home_menu = window_system.HasNativeHomeMenu();
    applet->program_id = program_id;
    applet->applet_id = AppletId::Application;
    applet->type = AppletType::Application;
    applet->library_applet_mode = LibraryAppletMode::AllForeground;

    window_system.TrackApplet(applet, true, true);

    *out_application_accessor = std::make_shared<IApplicationAccessor>(system, applet, window_system);
    R_SUCCEED();
}

} // namespace

IApplicationCreator::IApplicationCreator(Core::System& system_, WindowSystem& window_system)
    : ServiceFramework{system_, "IApplicationCreator"}, m_window_system{window_system} {
    // clang-format off
    static const FunctionInfo functions[] = {
        {0, D<&IApplicationCreator::CreateApplication>, "CreateApplication"},
        {1, nullptr, "PopLaunchRequestedApplication"},
        {10, D<&IApplicationCreator::CreateSystemApplication>, "CreateSystemApplication"},
        {100, nullptr, "PopFloatingApplicationForDevelopment"},
    };
    // clang-format on

    RegisterHandlers(functions);
}

IApplicationCreator::~IApplicationCreator() = default;

Result IApplicationCreator::CreateApplication(
    Out<SharedPointer<IApplicationAccessor>> out_application_accessor, u64 application_id) {
    LOG_INFO(Service_NS, "called, application_id={:016X}", application_id);
    if (!m_window_system.ReserveApplicationSlot()) {
        LOG_WARNING(Service_AM, "Application {:016X} not created: the application slot is taken",
                    application_id);
        R_THROW(Kernel::ResultBusy);
    }
    SCOPE_EXIT { m_window_system.ReleaseApplicationSlotReservation(); };
    R_TRY(CreateGuestApplication(out_application_accessor, system, m_window_system, application_id));
    Core::LaunchTimestampCache::SaveLaunchTimestamp(application_id);
    R_SUCCEED();
}

Result IApplicationCreator::CreateSystemApplication(
    Out<SharedPointer<IApplicationAccessor>> out_application_accessor, u64 application_id) {

    R_UNLESS(m_window_system.ReserveApplicationSlot(), Kernel::ResultBusy);
    SCOPE_EXIT { m_window_system.ReleaseApplicationSlotReservation(); };
    FileSys::VirtualFile nca_raw{};

    auto& storage = system.GetContentProviderUnion();
    nca_raw = storage.GetEntryRaw(application_id, FileSys::ContentRecordType::Program);

    R_UNLESS(nca_raw != nullptr, ResultUnknown);

    std::vector<u8> control;
    std::unique_ptr<Loader::AppLoader> loader;
    auto process = CreateProcess(system, application_id, 1, HLE::ApiVersion::HOS_VERSION_MAJOR);
    R_UNLESS(process != nullptr, ResultUnknown);

    const auto applet = std::make_shared<Applet>(system, std::move(process), true);
    applet->owned_by_application_creator = true;
    applet->launched_from_native_home_menu = m_window_system.HasNativeHomeMenu();
    applet->program_id = application_id;
    applet->applet_id = AppletId::Starter;
    applet->type = AppletType::LibraryApplet;
    applet->library_applet_mode = LibraryAppletMode::AllForeground;

    m_window_system.TrackApplet(applet, true, true);

    *out_application_accessor = std::make_shared<IApplicationAccessor>(system, applet, m_window_system);
    Core::LaunchTimestampCache::SaveLaunchTimestamp(application_id);
    R_SUCCEED();
}

} // namespace Service::AM
