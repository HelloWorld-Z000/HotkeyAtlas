#pragma once

// Where the keys and buttons sit on the keyboard, mouse and gamepad pictures.

#include "HotkeyAtlas.h"
#include "SKSEMenuFramework.h"

namespace ImGui = ImGuiMCP;

namespace HA::UI
{
    struct KeyDef
    {
        const char*     label;
        std::uint32_t   dik;  // DirectInput scancode
        ImGui::ImGuiKey key;  // same key in ImGui, used to capture a new binding
        float           x, y, w, h;
    };

#define K(label, dik, imkey, x, y, w, h) { label, dik, ImGui::ImGuiKey_##imkey, x, y, w, h }
    // positions are in "key units"
    inline const KeyDef kKeys[] = {
        K("Esc", 1, Escape, 0, 0, 1, 1),
        K("F1", 59, F1, 2, 0, 1, 1), K("F2", 60, F2, 3, 0, 1, 1), K("F3", 61, F3, 4, 0, 1, 1), K("F4", 62, F4, 5, 0, 1, 1),
        K("F5", 63, F5, 6.5f, 0, 1, 1), K("F6", 64, F6, 7.5f, 0, 1, 1), K("F7", 65, F7, 8.5f, 0, 1, 1), K("F8", 66, F8, 9.5f, 0, 1, 1),
        K("F9", 67, F9, 11, 0, 1, 1), K("F10", 68, F10, 12, 0, 1, 1), K("F11", 87, F11, 13, 0, 1, 1), K("F12", 88, F12, 14, 0, 1, 1),
        K("PrtSc", 183, PrintScreen, 15.25f, 0, 1, 1), K("ScrLk", 70, ScrollLock, 16.25f, 0, 1, 1), K("Pause", 197, Pause, 17.25f, 0, 1, 1),

        K("`", 41, GraveAccent, 0, 1.25f, 1, 1),
        K("1", 2, 1, 1, 1.25f, 1, 1), K("2", 3, 2, 2, 1.25f, 1, 1), K("3", 4, 3, 3, 1.25f, 1, 1), K("4", 5, 4, 4, 1.25f, 1, 1),
        K("5", 6, 5, 5, 1.25f, 1, 1), K("6", 7, 6, 6, 1.25f, 1, 1), K("7", 8, 7, 7, 1.25f, 1, 1), K("8", 9, 8, 8, 1.25f, 1, 1),
        K("9", 10, 9, 9, 1.25f, 1, 1), K("0", 11, 0, 10, 1.25f, 1, 1), K("-", 12, Minus, 11, 1.25f, 1, 1), K("=", 13, Equal, 12, 1.25f, 1, 1),
        K("Bksp", 14, Backspace, 13, 1.25f, 2, 1),
        K("Ins", 210, Insert, 15.25f, 1.25f, 1, 1), K("Home", 199, Home, 16.25f, 1.25f, 1, 1), K("PgUp", 201, PageUp, 17.25f, 1.25f, 1, 1),

        K("Tab", 15, Tab, 0, 2.25f, 1.5f, 1),
        K("Q", 16, Q, 1.5f, 2.25f, 1, 1), K("W", 17, W, 2.5f, 2.25f, 1, 1), K("E", 18, E, 3.5f, 2.25f, 1, 1), K("R", 19, R, 4.5f, 2.25f, 1, 1),
        K("T", 20, T, 5.5f, 2.25f, 1, 1), K("Y", 21, Y, 6.5f, 2.25f, 1, 1), K("U", 22, U, 7.5f, 2.25f, 1, 1), K("I", 23, I, 8.5f, 2.25f, 1, 1),
        K("O", 24, O, 9.5f, 2.25f, 1, 1), K("P", 25, P, 10.5f, 2.25f, 1, 1), K("[", 26, LeftBracket, 11.5f, 2.25f, 1, 1),
        K("]", 27, RightBracket, 12.5f, 2.25f, 1, 1), K("\\", 43, Backslash, 13.5f, 2.25f, 1.5f, 1),
        K("Del", 211, Delete, 15.25f, 2.25f, 1, 1), K("End", 207, End, 16.25f, 2.25f, 1, 1), K("PgDn", 209, PageDown, 17.25f, 2.25f, 1, 1),

        K("Caps", 58, CapsLock, 0, 3.25f, 1.75f, 1),
        K("A", 30, A, 1.75f, 3.25f, 1, 1), K("S", 31, S, 2.75f, 3.25f, 1, 1), K("D", 32, D, 3.75f, 3.25f, 1, 1), K("F", 33, F, 4.75f, 3.25f, 1, 1),
        K("G", 34, G, 5.75f, 3.25f, 1, 1), K("H", 35, H, 6.75f, 3.25f, 1, 1), K("J", 36, J, 7.75f, 3.25f, 1, 1), K("K", 37, K, 8.75f, 3.25f, 1, 1),
        K("L", 38, L, 9.75f, 3.25f, 1, 1), K(";", 39, Semicolon, 10.75f, 3.25f, 1, 1), K("'", 40, Apostrophe, 11.75f, 3.25f, 1, 1),
        K("Enter", 28, Enter, 12.75f, 3.25f, 2.25f, 1),

        K("LShift", 42, LeftShift, 0, 4.25f, 2.25f, 1),
        K("Z", 44, Z, 2.25f, 4.25f, 1, 1), K("X", 45, X, 3.25f, 4.25f, 1, 1), K("C", 46, C, 4.25f, 4.25f, 1, 1), K("V", 47, V, 5.25f, 4.25f, 1, 1),
        K("B", 48, B, 6.25f, 4.25f, 1, 1), K("N", 49, N, 7.25f, 4.25f, 1, 1), K("M", 50, M, 8.25f, 4.25f, 1, 1),
        K(",", 51, Comma, 9.25f, 4.25f, 1, 1), K(".", 52, Period, 10.25f, 4.25f, 1, 1), K("/", 53, Slash, 11.25f, 4.25f, 1, 1),
        K("RShift", 54, RightShift, 12.25f, 4.25f, 2.75f, 1), K("Up", 200, UpArrow, 16.25f, 4.25f, 1, 1),

        K("LCtrl", 29, LeftCtrl, 0, 5.25f, 1.25f, 1), K("LWin", 219, LeftSuper, 1.25f, 5.25f, 1.25f, 1), K("LAlt", 56, LeftAlt, 2.5f, 5.25f, 1.25f, 1),
        K("Space", 57, Space, 3.75f, 5.25f, 6.25f, 1), K("RAlt", 184, RightAlt, 10, 5.25f, 1.25f, 1), K("RWin", 220, RightSuper, 11.25f, 5.25f, 1.25f, 1),
        K("Menu", 221, Menu, 12.5f, 5.25f, 1.25f, 1), K("RCtrl", 157, RightCtrl, 13.75f, 5.25f, 1.25f, 1),
        K("Left", 203, LeftArrow, 15.25f, 5.25f, 1, 1), K("Down", 208, DownArrow, 16.25f, 5.25f, 1, 1), K("Right", 205, RightArrow, 17.25f, 5.25f, 1, 1),

        // numpad
        K("Num", 69, NumLock, 19, 1.25f, 1, 1), K("N/", 181, KeypadDivide, 20, 1.25f, 1, 1), K("N*", 55, KeypadMultiply, 21, 1.25f, 1, 1), K("N-", 74, KeypadSubtract, 22, 1.25f, 1, 1),
        K("N7", 71, Keypad7, 19, 2.25f, 1, 1), K("N8", 72, Keypad8, 20, 2.25f, 1, 1), K("N9", 73, Keypad9, 21, 2.25f, 1, 1), K("N+", 78, KeypadAdd, 22, 2.25f, 1, 2),
        K("N4", 75, Keypad4, 19, 3.25f, 1, 1), K("N5", 76, Keypad5, 20, 3.25f, 1, 1), K("N6", 77, Keypad6, 21, 3.25f, 1, 1),
        K("N1", 79, Keypad1, 19, 4.25f, 1, 1), K("N2", 80, Keypad2, 20, 4.25f, 1, 1), K("N3", 81, Keypad3, 21, 4.25f, 1, 1), K("NEnt", 156, KeypadEnter, 22, 4.25f, 1, 2),
        K("N0", 82, Keypad0, 19, 5.25f, 2, 1), K("N.", 83, KeypadDecimal, 21, 5.25f, 1, 1),
    };
#undef K

