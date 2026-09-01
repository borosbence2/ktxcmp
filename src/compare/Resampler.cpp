#include "compare/Resampler.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ktxcmp {
namespace {

constexpr double kPi = 3.14159265358979323846;

double sinc(double x) {
    if (std::fabs(x) < 1e-9)
        return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

// Modified Bessel function of the first kind, order zero. Series form; the
// argument here never exceeds the Kaiser beta, so it converges quickly.
double besselI0(double x) {
    double sum = 1.0;
    double term = 1.0;
    for (int k = 1; k < 32; ++k) {
        term *= (x * x) / (4.0 * k * k);
        sum += term;
        if (term < sum * 1e-16)
            break;
    }
    return sum;
}

double mitchell(double x) {
    // B = C = 1/3, the values Mitchell and Netravali recommend.
    constexpr double b = 1.0 / 3.0;
    constexpr double c = 1.0 / 3.0;
    x = std::fabs(x);
    if (x < 1.0)
        return ((12.0 - 9.0 * b - 6.0 * c) * x * x * x +
                (-18.0 + 12.0 * b + 6.0 * c) * x * x + (6.0 - 2.0 * b)) /
               6.0;
    if (x < 2.0)
        return ((-b - 6.0 * c) * x * x * x + (6.0 * b + 30.0 * c) * x * x +
                (-12.0 * b - 48.0 * c) * x + (8.0 * b + 24.0 * c)) /
               6.0;
    return 0.0;
}

double filterRadius(Filter f) {
    switch (f) {
        case Filter::Box:      return 0.5;
        case Filter::Triangle: return 1.0;
        case Filter::Mitchell: return 2.0;
        case Filter::Kaiser:   return 3.0;
        case Filter::Lanczos3: return 3.0;
    }
    return 1.0;
}

double filterWeight(Filter f, double x) {
    switch (f) {
        case Filter::Box:
            return std::fabs(x) <= 0.5 ? 1.0 : 0.0;
        case Filter::Triangle: {
            const double a = std::fabs(x);
            return a < 1.0 ? 1.0 - a : 0.0;
        }
        case Filter::Mitchell:
            return mitchell(x);
        case Filter::Lanczos3: {
            const double a = std::fabs(x);
            return a < 3.0 ? sinc(x) * sinc(x / 3.0) : 0.0;
        }
        case Filter::Kaiser: {
            const double a = std::fabs(x);
            if (a >= 3.0)
                return 0.0;
            constexpr double beta = 4.0;
            const double t = a / 3.0;
            return sinc(x) * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) /
                   besselI0(beta);
        }
    }
    return 0.0;
}

float srgbToLinear(float v) {
    if (!(v > 0.0f))
        return 0.0f;
    if (v <= 0.04045f)
        return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float v) {
    if (!(v > 0.0f))
        return 0.0f;
    if (v <= 0.0031308f)
        return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Which source samples feed one destination sample, and how much of each.
struct Contribution {
    int first = 0;
    std::vector<float> weights;
};

std::vector<Contribution> buildContributions(int srcSize, int dstSize, Filter f) {
    std::vector<Contribution> out(static_cast<std::size_t>(dstSize));
    const double scale = static_cast<double>(dstSize) / static_cast<double>(srcSize);

    // Downsampling widens the kernel so it averages the whole footprint;
    // upsampling leaves it at its natural width.
    const double filterScale = scale < 1.0 ? 1.0 / scale : 1.0;
    const double support = filterRadius(f) * filterScale;

    for (int i = 0; i < dstSize; ++i) {
        const double centre = (static_cast<double>(i) + 0.5) / scale - 0.5;
        int left = static_cast<int>(std::ceil(centre - support));
        int right = static_cast<int>(std::floor(centre + support));
        if (right < left)
            right = left;

        Contribution& c = out[static_cast<std::size_t>(i)];
        c.first = std::max(0, left);
        const int last = std::min(srcSize - 1, right);

        double total = 0.0;
        std::vector<double> w;
        w.reserve(static_cast<std::size_t>(last - c.first + 1));
        for (int j = c.first; j <= last; ++j) {
            const double weight = filterWeight(f, (static_cast<double>(j) - centre) / filterScale);
            w.push_back(weight);
            total += weight;
        }
        // Renormalising is what keeps a flat image flat: without it the clamped
        // edges would darken, which would then read as encoder error.
        if (total == 0.0) {
            w.assign(w.size(), 0.0);
            if (!w.empty())
                w[0] = 1.0;
        } else {
            for (double& x : w)
                x /= total;
        }
        c.weights.reserve(w.size());
        for (double weight : w)
            c.weights.push_back(static_cast<float>(weight));
    }
    return out;
}

}  // namespace

const char* filterName(Filter f) noexcept {
    switch (f) {
        case Filter::Box:      return "Box";
        case Filter::Triangle: return "Triangle";
        case Filter::Kaiser:   return "Kaiser";
        case Filter::Lanczos3: return "Lanczos3";
        case Filter::Mitchell: return "Mitchell";
    }
    return "unknown";
}

Result<Surface> resample(const Surface& src, int dstW, int dstH, Filter filter, bool linearLight) {
    if (src.w <= 0 || src.h <= 0 || src.empty())
        return fail(ErrorCode::Internal, "cannot resample an empty surface");
    if (dstW <= 0 || dstH <= 0)
        return fail(ErrorCode::Internal, "resample target must be at least 1x1");
    if (src.d != 1)
        return fail(ErrorCode::Internal, "resampling 3D surfaces is not supported");

    // Colour is filtered in linear light when asked; alpha never is.
    const bool convert = linearLight && src.tf == TransferFn::Srgb;

    std::vector<float> working(src.rgba.size());
    for (std::size_t i = 0; i < src.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c)
            working[i + c] = convert ? srgbToLinear(src.rgba[i + c]) : src.rgba[i + c];
        working[i + 3] = src.rgba[i + 3];
    }

