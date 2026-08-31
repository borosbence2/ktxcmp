// CompareEngine and PNG-loader harness.
//
// The metric definitions in CLAUDE.md are contractual, so these tests pin them
// against arithmetic worked out by hand rather than against whatever the code
// currently produces.

#include "Corpus.hpp"

#include "compare/CompareEngine.hpp"
#include "container/KtxFile.hpp"
#include "decode/Decoder.hpp"
#include "decode/PngLoader.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    if (!ok)
        ++g_failures;
}

void checkNear(double got, double want, double tol, const std::string& what) {
    const bool ok = std::fabs(got - want) <= tol;
    ++g_checks;
    if (ok) {
        std::printf("  ok    %s (%.4f)\n", what.c_str(), got);
    } else {
        ++g_failures;
        std::printf("  FAIL  %s: got %.4f, want %.4f +/- %.4f\n", what.c_str(), got, want, tol);
    }
}

using ktxcmp::Surface;
using ktxcmp::TransferFn;

Surface makeSurface(int w, int h, TransferFn tf = TransferFn::Srgb) {
    Surface s;
    s.w = w;
    s.h = h;
    s.d = 1;
    s.tf = tf;
    s.rgba.assign(static_cast<std::size_t>(w) * h * 4u, 0.0f);
    for (std::size_t i = 3; i < s.rgba.size(); i += 4)
        s.rgba[i] = 1.0f;
    return s;
}

// ------------------------------------------------------------ definitions ---

void testIdenticalIsInfinite() {
    Surface a = makeSurface(8, 8);
    for (std::size_t i = 0; i < a.rgba.size(); ++i)
        a.rgba[i] = static_cast<float>(i % 251) / 255.0f;
    Surface b = a;

    auto r = ktxcmp::compare(a, b, false);
    if (!r) {
        check(false, "identical compare: " + r.error().message);
        return;
    }
    check(std::isinf(r->rgb.psnr), "identical surfaces give infinite PSNR");
    check(r->rgb.rmse == 0.0, "identical surfaces give zero RMSE");
    check(r->rgb.maxError == 0.0, "identical surfaces give zero max error");
    checkNear(r->ssim, 1.0, 1e-9, "identical surfaces give SSIM 1");
}

void testOneUnitEverywhere() {
    // Every channel of every texel differs by exactly 1/255, so MSE is 1 and
    // PSNR is 10*log10(255^2) = 48.1308 dB. A classic anchor value.
    Surface a = makeSurface(16, 16);
    Surface b = makeSurface(16, 16);
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            b.rgba[i + c] = 1.0f / 255.0f;

    auto r = ktxcmp::compare(a, b, false);
    if (!r) {
        check(false, "one-unit compare: " + r.error().message);
        return;
    }
    checkNear(r->rgb.psnr, 48.1308, 0.01, "a uniform 1/255 error gives 48.13 dB");
    checkNear(r->rgb.rmse, 1.0, 0.001, "RMSE is in 0-255 units");
    checkNear(r->rgb.maxError, 1.0, 0.001, "max error is in 0-255 units");
}

void testPsnrIsPooledNotAveraged() {
    // Two texels. Only red differs, and only in the first texel, by 10/255.
    //
    //   pooled:   MSE = 100 / (3 * 2) = 16.667  ->  35.91 dB
    //   averaged: PSNR_R = 31.14, PSNR_G = PSNR_B = infinity  ->  infinity
    //
    // CLAUDE.md specifies the pooled form, so an implementation that averaged
    // three per-channel PSNRs would report "identical" here.
    Surface a = makeSurface(2, 1);
    Surface b = makeSurface(2, 1);
    a.rgba[0] = 10.0f / 255.0f;

    auto r = ktxcmp::compare(a, b, false);
    if (!r) {
        check(false, "pooled compare: " + r.error().message);
        return;
    }
    check(!std::isinf(r->rgb.psnr), "PSNR pools RGB rather than averaging per-channel PSNRs");
    checkNear(r->rgb.psnr, 35.9122, 0.01, "pooled PSNR matches the hand-computed value");
    check(r->rgb.maxErrorX == 0 && r->rgb.maxErrorY == 0,
          "max error reports the coordinate it happened at");
}

