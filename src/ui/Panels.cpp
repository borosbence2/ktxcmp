// The four dock panels.
//
// M1: the source cards, the format banner and the mip strip read real container
// metadata. Nothing here decodes a texel, so the viewport is still empty.

#include "ui/Ui.hpp"

#include "app/AppState.hpp"
#include "compare/ChainAnalysis.hpp"
#include "compare/CompareEngine.hpp"
#include "compare/NormalMap.hpp"
#include "container/KtxFile.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <memory>
#include <vector>
#include <cmath>
#include <cstdio>
#include <string>

namespace ktxcmp::ui {
namespace {

// Defined with the metrics panel; the viewport needs it for the inspector.
bool normalModeActive(const AppState& app);

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
    // An explicit chain replaces the single reference entirely.
    if (slot.hasChain()) {
        ImGui::TextWrapped("%s", slot.chainSource.c_str());
        ImGui::Separator();

        std::size_t matched = 0;
        for (const SurfacePtr& s : slot.chain)
            if (s)
                ++matched;
        field("format", "PNG set");
        field("matched", (std::to_string(matched) + "/" + std::to_string(slot.chain.size())).c_str());
        field("chain", "explicit");

        // Unmatched levels are stated, never filled in from somewhere else
        // (PLAN.md M7).
        if (!slot.chainProblems.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
            for (const std::string& problem : slot.chainProblems)
                ImGui::TextWrapped("%s", problem.c_str());
            ImGui::PopStyleColor();
        }
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
    field("chain", "generated");

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
    ui.hoverNormalMode = false;
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

                // In normal-map mode the readout is directions and the angle
                // between them; 8-bit ints would be meaningless here.
                ui.hoverNormalMode = normalModeActive(app);
                if (ui.hoverNormalMode) {
                    const bool isSigned = app.slotA.ktx->info().format.isSigned;
                    auto unpackNormal = [isSigned](const float* p, float out[3]) {
                        const double x = isSigned ? p[0] : (2.0 * p[0] - 1.0);
                        const double y = isSigned ? p[1] : (2.0 * p[1] - 1.0);
                        const double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
                        const double len = std::sqrt(x * x + y * y + z * z);
                        const double inv = len > 0.0 ? 1.0 / len : 0.0;
                        out[0] = static_cast<float>(x * inv);
                        out[1] = static_cast<float>(y * inv);
                        out[2] = static_cast<float>(z * inv);
                    };
                    unpackNormal(v, ui.hoverNormalA);
                    if (ui.hasHoverB) {
                        unpackNormal(reference->at(tx, ty), ui.hoverNormalB);
                        const double dot = std::clamp(
                            static_cast<double>(ui.hoverNormalA[0]) * ui.hoverNormalB[0] +
                                static_cast<double>(ui.hoverNormalA[1]) * ui.hoverNormalB[1] +
                                static_cast<double>(ui.hoverNormalA[2]) * ui.hoverNormalB[2],
                            -1.0, 1.0);
                        ui.hoverAngleDeg = static_cast<float>(std::acos(dot) * 57.29577951308232);
                    }
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

void drawFormatBanner(AppState& app) {
    // Accent, not warning: successful auto-detection is normal operation.
    const ImVec4 accent = accentColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(accent.x, accent.y, accent.z, 0.22f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(accent.x, accent.y, accent.z, 0.70f));
    if (beginCard("##formatBanner")) {
        if (app.slotA.loaded()) {
            const KtxInfo& info = app.slotA.ktx->info();
            ImGui::TextWrapped("%s", info.formatName.c_str());

            const bool detectable = looksLikeNormalMap(info.format);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (detectable && !app.view.rawRgOverride)
                ImGui::TextWrapped("normal map, z reconstructed");
            else if (detectable)
                ImGui::TextWrapped("raw RG (override)");
            else
                ImGui::TextWrapped("%s, %s", info.version == ContainerVersion::Ktx1 ? "KTX1" : "KTX2",
                                   info.hasDfd ? "DFD present" : "no DFD");
            ImGui::PopStyleColor();

            // The override only means anything where a detection was made.
            if (detectable) {
                bool raw = app.view.rawRgOverride;
                if (ImGui::Checkbox("raw RG", &raw))
                    app.view.rawRgOverride = raw;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Read the two channels as plain data instead of a normal "
                                      "map. Reports PSNR rather than angular error.");
            }
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

    ImGui::Checkbox("Linear light", &v.resampleLinearLight);
}

// Whether the current texture is being read as a normal map. BC5 selects it by
// default; the banner offers the override (PLAN.md M6).
bool normalModeActive(const AppState& app) {
    if (!app.slotA.loaded())
        return false;
    if (app.view.rawRgOverride)
        return false;
    return looksLikeNormalMap(app.slotA.ktx->info().format);
}

// Hero number, then the supporting rows. In normal-map mode the whole set
// changes together - hero, rows and plot axis - because degrees and decibels
// are not interchangeable and a half-swapped panel would invite reading one as
// the other (CLAUDE.md, PLAN.md M6).
void drawMetrics(AppState& app) {
    const bool normalMode = normalModeActive(app);
    const char* heroLabel = normalMode ? "mean angular err" : "PSNR-RGB";

    std::string hero = kEmptyValue;
    std::string rowA = kEmptyValue, rowB = kEmptyValue, rowC = kEmptyValue, rowD = kEmptyValue;
    const char* labelA = normalMode ? "median" : "RMSE";
    const char* labelB = normalMode ? "p95" : "SSIM";
    const char* labelC = normalMode ? "max" : "max err";
    const char* labelD = normalMode ? "|n| dev" : "alpha";

    std::string note;
    bool isError = false;
    bool waiting = false;

    if (app.view.compareMode != CompareMode::EncodeFidelity) {
        note = "per-level numbers are in the plot and table";
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
            const MetricsResult& m = **result;
            char buf[80];
            if (m.normalMode) {
                std::snprintf(buf, sizeof(buf), "%.3f deg", m.normal.meanAngleDeg);
                hero = buf;
                std::snprintf(buf, sizeof(buf), "%.3f", m.normal.medianAngleDeg);
                rowA = buf;
                std::snprintf(buf, sizeof(buf), "%.3f", m.normal.p95AngleDeg);
                rowB = buf;
                std::snprintf(buf, sizeof(buf), "%.2f @%d,%d", m.normal.maxAngleDeg,
                              m.normal.maxAngleX, m.normal.maxAngleY);
                rowC = buf;
                std::snprintf(buf, sizeof(buf), "%.4f", m.normal.meanLengthDeviation);
                rowD = buf;
                if (m.normal.excludedNonFinite > 0)
                    note = std::to_string(m.normal.excludedNonFinite) + " texels excluded";
            } else {
                const CompareResult& c = m.colour;
                hero = formatPsnr(c.rgb.psnr);
                std::snprintf(buf, sizeof(buf), "%.3f", c.rgb.rmse);
                rowA = buf;
                std::snprintf(buf, sizeof(buf), "%.4f", c.ssim);
                rowB = buf;
                std::snprintf(buf, sizeof(buf), "%.1f @%d,%d", c.rgb.maxError, c.rgb.maxErrorX,
                              c.rgb.maxErrorY);
                rowC = buf;
                rowD = c.alpha.identical ? "identical" : formatPsnr(c.alpha.psnr);
                if (c.excludedNonFinite > 0)
                    note = std::to_string(c.excludedNonFinite) + " texels excluded (NaN/Inf)";
                // The toggle must be labelled wherever the numbers appear.
                if (c.linearLight)
                    note = note.empty() ? "linear light" : note + ", linear light";
            }
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

    field(labelA, rowA.c_str());
    field(labelB, rowB.c_str());
    field(labelC, rowC.c_str());
    field(labelD, rowD.c_str());

    if (!note.empty()) {
        if (isError)
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
        ImGui::TextWrapped("%s", note.c_str());
        if (isError)
            ImGui::PopStyleColor();
    }
}

// The chain report for the current settings, or nothing while it is still being
// computed. Shared by the plot, the table and the strip badges so all three
// agree about what they are showing.
const ChainReport* currentChainReport(AppState& app) {
    if (!app.chainAvailable())
        return nullptr;
    static ChainReport held;
    auto report = app.chainAnalyser.result(app.chainToken());
    if (!report || !*report)
        return nullptr;
    held = **report;
    return &held;
}

// Error by level. You scan this; you do not read fifteen rows of numbers, which
// is why the table is behind a toggle (PLAN.md, right rail).
void drawErrorPlot(AppState& app, const ChainReport* report) {
    // The axis changes with the metric family, so the label has to as well:
    // degrees and decibels run in opposite directions.
    const bool normalMode = report != nullptr && report->normalMode;
    sectionLabel(normalMode ? "Angular error by level (deg)" : "PSNR by level (dB)");

    ImGui::PushStyleColor(ImGuiCol_ChildBg, viewportBgColor());
    ImGui::BeginChild("##errorPlot", ImVec2(0.0f, 132.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar);

    if (app.view.compareMode == CompareMode::EncodeFidelity) {
        centeredHint("mode 1 is one level");
    } else if (report == nullptr) {
        pendingHint("analysing");
    } else {
        struct Point {
            int level;
            float psnr;
        };
        std::vector<Point> points;
        float lo = 1e9f, hi = -1e9f;
        for (const LevelStats& level : report->levels) {
            float v = 0.0f;
            if (normalMode) {
                if (!level.hasNormalMetrics)
                    continue;
                v = static_cast<float>(level.normal.meanAngleDeg);
            } else {
                if (!level.hasMetrics || std::isinf(level.metrics.rgb.psnr))
                    continue;
                v = static_cast<float>(level.metrics.rgb.psnr);
            }
            points.push_back(Point{level.level, v});
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }

        if (points.empty()) {
            centeredHint("no comparable levels");
        } else {
            if (hi - lo < 1.0f) {
                lo -= 1.0f;
                hi += 1.0f;
            }
            const float pad = 6.0f;
            const ImVec2 origin = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            const float plotX0 = origin.x + pad + 24.0f;  // room for the axis labels
            const float plotX1 = origin.x + size.x - pad;
            const float plotY0 = origin.y + pad;
            const float plotY1 = origin.y + size.y - pad;
            ImDrawList* draw = ImGui::GetWindowDrawList();

            const ImU32 axis = ImGui::GetColorU32(ImGuiCol_Separator);
            draw->AddLine(ImVec2(plotX0, plotY1), ImVec2(plotX1, plotY1), axis);
            draw->AddLine(ImVec2(plotX0, plotY0), ImVec2(plotX0, plotY1), axis);

            char label[32];
            const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            std::snprintf(label, sizeof(label), "%.0f", static_cast<double>(hi));
            draw->AddText(ImVec2(origin.x + pad, plotY0 - 2.0f), dim, label);
            std::snprintf(label, sizeof(label), "%.0f", static_cast<double>(lo));
            draw->AddText(ImVec2(origin.x + pad, plotY1 - 13.0f), dim, label);

            const int maxLevel =
                report->levels.empty() ? 1 : static_cast<int>(report->levels.size()) - 1;
            auto plotPos = [&](const Point& p) {
                const float tx =
                    maxLevel > 0 ? static_cast<float>(p.level) / static_cast<float>(maxLevel)
                                 : 0.5f;
                const float ty = (p.psnr - lo) / (hi - lo);
                return ImVec2(plotX0 + tx * (plotX1 - plotX0), plotY1 - ty * (plotY1 - plotY0));
            };

            // The badge threshold, drawn so a spike is read against something.
            // It only applies to the PSNR axis; degrees have their own scale.
            if (!normalMode && app.errorBadgeThresholdDb > lo && app.errorBadgeThresholdDb < hi) {
                const float ty = (app.errorBadgeThresholdDb - lo) / (hi - lo);
                const float y = plotY1 - ty * (plotY1 - plotY0);
                draw->AddLine(ImVec2(plotX0, y), ImVec2(plotX1, y),
                              ImGui::GetColorU32(errorColor()), 1.0f);
            }

            const ImU32 lineColour = ImGui::GetColorU32(accentColor());
            for (std::size_t i = 1; i < points.size(); ++i)
                draw->AddLine(plotPos(points[i - 1]), plotPos(points[i]), lineColour, 1.5f);

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const bool hovered = ImGui::IsWindowHovered();
            for (const Point& p : points) {
                const ImVec2 at = plotPos(p);
                const bool selected = p.level == app.view.level;
                const bool nearPoint = hovered && std::fabs(mouse.x - at.x) < 8.0f;
                draw->AddCircleFilled(at, selected ? 4.5f : 3.0f,
                                      selected ? ImGui::GetColorU32(ImGuiCol_Text) : lineColour);
                // Clicking a point selects that level everywhere (PLAN.md M5).
                if (nearPoint && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    app.view.level = p.level;
                if (nearPoint)
                    ImGui::SetTooltip(normalMode ? "level %d: %.3f deg" : "level %d: %.2f dB",
                                      p.level, static_cast<double>(p.psnr));
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// The full table is not permanently on screen; it lives behind this toggle and
// in the CSV export (PLAN.md, right rail).
void drawLevelTable(AppState& app, const ChainReport* report) {
    ImGui::Checkbox("Level table", &app.showLevelTable);
    if (!app.showLevelTable || report == nullptr)
        return;

    if (ImGui::BeginTable("##levels", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        const bool normalMode = report->normalMode;
        ImGui::TableSetupColumn("lvl");
        ImGui::TableSetupColumn(normalMode ? "mean deg" : "PSNR");
        ImGui::TableSetupColumn(normalMode ? "max deg" : "SSIM");
        ImGui::TableHeadersRow();
        for (const LevelStats& level : report->levels) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char id[24];
            std::snprintf(id, sizeof(id), "%d##row%d", level.level, level.level);
            if (ImGui::Selectable(id, app.view.level == level.level,
                                  ImGuiSelectableFlags_SpanAllColumns))
                app.view.level = level.level;
            ImGui::TableNextColumn();
            if (normalMode && level.hasNormalMetrics)
                ImGui::Text("%.4f", level.normal.meanAngleDeg);
            else if (!normalMode && level.hasMetrics)
                ImGui::TextUnformatted(formatPsnr(level.metrics.rgb.psnr).c_str());
            else
                ImGui::TextDisabled("%s", kEmptyValue);
            ImGui::TableNextColumn();
            if (normalMode && level.hasNormalMetrics)
                ImGui::Text("%.4f", level.normal.maxAngleDeg);
            else if (!normalMode && level.hasMetrics)
                ImGui::Text("%.4f", level.metrics.ssim);
            else
                ImGui::TextDisabled("%s", kEmptyValue);
        }
        ImGui::EndTable();
    }

    if (ImGui::Button("Export CSV"))
        app.exportCsvRequested = true;
}

void drawChainWarnings(const ChainReport* report) {
    if (report == nullptr || report->warnings.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, errorColor());
    for (const ChainWarning& w : report->warnings) {
        if (w.level < 0)
            ImGui::TextWrapped("chain: %s", w.message.c_str());
        else
            ImGui::TextWrapped("level %d %s", w.level, w.message.c_str());
    }
    ImGui::PopStyleColor();
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
        const ChainReport* report = currentChainReport(app);
        drawErrorPlot(app, report);
        ImGui::Spacing();
        drawLevelTable(app, report);
        drawChainWarnings(report);
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

        // Warnings live on the thumbnails, not in a separate problems panel: the
        // strip is already the navigation, so one glance shows which level to
        // look at (PLAN.md, bottom).
        const ChainReport* report = currentChainReport(app);

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

            // A badge if this level was flagged, or if its error crossed the
            // threshold. One mark, whichever the reason; the tooltip says which.
            std::string badgeReason;
            if (report != nullptr) {
                for (const ChainWarning& w : report->warnings)
                    if (w.level == level)
                        badgeReason += (badgeReason.empty() ? "" : "\n") + w.message;
                const std::size_t index = static_cast<std::size_t>(level);
                if (index < report->levels.size()) {
                    const LevelStats& stats = report->levels[index];
                    if (stats.hasMetrics && !std::isinf(stats.metrics.rgb.psnr) &&
                        stats.metrics.rgb.psnr < app.errorBadgeThresholdDb) {
                        char msg[96];
                        std::snprintf(msg, sizeof(msg), "PSNR %.2f dB is below the %.0f dB threshold",
                                      stats.metrics.rgb.psnr,
                                      static_cast<double>(app.errorBadgeThresholdDb));
                        badgeReason += (badgeReason.empty() ? "" : "\n");
                        badgeReason += msg;
                    }
                }
            }
            if (!badgeReason.empty()) {
                const float r = 5.0f * app.uiScale;
                const ImVec2 at(cellPos.x + cellSize.x - r - 4.0f, cellPos.y + r + 4.0f);
                draw->AddCircleFilled(at, r, ImGui::GetColorU32(errorColor()));
            }
            ImGui::EndGroup();

            if (hovered) {
                if (badgeReason.empty())
                    ImGui::SetTooltip("level %d: %dx%d, %s", level, li.w, li.h,
                                      byteSizeText(li.imageBytes).c_str());
                else
                    ImGui::SetTooltip("level %d: %dx%d, %s\n%s", level, li.w, li.h,
                                      byteSizeText(li.imageBytes).c_str(), badgeReason.c_str());
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

}  // namespace ktxcmp::ui
