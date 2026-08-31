// The four dock panels. M0 renders the layout and its empty state only: no
// panel reads a file, a surface, or a metric, because none of that exists yet.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"

#include <imgui.h>

#include <cfloat>
#include <cstdio>

namespace ktxcmp::ui {
namespace {

const char* viewModeName(ViewMode m) {
    switch (m) {
        case ViewMode::A:     return "A";
        case ViewMode::B:     return "B";
        case ViewMode::Diff:  return "Diff";
        case ViewMode::Split: return "Split";
        case ViewMode::Onion: return "Onion";
    }
    return "?";
}

bool toggleButton(const char* label, bool selected, const ImVec2& size = ImVec2(0, 0)) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, accentColor());
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accentColor());
    }
    const bool pressed = ImGui::Button(label, size);
    if (selected)
        ImGui::PopStyleColor(2);
    return pressed;
}

// Places the next item flush against the right edge of the content region.
void alignRight(float itemWidth) {
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > itemWidth)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - itemWidth);
}

void centeredHint(const char* text) {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + (size.x - textSize.x) * 0.5f,
                                     pos.y + (size.y - textSize.y) * 0.5f));
    ImGui::TextDisabled("%s", text);
}

void sectionLabel(const char* text) {
    ImGui::TextDisabled("%s", text);
}

// ---------------------------------------------------------------- slots ----

void drawSlotA(const SlotState& slot) {
    (void)slot;
    sectionLabel("SLOT A");
    if (beginCard("##slotA")) {
        ImGui::TextDisabled("no file");
        ImGui::Separator();
        field("format", kEmptyValue);
        field("size", kEmptyValue);
        field("levels", kEmptyValue);
        field("layers", kEmptyValue);
        field("faces", kEmptyValue);
        field("supercomp", kEmptyValue);
        field("flags", kEmptyValue);
    }
    endCard();
}

void drawSlotB(const SlotState& slot, bool unused) {
    (void)slot;
    if (unused)
        pushGrayscale();

    sectionLabel("SLOT B");
    if (unused) {
        ImGui::SameLine();
        const char* tag = "not used";
        alignRight(ImGui::CalcTextSize(tag).x);
        ImGui::TextDisabled("%s", tag);
    }

    if (beginCard("##slotB")) {
        ImGui::TextDisabled("no file");
        ImGui::Separator();
        field("format", kEmptyValue);
        field("size", kEmptyValue);
        field("chain", kEmptyValue);
    }
    endCard();

    if (unused)
        popGrayscale();
}

void drawDropTarget() {
    if (beginCard("##drop", ImGui::GetFrameHeight() * 2.2f)) {
        centeredHint("Drop files here");
    }
    endCard();
}

// ------------------------------------------------------------- viewport ----

void drawViewModeRow(AppState& app) {
    ViewState& v = app.view;
    static const ViewMode kModes[] = {ViewMode::A, ViewMode::B, ViewMode::Diff,
                                      ViewMode::Split, ViewMode::Onion};
    for (int i = 0; i < IM_ARRAYSIZE(kModes); ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (toggleButton(viewModeName(kModes[i]), v.viewMode == kModes[i]))
            v.viewMode = kModes[i];
    }
}

void drawViewportBody(const AppState& app, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, viewportBgColor());
    ImGui::BeginChild("##viewportBody", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    centeredHint("No image loaded");

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float pad = ImGui::GetStyle().FramePadding.x + 2.0f;

    char overlay[96];
    std::snprintf(overlay, sizeof(overlay), "%s | mip %d | %s",
                  viewModeName(app.view.viewMode), app.view.level, kEmptyValue);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y + pad));
    ImGui::TextDisabled("%s", overlay);

    const char* zoom = "100%";
    const ImVec2 zoomSize = ImGui::CalcTextSize(zoom);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - zoomSize.x - pad,
                                     pos.y + size.y - zoomSize.y - pad));
    ImGui::TextDisabled("%s", zoom);

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// Channel isolate buttons, then exactly one contextual control on the right.
// There is no second permanent control row (PLAN.md, UI specification).
void drawControlRow(AppState& app) {
    ViewState& v = app.view;
    const float sq = ImGui::GetFrameHeight();
    const ImVec2 chip(sq, sq);

    if (toggleButton("R", v.channels.r, chip)) v.channels.r = !v.channels.r;
    ImGui::SameLine();
    if (toggleButton("G", v.channels.g, chip)) v.channels.g = !v.channels.g;
    ImGui::SameLine();
    if (toggleButton("B", v.channels.b, chip)) v.channels.b = !v.channels.b;
    ImGui::SameLine();
    if (toggleButton("A", v.channels.a, chip)) v.channels.a = !v.channels.a;

    ImGui::SameLine();
    switch (v.viewMode) {
        case ViewMode::Diff: {
            static const char* kGains[] = {"gain 1x", "gain 4x", "gain 8x", "gain 16x"};
            static const int kGainValues[] = {1, 4, 8, 16};
            int current = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kGainValues); ++i)
                if (kGainValues[i] == v.diffGain)
                    current = i;
            const float width = ImGui::CalcTextSize("gain 16x").x + sq;
            alignRight(width);
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##gain", &current, kGains, IM_ARRAYSIZE(kGains)))
                v.diffGain = kGainValues[current];
            break;
        }
        case ViewMode::Split: {
            const float width = ImGui::CalcTextSize("wipe 0.00").x * 1.6f;
            alignRight(width);
            ImGui::SetNextItemWidth(width);
            ImGui::SliderFloat("##wipe", &v.splitWipe, 0.0f, 1.0f, "wipe %.2f");
            break;
        }
        case ViewMode::Onion: {
            const float width = ImGui::CalcTextSize("blend 0.00").x * 1.6f;
            alignRight(width);
            ImGui::SetNextItemWidth(width);
            ImGui::SliderFloat("##blend", &v.onionBlend, 0.0f, 1.0f, "blend %.2f");
            break;
        }
        case ViewMode::A:
        case ViewMode::B:
            ImGui::NewLine();  // no contextual control in these modes
            break;
    }
}

