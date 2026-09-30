// Built-in notes from Notes.json: what Skyrim controls and mod hotkeys do.

#include "Internal.h"
#include "Json.h"

namespace HA
{
    // ---------------------------------------------------------------- built-in notes
    // Data/SKSE/Plugins/HotkeyAtlas/Notes.json: what each Skyrim control and mod hotkey
    // does (see the file's _readme). Read on every scan, so new notes need no rebuild.


    namespace
    {
        std::mutex                          g_builtInLock;
        std::shared_ptr<const BuiltInNotes> g_builtIn = std::make_shared<const BuiltInNotes>();
    }

    std::shared_ptr<const BuiltInNotes> BuiltIn()
    {
        std::lock_guard l(g_builtInLock);
        return g_builtIn;
    }

    // "Left Equip" == "LeftEquip" == "left-equip"
    std::string NormEvent(std::string_view s)
    {
        std::string out;
        for (const char c : s)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '/') out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    // The text in the interface language, {0}, {1}... filled in.
    std::string Resolve(const LocText& t, std::initializer_list<std::string_view> args)
    {
        const auto lang = ActiveLanguage();
        for (const auto& [l, text] : t.tr) {
            if (l != lang) continue;
            std::string out = text;
            std::size_t n   = 0;
            for (const auto arg : args) {
                const auto mark = '{' + std::to_string(n++) + '}';
                for (auto p = out.find(mark); p != npos; p = out.find(mark, p + arg.size())) out.replace(p, mark.size(), arg);
            }
            return out;
        }
        return t.en.empty() ? std::string() : TLF(t.en, args);
    }

    std::string DescribeControl(int c, std::string_view event, Device device)
    {
        const auto notes = BuiltIn();
        const auto lc    = LogicalContext(c);
        const auto ev    = NormEvent(event);

        // Hotkey1..Hotkey8: quick slots
        if (ev.size() == 7 && ev.starts_with("hotkey") && std::isdigit(static_cast<unsigned char>(ev[6])))
            return Resolve(lc == kCtxFavorites ? notes->quickSlotFavorites : notes->quickSlot, { std::string_view(&ev[6], 1) });
        if (lc != kCtxAny)
            for (const auto& t : notes->controls)
                if (t.ctx == lc && t.event == ev) return Resolve(t.text);
        for (const auto& t : notes->devices)
            if (t.device == device && t.event == ev) return Resolve(t.text);
        for (const auto& t : notes->controls)
            if (t.ctx == kCtxAny && t.event == ev) return Resolve(t.text);
        // directions without a specific entry
        for (const auto& t : notes->directions)
            if (t.event == ev) return lc == kCtxAny ? Resolve(t.text) : TLF("{0} in: {1}", { Resolve(t.text), TL(kCtxInfo[lc].where) });
        return {};
    }

    // ---------------------------------------------------------------- Notes.json

    namespace
    {
        const fs::path kNotesPath = "Data/SKSE/Plugins/HotkeyAtlas/Notes.json";

        // "text" or { "english": "text", "russian": "текст" }
        LocText ReadLocText(const JNode& n)
        {
            LocText t;
            if (n.t == JNode::T::Str) {
                t.en = n.s;
            } else if (n.t == JNode::T::Obj) {
                for (const auto& [lang, v] : n.obj) {
                    if (v.t != JNode::T::Str || v.s.empty()) continue;
                    if (Lower(lang) == "english")
                        t.en = v.s;
                    else
                        t.tr.emplace_back(Lower(lang), v.s);
                }
            }
            return t;
        }

        std::optional<int> ContextFromName(std::string_view name)
        {
            static constexpr std::pair<std::string_view, int> kNames[] = { { "gameplay", kCtxGameplay }, { "menu", kCtxMenu },
                { "console", kCtxConsole }, { "itemmenu", kCtxItemMenu }, { "inventory", kCtxInventory }, { "debugtext", kCtxDebugText },
                { "favorites", kCtxFavorites }, { "map", kCtxMap }, { "stats", kCtxStats }, { "cursor", kCtxCursor }, { "book", kCtxBook },
                { "debugoverlay", kCtxDebugOverlay }, { "journal", kCtxJournal }, { "tfc", kCtxTFC }, { "mapdebug", kCtxMapDebug },
                { "lockpicking", kCtxLockpicking }, { "marketplace", kCtxMarketplace }, { "favor", kCtxFavor }, { "any", kCtxAny } };
            const auto n = NormEvent(name);
            for (const auto& [k, c] : kNames)
                if (k == n) return c;
            return std::nullopt;
        }
    }

