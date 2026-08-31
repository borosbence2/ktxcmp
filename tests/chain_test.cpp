// Chain analysis harness: compare modes 2 and 3, validation, and CSV.
//
// The centrepiece is M5's done-criterion. Two checkerboards are written with
// identical base levels and different mip chains: one averaged in sRGB (the
// classic mistake) and one in linear light. Mode 3 with linear-light resampling
// should light up on the first and be quiet on the second, and turning the
// toggle off should swap which one looks wrong.

#include "Corpus.hpp"

#include "compare/ChainAnalysis.hpp"
#include "container/KtxFile.hpp"
#include "decode/Decoder.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    if (!ok)
        ++g_failures;
}

using ktxcmp::ChainInput;
using ktxcmp::ChainReport;
using ktxcmp::CompareMode;
using ktxcmp::Filter;
using ktxcmp::Surface;
using ktxcmp::SurfacePtr;

// Decodes every level of a file into the vector the analyser wants.
std::vector<SurfacePtr> decodeAllLevels(const std::filesystem::path& path, std::string& why) {
    std::vector<SurfacePtr> out;
    auto file = ktxcmp::KtxFile::open(path);
    if (!file) {
        why = file.error().message;
        return out;
    }
    const ktxcmp::KtxInfo& info = file->info();
    for (int level = 0; level < info.levelCount; ++level) {
        auto bytes = file->levelBytes(level);
        if (!bytes) {
            why = bytes.error().message;
            return {};
        }
        const ktxcmp::LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
        auto decoded = ktxcmp::decode(info.format, *bytes, li.w, li.h, li.d);
        if (!decoded) {
            why = decoded.error().message;
            return {};
        }
        out.push_back(std::make_shared<const Surface>(std::move(*decoded)));
    }
    return out;
}

// Worst (lowest) PSNR across the levels that have one. That is the "spike" the
// error-by-level plot is meant to make obvious.
double worstPsnr(const ChainReport& r) {
    double worst = 1e9;
    for (const auto& level : r.levels)
        if (level.hasMetrics && level.metrics.rgb.psnr < worst)
            worst = level.metrics.rgb.psnr;
    return worst;
}

void testGammaSpaceMipsShowASpike(const std::filesystem::path& dir) {
    std::string why;
    const auto gamma = decodeAllLevels(dir / ktxcmp::test::kGammaMipsFixture, why);
    const auto linear = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (gamma.empty() || linear.empty()) {
        check(false, "could not decode the checkerboard fixtures: " + why);
        return;
    }

    auto run = [](const std::vector<SurfacePtr>& levels, bool resampleLinear) {
        ChainInput in;
        in.levels = levels;
        in.mode = CompareMode::SelfConsistency;
        in.filter = Filter::Box;  // exact 2x2, so the only variable is the space
        in.resampleLinearLight = resampleLinear;
        in.metricLinearLight = false;
        return ktxcmp::analyseChain(in);
    };

    auto gammaLinearOn = run(gamma, true);
    auto gammaLinearOff = run(gamma, false);
    auto linearLinearOn = run(linear, true);

    if (!gammaLinearOn || !gammaLinearOff || !linearLinearOn) {
        check(false, "chain analysis failed");
        return;
    }

    const double spike = worstPsnr(*gammaLinearOn);
    const double collapsed = worstPsnr(*gammaLinearOff);
    const double clean = worstPsnr(*linearLinearOn);

    std::printf("        gamma-space mips, linear-light resampling ON : worst PSNR %.2f dB\n",
                spike);
    std::printf("        gamma-space mips, linear-light resampling OFF: worst PSNR %.2f dB\n",
                collapsed);
    std::printf("        linear-light mips, linear-light resampling ON: worst PSNR %.2f dB\n",
                clean);

    // M5's done-criterion, in two halves.
    check(spike < 30.0, "a gamma-space chain shows a clear error spike under linear-light mode");
    check(collapsed - spike > 15.0,
          "turning linear-light resampling off collapses that spike");
    check(clean > 30.0, "a correctly built chain does not show the spike in the first place");
    check(clean - spike > 15.0,
          "the two chains are told apart by a wide margin, not a hair");
}

void testModeTwoUsesTheReference(const std::filesystem::path& dir) {
    std::string why;
    const auto levels = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (levels.empty()) {
        check(false, "decode: " + why);
        return;
    }

    ChainInput in;
    in.levels = levels;
    in.reference = levels.front();  // the base level is its own perfect reference
    in.mode = CompareMode::ChainVsReference;
    in.filter = Filter::Box;
    in.resampleLinearLight = true;

    auto report = ktxcmp::analyseChain(in);
    if (!report) {
        check(false, "mode 2: " + report.error().message);
        return;
    }
    check(report->levels.size() == levels.size(), "mode 2 reports every level");
    check(report->levels.front().hasMetrics && std::isinf(report->levels.front().metrics.rgb.psnr),
          "level 0 against an identical reference is exact");
    check(report->levels.back().hasMetrics, "the smallest level still gets a number");
}

