#include "compare/ChainAnalysis.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace ktxcmp {

const char* compareModeName(CompareMode m) noexcept {
    switch (m) {
        case CompareMode::EncodeFidelity:   return "1 encode fidelity";
        case CompareMode::ChainVsReference: return "2 chain vs reference";
        case CompareMode::SelfConsistency:  return "3 self-consistency";
    }
    return "unknown";
}

namespace {

// The validation checks CLAUDE.md lists, computed per level.
void measureLevel(const Surface& s, double alphaThreshold, LevelStats& out) {
    double lumaSum = 0.0;
    std::size_t covered = 0;
    std::size_t counted = 0;

    const float* first = nullptr;
    bool constant = true;

    const std::size_t texels = s.texelCount();
    for (std::size_t i = 0; i < texels; ++i) {
        const float* p = s.rgba.data() + i * 4u;
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) ||
            !std::isfinite(p[3])) {
            ++out.nonFinite;
            continue;
        }
        if (first == nullptr) {
            first = p;
        } else if (constant) {
            for (int c = 0; c < 4; ++c)
                if (p[c] != first[c])
                    constant = false;
        }
        lumaSum += (0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]) * 255.0;
        if (p[3] >= alphaThreshold)
            ++covered;
        ++counted;
    }

    if (counted > 0) {
        out.meanLuma = lumaSum / static_cast<double>(counted);
        out.alphaCoverage = static_cast<double>(covered) / static_cast<double>(counted);
    }
    out.constant = counted > 0 && constant;
    out.black = out.constant && first != nullptr && first[0] == 0.0f && first[1] == 0.0f &&
                first[2] == 0.0f;
}

int expectedLevelCount(int w, int h) {
    int levels = 1;
    for (int d = (w > h ? w : h); d > 1; d /= 2)
        ++levels;
    return levels;
}

std::string fixed(double v, int places) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", places, v);
    return buf;
}

}  // namespace

Result<ChainReport> analyseChain(const ChainInput& input) {
    if (input.levels.empty())
        return fail(ErrorCode::Internal, "no levels to analyse");
    for (const SurfacePtr& s : input.levels)
        if (!s)
            return fail(ErrorCode::Internal, "a level is still decoding");

    if (input.mode == CompareMode::ChainVsReference && !input.reference)
        return fail(ErrorCode::Internal, "mode 2 needs a reference in slot B");
    if (input.mode == CompareMode::EncodeFidelity)
        return fail(ErrorCode::Internal, "mode 1 is a single-level comparison, not a chain");

    ChainReport report;
    report.mode = input.mode;
    report.filter = input.filter;
    report.resampleLinearLight = input.resampleLinearLight;
    report.metricLinearLight = input.metricLinearLight;
    report.baseWidth = input.levels.front()->w;
    report.baseHeight = input.levels.front()->h;
    report.expectedLevels = expectedLevelCount(report.baseWidth, report.baseHeight);

    const int count = static_cast<int>(input.levels.size());
    report.levels.reserve(input.levels.size());

    for (int level = 0; level < count; ++level) {
        const Surface& s = *input.levels[static_cast<std::size_t>(level)];
        LevelStats stats;
        stats.level = level;
        stats.w = s.w;
        stats.h = s.h;
        measureLevel(s, report.alphaThreshold, stats);

        if (input.mode == CompareMode::ChainVsReference) {
            // At a level the reference is already the size of, there is nothing
            // to resample, and resampling anyway would not be free: a
            // linear-light round trip through the transfer function loses a
            // little precision, and Mitchell is not even an identity at scale 1.
            // Mode 2 at level 0 should therefore agree exactly with mode 1.
            const bool sameSize = input.reference->w == s.w && input.reference->h == s.h;
            auto resized = sameSize ? Result<Surface>(*input.reference)
                                    : resample(*input.reference, s.w, s.h, input.filter,
                                               input.resampleLinearLight);
            if (!resized) {
                stats.note = resized.error().message;
            } else if (resized->tf != s.tf) {
                stats.note = "reference and texture are in different transfer functions";
            } else {
                auto metrics = compare(s, *resized, input.metricLinearLight);
                if (metrics) {
                    stats.metrics = *metrics;
                    stats.hasMetrics = true;
                } else {
                    stats.note = metrics.error().message;
                }
            }
        } else {  // SelfConsistency
            if (level == 0) {
                stats.note = "no level above to halve";
            } else {
                const Surface& previous = *input.levels[static_cast<std::size_t>(level - 1)];
                auto halved = resample(previous, s.w, s.h, input.filter,
                                       input.resampleLinearLight);
                if (!halved) {
                    stats.note = halved.error().message;
                } else {
                    auto metrics = compare(s, *halved, input.metricLinearLight);
                    if (metrics) {
                        stats.metrics = *metrics;
                        stats.hasMetrics = true;
                    } else {
                        stats.note = metrics.error().message;
                    }
                }
            }
        }

        report.levels.push_back(std::move(stats));
    }

    // ---- chain validation (CLAUDE.md, Chain validation checks) ----

    if (count != report.expectedLevels)
        report.warnings.push_back(
            ChainWarning{-1, "chain has " + std::to_string(count) + " levels, expected " +
                                 std::to_string(report.expectedLevels) + " for " +
                                 std::to_string(report.baseWidth) + "x" +
                                 std::to_string(report.baseHeight) + " (truncated)"});

    for (int level = 0; level < count; ++level) {
        const LevelStats& stats = report.levels[static_cast<std::size_t>(level)];
        const int ew = (report.baseWidth >> level) > 1 ? (report.baseWidth >> level) : 1;
        const int eh = (report.baseHeight >> level) > 1 ? (report.baseHeight >> level) : 1;
        if (stats.w != ew || stats.h != eh)
            report.warnings.push_back(
                ChainWarning{level, "is " + std::to_string(stats.w) + "x" +
                                        std::to_string(stats.h) + ", expected " +
                                        std::to_string(ew) + "x" + std::to_string(eh)});
        if (stats.nonFinite > 0)
            report.warnings.push_back(ChainWarning{
                level, std::to_string(stats.nonFinite) + " texels are NaN or Inf"});
        if (stats.black)
            report.warnings.push_back(ChainWarning{level, "is entirely black"});
        else if (stats.constant)
            report.warnings.push_back(ChainWarning{level, "is a single constant colour"});

        // Alpha-test coverage drift, against level 0.
        if (level > 0 && !report.levels.empty()) {
            const double base = report.levels.front().alphaCoverage;
            const double drift = std::fabs(stats.alphaCoverage - base);
            if (drift > report.coverageDrift)
                report.warnings.push_back(ChainWarning{
                    level, "alpha coverage drifted to " + fixed(stats.alphaCoverage * 100.0, 1) +
                               "% from " + fixed(base * 100.0, 1) + "% at level 0"});
        }
    }

    return report;
}

