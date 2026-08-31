#pragma once

// Application state. Constructed in main and passed by reference; there are no
// singletons (CLAUDE.md, Conventions).

#include "cache/ImageCache.hpp"
#include "compare/CompareService.hpp"
#include "container/KtxFile.hpp"
#include "core/Error.hpp"
#include "core/Subresource.hpp"
#include "core/Surface.hpp"
#include "decode/DecodeService.hpp"
#include "decode/PngLoader.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace ktxcmp {

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

    // shared_ptr, not optional: a decode job holds a reference for its lifetime,
    // so loading a new file cannot free bytes a worker is still reading.
    std::shared_ptr<KtxFile> ktx;

    // A PNG reference instead. A slot holds one or the other, never both.
    SurfacePtr reference;
    PngInfo referenceInfo;
    // PNG has no reliable colour-space signal, so this is an assumption the user
    // can override rather than something read from the file (CLAUDE.md, trap 4).
    TransferFn referenceTf = TransferFn::Srgb;

    std::optional<Error> error;

    [[nodiscard]] bool loaded() const { return ktx != nullptr; }
    [[nodiscard]] bool isReference() const { return reference != nullptr; }
    [[nodiscard]] bool occupied() const { return loaded() || isReference(); }
    [[nodiscard]] bool failed() const { return error.has_value(); }
};

class AppState {
public:
    AppState();

    SlotState slotA;
    SlotState slotB;
    ViewState view;

    ImageCache cache;
    DecodeService decoder;
    CompareService comparer;

    float uiScale = 1.0f;  // display content scale, applied to layout constants
    bool  running = true;
    bool  resetLayout = false;

    // Set by the UI, acted on by main: keeps SDL out of the panel code.
    bool openDialogRequested = false;

    // Callable from any thread. SDL's dialog callback is not promised to run on
    // the main one, and a drop event does run on it, so both go through here and
    // are drained where loading is safe.
    // dropX/dropY are in screen pixels, or negative when the file did not come
    // from a drop (a dialog or the command line).
    void enqueueOpen(std::filesystem::path path, float dropX = -1.0f, float dropY = -1.0f);
    void processPendingOpens();

    void loadIntoSlot(Slot slot, const std::filesystem::path& path);
    // Re-reads the reference under a different colour-space assumption.
    void setReferenceTransfer(Slot slot, TransferFn tf);

    // Screen rectangles of the two slot cards, published by the UI each frame so
    // a drop can be routed to the card it landed on.
    struct Rect {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        [[nodiscard]] bool contains(float x, float y) const {
            return x >= x0 && x < x1 && y >= y0 && y < y1;
        }
        [[nodiscard]] bool valid() const { return x1 > x0 && y1 > y0; }
    };
    Rect slotRect[2];

    // Asks the compare service for the current pairing. Mode 1 only for now:
    // KTX mip 0 against the reference, no resampling.
    void requestCompare();
    [[nodiscard]] std::uint64_t compareToken() const { return m_compareToken; }
    [[nodiscard]] bool compareAvailable() const { return m_compareAvailable; }

    // Asks for what the UI is about to draw: the selected subresource first,
    // then every level for the mip strip. Cheap to call every frame - the cache
    // rejects anything already present or in flight.
    void requestVisible();

    [[nodiscard]] SubresourceKey selectionKey(Slot slot) const;
    [[nodiscard]] SlotState& slot(Slot which) { return which == Slot::A ? slotA : slotB; }
    [[nodiscard]] const SlotState& slot(Slot which) const {
        return which == Slot::A ? slotA : slotB;
    }

    // Clamps the selection to what the loaded file actually has.
    void clampSelection();

private:
    struct PendingOpen {
        std::filesystem::path path;
        float x = -1.0f;
        float y = -1.0f;
    };

    std::mutex m_pendingMutex;
    std::vector<PendingOpen> m_pending;
    std::uint64_t m_compareToken = 0;
    bool m_compareAvailable = false;
};

}  // namespace ktxcmp
