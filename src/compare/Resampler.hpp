#pragma once

// Separable resampling, for building the reference chain in compare mode 2 and
// the halved level in mode 3.
//
// The filter choice changes the answer, which is why CLAUDE.md requires the
// filter and the linear-light flag to appear in every table and every export:
// a number from mode 2 means nothing without them.

#include "core/Error.hpp"
#include "core/Surface.hpp"

namespace ktxcmp {

enum class Filter { Box, Triangle, Kaiser, Lanczos3, Mitchell };

[[nodiscard]] const char* filterName(Filter f) noexcept;

// linearLight filters the colour channels in linear light, which is the correct
// way to downsample colour and is on by default (PLAN.md M5). Alpha is filtered
// as stored either way: it is coverage, not colour, and has no transfer function
// to undo.
[[nodiscard]] Result<Surface> resample(const Surface& src, int dstW, int dstH, Filter filter,
                                       bool linearLight);

}  // namespace ktxcmp
