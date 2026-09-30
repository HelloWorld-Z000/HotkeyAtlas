// Finds every hotkey: Skyrim controls plus the key settings in mods' ini and json files.

#include "Internal.h"
#include "Json.h"

namespace HA
{
    namespace
    {
        std::mutex                   g_lock;
        std::shared_ptr<const Model> g_model = std::make_shared<const Model>();
        std::string                  g_status;
        std::atomic<bool>            g_busy{ false };
        std::atomic<bool>            g_again{ false };
    }

    void SetStatus(std::string s)
    {
        std::lock_guard l(g_lock);
        g_status = std::move(s);
    }

    namespace
    {
        std::atomic<bool> g_restoredFiles{ false };

        // Applies our remap record to a scanned mod binding. The mod's file keeps its own key
        // (`defaultKey`); the binding shows the key the user picked, which the input hook
        // translates at runtime.
        void MarkFileEdit(Binding& b)
        {
            std::optional<Override> rec;
            {
                std::lock_guard l(g_ovLock);
                if (const auto it = g_fileEdits.find(FileEditId(b)); it != g_fileEdits.end()) rec = it->second;
            }
            if (!rec) return;
            const auto inFile = CurrentCode(b);

            if (b.device == Device::Keyboard && rec->key == inFile && rec->original != inFile) {
                // written into the mod's file by an older Hotkey Atlas: put the file back
                std::string err;
                if (!RestoreFileValue(b, rec->original, err)) {
                    logger::warn("could not restore {} {}: {}", b.origin, b.iniKey, err);
                    return;
                }
                logger::info("restored {} {} to its original key", b.origin, b.iniKey);
                g_restoredFiles = true;  // other offsets in this file are stale now: scan again
            } else if (rec->original != inFile) {
                return;  // the mod's own key changed since: this remap no longer applies
            }
            b.overridden = true;
            b.defaultKey = rec->original;
            if (rec->key == kUnbound) {
                b.key  = kUnbound;
                b.mods = 0;
            } else if (CodeDevice(rec->key) == Device::Keyboard) {
                b.key  = ComboKey(rec->key);
                b.mods = ComboMods(rec->key);
            } else {
                b.device = CodeDevice(rec->key);  // a key moved to the mouse is shown there
                b.key    = CodeId(rec->key);
                b.mods   = 0;
            }
            b.hold    = HoldOf(rec->key);
            b.trigger = TriggerOf(rec->key);
        }

        // ---------------------------------------------------------------- ini scanning

        // Path relative to Data/. Lexical on purpose: fs::relative resolves through MO2's
        // virtual file system to the real mod folder and yields "../../MO2/mods/...".
        fs::path RelToData(const fs::path& file, const fs::path& dataDir)
        {
            auto rel = file.lexically_relative(dataDir);
            if (rel.empty() || *rel.begin() == "..") return file.filename();
            return rel;
        }

        std::string OwnerFromPath(const fs::path& file, const fs::path& dataDir)
        {
            const auto               rel = RelToData(file, dataDir);
            std::vector<std::string> parts;
            for (const auto& p : rel) parts.push_back(p.string());
            // SKSE/Plugins/<Mod>/something.ini -> <Mod>;  SKSE/Plugins/<Mod>.ini and MCM/Settings/<Mod>.ini -> stem
            if (parts.size() > 3 && Lower(parts[0]) == "skse" && Lower(parts[1]) == "plugins") return parts[2];
            return file.stem().string();
        }

        struct IniEntry
        {
            std::string section;
            std::string name;
            std::string base;   // BaseName(name)
            std::string value;  // without trailing comment
            int         line;
        };

        // "uToggleUIKey" -> "toggleuikey": no Hungarian prefix, lower case
        std::string BaseName(const std::string& name)
        {
            if (name.size() > 2 && std::strchr("ibfsu", name[0]) && std::isupper(static_cast<unsigned char>(name[1]))) return Lower(name.substr(1));
            return Lower(name);
        }

        bool ParseBool(const std::string& v)
        {
            const auto l = Lower(v);
            if (l == "true" || l == "on" || l == "yes") return true;
            const auto n = ParseUInt(l);
            return n && *n != 0;
        }

