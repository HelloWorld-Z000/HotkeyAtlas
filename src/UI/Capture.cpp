// Capturing a new bind: single inputs and "hold one, press another" combos.

#include "UI/UI.h"

namespace HA::UI
{
    namespace
    {
        // ---- capture state. An input pressed alone waits: let go, it is the binding; another
        // input pressed while it is held makes a combo "first + second". Keyboard and mouse
        // combine with each other, the gamepad only with itself.
        std::uint32_t g_pendingModifier = 0;  // Shift / Ctrl / Alt pressed, not yet let go (DIK)
        std::uint32_t g_pendingFirst    = 0;  // key or mouse button held first (code), 0 = none
        std::uint32_t g_pendingPad      = 0;  // gamepad button held first (GamepadButton), 0 = none
    }

    void ClearPending()
    {
        g_pendingModifier = 0;
        g_pendingFirst    = 0;
        g_pendingPad      = 0;
    }

    namespace
    {
        // Windows state first: the menu framework does not always pass modifier keys to ImGui.
        std::uint8_t HeldMods()
        {
            std::uint8_t m = HeldModsOS();
            if (ImGui::IsKeyDown(ImGui::ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGui::ImGuiKey_RightShift)) m |= kShift;
            if (ImGui::IsKeyDown(ImGui::ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGui::ImGuiKey_RightCtrl)) m |= kCtrl;
            if (ImGui::IsKeyDown(ImGui::ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGui::ImGuiKey_RightAlt)) m |= kAlt;
            return m;
        }

        // ---- the menu's own open / close hotkey is switched off while a capture waits, so that
        // key (or gamepad button) can be picked without closing the menu

        bool g_hotkeyOff = false;  // we switched it off
        bool g_hotkeyWas = true;   // its state before that

        std::uint32_t PadButtonsHeld();

        bool AnyInputHeld()
        {
            if (PadButtonsHeld()) return true;
            for (const auto& k : kKeys)
                if (ImGui::IsKeyDown(k.key)) return true;
            return false;
        }
    }

    // Called every frame and whenever a capture starts or ends. The hotkey comes back only
    // once the key that ended the capture is let go, so the release can't toggle the menu.
    void SyncMenuHotkey(bool force)
    {
        const bool off = g_capture.has_value();
        if (off == g_hotkeyOff) return;
        if (off) {
            g_hotkeyWas = SKSEMenuFramework::IsHotkeyEnabled();
            SKSEMenuFramework::SetHotkeyEnabled(false);
        } else {
            if (!force && AnyInputHeld()) return;
            SKSEMenuFramework::SetHotkeyEnabled(g_hotkeyWas);
        }
        g_hotkeyOff = off;
    }

    void EndCapture()
    {
        g_capture.reset();
        ClearPending();
        SyncMenuHotkey();
    }


    namespace
    {
        // Ends a capture with the input code `code` (see MakeCode, WithHold).
        void FinishCapture(std::uint32_t code)
        {
            // a gamepad button for a key or mouse action goes to the action's own gamepad mapping,
            // or is added next to it when there is none: the key stays
            if (CodeDevice(code) == Device::Gamepad && CodeDevice(g_capture->defaultKey) != Device::Gamepad)
                BindGamepad(*g_capture, code);
            else
                Rebind(*g_capture, code);
            g_selDevice   = CodeDevice(code);
            g_selected    = g_selDevice == Device::Keyboard ? ComboKey(code) : CodeId(code);
            g_hasSelected = true;
            EndCapture();
        }

        // Gamepad buttons a capture listens to; sticks only for stick actions (see PadStep).
        constexpr std::uint32_t kPadTargets[]   = { kPadA, kPadB, kPadX, kPadY, kPadLB, kPadRB, kPadLT, kPadRT,
              kPadUp, kPadDown, kPadLeft, kPadRight, kPadBack, kPadStart, kPadL3, kPadR3, kPadLeftStick, kPadRightStick };

        // Triggers and sticks are no XInput button bits (their ids even overlap the D-pad bits).
        constexpr bool IsAnalog(std::uint32_t id) { return id == kPadLT || id == kPadRT || id == kPadLeftStick || id == kPadRightStick; }

        constexpr std::uint32_t kLeftStickBit = 1u << 18, kRightStickBit = 1u << 19;

        // Bit of a gamepad button in PadButtonsHeld().
        constexpr std::uint32_t PadBit(std::uint32_t id)
        {
            return id == kPadLT ? 1u << 16 : id == kPadRT ? 1u << 17 : id == kPadLeftStick ? kLeftStickBit : id == kPadRightStick ? kRightStickBit : id;
        }

