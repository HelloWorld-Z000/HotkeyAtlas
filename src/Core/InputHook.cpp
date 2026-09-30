// Input hook: combos, keys moved to mouse / gamepad buttons and mod key remaps.

#include "Internal.h"

namespace HA
{
    // ---------------------------------------------------------------- combo input hook
    // Game thread only: rebuilt whenever overrides change, read by the input hook.

    namespace
    {
        struct ComboBind
        {
            int               ctx;
            std::uint32_t     key;
            std::uint8_t      mods;
            std::uint32_t     hold;  // input that must be held too (see HoldOf), 0 = none
            RE::BSFixedString event;
        };

        // A mod's key moved by the user: pressing `from` makes the mod see `to` (its own key).
        struct Remap
        {
            std::uint32_t from;  // combo the user presses now
            std::uint32_t to;    // combo written in the mod's config
        };

        std::vector<ComboBind>                               g_combos;
        // Skyrim controls moved from a key to a mouse button, or given an extra gamepad button:
        // the button press gets the event
        struct ButtonBind
        {
            int               ctx;
            std::uint32_t     code;  // mouse or gamepad code, see MakeCode; may carry a held input
            RE::BSFixedString event;
        };
        std::vector<ButtonBind>                              g_buttonBinds;
        std::vector<ButtonBind>                              g_stickBinds;  // same, on a stick used as a button
        bool                                                 g_stickUsed = false;  // any stick bind or remap from a stick
        std::unordered_map<std::uint32_t, RE::BSFixedString> g_activeButtons;  // mouse code held -> event
        std::unordered_map<std::uint32_t, RE::BSFixedString> g_activeCombos;  // key held as part of a combo -> event
        std::vector<Remap>                                   g_remaps;
        std::vector<std::uint32_t>                           g_blocked;       // mods' original combos, hidden from them
        std::unordered_map<std::uint32_t, std::uint32_t>     g_activeRemaps;  // physical key held -> combo shown to mods

        constexpr std::uint32_t kHiddenKey = 0xFF;  // idCode given to a blocked key press
    }

    // Caller holds g_ovLock.
    void RebuildComboTableLocked()
    {
        g_combos.clear();
        g_buttonBinds.clear();
        g_stickBinds.clear();
        // <ctx>|<event>|<original key> -> ctx, event
        const auto parse = [](const std::string& id) -> std::optional<std::pair<int, RE::BSFixedString>> {
            const auto p1 = id.find('|');
            const auto p2 = id.rfind('|');
            if (p1 == npos || p2 == p1) return std::nullopt;
            const auto ctx = ParseUInt(std::string_view(id).substr(0, p1));
            if (!ctx) return std::nullopt;
            return std::pair{ static_cast<int>(*ctx), RE::BSFixedString(id.substr(p1 + 1, p2 - p1 - 1)) };
        };
        for (const auto& [id, ov] : g_overrides) {
            const bool held    = HoldOf(ov.key) != 0;
            const bool combo   = CodeDevice(ov.key) == Device::Keyboard && ov.key != kUnbound && (ComboMods(ov.key) || held);
            const bool toMouse = CodeDevice(ov.original) == Device::Keyboard && CodeDevice(ov.key) == Device::Mouse;
            const bool toStick = IsStick(ov.key) && !IsStick(ov.original);
            const bool padHold = held && CodeDevice(ov.key) == Device::Gamepad;  // gamepad combo: fired by the hook too
            if (!combo && !toMouse && !toStick && !padHold) continue;
            const auto p = parse(id);
            if (!p) continue;
            const auto& [ctx, event] = *p;
            if (combo)
                g_combos.push_back({ ctx, ComboKey(ov.key), ComboMods(ov.key), HoldOf(ov.key), event });
            else
                (toStick ? g_stickBinds : g_buttonBinds).push_back({ ctx, ov.key, event });
        }
        for (const auto& [id, ov] : g_padControls)
            if (const auto p = parse(id)) (IsStick(ov.key) ? g_stickBinds : g_buttonBinds).push_back({ p->first, ov.key, p->second });

        g_remaps.clear();
        g_blocked.clear();
        // an unbound key has no new combo: nothing to translate, only the old one to hide
        for (const auto& [id, ov] : g_fileEdits)
            if (ov.key != ov.original && ov.key != kUnbound) g_remaps.push_back({ ov.key, ov.original });
        // an added gamepad button shows the mod its own key; the key itself keeps working
        for (const auto& [id, ov] : g_padMods) g_remaps.push_back({ ov.key, ov.original });
        g_stickUsed = !g_stickBinds.empty() || std::ranges::any_of(g_remaps, [](const Remap& r) { return IsStick(r.from); });
        // the old combo stops working for the mod, unless it is also some remap's new combo
        for (const auto& [id, ov] : g_fileEdits)
            if (ov.key != ov.original && std::ranges::none_of(g_remaps, [&](const Remap& o) { return o.from == ov.original; }))
                g_blocked.push_back(ov.original);
    }

