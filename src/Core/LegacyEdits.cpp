// Puts back mod config values written by old versions (this one never edits them).

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- ini editing

    namespace
    {
        bool ReadWhole(const fs::path& file, std::string& text, std::string& err)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                err = "cannot open file";
                return false;
            }
            text.assign(std::istreambuf_iterator<char>(in), {});
            return true;
        }

        bool WriteWhole(const fs::path& file, const std::string& text, std::string& err)
        {
            std::error_code ec;
            auto            bak = file;
            bak += ".hotkeyatlas.bak";
            if (!fs::exists(bak, ec)) fs::copy_file(file, bak, ec);

            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            if (!out) {
                err = "cannot write file";
                return false;
            }
            out << text;
            return true;
        }


        // The value part of `name = value` on line `idx`, checked against the scan.
        struct IniValue
        {
            std::string* line;
            std::size_t  start;
            std::string  value;
        };

        std::optional<IniValue> FindIniValue(std::vector<std::string>& lines, int idx, const std::string& name)
        {
            if (idx < 0 || static_cast<std::size_t>(idx) >= lines.size()) return std::nullopt;
            auto&      ln = lines[idx];
            const auto eq = ln.find('=');
            if (eq == npos) return std::nullopt;
            auto keyPart = ln.substr(0, eq);
            if (keyPart.starts_with(kBom)) keyPart.erase(0, kBom.size());
            if (Trim(keyPart) != name) return std::nullopt;
            auto vs = ln.find_first_not_of(" \t", eq + 1);
            if (vs == npos) vs = ln.size();
            auto ve = ln.find_first_of(";#\r", vs);
            if (ve == npos) ve = ln.size();
            return IniValue{ &ln, vs, Trim(ln.substr(vs, ve - vs)) };
        }

        void ReplaceIniValue(const IniValue& v, const std::string& text)
        {
            v.line->replace(v.start, v.value.size(), text);
        }

        std::string FormatKeyLike(const std::string& old, std::uint32_t key)
        {
            const bool hex = old.size() > 2 && old[0] == '0' && (old[1] == 'x' || old[1] == 'X');
            char       buf[16];
            std::snprintf(buf, sizeof buf, hex ? "0x%X" : "%u", key);
            return buf;
        }

        // Writes key + modifiers. `written` gets the combo actually stored (a setting may
        // not support every modifier); `note` explains what was dropped.
        bool EditIni(const Binding& b, std::uint32_t combo, std::uint32_t& written, std::string& note, std::string& err)
        {
            std::string text;
            if (!ReadWhole(b.file, text, err)) return false;

            // split on '\n' only, so joining with '\n' restores the file byte for byte
            std::vector<std::string> lines;
            for (std::size_t pos = 0;;) {
                const auto nl = text.find('\n', pos);
                if (nl == npos) {
                    lines.push_back(text.substr(pos));
                    break;
                }
                lines.push_back(text.substr(pos, nl - pos));
                pos = nl + 1;
            }

            const auto main = FindIniValue(lines, b.line, b.iniKey);
            if (!main) {
                err = "file changed since the scan";
                return false;
            }
            if (ParseKeyName(main->value)) {
                err = "the key is stored by name, only numbers are written back";
                return false;
            }
            if (const auto cur = ParseKey(main->value); !cur || *cur != b.key) {
                err = "value changed on disk";
                return false;
            }

            const auto key  = ComboKey(combo);
            auto       mods = ComboMods(combo);

            switch (b.modStyle) {
            case ModStyle::Flags:
                for (int bit = 0; bit < 3; ++bit) {
                    const bool on = mods & kModBits[bit];
                    if (b.flags[bit].line < 0) {
                        if (on) note = std::string("this mod has no ") + kModNames[bit] + " option; ";
                        mods &= ~kModBits[bit];
                        continue;
                    }
                    const auto v = FindIniValue(lines, b.flags[bit].line, b.flags[bit].name);
                    if (!v) {
                        err = "file changed since the scan";
                        return false;
                    }
                    const auto l = Lower(v->value);
                    ReplaceIniValue(*v, l == "true" || l == "false" ? (on ? "true" : "false") : (on ? "1" : "0"));
                }
                break;
            case ModStyle::ModKey:
                {
                    const auto v = FindIniValue(lines, b.modKey.line, b.modKey.name);
                    if (!v) {
                        err = "file changed since the scan";
                        return false;
                    }
                    // one modifier key only: keep the first of Shift, Ctrl, Alt
                    std::uint32_t modDik = 0;
                    for (int bit = 0; bit < 3; ++bit)
                        if (mods & kModBits[bit]) {
                            if (!modDik) {
                                modDik = kModDik[bit];
                                mods   = kModBits[bit];
                            } else {
                                note = "this mod supports one modifier only; ";
                            }
                        }
                    ReplaceIniValue(*v, FormatKeyLike(v->value, modDik));
                }
                break;
            default:
                if (mods) note = "this setting has no modifier, saved without it; ";
                mods = 0;
                break;
            }
            // flags/modifier lines were edited first; the main line is a separate string, still valid
            ReplaceIniValue(*main, FormatKeyLike(main->value, key));

            std::string joined;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (i) joined += '\n';
                joined += lines[i];
            }
            if (!WriteWhole(b.file, joined, err)) return false;
            written = Combo(key, mods);
            return true;
        }

        bool EditJson(const Binding& b, std::uint32_t combo, std::uint32_t& written, std::string& note, std::string& err)
        {
            std::string text;
            if (!ReadWhole(b.file, text, err)) return false;

            const auto encode = [&](std::uint32_t dik) -> std::optional<std::uint32_t> {
                return b.virtualKey ? DikToVk(dik) : std::optional<std::uint32_t>(dik);
            };
            const auto key  = ComboKey(combo);
            auto       mods = ComboMods(combo);
            const auto code = encode(key);
            if (!code) {
                err = "this mod cannot store that key";
                return false;
            }

            if (b.modStyle == ModStyle::JsonArray) {
                if (b.arrayOffset + b.arrayText.size() > text.size() || text.compare(b.arrayOffset, b.arrayText.size(), b.arrayText) != 0) {
                    err = "file changed since the scan";
                    return false;
                }
                // modifiers first, the key last: [16, 106]
                std::string arr = "[";
                for (int bit = 0; bit < 3; ++bit) {
                    if (!(mods & kModBits[bit])) continue;
                    const std::uint32_t m = b.virtualKey ? std::array<std::uint32_t, 3>{ VK_SHIFT, VK_CONTROL, VK_MENU }[bit] : kModDik[bit];
                    arr += std::to_string(m) + ", ";
                }
                arr += std::to_string(*code) + "]";
                text.replace(b.arrayOffset, b.arrayText.size(), arr);
            } else {
                if (b.valueOffset + b.valueLength > text.size() || text.compare(b.valueOffset, b.valueLength, std::to_string(b.fileValue)) != 0) {
                    err = "file changed since the scan";
                    return false;
                }
                if (mods) note = "this setting has no modifier, saved without it; ";
                mods = 0;
                text.replace(b.valueOffset, b.valueLength, std::to_string(*code));
            }

            if (!WriteWhole(b.file, text, err)) return false;
            written = Combo(key, mods);
            return true;
        }
    }

    // Only used to undo edits made by older versions: Hotkey Atlas no longer writes to
    // other mods' files, remaps are applied by the input hook instead.
    bool RestoreFileValue(const Binding& b, std::uint32_t combo, std::string& err)
    {
        std::uint32_t written = combo;
        std::string   note;
        return b.kind == Kind::Json ? EditJson(b, combo, written, note, err) : EditIni(b, combo, written, note, err);
    }
}