    // Mouse and gamepad are drawn as pictures with the buttons on them; positions in units
    // of the picture (the keyboard uses the same struct with key units, all rectangles).
    enum class Shape : std::uint8_t
    {
        Rect,    // x, y, w, h
        Circle,  // centre x, y; radius w
        Arrow    // rect with a triangle pointing the D-pad direction
    };

    // Where a picture button's name goes: inside it, or (a button too small to hold it)
    // outside the body at (lx, ly), with a thin line to the button.
    enum class LabelAt : std::uint8_t
    {
        Inside,
        Left,   // right-aligned, ending at lx, centred on ly
        Right,  // left-aligned, starting at lx, centred on ly
        Below,  // centred on lx, top at ly
        None    // an icon instead (wheel arrows, MMB, PlayStation symbols)
    };

    struct ButtonDef
    {
        const char*   label;  // N_() text, shown through TL()
        std::uint32_t id;     // MouseButton / GamepadButton / keyboard DIK
        float         x, y, w, h;
        Shape         shape   = Shape::Rect;
        float         round   = 0.1f;  // corner rounding, in units
        int           corners = 0;     // ImDrawFlags corner set, 0 = all
        LabelAt       at      = LabelAt::Inside;
        float         lx = 0, ly = 0;  // outside label anchor, in units
    };

