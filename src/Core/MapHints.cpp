// Keeps the button hints of SkyUI's map on the current binds.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- map button hints
    // SkyUI's map (Interface/map.swf) writes fixed keys into its bottom bar (L, E, Mouse1...)
    // instead of asking the control map like its other menus do, so a rebound map control
    // kept its old hint. When the map opens its controls get the event names instead and the
    // bar is rebuilt. SetPlatform (keyboard <-> gamepad) writes the fixed keys back: wrapped.
    namespace
    {
        struct MapHint
        {
            const char* field;     // member of Map.MapMenu
            const char* event;     // control map event
            bool        gameplay;  // in the Gameplay context, not Map
            bool        keyboard;  // only on keyboard / mouse: SkyUI's gamepad button stays
        };
        constexpr MapHint kMapHints[] = {
            { "_localMapControls", "LocalMap", false, false },
            { "_playerLocControls", "PlayerPosition", false, false },
            { "_setDestControls", "Click", false, true },
            { "_journalControls", "Journal", true, false },
        };
        // "_findLocControls" (F) is left alone: search is SkyUI's own key, it has no control

        // SkyUI's MappedButton looks {name, context} up with skse.GetMappedKey
        RE::GFxValue MappedControl(RE::GFxMovie* movie, const char* event, double ctx)
        {
            RE::GFxValue o;
            movie->CreateObject(&o);
            o.SetMember("name", event);
            o.SetMember("context", ctx);
            return o;
        }

        double SkyUIContext(RE::GFxMovie* movie, const char* name, double fallback)
        {
            RE::GFxValue v;
            const auto   path = std::string("_global.skyui.defines.Input.") + name;
            return movie->GetVariable(&v, path.c_str()) && v.IsNumber() ? v.GetNumber() : fallback;
        }

        void FixMapHints(RE::GFxMovie* movie)
        {
            RE::GFxValue map, platform;
            if (!movie || !movie->GetVariable(&map, "_root.WorldMap") || !map.IsObject() || !map.HasMember("_localMapControls")) return;  // not SkyUI's map
            if (!map.GetMember("_platform", &platform) || !platform.IsNumber()) return;  // SetPlatform not called yet
            const bool   gamepad = platform.GetNumber() != 0;  // ButtonChange.PLATFORM_PC = 0
            const double mapCtx  = SkyUIContext(movie, "CONTEXT_MAP", kCtxMap);
            const double playCtx = SkyUIContext(movie, "CONTEXT_GAMEPLAY", kCtxGameplay);
            for (const auto& h : kMapHints)
                if (!gamepad || !h.keyboard) map.SetMember(h.field, MappedControl(movie, h.event, h.gameplay ? playCtx : mapCtx));
            if (gamepad) {  // on the mouse zoom is the wheel, no control to look up
                RE::GFxValue zoom;
                movie->CreateArray(&zoom);
                zoom.PushBack(MappedControl(movie, "Zoom In", mapCtx));
                zoom.PushBack(MappedControl(movie, "Zoom Out", mapCtx));
                map.SetMember("_zoomControls", zoom);
            }
            const RE::GFxValue arg(gamepad);
            map.Invoke("createButtons", nullptr, &arg, 1);
        }

        // WorldMap.SetPlatform: SkyUI's own, then the hints again
        class SetPlatformHook : public RE::GFxFunctionHandler
        {
        public:
            void Call(Params& a) override
            {
                if (a.thisPtr) a.thisPtr->Invoke("_haSetPlatform", a.retVal, a.args, a.argCount);
                FixMapHints(a.movie);
            }
        };

        void HookMapMenu(RE::GFxMovie* movie)
        {
            RE::GFxValue map, original;
            if (!movie->GetVariable(&map, "_root.WorldMap") || !map.IsObject() || !map.HasMember("_localMapControls")) return;
            if (!map.HasMember("_haSetPlatform") && map.GetMember("SetPlatform", &original) && original.IsObject()) {
                static SetPlatformHook hook;
                RE::GFxValue           fn;
                movie->CreateFunction(&fn, &hook);
                map.SetMember("_haSetPlatform", original);
                map.SetMember("SetPlatform", fn);
            }
            FixMapHints(movie);
        }

        class MapMenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* e, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (e && e->opening && e->menuName == RE::MapMenu::MENU_NAME)
                    SKSE::GetTaskInterface()->AddUITask([] {
                        auto* ui   = RE::UI::GetSingleton();
                        auto  menu = ui ? ui->GetMenu(RE::MapMenu::MENU_NAME) : nullptr;
                        if (menu && menu->uiMovie) HookMapMenu(menu->uiMovie.get());
                    });
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void WatchMapMenu()
    {
        static MapMenuWatcher watcher;
        if (auto* ui = RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&watcher);
    }
}
