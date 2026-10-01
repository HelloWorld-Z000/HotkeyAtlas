// Rebinding, gamepad buttons added to keys, and resetting to the defaults.

#include "Internal.h"

namespace HA
{
    // okStatus: shown after a successful save; empty clears the status line
    void SaveConfigAsync(std::string okStatus)
    {
        std::thread([okStatus = std::move(okStatus)] {
            std::string err;
            if (SaveConfigFile(err)) {
                SetStatus(okStatus);
            } else {
                SetStatus(TLF("Saving HotkeyAtlas.ini failed: {0}", { err }));
            }
            Rescan();
        }).detach();
    }

    void Rebind(const Binding& binding, std::uint32_t newCombo)
    {
        if (newCombo == CurrentCode(binding)) return;
        if (!binding.editable && !(binding.overridden && newCombo == binding.defaultKey)) return;  // read-only: Reset only

        // A YAML hotkey file (SkyrimNet): the mod reads the keyboard itself, past the input hook,
        // so the new key goes into its file, which it reloads at once. One plain key or mouse button.
        if (binding.kind == Kind::Yaml) {
            if (!YamlValue(newCombo)) {
                SetStatus(TLF("{0} takes a single key or mouse button only: no Shift / Ctrl / Alt, combos, gamepad, double tap or hold.",
                    { binding.owner }));
                return;
            }
            SKSE::GetTaskInterface()->AddTask([b = binding, newCombo] {
                const auto  id = FileEditId(b);
                std::string err;
                if (!WriteYamlKey(id, newCombo, err)) {
                    SetStatus(TLF("Rebind failed: {0}", { err }));
                    return;
                }
                {
                    std::lock_guard l(g_ovLock);
                    RecordChange(g_fileEdits, id, b.defaultKey, newCombo);
                }
                SaveConfigAsync();
            });
            return;
        }

        // a mouse button stays on the mouse, a gamepad button on the gamepad (a stick included);
        // a key may also go to the mouse (see NewMousePress). A gamepad button for a key or mouse
        // action is added next to it instead, see BindGamepad.
        const auto home = CodeDevice(binding.defaultKey);
        if (newCombo != kUnbound && CodeDevice(newCombo) != home && !(home == Device::Keyboard && CodeDevice(newCombo) == Device::Mouse)) return;
        // a stick action (movement, camera) only moves to the other stick
        if (newCombo != kUnbound && IsStick(binding.defaultKey) && (!IsStick(BaseCode(newCombo)) || HoldOf(newCombo))) return;
        // double tap / hold need a press and a release: no stick, no wheel
        if (TriggerOf(newCombo) != Trigger::Press && (IsStick(BaseCode(newCombo)) || IsWheel(BaseCode(newCombo)))) return;
        // combos: gamepad with gamepad, keyboard and mouse with each other
        if (const auto h = HoldOf(newCombo); h && (CodeDevice(h) == Device::Gamepad) != (CodeDevice(newCombo) == Device::Gamepad)) return;

        if (binding.kind == Kind::ControlMap) {
            // combos, keys moved to the mouse and buttons moved to a stick are fired by the input hook
            if (newCombo != kUnbound && PhysicalKey(newCombo, home, binding.defaultKey) == 0xFF && !InputHookInstalled()) {
                SetStatus(TL("Combos for Skyrim controls need the input hook, which failed to install (see HotkeyAtlas.log)."));
                return;
            }
            SKSE::GetTaskInterface()->AddTask([b = binding, newCombo, home] {
                const auto newKey = PhysicalKey(newCombo, home, b.defaultKey);  // 0xFF: combo, unbound, moved to the mouse or a stick
                auto*      cm     = RE::ControlMap::GetSingleton();
                auto*      maps   = cm && b.ctx >= 0 && b.ctx < ContextCount() ? DeviceMappings(cm, b.ctx, home) : nullptr;

                // find by event + current key; indices shift whenever the array is re-sorted
                RE::ControlMap::UserEventMapping* target = nullptr;
                std::string                       conflicts;
                if (maps) {
                    for (auto& m : *maps) {
                        const char* name = m.eventID.c_str();
                        if (!name || !*name) continue;
                        if (!target && b.action == name && m.inputKey == PhysicalKey(CurrentCode(b), home, b.defaultKey))
                            target = &m;
                        else if (newKey != 0xFF && m.inputKey == newKey)
                            conflicts += (conflicts.empty() ? "" : ", ") + std::string(name);
                    }
                }
                if (!target) {
                    SetStatus(TL("Rebind failed: the control map changed, rescan and try again."));
                    Rescan();
                    return;
                }

                {
                    std::lock_guard l(g_ovLock);
                    RecordChange(g_overrides, OverrideId(b.ctx, b.action, b.defaultKey), b.defaultKey, newCombo);
                    RebuildComboTableLocked();
                }
                target->inputKey = newKey;
                SortByKey(*maps);  // without this the game's key lookup can't find the new key

                std::string msg;  // silent on success, only a key conflict is worth a message
                if (!conflicts.empty())
                    msg = TLF("In {0} this bind is also used by: {1}. The game may ignore one of them.", { b.context, conflicts });

                // a key moved to a mouse button takes that button over in this context
                if (home == Device::Keyboard && newCombo != kUnbound && CodeDevice(newCombo) == Device::Mouse && !HoldOf(newCombo) &&
                    TriggerOf(newCombo) == Trigger::Press) {
                    std::string taken;
                    if (auto* mouse = DeviceMappings(cm, b.ctx, Device::Mouse))
                        for (const auto& m : *mouse)
                            if (m.inputKey == CodeId(newCombo) && m.eventID.c_str() && *m.eventID.c_str())
                                taken += (taken.empty() ? "" : ", ") + std::string(m.eventID.c_str());
                    if (!taken.empty())
                        msg = TLF("In {0} this mouse button was used by: {1}. It now does {2} there instead.", { b.context, taken, b.action });
                }
                SaveConfigAsync(std::move(msg));
            });
            return;
        }

        // Mod settings: the mod's file is never touched. The remap lives in our ini and the
        // input hook shows the mod its original key whenever the new one is pressed.
        if (!InputHookInstalled()) {
            SetStatus(TL("Rebinding mod binds needs the input hook, which failed to install (see HotkeyAtlas.log)."));
            return;
        }
        SKSE::GetTaskInterface()->AddTask([b = binding, newCombo] {  // game thread: the hook reads the table there
            {
                std::lock_guard l(g_ovLock);
                RecordChange(g_fileEdits, FileEditId(b), b.defaultKey, newCombo);
                RebuildComboTableLocked();
            }
            SaveConfigAsync();
        });
    }

