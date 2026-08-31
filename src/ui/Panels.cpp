// The four dock panels.
//
// M1: the source cards, the format banner and the mip strip read real container
// metadata. Nothing here decodes a texel, so the viewport is still empty.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"
#include "compare/CompareEngine.hpp"
#include "container/KtxFile.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <memory>
#include <cmath>
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

void drawSlotA(AppState& app) {
    sectionLabel("SLOT A");
    if (beginCard("##slotA"))
        drawSlotBody(app.slotA);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    endCard();
    app.slotRect[0] = AppState::Rect{min.x, min.y, max.x, max.y};
}

void drawReferenceBody(AppState& app, SlotState& slot) {
    if (slot.failed()) {
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        ImGui::TextWrapped("%s", categoryName(slot.error->code));
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", slot.error->message.c_str());
        return;
    }
    if (!slot.isReference()) {
        ImGui::TextDisabled("no file");
        ImGui::Separator();
        field("format", kEmptyValue);
        field("size", kEmptyValue);
        field("chain", kEmptyValue);
        return;
    }

    ImGui::TextWrapped("%s", slot.path.filename().string().c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", slot.path.string().c_str());
    ImGui::Separator();

    char fmt[32];
    std::snprintf(fmt, sizeof(fmt), "PNG %d-bit", slot.referenceInfo.bitDepth);
    field("format", fmt);
    field("size", dimsText(slot.reference->w, slot.reference->h).c_str());
    field("chain", "explicit");

    // PNG carries no reliable colour-space signal, so this is an assumption on
    // display, not a fact read from the file (CLAUDE.md, trap 4).
    bool srgb = slot.referenceTf == TransferFn::Srgb;
    if (ImGui::Checkbox("assume sRGB", &srgb))
        app.setReferenceTransfer(Slot::B, srgb ? TransferFn::Srgb : TransferFn::Linear);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("PNG has no dependable colour-space tag. gAMA is never consulted.");
}

void drawSlotB(AppState& app, bool unused) {
    if (unused)
        pushGrayscale();

    sectionLabel("SLOT B");
    if (unused) {
        ImGui::SameLine();
        const char* tag = "not used";
        alignRight(ImGui::CalcTextSize(tag).x);
        ImGui::TextDisabled("%s", tag);
    }

    if (beginCard("##slotB"))
        drawReferenceBody(app, app.slotB);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    endCard();
    app.slotRect[1] = AppState::Rect{min.x, min.y, max.x, max.y};

    if (unused)
        popGrayscale();
}

void drawDropTarget() {
    if (beginCard("##drop", ImGui::GetFrameHeight() * 2.2f)) {
        centeredHint("Drop .ktx / .ktx2");
    }
    endCard();
}

std::uint32_t channelKeyOf(const ChannelMask& c) {
    return (c.r ? 1u : 0u) | (c.g ? 2u : 0u) | (c.b ? 4u : 0u) | (c.a ? 8u : 0u);
}

// Drops every built texture when the channel mask changes, so the next frames
// rebuild them from the display cache rather than showing the old isolation.
void syncChannelGeneration(AppState& app, UiState& ui) {
    const std::uint32_t key = channelKeyOf(app.view.channels);
    if (key == ui.builtChannels)
        return;
    ui.builtChannels = key;
    for (auto& t : ui.levelTextures)
        if (t)
            t->release();
    for (auto& t : ui.thumbnails)
        if (t)
            t->release();
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

// Wheel zooms about the cursor, middle drag or space+drag pans. Filtering is a
// texture parameter: nearest when magnified, box-filtered mips when minified
// (PLAN.md M2), so GL picks the right one from the actual scale.
void drawViewportBody(AppState& app, UiState& ui, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, viewportBgColor());
    ImGui::BeginChild("##viewportBody", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    SlotState& slot = app.slotA;
    const SubresourceKey key = app.selectionKey(Slot::A);
    const CacheState cacheState = slot.loaded() ? app.cache.state(key) : CacheState::Missing;
    SurfacePtr held = app.cache.get(key);
    const Surface* surface = held.get();

    // Whichever image this view mode is actually showing.
    ImageTexture* texture = nullptr;
    const SurfacePtr reference = app.slotB.reference;
    const bool sizesMatch =
        held && reference && held->w == reference->w && held->h == reference->h;

    if (slot.loaded()) {
        const int levels = slot.ktx->info().levelCount;
        ui.levelTextures.resize(static_cast<std::size_t>(levels));
        const std::size_t index = static_cast<std::size_t>(app.view.level);
        if (index < ui.levelTextures.size()) {
            auto& entry = ui.levelTextures[index];
            if (!entry)
                entry = std::make_unique<ImageTexture>();
            const DisplayKey dk{key, ui.builtChannels, 0, 0};
            if (held)
                ui.display.request(dk, held, 0);
            if (auto image = ui.display.get(dk))
                entry->update(image);
            texture = entry.get();
        }
    }

    if (reference) {
        const SubresourceKey refKey{Slot::B, 0, 0, 0};
        const DisplayKey dk{refKey, ui.builtChannels, 0, 0};
        ui.display.request(dk, reference, 0);
        if (auto image = ui.display.get(dk))
            ui.referenceTexture.update(image);
    } else {
        ui.referenceTexture.release();
    }

    if (app.view.viewMode == ViewMode::Diff && sizesMatch) {
        const DisplayKey dk{key, ui.builtChannels, 0, app.view.diffGain};
        ui.display.request(dk, held, 0, reference);
        if (auto image = ui.display.get(dk))
            ui.diffTexture.update(image);
    }

    switch (app.view.viewMode) {
        case ViewMode::B:
            texture = ui.referenceTexture.valid() ? &ui.referenceTexture : nullptr;
            break;
        case ViewMode::Diff:
            texture = (sizesMatch && ui.diffTexture.valid()) ? &ui.diffTexture : nullptr;
            break;
        default:
            break;  // A, Split and Onion draw slot A as the base
    }
    const bool haveTexture = texture != nullptr && texture->valid();

    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const bool haveImage = haveTexture;
    const float imgW = haveImage ? static_cast<float>(texture->width()) : 1.0f;
    const float imgH = haveImage ? static_cast<float>(texture->height()) : 1.0f;

    const bool sizeChanged =
        std::fabs(size.x - ui.fittedW) > 0.5f || std::fabs(size.y - ui.fittedH) > 0.5f;
    const bool imageChanged = haveImage && (texture->width() != ui.fittedTexW ||
                                            texture->height() != ui.fittedTexH);
    if (haveImage && size.x > 1.0f && size.y > 1.0f &&
        (ui.fitRequested || (ui.autoFit && (sizeChanged || imageChanged)))) {
        const float fit = std::min(size.x / imgW, size.y / imgH);
        ui.zoom = fit > 0.0f ? fit : 1.0f;
        ui.panX = (size.x - imgW * ui.zoom) * 0.5f;
        ui.panY = (size.y - imgH * ui.zoom) * 0.5f;
        ui.fitRequested = false;
        ui.autoFit = true;
        ui.fittedW = size.x;
        ui.fittedH = size.y;
        ui.fittedTexW = texture->width();
        ui.fittedTexH = texture->height();
    }

    const bool hovered = ImGui::IsWindowHovered();
    if (haveImage && hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            // Keep the texel under the cursor under the cursor.
            const float factor = io.MouseWheel > 0.0f ? 1.25f : 1.0f / 1.25f;
            const float newZoom = std::clamp(ui.zoom * factor, 0.01f, 256.0f);
            const float cx = io.MousePos.x - origin.x - ui.panX;
            const float cy = io.MousePos.y - origin.y - ui.panY;
            ui.panX += cx - cx * (newZoom / ui.zoom);
            ui.panY += cy - cy * (newZoom / ui.zoom);
            ui.zoom = newZoom;
            ui.autoFit = false;  // the user is driving now
        }
        const bool spaceDrag = ImGui::IsKeyDown(ImGuiKey_Space) &&
                               ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || spaceDrag) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            ui.panX += delta.x;
            ui.panY += delta.y;
            ui.autoFit = false;
        }
    }

    ui.hasHover = false;
    ui.hasHoverB = false;
    if (haveImage) {
        const ImVec2 topLeft(origin.x + ui.panX, origin.y + ui.panY);
        ImGui::SetCursorScreenPos(topLeft);
        ImGui::Image(static_cast<ImTextureID>(texture->id()),
                     ImVec2(imgW * ui.zoom, imgH * ui.zoom));

        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Pixel grid, once a texel is big enough for the line not to swamp it.
        if (ui.zoom >= 8.0f) {
            const ImU32 line = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.18f));
            const float x0 = std::max(topLeft.x, origin.x);
            const float x1 = std::min(topLeft.x + imgW * ui.zoom, origin.x + size.x);
            const float y0 = std::max(topLeft.y, origin.y);
            const float y1 = std::min(topLeft.y + imgH * ui.zoom, origin.y + size.y);
            const int firstX = static_cast<int>(std::max(0.0f, (origin.x - topLeft.x) / ui.zoom));
            const int firstY = static_cast<int>(std::max(0.0f, (origin.y - topLeft.y) / ui.zoom));
            for (int x = firstX; x <= texture->width(); ++x) {
                const float px = topLeft.x + static_cast<float>(x) * ui.zoom;
                if (px > x1)
                    break;
                if (px >= x0)
                    draw->AddLine(ImVec2(px, y0), ImVec2(px, y1), line);
            }
            for (int y = firstY; y <= texture->height(); ++y) {
                const float py = topLeft.y + static_cast<float>(y) * ui.zoom;
                if (py > y1)
                    break;
                if (py >= y0)
                    draw->AddLine(ImVec2(x0, py), ImVec2(x1, py), line);
            }
        }

        if (hovered && surface) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const int tx = static_cast<int>(std::floor((mouse.x - topLeft.x) / ui.zoom));
            const int ty = static_cast<int>(std::floor((mouse.y - topLeft.y) / ui.zoom));
            if (tx >= 0 && ty >= 0 && tx < surface->w && ty < surface->h) {
                ui.hasHover = true;
                ui.hoverX = tx;
                ui.hoverY = ty;
                const float* v = surface->at(tx, ty);
                for (int c = 0; c < 4; ++c)
                    ui.hoverValue[c] = v[c];

                ui.hasHoverB = false;
                if (reference && tx < reference->w && ty < reference->h) {
                    const float* vb = reference->at(tx, ty);
                    for (int c = 0; c < 4; ++c)
                        ui.hoverValueB[c] = vb[c];
                    ui.hasHoverB = true;
                }
            }
        }
    } else if (app.view.viewMode == ViewMode::Diff && held && !sizesMatch) {
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        centeredHint(reference ? "reference dimensions do not match mip 0"
                               : "Diff needs a reference in slot B");
        ImGui::PopStyleColor();
    } else if (app.view.viewMode == ViewMode::B && !reference) {
        centeredHint("no reference in slot B");
    } else if (cacheState == CacheState::Failed) {
        const auto err = app.cache.error(key);
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        centeredHint(err ? err->message.c_str() : "decode failed");
        ImGui::PopStyleColor();
    } else if (cacheState == CacheState::Pending || (slot.loaded() && !surface)) {
        pendingHint("decoding");
    } else {
        centeredHint("No image loaded");
    }

    const float pad = ImGui::GetStyle().FramePadding.x + 2.0f;
    std::string dims = kEmptyValue;
    if (surface)
        dims = dimsText(surface->w, surface->h);

    char overlay[128];
    std::snprintf(overlay, sizeof(overlay), "%s | mip %d | %s",
                  viewModeName(app.view.viewMode), app.view.level, dims.c_str());
    ImGui::SetCursorScreenPos(ImVec2(origin.x + pad, origin.y + pad));
    ImGui::TextDisabled("%s", overlay);

    char zoom[32];
    std::snprintf(zoom, sizeof(zoom), "%.0f%%", ui.zoom * 100.0f);
    const ImVec2 zoomSize = ImGui::CalcTextSize(zoom);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + size.x - zoomSize.x - pad,
                                     origin.y + size.y - zoomSize.y - pad));
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

