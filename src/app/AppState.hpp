#pragma once

// Application state. Constructed in main and passed by reference; there are no
// singletons (CLAUDE.md, Conventions).
//
// M0 declares the selection vocabulary the panels need in order to lay
// themselves out. Nothing here is populated from a file yet.

#include <filesystem>

namespace ktxcmp {

enum class Slot { A, B };

// The view-mode row above the viewport.
enum class ViewMode { A, B, Diff, Split, Onion };

// CLAUDE.md, Compare modes.
enum class CompareMode {
    EncodeFidelity,    // 1: KTX mip 0 vs reference, no resampling
    ChainVsReference,  // 2: reference downsampled to each level
    SelfConsistency,   // 3: KTX level N-1 downsampled x2 vs level N
};

// Resampling kernel for compare modes 2 and 3 (PLAN.md M5).
enum class Filter { Box, Triangle, Kaiser, Lanczos3, Mitchell };

struct ChannelMask {
    bool r = true, g = true, b = true, a = true;
};

// The single selection record every panel reads from.
struct ViewState {
    Slot        slot        = Slot::A;
    int         level       = 0;
    int         layer       = 0;
    int         face        = 0;
    ChannelMask channels{};
    ViewMode    viewMode    = ViewMode::A;
    CompareMode compareMode = CompareMode::EncodeFidelity;

    // The one contextual control that changes with view mode.
    int   diffGain   = 1;     // 1x / 4x / 8x / 16x
    float splitWipe  = 0.5f;  // 0..1
    float onionBlend = 0.5f;  // 0..1

    // Modes 2 and 3 only. Both must appear in every table and export.
    Filter filter      = Filter::Lanczos3;
    bool   linearLight = true;
};

// One loaded file. M0 holds no pixels; panels render the empty state.
struct SlotState {
    std::filesystem::path path;
    bool loaded = false;
};

struct AppState {
    SlotState slotA;
    SlotState slotB;
    ViewState view;

    float uiScale = 1.0f;  // display content scale, applied to layout constants
    bool  running = true;
    bool  resetLayout = false;
};

}  // namespace ktxcmp