std::string toCsv(const ChainReport& report, const std::string& pathA, const std::string& pathB) {
    std::ostringstream out;
    // Everything needed to interpret the numbers, before the numbers.
    out << "# ktxcmp per-level report\n";
    out << "# mode," << compareModeName(report.mode) << "\n";
    out << "# filter," << filterName(report.filter) << "\n";
    out << "# resample_linear_light," << (report.resampleLinearLight ? "true" : "false") << "\n";
    out << "# metric_linear_light," << (report.metricLinearLight ? "true" : "false") << "\n";
    out << "# alpha_threshold," << fixed(report.alphaThreshold, 3) << "\n";
    out << "# slot_a," << pathA << "\n";
    out << "# slot_b," << pathB << "\n";
    out << "level,width,height,psnr_rgb_db,rmse,ssim,max_error,max_error_x,max_error_y,"
           "alpha_psnr_db,mean_luma,alpha_coverage,excluded_non_finite,note\n";

    for (const LevelStats& s : report.levels) {
        out << s.level << ',' << s.w << ',' << s.h << ',';
        if (s.hasMetrics) {
            const CompareResult& m = s.metrics;
            out << (std::isinf(m.rgb.psnr) ? "inf" : fixed(m.rgb.psnr, 4)) << ','
                << fixed(m.rgb.rmse, 6) << ',' << fixed(m.ssim, 6) << ','
                << fixed(m.rgb.maxError, 4) << ',' << m.rgb.maxErrorX << ',' << m.rgb.maxErrorY
                << ',' << (std::isinf(m.alpha.psnr) ? "inf" : fixed(m.alpha.psnr, 4)) << ',';
        } else {
            out << ",,,,,,,";
        }
        out << fixed(s.meanLuma, 4) << ',' << fixed(s.alphaCoverage, 6) << ','
            << (s.hasMetrics ? s.metrics.excludedNonFinite : s.nonFinite) << ',';
        // Notes are free text, so they are quoted and any quote is doubled.
        std::string note = s.note;
        std::string escaped;
        for (char c : note) {
            if (c == '"')
                escaped += '"';
            escaped += c;
        }
        out << '"' << escaped << "\"\n";
    }

    if (!report.warnings.empty()) {
        out << "# warnings\n";
        for (const ChainWarning& w : report.warnings)
            out << "# ," << (w.level < 0 ? std::string("chain") : std::to_string(w.level)) << ','
                << w.message << "\n";
    }
    return out.str();
}

}  // namespace ktxcmp
