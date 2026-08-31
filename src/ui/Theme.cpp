// Visual style. Kept in one place so panels never hard-code colours.

#include "ui/Ui.hpp"

#include <imgui.h>

namespace ktxcmp::ui {

ImVec4 accentColor()     { return ImVec4(0.29f, 0.56f, 0.78f, 1.00f); }
ImVec4 panelBgColor()    { return ImVec4(0.13f, 0.14f, 0.16f, 1.00f); }
ImVec4 viewportBgColor() { return ImVec4(0.09f, 0.09f, 0.10f, 1.00f); }
ImVec4 errorColor()      { return ImVec4(0.90f, 0.49f, 0.42f, 1.00f); }

void applyTheme(float scale) {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 0.0f;
    s.ChildRounding     = 3.0f;
    s.FrameRounding     = 3.0f;
    s.GrabRounding      = 3.0f;
    s.PopupRounding     = 3.0f;
    s.TabRounding       = 3.0f;
    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.WindowPadding     = ImVec2(6, 6);
    s.FramePadding      = ImVec2(5, 3);
    s.ItemSpacing       = ImVec2(6, 4);
    s.ItemInnerSpacing  = ImVec2(4, 4);
    s.ScrollbarSize     = 11.0f;
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = panelBgColor();
    c[ImGuiCol_ChildBg]         = ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.11f, 0.12f, 0.14f, 0.98f);
    c[ImGuiCol_Border]          = ImVec4(0.26f, 0.27f, 0.30f, 1.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.21f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.27f, 0.29f, 0.33f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.31f, 0.34f, 0.39f, 1.00f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_MenuBarBg]       = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.29f, 0.31f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonActive]    = accentColor();
    c[ImGuiCol_Header]          = ImVec4(0.24f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.29f, 0.31f, 0.35f, 1.00f);
    c[ImGuiCol_HeaderActive]    = accentColor();
    c[ImGuiCol_Separator]       = ImVec4(0.24f, 0.25f, 0.28f, 1.00f);
    c[ImGuiCol_Tab]             = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);
    c[ImGuiCol_TabSelected]     = ImVec4(0.20f, 0.22f, 0.25f, 1.00f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.26f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_DockingPreview]  = ImVec4(accentColor().x, accentColor().y, accentColor().z, 0.55f);
    c[ImGuiCol_DockingEmptyBg]  = viewportBgColor();
    c[ImGuiCol_TextDisabled]    = ImVec4(0.52f, 0.54f, 0.58f, 1.00f);

    s.ScaleAllSizes(scale);
}

namespace {

ImVec4 desaturate(const ImVec4& v) {
    const float l = 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
    return ImVec4(l, l, l, v.w);
}

constexpr ImGuiCol kGrayscaled[] = {
    ImGuiCol_Text, ImGuiCol_TextDisabled, ImGuiCol_ChildBg,
    ImGuiCol_Border, ImGuiCol_Separator, ImGuiCol_FrameBg,
};

}  // namespace

// Luminance-preserving grey. The card stays legible; only its state reads as
// inactive. Deliberately not an alpha fade (PLAN.md, left rail).
void pushGrayscale() {
    const ImVec4* c = ImGui::GetStyle().Colors;
    for (ImGuiCol col : kGrayscaled)
        ImGui::PushStyleColor(col, desaturate(c[col]));
}

void popGrayscale() {
    ImGui::PopStyleColor(IM_ARRAYSIZE(kGrayscaled));
}

}  // namespace ktxcmp::ui