        // Index of a setting in the same section whose base name is one of `names`, or npos.
        std::size_t FindSibling(const std::vector<IniEntry>& entries, std::size_t self, const std::vector<std::string>& names)
        {
            for (std::size_t j = 0; j < entries.size(); ++j) {
                if (j == self || entries[j].section != entries[self].section) continue;
                for (const auto& n : names)
                    if (entries[j].base == n) return j;
            }
            return npos;
        }

        // Settings an MCM Helper menu declares as "keymap": bound keys by definition, whatever
        // their name. Key: lower-case "section|name"; value: label shown in the MCM.
        struct McmKeymaps
        {
            std::string                        owner;
            std::map<std::string, std::string> labels;
            std::set<std::string>*             seen       = nullptr;  // keymaps already found in another file
            bool                               onlyUnseen = false;    // defaults file: fill in what the user file lacks
        };

        std::string McmKeyId(const std::string& section, const std::string& name)
        {
            return Lower(section) + '|' + Lower(name);
        }

        enum class FileResult
        {
            Read,
            TooBig,
            Unreadable,
            BadJson
        };

        constexpr std::uintmax_t kMaxFileSize = 2u * 1024 * 1024;

        FileResult ScanIni(const fs::path& file, const fs::path& dataDir, std::vector<Binding>& out, const McmKeymaps* mcm = nullptr)
        {
            std::error_code ec;
            const auto      size = fs::file_size(file, ec);
            if (ec) return FileResult::Unreadable;
            if (size > kMaxFileSize) return FileResult::TooBig;
            std::ifstream in(file, std::ios::binary);
            if (!in) return FileResult::Unreadable;

            const auto owner  = mcm ? mcm->owner : OwnerFromPath(file, dataDir);
            const auto origin = RelToData(file, dataDir).generic_string();

            // pass 1: every setting, so modifiers stored in sibling settings can be matched
            std::vector<IniEntry> entries;
            std::string           section, line;
            int                   lineNo = -1;
            while (std::getline(in, line)) {
                ++lineNo;
                if (lineNo == 0 && line.starts_with(kBom)) line.erase(0, kBom.size());
                const auto t = Trim(line);
                if (t.empty() || t[0] == ';' || t[0] == '#' || t.starts_with("//")) continue;
                if (t.front() == '[') {
                    if (const auto r = t.find(']'); r != npos) section = t.substr(1, r - 1);
                    continue;
                }
                const auto eq = t.find('=');
                if (eq == npos) continue;
                auto value = t.substr(eq + 1);
                if (const auto p = value.find_first_of(";#"); p != npos) value.erase(p);
                const auto name = Trim(t.substr(0, eq));
                entries.push_back({ section, name, BaseName(name), Trim(value), lineNo });
            }

            // pass 2: key settings plus their modifiers; settings used as a modifier are not listed on their own
            std::vector<bool> consumed(entries.size());
            std::vector<std::pair<std::size_t, Binding>> found;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                const auto& e     = entries[i];
                std::string label = Humanize(e.name);
                if (mcm) {
                    const auto id = McmKeyId(e.section, e.name);
                    const auto it = mcm->labels.find(id);
                    if (it == mcm->labels.end()) {
                        if (mcm->onlyUnseen || !IsKeyLike(e.name)) continue;
                    } else {
                        if (mcm->onlyUnseen && mcm->seen->contains(id)) continue;
                        if (!it->second.empty()) label = it->second;
                        mcm->seen->insert(id);
                    }
                } else if (!IsKeyLike(e.name)) {
                    continue;
                }
                if (IsGamepadSetting(e.name) && ParseKeyName(e.value)) continue;  // "LB", "A": gamepad buttons
                if (const auto button = ParseSkseButton(e.value)) {
                    // mouse / gamepad: remapped by the input hook like keys, no modifiers
                    Binding b;
                    b.device     = button->first;
                    b.key        = button->second;
                    b.action     = label;
                    b.owner      = owner;
                    b.context    = e.section;
                    b.origin     = origin;
                    b.kind       = Kind::Ini;
                    b.editable   = true;
                    b.file       = file;
                    b.iniKey     = e.name;
                    b.line       = e.line;
                    b.defaultKey = CurrentCode(b);
                    MarkFileEdit(b);
                    found.emplace_back(i, std::move(b));
                    continue;
                }
                const auto val = ParseKey(e.value);
                if (!val) continue;

                Binding b;
                b.key      = *val;
                b.action   = label;
                b.owner    = owner;
                b.context  = e.section;
                b.origin   = origin;
                b.kind     = Kind::Ini;
                b.editable = true;
                b.file     = file;
                b.iniKey   = e.name;
                b.line     = e.line;

                // uToggleUIKey + uToggleUIKeyShift / Ctrl / Alt
                static constexpr std::pair<const char*, int> kFlagNames[] = {
                    { "shift", 0 }, { "ctrl", 1 }, { "control", 1 }, { "alt", 2 }
                };
                for (const auto& [suffix, bit] : kFlagNames) {
                    const auto j = FindSibling(entries, i, { e.base + suffix, e.base + '_' + suffix });
                    if (j == npos) continue;
                    b.modStyle        = ModStyle::Flags;
                    b.flags[bit].line = entries[j].line;
                    b.flags[bit].name = entries[j].name;
                    if (ParseBool(entries[j].value)) b.mods |= kModBits[bit];
                    consumed[j] = true;
                }

                // iHotkey + iHotkeyModifier, iToggleKey + iToggleModifierKey, ...
                if (b.modStyle == ModStyle::None) {
                    std::vector<std::string> names = { e.base + "modifier", e.base + "_modifier", e.base + "mod", e.base + "modifierkey", e.base + "modkey" };
                    if (e.base.ends_with("key")) {
                        const auto stem = e.base.substr(0, e.base.size() - 3);
                        names.push_back(stem + "modifierkey");
                        names.push_back(stem + "modkey");
                        names.push_back(stem + "modifier");
                    }
                    if (const auto j = FindSibling(entries, i, names); j != npos) {
                        b.modStyle    = ModStyle::ModKey;
                        b.modKey.line = entries[j].line;
                        b.modKey.name = entries[j].name;
                        if (const auto m = ParseKey(entries[j].value)) b.mods = ModBitForDik(*m);
                        consumed[j] = true;
                    }
                }

                b.defaultKey = Combo(b.key, b.mods);
                MarkFileEdit(b);
                found.emplace_back(i, std::move(b));
            }
            for (auto& [i, b] : found)
                if (!consumed[i]) out.push_back(std::move(b));
            return FileResult::Read;
        }

