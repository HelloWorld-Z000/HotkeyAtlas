// Interface translation: Translations/<language>.txt, looked up by the English text.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- translation

    namespace
    {
        struct SvHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        };
        using Dict = std::unordered_map<std::string, std::string, SvHash, std::equal_to<>>;

        const fs::path                     kTranslationDir = "Data/SKSE/Plugins/HotkeyAtlas/Translations";
    }
    std::mutex                         g_trLock;
    namespace
    {
        std::shared_ptr<const Dict>        g_dict = std::make_shared<const Dict>();
    }
    std::vector<std::shared_ptr<Dict>> g_oldDicts;  // TL() hands out pointers into these: never freed
    std::string                        g_language;  // from HotkeyAtlas.ini, guarded by g_trLock
    namespace
    {
        std::string                        g_activeLanguage;

        const std::string* Translate(std::string_view en)
        {
            std::lock_guard l(g_trLock);
            const auto      it = g_dict->find(en);
            return it != g_dict->end() && !it->second.empty() ? &it->second : nullptr;
        }

        // "\n" -> newline, "\\" -> backslash
        std::string Unescape(std::string_view s)
        {
            std::string out;
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    const char n = s[++i];
                    out += n == 'n' ? '\n' : n == 't' ? '\t' : n;
                } else {
                    out += s[i];
                }
            }
            return out;
        }
    }

    const char* TL(const char* en)
    {
        const auto* t = Translate(en);
        return t ? t->c_str() : en;
    }

    const std::string& TL(const std::string& en)
    {
        const auto* t = Translate(en);
        return t ? *t : en;
    }

    std::string TLF(std::string_view en, std::initializer_list<std::string_view> args)
    {
        const auto* t   = Translate(en);
        std::string out = t ? *t : std::string(en);
        std::size_t n   = 0;
        for (const auto arg : args) {
            const auto mark = '{' + std::to_string(n++) + '}';
            for (auto p = out.find(mark); p != npos; p = out.find(mark, p + arg.size())) out.replace(p, mark.size(), arg);
        }
        return out;
    }

    std::vector<std::string> Languages()
    {
        std::vector<std::string> out;
        std::error_code          ec;
        for (fs::directory_iterator it(kTranslationDir, ec), end; !ec && it != end; it.increment(ec))
            if (Lower(it->path().extension().string()) == ".txt") out.push_back(Lower(it->path().stem().string()));
        std::ranges::sort(out);
        return out;
    }

    std::string Language()
    {
        std::lock_guard l(g_trLock);
        return g_language;
    }

    std::string ActiveLanguage()
    {
        std::lock_guard l(g_trLock);
        return g_activeLanguage;
    }

    // Reads <language>.txt: "English text = translation" per line, UTF-8, ; comments.
    void LoadTranslation()
    {
        auto lang = Language();
        if (lang.empty()) {
            if (auto* s = RE::GetINISetting("sLanguage:General"); s && s->GetString()) lang = Lower(s->GetString());
            if (lang.empty()) lang = "english";
        }

        auto          dict = std::make_shared<Dict>();
        std::ifstream in(kTranslationDir / (lang + ".txt"), std::ios::binary);
        std::string   line;
        for (bool first = true; in && std::getline(in, line); first = false) {
            if (first && line.starts_with(kBom)) line.erase(0, kBom.size());
            const auto t = Trim(line);
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;
            const auto eq = t.find('=');  // English text never contains '='
            if (eq == npos) continue;
            auto en = Unescape(Trim(t.substr(0, eq)));
            auto tr = Unescape(Trim(t.substr(eq + 1)));
            if (!en.empty()) (*dict)[std::move(en)] = std::move(tr);
        }
        logger::info("translation: {} ({} strings)", lang, dict->size());

        std::lock_guard l(g_trLock);
        g_oldDicts.push_back(dict);
        g_dict           = std::move(dict);
        g_activeLanguage = lang;
    }

    void SetLanguage(std::string lang)
    {
        lang = Lower(Trim(lang));
        {
            std::lock_guard l(g_trLock);
            if (g_language == lang) return;
            g_language = lang;
        }
        LoadTranslation();
        SaveConfigQuiet();
        Rescan();  // descriptions and context names are translated while scanning
    }
}
