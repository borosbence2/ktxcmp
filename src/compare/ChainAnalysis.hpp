#pragma once

// Per-level analysis of a mip chain: compare modes 2 and 3, plus the validation
// checks CLAUDE.md lists.
//
// Mode 2 depends on the filter and on whether resampling happened in linear
// light, so both travel with the report and must appear in any table or export
// built from it. A mode 2 number without them is not interpretable.

#include "compare/CompareMode.hpp"
#include "compare/CompareEngine.hpp"
#include "compare/NormalMap.hpp"
#include "compare/Resampler.hpp"
#include "core/Error.hpp"
#include "core/Surface.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ktxcmp {

struct LevelStats {
    int level = 0;
    int w = 0;
    int h = 0;

    bool hasMetrics = false;
    CompareResult metrics{};

    // In normal-map mode this replaces the colour metrics entirely; the two are
    // never both present (CLAUDE.md: PSNR is not reported for a normal map).
    bool hasNormalMetrics = false;
    NormalMetrics normal{};
    std::string note;  // why there is no metric, when there is none

    // Validation, computed whether or not a comparison was possible.
    double meanLuma = 0.0;       // Rec.709, 0-255
    double alphaCoverage = 0.0;  // fraction of texels at or above the threshold
    bool constant = false;       // every texel identical
    bool black = false;          // constant and zero
    std::size_t nonFinite = 0;
};

struct ChainWarning {
    int level = -1;  // -1 when it concerns the chain as a whole
    std::string message;
};

struct ChainReport {
    CompareMode mode = CompareMode::ChainVsReference;
    Filter filter = Filter::Lanczos3;
    bool resampleLinearLight = true;
    bool metricLinearLight = false;
    bool normalMode = false;

    int baseWidth = 0;
    int baseHeight = 0;
    int expectedLevels = 0;

    std::vector<LevelStats> levels;
    std::vector<ChainWarning> warnings;

    double alphaThreshold = 0.5;   // for the coverage check
    double coverageDrift = 0.05;   // how much drift is worth a warning
};

struct ChainInput {
    // Decoded levels of slot A, in order. Empty entries are not allowed.
    std::vector<SurfacePtr> levels;
    // Mode 2 only; ignored by mode 3, which needs no reference.
    SurfacePtr reference;

    CompareMode mode = CompareMode::ChainVsReference;
    Filter filter = Filter::Lanczos3;
    bool resampleLinearLight = true;
    bool metricLinearLight = false;

    // Normal maps are compared by angle, and a resampled normal map has to be
    // renormalised first (CLAUDE.md, trap 7).
    bool normalMode = false;
    bool isSigned = false;
};

[[nodiscard]] Result<ChainReport> analyseChain(const ChainInput& input);

// CSV of the per-level table. The header carries mode, filter and both
// linear-light flags, because the numbers below mean nothing without them
// (CLAUDE.md, Compare modes).
[[nodiscard]] std::string toCsv(const ChainReport& report, const std::string& pathA,
                                const std::string& pathB);

}  // namespace ktxcmp
