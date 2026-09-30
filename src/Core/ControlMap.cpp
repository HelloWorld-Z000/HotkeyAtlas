// The game's control map: reading it and moving Skyrim controls in memory.

#include "Internal.h"

namespace HA
{
    using Mappings = RE::BSTArray<RE::ControlMap::UserEventMapping>;

    // The game finds the event for a pressed key with a binary search over this array
    // (see ControlMap::GetUserEventName), so it must stay sorted by inputKey after any
    // change. Stable insertion sort: tiny arrays, and equal keys keep their order.
    void SortByKey(Mappings& maps)
    {
        for (std::uint32_t i = 1; i < maps.size(); ++i)
            for (std::uint32_t j = i; j > 0 && maps[j - 1].inputKey > maps[j].inputKey; --j) std::swap(maps[j - 1], maps[j]);
    }

    // Key stored in the game's control map for a combo. The game has no modifier support,
    // so combos are unbound there (0xFF) and fired by our input hook instead.
    // Value for a control map array of device `in`. Mouse and gamepad codes store their
    // button id. A code of another device (a key moved to the mouse) leaves the array
    // unbound: the input hook fires it, like combos. So does a button action put on a stick
    // (`original` is the control's own code): a stick sends thumbstick events, which button
    // handlers ignore, so the hook turns pushing it into presses.
    std::uint16_t PhysicalKey(std::uint32_t code, Device in, std::uint32_t original)
    {
        if (code == kUnbound || CodeDevice(code) != in || HoldOf(code)) return 0xFF;  // a combo with a held input: the hook
        if (TriggerOf(code) != Trigger::Press) return 0xFF;                           // double tap / hold: the hook too
        if (IsStick(code) && !IsStick(original)) return 0xFF;
        if (in != Device::Keyboard) return static_cast<std::uint16_t>(CodeId(code));
        return ComboMods(code) ? 0xFF : static_cast<std::uint16_t>(ComboKey(code));
    }

    namespace
    {
        // The device arrays of the control map, in Device order.
        constexpr RE::INPUT_DEVICES::INPUT_DEVICE kGameDevices[] = { RE::INPUT_DEVICES::kKeyboard, RE::INPUT_DEVICES::kMouse, RE::INPUT_DEVICES::kGamepad };
    }

    Mappings* DeviceMappings(RE::ControlMap* cm, int c, Device d)
    {
        auto* ic = cm->controlMap[c];
        return ic ? &ic->deviceMappings[kGameDevices[static_cast<int>(d)]] : nullptr;
    }

    // Override whose target is what this mapping has now, i.e. it was applied. Overrides of
    // the same event on other devices are told apart by the device of their original code.
    // Caller holds g_ovLock.
    const Override* AppliedOverride(int c, const RE::ControlMap::UserEventMapping& m, Device d)
    {
        const auto prefix = std::to_string(c) + '|' + m.eventID.c_str() + '|';
        for (auto it = g_overrides.lower_bound(prefix); it != g_overrides.end() && it->first.starts_with(prefix); ++it)
            if (CodeDevice(it->second.original) == d && PhysicalKey(it->second.key, d, it->second.original) == m.inputKey) return &it->second;
        return nullptr;
    }

    // Must run on the game thread. The game rebuilds the control map when controls are
    // reset or remapped in its own menu, so this is re-run on every scan and game load.
    void ApplyOverrides()
    {
        auto* cm = RE::ControlMap::GetSingleton();
        if (!cm) return;
        std::lock_guard l(g_ovLock);
        RebuildComboTableLocked();
        for (int c = 0; c < ContextCount() && !g_overrides.empty(); ++c) {
            for (const auto d : kDevices) {
                auto* maps = DeviceMappings(cm, c, d);
                if (!maps) continue;
                bool changed = false;
                for (auto& m : *maps) {
                    const char* name = m.eventID.c_str();
                    if (!name || !*name || m.inputKey == 0xFF || m.inputKey == 0xFFFF) continue;
                    // still on the game's key -> move it to ours
                    const auto it = g_overrides.find(OverrideId(c, name, MakeCode(d, m.inputKey)));
                    if (it == g_overrides.end() || PhysicalKey(it->second.key, d, it->second.original) == m.inputKey) continue;
                    m.inputKey = PhysicalKey(it->second.key, d, it->second.original);
                    changed    = true;
                }
                if (changed) SortByKey(*maps);
            }
        }
        RebuildComboTableLocked();
    }

    // Must run on the game thread.
    std::vector<Binding> ReadControlMap()
    {
        std::vector<Binding> out;
        auto*                cm = RE::ControlMap::GetSingleton();
        if (!cm) return out;

        std::lock_guard l(g_ovLock);
        for (int c = 0; c < ContextCount(); ++c) {
            for (const auto d : kDevices) {
                auto* maps = DeviceMappings(cm, c, d);
                if (!maps) continue;
                for (std::uint32_t i = 0; i < maps->size(); ++i) {
                    const auto& m    = (*maps)[i];
                    const char* name = m.eventID.c_str();
                    if (!name || !*name) continue;
                    const auto* ov = AppliedOverride(c, m, d);
                    // 0xFF / 0xFFFF mean unbound (combos are unbound in the game and live in `ov`);
                    // controls the user unbound stay listed so they can be bound or reset again
                    const bool unbound = d == Device::Keyboard ? m.inputKey >= 255 : m.inputKey == 0xFF || m.inputKey == 0xFFFF;
                    // (a combo, or a key moved to the mouse, leaves the array unbound too)
                    if (unbound && !(ov && (ov->key == kUnbound || PhysicalKey(ov->key, d, ov->original) == 0xFF))) continue;

                    Binding b;
                    b.device = d;
                    if (ov && ov->key == kUnbound) {
                        b.key = kUnbound;
                    } else if (ov && CodeDevice(ov->key) != Device::Keyboard) {
                        b.device = CodeDevice(ov->key);  // shown where it is pressed now
                        b.key    = CodeId(ov->key);
                    } else if (d == Device::Keyboard) {
                        b.key  = ov ? ComboKey(ov->key) : static_cast<std::uint32_t>(m.inputKey);
                        b.mods = ov ? ComboMods(ov->key) : 0;
                    } else {
                        b.key = m.inputKey;
                    }
                    if (ov) {
                        b.hold    = HoldOf(ov->key);  // a combo with a held key or button
                        b.trigger = TriggerOf(ov->key);
                    }
                    b.action      = name;
                    // Creation Club controls get their own owner so they can be hidden in the blacklist
                    b.owner       = VanillaOwner(c);
                    b.context     = ContextName(c);
                    b.contextHint = ContextHint(c);
                    b.description = DescribeControl(c, b.action, d);
                    b.origin      = "ControlMap";
                    b.kind        = Kind::ControlMap;
                    // overrides live in our ini, so non-remappable controls are fine too;
                    // mouse movement (Look, Cursor...) is no button to move
                    b.editable    = !(d == Device::Mouse && m.inputKey == kMouseMove);
                    b.ctx         = c;
                    b.index       = static_cast<int>(i);
                    b.defaultKey  = CurrentCode(b);
                    if (ov) {
                        b.overridden = true;
                        b.defaultKey = ov->original;
                        b.origin     = "SKSE/Plugins/HotkeyAtlas.ini";
                    }
                    out.push_back(std::move(b));
                }
            }
        }
        return out;
    }
}
