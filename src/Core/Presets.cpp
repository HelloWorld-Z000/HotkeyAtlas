// Presets: named sets of bind changes, switched in place.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- presets
    // A preset holds every bind change (see kPresetDir). The active one is rewritten with
    // each change (SaveConfigFile); switching loads another one in place of the current binds.

    std::string PresetName(std::string_view text)
    {
        auto name = Trim(text);
        std::erase_if(name, [](char c) { return static_cast<unsigned char>(c) < 32 || std::strchr("\\/:*?\"<>|", c); });
        name = Trim(name);
        while (!name.empty() && name.back() == '.') name.pop_back();  // Windows drops trailing dots
        return name;
    }

    std::vector<std::string> Presets()
    {
        std::vector<std::string> out;
        std::error_code          ec;
        for (fs::directory_iterator it(kPresetDir, ec), end; !ec && it != end; it.increment(ec))
            if (Lower(it->path().extension().string()) == ".ini") out.push_back(Utf8(it->path().stem()));
        std::ranges::sort(out, [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
        return out;
    }

    std::string ActivePreset()
    {
        std::lock_guard l(g_ovLock);
        return g_preset;
    }

    namespace
    {
        bool PresetExists(std::string_view name)
        {
            std::error_code ec;
            return !name.empty() && fs::exists(PresetPath(name), ec);
        }
    }

    // Writes the active preset's file when it has none yet (first start: Default).
    void EnsureActivePreset()
    {
        std::string name, binds, err;
        {
            std::lock_guard l(g_ovLock);
            name  = g_preset;
            binds = BindSetText(g_overrides, g_fileEdits, g_padControls, g_padMods);
        }
        if (!PresetExists(name) && !WritePresetFile(name, binds, err)) logger::warn("preset {}: {}", name, err);
    }

    bool NewPreset(std::string_view name, std::string& err)
    {
        if (name.empty()) return false;
        if (PresetExists(name)) return SwitchPreset(name, err);
        {
            std::lock_guard l(g_ovLock);
            g_preset = name;  // the next save writes the current binds into it
        }
        logger::info("preset created: {}", name);
        SaveConfigAsync(TLF("Preset created: {0}", { name }));
        return true;
    }

    bool SwitchPreset(std::string_view name, std::string& err)
    {
        const auto    path = PresetPath(name);
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            err = TLF("cannot read {0}", { Utf8(path) });
            return false;
        }
        BindSet     binds;
        std::string section, line;
        for (bool first = true; std::getline(in, line); first = false) {
            if (first && line.starts_with(kBom)) line.erase(0, kBom.size());
            const auto t = Trim(line);
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;
            if (t.front() == '[') {
                if (const auto r = t.find(']'); r != npos) section = Lower(t.substr(1, r - 1));
                continue;
            }
            if (const auto eq = t.rfind('='); eq != npos) ParseBindLine(section, Trim(t.substr(0, eq)), Trim(t.substr(eq + 1)), binds);
        }
        in.close();
        {
            // from now on saves go to the new preset; the task below then writes its binds there
            std::lock_guard l(g_ovLock);
            g_preset = name;
        }

        // game thread: the control map goes back to the game's keys, then gets the preset's
        SKSE::GetTaskInterface()->AddTask([binds = std::move(binds), name = std::string(name)]() mutable {
            RestoreControls(std::nullopt);
            {
                std::lock_guard l(g_ovLock);
                g_overrides   = std::move(binds.controls);
                g_fileEdits   = std::move(binds.files);
                g_padControls = std::move(binds.padControls);
                g_padMods     = std::move(binds.padMods);
                RebuildComboTableLocked();
            }
            ClearActiveInputs();
            ApplyOverrides();
            logger::info("preset loaded: {}", name);
            SaveConfigAsync(TLF("Preset loaded: {0}", { name }));
        });
        return true;
    }

    bool DeletePreset(std::string_view name, std::string& err)
    {
        if (Lower(std::string(name)) == Lower(ActivePreset())) {
            // the active one: switch to another first
            const auto all  = Presets();
            const auto next = std::ranges::find_if(all, [&](const std::string& p) { return Lower(p) != Lower(std::string(name)); });
            if (next == all.end()) {
                err = TL("The only preset can't be deleted.");
                return false;
            }
            if (!SwitchPreset(*next, err)) return false;
        }
        std::error_code ec;
        if (!fs::remove(PresetPath(name), ec) || ec) {
            err = TLF("cannot delete {0}", { Utf8(PresetPath(name)) });
            return false;
        }
        logger::info("preset deleted: {}", name);
        return true;
    }
}
