// YAML hotkey files (SkyrimNet's SKSE/Plugins/SkyrimNet/config/Hotkey.yaml): "name: <VK code>"
// lines, -1 = unset. SkyrimNet reads the keyboard itself (GetAsyncKeyState), so the input hook
// can't remap its keys; a rebind writes the file instead, which SkyrimNet reloads at once.

#include "Internal.h"

namespace HA
{
    std::vector<YamlEntry> ParseYamlScalars(const std::string& text)
    {
        std::vector<YamlEntry>                    out;
        std::vector<std::pair<std::size_t, std::string>> parents;  // indent, key of the open mappings
        std::size_t                               pos  = text.starts_with(kBom) ? kBom.size() : 0;
        int                                       line = -1;
        while (pos < text.size()) {
            ++line;
            const auto end  = text.find('\n', pos);
            const auto stop = end == npos ? text.size() : end;
            const auto from = pos;
            pos             = end == npos ? text.size() : end + 1;

            const std::string_view raw(text.data() + from, stop - from);
            const auto             indent = raw.find_first_not_of(' ');
            if (indent == npos) continue;
            const auto body = raw.substr(indent);
            if (body.empty() || body[0] == '#' || body[0] == '-' || body[0] == '\r' || body.starts_with("---") || body.starts_with("...")) continue;
            const auto colon = body.find(':');
            if (colon == npos) continue;

            while (!parents.empty() && parents.back().first >= indent) parents.pop_back();
            auto key = Trim(body.substr(0, colon));
            if (key.size() >= 2 && (key.front() == '"' || key.front() == '\'') && key.back() == key.front()) key = key.substr(1, key.size() - 2);
            if (key.empty()) continue;

            // the value: up to a comment or the line end
            auto vstart = body.find_first_not_of(" \t", colon + 1);
            if (vstart == npos || body[vstart] == '#' || body[vstart] == '\r') {
                parents.emplace_back(indent, key);  // "parent:" opens a mapping
                continue;
            }
            auto vend = body.find(" #", vstart);
            if (vend == npos) vend = body.size();
            while (vend > vstart && (body[vend - 1] == ' ' || body[vend - 1] == '\t' || body[vend - 1] == '\r')) --vend;

            std::string path;
            for (const auto& [i, k] : parents) path += (path.empty() ? "" : ".") + k;
            out.push_back({ std::move(path), std::move(key), std::string(body.substr(vstart, vend - vstart)),
                from + indent + vstart, vend - vstart, line });
        }
        return out;
    }

    // Windows virtual-key code -> input code; -1 (unset) -> kUnbound; nullopt: nothing we can show.
    std::optional<std::uint32_t> YamlCode(long vk)
    {
        if (vk < 0) return kUnbound;
        switch (vk) {
        case VK_LBUTTON: return MakeCode(Device::Mouse, kMouseLeft);
        case VK_RBUTTON: return MakeCode(Device::Mouse, kMouseRight);
        case VK_MBUTTON: return MakeCode(Device::Mouse, kMouseMiddle);
        case VK_XBUTTON1: return MakeCode(Device::Mouse, kMouse4);
        case VK_XBUTTON2: return MakeCode(Device::Mouse, kMouse5);
        default: break;
        }
        if (const auto dik = VkToDik(static_cast<std::uint32_t>(vk))) return *dik;
        return std::nullopt;
    }

    // Input code -> the number written in the file; nullopt: the file can't hold it (a modifier,
    // a combo, a double tap / hold, the gamepad: SkyrimNet takes one plain key or mouse button).
    std::optional<long> YamlValue(std::uint32_t code)
    {
        if (code == kUnbound) return -1;
        if (HoldOf(code) || TriggerOf(code) != Trigger::Press) return std::nullopt;
        switch (CodeDevice(code)) {
        case Device::Keyboard:
            if (ComboMods(code)) return std::nullopt;
            if (const auto vk = DikToVk(ComboKey(code))) return static_cast<long>(*vk);
            return std::nullopt;
        case Device::Mouse:
            switch (CodeId(code)) {
            case kMouseLeft: return VK_LBUTTON;
            case kMouseRight: return VK_RBUTTON;
            case kMouseMiddle: return VK_MBUTTON;
            case kMouse4: return VK_XBUTTON1;
            case kMouse5: return VK_XBUTTON2;
            default: return std::nullopt;
            }
        default: return std::nullopt;
        }
    }

    namespace
    {
        // FileEditId() = <origin>|<path of the parent mappings>|<name>
        struct EditId
        {
            std::string origin, path, name;
        };

        std::optional<EditId> SplitId(const std::string& id)
        {
            const auto p1 = id.find('|');
            const auto p2 = id.rfind('|');
            if (p1 == npos || p2 == p1) return std::nullopt;
            return EditId{ id.substr(0, p1), id.substr(p1 + 1, p2 - p1 - 1), id.substr(p2 + 1) };
        }
    }

    bool IsYamlEditId(const std::string& id)
    {
        const auto origin = Lower(id.substr(0, id.find('|')));
        return origin.ends_with(".yaml") || origin.ends_with(".yml");
    }

    bool WriteYamlKey(const std::string& id, std::uint32_t code, std::string& err)
    {
        const auto parts = SplitId(id);
        const auto value = YamlValue(code);
        if (!parts || !value) {
            err = TL("this key can't be written there");
            return false;
        }
        const auto file = fs::path("Data") / fs::path(std::u8string(parts->origin.begin(), parts->origin.end()));
        std::string text;
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                err = TLF("cannot read {0}", { parts->origin });
                return false;
            }
            text.assign(std::istreambuf_iterator<char>(in), {});
        }
        const auto entries = ParseYamlScalars(text);
        const auto it = std::ranges::find_if(entries, [&](const YamlEntry& e) { return e.path == parts->path && e.name == parts->name; });
        if (it == entries.end()) {
            err = TLF("{0} not found in {1}", { parts->name, parts->origin });
            return false;
        }
        const auto written = std::to_string(*value);
        if (it->value == written) return true;
        text.replace(it->valueOffset, it->valueLength, written);  // only the number changes, the rest stays byte for byte

        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size()))) {
            err = TLF("cannot write {0}", { parts->origin });
            return false;
        }
        logger::info("{}: {} = {}", parts->origin, parts->name, written);
        return true;
    }

    void SyncYamlFiles(const Overrides& before, const Overrides& after)
    {
        std::set<std::string> ids;
        for (const auto* m : { &before, &after })
            for (const auto& [id, ov] : *m)
                if (IsYamlEditId(id)) ids.insert(id);
        for (const auto& id : ids) {
            // the change that stays, else back to the file's own key
            const auto now  = after.find(id);
            const auto code = now != after.end() ? now->second.key : before.at(id).original;
            std::string err;
            if (!WriteYamlKey(id, code, err)) logger::warn("{}: {}", id, err);
        }
    }
}