    namespace
    {
        // The gamepad mappings of the same Skyrim event in the same context: Jump on the keyboard
        // and Jump on the gamepad are one action, only the key differs per device. An event can have
        // more than one (Favorites: d-pad up and down).
        std::vector<const Binding*> PadTwins(const Model& model, const Binding& b)
        {
            std::vector<const Binding*> out;
            if (b.kind != Kind::ControlMap) return out;
            for (const auto& t : model.all)
                if (t.kind == Kind::ControlMap && t.ctx == b.ctx && CodeDevice(t.defaultKey) == Device::Gamepad && t.action == b.action) out.push_back(&t);
            return out;
        }
    }

    void BindGamepad(const Binding& binding, std::uint32_t padCode)
    {
        if (padCode == binding.padKey || !binding.editable || CodeDevice(binding.defaultKey) == Device::Gamepad) return;
        if (binding.kind == Kind::Yaml) {  // its mod reads the keyboard itself, see Rebind
            SetStatus(TLF("{0} takes a single key or mouse button only: no Shift / Ctrl / Alt, combos, gamepad, double tap or hold.",
                { binding.owner }));
            return;
        }
        if (padCode != kUnbound && CodeDevice(padCode) != Device::Gamepad) return;
        if (const auto h = HoldOf(padCode); h && CodeDevice(h) != Device::Gamepad) return;
        if (TriggerOf(padCode) != Trigger::Press && IsStick(BaseCode(padCode))) return;  // see Rebind
        // a Skyrim action that has a gamepad mapping of its own gets that one rebound instead of a
        // second, added button: one action, one gamepad button
        if (padCode != kUnbound) {
            const auto model = GetModel();
            if (const auto twins = PadTwins(*model, binding); !twins.empty()) {
                if (binding.padKey != kUnbound) BindGamepad(binding, kUnbound);  // one added by an older version
                // already one of its own buttons: nothing to add
                for (const auto* t : twins)
                    if (CurrentCode(*t) == padCode) return;
                const auto it = std::find_if(twins.begin(), twins.end(), [](const Binding* t) { return t->editable; });
                if (it == twins.end()) {
                    SetStatus(TLF("{0} in {1} already has its own gamepad button, which can't be changed here.", { binding.action, binding.context }));
                    return;
                }
                Rebind(**it, padCode);
                return;
            }
        }
        if (!InputHookInstalled()) {
            SetStatus(TL("Gamepad buttons for keyboard and mouse actions need the input hook, which failed to install (see HotkeyAtlas.log)."));
            return;
        }
        SKSE::GetTaskInterface()->AddTask([b = binding, padCode] {  // game thread: the hook reads the table there
            {
                std::lock_guard l(g_ovLock);
                const bool control = b.kind == Kind::ControlMap;
                auto&      map     = control ? g_padControls : g_padMods;
                const auto id      = control ? OverrideId(b.ctx, b.action, b.defaultKey) : FileEditId(b);
                if (padCode == kUnbound)
                    map.erase(id);
                else
                    map[id] = { padCode, b.defaultKey };
                RebuildComboTableLocked();
            }
            SaveConfigAsync();
        });
    }