// ------------------------------------------------------------- analysis ----

void drawFormatBanner() {
    // Accent, not warning: successful auto-detection is normal operation.
    const ImVec4 accent = accentColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(accent.x, accent.y, accent.z, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.70f));
    if (beginCard("##formatBanner")) {
        // Wrapped, not clipped: real format strings are longer than the rail.
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("no file");
        ImGui::TextWrapped("no interpretation yet");
        ImGui::PopStyleColor();
    }
    endCard();
    ImGui::PopStyleColor(2);
}

void drawCompareControls(AppState& app) {
    ViewState& v = app.view;

    // A dropdown, not tabs: modes 2 and 3 bring dependent controls with them.
    static const char* kModes[] = {"1 Encode fidelity", "2 Chain vs ref", "3 Self-consistency"};
    int current = static_cast<int>(v.compareMode);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##compareMode", &current, kModes, IM_ARRAYSIZE(kModes)))
        v.compareMode = static_cast<CompareMode>(current);

    if (v.compareMode == CompareMode::EncodeFidelity)
        return;  // no resampling, so no filter controls

    static const char* kFilters[] = {"Box", "Triangle", "Kaiser", "Lanczos3", "Mitchell"};
    int filter = static_cast<int>(v.filter);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::Combo("##filter", &filter, kFilters, IM_ARRAYSIZE(kFilters)))
        v.filter = static_cast<Filter>(filter);

    ImGui::Checkbox("Linear light", &v.linearLight);
}

void drawMetrics() {
    // Hero number, then the supporting rows. The full per-level table lives
    // behind the CSV export, not permanently on screen.
    if (beginCard("##hero")) {
        ImGui::TextDisabled("PSNR-RGB");
        const ImGuiStyle& style = ImGui::GetStyle();
        const float base = style.FontSizeBase > 0.0f ? style.FontSizeBase : ImGui::GetFontSize();
        ImGui::PushFont(nullptr, base * 1.9f);
        ImGui::TextUnformatted(kEmptyValue);
        ImGui::PopFont();
    }
    endCard();

    field("RMSE", kEmptyValue);
    field("SSIM", kEmptyValue);
    field("max err", kEmptyValue);
    field("alpha", kEmptyValue);
}

void drawErrorPlot() {
    sectionLabel("Error by level");
    // Tall enough to click points on (PLAN.md, right rail).
    if (beginCard("##errorPlot", 132.0f)) {
        centeredHint("no data");
    }
    endCard();
}

}  // namespace

void drawSourcesPanel(AppState& app) {
    if (ImGui::Begin(kWinSources)) {
        drawSlotA(app.slotA);
        ImGui::Spacing();
        drawSlotB(app.slotB, app.view.compareMode == CompareMode::SelfConsistency);
        ImGui::Spacing();
        drawDropTarget();
    }
    ImGui::End();
}

void drawViewportPanel(AppState& app) {
    if (ImGui::Begin(kWinViewport)) {
        drawViewModeRow(app);

        const float controlRow = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        const float body = ImGui::GetContentRegionAvail().y - controlRow;
        drawViewportBody(app, body > 0.0f ? body : 0.0f);

        drawControlRow(app);
    }
    ImGui::End();
}

void drawAnalysisPanel(AppState& app) {
    if (ImGui::Begin(kWinAnalysis)) {
        drawFormatBanner();
        ImGui::Spacing();
        drawCompareControls(app);
        ImGui::Spacing();
        drawMetrics();
        ImGui::Spacing();
        drawErrorPlot();
    }
    ImGui::End();
}

void drawMipStripPanel(AppState& app) {
    (void)app;
    if (ImGui::Begin(kWinMipStrip)) {
        // Warning badges live on these thumbnails, not in a separate panel.
        if (beginCard("##mipStrip")) {
            centeredHint("No mip levels");
        }
        endCard();
    }
    ImGui::End();
}

}  // namespace ktxcmp::ui
