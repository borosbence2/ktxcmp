#pragma once

// Application state. Constructed in main and passed by reference; there are no
// singletons (CLAUDE.md, Conventions).

#include "container/KtxFile.hpp"
#include "core/Error.hpp"

#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

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

// One loaded file, or one failed attempt at loading a file. A failure keeps its
// message: an empty slot and a rejected slot are not the same state.
struct SlotState {
    std::filesystem::path path;
    std::optional<KtxFile> ktx;
    std::optional<Error> error;

    [[nodiscard]] bool loaded() const { return ktx.has_value(); }
    [[nodiscard]] bool failed() const { return error.has_value(); }
    void clear() {
        path.clear();
        ktx.reset();
        error.reset();
    }
};

class AppState {
public:
    SlotState slotA;
    SlotState slotB;
    ViewState view;

    float uiScale = 1.0f;  // display content scale, applied to layout constants
    bool  running = true;
    bool  resetLayout = false;

    // Set by the UI, acted on by main: keeps SDL out of the panel code.
    bool openDialogRequested = false;

    // Callable from any thread. SDL's dialog callback is not promised to run on
    // the main one, and a drop event does run on it, so both go through here and
    // are drained where loading is safe.
    void enqueueOpen(std::filesystem::path path);
    void processPendingOpens();

    void loadIntoSlot(Slot slot, const std::filesystem::path& path);

    [[nodiscard]] SlotState& slot(Slot which) { return which == Slot::A ? slotA : slotB; }
    [[nodiscard]] const SlotState& slot(Slot which) const {
        return which == Slot::A ? slotA : slotB;
    }

private:
    std::mutex m_pendingMutex;
    std::vector<std::filesystem::path> m_pending;
};

}  // namespace ktxcmp
