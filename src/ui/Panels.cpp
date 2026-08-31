// The four dock panels.
//
// M1: the source cards, the format banner and the mip strip read real container
// metadata. Nothing here decodes a texel, so the viewport is still empty.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"
#include "container/KtxFile.hpp"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <string>

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

std::string dimsText(int w, int h) {
    return std::to_string(w) + "x" + std::to_string(h);
}

std::string byteSizeText(std::uint64_t bytes) {
    char buf[48];
    if (bytes >= 1024ull * 1024ull)
        std::snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    else if (bytes >= 1024ull)
        std::snprintf(buf, sizeof(buf), "%.1f kB", static_cast<double>(bytes) / 1024.0);
    else
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    return buf;
}

// Describes what the container actually told us, without inventing a value for
// anything it could not tell us (CLAUDE.md, trap 9).
std::string flagsText(const KtxInfo& info) {
    std::string out = info.format.transferFn == TransferFn::Srgb ? "sRGB" : "linear";
    if (!info.hasDfd)
        return out + ", no DFD";
    if (info.premultiplied.has_value())
        out += *info.premultiplied ? ", premultiplied" : ", straight alpha";
    return out;
}

// ---------------------------------------------------------------- slots ----

void drawSlotBody(const SlotState& slot) {
    if (slot.failed()) {
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        ImGui::TextWrapped("%s", categoryName(slot.error->code));
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", slot.error->message.c_str());
        return;
    }
    if (!slot.loaded()) {
        ImGui::TextDisabled("no file");
        ImGui::Separator();
        field("format", kEmptyValue);
        field("size", kEmptyValue);
        field("levels", kEmptyValue);
        field("layers", kEmptyValue);
        field("faces", kEmptyValue);
        field("supercomp", kEmptyValue);
        field("flags", kEmptyValue);
        return;
    }

    const KtxInfo& info = slot.ktx->info();
    ImGui::TextWrapped("%s", slot.path.filename().string().c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", slot.path.string().c_str());
    ImGui::Separator();

    field("format", info.formatName.c_str());
    field("size", dimsText(info.baseWidth, info.baseHeight).c_str());
    field("levels", std::to_string(info.levelCount).c_str());
    field("layers", std::to_string(info.layerCount).c_str());
    field("faces", std::to_string(info.faceCount).c_str());
    field("supercomp", supercompressionName(info.supercompression));
    field("container", info.version == ContainerVersion::Ktx1 ? "KTX1" : "KTX2");
    field("flags", flagsText(info).c_str());
    field("bytes", byteSizeText(info.dataSize).c_str());
    if (info.needsTranscoding)
        ImGui::TextDisabled("needs transcode");
}

void drawSlotA(const SlotState& slot) {
    sectionLabel("SLOT A");
    if (beginCard("##slotA"))
        drawSlotBody(slot);
    endCard();
}

void drawSlotB(const SlotState& slot, bool unused) {
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
        if (slot.loaded() || slot.failed()) {
            drawSlotBody(slot);
        } else {
            ImGui::TextDisabled("no file");
            ImGui::Separator();
            field("format", kEmptyValue);
            field("size", kEmptyValue);
            field("chain", kEmptyValue);
        }
    }
    endCard();

    if (unused)
        popGrayscale();
}

void drawDropTarget() {
    if (beginCard("##drop", ImGui::GetFrameHeight() * 2.2f)) {
        centeredHint("Drop .ktx / .ktx2");
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

    const SlotState& slot = app.slotA;
    centeredHint(slot.loaded() ? "Decoding arrives at M2" : "No image loaded");

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float pad = ImGui::GetStyle().FramePadding.x + 2.0f;

    std::string dims = kEmptyValue;
    if (slot.loaded()) {
        const KtxInfo& info = slot.ktx->info();
        const int level = app.view.level < info.levelCount ? app.view.level : 0;
        dims = dimsText(info.levels[static_cast<std::size_t>(level)].w,
                        info.levels[static_cast<std::size_t>(level)].h);
    }
    char overlay[128];
    std::snprintf(overlay, sizeof(overlay), "%s | mip %d | %s",
                  viewModeName(app.view.viewMode), app.view.level, dims.c_str());
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

void drawFormatBanner(const AppState& app) {
    // Accent, not warning: successful auto-detection is normal operation.
    const ImVec4 accent = accentColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(accent.x, accent.y, accent.z, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.70f));
    if (beginCard("##formatBanner")) {
        // Wrapped, not clipped: real format strings are longer than the rail.
        if (app.slotA.loaded()) {
            const KtxInfo& info = app.slotA.ktx->info();
            ImGui::TextWrapped("%s", info.formatName.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s, %s", info.version == ContainerVersion::Ktx1 ? "KTX1" : "KTX2",
                               info.hasDfd ? "DFD present" : "no DFD");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("no file");
            ImGui::TextWrapped("no interpretation yet");
            ImGui::PopStyleColor();
        }
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
        drawFormatBanner(app);
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
    if (ImGui::Begin(kWinMipStrip)) {
        if (!app.slotA.loaded()) {
            if (beginCard("##mipStrip"))
                centeredHint("No mip levels");
            endCard();
        } else {
            // Thumbnails and warning badges arrive at M3/M5; the strip is already
            // the navigation, so the levels are selectable now.
            const KtxInfo& info = app.slotA.ktx->info();
            const float cellW = 84.0f * app.uiScale;
            const float cellH = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize;
            ImGui::BeginChild("##mipStrip", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar);
            for (int level = 0; level < info.levelCount; ++level) {
                const LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
                if (level > 0)
                    ImGui::SameLine();
                ImGui::BeginGroup();
                char label[64];
                std::snprintf(label, sizeof(label), "%d\n%dx%d##lvl%d", level, li.w, li.h, level);
                if (toggleButton(label, app.view.level == level,
                                 ImVec2(cellW, cellH > 0.0f ? cellH : 0.0f)))
                    app.view.level = level;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("level %d: %dx%d, %s", level, li.w, li.h,
                                      byteSizeText(li.imageBytes).c_str());
                ImGui::EndGroup();
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
}

}  // namespace ktxcmp::ui
