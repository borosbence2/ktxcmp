#pragma once

// Normal-map interpretation and angular error.
//
// For BC5 in normal-map mode PSNR is not reported at all (CLAUDE.md): the
// question is not "how far is this colour" but "how far has this direction
// turned", and those are different quantities with different units.

#include "container/FormatId.hpp"
#include "core/Error.hpp"
#include "core/Surface.hpp"

#include <cstddef>
#include <vector>

namespace ktxcmp {

// Unit normals plus the length each one had before being normalised. That
// length is the interesting diagnostic: filtering a normal map shortens the
// vectors, and a chain that never renormalised shows it here (trap 7).
struct NormalField {
    int w = 0;
    int h = 0;
    std::vector<float> xyz;     // 3 per texel, unit length
    std::vector<float> length;  // 1 per texel, |n| before normalising
};

// isSigned selects the remap: SNORM data is already in [-1,1], UNORM has to be
// expanded from [0,1]. Getting this backwards silently halves every angle
// (CLAUDE.md, trap 2).
//
// z is reconstructed as sqrt(max(0, 1 - x^2 - y^2)). When x^2 + y^2 exceeds 1
// the stored pair is not a valid normal at all; z clamps to zero and the
// recorded length exceeds 1, which is how that shows up rather than being
// silently absorbed.
[[nodiscard]] NormalField reconstructNormals(const Surface& src, bool isSigned);

struct NormalMetrics {
    // Angular error in degrees: acos(clamp(dot(n_ref, n_test), -1, 1)).
    double meanAngleDeg = 0.0;
    double medianAngleDeg = 0.0;
    double p95AngleDeg = 0.0;
    double maxAngleDeg = 0.0;
    int maxAngleX = -1;
    int maxAngleY = -1;

    // Deviation of |n| from 1.0, pre-normalisation, on the test side.
    double meanLengthDeviation = 0.0;
    double maxLengthDeviation = 0.0;

    std::size_t samples = 0;
    std::size_t excludedNonFinite = 0;
};

[[nodiscard]] Result<NormalMetrics> compareNormals(const NormalField& test,
                                                   const NormalField& reference);

// Renormalises a resampled normal map. Filtering shortens the vectors, and
// comparing without this reports an angular error that is really a length
// error (CLAUDE.md, trap 7).
[[nodiscard]] Result<Surface> renormaliseNormals(const Surface& src, bool isSigned);

// BC5 means a normal map unless the user says otherwise (PLAN.md M6).
[[nodiscard]] bool looksLikeNormalMap(const FormatId& format) noexcept;

}  // namespace ktxcmp