void testAlphaIsSeparate() {
    // Identical colour, different alpha. Alpha must never enter an RGB figure.
    Surface a = makeSurface(4, 4);
    Surface b = makeSurface(4, 4);
    for (std::size_t i = 3; i < b.rgba.size(); i += 4)
        b.rgba[i] = 0.5f;

    auto r = ktxcmp::compare(a, b, false);
    if (!r) {
        check(false, "alpha compare: " + r.error().message);
        return;
    }
    check(std::isinf(r->rgb.psnr), "an alpha-only difference leaves RGB PSNR infinite");
    check(r->rgb.rmse == 0.0, "an alpha-only difference leaves RGB RMSE at zero");
    check(!std::isinf(r->alpha.psnr) && r->alpha.rmse > 0.0,
          "the alpha difference is reported, separately");
}

void testRefusesMismatchedDimensions() {
    const Surface a = makeSurface(8, 8);
    const Surface b = makeSurface(8, 4);
    auto r = ktxcmp::compare(a, b, false);
    check(!r, "mismatched dimensions are refused");
    if (!r)
        check(r.error().message.find("dimensions differ") != std::string::npos,
              "the refusal says dimensions differ, in words");
}

void testRefusesMismatchedTransferFunctions() {
    // CLAUDE.md rule 4: a wrong metric is worse than no metric.
    const Surface a = makeSurface(8, 8, TransferFn::Srgb);
    const Surface b = makeSurface(8, 8, TransferFn::Linear);
    auto r = ktxcmp::compare(a, b, false);
    check(!r, "mismatched transfer functions are refused");
    if (!r)
        check(r.error().message.find("transfer function") != std::string::npos,
              "the refusal names the transfer function mismatch");
}

void testNonFiniteIsExcludedAndCounted() {
    // One NaN would otherwise turn every mean into NaN (CLAUDE.md, trap 10).
    Surface a = makeSurface(4, 4);
    Surface b = makeSurface(4, 4);
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c)
            b.rgba[i + c] = 1.0f / 255.0f;
    a.rgba[0] = std::numeric_limits<float>::quiet_NaN();

    auto r = ktxcmp::compare(a, b, false);
    if (!r) {
        check(false, "NaN compare: " + r.error().message);
        return;
    }
    check(r->excludedNonFinite == 1, "the NaN texel is counted as excluded");
    check(r->samples == 15, "the other fifteen texels are still measured");
    check(std::isfinite(r->rgb.psnr), "one NaN does not turn the whole metric into NaN");
    checkNear(r->rgb.psnr, 48.1308, 0.01, "the surviving texels give the expected PSNR");
}

void testLinearLightChangesTheNumber() {
    // Dark values, where the sRGB curve is steepest and the two spaces disagree
    // most. At mid-grey the same difference moves PSNR by under half a dB, which
    // is exactly why the toggle has to be labelled wherever it appears.
    Surface a = makeSurface(8, 8);
    Surface b = makeSurface(8, 8);
    for (std::size_t i = 0; i < a.rgba.size(); i += 4)
        for (int c = 0; c < 3; ++c) {
            a.rgba[i + c] = 0.02f;
            b.rgba[i + c] = 0.06f;
        }

    auto srgb = ktxcmp::compare(a, b, false);
    auto linear = ktxcmp::compare(a, b, true);
    if (!srgb || !linear) {
        check(false, "linear-light compare failed");
        return;
    }
    std::printf("        sRGB %.2f dB, linear-light %.2f dB\n", srgb->rgb.psnr, linear->rgb.psnr);
    check(linear->rgb.psnr - srgb->rgb.psnr > 5.0,
          "the linear-light toggle changes the measurement, decisively in shadow");
    check(!srgb->linearLight && linear->linearLight,
          "the result carries the flag, so output can be labelled");
}

void testDifferenceSurface() {
    Surface a = makeSurface(4, 4);
    Surface b = makeSurface(4, 4);
    a.rgba[0] = 0.25f;
    auto d = ktxcmp::differenceSurface(a, b, 4);
    if (!d) {
        check(false, "difference: " + d.error().message);
        return;
    }
    checkNear(d->rgba[0], 1.0, 1e-6, "the difference is amplified by the gain");
    check(d->rgba[3] == 1.0f, "the difference surface is opaque");
    check(d->rgba[4] == 0.0f, "an identical texel differences to zero");
}