    const auto horizontal = buildContributions(src.w, dstW, filter);
    std::vector<float> mid(static_cast<std::size_t>(dstW) * src.h * 4u, 0.0f);
    for (int y = 0; y < src.h; ++y) {
        const float* row = working.data() + static_cast<std::size_t>(y) * src.w * 4u;
        float* out = mid.data() + static_cast<std::size_t>(y) * dstW * 4u;
        for (int x = 0; x < dstW; ++x) {
            const Contribution& c = horizontal[static_cast<std::size_t>(x)];
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t k = 0; k < c.weights.size(); ++k) {
                const float* s = row + (static_cast<std::size_t>(c.first) + k) * 4u;
                for (int ch = 0; ch < 4; ++ch)
                    acc[ch] += s[ch] * c.weights[k];
            }
            for (int ch = 0; ch < 4; ++ch)
                out[static_cast<std::size_t>(x) * 4u + ch] = acc[ch];
        }
    }

    const auto vertical = buildContributions(src.h, dstH, filter);
    Surface dst;
    dst.w = dstW;
    dst.h = dstH;
    dst.d = 1;
    dst.tf = src.tf;
    dst.premultiplied = src.premultiplied;
    dst.rgba.assign(static_cast<std::size_t>(dstW) * dstH * 4u, 0.0f);

    for (int y = 0; y < dstH; ++y) {
        const Contribution& c = vertical[static_cast<std::size_t>(y)];
        float* out = dst.rgba.data() + static_cast<std::size_t>(y) * dstW * 4u;
        for (int x = 0; x < dstW; ++x) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t k = 0; k < c.weights.size(); ++k) {
                const float* s =
                    mid.data() + ((static_cast<std::size_t>(c.first) + k) * dstW + x) * 4u;
                for (int ch = 0; ch < 4; ++ch)
                    acc[ch] += s[ch] * c.weights[k];
            }
            for (int ch = 0; ch < 3; ++ch)
                out[static_cast<std::size_t>(x) * 4u + ch] = convert ? linearToSrgb(acc[ch]) : acc[ch];
            out[static_cast<std::size_t>(x) * 4u + 3] = acc[3];
        }
    }
    return dst;
}

}  // namespace ktxcmp
