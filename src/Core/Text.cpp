// Text helpers and key parsing: names, numbers, SKSE button codes, Windows VK codes.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- text helpers

    std::string Trim(std::string_view s)
    {
        constexpr auto ws = " \t\r\n";
        const auto     b  = s.find_first_not_of(ws);
        if (b == std::string_view::npos) return {};
        const auto e = s.find_last_not_of(ws);
        return std::string(s.substr(b, e - b + 1));
    }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // "iToggleHotkey:Main" -> "Toggle Hotkey"
    std::string Humanize(std::string name)
    {
        if (const auto p = name.find(':'); p != npos) name.erase(p);
        if (name.size() > 2 && std::strchr("ibfsu", name[0]) && std::isupper(static_cast<unsigned char>(name[1]))) name.erase(0, 1);
        std::string out;
        for (std::size_t i = 0; i < name.size(); ++i) {
            const char c = name[i];
            if (c == '_') {
                out += ' ';
                continue;
            }
            const auto prev = static_cast<unsigned char>(i ? name[i - 1] : ' ');
            if (i && std::isupper(static_cast<unsigned char>(c)) && (std::islower(prev) || std::isdigit(prev))) out += ' ';
            out += c;
        }
        return out;
    }

    // Setting names that plausibly hold a key code.
    bool IsKeyLike(const std::string& name)
    {
        // Hungarian prefixes b/f/s = bool/float/string, not an integer key code
        if (name.size() > 2 && std::strchr("bfs", name[0]) && std::isupper(static_cast<unsigned char>(name[1]))) return false;
        const auto l = Lower(name);
        if (l.find("keyword") != npos) return false;
        for (auto w : { "key", "shortcut", "button" })
            if (l.find(w) != npos) return true;
        return false;
    }

    namespace
    {
        // Key names some mods write instead of a code (SKSE Menu Framework: ToggleKey = F1).
        struct KeyName
        {
            const char*   name;  // compared with NormKeyName()
            std::uint32_t dik;
        };
        constexpr KeyName kKeyNames[] = {
            { "escape", 1 }, { "esc", 1 }, { "minus", 12 }, { "equals", 13 }, { "backspace", 14 }, { "tab", 15 },
            { "q", 16 }, { "w", 17 }, { "e", 18 }, { "r", 19 }, { "t", 20 }, { "y", 21 }, { "u", 22 }, { "i", 23 }, { "o", 24 }, { "p", 25 },
            { "leftbracket", 26 }, { "rightbracket", 27 }, { "enter", 28 }, { "return", 28 },
            { "leftcontrol", 29 }, { "lcontrol", 29 }, { "leftctrl", 29 }, { "lctrl", 29 }, { "ctrl", 29 }, { "control", 29 },
            { "a", 30 }, { "s", 31 }, { "d", 32 }, { "f", 33 }, { "g", 34 }, { "h", 35 }, { "j", 36 }, { "k", 37 }, { "l", 38 },
            { "semicolon", 39 }, { "apostrophe", 40 }, { "grave", 41 }, { "tilde", 41 }, { "`", 41 }, { "~", 41 },
            { "leftshift", 42 }, { "lshift", 42 }, { "shift", 42 }, { "backslash", 43 },
            { "z", 44 }, { "x", 45 }, { "c", 46 }, { "v", 47 }, { "b", 48 }, { "n", 49 }, { "m", 50 },
            { "comma", 51 }, { "period", 52 }, { "slash", 53 }, { "rightshift", 54 }, { "rshift", 54 },
            { "numpadmultiply", 55 }, { "multiply", 55 }, { "leftalt", 56 }, { "lalt", 56 }, { "alt", 56 },
            { "space", 57 }, { "spacebar", 57 }, { "capslock", 58 },
            { "f1", 59 }, { "f2", 60 }, { "f3", 61 }, { "f4", 62 }, { "f5", 63 }, { "f6", 64 }, { "f7", 65 }, { "f8", 66 }, { "f9", 67 }, { "f10", 68 },
            { "numlock", 69 }, { "scrolllock", 70 },
            { "numpad7", 71 }, { "numpad8", 72 }, { "numpad9", 73 }, { "numpadminus", 74 }, { "numpadsubtract", 74 },
            { "numpad4", 75 }, { "numpad5", 76 }, { "numpad6", 77 }, { "numpadplus", 78 }, { "numpadadd", 78 },
            { "numpad1", 79 }, { "numpad2", 80 }, { "numpad3", 81 }, { "numpad0", 82 }, { "numpaddecimal", 83 }, { "numpadperiod", 83 },
            { "f11", 87 }, { "f12", 88 }, { "numpadenter", 156 }, { "rightcontrol", 157 }, { "rcontrol", 157 }, { "rightctrl", 157 }, { "rctrl", 157 },
            { "numpaddivide", 181 }, { "divide", 181 }, { "printscreen", 183 }, { "prtsc", 183 }, { "rightalt", 184 }, { "ralt", 184 },
            { "pause", 197 }, { "home", 199 }, { "up", 200 }, { "uparrow", 200 }, { "pageup", 201 }, { "pgup", 201 },
            { "left", 203 }, { "leftarrow", 203 }, { "right", 205 }, { "rightarrow", 205 }, { "end", 207 },
            { "down", 208 }, { "downarrow", 208 }, { "pagedown", 209 }, { "pgdn", 209 }, { "insert", 210 }, { "delete", 211 },
            { "leftwin", 219 }, { "lwin", 219 }, { "rightwin", 220 }, { "rwin", 220 }, { "apps", 221 },
        };

        // "Page_Up" / "DIK_PGUP" / "Num Pad 7" -> "pageup" / "pgup" / "numpad7"
        std::string NormKeyName(std::string_view s)
        {
            std::string out;
            for (const char c : s)
                if (c != ' ' && c != '_') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (const auto prefix : { "dik", "key", "vk" })
                if (out.size() > std::strlen(prefix) + 1 && out.starts_with(prefix)) {
                    out.erase(0, std::strlen(prefix));
                    break;
                }
            if (out.starts_with("num") && !out.starts_with("numpad") && !out.starts_with("numlock")) out.insert(3, "pad");  // Num7 -> NumPad7
            return out;
        }
    }

    std::optional<std::uint32_t> ParseKeyName(std::string_view v)
    {
        if (v.empty() || std::isdigit(static_cast<unsigned char>(v[0]))) return std::nullopt;
        const auto n = NormKeyName(v);
        for (const auto& k : kKeyNames)
            if (n == k.name) return k.dik;
        return std::nullopt;
    }

    // Accepts decimal, 0x-hex or a key name (F1, PageUp...). Keyboard DIK scancodes only
    // (2..255): 0 and 1 are far more often "disabled" or booleans than a real key.
    std::optional<std::uint32_t> ParseKey(std::string v)
    {
        if (const auto p = v.find_first_of(";#"); p != npos) v.erase(p);
        v = Trim(v);
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) v = v.substr(1, v.size() - 2);
        if (v.empty()) return std::nullopt;
        if (const auto named = ParseKeyName(v); named && *named >= 2) return named;
        int         base = 10;
        const char* b    = v.data();
        const char* e    = b + v.size();
        if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
            base = 16;
            b += 2;
        }
        unsigned value = 0;
        const auto [p, ec] = std::from_chars(b, e, value, base);
        if (ec != std::errc{} || p != e) return std::nullopt;
        if (value < 2 || value > 255) return std::nullopt;
        return value;
    }

    // SKSE / MCM Helper key codes above the keyboard: 256-265 mouse, 266-281 gamepad.
    std::optional<std::pair<Device, std::uint32_t>> ParseSkseButton(std::string v)
    {
        if (const auto p = v.find_first_of(";#"); p != npos) v.erase(p);
        v = Trim(v);
        unsigned code = 0;
        const auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), code);
        if (ec != std::errc{} || p != v.data() + v.size()) return std::nullopt;
        if (code >= 256 && code <= 265) return std::pair{ Device::Mouse, code - 256 };  // 264/265: wheel up/down
        static constexpr std::uint32_t kPad[] = { kPadUp, kPadDown, kPadLeft, kPadRight, kPadStart, kPadBack, kPadL3, kPadR3,
            kPadLB, kPadRB, kPadA, kPadB, kPadX, kPadY, kPadLT, kPadRT };
        if (code >= 266 && code <= 281) return std::pair{ Device::Gamepad, kPad[code - 266] };
        return std::nullopt;
    }

    // Settings for a gamepad: their named values (LB, A, Y...) are not keyboard keys.
    bool IsGamepadSetting(const std::string& name)
    {
        const auto l = Lower(name);
        for (auto w : { "gamepad", "controller", "joystick", "joypad", "xbox", "dualshock" })
            if (l.find(w) != npos) return true;
        return false;
    }

    // ---------------------------------------------------------------- virtual-key codes
    // Some mods (Community Shaders) store Windows VK codes instead of DIK scancodes.

    namespace
    {
        struct VkDik
        {
            std::uint32_t vk, dik;
        };

        // Keys MapVirtualKey gets wrong or ambiguous (extended keys, numpad, modifiers).
        // The first entry for a DIK is the one written back.
        constexpr VkDik kVkTable[] = {
            { VK_PRIOR, 201 }, { VK_NEXT, 209 }, { VK_END, 207 }, { VK_HOME, 199 },
            { VK_LEFT, 203 }, { VK_UP, 200 }, { VK_RIGHT, 205 }, { VK_DOWN, 208 },
            { VK_INSERT, 210 }, { VK_DELETE, 211 }, { VK_SNAPSHOT, 183 }, { VK_PAUSE, 197 },
            { VK_NUMLOCK, 69 }, { VK_DIVIDE, 181 }, { VK_MULTIPLY, 55 }, { VK_ADD, 78 },
            { VK_SUBTRACT, 74 }, { VK_DECIMAL, 83 },
            { VK_NUMPAD0, 82 }, { VK_NUMPAD1, 79 }, { VK_NUMPAD2, 80 }, { VK_NUMPAD3, 81 }, { VK_NUMPAD4, 75 },
            { VK_NUMPAD5, 76 }, { VK_NUMPAD6, 77 }, { VK_NUMPAD7, 71 }, { VK_NUMPAD8, 72 }, { VK_NUMPAD9, 73 },
            { VK_LSHIFT, 42 }, { VK_RSHIFT, 54 }, { VK_LCONTROL, 29 }, { VK_RCONTROL, 157 },
            { VK_LMENU, 56 }, { VK_RMENU, 184 }, { VK_LWIN, 219 }, { VK_RWIN, 220 }, { VK_APPS, 221 },
            { VK_SHIFT, 42 }, { VK_CONTROL, 29 }, { VK_MENU, 56 },
        };
    }

    std::optional<std::uint32_t> VkToDik(std::uint32_t vk)
    {
        for (const auto& e : kVkTable)
            if (e.vk == vk) return e.dik;
        const auto sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        if (sc == 0 || sc > 0x7F) return std::nullopt;
        return sc;
    }

    std::optional<std::uint32_t> DikToVk(std::uint32_t dik)
    {
        for (const auto& e : kVkTable)
            if (e.dik == dik) return e.vk;
        if (dik >= 0x80) return std::nullopt;
        const auto vk = MapVirtualKeyW(dik, MAPVK_VSC_TO_VK);
        if (!vk) return std::nullopt;
        return vk;
    }

    std::uint8_t ModBitForDik(std::uint32_t dik)
    {
        if (dik == 42 || dik == 54) return kShift;
        if (dik == 29 || dik == 157) return kCtrl;
        if (dik == 56 || dik == 184) return kAlt;
        return 0;
    }

    // Mods whose settings files store VK codes (matched against the owner, lower case).
    bool UsesVirtualKeys(const std::string& owner)
    {
        return Lower(owner) == "communityshaders";
    }

    std::string ModsText(std::uint8_t mods)
    {
        std::string s;
        if (mods & kCtrl) s += "Ctrl+";
        if (mods & kShift) s += "Shift+";
        if (mods & kAlt) s += "Alt+";
        return s;
    }
}
