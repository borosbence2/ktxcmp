#pragma once

// Metrics between two surfaces.
//
// The definitions in CLAUDE.md are contractual. In particular PSNR-RGB pools
// MSE across R, G and B together over 3N samples - it is not the average of
// three per-channel PSNRs, which is a different number - and alpha is reported
// separately and never enters an RGB figure.

#include "core/Error.hpp"
#include "core/Surface.hpp"

#include <cstddef>
#include <string>

namespace ktxcmp {

struct ChannelStats {
    double rmse = 0.0;       // 0-255 units
    double psnr = 0.0;       // dB; infinity when identical
    double maxError = 0.0;   // 0-255 units
    int maxErrorX = -1;
    int maxErrorY = -1;
    bool identical = false;
};

struct CompareResult {
    ChannelStats rgb;    // pooled across R, G, B
    ChannelStats alpha;  // separate, always
    double ssim = 0.0;   // Rec.709 luma, 11x11 Gaussian sigma 1.5

    int w = 0;
    int h = 0;
    std::size_t samples = 0;  // texels that contributed

    // Texels excluded because one side was NaN or Inf. astc-encoder emits NaN
    // for undecodable blocks (CLAUDE.md, trap 10), and a single one would
    // otherwise turn every mean into NaN.
    std::size_t excludedNonFinite = 0;

    // Off by default, and must be labelled wherever the numbers appear.
    bool linearLight = false;
};

// Refuses rather than guessing: mismatched dimensions, and mismatched transfer
// functions (CLAUDE.md rule 4 - a wrong metric is worse than no metric).
[[nodiscard]] Result<CompareResult> compare(const Surface& a, const Surface& b, bool linearLight);

// Absolute difference, amplified, as a surface the viewer can display like any
// other. Alpha is set opaque: the diff is about colour error.
[[nodiscard]] Result<Surface> differenceSurface(const Surface& a, const Surface& b, int gain);

[[nodiscard]] std::string formatPsnr(double psnr);

}  // namespace ktxcmp
