// Normal-map harness.
//
// M6's done-criterion: a BC5 normal map reports sub-degree mean angular error
// against its source, and the raw RG override produces PSNR instead. The
// fixture is encoded from a field this test can rebuild exactly, so the number
// is checked rather than admired.

#include "Corpus.hpp"

#include "compare/CompareEngine.hpp"
#include "compare/NormalMap.hpp"
#include "compare/Resampler.hpp"
#include "container/KtxFile.hpp"
#include "decode/Decoder.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
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

using ktxcmp::Filter;
using ktxcmp::NormalField;
using ktxcmp::Surface;
using ktxcmp::TransferFn;

// The source field as a Surface in the same UNORM encoding BC5 stores.
Surface sourceAsSurface(int w, int h) {
    const std::vector<float> field = ktxcmp::test::normalFieldXyz(w, h);
    Surface s;
    s.w = w;
    s.h = h;
    s.d = 1;
    s.tf = TransferFn::Linear;  // a normal map is not colour
    s.rgba.assign(static_cast<std::size_t>(w) * h * 4u, 0.0f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        const float* n = field.data() + i * 3u;
        float* p = s.rgba.data() + i * 4u;
        p[0] = n[0] * 0.5f + 0.5f;
        p[1] = n[1] * 0.5f + 0.5f;
        p[2] = 0.0f;  // BC5 stores no z; reconstruction supplies it
        p[3] = 1.0f;
    }
    return s;
}

std::optional<Surface> decodeFixture(const std::filesystem::path& dir, std::string& why,
                                     ktxcmp::FormatId& formatOut) {
    auto file = ktxcmp::KtxFile::open(dir / ktxcmp::test::kBc5NormalFixture);
    if (!file) {
        why = file.error().message;
        return std::nullopt;
    }
    formatOut = file->info().format;
    auto bytes = file->levelBytes(0);
    if (!bytes) {
        why = bytes.error().message;
        return std::nullopt;
    }
    const ktxcmp::LevelInfo& li = file->info().levels[0];
    auto decoded = ktxcmp::decode(formatOut, *bytes, li.w, li.h, li.d);
    if (!decoded) {
        why = decoded.error().message;
        return std::nullopt;
    }
    return std::move(*decoded);
}

void testDetection() {
    ktxcmp::FormatId bc5{ktxcmp::FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, false};
    ktxcmp::FormatId bc7{ktxcmp::FormatFamily::Bc7, 4, 4, 16, 4, TransferFn::Srgb, false};
    ktxcmp::FormatId astc{ktxcmp::FormatFamily::Astc, 6, 6, 16, 4, TransferFn::Srgb, false};
    check(ktxcmp::looksLikeNormalMap(bc5), "BC5 is taken to be a normal map by default");
    check(!ktxcmp::looksLikeNormalMap(bc7), "BC7 is not");
    check(!ktxcmp::looksLikeNormalMap(astc), "ASTC is not");
}

void testSubDegreeAngularError(const std::filesystem::path& dir) {
    std::string why;
    ktxcmp::FormatId format{};
    auto decoded = decodeFixture(dir, why, format);
    if (!decoded) {
        check(false, "BC5 fixture: " + why);
        return;
    }
    check(format.family == ktxcmp::FormatFamily::Bc5 && !format.isSigned,
          "the fixture comes back as BC5 UNORM");

    const Surface source = sourceAsSurface(decoded->w, decoded->h);
    const NormalField test = ktxcmp::reconstructNormals(*decoded, format.isSigned);
    const NormalField reference = ktxcmp::reconstructNormals(source, false);

    auto metrics = ktxcmp::compareNormals(test, reference);
    if (!metrics) {
        check(false, "angular compare: " + metrics.error().message);
        return;
    }
    std::printf("        mean %.4f deg, median %.4f, p95 %.4f, max %.4f @%d,%d\n",
                metrics->meanAngleDeg, metrics->medianAngleDeg, metrics->p95AngleDeg,
                metrics->maxAngleDeg, metrics->maxAngleX, metrics->maxAngleY);
    std::printf("        |n| deviation: mean %.6f, max %.6f\n", metrics->meanLengthDeviation,
                metrics->maxLengthDeviation);

    // M6's done-criterion.
    check(metrics->meanAngleDeg < 1.0,
          "a BC5 normal map reports sub-degree mean angular error against its source");
    check(metrics->maxAngleDeg > 0.0, "the error is measured, not trivially zero");
    check(metrics->medianAngleDeg <= metrics->p95AngleDeg &&
              metrics->p95AngleDeg <= metrics->maxAngleDeg,
          "median, p95 and max are ordered as a distribution should be");
    check(metrics->excludedNonFinite == 0, "no texel was excluded");
}

void testRawRgOverrideGivesPsnr(const std::filesystem::path& dir) {
    std::string why;
    ktxcmp::FormatId format{};
    auto decoded = decodeFixture(dir, why, format);
    if (!decoded)
        return;

    // The override reads the same file as plain two-channel data, so the ordinary
    // colour metric applies and reports decibels rather than degrees.
    Surface source = sourceAsSurface(decoded->w, decoded->h);
    source.tf = decoded->tf;
    auto psnr = ktxcmp::compare(*decoded, source, false);
    if (!psnr) {
        check(false, "raw RG compare: " + psnr.error().message);
        return;
    }
    std::printf("        raw RG: PSNR %.2f dB, RMSE %.3f\n", psnr->rgb.psnr, psnr->rgb.rmse);
    check(std::isfinite(psnr->rgb.psnr) && psnr->rgb.psnr > 30.0,
          "the raw RG override produces a PSNR instead of an angle");
}