    // Puts the Skyrim controls we moved (on `only`, none = every device) back on the game's
    // keys in the control map; the override records stay. Game thread. Returns how many.
    std::size_t RestoreControls(std::optional<Device> only)
    {
        auto* cm = RE::ControlMap::GetSingleton();
        if (!cm) return 0;
        std::size_t     count = 0;
        std::lock_guard l(g_ovLock);
        for (int c = 0; c < ContextCount(); ++c) {
            for (const auto d : kDevices) {
                if (only && d != *only) continue;
                auto* maps = DeviceMappings(cm, c, d);
                if (!maps) continue;
                bool changed = false;
                for (auto& m : *maps) {
                    const char* name = m.eventID.c_str();
                    if (!name || !*name) continue;
                    if (const auto* ov = AppliedOverride(c, m, d)) {
                        m.inputKey = PhysicalKey(ov->original, d);
                        changed    = true;
                        ++count;
                    }
                }
                if (changed) SortByKey(*maps);
            }
        }
        return count;
    }


    void ResetAll(std::optional<Device> only)
    {
        SKSE::GetTaskInterface()->AddTask([only] {  // game thread: control map and hook tables
            // a change belongs to the device its binding came from (a key moved to the mouse is
            // the keyboard's); gamepad buttons added to keys and mouse buttons are the gamepad's
            const auto mine = [&](const Override& ov) { return !only || CodeDevice(ov.original) == *only; };
            const bool pad  = !only || *only == Device::Gamepad;

            const auto controls = RestoreControls(only);

            // mod remaps only exist in our ini: dropping them is enough
            std::size_t mods = 0, pads = 0;
            Overrides   filesBefore, filesAfter;  // keys written into YAML files go back there
            {
                std::lock_guard l(g_ovLock);
                std::erase_if(g_overrides, [&](const auto& e) { return mine(e.second); });
                filesBefore = g_fileEdits;
                mods = std::erase_if(g_fileEdits, [&](const auto& e) { return mine(e.second); });
                filesAfter = g_fileEdits;
                if (pad) {
                    pads = g_padControls.size() + g_padMods.size();
                    g_padControls.clear();
                    g_padMods.clear();
                }
                RebuildComboTableLocked();
            }
            SyncYamlFiles(filesBefore, filesAfter);
            ClearActiveInputs();
            logger::info("reset to defaults ({}): {} Skyrim control(s), {} mod key(s), {} added gamepad button(s)",
                !only ? "all" : *only == Device::Keyboard ? "keyboard" : *only == Device::Mouse ? "mouse" : "gamepad", controls, mods, pads);
            SaveConfigAsync();
        });
    }
}