        // Held gamepad buttons as GamepadButton bits; LT / RT in bits 16 / 17 and the sticks,
        // when pushed far, in bits 18 / 19.
        std::uint32_t PadButtonsHeld()
        {
            const auto pads = ReadPads();
            auto       held = pads.buttons;
            if (pads.stick[0] > 0.75f) held |= kLeftStickBit;
            if (pads.stick[1] > 0.75f) held |= kRightStickBit;
            return held;
        }

        std::uint32_t g_padPrev = 0;  // PadButtonsHeld() last frame: only new presses count

        // `id` with the gamepad button held first, if it is still held (sticks never combine).
        std::uint32_t PadCompose(std::uint32_t id)
        {
            const bool combo = g_pendingPad && g_pendingPad != id && !IsAnalog(id) && (PadButtonsHeld() & PadBit(g_pendingPad));
            return WithHold(MakeCode(Device::Gamepad, id), combo ? MakeCode(Device::Gamepad, g_pendingPad) : 0);
        }

        // One frame of gamepad capture; the finished code, if any. With `sticks` only a stick
        // push counts (stick actions). Otherwise sticks don't count: they also move the menu
        // cursor, a stray push would bind them.
        std::optional<std::uint32_t> PadStep(bool sticks)
        {
            const auto held  = PadButtonsHeld();
            const auto fresh = held & ~g_padPrev;
            g_padPrev        = held;
            if (sticks) {
                if (fresh & kLeftStickBit) return MakeCode(Device::Gamepad, kPadLeftStick);
                if (fresh & kRightStickBit) return MakeCode(Device::Gamepad, kPadRightStick);
                return std::nullopt;
            }
            std::uint32_t pressed = 0;
            for (const auto b : kPadTargets)
                if (!IsAnalog(b) || b == kPadLT || b == kPadRT)
                    if (fresh & PadBit(b)) {
                        pressed = b;
                        break;
                    }
            if (pressed) {
                if (g_pendingPad && g_pendingPad != pressed) return PadCompose(pressed);  // held first + this one
                g_pendingPad = pressed;
                return std::nullopt;
            }
            if (g_pendingPad && !(held & PadBit(g_pendingPad)))  // let go alone
                return MakeCode(Device::Gamepad, std::exchange(g_pendingPad, 0));
            return std::nullopt;
        }

        // A key or mouse `code` (no modifiers yet) with what is held now: the input held first
        // and Shift / Ctrl / Alt. A key keeps them as modifiers; a mouse button takes the held
        // input, or else a held modifier key, as its held part.
        std::uint32_t KbCompose(std::uint32_t code, std::uint8_t mods)
        {
            std::uint32_t hold = g_pendingFirst != code && g_pendingFirst && IsHeld(g_pendingFirst) ? g_pendingFirst : 0;
            if (CodeDevice(code) == Device::Keyboard) return WithHold(Combo(code, static_cast<std::uint8_t>(mods & ~ModBitForDik(code))), hold);
            if (!hold && mods) {
                if (g_pendingModifier)
                    hold = g_pendingModifier;
                else
                    for (int bit = 0; bit < 3 && !hold; ++bit)
                        if (mods & kModBits[bit]) hold = kModDik[bit];
            }
            return WithHold(code, hold);
        }
    }

    // Starts capturing a new key / button for `b`.
    void StartCapture(const Binding& b)
    {
        g_capture = b;
        ClearPending();
        g_padPrev = PadButtonsHeld();  // buttons already held don't count
        SyncMenuHotkey();
    }

    namespace
    {
        // A mouse button pressed this frame that may be a target: middle, side buttons, wheel.
        std::optional<std::uint32_t> NewMousePress()
        {
            static constexpr std::pair<int, std::uint32_t> kImGuiMouse[] = { { 2, kMouseMiddle }, { 3, kMouse4 }, { 4, kMouse5 } };
            for (const auto& [imgui, button] : kImGuiMouse)
                if (ImGui::IsMouseClicked(imgui)) return button;
            if (const auto* io = ImGui::GetIO(); io && io->MouseWheel != 0.0f) return io->MouseWheel > 0.0f ? kMouseWheelUp : kMouseWheelDown;
            return std::nullopt;
        }

