#pragma once

// Decode layer: block format -> Surface. Knows nothing about containers or files.

#include "container/FormatId.hpp"
#include "core/Error.hpp"
#include "core/Surface.hpp"

#include <cstdint>
#include <span>

namespace ktxcmp {

// `blocks` is one subresource's still-encoded bytes, block-padded as stored.
// The returned Surface is exactly w x h x d: padding never escapes this call
// (CLAUDE.md, trap 3).
[[nodiscard]] Result<Surface> decode(const FormatId& format, std::span<const std::uint8_t> blocks,
                                     int w, int h, int d = 1);

}  // namespace ktxcmp