    // event -> text pairs of an object
    template <class F>
    void ForEachText(const JNode* n, F&& f)
    {
        if (n && n->t == JNode::T::Obj)
            for (const auto& [event, v] : n->obj)
                if (auto t = ReadLocText(v); !t.en.empty() || !t.tr.empty()) f(NormEvent(event), std::move(t));
    }

    // Keeps the notes read last when the file is missing or broken.
    void LoadBuiltInNotes()
    {
        std::ifstream in(kNotesPath, std::ios::binary);
        if (!in) {
            logger::warn("notes: {} not found, controls have no built-in notes", kNotesPath.generic_string());
            return;
        }
        const std::string text(std::istreambuf_iterator<char>(in), {});
        JNode             root;
        try {
            root = JsonDom(text).Parse();
        } catch (...) {
            logger::error("notes: {} is not valid JSON, the previous notes stay", kNotesPath.generic_string());
            return;
        }

        auto notes = std::make_shared<BuiltInNotes>();
        if (const auto* controls = root.Get("controls"); controls && controls->t == JNode::T::Obj) {
            for (const auto& [name, group] : controls->obj) {
                const auto ctx = ContextFromName(name);
                if (!ctx) {
                    logger::warn("notes: unknown context \"{}\"", name);
                    continue;
                }
                ForEachText(&group, [&](std::string ev, LocText t) { notes->controls.push_back({ *ctx, std::move(ev), std::move(t) }); });
            }
        }
        if (const auto* devices = root.Get("devices"); devices && devices->t == JNode::T::Obj) {
            for (const auto& [name, group] : devices->obj) {
                const auto n = NormEvent(name);
                const auto d = n == "keyboard" ? std::optional(Device::Keyboard)
                             : n == "mouse"    ? std::optional(Device::Mouse)
                             : n == "gamepad"  ? std::optional(Device::Gamepad)
                                               : std::nullopt;
                if (!d) {
                    logger::warn("notes: unknown device \"{}\"", name);
                    continue;
                }
                ForEachText(&group, [&](std::string ev, LocText t) { notes->devices.push_back({ *d, std::move(ev), std::move(t) }); });
            }
        }
        ForEachText(root.Get("directions"), [&](std::string ev, LocText t) { notes->directions.push_back({ kCtxAny, std::move(ev), std::move(t) }); });
        if (const auto* slots = root.Get("quickSlots")) {
            if (const auto* t = slots->Get("use")) notes->quickSlot = ReadLocText(*t);
            if (const auto* t = slots->Get("favorites")) notes->quickSlotFavorites = ReadLocText(*t);
        }
        if (const auto* mods = root.Get("mods"); mods && mods->t == JNode::T::Arr) {
            for (const auto& m : mods->arr) {
                ModNote note;
                note.mod     = Lower(m.Str("mod"));
                note.file    = Lower(m.Str("file"));
                note.section = Lower(m.Str("section"));
                note.setting = Lower(m.Str("setting"));
                note.action  = Lower(m.Str("action"));
                std::ranges::replace(note.file, '\\', '/');
                if (const auto* t = m.Get("text")) note.text = ReadLocText(*t);
                if ((note.setting.empty() && note.action.empty()) || (note.mod.empty() && note.file.empty()) ||
                    (note.text.en.empty() && note.text.tr.empty())) {
                    logger::warn("notes: mod entry \"{}\" / \"{}\" needs setting or action, mod or file, and text", m.Str("mod"),
                        m.Str("setting").empty() ? m.Str("action") : m.Str("setting"));
                    continue;
                }
                notes->mods.push_back(std::move(note));
            }
        }
        logger::info("notes: {} controls, {} mod hotkeys", notes->controls.size() + notes->devices.size() + notes->directions.size(), notes->mods.size());

        std::lock_guard l(g_builtInLock);
        g_builtIn = std::move(notes);
    }

    // Built-in note of a mod hotkey: the first entry of Notes.json "mods" that matches.
    std::string DescribeModBinding(const Binding& b, const BuiltInNotes& notes)
    {
        if (notes.mods.empty()) return {};
        const auto origin = Lower(b.origin);
        const auto slash  = origin.find_last_of('/');
        const auto name   = slash == npos ? origin : origin.substr(slash + 1);
        for (const auto& m : notes.mods) {
            if (!m.setting.empty() && m.setting != Lower(b.iniKey)) continue;
            if (!m.action.empty() && m.action != Lower(b.action)) continue;
            if (!m.mod.empty() && m.mod != Lower(b.owner)) continue;
            if (!m.section.empty() && m.section != Lower(b.context)) continue;
            if (!m.file.empty() && m.file != (m.file.find('/') == npos ? name : origin)) continue;
            return Resolve(m.text);
        }
        return {};
    }
}
