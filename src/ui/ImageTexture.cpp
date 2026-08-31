#include "ui/ImageTexture.hpp"

#include <SDL3/SDL_opengl.h>

// GL 1.2 enum values. The functions we call are all GL 1.1, which is what
// opengl32 exports directly, but these constants are not in the 1.1 header.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif

namespace ktxcmp::ui {

ImageTexture::~ImageTexture() {
    release();
}

void ImageTexture::release() {
    if (m_id != 0) {
        const GLuint id = m_id;
        glDeleteTextures(1, &id);
        m_id = 0;
    }
    m_source.reset();
    m_w = m_h = 0;
}

void ImageTexture::update(const DisplayImagePtr& image) {
    if (m_source == image && m_id != 0)
        return;
    if (!image || image->mips.empty() || image->w <= 0 || image->h <= 0) {
        release();
        return;
    }

    if (m_id == 0) {
        GLuint id = 0;
        glGenTextures(1, &id);
        m_id = id;
    }
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // The chain was box filtered on the CPU: glGenerateMipmap is GL 3.0 and
    // opengl32 does not export it, and we want a box filter specifically
    // ("nearest above 1:1, box filtered below", PLAN.md M2).
    int w = image->w;
    int h = image->h;
    int levelIndex = 0;
    for (const auto& level : image->mips) {
        glTexImage2D(GL_TEXTURE_2D, levelIndex, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     level.data());
        if (w == 1 && h == 1)
            break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        ++levelIndex;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levelIndex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_w = image->w;
    m_h = image->h;
    m_source = image;
}

}  // namespace ktxcmp::ui
