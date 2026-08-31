// Frame composition: menu bar, status bar, dockspace, default layout.
//
// The default layout is built once, when no dock node exists for the dockspace
// id - which is the case on a first run and after "Reset layout". Otherwise the
// layout comes from imgui.ini and the builder must not touch it.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder, BeginViewportSideBar, ImHashStr

namespace ktxcmp::ui {
namespace {

ImGuiID dockspaceId() {
    // Hashed directly rather than via GetID so it does not depend on which
    // window happens to be current.
    return ImHashStr("ktxcmp.dockspace");
}

void buildDefaultLayout(ImGuiID rootId, ImVec2 size, float scale) {
    const float w = size.x > 1.0f ? size.x : 1280.0f;
    const float h = size.y > 1.0f ? size.y : 800.0f;

    const float leftPx   = kLeftRailWidth * scale;
    const float rightPx  = kRightRailWidth * scale;
    const float bottomPx = kMipStripHeight * scale;

    ImGui::DockBuilderRemoveNode(rootId);
    ImGui::DockBuilderAddNode(rootId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(rootId, ImVec2(w, h));

    // Ratios are relative to the node being split, which shrinks as we go.
    ImGuiID centre = rootId;
    const ImGuiID left =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, leftPx / w, nullptr, &centre);
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, rightPx / (w - leftPx), nullptr, &centre);
    const ImGuiID bottom =
        ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, bottomPx / h, nullptr, &centre);

    ImGui::DockBuilderDockWindow(kWinSources, left);
    ImGui::DockBuilderDockWindow(kWinAnalysis, right);
    ImGui::DockBuilderDockWindow(kWinMipStrip, bottom);
    ImGui::DockBuilderDockWindow(kWinViewport, centre);

    // The rails hold their pixel width when the window resizes; the viewport
    // takes the slack. Without this the split is a ratio and the layout stops
    // holding at small sizes.
    auto lock = [](ImGuiID id, ImGuiDockNodeFlags flag) {
        if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(id))
            node->LocalFlags |= flag;
    };
    lock(left,   ImGuiDockNodeFlags_NoResizeX);
    lock(right,  ImGuiDockNodeFlags_NoResizeX);
    lock(bottom, ImGuiDockNodeFlags_NoResizeY);

    ImGui::DockBuilderFinish(rootId);
}

void drawMenuBar(AppState& app) {
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Quit", "Alt+F4"))
            app.running = false;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Reset layout"))
            app.resetLayout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

}  // namespace

void field(const char* label, const char* value) {
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    const float valueWidth = ImGui::CalcTextSize(value).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > valueWidth)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - valueWidth);
    ImGui::TextUnformatted(value);
}

bool beginCard(const char* id, float height) {
    ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    if (height <= 0.0f)
        flags |= ImGuiChildFlags_AutoResizeY;
    return ImGui::BeginChild(id, ImVec2(0.0f, height), flags);
}

void endCard() {
    ImGui::EndChild();
}

void drawStatusBar(AppState& app) {
    (void)app;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();

    if (ImGui::BeginViewportSideBar("##ktxcmp.statusbar", vp, ImGuiDir_Down, height,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            // Pixel inspector. Format-aware from M6; ints for now, nothing to show.
            ImGui::TextDisabled("xy");
            ImGui::SameLine();
            ImGui::TextUnformatted(kEmptyValue);

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("A");
            ImGui::SameLine();
            ImGui::TextUnformatted(kEmptyValue);

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("B");
            ImGui::SameLine();
            ImGui::TextUnformatted(kEmptyValue);

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("delta");
            ImGui::SameLine();
            ImGui::TextUnformatted(kEmptyValue);

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

void drawFrame(AppState& app) {
    drawMenuBar(app);
    drawStatusBar(app);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiID rootId = dockspaceId();

    const bool needsLayout = app.resetLayout || ImGui::DockBuilderGetNode(rootId) == nullptr;
    if (needsLayout) {
        app.resetLayout = false;
        buildDefaultLayout(rootId, vp->WorkSize, app.uiScale);
    }

    ImGui::DockSpaceOverViewport(rootId, vp, ImGuiDockNodeFlags_None);

    drawSourcesPanel(app);
    drawViewportPanel(app);
    drawAnalysisPanel(app);
    drawMipStripPanel(app);
}

}  // namespace ktxcmp::ui
