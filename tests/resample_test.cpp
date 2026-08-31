// Resampler harness.
//
// The interesting property is not sharpness, it is that downsampling colour in
// gamma space is wrong and the tool has to be able to show that. A black and
// white checkerboard halved should land near 188/255 in linear light and near
// 128/255 in gamma space, and that 60-unit gap is the whole point of M5.

#include "compare/CompareEngine.hpp"
#include "compare/Resampler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

using ktxcmp::Filter;
using ktxcmp::Surface;
using ktxcmp::TransferFn;

const Filter kAllFilters[] = {Filter::Box, Filter::Triangle, Filter::Kaiser, Filter::Lanczos3,
                             Filter::Mitchell};

Surface constantSurface(int w, int h, float v) {
    Surface s;
    s.w = w;
    s.h = h;
    s.d = 1;
    s.tf = TransferFn::Srgb;
    s.rgba.assign(static_cast<std::size_t>(w) * h * 4u, v);
    for (std::size_t i = 3; i < s.rgba.size(); i += 4)
        s.rgba[i] = 1.0f;
    return s;
}

Surface checkerboard(int w, int h) {
    Surface s = constantSurface(w, h, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float v = ((x + y) % 2 == 0) ? 1.0f : 0.0f;
            float* p = s.at(x, y);
            p[0] = p[1] = p[2] = v;
        }
    return s;
}

// A flat image has to stay flat under every filter, at its original value. This
// is what catches an unnormalised kernel or a bad edge clamp, both of which show
// up as a dark border that would then read as encoder error.
void testConstantStaysConstant() {
    for (Filter f : kAllFilters) {
        const Surface src = constantSurface(64, 64, 0.5f);
        auto dst = ktxcmp::resample(src, 16, 16, f, true);
        if (!dst) {
            check(false, std::string(filterName(f)) + ": " + dst.error().message);
            continue;
        }
        double worst = 0.0;
        for (int y = 0; y < dst->h; ++y)
            for (int x = 0; x < dst->w; ++x)
                for (int c = 0; c < 3; ++c)
                    worst = std::max(worst, std::fabs(dst->at(x, y)[c] - 0.5));
        checkNear(worst, 0.0, 1e-4,
                  std::string(filterName(f)) + " keeps a flat image flat, edges included");
    }
}

// Box halving is exactly a 2x2 average, so it can be checked against arithmetic
// rather than against itself.
void testBoxHalvingIsA2x2Average() {
    Surface src = constantSurface(4, 2, 0.0f);
    const float values[8] = {0.0f, 0.4f, 0.8f, 0.2f, 0.6f, 1.0f, 0.2f, 0.4f};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x) {
            float* p = src.at(x, y);
            p[0] = p[1] = p[2] = values[y * 4 + x];
        }

    // Gamma space, so the stored values average directly.
    auto dst = ktxcmp::resample(src, 2, 1, Filter::Box, false);
    if (!dst) {
        check(false, "box halving: " + dst.error().message);
        return;
    }
    checkNear(dst->at(0, 0)[0], (0.0 + 0.4 + 0.6 + 1.0) / 4.0, 1e-5,
              "box halving averages the first 2x2 block");
    checkNear(dst->at(1, 0)[0], (0.8 + 0.2 + 0.2 + 0.4) / 4.0, 1e-5,
              "box halving averages the second 2x2 block");
}

// The headline case. Halving a black and white checkerboard in linear light
// gives mid-grey in *light*, which is 0.7354 once re-encoded; doing it in gamma
// space gives 0.5. Roughly 60 units of 255 apart.
void testLinearLightVersusGammaSpace() {
    const Surface src = checkerboard(64, 64);

    auto linear = ktxcmp::resample(src, 32, 32, Filter::Box, true);
    auto gamma = ktxcmp::resample(src, 32, 32, Filter::Box, false);
    if (!linear || !gamma) {
        check(false, "checkerboard resample failed");
        return;
    }

    const double lin = linear->at(16, 16)[0];
    const double gam = gamma->at(16, 16)[0];
    std::printf("        linear light %.4f (%.0f/255), gamma space %.4f (%.0f/255)\n", lin,
                lin * 255.0, gam, gam * 255.0);

    checkNear(lin, 0.7354, 0.002, "halving in linear light gives the photometric average");
    checkNear(gam, 0.5, 0.002, "halving in gamma space gives the arithmetic average");
    check((lin - gam) * 255.0 > 50.0,
          "the two differ by more than fifty units, which is why the toggle exists");
}

void testAlphaIsNotGammaCorrected() {
    // Alpha is coverage, not colour: it must average arithmetically whichever
    // way the colour toggle is set.
    Surface src = constantSurface(4, 4, 0.0f);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            src.at(x, y)[3] = (x % 2 == 0) ? 0.0f : 1.0f;

    auto dst = ktxcmp::resample(src, 2, 2, Filter::Box, true);
    if (!dst) {
        check(false, "alpha resample: " + dst.error().message);
        return;
    }
    checkNear(dst->at(0, 0)[3], 0.5, 1e-5,
              "alpha averages arithmetically even with linear-light colour on");
}

void testTransferFunctionIsPreserved() {
    for (bool linear : {false, true}) {
        const Surface src = constantSurface(8, 8, 0.5f);
        auto dst = ktxcmp::resample(src, 4, 4, Filter::Lanczos3, linear);
        if (!dst) {
            check(false, "tf preservation: " + dst.error().message);
            continue;
        }
        check(dst->tf == src.tf,
              std::string("resampling records the same transfer function (linear-light ") +
                  (linear ? "on)" : "off)"));
    }
}

void testFilterChoiceChangesTheResult() {
    const Surface src = checkerboard(32, 32);
    auto box = ktxcmp::resample(src, 8, 8, Filter::Box, true);
    auto lanczos = ktxcmp::resample(src, 8, 8, Filter::Lanczos3, true);
    if (!box || !lanczos) {
        check(false, "filter comparison failed");
        return;
    }
    double worst = 0.0;
    for (std::size_t i = 0; i < box->rgba.size(); ++i)
        worst = std::max(worst, static_cast<double>(std::fabs(box->rgba[i] - lanczos->rgba[i])));
    check(worst > 1e-4,
          "different filters give different results, so reporting the filter matters");
}

void testHalveFollowsTheMipRule() {
    const Surface src = constantSurface(9, 1, 0.25f);
    auto dst = ktxcmp::halveSurface(src, Filter::Box, true);
    if (!dst) {
        check(false, "halve: " + dst.error().message);
        return;
    }
    check(dst->w == 4 && dst->h == 1, "halving floors like max(1, base >> 1) does");
}

void testRefusesEmpty() {
    const Surface empty;
    check(!ktxcmp::resample(empty, 4, 4, Filter::Box, true), "an empty surface is refused");
    const Surface src = constantSurface(4, 4, 0.5f);
    check(!ktxcmp::resample(src, 0, 4, Filter::Box, true), "a zero target size is refused");
}

}  // namespace

int main() {
    std::printf("energy\n");
    testConstantStaysConstant();
    std::printf("arithmetic\n");
    testBoxHalvingIsA2x2Average();
    testHalveFollowsTheMipRule();
    std::printf("colour space\n");
    testLinearLightVersusGammaSpace();
    testAlphaIsNotGammaCorrected();
    testTransferFunctionIsPreserved();
    std::printf("filters\n");
    testFilterChoiceChangesTheResult();
    std::printf("refusals\n");
    testRefusesEmpty();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
