#pragma once

// The core invariant of the project (CLAUDE.md).
//
//   1. RGBA32F is the only intermediate. Never decode to RGBA8 as a staging step.
//   2. Never clamp to [0,1] anywhere in the decode or metric path.
//   3. `tf` is recorded, not applied. Conversion happens explicitly, and only
//      where a specific operation requires it.
//   4. CompareEngine refuses mismatched `tf`.
//   5. Crop to logical dimensions after block decode.

#include "container/FormatId.hpp"

#include <cstddef>
#include <vector>

namespace ktxcmp {

struct Surface {
    int w = 0, h = 0, d = 1;
    std::vector<float> rgba;  // w*h*d*4, NOT clamped to [0,1]
    TransferFn tf = TransferFn::Srgb;
    bool premultiplied = false;

    [[nodiscard]] bool empty() const { return rgba.empty(); }

    [[nodiscard]] std::size_t texelCount() const {
        return static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
               static_cast<std::size_t>(d);
    }

    [[nodiscard]] std::size_t indexOf(int x, int y, int z = 0) const {
        return ((static_cast<std::size_t>(z) * static_cast<std::size_t>(h) +
                 static_cast<std::size_t>(y)) *
                    static_cast<std::size_t>(w) +
                static_cast<std::size_t>(x)) *
               4u;
    }

    [[nodiscard]] const float* at(int x, int y, int z = 0) const {
        return rgba.data() + indexOf(x, y, z);
    }
    [[nodiscard]] float* at(int x, int y, int z = 0) { return rgba.data() + indexOf(x, y, z); }
};

}  // namespace ktxcmp