        // Keyboard and mouse capture, one frame; the finished code, if any. `mouseOnly`: the
        // binding must end on a mouse button (keys may still be its held part).
        std::optional<std::uint32_t> KbStep(bool mouseOnly)
        {
            const auto mods = HeldMods();

            // the input held first, let go without a second one: it alone is the binding
            if (g_pendingFirst && !IsHeld(g_pendingFirst)) {
                const auto first = std::exchange(g_pendingFirst, 0);
                if (!(mouseOnly && CodeDevice(first) == Device::Keyboard)) return first;
            }

            // a press: combo with what is held, or it waits (a wheel tick can't be held)
            const auto press = [&](std::uint32_t code) -> std::optional<std::uint32_t> {
                if ((g_pendingFirst && g_pendingFirst != code) || mods || IsWheel(code)) {
                    if (mouseOnly && CodeDevice(code) == Device::Keyboard) {  // a key only as the held part
                        if (!g_pendingFirst) g_pendingFirst = code;
                        return std::nullopt;
                    }
                    return KbCompose(code, mods);
                }
                g_pendingFirst = code;
                return std::nullopt;
            };

            if (const auto button = NewMousePress())
                if (const auto r = press(MakeCode(Device::Mouse, *button))) return r;

            for (const auto& k : kKeys) {
                if (k.dik == 1) continue;  // Esc closes the menu, it can't be a target here
                if (ModBitForDik(k.dik)) {
                    if (ImGui::IsKeyPressed(k.key, false) && !g_pendingModifier) g_pendingModifier = k.dik;
                    // a modifier let go without another key: it is the key, with the other
                    // modifiers still held (Shift+Ctrl) and the input held first (G+Shift)
                    if (ImGui::IsKeyReleased(k.key) && g_pendingModifier == k.dik) {
                        g_pendingModifier = 0;
                        if (!mouseOnly) return KbCompose(k.dik, mods);
                    }
                    continue;
                }
                if (ImGui::IsKeyPressed(k.key, false))
                    if (const auto r = press(k.dik)) return r;
            }
            return std::nullopt;
        }
    }

    // While a rebind is pending, the next input becomes the new binding. What is accepted
    // depends on where the binding comes from (its original code): a key takes a key or a
    // mouse target; a mouse action a mouse target; a gamepad action a gamepad button or a
    // stick; a stick action the other stick. Pressed and let go alone, an input binds by
    // itself; held while another is pressed, the two make a combo (keyboard and mouse
    // together, the gamepad on its own). A gamepad button pressed for a key or mouse action
    // is added to it.
    void HandleCapture()
    {
        SyncMenuHotkey();
        if (!g_capture) {
            ClearPending();
            return;
        }
        const auto  home  = CodeDevice(g_capture->defaultKey);
        const bool  stick = IsStick(g_capture->defaultKey);
        const char* ask   = stick                    ? N_("Push the stick for: {0} ({1})")
                            : home == Device::Mouse   ? N_("Press the new mouse or gamepad button for: {0} ({1})")
                            : home == Device::Gamepad ? N_("Press the new gamepad button for: {0} ({1})")
                                                      : N_("Press the new keyboard, mouse or gamepad button for: {0} ({1})");
        ImGui::TextUnformatted(TLF(ask, { g_capture->action, g_capture->owner }).c_str());
        ImGui::SameLine();
        if (ImGui::Button(Id(TL("Cancel"), "cancelcapture").c_str())) {
            EndCapture();
            return;
        }

        // gamepad: the binding itself, or a button added to a key or mouse action
        if (const auto pad = PadStep(stick)) {
            FinishCapture(*pad);
            return;
        }
        const auto padHint = [] {
            if (g_pendingPad)
                Muted(TLF("{0} held: press another button for a combo, or let go to bind it alone.", { ButtonLabel(Device::Gamepad, g_pendingPad) }).c_str());
            else
                Muted(TL("Press and let go for a single button, hold a button and press another for a combo."));
        };

        if (home == Device::Gamepad) {
            if (!stick) padHint();
            return;
        }

        if (const auto code = KbStep(home == Device::Mouse)) {
            FinishCapture(*code);
            return;
        }
        if (g_pendingFirst)
            Muted(TLF("{0} held: press another button for a combo, or let go to bind it alone.", { CodeLabel(g_pendingFirst) }).c_str());
        else if (const auto mods = HeldMods())
            Muted((ModsText(mods) + "...").c_str());
        else
            Muted(TL("Press and let go for a single button, hold a button and press another for a combo."));
        if (g_pendingPad) padHint();
    }
}
