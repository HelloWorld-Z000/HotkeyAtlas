// Shared page state, labels and the toolbar (language, presets, reset).

#include "UI/UI.h"

namespace HA::UI
{
    // ---- UI state (render thread only)
    Device                 g_selDevice = Device::Keyboard;  // device of g_selected
    std::uint32_t          g_selected = 0;
    bool                   g_hasSelected = false;  // mouse button 0 (LMB) is a valid selection

    // a click on a key selects it, a second click on the selected key clears the selection
    void Select(Device device, std::uint32_t id)
    {
        const bool again = g_hasSelected && g_selDevice == device && g_selected == id;
        g_selDevice      = device;
        g_selected       = id;
        g_hasSelected    = !again;
    }
    std::optional<Binding> g_capture;
    char                   g_filter[128] = "";
    std::string            g_noteEdit;  // NoteId() of the note being edited, empty = none
    char                   g_noteBuf[256] = "";
    bool                   g_noteFocus    = false;  // focus the note editor on its first frame

    // ---- helpers
    std::string KeyLabel(std::uint32_t dik)
    {
        for (const auto& k : kKeys)
            if (k.dik == dik) return k.label;
        char b[16];
        std::snprintf(b, sizeof b, "0x%02X", dik);
        return b;
    }

    // ASCII and Cyrillic (UTF-8) to lower case, so a Russian filter matches whatever the case.
    std::string LowerStr(std::string s)
    {
        for (std::size_t i = 0; i < s.size(); ++i) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                s[i] = static_cast<char>(std::tolower(c));
            } else if (c == 0xD0 && i + 1 < s.size()) {
                const auto n = static_cast<unsigned char>(s[i + 1]);
                if (n >= 0x90 && n <= 0x9F) {  // А..П -> а..п
                    s[i + 1] = static_cast<char>(n + 0x20);
                } else if (n >= 0xA0 && n <= 0xAF) {  // Р..Я -> р..я
                    s[i]     = static_cast<char>(0xD1);
                    s[i + 1] = static_cast<char>(n - 0x20);
                } else if (n == 0x81) {  // Ё -> ё
                    s[i]     = static_cast<char>(0xD1);
                    s[i + 1] = static_cast<char>(0x91);
                }
                ++i;
            }
        }
        return s;
    }

    void Muted(const char* text)
    {
        ImGui::PushStyleColor(ImGui::ImGuiCol_Text, ImGui::ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    void Tooltip(const std::string& text)
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // Translated label with an ImGui id that stays the same in every language.
    std::string Id(std::string_view label, std::string_view id)
    {
        return std::string(label) + "###" + std::string(id);
    }

    namespace
    {
        // "russian" -> "Russian"
        std::string LanguageLabel(std::string lang)
        {
            if (!lang.empty()) lang[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(lang[0])));
            return lang;
        }

        std::vector<std::string> g_languages;  // translation files, read when the popup opens

        void LanguageButton()
        {
            if (ImGui::Button(Id(TL("Language"), "language").c_str())) {
                g_languages = Languages();
                ImGui::OpenPopup("##language");
            }
            if (ImGui::IsItemHovered())
                Tooltip(TLF("Interface language: {0}.", { LanguageLabel(ActiveLanguage()) }));
            if (!ImGui::BeginPopup("##language")) return;
            const auto chosen = Language();
            if (ImGui::Selectable(Id(TL("Game language"), "auto").c_str(), chosen.empty())) SetLanguage({});
            for (const auto& lang : g_languages)
                if (ImGui::Selectable(Id(LanguageLabel(lang), lang).c_str(), chosen == lang)) SetLanguage(lang);
            if (g_languages.empty()) Muted(TL("No translation files found."));
            ImGui::EndPopup();
        }

        std::vector<std::string> g_presets;  // preset files, read when the popup opens
        char                     g_presetName[64] = "";
        std::string              g_presetDelete;  // preset whose Delete waits for its second click

        // Every change goes into the active preset; a click on another one switches to it.
        void PresetsButton()
        {
            const auto active = ActivePreset();
            if (ImGui::Button(Id(TLF("Preset: {0}", { active }), "presets").c_str())) {
                g_presets = Presets();
                g_presetDelete.clear();
                ImGui::OpenPopup("##presets");
            }
            if (ImGui::IsItemHovered()) Tooltip(TL("Every bind change is saved into the active preset. Click a preset to switch to it."));
            if (!ImGui::BeginPopup("##presets")) return;

            std::string err;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
            if (ImGui::InputTextWithHint("##newpreset", TL("New preset: name, then Enter"), g_presetName, sizeof g_presetName,
                    ImGui::ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (const auto name = PresetName(g_presetName); !name.empty()) {
                    if (NewPreset(name, err)) {
                        g_presetName[0] = '\0';
                        g_presets       = Presets();
                        if (std::ranges::find(g_presets, name) == g_presets.end()) g_presets.push_back(name);  // file written by the save
                    } else {
                        SetStatus(err);
                    }
                }
            }
            if (ImGui::IsItemHovered()) Tooltip(TL("A new preset starts as a copy of the current binds."));
            ImGui::Separator();

            // names in a column wide enough for the longest, Delete to the right of each
            float nameW = ImGui::GetFontSize() * 8.0f;
            for (const auto& p : g_presets) nameW = (std::max)(nameW, ImGui::CalcTextSize(p.c_str()).x + ImGui::GetFontSize());
            for (const auto& p : g_presets) {
                ImGui::PushID(p.c_str());
                const bool isActive = Lower(p) == Lower(active);
                if (ImGui::Selectable(Id(p, "name").c_str(), isActive, ImGui::ImGuiSelectableFlags_DontClosePopups, ImGui::ImVec2(nameW, 0.0f)) && !isActive) {
                    g_presetDelete.clear();
                    if (SwitchPreset(p, err))
                        g_capture.reset();
                    else
                        SetStatus(err);
                }
                ImGui::SameLine();
                const bool armed = g_presetDelete == p;
                if (armed) ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImGui::ImVec4(0.65f, 0.16f, 0.16f, 1.0f));
                if (ImGui::Button(Id(TL(armed ? N_("Confirm delete") : N_("Delete")), "delete").c_str())) {
                    if (!armed) {
                        g_presetDelete = p;
                    } else {
                        g_presetDelete.clear();
                        if (DeletePreset(p, err))
                            std::erase(g_presets, p);
                        else
                            SetStatus(err);
                    }
                }
                if (armed) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }

    // refreshed once per frame by each page
    std::shared_ptr<const std::set<std::string>> g_bl = std::make_shared<const std::set<std::string>>();

    std::shared_ptr<const Notes> g_uiNotes = std::make_shared<const Notes>();

    const std::string* UserNote(const Binding& b)
    {
        if (g_uiNotes->empty()) return nullptr;
        const auto it = g_uiNotes->find(NoteId(b));
        return it != g_uiNotes->end() ? &it->second : nullptr;
    }

    // The user's note, else the built-in description.
    const std::string& NoteText(const Binding& b)
    {
        const auto* n = UserNote(b);
        return n ? *n : b.description;
    }

    bool IsBlacklisted(const std::string& owner)
    {
        return !g_bl->empty() && g_bl->contains(LowerStr(owner));
    }

    bool Visible(const Binding& b)
    {
        if (b.kind == Kind::ControlMap && HideVanilla()) return false;
        return !IsBlacklisted(b.owner);
    }

    // Visible bindings on a key or button: a combo is on every key and button it takes
    // (Shift + Mouse 4 on Shift and on Mouse 4, LB + A on LB and on A).
    std::vector<std::size_t> RowsFor(const Model& m, Device device, std::uint32_t id)
    {
        std::vector<std::size_t> rows;
        if (device == Device::Keyboard) {
            if (const auto it = m.byKey.find(id); it != m.byKey.end())
                for (auto i : it->second)
                    if (Visible(m.all[i])) rows.push_back(i);
            return rows;
        }
        // a gamepad button added to a key or mouse action is listed on the gamepad too
        for (std::size_t i = 0; i < m.all.size(); ++i)
            if (CodeUsing(m.all[i], device, id) != kUnbound && Visible(m.all[i])) rows.push_back(i);
        return rows;
    }

    // RowsFor plus the bindings unbound since that were on this key or button at first: the
    // table keeps them where they were, shown as Unbound with a Bind button. The key's count
    // and colour leave them out, the key is free.
    std::vector<std::size_t> TableRowsFor(const Model& m, Device device, std::uint32_t id)
    {
        auto rows = RowsFor(m, device, id);
        for (std::size_t i = 0; i < m.all.size(); ++i) {
            const auto& b = m.all[i];
            if (b.key == kUnbound && b.overridden && CodeUses(b.defaultKey, device, id) && Visible(b)) rows.push_back(i);
        }
        return rows;
    }

    // Amber only when the very same combo is used by different owners: O, Shift+O and
    // G + O on the O key don't clash.
    KeyInfo Info(const Model& m, Device device, std::uint32_t id)
    {
        KeyInfo    r;
        const auto rows = RowsFor(m, device, id);
        r.count         = rows.size();
        // all Skyrim controls count as one owner (Creation Club and Debug included): they live in
        // separate input contexts and never clash with each other
        static const std::string kGame = "\x01game";
        std::map<std::uint32_t, const std::string*> ownerByCombo;  // exact input code -> first owner
        for (auto i : rows) {
            const auto& b     = m.all[i];
            const auto* owner = b.kind == Kind::ControlMap ? &kGame : &b.owner;
            auto&       first = ownerByCombo[CodeUsing(b, device, id)];
            if (!first)
                first = owner;
            else if (*first != *owner)
                r.overlap = true;
        }
        return r;
    }

    std::string ComboLabel(std::uint32_t key, std::uint8_t mods)
    {
        if (key == kUnbound) return TL("Unbound");
        return ModsText(mods) + KeyLabel(key);
    }

    std::string ButtonLabel(Device device, std::uint32_t id)
    {
        if (device == Device::Keyboard) return KeyLabel(id);
        if (device == Device::Gamepad && PlayStationLabels())
            if (const auto* ps = PsLabel(id)) return TL(ps);
        for (const auto& d : device == Device::Mouse ? std::span<const ButtonDef>(kMouseButtons) : std::span<const ButtonDef>(kPadButtons))
            if (d.id == id) return TL(d.label);
        char b[16];
        std::snprintf(b, sizeof b, "0x%X", id);
        return b;
    }

    std::string CodeLabel(std::uint32_t code)
    {
        if (code == kUnbound) return TL("Unbound");
        const auto held = HoldOf(code) ? CodeLabel(HoldOf(code)) + " + " : std::string();
        if (CodeDevice(code) != Device::Keyboard) return held + ButtonLabel(CodeDevice(code), CodeId(code));
        return held + ComboLabel(ComboKey(code), ComboMods(code));
    }

    std::string ComboLabel(const Binding& b)
    {
        if (b.key == kUnbound) return TL("Unbound");
        const auto held = b.hold ? CodeLabel(b.hold) + " + " : std::string();
        if (b.device != Device::Keyboard) return held + ButtonLabel(b.device, b.key);
        return held + ComboLabel(b.key, b.mods);
    }

    // Key column: the binding's key and the gamepad button added to it ("T / RB"). Seen
    // from the gamepad (`view`) the button comes first ("RB / T"); the other device's
    // part is left out when "Gamepad / keyboard binds" is off. `all`: both, for searching.
    std::string KeyText(const Binding& b, Device view, bool all)
    {
        if (b.padKey == kUnbound) return ComboLabel(b);
        const bool both = all || ShowCrossDevice();
        if (view == Device::Gamepad && b.device != Device::Gamepad)
            return CodeLabel(b.padKey) + (both ? "  /  " + ComboLabel(b) : std::string());
        return ComboLabel(b) + (both ? "  /  " + CodeLabel(b.padKey) : std::string());
    }

    namespace
    {
        bool g_confirmReset = false;
    }

    // `reset`: the device whose changes Reset undoes (the Bind editor's open tab), none = all.
    void Toolbar(std::optional<Device> reset)
    {
        if (ImGui::Button(Id(TL("Rescan"), "rescan").c_str())) Rescan();

        ImGui::SameLine();
        bool hide = HideVanilla();
        if (ImGui::Checkbox(Id(TL("Hide vanilla binds"), "hidevanilla").c_str(), &hide)) SetHideVanilla(hide);
        if (ImGui::IsItemHovered()) Tooltip(TL("Hides vanilla binds."));

        ImGui::SameLine();
        bool cross = ShowCrossDevice();
        if (ImGui::Checkbox(Id(TL("Show gamepad binds"), "crossdevice").c_str(), &cross)) SetShowCrossDevice(cross);
        if (ImGui::IsItemHovered())
            Tooltip(TL("If on, the Bind column also shows the matching gamepad button when the action is bound to the gamepad.\n"
                       "All binds also lists the actions and binds that exist only on the gamepad."));

        ImGui::SameLine();
        if (ImGui::Button(Id(TL("Columns"), "columns").c_str())) ImGui::OpenPopup("##columns");
        if (ImGui::IsItemHovered()) Tooltip(TL("Choose which columns to show."));
        if (ImGui::BeginPopup("##columns")) {
            auto mask = HiddenColumns();
            for (const auto& col : kOptionalColumns) {
                bool shown = !(mask & col.bit);
                if (ImGui::Checkbox(Id(TL(col.name), col.name).c_str(), &shown)) mask = shown ? mask & ~col.bit : mask | col.bit;
            }
            SetHiddenColumns(mask);
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        LanguageButton();

        ImGui::SameLine();
        PresetsButton();

        ImGui::SameLine();
        // a confirmation left open on another tab or page doesn't carry over
        static std::optional<std::optional<Device>> confirmFor;
        if (g_confirmReset && confirmFor && *confirmFor != reset) g_confirmReset = false;
        confirmFor = reset;
        if (!g_confirmReset) {
            const char* label = !reset                         ? N_("Reset all")
                                : *reset == Device::Keyboard   ? N_("Reset keyboard")
                                : *reset == Device::Mouse      ? N_("Reset mouse")
                                                               : N_("Reset gamepad");
            if (ImGui::Button(Id(TL(label), "reset").c_str())) g_confirmReset = true;
            if (ImGui::IsItemHovered()) Tooltip(TL("Undo bind changes."));
        } else {
            ImGui::PushStyleColor(ImGui::ImGuiCol_Button, ImGui::ImVec4(0.65f, 0.16f, 0.16f, 1.0f));
            if (ImGui::Button(Id(TL("Confirm reset"), "confirmreset").c_str())) {
                ResetAll(reset);
                g_capture.reset();
                g_confirmReset = false;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button(Id(TL("Cancel"), "cancelreset").c_str())) g_confirmReset = false;
        }

        ImGui::SameLine();
        Muted(IsBusy() ? TL("Scanning...") : TL("Ready"));
        if (const auto st = GetStatus(); !st.empty()) Muted(st.c_str());
    }
}