        // ---------------------------------------------------------------- json scanning

        // Minimal JSON walker: finds integer values under key-like names, including small
        // arrays such as "Effects11ToggleKey": [16, 106] (Shift + Num*).
        class JsonScanner
        {
        public:
            JsonScanner(const std::string& text, const Binding& proto, std::vector<Binding>& out) :
                _s(text), _proto(proto), _out(out) {}

            void Run()
            {
                if (_s.compare(0, kBom.size(), kBom) == 0) _i = kBom.size();
                Value("", "", "", 0);
            }

        private:
            struct Number
            {
                std::size_t   offset, length;
                std::uint32_t value;
            };

            const std::string&    _s;
            const Binding&        _proto;
            std::vector<Binding>& _out;
            std::size_t           _i = 0;

            [[noreturn]] static void Fail() { throw std::runtime_error("bad json"); }

            void Ws()
            {
                while (_i < _s.size() && std::strchr(" \t\r\n", _s[_i]) && _s[_i]) ++_i;
            }

            char Peek()
            {
                Ws();
                return _i < _s.size() ? _s[_i] : '\0';
            }

            void Expect(char c)
            {
                if (Peek() != c) Fail();
                ++_i;
            }

            std::string String()
            {
                Expect('"');
                std::string r;
                while (_i < _s.size() && _s[_i] != '"') {
                    if (_s[_i] == '\\') ++_i;
                    if (_i < _s.size()) r += _s[_i++];
                }
                if (_i >= _s.size()) Fail();
                ++_i;
                return r;
            }