// --------------------------------------------------------------- PNG load ---

void testPngLoads(const std::filesystem::path& dir) {
    for (const auto& f : ktxcmp::test::pngFixtures()) {
        ktxcmp::PngInfo info;
        auto surface = ktxcmp::loadPng(dir / f.filename, TransferFn::Srgb, &info);
        if (!surface) {
            check(false, std::string(f.filename) + ": " + surface.error().message);
            continue;
        }
        check(surface->w == f.w && surface->h == f.h,
              std::string(f.filename) + " loads at " + std::to_string(f.w) + "x" +
                  std::to_string(f.h));
        check(info.bitDepth == f.bitDepth,
              std::string(f.filename) + " reports " + std::to_string(f.bitDepth) + "-bit");
        check(surface->tf == TransferFn::Srgb,
              std::string(f.filename) + " records the assumed transfer function");
    }
}

void testPngOverrideIsHonoured(const std::filesystem::path& dir) {
    const auto& fixtures = ktxcmp::test::pngFixtures();
    if (fixtures.empty())
        return;
    auto linear = ktxcmp::loadPng(dir / fixtures[0].filename, TransferFn::Linear);
    check(linear && linear->tf == TransferFn::Linear,
          "the sRGB assumption can be overridden to linear");
}

void testSixteenBitIsNotTruncated(const std::filesystem::path& dir) {
    const auto& fixtures = ktxcmp::test::pngFixtures();
    const ktxcmp::test::PngFixture* wide = nullptr;
    for (const auto& f : fixtures)
        if (f.bitDepth == 16)
            wide = &f;
    if (!wide) {
        check(false, "no 16-bit fixture");
        return;
    }

    auto surface = ktxcmp::loadPng(dir / wide->filename, TransferFn::Srgb);
    if (!surface) {
        check(false, "16-bit load: " + surface.error().message);
        return;
    }
    const std::vector<std::uint16_t> expected = ktxcmp::test::gradient16(wide->w, wide->h);

    // The blue channel was built so neighbouring texels share a high byte and
    // differ in the low one. A loader that dropped either byte would collapse
    // them (CLAUDE.md, trap 5).
    std::size_t exact = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const double want = static_cast<double>(expected[i]) / 65535.0;
        const double got = surface->rgba[i];
        worst = std::max(worst, std::fabs(got - want));
        if (std::fabs(got - want) < 1e-6)
            ++exact;
    }
    check(exact == expected.size(),
          "every 16-bit sample survives the load (worst error " + std::to_string(worst) + ")");

    const float b0 = surface->rgba[2];
    const float b1 = surface->rgba[6];
    check(b0 != b1, "texels that share a high byte stay distinct after loading");
}

// M4's done-criterion: the PSNR of a known-good ASTC encode has to land in a
// sane range. The fixture is a gradient encoded by libktx, so the source is
// known exactly and the number can be checked rather than eyeballed.
void testAstcEncodePsnrIsSane(const std::filesystem::path& dir) {
    const auto fixtures = ktxcmp::test::astcFixtures();
    if (fixtures.empty()) {
        check(false, "no ASTC fixture");
        return;
    }
    const auto& f = fixtures.front();

    auto file = ktxcmp::KtxFile::open(dir / f.filename);
    if (!file) {
        check(false, std::string(f.filename) + ": " + file.error().message);
        return;
    }
    auto bytes = file->levelBytes(0);
    if (!bytes) {
        check(false, "no level bytes");
        return;
    }
    auto decoded = ktxcmp::decode(file->info().format, *bytes, f.w, f.h);
    if (!decoded) {
        check(false, "decode: " + decoded.error().message);
        return;
    }

    // The same gradient the fixture was encoded from, as a Surface.
    const std::vector<std::uint8_t> src = ktxcmp::test::gradient(f.w, f.h);
    Surface source = makeSurface(f.w, f.h, TransferFn::Srgb);
    for (std::size_t i = 0; i < source.rgba.size(); ++i)
        source.rgba[i] = static_cast<float>(src[i]) / 255.0f;

    auto r = ktxcmp::compare(*decoded, source, false);
    if (!r) {
        check(false, "compare: " + r.error().message);
        return;
    }
    std::printf("        ASTC 6x6 sRGB vs source: PSNR %.2f dB, RMSE %.3f, SSIM %.4f,"
                " max %.1f @%d,%d\n",
                r->rgb.psnr, r->rgb.rmse, r->ssim, r->rgb.maxError, r->rgb.maxErrorX,
                r->rgb.maxErrorY);

    // A 6x6 ASTC encode of a smooth gradient is very good but not lossless, so a
    // sane result is bounded on both sides: an infinite PSNR would mean we were
    // comparing something against itself.
    check(r->rgb.psnr > 40.0 && std::isfinite(r->rgb.psnr),
          "a known-good ASTC encode gives a high but finite PSNR");
    check(r->ssim > 0.99, "SSIM of a known-good encode is close to 1");
    check(r->excludedNonFinite == 0, "a real encode produces no NaN");
}

