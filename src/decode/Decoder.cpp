#include "decode/Decoder.hpp"

// The signed and float BC4/BC5 entry points. Without this, bcdec exposes only
// the 8-bit unsigned variants and SNORM would silently decode as UNORM
// (CLAUDE.md, trap 2).
#define BCDEC_BC4BC5_PRECISE
#define BCDEC_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)  // vendored header, narrowing by design
#endif
#include <bcdec.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <astcenc.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ktxcmp {
namespace {

// Blocks are scattered straight into the float Surface, so the block-padded
// region never exists as a buffer and cannot leak into a metric.
void scatterRgba8Block(Surface& out, const std::uint8_t* block, int blockX, int blockY) {
    for (int y = 0; y < 4; ++y) {
        const int ty = blockY + y;
        if (ty >= out.h)
            break;
        for (int x = 0; x < 4; ++x) {
            const int tx = blockX + x;
            if (tx >= out.w)
                continue;
            const std::uint8_t* src = block + (static_cast<std::size_t>(y) * 4 + x) * 4;
            float* dst = out.at(tx, ty);
            dst[0] = static_cast<float>(src[0]) / 255.0f;
            dst[1] = static_cast<float>(src[1]) / 255.0f;
            dst[2] = static_cast<float>(src[2]) / 255.0f;
            dst[3] = static_cast<float>(src[3]) / 255.0f;
        }
    }
}

// BC5 carries two channels. Blue and alpha are left at the neutral values a
// two-channel texture implies; normal-map reconstruction is M6's job and
// belongs above this layer, not inside the decoder.
void scatterRg2Block(Surface& out, const float* block, int blockX, int blockY) {
    for (int y = 0; y < 4; ++y) {
        const int ty = blockY + y;
        if (ty >= out.h)
            break;
        for (int x = 0; x < 4; ++x) {
            const int tx = blockX + x;
            if (tx >= out.w)
                continue;
            const float* src = block + (static_cast<std::size_t>(y) * 4 + x) * 2;
            float* dst = out.at(tx, ty);
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = 0.0f;
            dst[3] = 1.0f;
        }
    }
}

Result<Surface> decodeBc(const FormatId& format, std::span<const std::uint8_t> blocks, int w, int h,
                         int d) {
    Surface out;
    out.w = w;
    out.h = h;
    out.d = d;
    out.tf = format.transferFn;
    out.rgba.assign(out.texelCount() * 4u, 0.0f);

    const int blocksX = format.blocksAcross(w);
    const int blocksY = format.blocksDown(h);
    const std::size_t needed = static_cast<std::size_t>(blocksX) * blocksY * d * 16u;
    if (blocks.size() < needed)
        return fail(ErrorCode::Malformed,
                    "need " + std::to_string(needed) + " bytes of " + formatName(format) +
                        " blocks but only " + std::to_string(blocks.size()) + " are present");

    const std::uint8_t* src = blocks.data();
    for (int z = 0; z < d; ++z) {
        for (int by = 0; by < blocksY; ++by) {
            for (int bx = 0; bx < blocksX; ++bx, src += 16) {
                if (format.family == FormatFamily::Bc7) {
                    std::uint8_t texels[4 * 4 * 4];
                    bcdec_bc7(src, texels, 4 * 4);
                    scatterRgba8Block(out, texels, bx * 4, by * 4);
                } else {
                    float texels[4 * 4 * 2];
                    bcdec_bc5_float(src, texels, 4 * 2, format.isSigned ? 1 : 0);
                    scatterRg2Block(out, texels, bx * 4, by * 4);
                }
            }
        }
    }
    return out;
}

Result<Surface> decodeAstc(const FormatId& format, std::span<const std::uint8_t> blocks, int w,
                           int h, int d) {
    // Trap 1: the profile comes from the format id's sRGB flag. Guessing here
    // shifts every value and every metric derived from it.
    const astcenc_profile profile =
        format.transferFn == TransferFn::Srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;

    astcenc_config config{};
    astcenc_error rc = astcenc_config_init(
        profile, static_cast<unsigned>(format.blockW), static_cast<unsigned>(format.blockH), 1,
        ASTCENC_PRE_MEDIUM, ASTCENC_FLG_DECOMPRESS_ONLY, &config);
    if (rc != ASTCENC_SUCCESS)
        return fail(ErrorCode::Internal,
                    std::string("astcenc config failed: ") + astcenc_get_error_string(rc));

    astcenc_context* context = nullptr;
    rc = astcenc_context_alloc(&config, 1, &context, nullptr);
    if (rc != ASTCENC_SUCCESS || context == nullptr)
        return fail(ErrorCode::Internal,
                    std::string("astcenc context failed: ") + astcenc_get_error_string(rc));

    Surface out;
    out.w = w;
    out.h = h;
    out.d = d;
    out.tf = format.transferFn;
    out.rgba.assign(out.texelCount() * 4u, 0.0f);

    // astcenc writes dim_x by dim_y texels per slice and handles the block
    // overhang itself, so the crop is already done when it returns.
    std::vector<void*> slices(static_cast<std::size_t>(d));
    for (int z = 0; z < d; ++z)
        slices[static_cast<std::size_t>(z)] =
            out.rgba.data() + static_cast<std::size_t>(z) * w * h * 4u;

    astcenc_image image{};
    image.dim_x = static_cast<unsigned>(w);
    image.dim_y = static_cast<unsigned>(h);
    image.dim_z = static_cast<unsigned>(d);
    image.data_type = ASTCENC_TYPE_F32;
    image.data = slices.data();

    const astcenc_swizzle swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
    rc = astcenc_decompress_image(context, blocks.data(), blocks.size(), &image, &swizzle, 0);
    astcenc_context_free(context);

    if (rc != ASTCENC_SUCCESS)
        return fail(ErrorCode::Malformed,
                    std::string("astcenc decompress failed: ") + astcenc_get_error_string(rc));
    return out;
}

Result<Surface> decodeUncompressed(const FormatId& format, std::span<const std::uint8_t> blocks,
                                   int w, int h, int d) {
    Surface out;
    out.w = w;
    out.h = h;
    out.d = d;
    out.tf = format.transferFn;
    out.rgba.assign(out.texelCount() * 4u, 0.0f);

    const std::size_t texels = out.texelCount();
    const std::size_t needed = texels * static_cast<std::size_t>(format.bytesPerBlock);
    if (blocks.size() < needed)
        return fail(ErrorCode::Malformed,
                    "need " + std::to_string(needed) + " bytes but only " +
                        std::to_string(blocks.size()) + " are present");

    if (format.family == FormatFamily::Rgba16) {
        // 16-bit must not be truncated on the way in (CLAUDE.md, trap 5).
        const std::uint8_t* src = blocks.data();
        for (std::size_t i = 0; i < texels * 4u; ++i) {
            const std::uint16_t v = static_cast<std::uint16_t>(src[i * 2] |
                                                               (src[i * 2 + 1] << 8));
            out.rgba[i] = static_cast<float>(v) / 65535.0f;
        }
    } else {
        for (std::size_t i = 0; i < texels * 4u; ++i)
            out.rgba[i] = static_cast<float>(blocks[i]) / 255.0f;
    }
    return out;
}

}  // namespace

Result<Surface> decode(const FormatId& format, std::span<const std::uint8_t> blocks, int w, int h,
                       int d) {
    if (w <= 0 || h <= 0 || d <= 0)
        return fail(ErrorCode::Internal, "decode called with a non-positive dimension");

    switch (format.family) {
        case FormatFamily::Bc5:
        case FormatFamily::Bc7:
            return decodeBc(format, blocks, w, h, d);
        case FormatFamily::Astc:
            return decodeAstc(format, blocks, w, h, d);
        case FormatFamily::Rgba8:
        case FormatFamily::Rgba16:
            return decodeUncompressed(format, blocks, w, h, d);
    }
    return fail(ErrorCode::UnsupportedFormat, "no decoder for " + formatName(format));
}

}  // namespace ktxcmp