// Hero number, then the supporting rows. The full per-level table lives behind
// the CSV export, not permanently on screen.
void drawMetrics(AppState& app) {
    const bool bcNormal = false;  // BC5 normal-map metrics arrive at M6
    const char* heroLabel = bcNormal ? "mean angular err" : "PSNR-RGB";

    std::string hero = kEmptyValue;
    std::string rmse = kEmptyValue, ssim = kEmptyValue, maxErr = kEmptyValue, alpha = kEmptyValue;
    std::string note;
    bool isError = false;
    bool waiting = false;

    if (app.view.compareMode != CompareMode::EncodeFidelity) {
        note = "modes 2 and 3 arrive at M5";
    } else if (!app.slotA.loaded()) {
        note = "no texture in slot A";
    } else if (!app.slotB.isReference()) {
        note = "drop a PNG reference on slot B";
    } else if (!app.compareAvailable()) {
        waiting = true;
    } else if (auto result = app.comparer.result(app.compareToken())) {
        if (!*result) {
            note = result->error().message;
            isError = true;
        } else {
            const CompareResult& r = **result;
            hero = formatPsnr(r.rgb.psnr);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.3f", r.rgb.rmse);
            rmse = buf;
            std::snprintf(buf, sizeof(buf), "%.4f", r.ssim);
            ssim = buf;
            std::snprintf(buf, sizeof(buf), "%.1f @%d,%d", r.rgb.maxError, r.rgb.maxErrorX,
                          r.rgb.maxErrorY);
            maxErr = buf;
            alpha = r.alpha.identical ? "identical" : formatPsnr(r.alpha.psnr);
            if (r.excludedNonFinite > 0)
                note = std::to_string(r.excludedNonFinite) + " texels excluded (NaN/Inf)";
            // The toggle must be labelled wherever the numbers appear.
            if (r.linearLight)
                note = note.empty() ? "linear light" : note + ", linear light";
        }
    } else {
        waiting = true;
    }

    if (beginCard("##hero")) {
        ImGui::TextDisabled("%s", heroLabel);
        if (waiting) {
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            drawPendingIndicator(ImVec2(pos.x + 14.0f, pos.y + 14.0f), 10.0f);
            ImGui::Dummy(ImVec2(0.0f, 28.0f));
        } else {
            const ImGuiStyle& style = ImGui::GetStyle();
            const float base = style.FontSizeBase > 0.0f ? style.FontSizeBase : ImGui::GetFontSize();
            ImGui::PushFont(nullptr, base * 1.9f);
            ImGui::TextUnformatted(hero.c_str());
            ImGui::PopFont();
        }
    }
    endCard();

    field("RMSE", rmse.c_str());
    field("SSIM", ssim.c_str());
    field("max err", maxErr.c_str());
    field("alpha", alpha.c_str());

    if (!note.empty()) {
        if (isError)
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        ImGui::TextWrapped("%s", note.c_str());
        if (isError)
            ImGui::PopStyleColor();
    }
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
        drawSlotA(app);
        ImGui::Spacing();
        drawSlotB(app, app.view.compareMode == CompareMode::SelfConsistency);
        ImGui::Spacing();
        drawDropTarget();
    }
    ImGui::End();
}

