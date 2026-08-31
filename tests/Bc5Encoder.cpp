// A small BC5 encoder, for test fixtures only.
//
// libktx encodes ASTC and Basis, not BC5, so without this there is no way to
// produce a BC5 normal map whose source is known exactly - and without a known
// source, "sub-degree mean angular error" (PLAN.md M6) cannot be checked.
//
// The encoder is deliberately simple: min/max endpoints and nearest-index
// selection. It is not competitive with a real one, which is the point: if our
// decoder can recover sub-degree normals from output this plain, it is decoding
// correctly.
//
// This is test tooling. The application never writes a file (CLAUDE.md, Scope).

#include "Corpus.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ktxcmp::test {
namespace {

// The eight values a BC4 block decodes to when e0 > e1, matching bcdec.
void paletteFor(std::uint8_t e0, std::uint8_t e1, float out[8]) {
    out[0] = e0;
    out[1] = e1;
    for (int i = 2; i < 8; ++i)
        out[i] = static_cast<float>((8 - i) * e0 + (i - 1) * e1) / 7.0f;
}

// One 4x4 block of a single channel into the 8 bytes BC4 uses.
void encodeBc4Block(const std::uint8_t values[16], std::uint8_t out[8]) {
    std::uint8_t lo = 255, hi = 0;
    for (int i = 0; i < 16; ++i) {
        lo = std::min(lo, values[i]);
        hi = std::max(hi, values[i]);
    }
    // e0 > e1 selects the eight-value mode; equal endpoints are a flat block.
    if (hi == lo) {
        if (hi < 255)
            ++hi;
        else
            --lo;
    }

    float palette[8];
    paletteFor(hi, lo, palette);

    out[0] = hi;
    out[1] = lo;
    for (int i = 2; i < 8; ++i)
        out[i] = 0;

    std::uint64_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        float bestError = 1e9f;
        for (int k = 0; k < 8; ++k) {
            const float e = std::fabs(palette[k] - static_cast<float>(values[i]));
            if (e < bestError) {
                bestError = e;
                best = k;
            }
        }
        indices |= static_cast<std::uint64_t>(best) << (i * 3);
    }
    for (int i = 0; i < 6; ++i)
        out[2 + i] = static_cast<std::uint8_t>((indices >> (i * 8)) & 0xFF);
}

}  // namespace

// A smooth tangent-space normal field: a shallow dome crossed with a ripple, so
// the normals actually turn rather than all pointing at the viewer.
std::vector<float> normalFieldXyz(int w, int h) {
    std::vector<float> out(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double u = (static_cast<double>(x) / (w - 1)) * 2.0 - 1.0;
            const double v = (static_cast<double>(y) / (h - 1)) * 2.0 - 1.0;

            // Slopes from a dome plus a gentle sine ripple.
            double nx = -0.55 * u + 0.12 * std::sin(u * 6.28318);
            double ny = -0.55 * v + 0.12 * std::sin(v * 6.28318);
            const double nz = 1.0;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);

            float* p = out.data() + (static_cast<std::size_t>(y) * w + x) * 3;
            p[0] = static_cast<float>(nx / len);
            p[1] = static_cast<float>(ny / len);
            p[2] = static_cast<float>(nz / len);
        }
    }
    return out;
}

// Encodes that field to BC5 UNORM: x in the first BC4 block, y in the second.
std::vector<std::uint8_t> encodeBc5Normals(int w, int h) {
    const std::vector<float> field = normalFieldXyz(w, h);
    const int blocksX = (w + 3) / 4;
    const int blocksY = (h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(blocksX) * blocksY * 16);

    std::size_t offset = 0;
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            std::uint8_t xs[16];
            std::uint8_t ys[16];
            for (int ty = 0; ty < 4; ++ty) {
                for (int tx = 0; tx < 4; ++tx) {
                    // Blocks past the edge repeat the last real texel, which is
                    // what an encoder does and what the crop then discards.
                    const int sx = std::min(bx * 4 + tx, w - 1);
                    const int sy = std::min(by * 4 + ty, h - 1);
                    const float* n = field.data() + (static_cast<std::size_t>(sy) * w + sx) * 3;
                    const int i = ty * 4 + tx;
                    xs[i] = static_cast<std::uint8_t>((n[0] * 0.5f + 0.5f) * 255.0f + 0.5f);
                    ys[i] = static_cast<std::uint8_t>((n[1] * 0.5f + 0.5f) * 255.0f + 0.5f);
                }
            }
            encodeBc4Block(xs, out.data() + offset);
            encodeBc4Block(ys, out.data() + offset + 8);
            offset += 16;
        }
    }
    return out;
}

}  // namespace ktxcmp::test
