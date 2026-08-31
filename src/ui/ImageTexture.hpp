#pragma once

// A GL texture and nothing else. Every byte it uploads was prepared off the UI
// thread by DisplayCache; this only makes GL calls, which have to happen here.

#include "ui/DisplayCache.hpp"

namespace ktxcmp::ui {

class ImageTexture {
public:
    ImageTexture() = default;
    ~ImageTexture();
    ImageTexture(const ImageTexture&) = delete;
    ImageTexture& operator=(const ImageTexture&) = delete;

    // No-op when the texture already holds this image.
    void update(const DisplayImagePtr& image);
    void release();

    [[nodiscard]] bool valid() const { return m_id != 0; }
    [[nodiscard]] unsigned id() const { return m_id; }
    [[nodiscard]] int width() const { return m_w; }
    [[nodiscard]] int height() const { return m_h; }

private:
    unsigned m_id = 0;
    int m_w = 0;
    int m_h = 0;
    DisplayImagePtr m_source;
};

}  // namespace ktxcmp::ui