void drawViewportPanel(AppState& app, UiState& ui) {
    syncChannelGeneration(app, ui);
    if (ImGui::Begin(kWinViewport)) {
        drawViewModeRow(app);

        const float controlRow = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        const float body = ImGui::GetContentRegionAvail().y - controlRow;
        drawViewportBody(app, ui, body > 0.0f ? body : 0.0f);

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
        drawMetrics(app);
        ImGui::Spacing();
        drawErrorPlot();
    }
    ImGui::End();
}

namespace {

void drawSubresourceSelectors(AppState& app) {
    if (!app.slotA.loaded())
        return;
    const KtxInfo& info = app.slotA.ktx->info();
    if (info.layerCount <= 1 && info.faceCount <= 1 && info.baseDepth <= 1)
        return;  // nothing to choose between, so nothing to show

    const float width = 92.0f * app.uiScale;
    if (info.layerCount > 1) {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderInt("layer", &app.view.layer, 0, info.layerCount - 1))
            app.clampSelection();
        ImGui::SameLine(0.0f, 14.0f);
    }
    if (info.faceCount > 1) {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderInt("face", &app.view.face, 0, info.faceCount - 1))
            app.clampSelection();
        ImGui::SameLine(0.0f, 14.0f);
    }
    if (info.baseDepth > 1) {
        // Depth slices live inside a level, so they are a view of the decoded
        // surface rather than a separate subresource.
        ImGui::TextDisabled("depth %d", info.baseDepth);
        ImGui::SameLine(0.0f, 14.0f);
    }
    ImGui::NewLine();
}

}  // namespace

