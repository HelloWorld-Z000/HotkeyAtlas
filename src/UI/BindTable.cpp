// The bind table: sorting, notes, edit buttons, one or two lines per bind.

#include "UI/UI.h"

namespace HA::UI
{
    namespace
    {
        // Edit column order, what the user changed first: a gamepad button added (or a gamepad
        // action rebound), then keys and mouse buttons rebound, then unbound; after them the
        // untouched bindings, read-only last.
        int EditRank(const Binding& b)
        {
            if (b.padKey != kUnbound || (b.overridden && b.device == Device::Gamepad && b.key != kUnbound)) return 0;
            if (b.overridden && b.key != kUnbound) return 1;
            if (b.key == kUnbound) return 2;
            return b.editable ? 3 : 4;
        }

        int CompareBindings(const Binding& a, const Binding& b, ImGui::ImGuiID col)
        {
            switch (col) {
            case kColKey:
                // unbound keys after every bound one: last going up, first going down
                return Compare3(std::tuple(a.key == kUnbound, a.device, a.device == Device::Keyboard ? Combo(a.key, a.mods) : a.key, a.hold),
                                std::tuple(b.key == kUnbound, b.device, b.device == Device::Keyboard ? Combo(b.key, b.mods) : b.key, b.hold));
            case kColAction:
                return CompareText(a.action, b.action);
            case kColNote:
                return CompareText(NoteText(a), NoteText(b));
            case kColMod:
                return CompareText(a.owner, b.owner);
            case kColContext:
                return CompareText(a.context, b.context);
            case kColSource:
                return CompareText(a.origin, b.origin);
            case kColEdit:
                return Compare3(EditRank(a), EditRank(b));
            default:
                return 0;
            }
        }

        // Note column: click to edit in place, Enter or clicking elsewhere saves, an empty
        // text brings back the built-in description.
        void NoteCell(const Binding& b)
        {
            const auto id = NoteId(b);
            if (g_noteEdit == id) {
                if (g_noteFocus) {
                    ImGui::SetKeyboardFocusHere();
                    g_noteFocus = false;
                }
                ImGui::SetNextItemWidth(-1.0f);
                const bool enter = ImGui::InputText("##note", g_noteBuf, sizeof g_noteBuf,
                    ImGui::ImGuiInputTextFlags_EnterReturnsTrue | ImGui::ImGuiInputTextFlags_AutoSelectAll);
                if (enter || ImGui::IsItemDeactivated()) {
                    SetNote(id, g_noteBuf);
                    g_noteEdit.clear();
                }
                return;
            }

            const auto* user = UserNote(b);
            const auto& text = user ? *user : b.description;
            if (!user) ImGui::PushStyleColor(ImGui::ImGuiCol_Text, ImGui::ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            const bool clicked = ImGui::Selectable(text.empty() ? TL("(click to add a note)") : text.c_str());
            if (!user) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                std::string tip = text;
                if (user && !b.description.empty()) tip += "\n\n" + TLF("Built-in: {0}", { b.description });
                if (!b.contextHint.empty()) tip += "\n\n" + b.context + ": " + b.contextHint;
                tip += std::string("\n\n") + (user ? TL("Click to edit. Clear the text to remove your note.") : TL("Click to write your own note."));
                Tooltip(tip);
            }
            if (clicked) {
                if (!g_noteEdit.empty()) SetNote(g_noteEdit, g_noteBuf);  // save the one left open elsewhere
                g_noteEdit = id;
                g_noteFocus = true;
                std::snprintf(g_noteBuf, sizeof g_noteBuf, "%s", user ? user->c_str() : "");
                g_capture.reset();
            }
        }

        void EditCell(const Binding& b)
        {
            if (!b.editable) {
                Muted(TL("read-only"));
                if (ImGui::IsItemHovered() && b.kind == Kind::ControlMap && b.device == Device::Mouse)
                    Tooltip(TL("Mouse movement is no button, it can't be moved."));
                if (b.overridden) {  // changed by an older version: can still be undone
                    ImGui::SameLine();
                    if (ImGui::Button(Id(TL("Reset"), "resetone").c_str())) Rebind(b, b.defaultKey);
                    if (ImGui::IsItemHovered()) Tooltip(TLF("Back to the original bind: {0}", { CodeLabel(b.defaultKey) }));
                }
                return;
            }
            if (ImGui::Button(b.key == kUnbound ? Id(TL("Assign"), "bind").c_str() : Id(TL("Rebind"), "rebind").c_str())) StartCapture(b);
            if (b.key != kUnbound) {
                ImGui::SameLine();
                if (ImGui::Button(Id(TL("Unbind"), "unbind").c_str())) Rebind(b, kUnbound);
                if (ImGui::IsItemHovered())
                    Tooltip(b.kind == Kind::ControlMap ? TL("Removes the bind.")
                                                       : TL("Removes the bind."));
            }
            if (b.padKey != kUnbound) {
                ImGui::SameLine();
                if (ImGui::Button(Id(TL("Remove gamepad"), "unbindpad").c_str())) BindGamepad(b, kUnbound);
                if (ImGui::IsItemHovered())
                    Tooltip(TLF("Remove the gamepad button {0} added to this action. The previous bind keeps working.", { CodeLabel(b.padKey) }));
            }
            if (b.overridden) {
                ImGui::SameLine();
                if (ImGui::Button(Id(TL("Reset"), "resetone").c_str())) Rebind(b, b.defaultKey);
                if (ImGui::IsItemHovered()) Tooltip(TLF("Back to the original bind: {0}", { CodeLabel(b.defaultKey) }));
            }
        }

