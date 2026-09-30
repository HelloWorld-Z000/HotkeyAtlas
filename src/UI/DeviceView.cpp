// The keyboard, mouse and gamepad drawn with their binds.

#include "UI/UI.h"

namespace HA::UI
{
    namespace
    {
        // Draws `text` centred horizontally in [x0, x1] at `top`, shrunk until it fits.
        void FitText(ImGui::ImDrawList* dl, const char* text, float x0, float x1, float top, float maxSize, ImGui::ImU32 col)
        {
            const float base = ImGui::GetFontSize();
            const float w    = ImGui::CalcTextSize(text).x;  // width at `base`
            float       size = maxSize;
            if (w > 0.0f) size = (std::min)(size, base * (x1 - x0) / w);
            const float tw = w * size / base;
            ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), size, ImGui::ImVec2(x0 + (x1 - x0 - tw) * 0.5f, top), col, text);
        }
    }

    // Keyboard keys as ButtonDefs, so every device is drawn by DrawDevice.
    std::span<const ButtonDef> KeyboardDefs()
    {
        static const auto defs = [] {
            std::vector<ButtonDef> v;
            for (const auto& k : kKeys) v.push_back({ k.label, k.dik, k.x, k.y, k.w, k.h });
            return v;
        }();
        return defs;
    }

    // Draws a device's buttons `unitsW` x `unitsH` key units big, scaled to the window
    // (at most `maxUnit` pixels per unit). Clicking a button selects it.
    void DrawDevice(const Model& model, Device device, std::span<const ButtonDef> defs, float unitsW, float unitsH, float maxUnit)
    {
        const float unit   = std::clamp(ImGui::GetContentRegionAvail().x / unitsW, 26.0f, maxUnit);
        const float gap    = (std::max)(2.0f, unit * 0.07f);
        const float pad    = (std::max)(2.0f, unit * 0.08f);
        const float labelH = (std::min)(ImGui::GetFontSize(), unit * 0.46f);
        const float countH = (std::min)(ImGui::GetFontSize() * 0.85f, unit * 0.34f);
        const float round  = unit * 0.1f;

        const auto origin = ImGui::GetCursorPos();
        const auto screen = ImGui::GetCursorScreenPos();
        auto*      dl     = ImGui::GetWindowDrawList();

        for (const auto& k : defs) {
            const auto    info = Info(model, device, k.id);
            ImGui::ImVec4 col  = info.count == 0 ? ImGui::ImVec4(0.17f, 0.17f, 0.19f, 1.0f)
                                 : info.overlap  ? ImGui::ImVec4(0.70f, 0.46f, 0.08f, 1.0f)
                                                 : ImGui::ImVec4(0.14f, 0.36f, 0.62f, 1.0f);

            const ImGui::ImVec2 p0(screen.x + k.x * unit, screen.y + k.y * unit);
            const ImGui::ImVec2 p1(p0.x + k.w * unit - gap, p0.y + k.h * unit - gap);

            char id[24];
            std::snprintf(id, sizeof id, "##k%d_%u", static_cast<int>(device), k.id);
            ImGui::SetCursorPos(ImGui::ImVec2(origin.x + k.x * unit, origin.y + k.y * unit));
            ImGui::InvisibleButton(id, ImGui::ImVec2(p1.x - p0.x, p1.y - p0.y));
            const bool selected = g_hasSelected && g_selDevice == device && g_selected == k.id;
            if (ImGui::IsItemClicked()) Select(device, k.id);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) col = ImGui::ImVec4((std::min)(col.x * 1.3f, 1.0f), (std::min)(col.y * 1.3f, 1.0f), (std::min)(col.z * 1.3f, 1.0f), 1.0f);

            ImGui::ImDrawListManager::AddRectFilled(dl, p0, p1, ImGui::GetColorU32(col), round, ImGui::ImDrawFlags_RoundCornersAll);
            if (selected)
                ImGui::ImDrawListManager::AddRect(dl, p0, p1, ImGui::GetColorU32(ImGui::ImVec4(1, 1, 1, 1)), round, ImGui::ImDrawFlags_RoundCornersAll, 2.0f);

            const auto  white = ImGui::GetColorU32(ImGui::ImVec4(1, 1, 1, 1));
            const char* label = device == Device::Keyboard ? k.label : TL(k.label);
            if (info.count) {
                // name on top, binding count in the bottom-right corner
                FitText(dl, label, p0.x + pad, p1.x - pad, p0.y + pad * 0.5f, labelH, white);
                char cnt[16];
                std::snprintf(cnt, sizeof cnt, "%zu", info.count);
                const float cw = ImGui::CalcTextSize(cnt).x * countH / ImGui::GetFontSize();
                ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), countH, ImGui::ImVec2(p1.x - pad - cw, p1.y - pad * 0.5f - countH),
                                                  ImGui::GetColorU32(ImGui::ImVec4(1, 1, 1, 0.75f)), cnt);
            } else {
                FitText(dl, label, p0.x + pad, p1.x - pad, (p0.y + p1.y - labelH) * 0.5f, labelH,
                        ImGui::GetColorU32(ImGui::ImVec4(0.75f, 0.75f, 0.75f, 1.0f)));
            }

            if (info.count && hovered) {
                ImGui::BeginTooltip();
                int shown = 0;
                for (auto i : RowsFor(model, device, k.id)) {
                    if (++shown > 12) {
                        ImGui::TextUnformatted("...");
                        break;
                    }
                    const auto& b = model.all[i];
                    auto line = (b.mods || b.hold || b.trigger != Trigger::Press ? "[" + ComboLabel(b) + "]  " : std::string()) + b.action + "  -  " + b.owner;
                    if (b.kind == Kind::ControlMap) line += " (" + b.context + ')';
                    if (const auto& note = NoteText(b); !note.empty()) line += ":  " + note;
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::EndTooltip();
            }
        }
        ImGui::SetCursorPos(ImGui::ImVec2(origin.x, origin.y + unitsH * unit));
        ImGui::Dummy(ImGui::ImVec2(unitsW * unit, 1.0f));
    }

    // ---- mouse / gamepad pictures

    namespace
    {
        ImGui::ImU32 Rgba(float r, float g, float b, float a = 1.0f) { return ImGui::GetColorU32(ImGui::ImVec4(r, g, b, a)); }

        // Tooltip with everything bound to one key or button.
        void BindingsTooltip(const Model& model, Device device, std::uint32_t id)
        {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(ButtonLabel(device, id).c_str());
            ImGui::Separator();
            const auto rows = RowsFor(model, device, id);
            if (rows.empty()) Muted(TL("This button has no binds."));
            int shown = 0;
            for (auto i : rows) {
                if (++shown > 12) {
                    ImGui::TextUnformatted("...");
                    break;
                }
                const auto& b    = model.all[i];
                const auto  keys = KeyText(b, device);
                auto        line = (keys != ButtonLabel(device, id) ? "[" + keys + "]  " : std::string()) + b.action + "  -  " + b.owner;
                if (b.kind == Kind::ControlMap) line += " (" + b.context + ')';
                if (const auto& note = NoteText(b); !note.empty()) line += ":  " + note;
                ImGui::TextUnformatted(line.c_str());
            }
            ImGui::EndTooltip();
        }

        void DrawMouseBody(ImGui::ImDrawList* dl, ImGui::ImVec2 o, float u, ImGui::ImU32 col, float th)
        {
            const auto P = [&](float x, float y) { return ImGui::ImVec2(o.x + x * u, o.y + y * u); };
            ImGui::ImDrawListManager::AddRect(dl, P(3.0f, 0.2f), P(8.0f, 8.8f), col, 2.0f * u, ImGui::ImDrawFlags_RoundCornersAll, th);
            ImGui::ImDrawListManager::AddLine(dl, P(3.05f, 3.72f), P(7.95f, 3.72f), col, th);  // buttons / palm
        }

        // Xbox-like outline: both halves mirrored, guide button, wells round the sticks,
        // D-pad and face buttons.
        void DrawXboxBody(ImGui::ImDrawList* dl, ImGui::ImVec2 o, float u, ImGui::ImU32 col, float th)
        {
            const auto P = [&](float x, float y) { return ImGui::ImVec2(o.x + x * u, o.y + y * u); };
            for (int side = 0; side < 2; ++side) {
                const auto Q = [&](float x, float y) { return P(side ? kPadW - x : x, y); };
                ImGui::ImDrawListManager::PathClear(dl);
                ImGui::ImDrawListManager::PathLineTo(dl, Q(7.0f, 1.3f));
                ImGui::ImDrawListManager::PathLineTo(dl, Q(3.9f, 1.3f));
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(2.4f, 1.25f), Q(1.3f, 1.65f), Q(0.95f, 2.8f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(0.6f, 4.0f), Q(0.1f, 6.6f), Q(0.35f, 8.1f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(0.55f, 9.2f), Q(2.0f, 9.4f), Q(2.8f, 8.6f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(3.5f, 7.9f), Q(4.0f, 7.05f), Q(5.0f, 7.0f), 0);
                ImGui::ImDrawListManager::PathLineTo(dl, Q(7.0f, 7.0f));
                ImGui::ImDrawListManager::PathStroke(dl, col, 0, th);
            }
            ImGui::ImDrawListManager::AddCircle(dl, P(7.0f, 2.2f), 0.45f * u, col, 32, th);  // guide button
            ImGui::ImDrawListManager::AddCircleFilled(dl, P(7.0f, 2.2f), 0.2f * u, Rgba(0.86f, 0.86f, 0.86f, 0.35f), 24);
            const auto dim = Rgba(0.86f, 0.86f, 0.86f, 0.35f);
            ImGui::ImDrawListManager::AddCircle(dl, P(3.3f, 3.2f), 1.1f * u, dim, 40, th * 0.7f);    // left stick well
            ImGui::ImDrawListManager::AddCircle(dl, P(5.0f, 5.2f), 1.2f * u, dim, 40, th * 0.7f);    // D-pad well
            ImGui::ImDrawListManager::AddCircle(dl, P(10.9f, 3.2f), 1.2f * u, dim, 40, th * 0.7f);   // face buttons well
            ImGui::ImDrawListManager::AddCircle(dl, P(9.0f, 5.2f), 1.1f * u, dim, 40, th * 0.7f);    // right stick well
        }

        // DualSense-like outline: both halves mirrored, touchpad, speaker dots, button wells.
        void DrawPadBody(ImGui::ImDrawList* dl, ImGui::ImVec2 o, float u, ImGui::ImU32 col, float th)
        {
            const auto P = [&](float x, float y) { return ImGui::ImVec2(o.x + x * u, o.y + y * u); };
            for (int side = 0; side < 2; ++side) {
                const auto Q = [&](float x, float y) { return P(side ? kPadW - x : x, y); };
                ImGui::ImDrawListManager::PathClear(dl);
                ImGui::ImDrawListManager::PathLineTo(dl, Q(7.0f, 1.2f));
                ImGui::ImDrawListManager::PathLineTo(dl, Q(3.4f, 1.2f));
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(2.2f, 1.1f), Q(1.2f, 1.5f), Q(0.9f, 2.6f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(0.6f, 3.8f), Q(0.1f, 6.4f), Q(0.3f, 7.9f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(0.5f, 9.0f), Q(1.9f, 9.3f), Q(2.6f, 8.6f), 0);
                ImGui::ImDrawListManager::PathBezierCubicCurveTo(dl, Q(3.3f, 7.9f), Q(3.7f, 6.9f), Q(4.6f, 6.7f), 0);
                ImGui::ImDrawListManager::PathLineTo(dl, Q(7.0f, 6.7f));
                ImGui::ImDrawListManager::PathStroke(dl, col, 0, th);
            }
            ImGui::ImDrawListManager::AddRect(dl, P(4.7f, 1.35f), P(9.3f, 3.5f), col, 0.35f * u, ImGui::ImDrawFlags_RoundCornersAll, th);  // touchpad
            for (int i = 0; i < 6; ++i) ImGui::ImDrawListManager::AddCircleFilled(dl, P(6.25f + i * 0.3f, 3.9f), 0.06f * u, col, 8);
            ImGui::ImDrawListManager::AddLine(dl, P(6.6f, 5.75f), P(7.4f, 5.75f), col, th);  // mic
            const auto dim = Rgba(0.86f, 0.86f, 0.86f, 0.35f);
            ImGui::ImDrawListManager::AddCircle(dl, P(2.9f, 3.3f), 1.25f * u, dim, 40, th * 0.7f);   // D-pad well
            ImGui::ImDrawListManager::AddCircle(dl, P(11.1f, 3.3f), 1.25f * u, dim, 40, th * 0.7f);  // face buttons well
        }

        // PlayStation face symbols drawn as lines: the game font has none of them.
        void DrawPsSymbol(ImGui::ImDrawList* dl, ImGui::ImVec2 c, float r, std::uint32_t id, ImGui::ImU32 col, float th)
        {
            const float s = r * 0.48f;
            switch (id) {
            case kPadA:
                ImGui::ImDrawListManager::AddLine(dl, ImGui::ImVec2(c.x - s, c.y - s), ImGui::ImVec2(c.x + s, c.y + s), col, th);
                ImGui::ImDrawListManager::AddLine(dl, ImGui::ImVec2(c.x - s, c.y + s), ImGui::ImVec2(c.x + s, c.y - s), col, th);
                break;
            case kPadB:
                ImGui::ImDrawListManager::AddCircle(dl, c, s, col, 24, th);
                break;
            case kPadX:
                ImGui::ImDrawListManager::AddRect(dl, ImGui::ImVec2(c.x - s * 0.85f, c.y - s * 0.85f), ImGui::ImVec2(c.x + s * 0.85f, c.y + s * 0.85f), col, 0.0f, 0, th);
                break;
            case kPadY:
                ImGui::ImDrawListManager::AddTriangle(dl, ImGui::ImVec2(c.x, c.y - s), ImGui::ImVec2(c.x - s * 1.05f, c.y + s * 0.75f),
                    ImGui::ImVec2(c.x + s * 1.05f, c.y + s * 0.75f), col, th);
                break;
            default:
                break;
            }
        }

        // Triangle for D-pad and wheel buttons: 0 up, 1 down, 2 left, 3 right.
        int ArrowDir(Device device, std::uint32_t id)
        {
            if (device == Device::Mouse) return id == kMouseWheelDown ? 1 : 0;
            switch (id) {
            case kPadDown: return 1;
            case kPadLeft: return 2;
            case kPadRight: return 3;
            default: return 0;
            }
        }

        void DrawArrow(ImGui::ImDrawList* dl, ImGui::ImVec2 p0, ImGui::ImVec2 p1, int dir, ImGui::ImU32 col)
        {
            const ImGui::ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            const float         h = (std::min)(p1.x - p0.x, p1.y - p0.y) * 0.28f;
            ImGui::ImVec2       a, b, d;
            switch (dir) {
            case 1: a = { c.x, c.y + h }, b = { c.x - h, c.y - h * 0.6f }, d = { c.x + h, c.y - h * 0.6f }; break;
            case 2: a = { c.x - h, c.y }, b = { c.x + h * 0.6f, c.y - h }, d = { c.x + h * 0.6f, c.y + h }; break;
            case 3: a = { c.x + h, c.y }, b = { c.x - h * 0.6f, c.y - h }, d = { c.x - h * 0.6f, c.y + h }; break;
            default: a = { c.x, c.y - h }, b = { c.x - h, c.y + h * 0.6f }, d = { c.x + h, c.y + h * 0.6f }; break;
            }
            ImGui::ImDrawListManager::AddTriangleFilled(dl, a, b, d, col);
        }

        bool Contains(const ButtonDef& d, ImGui::ImVec2 o, float u, ImGui::ImVec2 m)
        {
            const float x = (m.x - o.x) / u, y = (m.y - o.y) / u;
            if (d.shape == Shape::Circle) return (x - d.x) * (x - d.x) + (y - d.y) * (y - d.y) <= d.w * d.w;
            return x >= d.x && x <= d.x + d.w && y >= d.y && y <= d.y + d.h;
        }

        float TextW(const char* text, float size) { return ImGui::CalcTextSize(text).x * size / ImGui::GetFontSize(); }

        // `text` centred in [p0, p1] as large as fits up to `maxSize`; split over two lines at
        // the space nearest the middle when that is larger. Never smaller than `minSize`, so a
        // long translation may run a little past the box but is never left out. Returns the
        // bottom of the text drawn.
        float DrawFitted(ImGui::ImDrawList* dl, const char* text, ImGui::ImVec2 p0, ImGui::ImVec2 p1, float maxSize, float minSize, ImGui::ImU32 col)
        {
            const float bw = p1.x - p0.x, bh = p1.y - p0.y;
            const float w1 = TextW(text, 1.0f);
            float       one = w1 > 0.0f ? (std::min)({ maxSize, bw / w1, bh * 0.9f }) : maxSize;

            std::string_view s(text);
            std::size_t      cut = std::string_view::npos;
            for (auto p = s.find(' '); p != std::string_view::npos; p = s.find(' ', p + 1))
                if (cut == std::string_view::npos || (std::max)(p, s.size() - p) < (std::max)(cut, s.size() - cut)) cut = p;
            if (cut != std::string_view::npos) {
                const std::string a(s.substr(0, cut)), b(s.substr(cut + 1));
                const float       wa = TextW(a.c_str(), 1.0f), wb = TextW(b.c_str(), 1.0f);
                const float       two = (std::min)({ maxSize, bw / (std::max)(wa, wb), bh / 2.1f });
                if (two > one * 1.15f && two >= minSize * 0.9f) {
                    const float top = (p0.y + p1.y) * 0.5f - two * 1.02f;
                    ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), two, ImGui::ImVec2((p0.x + p1.x - wa * two) * 0.5f, top), col, a.c_str());
                    ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), two, ImGui::ImVec2((p0.x + p1.x - wb * two) * 0.5f, top + two * 1.04f), col, b.c_str());
                    return top + two * 2.04f;
                }
            }
            one = (std::max)(one, minSize);
            ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), one, ImGui::ImVec2((p0.x + p1.x - w1 * one) * 0.5f, (p0.y + p1.y - one) * 0.5f), col, text);
            return (p0.y + p1.y + one) * 0.5f;
        }

        // Binding count in a small dark badge centred on `c`.
        void DrawBadge(ImGui::ImDrawList* dl, ImGui::ImVec2 c, std::size_t count, float size, ImGui::ImU32 ring)
        {
            char cnt[16];
            std::snprintf(cnt, sizeof cnt, "%zu", count);
            // centred on the measured text box, snapped to whole pixels so it stays sharp
            const auto  ts = ImGui::CalcTextSize(cnt);
            const float w  = ts.x * size / ImGui::GetFontSize();
            const float h  = ts.y * size / ImGui::GetFontSize();
            const float r  = (std::max)(size * 0.62f, w * 0.5f + size * 0.22f);
            ImGui::ImDrawListManager::AddCircleFilled(dl, c, r, Rgba(0.07f, 0.07f, 0.09f, 0.95f), 24);
            ImGui::ImDrawListManager::AddCircle(dl, c, r, ring, 24, 1.0f);
            ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), size, ImGui::ImVec2(std::round(c.x - w * 0.5f), std::round(c.y - h * 0.5f)), Rgba(1, 1, 1, 0.95f), cnt);
        }

        // Where an outside label goes (its box), for drawing and for hit testing.
        struct OutsideLabel
        {
            ImGui::ImVec2 p0, p1;
            std::string   text;
        };

        std::optional<OutsideLabel> OutsideLabelOf(const ButtonDef& d, const char* text, ImGui::ImVec2 o, float u, float size)
        {
            if (d.at == LabelAt::Inside || d.at == LabelAt::None) return std::nullopt;
            OutsideLabel l;
            l.text = text;
            const float w = TextW(l.text.c_str(), size);
            const float x = o.x + d.lx * u, y = o.y + d.ly * u;
            switch (d.at) {
            case LabelAt::Left: l.p0 = { x - w, y - size * 0.5f }; break;
            case LabelAt::Right: l.p0 = { x, y - size * 0.5f }; break;
            default: l.p0 = { x - w * 0.5f, y }; break;  // Below
            }
            l.p1 = { l.p0.x + w, l.p0.y + size };
            return l;
        }

        // The wheel drawn as a wheel: a few ridges across it.
        void DrawWheelIcon(ImGui::ImDrawList* dl, ImGui::ImVec2 p0, ImGui::ImVec2 p1, ImGui::ImU32 col, float th)
        {
            const float w = p1.x - p0.x, h = p1.y - p0.y;
            for (int i = 1; i <= 4; ++i) {
                const float y = p0.y + h * (0.1f + 0.16f * static_cast<float>(i));
                ImGui::ImDrawListManager::AddLine(dl, ImGui::ImVec2(p0.x + w * 0.28f, y), ImGui::ImVec2(p1.x - w * 0.28f, y), col, th * 0.8f);
            }
        }

        // Xbox face button letter colours: A green, B red, X blue, Y yellow.
        std::optional<ImGui::ImU32> XboxFaceColor(std::uint32_t id)
        {
            switch (id) {
            case kPadA: return Rgba(0.45f, 0.82f, 0.32f);
            case kPadB: return Rgba(0.93f, 0.33f, 0.30f);
            case kPadX: return Rgba(0.36f, 0.62f, 1.0f);
            case kPadY: return Rgba(0.98f, 0.80f, 0.25f);
            default: return std::nullopt;
            }
        }
    }

    // Mouse or gamepad as a picture with its buttons on it. Later defs are on top (stick cap
    // over the stick), so hit testing walks them backwards; a button's outside label counts
    // as the button.
    void DrawPicture(const Model& model, Device device, std::span<const ButtonDef> defs, float unitsW, float unitsH, float maxUnit)
    {
        const float u    = std::clamp(ImGui::GetContentRegionAvail().x / unitsW, 16.0f, maxUnit);
        const auto  o    = ImGui::GetCursorScreenPos();
        auto*       dl   = ImGui::GetWindowDrawList();
        const float th   = (std::max)(1.5f, u * 0.045f);
        const bool  ps   = device == Device::Gamepad && PlayStationLabels();
        const auto  P    = [&](float x, float y) { return ImGui::ImVec2(o.x + x * u, o.y + y * u); };
        const float base = ImGui::GetFontSize();
        const float minS = (std::max)(11.0f, base * 0.62f);                   // smallest text drawn
        const float outS = std::clamp(u * 0.42f, minS, base);                 // outside labels
        const float cntS = std::clamp(u * 0.4f, (std::max)(12.0f, base * 0.62f), base * 0.9f);  // counts

        std::vector<KeyInfo>                     infos;
        std::vector<std::optional<OutsideLabel>> outside;
        infos.reserve(defs.size());
        outside.reserve(defs.size());
        for (const auto& d : defs) {
            infos.push_back(Info(model, device, d.id));
            const char* label = ps && PsLabel(d.id) ? TL(PsLabel(d.id)) : TL(d.label);
            outside.push_back(OutsideLabelOf(d, label, o, u, outS));
        }

        ImGui::InvisibleButton(device == Device::Mouse ? "##mousepic" : "##padpic", ImGui::ImVec2(unitsW * u, unitsH * u));
        int hot = -1;
        if (ImGui::IsItemHovered()) {
            const auto m = ImGui::GetMousePos();
            for (int i = static_cast<int>(defs.size()) - 1; i >= 0 && hot < 0; --i) {
                const auto& l = outside[i];
                if (Contains(defs[i], o, u, m) || (l && m.x >= l->p0.x - 2 && m.x <= l->p1.x + 2 && m.y >= l->p0.y - 2 && m.y <= l->p1.y + 2)) hot = i;
            }
        }
        if (hot >= 0 && ImGui::IsItemClicked()) Select(device, defs[hot].id);

        const auto outline = Rgba(0.86f, 0.86f, 0.86f, 0.9f);
        if (device == Device::Mouse)
            DrawMouseBody(dl, o, u, outline, th);
        else if (ps)
            DrawPadBody(dl, o, u, outline, th);
        else
            DrawXboxBody(dl, o, u, outline, th);

        for (int i = 0; i < static_cast<int>(defs.size()); ++i) {
            const auto&   d    = defs[i];
            const auto&   info = infos[i];
            ImGui::ImVec4 fill = info.count == 0 ? ImGui::ImVec4(0.12f, 0.12f, 0.14f, 0.85f)
                                 : info.overlap  ? ImGui::ImVec4(0.70f, 0.46f, 0.08f, 1.0f)
                                                 : ImGui::ImVec4(0.14f, 0.36f, 0.62f, 1.0f);
            if (i == hot) fill = ImGui::ImVec4((std::min)(fill.x * 1.4f + 0.05f, 1.0f), (std::min)(fill.y * 1.4f + 0.05f, 1.0f), (std::min)(fill.z * 1.4f + 0.05f, 1.0f), 1.0f);
            const bool  sel   = g_hasSelected && g_selDevice == device && g_selected == d.id;
            const auto  edge  = sel ? Rgba(1, 1, 1) : outline;
            const float eth   = sel ? th * 2.0f : th;
            const auto  text  = info.count ? Rgba(1, 1, 1) : Rgba(0.8f, 0.8f, 0.8f);
            const char* label = ps && PsLabel(d.id) ? TL(PsLabel(d.id)) : TL(d.label);
            const bool  stick = d.id == kPadLeftStick || d.id == kPadRightStick;
            const auto  num   = std::to_string(info.count);
            const float cw    = info.count ? TextW(num.c_str(), cntS) : 0.0f;
            const float ch    = info.count ? ImGui::CalcTextSize(num.c_str()).y * cntS / base : 0.0f;

            ImGui::ImVec2 p0, p1;         // inside label box
            ImGui::ImVec2 badge, anchor;  // count badge centre; where the outside label's line ends
            ImGui::ImVec2 cntAt(0, 0);    // a box's count: top-left of its text
            bool          armBadge = false;  // a D-pad arm's count, in a badge like a round button's
            bool          inside = d.at == LabelAt::Inside;
            if (d.shape == Shape::Circle) {
                const auto  c = P(d.x, d.y);
                const float r = d.w * u;
                ImGui::ImDrawListManager::AddCircleFilled(dl, c, r, ImGui::GetColorU32(fill), 40);
                ImGui::ImDrawListManager::AddCircle(dl, c, r, edge, 40, eth);
                // Xbox face buttons: their colour as an inner ring
                if (!ps && device == Device::Gamepad)
                    if (const auto fc = XboxFaceColor(d.id)) ImGui::ImDrawListManager::AddCircle(dl, c, r - eth * 1.5f, *fc, 40, th * 1.3f);
                p0 = { c.x - r * 0.72f, c.y - r * 0.72f };
                p1 = { c.x + r * 0.72f, c.y + r * 0.72f };
                if (ps && PsLabel(d.id) && d.id >= kPadA) {
                    DrawPsSymbol(dl, c, r, d.id, text, th);
                    inside = false;
                }
                if (stick) inside = false;  // the cap on top carries the text
                // badge on the rim: a stick's at the bottom, its cap's at the top; face buttons
                // outward from their group (Y top, X left, B right, A bottom); others top-right
                ImGui::ImVec2 dirv(0.707f, -0.707f);
                if (stick) dirv = { 0.0f, 1.0f };
                else if (device == Device::Gamepad && (d.id == kPadL3 || d.id == kPadR3)) dirv = { 0.0f, -1.0f };
                else if (device == Device::Gamepad && d.id == kPadY) dirv = { 0.0f, -1.0f };
                else if (device == Device::Gamepad && d.id == kPadA) dirv = { 0.0f, 1.0f };
                else if (device == Device::Gamepad && d.id == kPadX) dirv = { -1.0f, 0.0f };
                else if (device == Device::Gamepad && d.id == kPadB) dirv = { 1.0f, 0.0f };
                badge  = { c.x + r * dirv.x, c.y + r * dirv.y };
                anchor = { c.x, c.y + r };
            } else {
                p0              = P(d.x, d.y);
                p1              = P(d.x + d.w, d.y + d.h);
                const int flags = d.corners ? d.corners : ImGui::ImDrawFlags_RoundCornersAll;
                ImGui::ImDrawListManager::AddRectFilled(dl, p0, p1, ImGui::GetColorU32(fill), d.round * u, flags);
                ImGui::ImDrawListManager::AddRect(dl, p0, p1, edge, d.round * u, flags, eth);
                // the count (top-left of its text): a box's bottom-right corner, clear of the rounding
                const float inset = (std::min)(d.round * u, (p1.y - p0.y) * 0.5f) * 0.3f + 2.0f;
                cntAt             = { p1.x - inset - cntS * 0.35f - cw, p1.y - inset - cntS };  // a little in from the edge
                // a flat bar (triggers, bumpers): centred in its height, between the name and
                // the outer end (left for LT / LB, right for RT / RB)
                if (p1.y - p0.y < 0.8f * u && d.shape == Shape::Rect) {
                    const bool  left = device == Device::Gamepad && (d.id == kPadLT || d.id == kPadLB);
                    const float cx   = left ? p0.x + (p1.x - p0.x) * 0.17f : p1.x - (p1.x - p0.x) * 0.17f;
                    cntAt            = { std::round(cx - cw * 0.5f), std::round((p0.y + p1.y - ch) * 0.5f) };
                }
                badge  = { p1.x - 1.0f, p0.y + 1.0f };
                anchor = { d.at == LabelAt::Left ? p0.x : p1.x, (p0.y + p1.y) * 0.5f };
                if (d.shape == Shape::Arrow) {
                    const int dir = ArrowDir(device, d.id);
                    auto      a0 = p0, a1 = p1;  // the triangle's box
                    if (info.count && device == Device::Mouse) {
                        // wheel: arrow and count stacked, both centred; up has the count
                        // below the arrow, down above it, mirrored round the wheel
                        const float bh  = p1.y - p0.y;
                        const float mid = p0.y + bh * (dir == 0 ? 0.56f : 0.44f);
                        (dir == 0 ? a1.y : a0.y) = mid;
                        // the number a little toward the box's middle, away from its frame
                        const float cy = p0.y + bh * (dir == 0 ? 0.74f : 0.26f);
                        cntAt          = { std::round((p0.x + p1.x - cw) * 0.5f), std::round(cy - ch * 0.5f) };
                    } else if (info.count) {
                        // D-pad: a badge just past the arm's outer end, centred on it
                        const float         off = cntS * 0.35f;
                        const ImGui::ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
                        badge    = dir == 0 ? ImGui::ImVec2(c.x, p0.y - off) : dir == 1 ? ImGui::ImVec2(c.x, p1.y + off)
                                 : dir == 2 ? ImGui::ImVec2(p0.x - off, c.y) : ImGui::ImVec2(p1.x + off, c.y);
                        armBadge = true;
                    }
                    DrawArrow(dl, a0, a1, dir, text);
                }
                if (d.id == kMouseMiddle && device == Device::Mouse) DrawWheelIcon(dl, p0, p1, text, th);
            }

            // a tall box (LMB, RMB, mouse move): name and count stacked, centred together;
            // the name keeps clear of the sides and the rounding
            const bool stacked = inside && d.shape == Shape::Rect && p1.y - p0.y >= 0.8f * u;
            if (inside) {
                const float padX = d.shape == Shape::Rect ? (std::max)(0.1f * u, (std::min)(d.round, 0.5f) * u * 0.5f) : 0.0f;
                const float gap  = cntS * 0.15f;
                auto        q1   = ImGui::ImVec2(p1.x - padX, p1.y);
                if (stacked && info.count) q1.y -= ch + gap;
                const float bottom = DrawFitted(dl, label, { p0.x + padX, p0.y }, q1, base, minS,
                                                !ps && device == Device::Gamepad && XboxFaceColor(d.id) ? Rgba(1, 1, 1) : text);
                if (stacked) cntAt = { std::round((p0.x + p1.x - cw) * 0.5f), std::round(bottom + gap) };
            }

            if (const auto& l = outside[i]) {
                // name outside the body next to a thin line to the button; the count is inside the button
                const ImGui::ImVec2 from(d.at == LabelAt::Left ? l->p1.x + 3.0f : d.at == LabelAt::Right ? l->p0.x - 3.0f : (l->p0.x + l->p1.x) * 0.5f,
                                         d.at == LabelAt::Below ? l->p0.y - 1.0f : (l->p0.y + l->p1.y) * 0.5f);
                if (d.at != LabelAt::Below) ImGui::ImDrawListManager::AddLine(dl, from, anchor, Rgba(0.86f, 0.86f, 0.86f, sel || i == hot ? 0.9f : 0.4f), 1.0f);
                const auto ncol = i == hot || sel ? Rgba(1, 1, 1) : Rgba(0.88f, 0.88f, 0.88f);
                // Create / Options: the text a touch higher, so their line meets its middle
                const float lift = device == Device::Gamepad && d.at != LabelAt::Below ? outS * 0.08f : 0.0f;
                ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), outS, ImGui::ImVec2(std::round(l->p0.x), std::round(l->p0.y - lift)), ncol, label);
            }

            // the wheel click shows no count, it has no room beside the ridges
            if (info.count && !(device == Device::Mouse && d.id == kMouseMiddle)) {
                if (outside[i])  // small button, name outside: the count inside it
                    DrawFitted(dl, num.c_str(), p0, p1, cntS, 9.0f, Rgba(1, 1, 1));
                else if (d.shape == Shape::Circle || armBadge)
                    DrawBadge(dl, badge, info.count, cntS, info.overlap ? Rgba(0.95f, 0.68f, 0.2f) : Rgba(0.55f, 0.75f, 1.0f));
                else
                    ImGui::ImDrawListManager::AddText(dl, ImGui::GetFont(), cntS, cntAt, Rgba(1, 1, 1, 0.85f), num.c_str());
            }
        }

        if (hot >= 0) BindingsTooltip(model, device, defs[hot].id);
    }
}