void drawMipStripPanel(AppState& app, UiState& ui) {
    if (ImGui::Begin(kWinMipStrip)) {
        if (!app.slotA.loaded()) {
            if (beginCard("##mipStrip"))
                centeredHint("No mip levels");
            endCard();
            ImGui::End();
            return;
        }

        drawSubresourceSelectors(app);

        const KtxInfo& info = app.slotA.ktx->info();
        const int levels = info.levelCount;
        ui.thumbnails.resize(static_cast<std::size_t>(levels));

        const float cellW = 84.0f * app.uiScale;
        const float cellH = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize;
        ImGui::BeginChild("##mipStrip", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Building a thumbnail means a full RGBA8 conversion plus a mip chain, so
        // only a couple are uploaded per frame. They arrive progressively anyway.
        int builtThisFrame = 0;

        for (int level = 0; level < levels; ++level) {
            const LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
            if (level > 0)
                ImGui::SameLine();

            const SubresourceKey key{Slot::A, level, app.view.layer, app.view.face};
            const CacheState st = app.cache.state(key);

            auto& slotTex = ui.thumbnails[static_cast<std::size_t>(level)];
            if (st == CacheState::Ready) {
                if (!slotTex)
                    slotTex = std::make_unique<ImageTexture>();
                const DisplayKey dk{key, ui.builtChannels, static_cast<int>(96.0f * app.uiScale)};
                if (!slotTex->valid())
                    ui.display.request(dk, app.cache.get(key), 1);
                // One upload per frame: the conversion is off-thread but the GL
                // call is not, and twelve at once is a visible hitch.
                if (!slotTex->valid() && builtThisFrame < 1) {
                    if (auto image = ui.display.get(dk)) {
                        slotTex->update(image);
                        ++builtThisFrame;
                    }
                }
            }

            const ImVec2 cellSize(cellW, cellH > 0.0f ? cellH : 0.0f);
            const ImVec2 cellPos = ImGui::GetCursorScreenPos();

            ImGui::BeginGroup();
            char id[32];
            std::snprintf(id, sizeof(id), "##lvl%d", level);
            if (toggleButton(id, app.view.level == level, cellSize))
                app.view.level = level;
            const bool hovered = ImGui::IsItemHovered();

            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float pad = 4.0f * app.uiScale;

            char caption[48];
            std::snprintf(caption, sizeof(caption), "%d  %dx%d", level, li.w, li.h);
            const ImVec2 capSize = ImGui::CalcTextSize(caption);
            const float capY = cellPos.y + cellSize.y - capSize.y - pad;

            if (slotTex && slotTex->valid()) {
                const float availH = capY - cellPos.y - pad * 2.0f;
                const float availW = cellSize.x - pad * 2.0f;
                const float tw = static_cast<float>(slotTex->width());
                const float th = static_cast<float>(slotTex->height());
                const float scale = std::min(availW / tw, availH / th);
                const ImVec2 drawSize(tw * scale, th * scale);
                const ImVec2 at(cellPos.x + (cellSize.x - drawSize.x) * 0.5f,
                                cellPos.y + pad + (availH - drawSize.y) * 0.5f);
                draw->AddImage(static_cast<ImTextureID>(slotTex->id()), at,
                               ImVec2(at.x + drawSize.x, at.y + drawSize.y));
            } else if (st == CacheState::Failed) {
                ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
                const ImVec2 t = ImGui::CalcTextSize("failed");
                draw->AddText(ImVec2(cellPos.x + (cellSize.x - t.x) * 0.5f,
                                     cellPos.y + cellSize.y * 0.4f),
                              ImGui::GetColorU32(errorColor()), "failed");
                ImGui::PopStyleColor();
            } else {
                drawPendingIndicator(
                    ImVec2(cellPos.x + cellSize.x * 0.5f, cellPos.y + cellSize.y * 0.4f),
                    9.0f * app.uiScale);
            }

            draw->AddText(ImVec2(cellPos.x + (cellSize.x - capSize.x) * 0.5f, capY),
                          ImGui::GetColorU32(ImGuiCol_Text), caption);
            ImGui::EndGroup();

            if (hovered)
                ImGui::SetTooltip("level %d: %dx%d, %s", level, li.w, li.h,
                                  byteSizeText(li.imageBytes).c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

}  // namespace ktxcmp::ui
