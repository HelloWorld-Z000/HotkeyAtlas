// SKSE entry point: logging, the input hook, and the start-up order once the game data is loaded.

#include "HotkeyAtlas.h"

namespace
{
    void SetupLog()
    {
        auto path = logger::log_directory();
        if (!path) return;
        *path /= "HotkeyAtlas.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log  = std::make_shared<spdlog::logger>("global log", std::move(sink));
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(log));
    }

    void OnMessage(SKSE::MessagingInterface::Message* msg)
    {
        // every step where other plugins may have installed their own input hooks
        HA::EnsureInputHookOnTop();

        switch (msg->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            HA::LoadConfig();
            HA::LoadTranslation();
            HA::UI::Register();
            HA::WatchMapMenu();
            HA::Rescan();  // also applies the overrides
            logger::info("Hotkey Atlas ready");
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            HA::ApplyOverridesLater();
            break;
        default:
            break;
        }
    }
}

// Name and version come from xmake.lua (commonlibsse-ng.plugin rule).
SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    HA::InstallInputHook();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
