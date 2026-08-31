#include "compare/NormalMap.hpp"

#include <algorithm>
#include <cmath>

namespace ktxcmp {
namespace {

constexpr double kRadToDeg = 57.29577951308232;

bool finite3(const float* p) {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

}  // namespace

bool looksLikeNormalMap(const FormatId& format) noexcept {
    // Two channels of tangent-space xy is what BC5 is for. Nothing else in the
    // supported set implies it.
    return format.family == FormatFamily::Bc5;
}

NormalField reconstructNormals(const Surface& src, bool isSigned) {
    NormalField out;
    out.w = src.w;
    out.h = src.h;
    const std::size_t texels = src.texelCount();
    out.xyz.assign(texels * 3u, 0.0f);
    out.length.assign(texels, 0.0f);

    for (std::size_t i = 0; i < texels; ++i) {
        const float* p = src.rgba.data() + i * 4u;

        // SNORM already spans [-1,1]; UNORM has to be expanded.
        const double x = isSigned ? p[0] : (2.0 * p[0] - 1.0);
        const double y = isSigned ? p[1] : (2.0 * p[1] - 1.0);

        if (!std::isfinite(x) || !std::isfinite(y)) {
            out.xyz[i * 3u + 2] = std::numeric_limits<float>::quiet_NaN();
            out.length[i] = std::numeric_limits<float>::quiet_NaN();
            continue;
        }

        const double xy = x * x + y * y;
        const double z = std::sqrt(std::max(0.0, 1.0 - xy));

        // Pre-normalisation length. Unit by construction when xy <= 1; larger
        // when the stored pair is not a valid normal.
        const double len = std::sqrt(xy + z * z);
        out.length[i] = static_cast<float>(len);

        const double inv = len > 0.0 ? 1.0 / len : 0.0;
        out.xyz[i * 3u + 0] = static_cast<float>(x * inv);
        out.xyz[i * 3u + 1] = static_cast<float>(y * inv);
        out.xyz[i * 3u + 2] = static_cast<float>(z * inv);
    }
    return out;
}

Result<NormalMetrics> compareNormals(const NormalField& test, const NormalField& reference) {
    if (test.w != reference.w || test.h != reference.h)
        return fail(ErrorCode::Internal,
                    "dimensions differ: " + std::to_string(test.w) + "x" +
                        std::to_string(test.h) + " against " + std::to_string(reference.w) + "x" +
                        std::to_string(reference.h));
    if (test.w <= 0 || test.h <= 0)
        return fail(ErrorCode::Internal, "nothing to compare");

    NormalMetrics out;
    const std::size_t texels = test.length.size();
    std::vector<double> angles;
    angles.reserve(texels);

    double angleSum = 0.0;
    double lengthSum = 0.0;

    for (std::size_t i = 0; i < texels; ++i) {
        const float* a = test.xyz.data() + i * 3u;
        const float* b = reference.xyz.data() + i * 3u;
        if (!finite3(a) || !finite3(b) || !std::isfinite(test.length[i])) {
            ++out.excludedNonFinite;
            continue;
        }

        const double dot = std::clamp(static_cast<double>(a[0]) * b[0] +
                                          static_cast<double>(a[1]) * b[1] +
                                          static_cast<double>(a[2]) * b[2],
                                      -1.0, 1.0);
        const double angle = std::acos(dot) * kRadToDeg;
        angles.push_back(angle);
        angleSum += angle;

        if (angle > out.maxAngleDeg) {
            out.maxAngleDeg = angle;
            out.maxAngleX = static_cast<int>(i % static_cast<std::size_t>(test.w));
            out.maxAngleY = static_cast<int>(i / static_cast<std::size_t>(test.w));
        }

        const double deviation = std::fabs(static_cast<double>(test.length[i]) - 1.0);
        lengthSum += deviation;
        out.maxLengthDeviation = std::max(out.maxLengthDeviation, deviation);
    }

    out.samples = angles.size();
    if (out.samples == 0)
        return fail(ErrorCode::Internal,
                    "every texel was NaN or Inf, so there is nothing to measure");

    out.meanAngleDeg = angleSum / static_cast<double>(out.samples);
    out.meanLengthDeviation = lengthSum / static_cast<double>(out.samples);

    // nth_element rather than a full sort: the distribution is only needed at
    // two points, and a 2048 square level is four million samples.
    const std::size_t mid = out.samples / 2;
    std::nth_element(angles.begin(), angles.begin() + static_cast<std::ptrdiff_t>(mid),
                     angles.end());
    out.medianAngleDeg = angles[mid];

    const std::size_t p95 =
        std::min(out.samples - 1, static_cast<std::size_t>(0.95 * static_cast<double>(out.samples)));
    std::nth_element(angles.begin(), angles.begin() + static_cast<std::ptrdiff_t>(p95),
                     angles.end());
    out.p95AngleDeg = angles[p95];

    return out;
}

Result<Surface> renormaliseNormals(const Surface& src, bool isSigned) {
    if (src.empty())
        return fail(ErrorCode::Internal, "cannot renormalise an empty surface");

    Surface out = src;
    const std::size_t texels = src.texelCount();
    for (std::size_t i = 0; i < texels; ++i) {
        float* p = out.rgba.data() + i * 4u;
        const double x = isSigned ? p[0] : (2.0 * p[0] - 1.0);
        const double y = isSigned ? p[1] : (2.0 * p[1] - 1.0);
        const double z = isSigned ? p[2] : (2.0 * p[2] - 1.0);

        const double len = std::sqrt(x * x + y * y + z * z);
        if (!(len > 0.0) || !std::isfinite(len))
            continue;

        const double nx = x / len;
        const double ny = y / len;
        const double nz = z / len;

        p[0] = static_cast<float>(isSigned ? nx : (nx * 0.5 + 0.5));
        p[1] = static_cast<float>(isSigned ? ny : (ny * 0.5 + 0.5));
        p[2] = static_cast<float>(isSigned ? nz : (nz * 0.5 + 0.5));
    }
    return out;
}

}  // namespace ktxcmp