            // Skips a scalar; returns it if it is a non-negative integer.
            std::optional<Number> Scalar()
            {
                Ws();
                const auto start = _i;
                while (_i < _s.size() && !std::strchr(",]} \t\r\n", _s[_i])) ++_i;
                if (_i == start) Fail();
                unsigned   v = 0;
                const auto [p, ec] = std::from_chars(_s.data() + start, _s.data() + _i, v);
                if (ec != std::errc{} || p != _s.data() + _i) return std::nullopt;
                return Number{ start, _i - start, v };
            }

            std::optional<std::uint32_t> ToDik(std::uint32_t v) const
            {
                if (_proto.virtualKey) return VkToDik(v);
                if (v < 2 || v > 255) return std::nullopt;
                return v;
            }

            Binding Make(const std::string& path, const std::string& ctx, const std::string& name, const Number& n, std::uint32_t dik, const std::string& suffix)
            {
                Binding b     = _proto;
                b.key         = dik;
                b.action      = Humanize(name) + suffix;
                b.context     = ctx;
                b.iniKey      = path;
                b.valueOffset = n.offset;
                b.valueLength = n.length;
                b.fileValue   = n.value;
                return b;
            }

            void Push(Binding b)
            {
                b.defaultKey = Combo(b.key, b.mods);
                MarkFileEdit(b);
                _out.push_back(std::move(b));
            }

            void Emit(const std::string& path, const std::string& ctx, const std::string& name, const Number& n, std::uint32_t dik, const std::string& suffix)
            {
                Push(Make(path, ctx, name, n, dik, suffix));
            }

            void Value(const std::string& path, const std::string& ctx, const std::string& name, int depth)
            {
                if (depth > 64) Fail();
                const char c = Peek();
                if (c == '{') {
                    ++_i;
                    if (Peek() == '}') {
                        ++_i;
                        return;
                    }
                    for (;;) {
                        const auto key = String();
                        Expect(':');
                        Value(path.empty() ? key : path + '.' + key, path, key, depth + 1);
                        if (Peek() == ',') {
                            ++_i;
                            continue;
                        }
                        Expect('}');
                        return;
                    }
                }
                if (c == '[') {
                    const auto arrayStart = _i;
                    ++_i;
                    const bool          keyLike = IsKeyLike(name);
                    std::vector<Number> nums;
                    bool                plain = true;  // only integers inside
                    if (Peek() != ']') {
                        for (int idx = 0;; ++idx) {
                            const char e = Peek();
                            if (e == '{' || e == '[' || e == '"') {
                                plain = false;
                                Value(path + '[' + std::to_string(idx) + ']', ctx, name, depth + 1);
                            } else if (auto n = Scalar()) {
                                nums.push_back(*n);
                            } else {
                                plain = false;
                            }
                            if (Peek() == ',') {
                                ++_i;
                                continue;
                            }
                            break;
                        }
                    }
                    Expect(']');
                    if (keyLike && plain && !nums.empty()) EmitCombo(path, ctx, name, nums, arrayStart);
                    return;
                }
                if (c == '"') {
                    String();
                    return;
                }
                if (const auto n = Scalar(); n && IsKeyLike(name))
                    if (const auto dik = ToDik(n->value)) Emit(path, ctx, name, *n, *dik, "");
            }

