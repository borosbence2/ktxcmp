// Frame composition: menu bar, status bar, dockspace, default layout.
//
// The default layout is built once, when no dock node exists for the dockspace
// id - which is the case on a first run and after "Reset layout". Otherwise the
// layout comes from imgui.ini and the builder must not touch it.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"

#include <imgui.h>
#include <imgui_internal.h>  // DockBuilder, BeginViewportSideBar, ImHashStr

#include <thread>

namespace ktxcmp::ui {

namespace {
unsigned displayWorkers() {
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware > 2 ? 2u : 1u;
}
}  // namespace

UiState::UiState() : display(displayWorkers()) {}

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
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            app.openDialogRequested = true;
        if (ImGui::MenuItem("Open reference chain...", nullptr, false, app.slotA.loaded()))
            app.openChainDialogRequested = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("A folder of PNGs, matched to the mip levels by dimension.%s",
                              app.slotA.loaded() ? "" : " Load a texture first.");
        if (ImGui::MenuItem("Clear reference chain", nullptr, false, app.slotB.hasChain()))
            app.clearReferenceChain();
        ImGui::Separator();
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
    const float labelWidth = ImGui::CalcTextSize(label).x;
    const float valueWidth = ImGui::CalcTextSize(value).x;
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    ImGui::TextDisabled("%s", label);

    // The rails are 136 and 158px wide, so a real format name does not always
    // fit beside its label. Drop it to its own line rather than overlapping.
    if (labelWidth + gap + valueWidth <= rowWidth)
        ImGui::SameLine();

    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > valueWidth)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - valueWidth);
    ImGui::TextUnformatted(value);
}

// A single arc sweeping round. Deliberately the only "waiting" visual in the
// program, so a pending thumbnail and a pending viewport read as the same state.
void drawPendingIndicator(ImVec2 centre, float radius) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float t = static_cast<float>(ImGui::GetTime());
    const float start = t * 3.0f;
    const ImU32 colour = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    draw->PathClear();
    draw->PathArcTo(centre, radius, start, start + 1.9f, 24);
    draw->PathStroke(colour, ImDrawFlags_None, radius * 0.28f);
}

void pendingHint(const char* what) {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 centre(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f - 10.0f);
    drawPendingIndicator(centre, 11.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(what);
    ImGui::SetCursorScreenPos(ImVec2(centre.x - textSize.x * 0.5f, centre.y + 18.0f));
    ImGui::TextDisabled("%s", what);
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

void drawStatusBar(AppState& app, UiState& ui) {
    (void)app;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();

    if (ImGui::BeginViewportSideBar("##ktxcmp.statusbar", vp, ImGuiDir_Down, height,
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar)) {
        if (ImGui::BeginMenuBar()) {
            // Pixel inspector. Becomes format-aware at M6; 8-bit ints plus the
            // underlying float is the honest readout for an LDR surface.
            ImGui::TextDisabled("xy");
            ImGui::SameLine();
            if (ui.hasHover) {
                ImGui::Text("%d, %d", ui.hoverX, ui.hoverY);
            } else {
                ImGui::TextUnformatted(kEmptyValue);
            }

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("A");
            ImGui::SameLine();
            if (!ui.hasHover) {
                ImGui::TextUnformatted(kEmptyValue);
            } else if (ui.hoverNormalMode) {
                ImGui::Text("n %+.3f %+.3f %+.3f", ui.hoverNormalA[0], ui.hoverNormalA[1],
                            ui.hoverNormalA[2]);
            } else {
                ImGui::Text("%3d %3d %3d %3d", static_cast<int>(ui.hoverValue[0] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValue[1] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValue[2] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValue[3] * 255.0f + 0.5f));
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::TextDisabled("(%.4f %.4f %.4f %.4f)", ui.hoverValue[0], ui.hoverValue[1],
                                    ui.hoverValue[2], ui.hoverValue[3]);
            }

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled("B");
            ImGui::SameLine();
            if (!ui.hasHoverB) {
                ImGui::TextUnformatted(kEmptyValue);
            } else if (ui.hoverNormalMode) {
                ImGui::Text("n %+.3f %+.3f %+.3f", ui.hoverNormalB[0], ui.hoverNormalB[1],
                            ui.hoverNormalB[2]);
            } else {
                ImGui::Text("%3d %3d %3d %3d", static_cast<int>(ui.hoverValueB[0] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValueB[1] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValueB[2] * 255.0f + 0.5f),
                            static_cast<int>(ui.hoverValueB[3] * 255.0f + 0.5f));
            }

            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled(ui.hoverNormalMode ? "angle" : "delta");
            ImGui::SameLine();
            if (!ui.hasHover || !ui.hasHoverB) {
                ImGui::TextUnformatted(kEmptyValue);
            } else if (ui.hoverNormalMode) {
                ImGui::Text("%.3f deg", static_cast<double>(ui.hoverAngleDeg));
            } else {
                // Signed, in the same 0-255 units the metrics use.
                ImGui::Text("%+.1f %+.1f %+.1f %+.1f",
                            (ui.hoverValue[0] - ui.hoverValueB[0]) * 255.0f,
                            (ui.hoverValue[1] - ui.hoverValueB[1]) * 255.0f,
                            (ui.hoverValue[2] - ui.hoverValueB[2]) * 255.0f,
                            (ui.hoverValue[3] - ui.hoverValueB[3]) * 255.0f);
            }

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

// PLAN.md, Keyboard. Skipped while a widget has focus so typing never triggers
// a view change.
void handleShortcuts(AppState& app, UiState& ui) {
    if (ImGui::IsAnyItemActive())
        return;
    ViewState& v = app.view;

    const bool ctrl = ImGui::GetIO().KeyCtrl;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_1, false)) {
        ui.zoom = 1.0f;
        return;
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        app.openDialogRequested = true;
        return;
    }
    if (ctrl)
        return;

    // Isolate one channel, or 0 for all three.
    if (ImGui::IsKeyPressed(ImGuiKey_1, false)) v.channels = {true, false, false, false};
    if (ImGui::IsKeyPressed(ImGuiKey_2, false)) v.channels = {false, true, false, false};
    if (ImGui::IsKeyPressed(ImGuiKey_3, false)) v.channels = {false, false, true, false};
    if (ImGui::IsKeyPressed(ImGuiKey_4, false)) v.channels = {false, false, false, true};
    if (ImGui::IsKeyPressed(ImGuiKey_0, false)) v.channels = {true, true, true, false};

    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        ui.fitRequested = true;

    if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, true) && v.level > 0)
        --v.level;
    if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, true))
        ++v.level;  // clamped against the file in ensureDecoded

    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        v.viewMode = v.viewMode == ViewMode::A ? ViewMode::B : ViewMode::A;
}

void drawFrame(AppState& app, UiState& ui) {
    handleShortcuts(app, ui);
    drawMenuBar(app);
    drawStatusBar(app, ui);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiID rootId = dockspaceId();

    const bool needsLayout = app.resetLayout || ImGui::DockBuilderGetNode(rootId) == nullptr;
    if (needsLayout) {
        app.resetLayout = false;
        buildDefaultLayout(rootId, vp->WorkSize, app.uiScale);
    }

    ImGui::DockSpaceOverViewport(rootId, vp, ImGuiDockNodeFlags_None);

    drawSourcesPanel(app);
    drawViewportPanel(app, ui);
    drawAnalysisPanel(app);
    drawMipStripPanel(app, ui);
}

}  // namespace ktxcmp::ui
