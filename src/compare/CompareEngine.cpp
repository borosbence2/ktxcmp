#include "compare/CompareEngine.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace ktxcmp {
namespace {

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

// Both surfaces are brought into the space the metric is defined in. The default
// is sRGB-encoded values; the linear-light toggle asks for the other one.
float toMetricSpace(float v, TransferFn tf, bool linearLight) {
    if (linearLight)
        return tf == TransferFn::Srgb ? srgbToLinear(v) : v;
    return tf == TransferFn::Linear ? linearToSrgb(v) : v;
}

double psnrFromMse(double mse) {
    if (mse <= 0.0)
        return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10((255.0 * 255.0) / mse);
}

bool finite4(const float* p) {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) &&
           std::isfinite(p[3]);
}

// Separable Gaussian, sigma 1.5 over an 11-tap window, edges clamped. Clamping
// rather than cropping means border texels are measured too, which matters for
// a block format whose worst blocks are often at the edge.
std::vector<double> gaussianBlur(const std::vector<double>& src, int w, int h) {
    constexpr int kRadius = 5;
    double kernel[2 * kRadius + 1];
    double sum = 0.0;
    for (int i = -kRadius; i <= kRadius; ++i) {
        kernel[i + kRadius] = std::exp(-(i * i) / (2.0 * 1.5 * 1.5));
        sum += kernel[i + kRadius];
    }
    for (double& k : kernel)
        k /= sum;

    std::vector<double> tmp(src.size());
    std::vector<double> out(src.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double acc = 0.0;
            for (int i = -kRadius; i <= kRadius; ++i) {
                const int sx = std::min(std::max(x + i, 0), w - 1);
                acc += kernel[i + kRadius] * src[static_cast<std::size_t>(y) * w + sx];
            }
            tmp[static_cast<std::size_t>(y) * w + x] = acc;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double acc = 0.0;
            for (int i = -kRadius; i <= kRadius; ++i) {
                const int sy = std::min(std::max(y + i, 0), h - 1);
                acc += kernel[i + kRadius] * tmp[static_cast<std::size_t>(sy) * w + x];
            }
            out[static_cast<std::size_t>(y) * w + x] = acc;
        }
    }
    return out;
}

double computeSsim(const std::vector<double>& x, const std::vector<double>& y, int w, int h) {
    constexpr double kC1 = (0.01 * 255.0) * (0.01 * 255.0);
    constexpr double kC2 = (0.03 * 255.0) * (0.03 * 255.0);

    const std::size_t n = x.size();
    std::vector<double> xx(n), yy(n), xy(n);
    for (std::size_t i = 0; i < n; ++i) {
        xx[i] = x[i] * x[i];
        yy[i] = y[i] * y[i];
        xy[i] = x[i] * y[i];
    }

    const std::vector<double> muX = gaussianBlur(x, w, h);
    const std::vector<double> muY = gaussianBlur(y, w, h);
    const std::vector<double> sXX = gaussianBlur(xx, w, h);
    const std::vector<double> sYY = gaussianBlur(yy, w, h);
    const std::vector<double> sXY = gaussianBlur(xy, w, h);

    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double mx = muX[i];
        const double my = muY[i];
        const double vx = sXX[i] - mx * mx;
        const double vy = sYY[i] - my * my;
        const double cxy = sXY[i] - mx * my;
        total += ((2.0 * mx * my + kC1) * (2.0 * cxy + kC2)) /
                 ((mx * mx + my * my + kC1) * (vx + vy + kC2));
    }
    return total / static_cast<double>(n);
}

}  // namespace