            // [mod, mod, key] -> one binding on `key` with modifiers, owning the whole array;
            // otherwise one binding per element
            void EmitCombo(const std::string& path, const std::string& ctx, const std::string& name, const std::vector<Number>& nums, std::size_t arrayStart)
            {
                std::vector<std::optional<std::uint32_t>> diks;
                for (const auto& n : nums) diks.push_back(ToDik(n.value));

                int          main  = -1;
                std::uint8_t mods  = 0;
                bool         combo = true;
                for (std::size_t k = 0; k < nums.size(); ++k) {
                    if (!diks[k]) {
                        combo = false;
                        break;
                    }
                    if (const auto bit = ModBitForDik(*diks[k]); bit && nums.size() > 1) {
                        mods |= bit;
                    } else if (main < 0) {
                        main = static_cast<int>(k);
                    } else {
                        combo = false;
                    }
                }
                if (combo && main >= 0) {
                    auto b        = Make(path, ctx, name, nums[main], *diks[main], "");
                    b.mods        = mods;
                    b.modStyle    = ModStyle::JsonArray;
                    b.arrayOffset = arrayStart;
                    b.arrayText   = _s.substr(arrayStart, _i - arrayStart);
                    Push(std::move(b));
                    return;
                }
                for (std::size_t k = 0; k < nums.size(); ++k)
                    if (diks[k]) Emit(path + '[' + std::to_string(k) + ']', ctx, name, nums[k], *diks[k], " #" + std::to_string(k + 1));
            }
        };

        FileResult ScanJson(const fs::path& file, const fs::path& dataDir, std::vector<Binding>& out)
        {
            std::error_code ec;
            const auto      size = fs::file_size(file, ec);
            if (ec) return FileResult::Unreadable;
            if (size > kMaxFileSize) return FileResult::TooBig;
            std::ifstream in(file, std::ios::binary);
            if (!in) return FileResult::Unreadable;
            const std::string text(std::istreambuf_iterator<char>(in), {});

            Binding proto;
            proto.owner      = OwnerFromPath(file, dataDir);
            proto.origin     = file.filename().string();
            proto.origin     = RelToData(file, dataDir).generic_string();
            proto.kind       = Kind::Json;
            proto.editable   = true;
            proto.file       = file;
            proto.virtualKey = UsesVirtualKeys(proto.owner);

            std::vector<Binding> found;
            try {
                JsonScanner(text, proto, found).Run();
            } catch (...) {
                return FileResult::BadJson;  // ignore the whole file
            }
            out.insert(out.end(), std::make_move_iterator(found.begin()), std::make_move_iterator(found.end()));
            return FileResult::Read;
        }

        // Only settings-like json files: data files (translations, form lists...) are noise.
        bool IsSettingsJson(const fs::path& p)
        {
            const auto name = Lower(p.filename().string());
            if (name.find("default") != npos || name.find("test") != npos) return false;  // CS SettingsDefault/SettingsTest
            for (auto w : { "setting", "config", "user", "hotkey", "keybind", "control", "input" })
                if (name.find(w) != npos) return true;
            return false;
        }

