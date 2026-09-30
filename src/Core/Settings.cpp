// HotkeyAtlas.ini: bind changes, UI options, blacklist and the user's notes.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- control overrides
    // Skyrim controls changed through Hotkey Atlas live only in our own ini and are
    // applied to RE::ControlMap in memory. ControlMap.txt / ControlMap_Custom.txt are
    // never written by this plugin.


    std::mutex                      g_ovLock;
    std::map<std::string, Override> g_overrides;  // OverrideId() -> override (applied in memory)
    std::map<std::string, Override> g_fileEdits;  // FileEditId() -> edit made to a mod's own file
    // Gamepad buttons added to key / mouse bindings (key = pad code, original = the binding's
    // default code). The binding keeps its key; the input hook fires it from the pad too.
    std::map<std::string, Override> g_padControls;  // OverrideId()
    std::map<std::string, Override> g_padMods;      // FileEditId()
    namespace
    {
        std::atomic<bool>               g_hideVanilla{ false };
        std::atomic<std::uint32_t>      g_hiddenCols{ 0 };
        std::atomic<bool>               g_psLabels{ false };
        std::atomic<bool>               g_crossDevice{ true };

        // Mods (owner names, lower case) whose bindings are hidden. Swapped as a whole so the
        // render thread can read it without holding a lock.
        using Blacklist = std::set<std::string>;
        std::mutex                       g_blLock;
        std::shared_ptr<const Blacklist> g_blacklist = std::make_shared<const Blacklist>();

        // User notes, swapped as a whole like the blacklist.
        std::mutex                   g_noteLock;
        std::shared_ptr<const Notes> g_notes = std::make_shared<const Notes>();

        const fs::path kConfigPath = "Data/SKSE/Plugins/HotkeyAtlas.ini";
    }

    // Presets: Data/SKSE/Plugins/HotkeyAtlas/Presets/<name>.ini, the bind sections of
    // HotkeyAtlas.ini. The active one is rewritten with every change.
    const fs::path kPresetDir     = "Data/SKSE/Plugins/HotkeyAtlas/Presets";
    namespace
    {
        constexpr auto kDefaultPreset = "Default";
    }
    std::string    g_preset       = kDefaultPreset;  // active preset, guarded by g_ovLock

    // UTF-8 name -> file; names come from the user and may be Cyrillic
    fs::path PresetPath(std::string_view name)
    {
        return kPresetDir / fs::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size()) + u8".ini");
    }

    std::string Utf8(const fs::path& p)
    {
        const auto s = p.u8string();
        return std::string(reinterpret_cast<const char*>(s.data()), s.size());
    }

    bool WritePresetFile(std::string_view name, const std::string& binds, std::string& err)
    {
        std::error_code ec;
        fs::create_directories(kPresetDir, ec);
        const auto    path = PresetPath(name);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out || !(out << "; Hotkey Atlas preset: " << name << "\n\n" << binds)) {
            err = TLF("cannot write {0}", { Utf8(path) });
            return false;
        }
        return true;
    }

    // A control is identified by the key the game gave it, not by its array position:
    // the game keeps each context sorted by key, so positions move on every rebind.
    std::string OverrideId(int ctx, std::string_view action, std::uint32_t originalKey)
    {
        return std::to_string(ctx) + '|' + std::string(action) + '|' + std::to_string(originalKey);
    }

    std::string FileEditId(const Binding& b)
    {
        return b.origin + '|' + b.context + '|' + b.iniKey;
    }

    // Records a change so it shows as overridden and can be reset later.
    // Caller holds g_ovLock.
    void RecordChange(std::map<std::string, Override>& map, const std::string& id, std::uint32_t oldKey, std::uint32_t newKey)
    {
        const auto it       = map.find(id);
        const auto original = it != map.end() ? it->second.original : oldKey;
        if (newKey == original)
            map.erase(id);
        else
            map[id] = { newKey, original };
    }

    std::optional<std::uint32_t> ParseUInt(std::string_view s)
    {
        const auto t = Trim(s);
        unsigned   v = 0;
        const auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
        if (ec != std::errc{} || p != t.data() + t.size()) return std::nullopt;
        return v;
    }


    // One line of [ControlMap], [ModFiles], [ControlMapGamepad] or [ModFilesGamepad] into
    // `out`; `section` in lower case. False: not one of those sections.
    bool ParseBindLine(const std::string& section, const std::string& lhs, const std::string& rhs, BindSet& out)
    {
        const bool pad = section == "controlmapgamepad" || section == "modfilesgamepad";
        if (section != "controlmap" && section != "modfiles" && !pad) return false;

        // <a>|<b>|<c> = <key>|<original>
        const auto p1 = lhs.find('|');
        const auto p2 = lhs.rfind('|');
        const auto pr = rhs.find('|');
        if (p1 == npos || p2 == p1 || pr == npos) return true;
        const auto key = ParseUInt(rhs.substr(0, pr));
        const auto org = ParseUInt(rhs.substr(pr + 1));
        if (!key || !org || BaseCode(*key) >= kPadCode + 0x10000 || *org >= kPadCode + 0x10000) return true;  // input codes, see MakeCode
        if (WithHold(*key, HoldOf(*key)) != *key) return true;                                                   // a held input we can't read back
        if (TriggerOf(*key) > Trigger::Hold || TriggerOf(*org) != Trigger::Press) return true;                   // see Trigger
        if (pad && (CodeDevice(*key) != Device::Gamepad || CodeDevice(*org) == Device::Gamepad)) return true;

        if (section == "controlmap" || section == "controlmapgamepad") {
            // <ctx>|<event>|<original key>
            const auto ctx    = ParseUInt(lhs.substr(0, p1));
            const auto idKey  = ParseUInt(lhs.substr(p2 + 1));
            const auto action = Trim(lhs.substr(p1 + 1, p2 - p1 - 1));
            if (!ctx || !idKey || action.empty() || *idKey != *org) return true;  // also drops the old <occurrence> format
            (pad ? out.padControls : out.controls)[OverrideId(static_cast<int>(*ctx), action, *org)] = { *key, *org };
        } else {
            // <file>|<section>|<setting>; older versions stored "../../MO2/mods/<mod>/SKSE/..." under MO2
            auto id = lhs;
            if (id.starts_with("..")) {
                const auto low = Lower(id.substr(0, id.find('|')));
                for (auto root : { "/skse/plugins/", "/mcm/settings/" })
                    if (const auto p = low.find(root); p != npos) {
                        id.erase(0, p + 1);
                        break;
                    }
            }
            (pad ? out.padMods : out.files)[id] = { *key, *org };
        }
        return true;
    }

    // The four bind sections, with their comments. Caller holds g_ovLock for the globals.
    std::string BindSetText(const std::map<std::string, Override>& controls, const std::map<std::string, Override>& files,
        const std::map<std::string, Override>& padControls, const std::map<std::string, Override>& padMods)
    {
        const auto lines = [](const std::map<std::string, Override>& map) {
            std::string s;
            for (const auto& [id, ov] : map) s += id + " = " + std::to_string(ov.key) + '|' + std::to_string(ov.original) + '\n';
            return s;
        };
        return "; Skyrim control overrides. Applied in memory at runtime; ControlMap.txt and\n"
               "; ControlMap_Custom.txt are never modified. Delete a line to return that control\n"
               "; to the game's own binding.\n"
               "; <context>|<event>|<original DIK key> = <DIK key>|<original DIK key>\n"
               "[ControlMap]\n" +
               lines(controls) +
               "\n"
               "; Mod keys moved by the user. The mods' own config files are never modified: while\n"
               "; the game runs, pressing the new combo is shown to the mod as its original one.\n"
               "; <file>|<section>|<setting> = <combo>|<original combo>   (combo = DIK key + 256*Shift + 512*Ctrl + 1024*Alt;\n"
               "; mouse 65536 + button, gamepad 131072 + button; a key or button held first is packed above 1048576)\n"
               "[ModFiles]\n" +
               lines(files) +
               "\n"
               "; Gamepad buttons added to Skyrim keyboard controls; the key keeps working.\n"
               "; <context>|<event>|<original key> = <gamepad code>|<original key>   (gamepad code = 131072 + button id)\n"
               "[ControlMapGamepad]\n" +
               lines(padControls) +
               "\n"
               "; Gamepad buttons added to mod keys; the key keeps working.\n"
               "; <file>|<section>|<setting> = <gamepad code>|<original combo>\n"
               "[ModFilesGamepad]\n" +
               lines(padMods);
    }

    namespace
    {
        void LoadConfigFile()
        {
            std::ifstream in(kConfigPath, std::ios::binary);
            if (!in) return;

            BindSet                         binds;
            std::string                     preset = kDefaultPreset;
            bool                            hide = false;
            auto                            blacklist = std::make_shared<Blacklist>();
            auto                            notes     = std::make_shared<Notes>();
            std::string                     section, line;
            bool                            first = true;
            while (std::getline(in, line)) {
                if (first && line.starts_with(kBom)) line.erase(0, kBom.size());
                first        = false;
                const auto t = Trim(line);
                if (t.empty() || t[0] == ';' || t[0] == '#') continue;
                if (t.front() == '[') {
                    if (const auto r = t.find(']'); r != npos) section = Lower(t.substr(1, r - 1));
                    continue;
                }
                if (section == "notes") {
                    // <id> = <free text>: the id has no '=', the text may
                    const auto eq = t.find('=');
                    if (eq == npos) continue;
                    const auto id = Trim(t.substr(0, eq));
                    auto       note = Trim(t.substr(eq + 1));
                    if (!id.empty() && !note.empty()) (*notes)[id] = std::move(note);
                    continue;
                }
                const auto eq = t.rfind('=');
                if (eq == npos) continue;
                const auto lhs = Trim(t.substr(0, eq));
                const auto rhs = Trim(t.substr(eq + 1));

                if (section == "ui") {
                    if (Lower(lhs) == "bhidevanilla") hide = rhs == "1" || Lower(rhs) == "true";
                    if (Lower(lhs) == "bplaystationlabels") g_psLabels = rhs == "1" || Lower(rhs) == "true";
                    if (Lower(lhs) == "bshowcrossdevice") g_crossDevice = rhs == "1" || Lower(rhs) == "true";
                    if (Lower(lhs) == "slanguage") {
                        std::lock_guard tl(g_trLock);
                        g_language = Lower(rhs);
                    }
                    if (Lower(lhs) == "shiddencolumns") {
                        // comma separated column names
                        std::uint32_t mask = 0;
                        const auto    list = Lower(rhs);
                        for (const auto& col : kOptionalColumns)
                            for (std::size_t p = 0; p < list.size();) {
                                auto e = list.find(',', p);
                                if (e == npos) e = list.size();
                                if (Trim(std::string_view(list).substr(p, e - p)) == Lower(col.name)) mask |= col.bit;
                                p = e + 1;
                            }
                        g_hiddenCols = mask;
                    }
                    continue;
                }
                if (section == "presets") {
                    if (Lower(lhs) == "sactive" && !rhs.empty()) preset = rhs;
                    continue;
                }
                if (section == "blacklist") {
                    if (rhs == "1" || Lower(rhs) == "true") {
                        auto owner = Lower(lhs);
                        if (owner == "creation club") owner = "skyrim - creation club";  // renamed
                        blacklist->insert(std::move(owner));
                    }
                    continue;
                }
                ParseBindLine(section, lhs, rhs, binds);
            }

            {
                std::lock_guard l(g_blLock);
                g_blacklist = std::move(blacklist);
            }
            {
                std::lock_guard l(g_noteLock);
                g_notes = std::move(notes);
            }
            std::lock_guard l(g_ovLock);
            g_overrides   = std::move(binds.controls);
            g_fileEdits   = std::move(binds.files);
            g_padControls = std::move(binds.padControls);
            g_padMods     = std::move(binds.padMods);
            g_preset      = std::move(preset);
            g_hideVanilla = hide;
        }
    }

    bool SaveConfigFile(std::string& err)
    {
        std::string text =
            "; Hotkey Atlas settings.\n"
            "\n"
            "[UI]\n"
            "bHideVanilla = ";
        text += g_hideVanilla ? "1\n" : "0\n";
        text += "; gamepad shown with PlayStation names (L1, Cross...) instead of Xbox ones (LB, A...)\n"
                "bPlayStationLabels = ";
        text += g_psLabels ? "1\n" : "0\n";
        text += "; key column also shows the gamepad button added to a key (T / RB), and on the gamepad the key;\n"
                "; off: All hotkeys leaves out the gamepad-only binds\n"
                "bShowCrossDevice = ";
        text += g_crossDevice ? "1\n" : "0\n";
        text += "; table columns to hide: Action, Note, Mod, Context, Source\n"
                "sHiddenColumns = ";
        {
            std::string list;
            for (const auto& col : kOptionalColumns)
                if (g_hiddenCols & col.bit) list += (list.empty() ? "" : ", ") + std::string(col.name);
            text += list + '\n';
        }
        text += "; interface language: a file name from SKSE/Plugins/HotkeyAtlas/Translations without .txt;\n"
                "; empty = the game's language (sLanguage), English when there is no such file\n"
                "sLanguage = ";
        {
            std::lock_guard l(g_trLock);
            text += g_language + '\n';
        }

        text +=
            "\n"
            "; Mods whose hotkeys are hidden in the menu (their remaps keep working).\n"
            "[Blacklist]\n";
        {
            std::shared_ptr<const Blacklist> bl;
            {
                std::lock_guard l(g_blLock);
                bl = g_blacklist;
            }
            for (const auto& owner : *bl) text += owner + " = 1\n";
        }
        text +=
            "\n"
            "; Your notes on hotkeys, shown in the Note column.\n"
            "; ControlMap|<context>|<event> = <text>   or   <file>|<section>|<setting> = <text>\n"
            "[Notes]\n";
        {
            std::shared_ptr<const Notes> notes;
            {
                std::lock_guard l(g_noteLock);
                notes = g_notes;
            }
            for (const auto& [id, note] : *notes)
                if (id.find('=') == npos) text += id + " = " + note + '\n';
        }
        std::string binds, preset;
        {
            std::lock_guard l(g_ovLock);
            binds  = BindSetText(g_overrides, g_fileEdits, g_padControls, g_padMods);
            preset = g_preset;
        }
        text += "\n; the active preset (SKSE/Plugins/HotkeyAtlas/Presets), which gets every change\n"
                "[Presets]\n"
                "sActive = " +
                preset + "\n\n" + binds;

        std::error_code ec;
        fs::create_directories(kConfigPath.parent_path(), ec);
        std::ofstream out(kConfigPath, std::ios::binary | std::ios::trunc);
        if (!out || !(out << text)) {
            err = TLF("cannot write {0}", { kConfigPath.generic_string() });
            return false;
        }
        out.close();
        return WritePresetFile(preset, binds, err);
    }

    void LoadConfig()
    {
        LoadConfigFile();
        EnsureActivePreset();
    }

    std::shared_ptr<const std::set<std::string>> GetBlacklist()
    {
        std::lock_guard l(g_blLock);
        return g_blacklist;
    }

    void SaveConfigQuiet()
    {
        std::thread([] {
            std::string err;
            if (!SaveConfigFile(err)) SetStatus(TLF("Saving HotkeyAtlas.ini failed: {0}", { err }));
        }).detach();
    }

    void SetBlacklisted(const std::vector<std::string>& owners, bool hidden)
    {
        {
            std::lock_guard l(g_blLock);
            auto            next = std::make_shared<Blacklist>(*g_blacklist);
            for (const auto& owner : owners) {
                if (hidden)
                    next->insert(Lower(owner));
                else
                    next->erase(Lower(owner));
            }
            g_blacklist = std::move(next);
        }
        SaveConfigQuiet();
    }

    bool HideVanilla() { return g_hideVanilla.load(); }

    bool PlayStationLabels() { return g_psLabels.load(); }

    void SetPlayStationLabels(bool ps)
    {
        if (g_psLabels.exchange(ps) != ps) SaveConfigQuiet();
    }

    bool ShowCrossDevice() { return g_crossDevice.load(); }

    void SetShowCrossDevice(bool show)
    {
        if (g_crossDevice.exchange(show) != show) SaveConfigQuiet();
    }

    std::uint32_t HiddenColumns() { return g_hiddenCols.load(); }

    void SetHiddenColumns(std::uint32_t mask)
    {
        if (g_hiddenCols.exchange(mask) != mask) SaveConfigQuiet();
    }

    std::string NoteId(const Binding& b)
    {
        if (b.kind == Kind::ControlMap) return "ControlMap|" + std::to_string(b.ctx) + '|' + b.action;
        return FileEditId(b);
    }

    std::shared_ptr<const Notes> GetNotes()
    {
        std::lock_guard l(g_noteLock);
        return g_notes;
    }

    void SetNote(const std::string& id, std::string text)
    {
        // one ini line per note
        std::ranges::replace_if(text, [](char c) { return c == '\r' || c == '\n'; }, ' ');
        text = Trim(text);
        {
            std::lock_guard l(g_noteLock);
            const auto      it = g_notes->find(id);
            if (text.empty() ? it == g_notes->end() : it != g_notes->end() && it->second == text) return;
            auto next = std::make_shared<Notes>(*g_notes);
            if (text.empty())
                next->erase(id);
            else
                (*next)[id] = std::move(text);
            g_notes = std::move(next);
        }
        SaveConfigQuiet();
    }

    void SetHideVanilla(bool hide)
    {
        if (g_hideVanilla.exchange(hide) == hide) return;
        std::thread([] {
            std::string err;
            if (!SaveConfigFile(err)) SetStatus(TLF("Saving HotkeyAtlas.ini failed: {0}", { err }));
        }).detach();
    }
}
