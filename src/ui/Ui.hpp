#pragma once

#include <imgui.h>

namespace ktxcmp {
class AppState;
}

namespace ktxcmp::ui {

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
void drawFrame(AppState& app);

// Panels. Each opens and closes its own dock window.
void drawSourcesPanel(AppState& app);
void drawViewportPanel(AppState& app);
void drawAnalysisPanel(AppState& app);
void drawMipStripPanel(AppState& app);
void drawStatusBar(AppState& app);

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