void testModeTwoRefusesWithoutAReference(const std::filesystem::path& dir) {
    std::string why;
    const auto levels = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (levels.empty())
        return;
    ChainInput in;
    in.levels = levels;
    in.mode = CompareMode::ChainVsReference;
    auto report = ktxcmp::analyseChain(in);
    check(!report, "mode 2 without a reference is refused, not guessed at");

    ChainInput mode1;
    mode1.levels = levels;
    mode1.mode = CompareMode::EncodeFidelity;
    check(!ktxcmp::analyseChain(mode1), "mode 1 is not a chain analysis and says so");
}

void testModeThreeSkipsLevelZero(const std::filesystem::path& dir) {
    std::string why;
    const auto levels = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (levels.empty())
        return;
    ChainInput in;
    in.levels = levels;
    in.mode = CompareMode::SelfConsistency;
    auto report = ktxcmp::analyseChain(in);
    if (!report) {
        check(false, "mode 3: " + report.error().message);
        return;
    }
    check(!report->levels.front().hasMetrics && !report->levels.front().note.empty(),
          "mode 3 gives level 0 a reason rather than a number");
    check(report->levels[1].hasMetrics, "mode 3 measures level 1 against a halved level 0");
}

void testValidationFlagsATruncatedChain(const std::filesystem::path& dir) {
    std::string why;
    auto levels = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (levels.size() < 3)
        return;
    levels.resize(levels.size() - 2);  // lop off the smallest levels

    ChainInput in;
    in.levels = levels;
    in.mode = CompareMode::SelfConsistency;
    auto report = ktxcmp::analyseChain(in);
    if (!report) {
        check(false, "truncated: " + report.error().message);
        return;
    }
    bool flagged = false;
    for (const auto& w : report->warnings)
        if (w.level < 0 && w.message.find("truncated") != std::string::npos)
            flagged = true;
    check(flagged, "a truncated chain is flagged as a chain-level warning");
}

void testValidationFlagsAConstantLevel(const std::filesystem::path& dir) {
    std::string why;
    auto levels = decodeAllLevels(dir / ktxcmp::test::kLinearMipsFixture, why);
    if (levels.size() < 3)
        return;

    // Replace level 2 with solid black, which is what a broken chain generator
    // tends to leave behind.
    auto black = std::make_shared<Surface>(*levels[2]);
    for (float& v : black->rgba)
        v = 0.0f;
    for (std::size_t i = 3; i < black->rgba.size(); i += 4)
        black->rgba[i] = 1.0f;
    levels[2] = black;

    ChainInput in;
    in.levels = levels;
    in.mode = CompareMode::SelfConsistency;
    auto report = ktxcmp::analyseChain(in);
    if (!report) {
        check(false, "constant level: " + report.error().message);
        return;
    }
    bool flagged = false;
    for (const auto& w : report->warnings)
        if (w.level == 2 && w.message.find("black") != std::string::npos)
            flagged = true;
    check(flagged, "an all-black level is flagged by name");
}

void testCsvCarriesTheSettings(const std::filesystem::path& dir) {
    std::string why;
    const auto levels = decodeAllLevels(dir / ktxcmp::test::kGammaMipsFixture, why);
    if (levels.empty())
        return;

    ChainInput in;
    in.levels = levels;
    in.mode = CompareMode::SelfConsistency;
    in.filter = Filter::Mitchell;
    in.resampleLinearLight = true;
    in.metricLinearLight = true;
    auto report = ktxcmp::analyseChain(in);
    if (!report) {
        check(false, "csv: " + report.error().message);
        return;
    }

    const std::string csv = ktxcmp::toCsv(*report, "a.ktx2", "b.png");
    // Without these a mode 2 or 3 number is not interpretable, so the export
    // has to carry them (CLAUDE.md, Compare modes).
    check(csv.find("Mitchell") != std::string::npos, "the CSV names the filter");
    check(csv.find("resample_linear_light,true") != std::string::npos,
          "the CSV records the resampling colour space");
    check(csv.find("metric_linear_light,true") != std::string::npos,
          "the CSV records the metric colour space");
    check(csv.find("3 self-consistency") != std::string::npos, "the CSV names the compare mode");
    check(csv.find("a.ktx2") != std::string::npos && csv.find("b.png") != std::string::npos,
          "the CSV records both file paths");

    std::size_t rows = 0;
    for (std::size_t i = 0; i < csv.size(); ++i)
        if (csv[i] == '\n')
            ++rows;
    check(rows >= levels.size(), "the CSV has a row per level");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::temp_directory_path() / "ktxcmp_fixtures";

    std::printf("gamma-space mips (M5 done-criterion)\n");
    testGammaSpaceMipsShowASpike(dir);

    std::printf("modes\n");
    testModeTwoUsesTheReference(dir);
    testModeTwoRefusesWithoutAReference(dir);
    testModeThreeSkipsLevelZero(dir);

    std::printf("validation\n");
    testValidationFlagsATruncatedChain(dir);
    testValidationFlagsAConstantLevel(dir);

    std::printf("export\n");
    testCsvCarriesTheSettings(dir);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