        // Interface/Translations/<mod>_english.txt: UTF-16 "$KEY<tab>text" lines.
        std::unordered_map<std::string, std::string> LoadTranslations(const fs::path& data, const std::string& mod)
        {
            std::unordered_map<std::string, std::string> map;
            std::ifstream in(data / "Interface/Translations" / (mod + "_english.txt"), std::ios::binary);
            if (!in) return map;
            const std::string raw(std::istreambuf_iterator<char>(in), {});
            if (raw.size() < 2 || static_cast<unsigned char>(raw[0]) != 0xFF || static_cast<unsigned char>(raw[1]) != 0xFE) return map;

            const auto* w   = reinterpret_cast<const wchar_t*>(raw.data() + 2);
            const int   len = static_cast<int>((raw.size() - 2) / sizeof(wchar_t));
            std::string text(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr)), '\0');
            WideCharToMultiByte(CP_UTF8, 0, w, len, text.data(), static_cast<int>(text.size()), nullptr, nullptr);

            for (std::size_t pos = 0; pos < text.size();) {
                auto       nl   = text.find('\n', pos);
                const auto line = Trim(std::string_view(text).substr(pos, nl == npos ? npos : nl - pos));
                pos             = nl == npos ? text.size() : nl + 1;
                if (const auto tab = line.find('\t'); tab != npos) map[line.substr(0, tab)] = Trim(line.substr(tab + 1));
            }
            return map;
        }

        void CollectKeymaps(const JNode& n, const std::unordered_map<std::string, std::string>& tr, std::map<std::string, std::string>& out)
        {
            if (n.t == JNode::T::Obj && n.Str("type") == "keymap") {
                // "id": "iKeybind:Controls" = setting:section
                const auto id    = n.Str("id");
                const auto colon = id.find(':');
                if (colon != npos) {
                    auto label = n.Str("text");
                    if (label.starts_with('$')) {
                        const auto it = tr.find(label);
                        label         = it != tr.end() ? it->second : label.substr(1);
                    }
                    out[McmKeyId(id.substr(colon + 1), id.substr(0, colon))] = label;
                }
            }
            for (const auto& [k, v] : n.obj) CollectKeymaps(v, tr, out);
            for (const auto& v : n.arr) CollectKeymaps(v, tr, out);
        }

        // What a scan looked at, for the log.
        struct ScanStats
        {
            std::size_t files = 0, withKeys = 0, skipped = 0;
            void        Add(std::size_t hotkeys, bool skip)
            {
                ++files;
                withKeys += hotkeys > 0;
                skipped += skip;
            }
        };

        // Scans one ini/json file.
        void ScanCounted(const fs::path& file, const fs::path& data, std::vector<Binding>& out, ScanStats& stats, bool json,
            const McmKeymaps* mcm = nullptr)
        {
            const auto before = out.size();
            FileResult r;
            try {
                r = json ? ScanJson(file, data, out) : ScanIni(file, data, out, mcm);
            } catch (...) {
                r = FileResult::Unreadable;  // odd file names / encodings
            }
            stats.Add(out.size() - before, r != FileResult::Read);
        }

        // MCM Helper mods: MCM/Config/<Mod>/config.json declares the keymaps, the values are in
        // MCM/Settings/<Mod>.ini (only once the user changed something) or else in the
        // defaults, MCM/Config/<Mod>/settings.ini. Returns the MCM/Settings files handled here.
        std::set<std::string> ScanMcmHelper(const fs::path& data, std::vector<Binding>& out, ScanStats& stats)
        {
            std::set<std::string> handled;
            std::error_code       ec;
            for (fs::directory_iterator it(data / "MCM/Config", fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (!it->is_directory(e2)) continue;
                const auto mod    = it->path().filename().string();
                const auto config = it->path() / "config.json";
                try {
                    std::ifstream in(config, std::ios::binary);
                    if (!in) continue;  // not an MCM Helper menu
                    const std::string json(std::istreambuf_iterator<char>(in), {});

                    McmKeymaps keys;
                    keys.owner = mod;
                    CollectKeymaps(JsonDom(json).Parse(), LoadTranslations(data, mod), keys.labels);
                    stats.Add(keys.labels.size(), false);
                    if (keys.labels.empty()) continue;  // its MCM/Settings ini, if any, is still read by the generic scan

                    std::set<std::string> seen;
                    keys.seen       = &seen;
                    const auto user = data / "MCM/Settings" / (mod + ".ini");
                    if (fs::exists(user, e2)) {
                        ScanCounted(user, data, out, stats, false, &keys);
                        handled.insert(Lower(user.filename().string()));
                    }
                    keys.onlyUnseen    = true;
                    const auto defaults = it->path() / "settings.ini";
                    if (fs::exists(defaults, e2)) ScanCounted(defaults, data, out, stats, false, &keys);
                } catch (...) {
                    stats.Add(0, true);  // broken config.json
                }
            }
            return handled;
        }

        std::vector<Binding> ScanFiles(ScanStats& stats)
        {
            std::vector<Binding> out;
            const fs::path       data = "Data";  // process cwd is the game folder; MO2's VFS hooks these calls
            const auto           mcmHandled = ScanMcmHelper(data, out, stats);
            for (const auto* sub : { "SKSE/Plugins", "MCM/Settings" }) {
                std::error_code ec;
                for (fs::recursive_directory_iterator it(data / sub, fs::directory_options::skip_permission_denied, ec), end;
                     !ec && it != end; it.increment(ec)) {
                    std::error_code e2;
                    if (!it->is_regular_file(e2)) continue;
                    std::string ext, name;
                    try {
                        ext  = Lower(it->path().extension().string());
                        name = Lower(it->path().filename().string());
                    } catch (...) {
                        continue;  // name not representable
                    }
                    if (ext != ".ini" && ext != ".json") continue;  // dlls, logs, textures...
                    if (std::string_view(sub) == "MCM/Settings" && mcmHandled.contains(name)) continue;  // read by ScanMcmHelper

                    // skipped: Hotkey Atlas's own files, json whose name is not a settings name
                    if (name.starts_with("hotkeyatlas") || (ext == ".json" && !IsSettingsJson(it->path())))
                        stats.Add(0, true);
                    else
                        ScanCounted(it->path(), data, out, stats, ext == ".json");
                }
            }
            return out;
        }

        void Publish(std::vector<Binding> cm, std::vector<Binding> files, const ScanStats& stats)
        {
            logger::info("scan: {} files, {} with hotkeys, {} skipped, {} hotkeys in total ({} Skyrim controls)", stats.files, stats.withKeys,
                stats.skipped, cm.size() + files.size(), cm.size());
            auto model = std::make_shared<Model>();
            model->all = std::move(cm);
            model->all.insert(model->all.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
            if (const auto notes = BuiltIn(); !notes->mods.empty())
                for (auto& b : model->all)
                    if (b.kind != Kind::ControlMap) b.description = DescribeModBinding(b, *notes);
            {
                // gamepad buttons added to key / mouse bindings (dropped silently when the mod's key changed since)
                std::lock_guard l(g_ovLock);
                for (auto& b : model->all) {
                    if (CodeDevice(b.defaultKey) == Device::Gamepad) continue;
                    const bool  control = b.kind == Kind::ControlMap;
                    const auto& map     = control ? g_padControls : g_padMods;
                    const auto  it      = map.find(control ? OverrideId(b.ctx, b.action, b.defaultKey) : FileEditId(b));
                    if (it != map.end() && it->second.original == b.defaultKey) b.padKey = it->second.key;
                }
            }
            std::sort(model->all.begin(), model->all.end(), [](const Binding& a, const Binding& b) {
                return std::tie(a.device, a.key, a.mods, a.owner, a.action) < std::tie(b.device, b.key, b.mods, b.owner, b.action);
            });
            for (std::size_t i = 0; i < model->all.size(); ++i)
            {
                // every keyboard key a binding takes: its key, the key held first, its modifiers
                const auto& b    = model->all[i];
                const auto  code = CurrentCode(b);
                if (code == kUnbound) continue;
                std::uint32_t keys[8]{};
                std::size_t   n = 0;
                if (b.device == Device::Keyboard) keys[n++] = b.key;
                if (b.hold && CodeDevice(b.hold) == Device::Keyboard) keys[n++] = CodeId(b.hold);
                for (const auto dik : { 42u, 54u, 29u, 157u, 56u, 184u })
                    if (dik != b.key && CodeUses(code, Device::Keyboard, dik)) keys[n++] = dik;
                for (std::size_t k = 0; k < n; ++k)
                    if (std::find(keys, keys + k, keys[k]) == keys + k) model->byKey[keys[k]].push_back(i);
            }

            std::lock_guard l(g_lock);
            g_model = std::move(model);
        }
    }

    void Rescan()
    {
        if (g_busy.exchange(true)) {
            g_again = true;
            return;
        }
        SKSE::GetTaskInterface()->AddTask([] {
            ApplyOverrides();            // game thread
            LoadBuiltInNotes();          // Notes.json edits show after a Rescan
            auto cm = ReadControlMap();  // game thread
            std::thread([cm = std::move(cm)]() mutable {
                ScanStats stats;
                auto      files = ScanFiles(stats);
                Publish(std::move(cm), std::move(files), stats);
                g_busy = false;
                if (g_again.exchange(false) | g_restoredFiles.exchange(false)) Rescan();
            }).detach();
        });
    }

    void ApplyOverridesLater()
    {
        SKSE::GetTaskInterface()->AddTask([] { ApplyOverrides(); });
    }

    std::shared_ptr<const Model> GetModel()
    {
        std::lock_guard l(g_lock);
        return g_model;
    }

    bool IsBusy() { return g_busy.load(); }

    std::string GetStatus()
    {
        std::lock_guard l(g_lock);
        return g_status;
    }
}