    inline constexpr int kTopLeft  = ImGui::ImDrawFlags_RoundCornersTopLeft;
    inline constexpr int kTopRight = ImGui::ImDrawFlags_RoundCornersTopRight;
    inline constexpr int kTop      = ImGui::ImDrawFlags_RoundCornersTop;

    // 11 x 9 units: the body (x 3..8) in the middle, room on both sides for the side
    // buttons' names; two main buttons, wheel, side buttons, palm
    inline constexpr float kMouseW = 11.0f, kMouseH = 9.0f;
    inline constexpr ButtonDef kMouseButtons[] = {
        { N_("LMB"), kMouseLeft, 3.12f, 0.32f, 1.98f, 3.28f, Shape::Rect, 1.9f, kTopLeft },
        { N_("RMB"), kMouseRight, 5.9f, 0.32f, 1.98f, 3.28f, Shape::Rect, 1.9f, kTopRight },
        { N_("Wheel Up"), kMouseWheelUp, 5.16f, 0.4f, 0.68f, 0.95f, Shape::Arrow, 0.14f, 0, LabelAt::None },
        { N_("MMB"), kMouseMiddle, 5.22f, 1.43f, 0.56f, 0.74f, Shape::Rect, 0.24f, 0, LabelAt::None },
        { N_("Wheel Down"), kMouseWheelDown, 5.16f, 2.25f, 0.68f, 0.95f, Shape::Arrow, 0.14f, 0, LabelAt::None },
        { N_("Mouse 4"), kMouse4, 3.1f, 4.0f, 0.5f, 0.9f, Shape::Rect, 0.18f, 0, LabelAt::Left, 2.75f, 4.45f },
        { N_("Mouse 5"), kMouse5, 3.1f, 5.05f, 0.5f, 0.9f, Shape::Rect, 0.18f, 0, LabelAt::Left, 2.75f, 5.5f },
        { N_("Mouse 8"), kMouse8, 3.1f, 6.1f, 0.5f, 0.9f, Shape::Rect, 0.18f, 0, LabelAt::Left, 2.75f, 6.55f },
        { N_("Mouse 6"), kMouse6, 7.4f, 4.0f, 0.5f, 0.9f, Shape::Rect, 0.18f, 0, LabelAt::Right, 8.25f, 4.45f },
        { N_("Mouse 7"), kMouse7, 7.4f, 5.05f, 0.5f, 0.9f, Shape::Rect, 0.18f, 0, LabelAt::Right, 8.25f, 5.5f },
        { N_("Mouse move"), kMouseMove, 4.0f, 5.3f, 3.0f, 1.7f, Shape::Rect, 0.5f },
    };

