#pragma once

// Small JSON reader (MCM Helper config.json, Notes.json).

#include "Internal.h"

namespace HA
{
    // Small JSON tree, enough for MCM Helper's config.json and Notes.json.
    struct JNode
    {
        enum class T
        {
            Null,
            Str,
            Obj,
            Arr,
            Other
        } t = T::Null;
        std::string                                s;
        std::vector<std::pair<std::string, JNode>> obj;
        std::vector<JNode>                         arr;

        const JNode* Get(std::string_view k) const
        {
            for (const auto& [name, v] : obj)
                if (name == k) return &v;
            return nullptr;
        }
        std::string Str(std::string_view k) const
        {
            const auto* v = Get(k);
            return v && v->t == T::Str ? v->s : std::string();
        }
    };

    class JsonDom
    {
    public:
        explicit JsonDom(const std::string& s) : _s(s)
        {
            if (_s.compare(0, kBom.size(), kBom) == 0) _i = kBom.size();
        }
        JNode Parse(int depth = 0)
        {
            if (depth > 64) throw std::runtime_error("json too deep");
            JNode n;
            const char c = Peek();
            if (c == '{') {
                ++_i;
                n.t = JNode::T::Obj;
                if (Peek() == '}') return ++_i, n;
                for (;;) {
                    auto key = Str();
                    Expect(':');
                    n.obj.emplace_back(std::move(key), Parse(depth + 1));
                    if (Peek() == ',') {
                        ++_i;
                        continue;
                    }
                    Expect('}');
                    return n;
                }
            }
            if (c == '[') {
                ++_i;
                n.t = JNode::T::Arr;
                if (Peek() == ']') return ++_i, n;
                for (;;) {
                    n.arr.push_back(Parse(depth + 1));
                    if (Peek() == ',') {
                        ++_i;
                        continue;
                    }
                    Expect(']');
                    return n;
                }
            }
            if (c == '"') {
                n.t = JNode::T::Str;
                n.s = Str();
                return n;
            }
            const auto start = _i;
            while (_i < _s.size() && !std::strchr(",]} \t\r\n", _s[_i])) ++_i;
            if (_i == start) throw std::runtime_error("bad json");
            n.t = JNode::T::Other;
            return n;
        }

    private:
        const std::string& _s;
        std::size_t        _i = 0;

        char Peek()
        {
            while (_i < _s.size() && (_s[_i] == ' ' || _s[_i] == '\t' || _s[_i] == '\r' || _s[_i] == '\n')) ++_i;
            return _i < _s.size() ? _s[_i] : '\0';
        }
        void Expect(char c)
        {
            if (Peek() != c) throw std::runtime_error("bad json");
            ++_i;
        }
        std::string Str()
        {
            Expect('"');
            std::string r;
            while (_i < _s.size() && _s[_i] != '"') {
                if (_s[_i] == '\\' && _i + 1 < _s.size()) {
                    const char e = _s[++_i];
                    r += e == 'n' ? '\n' : e == 't' ? '\t' : e;  // \uXXXX kept raw: only labels use it
                    ++_i;
                    continue;
                }
                r += _s[_i++];
            }
            if (_i >= _s.size()) throw std::runtime_error("bad json");
            ++_i;
            return r;
        }
    };
}