Result<CompareResult> compare(const Surface& a, const Surface& b, bool linearLight) {
    if (a.w != b.w || a.h != b.h || a.d != b.d)
        return fail(ErrorCode::Internal,
                    "dimensions differ: " + std::to_string(a.w) + "x" + std::to_string(a.h) +
                        " against " + std::to_string(b.w) + "x" + std::to_string(b.h) +
                        ". Mode 1 compares mip 0 against the reference with no resampling.");

    // CLAUDE.md rule 4: refuse rather than produce a number from two surfaces in
    // different spaces.
    if (a.tf != b.tf)
        return fail(ErrorCode::Internal,
                    std::string("transfer functions differ: A is ") +
                        (a.tf == TransferFn::Srgb ? "sRGB" : "linear") + ", B is " +
                        (b.tf == TransferFn::Srgb ? "sRGB" : "linear") +
                        ". Set the reference override so both agree.");

    if (a.w <= 0 || a.h <= 0)
        return fail(ErrorCode::Internal, "nothing to compare");

    CompareResult out;
    out.w = a.w;
    out.h = a.h;
    out.linearLight = linearLight;

    const std::size_t texels = a.texelCount();
    double sseRgb = 0.0;
    double sseAlpha = 0.0;
    std::size_t counted = 0;

    std::vector<double> lumaA(texels), lumaB(texels);

    for (std::size_t i = 0; i < texels; ++i) {
        const float* pa = a.rgba.data() + i * 4u;
        const float* pb = b.rgba.data() + i * 4u;

        if (!finite4(pa) || !finite4(pb)) {
            ++out.excludedNonFinite;
            lumaA[i] = 0.0;
            lumaB[i] = 0.0;
            continue;
        }

        double va[4], vb[4];
        for (int c = 0; c < 4; ++c) {
            va[c] = toMetricSpace(pa[c], a.tf, linearLight) * 255.0;
            vb[c] = toMetricSpace(pb[c], b.tf, linearLight) * 255.0;
        }

        const int x = static_cast<int>(i % static_cast<std::size_t>(a.w));
        const int y = static_cast<int>(i / static_cast<std::size_t>(a.w));

        for (int c = 0; c < 3; ++c) {
            const double e = va[c] - vb[c];
            sseRgb += e * e;
            const double ae = std::fabs(e);
            if (ae > out.rgb.maxError) {
                out.rgb.maxError = ae;
                out.rgb.maxErrorX = x;
                out.rgb.maxErrorY = y;
            }
        }

        const double ea = va[3] - vb[3];
        sseAlpha += ea * ea;
        const double aea = std::fabs(ea);
        if (aea > out.alpha.maxError) {
            out.alpha.maxError = aea;
            out.alpha.maxErrorX = x;
            out.alpha.maxErrorY = y;
        }

        // Rec.709 luma, for SSIM only.
        lumaA[i] = 0.2126 * va[0] + 0.7152 * va[1] + 0.0722 * va[2];
        lumaB[i] = 0.2126 * vb[0] + 0.7152 * vb[1] + 0.0722 * vb[2];
        ++counted;
    }

    out.samples = counted;
    if (counted == 0)
        return fail(ErrorCode::Internal,
                    "every texel was NaN or Inf, so there is nothing to measure");

    // Pooled across the three channels together: 3N samples, one MSE.
    const double mseRgb = sseRgb / (3.0 * static_cast<double>(counted));
    out.rgb.rmse = std::sqrt(mseRgb);
    out.rgb.psnr = psnrFromMse(mseRgb);
    out.rgb.identical = sseRgb == 0.0;

    const double mseAlpha = sseAlpha / static_cast<double>(counted);
    out.alpha.rmse = std::sqrt(mseAlpha);
    out.alpha.psnr = psnrFromMse(mseAlpha);
    out.alpha.identical = sseAlpha == 0.0;

    out.ssim = computeSsim(lumaA, lumaB, a.w, a.h);
    return out;
}

Result<Surface> differenceSurface(const Surface& a, const Surface& b, int gain) {
    if (a.w != b.w || a.h != b.h)
        return fail(ErrorCode::Internal, "cannot difference surfaces of different sizes");

    Surface out;
    out.w = a.w;
    out.h = a.h;
    out.d = 1;
    out.tf = a.tf;
    out.rgba.assign(a.texelCount() * 4u, 1.0f);

    const float scale = static_cast<float>(gain > 0 ? gain : 1);
    for (std::size_t i = 0; i < a.texelCount(); ++i) {
        const float* pa = a.rgba.data() + i * 4u;
        const float* pb = b.rgba.data() + i * 4u;
        float* dst = out.rgba.data() + i * 4u;
        for (int c = 0; c < 3; ++c) {
            const float d = pa[c] - pb[c];
            dst[c] = std::isfinite(d) ? std::fabs(d) * scale
                                      : std::numeric_limits<float>::quiet_NaN();
        }
        dst[3] = 1.0f;
    }
    return out;
}

std::string formatPsnr(double psnr) {
    if (std::isinf(psnr))
        return "identical";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f dB", psnr);
    return buf;
}

}  // namespace ktxcmp
