#include "ui/ImageTexture.hpp"

#include <SDL3/SDL_opengl.h>

#include <cmath>

// GL 1.2 enum values. The functions we call are all GL 1.1, which is what
// opengl32 exports directly, but these constants are not in the 1.1 header.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

namespace ktxcmp::ui {
namespace {

std::uint32_t channelKey(const ChannelMask& c) {
    return (c.r ? 1u : 0u) | (c.g ? 2u : 0u) | (c.b ? 4u : 0u) | (c.a ? 8u : 0u);
}

std::uint8_t toByte(float v) {
    if (!(v > 0.0f))  // also catches NaN
        return 0;
    if (v >= 1.0f)
        return 255;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

float linearToSrgb(float v) {
    if (!(v > 0.0f))
        return 0.0f;
    if (v <= 0.0031308f)
        return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Builds the RGBA8 the screen sees. Clamping happens here and only here: the
// Surface itself is never clamped (CLAUDE.md, rule 2).
std::vector<std::uint8_t> makeDisplayPixels(const Surface& s, const ChannelMask& c) {
    std::vector<std::uint8_t> out(s.texelCount() * 4u, 255);

    const int rgbCount = (c.r ? 1 : 0) + (c.g ? 1 : 0) + (c.b ? 1 : 0);
    const bool alphaOnly = c.a && rgbCount == 0;
    const bool singleRgb = rgbCount == 1 && !c.a;

    const std::size_t texels = s.texelCount();
    for (std::size_t i = 0; i < texels; ++i) {
        const float* src = s.rgba.data() + i * 4u;
        std::uint8_t* dst = out.data() + i * 4u;

        // An undecodable ASTC block decodes to NaN, by astcenc's design
        // (CLAUDE.md, trap 10). Show it rather than letting it become an
        // arbitrary colour, so a corrupt level is visible instead of plausible.
        if (!std::isfinite(src[0]) || !std::isfinite(src[1]) || !std::isfinite(src[2]) ||
            !std::isfinite(src[3])) {
            dst[0] = 255;
            dst[1] = 0;
            dst[2] = 255;
            dst[3] = 255;
            continue;
        }

        float v[4];
        for (int k = 0; k < 4; ++k)
            v[k] = s.tf == TransferFn::Linear ? linearToSrgb(src[k]) : src[k];

        if (alphaOnly) {
            const std::uint8_t g = toByte(v[3]);
            dst[0] = dst[1] = dst[2] = g;
        } else if (singleRgb) {
            const std::uint8_t g = toByte(c.r ? v[0] : (c.g ? v[1] : v[2]));
            dst[0] = dst[1] = dst[2] = g;
        } else {
            dst[0] = c.r ? toByte(v[0]) : 0;
            dst[1] = c.g ? toByte(v[1]) : 0;
            dst[2] = c.b ? toByte(v[2]) : 0;
        }
        dst[3] = 255;
    }
    return out;
}

// Box filter, halving both axes. Odd dimensions drop the last row or column,
// which is only ever a display mip and never a measured one.
std::vector<std::uint8_t> halve(const std::vector<std::uint8_t>& src, int w, int h, int& outW,
                                int& outH) {
    outW = w > 1 ? w / 2 : 1;
    outH = h > 1 ? h / 2 : 1;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(outW) * outH * 4);
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            for (int c = 0; c < 4; ++c) {
                const int x0 = (w > 1) ? x * 2 : 0;
                const int y0 = (h > 1) ? y * 2 : 0;
                const int x1 = (w > 1) ? x0 + 1 : x0;
                const int y1 = (h > 1) ? y0 + 1 : y0;
                const int sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] +
                                src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                                src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] +
                                src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                out[(static_cast<std::size_t>(y) * outW + x) * 4 + c] =
                    static_cast<std::uint8_t>(sum / 4);
            }
        }
    }
    return out;
}

}  // namespace

ImageTexture::~ImageTexture() {
    release();
}

void ImageTexture::release() {
    if (m_id != 0) {
        const GLuint id = m_id;
        glDeleteTextures(1, &id);
        m_id = 0;
    }
    m_source = nullptr;
    m_channelKey = 0xFFFFFFFFu;
}

void ImageTexture::upload(const std::vector<std::uint8_t>& base) {
    if (m_id == 0) {
        GLuint id = 0;
        glGenTextures(1, &id);
        m_id = id;
    }
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // A display mip chain, box filtered on the CPU. glGenerateMipmap is GL 3.0
    // and opengl32 does not export it, and we want a box filter specifically:
    // "nearest above 1:1, box filtered below" (PLAN.md M2).
    std::vector<std::uint8_t> level = base;
    int w = m_w;
    int h = m_h;
    int levelIndex = 0;
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, levelIndex, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     level.data());
        if (w == 1 && h == 1)
            break;
        int nw = 0, nh = 0;
        level = halve(level, w, h, nw, nh);
        w = nw;
        h = nh;
        ++levelIndex;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ImageTexture::update(const Surface& surface, const ChannelMask& channels) {
    const std::uint32_t key = channelKey(channels);
    if (m_source == &surface && m_channelKey == key && m_id != 0)
        return;
    if (surface.empty() || surface.w <= 0 || surface.h <= 0) {
        release();
        return;
    }

    m_w = surface.w;
    m_h = surface.h;
    upload(makeDisplayPixels(surface, channels));
    m_source = &surface;
    m_channelKey = key;
    ++m_revision;
}

}  // namespace ktxcmp::ui
