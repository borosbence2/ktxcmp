#pragma once

// PNG reference images. 8-bit and 16-bit, always widened to RGBA32F.

#include "container/FormatId.hpp"  // TransferFn
#include "core/Error.hpp"
#include "core/Surface.hpp"

#include <filesystem>

namespace ktxcmp {

struct PngInfo {
    int w = 0;
    int h = 0;
    int channels = 0;   // as stored in the file, before widening to RGBA
    int bitDepth = 8;   // 8 or 16
};

// PNG carries no reliable colour-space signal, so the caller states what to
// assume and the UI exposes that as an override. gAMA is never consulted
// (CLAUDE.md, trap 4).
[[nodiscard]] Result<Surface> loadPng(const std::filesystem::path& path, TransferFn assume,
                                      PngInfo* infoOut = nullptr);

}  // namespace ktxcmp
