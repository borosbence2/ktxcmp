#pragma once

#include "ui/ImageTexture.hpp"

#include <imgui.h>

namespace ktxcmp {
class AppState;
}

namespace ktxcmp::ui {

// UI-owned state that AppState has no business knowing about: a GL texture and
// where the user has panned to. Constructed in main, passed by reference.
struct UiState {
    ImageTexture texture;

    float zoom = 1.0f;
    float panX = 0.0f;
    float panY = 0.0f;
    bool fitRequested = true;

    // The fit follows the viewport until the user zooms or pans, then stops
    // fighting them. Without this the first fit runs before the dock layout has
    // settled and the image ends up cropped against a stale child size.
    bool autoFit = true;
    float fittedW = 0.0f;
    float fittedH = 0.0f;
    int fittedTexW = 0;
    int fittedTexH = 0;

    // Pixel inspector readout, filled by the viewport, read by the status bar.
    bool hasHover = false;
    int hoverX = 0;
    int hoverY = 0;
    float hoverValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

// Dock window titles. The layout builder and the panels must agree on these.
inline constexpr const char* kWinSources  = "Sources";
inline constexpr const char* kWinViewport = "Viewport";
inline constexpr const char* kWinAnalysis = "Analysis";
inline constexpr const char* kWinMipStrip = "Mip Levels";

// Layout constants in logical pixels, scaled by AppState::uiScale.
// PLAN.md: the layout has to hold at 680px wide, so the rails are fixed and
// the viewport takes the slack.
inline constexpr float kLeftRailWidth  = 136.0f;
inline constexpr float kRightRailWidth = 158.0f;
inline constexpr float kMipStripHeight = 112.0f;

void applyTheme(float scale);

// One frame of the entire UI.
void drawFrame(AppState& app, UiState& ui);

// Panels. Each opens and closes its own dock window.
void drawSourcesPanel(AppState& app);
void drawViewportPanel(AppState& app, UiState& ui);
void drawAnalysisPanel(AppState& app);
void drawMipStripPanel(AppState& app);
void drawStatusBar(AppState& app, UiState& ui);

// Shared panel vocabulary.
ImVec4 accentColor();
ImVec4 panelBgColor();
ImVec4 viewportBgColor();
ImVec4 errorColor();

// "label            value", value right-aligned and dimmed when empty.
void field(const char* label, const char* value);

// A boxed section with a title, used for the slot cards and the metric card.
bool beginCard(const char* id, float height = 0.0f);
void endCard();

// Grayscale, not transparency: legible but visibly inactive (PLAN.md, left rail).
void pushGrayscale();
void popGrayscale();

inline constexpr const char* kEmptyValue = "-";

}  // namespace ktxcmp::ui