    // 14 x 9.4 units, Xbox-like: left stick high and D-pad low on the left, face buttons
    // high and right stick low on the right, View / Menu around the guide button
    inline constexpr float kPadW = 14.0f, kPadH = 9.4f;
    inline constexpr ButtonDef kPadButtons[] = {
        { N_("LT"), kPadLT, 1.8f, 0.0f, 2.3f, 0.55f, Shape::Rect, 0.25f, kTop },
        { N_("LB"), kPadLB, 1.5f, 0.62f, 2.8f, 0.48f, Shape::Rect, 0.22f },
        { N_("RT"), kPadRT, 9.9f, 0.0f, 2.3f, 0.55f, Shape::Rect, 0.25f, kTop },
        { N_("RB"), kPadRB, 9.7f, 0.62f, 2.8f, 0.48f, Shape::Rect, 0.22f },
        { N_("Back"), kPadBack, 6.05f, 3.2f, 0.27f, 0, Shape::Circle, 0, 0, LabelAt::Below, 6.05f, 3.58f },
        { N_("Start"), kPadStart, 7.95f, 3.2f, 0.27f, 0, Shape::Circle, 0, 0, LabelAt::Below, 7.95f, 3.58f },
        { N_("D-pad Up"), kPadUp, 4.7f, 4.22f, 0.6f, 0.64f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Down"), kPadDown, 4.7f, 5.54f, 0.6f, 0.64f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Left"), kPadLeft, 4.0f, 4.9f, 0.64f, 0.6f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Right"), kPadRight, 5.36f, 4.9f, 0.64f, 0.6f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("Y"), kPadY, 10.9f, 2.45f, 0.37f, 0, Shape::Circle },
        { N_("X"), kPadX, 10.15f, 3.2f, 0.37f, 0, Shape::Circle },
        { N_("B"), kPadB, 11.65f, 3.2f, 0.37f, 0, Shape::Circle },
        { N_("A"), kPadA, 10.9f, 3.95f, 0.37f, 0, Shape::Circle },
        { N_("Left stick"), kPadLeftStick, 3.3f, 3.2f, 0.85f, 0, Shape::Circle },
        { N_("L3"), kPadL3, 3.3f, 3.2f, 0.45f, 0, Shape::Circle },  // drawn over the stick: the cap
        { N_("Right stick"), kPadRightStick, 9.0f, 5.2f, 0.85f, 0, Shape::Circle },
        { N_("R3"), kPadR3, 9.0f, 5.2f, 0.45f, 0, Shape::Circle },
    };

    // 14 x 9.4 units, DualSense-like: sticks side by side at the bottom, touchpad in the
    // middle with Create / Options next to it
    inline constexpr ButtonDef kPadButtonsPs[] = {
        { N_("LT"), kPadLT, 1.7f, 0.0f, 2.3f, 0.55f, Shape::Rect, 0.25f, kTop },
        { N_("LB"), kPadLB, 1.5f, 0.62f, 2.7f, 0.48f, Shape::Rect, 0.22f },
        { N_("RT"), kPadRT, 10.0f, 0.0f, 2.3f, 0.55f, Shape::Rect, 0.25f, kTop },
        { N_("RB"), kPadRB, 9.8f, 0.62f, 2.7f, 0.48f, Shape::Rect, 0.22f },
        { N_("Back"), kPadBack, 4.14f, 1.45f, 0.38f, 0.8f, Shape::Rect, 0.19f, 0, LabelAt::Left, 3.9f, 1.75f },
        { N_("Start"), kPadStart, 9.48f, 1.45f, 0.38f, 0.8f, Shape::Rect, 0.19f, 0, LabelAt::Right, 10.1f, 1.75f },
        { N_("D-pad Up"), kPadUp, 2.6f, 2.32f, 0.6f, 0.64f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Down"), kPadDown, 2.6f, 3.64f, 0.6f, 0.64f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Left"), kPadLeft, 1.9f, 3.0f, 0.64f, 0.6f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("D-pad Right"), kPadRight, 3.26f, 3.0f, 0.64f, 0.6f, Shape::Arrow, 0.12f, 0, LabelAt::None },
        { N_("Y"), kPadY, 11.1f, 2.55f, 0.37f, 0, Shape::Circle },
        { N_("X"), kPadX, 10.35f, 3.3f, 0.37f, 0, Shape::Circle },
        { N_("B"), kPadB, 11.85f, 3.3f, 0.37f, 0, Shape::Circle },
        { N_("A"), kPadA, 11.1f, 4.05f, 0.37f, 0, Shape::Circle },
        { N_("Left stick"), kPadLeftStick, 5.0f, 5.15f, 0.9f, 0, Shape::Circle },
        { N_("L3"), kPadL3, 5.0f, 5.15f, 0.47f, 0, Shape::Circle },
        { N_("Right stick"), kPadRightStick, 9.0f, 5.15f, 0.9f, 0, Shape::Circle },
        { N_("R3"), kPadR3, 9.0f, 5.15f, 0.47f, 0, Shape::Circle },
    };

    // PlayStation names for the gamepad; nullptr = same as Xbox
    inline const char* PsLabel(std::uint32_t id)
    {
        switch (id) {
        case kPadLB: return N_("L1");
        case kPadLT: return N_("L2");
        case kPadRB: return N_("R1");
        case kPadRT: return N_("R2");
        case kPadBack: return N_("Create");
        case kPadStart: return N_("Options");
        case kPadA: return N_("Cross");
        case kPadB: return N_("Circle");
        case kPadX: return N_("Square");
        case kPadY: return N_("Triangle");
        default: return nullptr;
        }
    }
}