void testSignednessMatters() {
    // A UNORM surface read as SNORM halves every direction, which must not pass
    // unnoticed (CLAUDE.md, trap 2).
    Surface s;
    s.w = 2;
    s.h = 1;
    s.d = 1;
    s.tf = TransferFn::Linear;
    s.rgba = {0.5f, 0.5f, 0.0f, 1.0f, 0.9f, 0.5f, 0.0f, 1.0f};

    const NormalField asUnorm = ktxcmp::reconstructNormals(s, false);
    const NormalField asSnorm = ktxcmp::reconstructNormals(s, true);

    // 0.5 in UNORM is zero; in SNORM it is half a unit to the side.
    check(std::fabs(asUnorm.xyz[0]) < 1e-6f, "UNORM 0.5 maps to a flat normal");
    check(std::fabs(asSnorm.xyz[0] - 0.5f) < 1e-3f, "SNORM 0.5 stays at 0.5");
    check(std::fabs(asUnorm.xyz[3] - asSnorm.xyz[3]) > 1e-3f,
          "reading the wrong signedness gives a different normal, not a subtle one");
}

void testInvalidPairIsVisible() {
    // x^2 + y^2 > 1 is not a normal at all. z clamps to zero and the recorded
    // length exceeds 1 rather than the problem being absorbed silently.
    Surface s;
    s.w = 1;
    s.h = 1;
    s.d = 1;
    s.tf = TransferFn::Linear;
    s.rgba = {1.0f, 1.0f, 0.0f, 1.0f};  // UNORM 1.0 -> +1, so x=y=1

    const NormalField field = ktxcmp::reconstructNormals(s, false);
    check(field.length[0] > 1.0f, "an impossible xy pair records a length above 1");
    check(std::fabs(field.xyz[2]) < 1e-6f, "its z clamps to zero rather than going imaginary");
}

// Trap 7: averaging unit vectors that point in different directions gives a
// shorter vector, so a filtered normal map has to be renormalised before it is
// compared. Comparing without it reports an angular error that is really a
// length error.
//
// The "shorter" part only holds for a filter whose weights are all positive. A
// smooth field under Lanczos3 came out at 1.000042 - very slightly longer than
// unit - because the negative lobes overshoot. So this uses a divergent field
// and a box filter, where the shortening is guaranteed rather than incidental,
// and renormalising is checked to fix both cases.
void testDownsamplingNeedsRenormalising() {
    const int size = 32;

    // Neighbouring texels tilt hard in opposite directions, so averaging them
    // genuinely cancels.
    Surface field;
    field.w = size;
    field.h = size;
    field.d = 1;
    field.tf = TransferFn::Linear;
    field.rgba.assign(static_cast<std::size_t>(size) * size * 4u, 0.0f);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double s2 = 0.70710678;
            const double nx = ((x + y) % 2 == 0) ? s2 : -s2;
            float* p = field.at(x, y);
            p[0] = static_cast<float>(nx * 0.5 + 0.5);
            p[1] = 0.5f;
            p[2] = static_cast<float>(s2 * 0.5 + 0.5);
            p[3] = 1.0f;
        }

    auto meanLength = [](const Surface& s) {
        double sum = 0.0;
        const std::size_t texels = s.texelCount();
        for (std::size_t i = 0; i < texels; ++i) {
            const float* p = s.rgba.data() + i * 4u;
            const double x = 2.0 * p[0] - 1.0, y = 2.0 * p[1] - 1.0, z = 2.0 * p[2] - 1.0;
            sum += std::sqrt(x * x + y * y + z * z);
        }
        return sum / static_cast<double>(texels);
    };

    check(std::fabs(meanLength(field) - 1.0) < 1e-5, "the source field is unit length");

    auto halved = ktxcmp::resample(field, size / 2, size / 2, Filter::Box, false);
    if (!halved) {
        check(false, "resample: " + halved.error().message);
        return;
    }
    auto renormalised = ktxcmp::renormaliseNormals(*halved, false);
    if (!renormalised) {
        check(false, "renormalise: " + renormalised.error().message);
        return;
    }

    const double before = meanLength(*halved);
    const double after = meanLength(*renormalised);
    std::printf("        mean |n| after box filtering %.6f, after renormalising %.6f\n", before,
                after);

    check(before < 0.99, "box filtering a divergent field shortens the normals");
    check(std::fabs(after - 1.0) < 1e-4, "renormalising restores unit length");

    // And the angular consequence: without renormalising the comparison is
    // measuring the wrong thing.
    const NormalField shortened = ktxcmp::reconstructNormals(*halved, false);
    check(shortened.length[0] > 0.0f, "the shortened field still reconstructs");
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::temp_directory_path() / "ktxcmp_fixtures";

    std::printf("detection\n");
    testDetection();

    std::printf("angular error (M6 done-criterion)\n");
    testSubDegreeAngularError(dir);
    testRawRgOverrideGivesPsnr(dir);

    std::printf("reconstruction\n");
    testSignednessMatters();
    testInvalidPairIsVisible();

    std::printf("renormalising\n");
    testDownsamplingNeedsRenormalising();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