// Reports metrics for a real pair, for checking against actual content.
int runFiles(const std::filesystem::path& ktxPath, const std::filesystem::path& pngPath) {
    auto file = ktxcmp::KtxFile::open(ktxPath);
    if (!file) {
        std::printf("cannot open %s: %s\n", ktxPath.string().c_str(),
                    file.error().message.c_str());
        return 1;
    }
    auto reference = ktxcmp::loadPng(pngPath, TransferFn::Srgb);
    if (!reference) {
        std::printf("cannot open %s: %s\n", pngPath.string().c_str(),
                    reference.error().message.c_str());
        return 1;
    }
    auto bytes = file->levelBytes(0);
    if (!bytes) {
        std::printf("no level 0 bytes\n");
        return 1;
    }
    const ktxcmp::LevelInfo& li = file->info().levels[0];
    auto decoded = ktxcmp::decode(file->info().format, *bytes, li.w, li.h, li.d);
    if (!decoded) {
        std::printf("decode failed: %s\n", decoded.error().message.c_str());
        return 1;
    }

    std::printf("A: %s, %s, %dx%d\n", ktxPath.filename().string().c_str(),
                file->info().formatName.c_str(), li.w, li.h);
    std::printf("B: %s, PNG, %dx%d\n", pngPath.filename().string().c_str(), reference->w,
                reference->h);

    for (int pass = 0; pass < 2; ++pass) {
        const bool linear = pass == 1;
        auto r = ktxcmp::compare(*decoded, *reference, linear);
        if (!r) {
            std::printf("  refused: %s\n", r.error().message.c_str());
            return 0;  // a refusal is a valid, informative outcome
        }
        std::printf("  %-12s PSNR %8s | RMSE %7.3f | SSIM %.4f | max %.1f @%d,%d%s\n",
                    linear ? "linear light" : "sRGB", ktxcmp::formatPsnr(r->rgb.psnr).c_str(),
                    r->rgb.rmse, r->ssim, r->rgb.maxError, r->rgb.maxErrorX, r->rgb.maxErrorY,
                    r->excludedNonFinite ? " (some texels excluded)" : "");
        std::printf("               alpha %s\n",
                    r->alpha.identical ? "identical" : ktxcmp::formatPsnr(r->alpha.psnr).c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--files")
        return runFiles(argv[2], argv[3]);

    const std::filesystem::path dir =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::temp_directory_path() / "ktxcmp_fixtures";

    std::printf("metric definitions\n");
    testIdenticalIsInfinite();
    testOneUnitEverywhere();
    testPsnrIsPooledNotAveraged();
    testAlphaIsSeparate();

    std::printf("refusals\n");
    testRefusesMismatchedDimensions();
    testRefusesMismatchedTransferFunctions();

    std::printf("robustness\n");
    testNonFiniteIsExcludedAndCounted();
    testLinearLightChangesTheNumber();
    testDifferenceSurface();

    std::printf("ASTC encode fidelity\n");
    testAstcEncodePsnrIsSane(dir);

    std::printf("PNG reference\n");
    testPngLoads(dir);
    testPngOverrideIsHonoured(dir);
    testSixteenBitIsNotTruncated(dir);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
