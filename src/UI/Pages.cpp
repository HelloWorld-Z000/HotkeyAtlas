// The menu pages (Bind editor, All binds, Blacklist) and their registration.

#include "UI/UI.h"

namespace HA::UI
{
    namespace
    {
        // The menu closed (Esc...): our pages are no longer drawn, drop the capture and give the
        // hotkey back right away, or the menu could not be opened again.
        // the menu's pages, in order; their names follow the interface language
        constexpr const char* kSection = "Hotkey Atlas";
        constexpr const char* kPages[] = { N_("Bind editor"), N_("All binds"), N_("Blacklist") };
    }
    std::array<std::string, std::size(kPages)> g_pageNames;  // as the menu shows them now

    namespace
    {
        // Renames the pages after a language change. Called before the menu draws: renaming
        // while it walks its page list could pull a page out from under it.
        void SyncPageNames()
        {
            for (std::size_t i = 0; i < std::size(kPages); ++i) {
                const std::string name = TL(kPages[i]);
                if (name == g_pageNames[i]) continue;
                if (!SKSEMenuFramework::RenameSection(std::string(kSection) + '/' + g_pageNames[i], name))
                    logger::warn("menu page '{}' could not be renamed to '{}'", g_pageNames[i], name);
                g_pageNames[i] = name;  // no retry every frame on a failure
            }
        }

        void __stdcall OnMenuEvent(SKSEMenuFramework::Model::EventType type)
        {
            if (type == SKSEMenuFramework::Model::kBeforeRender) SyncPageNames();
            if (type != SKSEMenuFramework::Model::kCloseMenu) return;
            g_capture.reset();
            ClearPending();
            SyncMenuHotkey(true);
        }

