#pragma once

// Display-side texture. Owns one GL texture and the RGBA8 pixels behind it.
//
// The RGBA8 conversion here is a display artifact and nothing else reads it.
// The float Surface stays the source of truth for every measurement, so this
// does not violate "RGBA32F is the only intermediate" (CLAUDE.md): that rule is
// about the decode and metric path, not about what reaches a monitor.

#include "app/AppState.hpp"
#include "core/Surface.hpp"

#include <cstdint>
#include <vector>

namespace ktxcmp::ui {

class ImageTexture {
public:
    ImageTexture() = default;
    ~ImageTexture();
    ImageTexture(const ImageTexture&) = delete;
    ImageTexture& operator=(const ImageTexture&) = delete;

    // Rebuilds only when the surface or the channel selection actually changed.
    void update(const Surface& surface, const ChannelMask& channels);
    void release();

    [[nodiscard]] bool valid() const { return m_id != 0; }
    [[nodiscard]] unsigned id() const { return m_id; }
    [[nodiscard]] int width() const { return m_w; }
    [[nodiscard]] int height() const { return m_h; }

private:
    void upload(const std::vector<std::uint8_t>& base);

    unsigned m_id = 0;
    int m_w = 0;
    int m_h = 0;
    const Surface* m_source = nullptr;  // identity only, never dereferenced stale
    std::uint32_t m_channelKey = 0xFFFFFFFFu;
    std::uint64_t m_revision = 0;
};

}  // namespace ktxcmp::ui
