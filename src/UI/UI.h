#pragma once

// Shared state and helpers of the menu pages (render thread only).

#include "Core/Internal.h"
#include "UI/Layouts.h"

namespace HA::UI
{
    // ---------------------------------------------------------------- state (Common.cpp)

    extern Device                 g_selDevice;    // device of g_selected
    extern std::uint32_t          g_selected;     // key or button clicked on the device picture
    extern bool                   g_hasSelected;  // mouse button 0 (LMB) is a valid selection
    extern std::optional<Binding> g_capture;      // binding waiting for its new input
    extern char                   g_filter[128];
    extern std::string            g_noteEdit;  // NoteId() of the note being edited, empty = none
    extern char                   g_noteBuf[256];
    extern bool                   g_noteFocus;  // focus the note editor on its first frame

    // refreshed once per frame by each page
    extern std::shared_ptr<const std::set<std::string>> g_bl;
    extern std::shared_ptr<const Notes>                 g_uiNotes;

    // ---------------------------------------------------------------- helpers (Common.cpp)

    struct KeyInfo
    {
        std::size_t count   = 0;
        bool        overlap = false;  // bound by more than one owner
    };

    void                     Select(Device device, std::uint32_t id);  // a second click on the same one clears it
    std::string              KeyLabel(std::uint32_t dik);
    std::string              LowerStr(std::string s);  // ASCII and Cyrillic
    void                     Muted(const char* text);
    void                     Tooltip(const std::string& text);
    std::string              Id(std::string_view label, std::string_view id);  // translated label, fixed ImGui id
    const std::string*       UserNote(const Binding& b);
    const std::string&       NoteText(const Binding& b);  // the user's note, else the built-in one
    bool                     IsBlacklisted(const std::string& owner);
    bool                     Visible(const Binding& b);
    std::vector<std::size_t> RowsFor(const Model& m, Device device, std::uint32_t id);
    std::vector<std::size_t> TableRowsFor(const Model& m, Device device, std::uint32_t id);
    KeyInfo                  Info(const Model& m, Device device, std::uint32_t id);
    std::string              ComboLabel(std::uint32_t key, std::uint8_t mods);
    std::string              ComboLabel(const Binding& b);
    std::string              ButtonLabel(Device device, std::uint32_t id);
    std::string              CodeLabel(std::uint32_t code);
    std::string              KeyText(const Binding& b, Device view = Device::Keyboard, bool all = false);
    void                     Toolbar(std::optional<Device> reset = std::nullopt);  // reset: the device Reset undoes, none = all

    // ---------------------------------------------------------------- capture (Capture.cpp)

    void ClearPending();
    void SyncMenuHotkey(bool force = false);
    void StartCapture(const Binding& b);
    void EndCapture();
    void HandleCapture();

    // ---------------------------------------------------------------- device pictures (DeviceView.cpp)

    std::span<const ButtonDef> KeyboardDefs();
    void DrawDevice(const Model& model, Device device, std::span<const ButtonDef> defs, float unitsW, float unitsH, float maxUnit);
    void DrawPicture(const Model& model, Device device, std::span<const ButtonDef> defs, float unitsW, float unitsH, float maxUnit);

    // ---------------------------------------------------------------- tables (BindTable.cpp)
    // ---- table sorting: click a header; shift+click adds a secondary column

    // negative/zero/positive, ignoring case
    inline int CompareText(std::string_view a, std::string_view b)
    {
        const auto n = (std::min)(a.size(), b.size());
        for (std::size_t i = 0; i < n; ++i) {
            const int ca = std::tolower(static_cast<unsigned char>(a[i]));
            const int cb = std::tolower(static_cast<unsigned char>(b[i]));
            if (ca != cb) return ca - cb;
        }
        return static_cast<int>(a.size()) - static_cast<int>(b.size());
    }

    template <class T>
    int Compare3(const T& a, const T& b)
    {
        return a < b ? -1 : b < a ? 1 : 0;
    }

    // Sorts `rows` by the table's current sort specs; cmp(a, b, columnUserId) -> <0, 0, >0.
    // Stable, so equal rows keep the scan order (key, modifiers, mod, action).
    template <class Row, class Cmp>
    void SortRows(std::vector<Row>& rows, Cmp cmp)
    {
        const auto* specs = ImGui::TableGetSortSpecs();
        if (!specs || specs->SpecsCount == 0) return;
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            for (int s = 0; s < specs->SpecsCount; ++s) {
                const auto& spec = specs->Specs[s];
                const int   r    = cmp(a, b, spec.ColumnUserID);
                if (r != 0) return spec.SortDirection == ImGui::ImGuiSortDirection_Descending ? r > 0 : r < 0;
            }
            return false;
        });
    }

    enum Col : ImGui::ImGuiID
    {
        kColKey = 1,
        kColAction,
        kColMod,
        kColContext,
        kColSource,
        kColEdit,
        kColHide,
        kColHotkeys,
        kColNote
    };

    // `view`: the device the rows were picked on, see KeyText.
    void DrawTable(const char* id, const Model& model, std::vector<std::size_t> rows, bool showKey, Device view = Device::Keyboard);
    // ImGui's own menu on a right-clicked table header: in the interface language while one of
    // our pages draws, ImGui's English put back after it (other mods share the ImGui context)
    class TableMenuText
    {
    public:
        TableMenuText()
        {
            static const auto english = [] {
                std::array<ImGui::ImGuiLocEntry, std::size(kKeys)> e{};
                for (std::size_t i = 0; i < e.size(); ++i) e[i] = { kKeys[i], ImGui::LocalizeGetMsg(kKeys[i]) };
                return e;
            }();
            m_english = english.data();
            static std::array<std::string, std::size(kKeys)> text;  // ImGui keeps the pointers
            std::array<ImGui::ImGuiLocEntry, std::size(kKeys)> ours{};
            for (std::size_t i = 0; i < ours.size(); ++i) {
                const std::string_view en = english[i].Text ? english[i].Text : "";
                const auto             id = en.find("###");  // keeps the item's ImGui id
                text[i] = std::string(TL(kText[i])) + std::string(id == std::string_view::npos ? std::string_view() : en.substr(id));
                ours[i] = { kKeys[i], text[i].c_str() };
            }
            ImGui::LocalizeRegisterEntries(ours.data(), static_cast<int>(ours.size()));
        }
        ~TableMenuText() { ImGui::LocalizeRegisterEntries(m_english, static_cast<int>(std::size(kKeys))); }
        TableMenuText(const TableMenuText&)            = delete;
        TableMenuText& operator=(const TableMenuText&) = delete;

    private:
        static constexpr ImGui::ImGuiLocKey kKeys[] = { ImGui::ImGuiLocKey_TableSizeOne, ImGui::ImGuiLocKey_TableSizeAllFit,
            ImGui::ImGuiLocKey_TableSizeAllDefault, ImGui::ImGuiLocKey_TableResetOrder };
        static constexpr const char* kText[] = { N_("Size column to fit"), N_("Size all columns to fit"), N_("Size all columns to default"),
            N_("Reset order") };
        const ImGui::ImGuiLocEntry* m_english = nullptr;
    };
}