        // Heights measured on the last frame, per table and line mode.
        struct TableMetrics
        {
            float header = 0.0f;  // from the table's top to its first row
            float row    = 0.0f;  // one bind's row, borders included
        };
        std::unordered_map<std::string, TableMetrics> g_tableMetrics;
    }

    // `view`: the device the rows were picked on, see KeyText.
    void DrawTable(const char* id, const Model& model, std::vector<std::size_t> rows, bool showKey, Device view)
    {
        // columns shown, in order; Key and Edit fit their content, the rest share the width
        struct ColDef
        {
            ImGui::ImGuiID id;
            const char*    name;
            std::uint32_t  hideBit;  // 0: always shown
            float          weight;   // stretch weight; 0 = fixed, fit to content
        };
        constexpr ColDef kCols[] = {
            { kColKey, N_("Bind"), 0, 0.0f },
            { kColAction, N_("Action"), 1, 1.0f },
            { kColNote, N_("Note"), 2, 2.0f },
            { kColMod, N_("Mod"), 4, 1.0f },
            { kColContext, N_("Context"), 8, 1.0f },
            { kColSource, N_("Source"), 16, 1.0f },
            { kColEdit, N_("Edit"), 0, 0.0f },
        };
        const auto          hidden = HiddenColumns();
        std::vector<ColDef> cols;
        for (const auto& c : kCols)
            if (!(c.hideBit & hidden) && (c.id != kColKey || showKey)) cols.push_back(c);

        // What a column needs at its smallest: the Key and Edit contents (Edit with all its
        // buttons), a minimum for the stretch ones
        const auto* style  = ImGui::GetStyle();
        const float font   = ImGui::GetFontSize();
        const auto  textW  = [](const char* s) { return ImGui::CalcTextSize(s).x; };
        const float btn    = style->FramePadding.x * 2.0f, cell = style->CellPadding.x * 2.0f;
        const auto  needOf = [&](const ColDef& c) {
            switch (c.id) {
            case kColKey: return font * 7.0f + cell;
            case kColEdit:
                return textW(TL("Rebind")) + textW(TL("Unbind")) + textW(TL("Remove gamepad")) + textW(TL("Reset")) + btn * 4.0f +
                       style->ItemSpacing.x * 3.0f + cell;
            case kColNote: return font * 14.0f;
            case kColAction: return font * 10.0f;
            default: return font * 8.0f;
            }
        };

        // One line per bind when every column fits the window. Else two: Mod, Context and
        // Source move under the other columns (Mod under Action, Context under Note, Source
        // under Edit), so hiding columns brings the single line back. Only when even that
        // is too wide the table scrolls sideways.
        struct Slot
        {
            ColDef                top;
            std::optional<ColDef> bottom;
        };
        const auto  avail = ImGui::GetContentRegionAvail();
        float       need  = 0.0f;
        for (const auto& c : cols) need += needOf(c);
        std::vector<Slot> slots;
        const bool        twoLines = need > avail.x && cols.size() > 1;
        if (!twoLines) {
            for (const auto& c : cols) slots.push_back({ c, std::nullopt });
        } else {
            std::vector<ColDef> lower;
            for (const auto& c : cols)
                if (c.id == kColMod || c.id == kColContext || c.id == kColSource)
                    lower.push_back(c);
                else
                    slots.push_back({ c, std::nullopt });
            for (const auto& c : lower) {
                const auto under = c.id == kColMod ? kColAction : c.id == kColContext ? kColNote : kColEdit;
                auto       it    = std::ranges::find_if(slots, [&](const Slot& s) { return s.top.id == under && !s.bottom; });
                if (it == slots.end()) {  // its column is hidden: the free one furthest right
                    const auto r = std::ranges::find_if(slots.rbegin(), slots.rend(), [](const Slot& s) { return !s.bottom; });
                    if (r != slots.rend()) it = std::prev(r.base());
                }
                if (it != slots.end())
                    it->bottom = c;
                else
                    slots.push_back({ c, std::nullopt });
            }
            need = 0.0f;
            for (const auto& s : slots) need += (std::max)(needOf(s.top), s.bottom ? needOf(*s.bottom) : 0.0f);
        }
        const bool  anyBottom = std::ranges::any_of(slots, [](const Slot& s) { return s.bottom.has_value(); });
        const float inner     = (std::max)(avail.x, need);

        // height: all rows when they fit the space left, else that space and the rows scroll
        // (header row and Key column stay in view). Row and header heights are measured while
        // drawing, so the table ends right under its last row; the estimate is for the first frame.
        const std::string metricsId = std::string(id) + (twoLines ? "|2" : "|1");
        const float       line      = anyBottom ? ImGui::GetTextLineHeightWithSpacing() : 0.0f;
        const float       rowH      = ImGui::GetFrameHeight() + line + style->CellPadding.y * 2.0f + 1.0f;
        float             contentH  = rowH * static_cast<float>(rows.size() + 1) + 4.0f;
        if (const auto m = g_tableMetrics.find(metricsId); m != g_tableMetrics.end())
            contentH = m->second.header + m->second.row * static_cast<float>(rows.size()) + 2.0f;
        if (inner > avail.x) contentH += style->ScrollbarSize;
        const float height = (std::min)(contentH, (std::max)(avail.y, rowH * 8.0f));
        // one line and two keep their column widths apart
        ImGui::PushID(twoLines ? "lines2" : "lines1");
        if (!ImGui::BeginTable(id, static_cast<int>(slots.size()),
                ImGui::ImGuiTableFlags_RowBg | ImGui::ImGuiTableFlags_Borders | ImGui::ImGuiTableFlags_Resizable |
                    ImGui::ImGuiTableFlags_Sortable | ImGui::ImGuiTableFlags_SortMulti | ImGui::ImGuiTableFlags_ScrollX |
                    ImGui::ImGuiTableFlags_ScrollY,
                ImGui::ImVec2(0.0f, height), inner)) {
            ImGui::PopID();
            return;
        }
        ImGui::TableSetupScrollFreeze(showKey ? 1 : 0, 1);
        for (const auto& s : slots) {
            // Key and Edit fit their content (and what sits under them), the rest share the width
            int flags = s.top.weight > 0.0f ? ImGui::ImGuiTableColumnFlags_WidthStretch : ImGui::ImGuiTableColumnFlags_WidthFixed;
            if (s.top.id == kColKey) flags |= ImGui::ImGuiTableColumnFlags_DefaultSort;
            // a two-line header: the column sorts by its upper field
            const auto label = s.bottom ? Id(std::string(TL(s.top.name)) + '\n' + TL(s.bottom->name), std::string(s.top.name) + '+' + s.bottom->name)
                                        : Id(TL(s.top.name), s.top.name);
            ImGui::TableSetupColumn(label.c_str(), flags, s.top.weight, s.top.id);
        }
        ImGui::TableHeadersRow();

        SortRows(rows, [&](std::size_t a, std::size_t b, ImGui::ImGuiID col) { return CompareBindings(model.all[a], model.all[b], col); });

        const auto drawCell = [&](const Binding& b, ImGui::ImGuiID col) {
            switch (col) {
            case kColKey:
                if (b.key == kUnbound && b.padKey == kUnbound)
                    Muted(KeyText(b, view).c_str());
                else
                    ImGui::TextUnformatted(KeyText(b, view).c_str());
                break;
            case kColAction:
                ImGui::TextUnformatted(b.action.c_str());
                break;
            case kColNote:
                NoteCell(b);
                break;
            case kColMod:
                ImGui::TextUnformatted(b.owner.c_str());
                break;
            case kColContext:
                ImGui::TextUnformatted(b.context.c_str());
                if (!b.contextHint.empty() && ImGui::IsItemHovered()) Tooltip(b.contextHint);
                break;
            case kColSource:
                {
                    const auto slash = b.origin.find_last_of('/');
                    ImGui::TextUnformatted(slash == std::string::npos ? b.origin.c_str() : b.origin.c_str() + slash + 1);
                    if (ImGui::IsItemHovered()) Tooltip(b.origin);
                }
                break;
            case kColEdit:
                EditCell(b);
                break;
            default:
                break;
            }
        };

        float firstTop = 0.0f, lastBottom = 0.0f;  // screen y of the first row, bottom of the lowest cell
        for (auto i : rows) {
            const auto& b = model.all[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));
            for (int c = 0; c < static_cast<int>(slots.size()); ++c) {
                ImGui::TableSetColumnIndex(c);
                if (c == 0 && i == rows.front()) firstTop = ImGui::GetCursorScreenPos().y - style->CellPadding.y;
                drawCell(b, slots[c].top.id);
                if (slots[c].bottom) {  // the lower line, dimmed: it describes the one above
                    ImGui::PushStyleColor(ImGui::ImGuiCol_Text, ImGui::ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                    drawCell(b, slots[c].bottom->id);
                    ImGui::PopStyleColor();
                }
                lastBottom = (std::max)(lastBottom, ImGui::GetItemRectMax().y);
            }
            ImGui::PopID();
        }
        if (!rows.empty()) {
            // the table's top in the same coordinates (rows move up as it scrolls)
            const float top = ImGui::GetWindowPos().y - ImGui::GetScrollY();
            g_tableMetrics[metricsId] = { firstTop - top, (lastBottom + style->CellPadding.y - firstTop) / static_cast<float>(rows.size()) };
        }
        ImGui::EndTable();
        ImGui::PopID();
    }

}