    // Physical modifier state straight from Windows: works regardless of which UI has focus.
    std::uint8_t HeldModsOS()
    {
        std::uint8_t m = 0;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) m |= kShift;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) m |= kCtrl;
        if (GetAsyncKeyState(VK_MENU) & 0x8000) m |= kAlt;
        return m;
    }

    namespace
    {
        int ActiveContext()
        {
            auto* cm = RE::ControlMap::GetSingleton();
            if (!cm) return 0;
            const auto& stack = cm->GetRuntimeData().contextPriorityStack;
            return stack.empty() ? 0 : static_cast<int>(stack.back());
        }

        // Synthetic modifier events, allocated once from the game heap and reused every frame.
        // They are linked into the event list only for the duration of one dispatch.
        class EventPool
        {
        public:
            void Reset() { _used = 0; }

            // Keyboard key. down: IsDown() (first frame of a press); up: IsUp()
            RE::InputEvent* Button(std::uint32_t dik, bool down)
            {
                return Button(RE::INPUT_DEVICE::kKeyboard, dik, down ? 1.0f : 0.0f, down ? 0.0f : 0.1f, ""sv);  // no event: no Skyrim action
            }

            RE::InputEvent* Button(RE::INPUT_DEVICE device, std::uint32_t id, float value, float held, const RE::BSFixedString& event)
            {
                if (_used == _pool.size()) {
                    auto* e = RE::ButtonEvent::Create(RE::INPUT_DEVICE::kKeyboard, ""sv, id, 0.0f, 0.0f);
                    if (!e) return nullptr;
                    _pool.push_back(e);
                }
                auto* e   = _pool[_used++];
                e->device = device;
                e->SetIDCode(id);
                e->SetUserEvent(event);
                e->GetRuntimeData().value        = value;
                e->GetRuntimeData().heldDownSecs = held;
                e->next                          = nullptr;
                return e;
            }

        private:
            std::vector<RE::ButtonEvent*> _pool;
            std::size_t                   _used = 0;
        };
        EventPool g_eventPool;

        // Synthetic modifier presses/releases taking what mods see from state `from` to `to`.
        void ModifierTransition(std::uint8_t from, std::uint8_t to, std::vector<RE::InputEvent*>& out)
        {
            for (int bit = 0; bit < 3; ++bit) {
                const bool f = from & kModBits[bit], t = to & kModBits[bit];
                if (f == t) continue;
                if (auto* e = g_eventPool.Button(kModDik[bit], t)) out.push_back(e);
            }
        }

        // Rewrites keyboard button events before any sink sees them:
        //  - vanilla combos get the user event of their control;
        //  - a remapped mod key is shown to mods as the key in their own config (idCode),
        //    with fake modifier presses when that key needs different modifiers;
        //  - the mod's original key is hidden (idCode 0xFF), Skyrim's own action on it stays.
        // Returns the (possibly new) head of the list; `restore` undoes the relinking.
        // Keys whose "down" is sent one frame late, after the synthetic modifiers (see present()).
        std::vector<std::uint32_t> g_pendingDown;

        // Synthetic key releases for mouse wheel ticks shown to mods as a key: the wheel has
        // no "up", so the key is let go a couple of frames later.
        struct PendingUp
        {
            std::uint32_t combo;
            int           frames;
        };
        std::vector<PendingUp> g_pendingUp;

        // ---- gamepad state straight from XInput: the menu does not pass gamepad buttons on,
        // and a stick used as a button needs its deflection

        using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);

        XInputGetStateFn XInputGetStatePtr()
        {
            static const auto fn = []() -> XInputGetStateFn {
                for (auto dll : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
                    if (auto* h = LoadLibraryW(dll)) return reinterpret_cast<XInputGetStateFn>(GetProcAddress(h, "XInputGetState"));
                return nullptr;
            }();
            return fn;
        }
    }

    // Every connected pad merged. Asking XInput about an empty slot is slow, so empty slots
    // are probed only once a second (this runs every frame while a stick is in use).
    PadState ReadPads()
    {
        static std::atomic<DWORD>     connected{ 0 };
        static std::atomic<ULONGLONG> nextProbe{ 0 };
        PadState                      out;
        auto*                         get = XInputGetStatePtr();
        if (!get) return out;
        const auto now   = GetTickCount64();
        const bool probe = now >= nextProbe.load();
        if (probe) nextProbe = now + 1000;
        const auto push = [](SHORT x, SHORT y) {
            return (std::min)(1.0f, std::sqrt(static_cast<float>(x) * x + static_cast<float>(y) * y) / 32767.0f);
        };
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            const DWORD bit = 1u << i;
            if (!(connected & bit) && !probe) continue;
            XINPUT_STATE st{};
            if (get(i, &st) != ERROR_SUCCESS) {
                connected &= ~bit;
                continue;
            }
            connected |= bit;
            out.buttons |= st.Gamepad.wButtons;
            if (st.Gamepad.bLeftTrigger > 128) out.buttons |= 1u << 16;
            if (st.Gamepad.bRightTrigger > 128) out.buttons |= 1u << 17;
            out.stick[0] = (std::max)(out.stick[0], push(st.Gamepad.sThumbLX, st.Gamepad.sThumbLY));
            out.stick[1] = (std::max)(out.stick[1], push(st.Gamepad.sThumbRX, st.Gamepad.sThumbRY));
        }
        return out;
    }

    namespace
    {
        // A stick used as a button: pushed past the threshold = pressed, back to the centre = released.
        struct StickPress
        {
            bool              down    = false;
            ULONGLONG         since   = 0;         // GetTickCount64() at the press
            std::uint32_t     remapTo = kUnbound;  // mod key shown while it is pushed
            RE::BSFixedString event;               // or the Skyrim control it fires
        };
        StickPress g_stickPress[2];  // left, right
    }

    // Whether the held part of a combo is down right now, read from Windows / XInput: the
    // event stream only tells about changes.
    bool IsHeld(std::uint32_t hold)
    {
        const auto id = CodeId(hold);
        switch (CodeDevice(hold)) {
        case Device::Keyboard:
            if (const auto bit = ModBitForDik(id)) return HeldModsOS() & bit;  // either Shift, Ctrl, Alt
            if (const auto vk = DikToVk(id)) return GetAsyncKeyState(static_cast<int>(*vk)) & 0x8000;
            return false;
        case Device::Mouse:
            {
                static constexpr int kVk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
                return id < std::size(kVk) && (GetAsyncKeyState(kVk[id]) & 0x8000);
            }
        default:
            {
                const auto pads = ReadPads();
                if (id == kPadLeftStick || id == kPadRightStick) return pads.stick[id == kPadRightStick] > 0.6f;
                return pads.buttons & (id == kPadLT ? 1u << 16 : id == kPadRT ? 1u << 17 : id);
            }
        }
    }

    namespace
    {
        bool HoldOk(std::uint32_t code)
        {
            const auto h = HoldOf(code);
            return !h || IsHeld(h);
        }
    }

    // Of the entries whose code (without the held part) is `base` and whose held input is
    // down, the most specific one: a combo with a held input wins over the plain key.
    template <class T, class Code>
    T* BestMatch(std::vector<T>& v, std::uint32_t base, Code code, int ctx = -1)
    {
        T* best = nullptr;
        for (auto& e : v) {
            if constexpr (requires { e.ctx; })
                if (e.ctx != ctx) continue;
            const auto c = code(e);
            if (BaseCode(c) != base || !HoldOk(c)) continue;
            if (!best || (HoldOf(c) && !HoldOf(code(*best)))) best = &e;
        }
        return best;
    }

    namespace
    {
        RE::InputEvent* ProcessInput(RE::InputEvent* head, std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>>& restore)
        {
            if (g_combos.empty() && g_activeCombos.empty() && g_remaps.empty() && g_activeRemaps.empty() && g_pendingDown.empty() &&
                g_buttonBinds.empty() && g_activeButtons.empty() && g_pendingUp.empty() && !g_stickUsed && !g_stickPress[0].down &&
                !g_stickPress[1].down)
                return head;
            g_eventPool.Reset();

            std::vector<RE::InputEvent*> seq;
            bool                         relinked = false;
            int                          ctx      = -1;
            const auto                   held     = HeldModsOS();

            // A key press made up for a mod whose key was moved to a mouse button, with the
            // modifiers it needs (the key itself one frame later when they change, see present()).
            const auto keyDown = [&](std::uint32_t combo) {
                std::vector<RE::InputEvent*> mods;
                ModifierTransition(held, ComboMods(combo), mods);
                seq.insert(seq.end(), mods.begin(), mods.end());
                if (!mods.empty())
                    g_pendingDown.push_back(ComboKey(combo));
                else if (auto* d = g_eventPool.Button(ComboKey(combo), true))
                    seq.push_back(d);
                relinked = true;
            };
            const auto keyUp = [&](std::uint32_t combo) {
                if (auto* u = g_eventPool.Button(ComboKey(combo), false)) seq.push_back(u);
                ModifierTransition(ComboMods(combo), held, seq);
                relinked = true;
            };

            // the delayed key downs go first, before this frame's events (a quick tap's "up" may follow)
            for (const auto key : std::exchange(g_pendingDown, {}))
                if (auto* d = g_eventPool.Button(key, true)) {
                    seq.push_back(d);
                    relinked = true;
                }
            for (auto it = g_pendingUp.begin(); it != g_pendingUp.end();) {
                if (--it->frames > 0) {
                    ++it;
                    continue;
                }
                keyUp(it->combo);
                it = g_pendingUp.erase(it);
            }

            // Sticks used as buttons. Read from XInput every frame: thumbstick events are not
            // sent while a stick stays put. A held press repeats every frame like a real button.
            if (g_stickUsed || g_stickPress[0].down || g_stickPress[1].down) {
                const auto pads = ReadPads();
                const auto now  = GetTickCount64();
                for (int s = 0; s < 2; ++s) {
                    auto&      st = g_stickPress[s];
                    const auto id = s ? kPadRightStick : kPadLeftStick;
                    const bool on = pads.stick[s] > (st.down ? 0.35f : 0.6f);  // hysteresis: no flicker at the edge
                    if (!on && !st.down) continue;
                    const bool press = !st.down;
                    if (press) {
                        const auto code = MakeCode(Device::Gamepad, id);
                        st              = {};
                        if (const auto r = std::ranges::find(g_remaps, code, &Remap::from); r != g_remaps.end()) {
                            st.remapTo = r->to;
                        } else if (!g_stickBinds.empty()) {
                            if (ctx < 0) ctx = ActiveContext();
                            const auto b = std::ranges::find_if(g_stickBinds, [&](const ButtonBind& b) { return b.code == code && b.ctx == ctx; });
                            if (b != g_stickBinds.end()) st.event = b->event;
                        }
                        if (st.remapTo == kUnbound && st.event.empty()) continue;  // nothing on this stick here
                        st.down  = true;
                        st.since = now;
                    }
                    const bool release = !on;
                    if (release) st.down = false;
                    if (st.remapTo != kUnbound && CodeDevice(st.remapTo) == Device::Keyboard) {
                        if (press)
                            keyDown(st.remapTo);
                        else if (release)
                            keyUp(st.remapTo);
                        continue;
                    }
                    const float     secs = press ? 0.0f : (std::max)(0.001f, static_cast<float>(now - st.since) / 1000.0f);
                    RE::InputEvent* e    = nullptr;
                    if (st.remapTo != kUnbound)  // a mod's mouse or gamepad button
                        e = g_eventPool.Button(CodeDevice(st.remapTo) == Device::Mouse ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kGamepad,
                            CodeId(st.remapTo), release ? 0.0f : 1.0f, secs, ""sv);
                    else
                        e = g_eventPool.Button(RE::INPUT_DEVICE::kGamepad, id, release ? 0.0f : 1.0f, secs, st.event);
                    if (e) {
                        seq.push_back(e);
                        relinked = true;
                    }
                }
            }

            for (auto* e = head; e; e = e->next) {
                restore.emplace_back(e, e->next);
                // mouse / gamepad buttons:
                //  - a mod's button moved to another button of the same device is shown to it as
                //    its own button, its old button is hidden;
                //  - a mod's key moved to a mouse button: the button is hidden and the key pressed;
                //  - a Skyrim control moved from a key to a mouse button gets the button's press.
                if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kButton &&
                    (e->GetDevice() == RE::INPUT_DEVICE::kMouse || e->GetDevice() == RE::INPUT_DEVICE::kGamepad)) {
                    auto*      btn  = e->AsButtonEvent();
                    const auto code = MakeCode(e->GetDevice() == RE::INPUT_DEVICE::kMouse ? Device::Mouse : Device::Gamepad, btn->GetIDCode());
                    const auto hide = [&] {
                        btn->SetIDCode(kHiddenKey);
                        btn->SetUserEvent(""sv);
                    };
                    const auto show = [&](std::uint32_t to) {
                        btn->SetIDCode(to == kHiddenKey ? kHiddenKey : CodeId(to));
                        if (to == kHiddenKey) return;
                        btn->SetUserEvent(""sv);  // the button now belongs to the mod action
                        // a gamepad button added to a mod's mouse button is shown as that mouse button
                        btn->device = CodeDevice(to) == Device::Mouse ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kGamepad;
                    };
                    seq.push_back(e);  // synthetic keys go after it

                    if (const auto it = g_activeRemaps.find(code); it != g_activeRemaps.end()) {
                        const auto to       = it->second;
                        const bool released = !btn->IsPressed();
                        if (to != kHiddenKey && CodeDevice(to) == Device::Keyboard) {
                            hide();
                            if (released) keyUp(to);
                        } else {
                            show(to);
                        }
                        if (released) g_activeRemaps.erase(it);
                        continue;
                    }
                    if (const auto it = g_activeButtons.find(code); it != g_activeButtons.end()) {
                        btn->SetUserEvent(it->second);
                        if (!btn->IsPressed()) g_activeButtons.erase(it);
                        continue;
                    }
                    if (!btn->IsDown()) continue;

                    if (const auto* r = BestMatch(g_remaps, code, [](const Remap& r) { return r.from; })) {
                        if (CodeDevice(r->to) == Device::Keyboard) {
                            hide();
                            keyDown(r->to);
                            if (IsWheel(code))
                                g_pendingUp.push_back({ r->to, 3 });
                            else
                                g_activeRemaps[code] = r->to;
                        } else {
                            show(r->to);
                            if (!IsWheel(code)) g_activeRemaps[code] = r->to;
                        }
                        continue;
                    }
                    if (!g_buttonBinds.empty()) {
                        if (ctx < 0) ctx = ActiveContext();
                        if (const auto* b = BestMatch(g_buttonBinds, code, [](const ButtonBind& b) { return b.code; }, ctx)) {
                            btn->SetUserEvent(b->event);
                            if (!IsWheel(code)) g_activeButtons[code] = b->event;
                            continue;
                        }
                    }
                    if (std::ranges::find(g_blocked, code) != g_blocked.end()) {
                        show(kHiddenKey);
                        if (!IsWheel(code)) g_activeRemaps[code] = kHiddenKey;
                    }
                    continue;
                }                if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                    seq.push_back(e);
                    continue;
                }
                auto*      btn = e->AsButtonEvent();
                const auto key = btn->GetIDCode();

                // Show the mods `combo` instead of this key. Modifiers change like a real key
                // press: switched to the combo's before the key goes down, switched back after
                // it comes up, untouched while it is held. Doing it every frame floods ImGui
                // based mods (OAR) with modifier events: lag and a stuck queue.
                const auto present = [&](std::uint32_t combo) {
                    btn->SetIDCode(ComboKey(combo));
                    btn->SetUserEvent(""sv);  // the key now belongs to the mod action
                    std::vector<RE::InputEvent*> before, after;
                    if (btn->IsDown()) {
                        ModifierTransition(held, ComboMods(combo), before);
                        if (!before.empty()) {
                            // ImGui applies modifier events at the start of the next frame (OAR
                            // closes its menu on io.KeyShift): modifiers this frame, key next frame
                            btn->SetIDCode(kHiddenKey);
                            g_pendingDown.push_back(ComboKey(combo));
                        }
                    } else if (!btn->IsPressed()) {
                        ModifierTransition(ComboMods(combo), held, after);
                    }
                    relinked |= !before.empty() || !after.empty();
                    seq.insert(seq.end(), before.begin(), before.end());
                    seq.push_back(e);
                    seq.insert(seq.end(), after.begin(), after.end());
                };

                // a press already being translated: keep it consistent until release
                if (const auto it = g_activeRemaps.find(key); it != g_activeRemaps.end()) {
                    const auto combo = it->second;
                    if (!btn->IsPressed()) g_activeRemaps.erase(it);
                    if (combo == kHiddenKey) {
                        btn->SetIDCode(kHiddenKey);
                        seq.push_back(e);
                    } else {
                        present(combo);
                    }
                    continue;
                }
                if (const auto it = g_activeCombos.find(key); it != g_activeCombos.end()) {
                    btn->SetUserEvent(it->second);
                    if (!btn->IsPressed()) g_activeCombos.erase(it);
                    seq.push_back(e);
                    continue;
                }
                if (!btn->IsDown()) {
                    seq.push_back(e);
                    continue;
                }

                // a modifier pressed as the key itself doesn't count as its own modifier
                const auto pressed = Combo(key, static_cast<std::uint8_t>(held & ~ModBitForDik(key)));
                if (const auto* r = BestMatch(g_remaps, pressed, [](const Remap& r) { return r.from; })) {
                    g_activeRemaps[key] = r->to;
                    present(r->to);
                    continue;
                }
                if (!g_combos.empty()) {
                    if (ctx < 0) ctx = ActiveContext();
                    const auto* c = BestMatch(g_combos, pressed, [](const ComboBind& c) { return WithHold(Combo(c.key, c.mods), c.hold); }, ctx);
                    if (c) {
                        btn->SetUserEvent(c->event);
                        g_activeCombos[key] = c->event;
                        seq.push_back(e);
                        continue;
                    }
                }
                if (std::ranges::find(g_blocked, pressed) != g_blocked.end()) {
                    btn->SetIDCode(kHiddenKey);
                    g_activeRemaps[key] = kHiddenKey;
                }
                seq.push_back(e);
            }

            if (!relinked || seq.empty()) {
                restore.clear();  // same list, only fields changed
                return head;
            }
            for (std::size_t i = 0; i < seq.size(); ++i) seq[i]->next = i + 1 < seq.size() ? seq[i + 1] : nullptr;
            return seq.front();
        }

        // Other mods (OAR, ...) hook the same call to read input. Whoever hooks last runs first,
        // so we re-hook on top of them after they load. Each layer needs its own thunk and
        // saved original; only the outermost one processes, inner ones just pass through.
        bool g_inDispatch = false;  // game thread only
    }

    template <int N>
    struct InputHook
    {
        static void Thunk(RE::BSTEventSource<RE::InputEvent*>* a_source, RE::InputEvent* const* a_events)
        {
            if (g_inDispatch || !a_events) return func(a_source, a_events);
            g_inDispatch = true;
            std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> restore;
            RE::InputEvent*                                         head = ProcessInput(*a_events, restore);
            func(a_source, &head);
            for (auto& [e, next] : restore) e->next = next;  // hand the game its own list back
            g_inDispatch = false;
        }
        static inline REL::Relocation<decltype(Thunk)> func;
    };

    namespace
    {
        std::atomic<bool> g_hookInstalled{ false };
        std::uintptr_t    g_hookSite   = 0;
        std::uintptr_t    g_ourTarget  = 0;  // call target we wrote most recently
        int               g_hookLayers = 0;
        constexpr int     kMaxLayers   = 4;

        std::uintptr_t CallTarget(std::uintptr_t site)
        {
            return site + 5 + *reinterpret_cast<const std::int32_t*>(site + 1);
        }

        template <int N>
        void WriteLayer()
        {
            InputHook<N>::func = SKSE::GetTrampoline().write_call<5>(g_hookSite, InputHook<N>::Thunk);
        }

        bool AddHookLayer()
        {
            switch (g_hookLayers) {
            case 0: WriteLayer<0>(); break;
            case 1: WriteLayer<1>(); break;
            case 2: WriteLayer<2>(); break;
            case 3: WriteLayer<3>(); break;
            default: return false;
            }
            ++g_hookLayers;
            g_ourTarget = CallTarget(g_hookSite);
            return true;
        }
    }

    bool InputHookInstalled() { return g_hookInstalled.load(); }

    // Hooks the call that hands each frame's input events to their sinks
    // (BSInputDeviceManager poll -> BSTEventSource<InputEvent*>::SendEvent).
    void InstallInputHook()
    {
        REL::Relocation<std::uintptr_t> func{ RELOCATION_ID(67315, 68617) };
        const auto                      site = func.address() + REL::VariantOffset(0x7B, 0x7B, 0x81).offset();
        // must be a rel32 call; anything else means this game version differs: don't patch
        if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
            logger::error("input hook: unexpected code at {:X}, combos and mod remaps are disabled", site);
            return;
        }
        SKSE::AllocTrampoline(14 * kMaxLayers);  // room for the re-hooks below
        g_hookSite = site;
        AddHookLayer();
        g_hookInstalled = true;
        logger::info("input hook installed");
    }

    // Game thread. If a mod loaded after us hooked the same call, it now sees input before
    // we translate it: wrap it once more so we are outermost again.
    void EnsureInputHookOnTop()
    {
        if (!g_hookInstalled || CallTarget(g_hookSite) == g_ourTarget) return;
        if (AddHookLayer())
            logger::info("input hook: another mod hooked input after us, re-hooked on top (layer {})", g_hookLayers);
        else
            logger::warn("input hook: still not first after {} layers, remaps may not reach some mods", kMaxLayers);
    }

    // hook state of inputs held under the old tables. Game thread.
    void ClearActiveInputs()
    {
        g_activeCombos.clear();
        g_activeRemaps.clear();
        g_activeButtons.clear();
    }
}