        // ---- pages
        void __stdcall RenderKeyboard()
        {
            const TableMenuText tableMenu;
            const auto model = GetModel();
            g_bl             = GetBlacklist();
            g_uiNotes        = GetNotes();
            static auto lastShown = Device::Keyboard;  // the tab open last frame: the toolbar comes first
            Toolbar(lastShown);
            HandleCapture();
            ImGui::Separator();

            // one device at a time: the page stays short. Our own tab colours: the menu's theme
            // lights the tabs up the wrong way round (and the unfocused set whenever the game has
            // focus); here the open tab and the one under the cursor are the lit ones
            const ImGui::ImVec4 tabIdle(0.16f, 0.16f, 0.18f, 1.0f), tabHover(0.24f, 0.42f, 0.66f, 1.0f), tabOpen(0.14f, 0.36f, 0.62f, 1.0f);
            ImGui::PushStyleColor(ImGui::ImGuiCol_Tab, tabIdle);
            ImGui::PushStyleColor(ImGui::ImGuiCol_TabUnfocused, tabIdle);
            ImGui::PushStyleColor(ImGui::ImGuiCol_TabHovered, tabHover);
            ImGui::PushStyleColor(ImGui::ImGuiCol_TabActive, tabOpen);
            ImGui::PushStyleColor(ImGui::ImGuiCol_TabUnfocusedActive, tabOpen);
            auto shown = Device::Keyboard;
            if (ImGui::BeginTabBar("##devices")) {
                if (ImGui::BeginTabItem(Id(TL("Keyboard"), "tabkeyboard").c_str())) {
                    DrawDevice(*model, Device::Keyboard, KeyboardDefs(), 23.0f, 6.25f, 72.0f);  // numpad included
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(Id(TL("Mouse"), "tabmouse").c_str())) {
                    shown = Device::Mouse;
                    DrawPicture(*model, Device::Mouse, kMouseButtons, kMouseW, kMouseH, 54.0f);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(Id(TL("Gamepad"), "tabgamepad").c_str())) {
                    shown   = Device::Gamepad;
                    // Xbox | PlayStation switch: two joined halves, the chosen one lit like the open tab
                    const bool ps = PlayStationLabels();
                    const auto half = [&](const char* label, bool on, const char* tip) {
                        ImGui::PushStyleColor(ImGui::ImGuiCol_Button, on ? tabOpen : tabIdle);
                        ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonHovered, on ? tabOpen : tabHover);
                        ImGui::PushStyleColor(ImGui::ImGuiCol_ButtonActive, tabOpen);
                        const bool pressed = ImGui::Button(label);
                        ImGui::PopStyleColor(3);
                        if (ImGui::IsItemHovered()) Tooltip(TL(tip));
                        return pressed && !on;
                    };
                    if (half("Xbox##padxbox", !ps, N_("Controller picture and Xbox button names (LB, RT, A, B...)."))) SetPlayStationLabels(false);
                    ImGui::SameLine(0.0f, 0.0f);
                    if (half("PlayStation##padps", ps, N_("Controller picture and PlayStation button names (L1, R2, Cross, Circle...)."))) SetPlayStationLabels(true);
                    const bool psNow = PlayStationLabels();
                    DrawPicture(*model, Device::Gamepad, psNow ? std::span<const ButtonDef>(kPadButtonsPs) : std::span<const ButtonDef>(kPadButtons), kPadW, kPadH, 52.0f);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::PopStyleColor(5);
            lastShown = shown;
            ImGui::Separator();

            if (!g_hasSelected || g_selDevice != shown) {
                Muted(TL("Click a button to see all its binds. Blue: bound, amber: used by more than one mod."));
                return;
            }
            ImGui::TextUnformatted(TLF("Bind: {0}", { ButtonLabel(g_selDevice, g_selected) }).c_str());
            const auto rows = TableRowsFor(*model, g_selDevice, g_selected);
            if (rows.empty()) {
                Muted(TL("This button has no binds."));
                return;
            }
            DrawTable("##key", *model, rows, true, g_selDevice);  // key column shows the combo (O vs Shift+O)
        }

        void __stdcall RenderAll()
        {
            const TableMenuText tableMenu;
            const auto model = GetModel();
            g_bl             = GetBlacklist();
            g_uiNotes        = GetNotes();
            Toolbar();
            HandleCapture();
            ImGui::Separator();
            ImGui::InputText(Id(TL("Filter"), "filter").c_str(), g_filter, sizeof g_filter);
            if (ImGui::IsItemHovered())
                Tooltip(TLF("Searches bind, action, note, mod, context and file. Type \"{0}\" to list removed binds, \"{1}\" to list the ones you changed.",
                    { TL("Unbound"), TL("Changed") }));

            // "Show gamepad binds" off: leave out the binds that exist only on the gamepad, the
            // Skyrim events with no key or mouse button in their context and mods' gamepad buttons
            const bool                                 pads = ShowCrossDevice();
            std::set<std::pair<int, std::string_view>> keyed;  // (context, event) with a key or mouse binding
            if (!pads)
                for (const auto& b : model->all)
                    if (b.kind == Kind::ControlMap && CodeDevice(b.defaultKey) != Device::Gamepad) keyed.emplace(b.ctx, b.action);

            const auto               needle = LowerStr(g_filter);
            std::vector<std::size_t> rows;
            rows.reserve(model->all.size());
            for (std::size_t i = 0; i < model->all.size(); ++i) {
                const auto& b = model->all[i];
                if (!Visible(b)) continue;
                if (!pads && CodeDevice(b.defaultKey) == Device::Gamepad && !(b.kind == Kind::ControlMap && keyed.contains({ b.ctx, b.action }))) continue;
                if (!needle.empty()) {
                    auto hay = LowerStr(KeyText(b, Device::Keyboard, true) + ' ' + b.action + ' ' + NoteText(b) + ' ' + b.owner + ' ' + b.context + ' ' + b.origin);
                    // rebound, unbound or given a gamepad button with Hotkey Atlas: findable by
                    // "Changed" in the interface language and in English
                    if (b.overridden || b.padKey != kUnbound) hay += ' ' + LowerStr(TL("Changed")) + " changed";
                    if (hay.find(needle) == std::string::npos) continue;
                }
                rows.push_back(i);
            }
            DrawTable("##all", *model, rows, true);
        }

        char g_blFilter[128] = "";

        // Every mod that has bindings (Skyrim's own controls have "Hide vanilla" instead),
        // with a checkbox to hide it everywhere else in the menu.
        void __stdcall RenderBlacklist()
        {
            EndCapture();  // a capture left pending on another page: this page can't show it
            const TableMenuText tableMenu;
            const auto model = GetModel();
            g_bl             = GetBlacklist();

            // owner -> binding count; blacklisted mods that are gone from the scan stay listed so they can be removed
            std::map<std::string, std::size_t, std::less<>> counts;
            for (const auto& b : model->all)
                if (b.kind != Kind::ControlMap || b.owner != "Skyrim") ++counts[b.owner];
            for (const auto& lower : *g_bl)
                if (std::ranges::none_of(counts, [&](const auto& o) { return LowerStr(o.first) == lower; })) counts.emplace(lower, 0);

            // the rows the filter shows; Hide all / Show all act on these only
            const auto                                       needle = LowerStr(g_blFilter);
            std::vector<std::pair<std::string, std::size_t>> owners;
            for (const auto& [owner, count] : counts)
                if (needle.empty() || LowerStr(owner).find(needle) != std::string::npos) owners.emplace_back(owner, count);
            std::vector<std::string> names;
            for (const auto& [owner, count] : owners) names.push_back(owner);

            if (ImGui::Button(Id(TL("Rescan"), "rescan").c_str())) Rescan();
            ImGui::SameLine();
            if (ImGui::Button(Id(TL("Hide all"), "hideall").c_str())) SetBlacklisted(names, true);
            ImGui::SameLine();
            if (ImGui::Button(Id(TL("Show all"), "showall").c_str())) SetBlacklisted(names, false);
            ImGui::SameLine();
            Muted(IsBusy() ? TL("Scanning...") : TL("Checked mods are hidden from the keyboard and the tables. Their remaps keep working."));
            ImGui::Separator();
            ImGui::InputText(Id(TL("Filter"), "blfilter").c_str(), g_blFilter, sizeof g_blFilter);
            if (!needle.empty()) {
                ImGui::SameLine();
                Muted(TL("Hide all / Show all apply to the filtered list."));
            }

            if (!ImGui::BeginTable("##blacklist", 3, ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_Sortable | ImGui::ImGuiTableFlags_SortMulti))
                return;
            ImGui::TableSetupColumn(Id(TL("Hide"), "hide").c_str(), ImGui::ImGuiTableColumnFlags_WidthFixed, 0.0f, kColHide);
            ImGui::TableSetupColumn(Id(TL("Mod"), "mod").c_str(), ImGui::ImGuiTableColumnFlags_DefaultSort, 0.0f, kColMod);
            ImGui::TableSetupColumn(Id(TL("Binds"), "hotkeys").c_str(), ImGui::ImGuiTableColumnFlags_WidthFixed | ImGui::ImGuiTableColumnFlags_PreferSortDescending, 0.0f, kColHotkeys);
            ImGui::TableHeadersRow();

            SortRows(owners, [&](const auto& a, const auto& b, ImGui::ImGuiID col) {
                switch (col) {
                case kColHide:
                    return Compare3(!IsBlacklisted(a.first), !IsBlacklisted(b.first));  // hidden first
                case kColHotkeys:
                    return Compare3(a.second, b.second);
                default:
                    return CompareText(a.first, b.first);
                }
            });

            int row = 0;
            for (const auto& [owner, count] : owners) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                bool hidden = IsBlacklisted(owner);
                char id[32];
                std::snprintf(id, sizeof id, "##bl%d", row++);
                if (ImGui::Checkbox(id, &hidden)) SetBlacklisted({ owner }, hidden);

                ImGui::TableSetColumnIndex(1);
                if (hidden)
                    Muted(owner.c_str());
                else
                    ImGui::TextUnformatted(owner.c_str());

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%zu", count);
            }
            ImGui::EndTable();
        }
    }

    void Register()
    {
        if (!GetModuleHandleA("SKSEMenuFramework.dll")) {
            logger::warn("SKSE Menu Framework is not loaded, UI not registered");
            return;
        }
        SKSEMenuFramework::SetSection(kSection);
        constexpr SKSEMenuFramework::Model::RenderFunction render[] = { RenderKeyboard, RenderAll, RenderBlacklist };
        static_assert(std::size(render) == std::size(kPages));
        for (std::size_t i = 0; i < std::size(kPages); ++i) {
            g_pageNames[i] = TL(kPages[i]);
            SKSEMenuFramework::AddSectionItem(g_pageNames[i], render[i]);
        }
        SKSEMenuFramework::AddEvent(OnMenuEvent, 0.0f);  // kept for the whole game
    }
}
