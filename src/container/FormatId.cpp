#include "container/FormatId.hpp"

#include <array>

namespace ktxcmp {
namespace {

// Values from libktx's lib/vkformat_enum.h and the KHR GL enums. Copied rather
// than included: vkformat_enum.h is private to libktx, and these numbers are
// frozen by their respective specifications.
constexpr std::uint32_t kVkR8G8B8A8Unorm      = 37;
constexpr std::uint32_t kVkR8G8B8A8Srgb       = 43;
constexpr std::uint32_t kVkR16G16B16A16Unorm  = 91;
constexpr std::uint32_t kVkBc5UnormBlock      = 141;
constexpr std::uint32_t kVkBc5SnormBlock      = 142;
constexpr std::uint32_t kVkBc7UnormBlock      = 145;
constexpr std::uint32_t kVkBc7SrgbBlock       = 146;
constexpr std::uint32_t kVkAstc4x4UnormBlock  = 157;  // UNORM/SRGB interleaved

constexpr std::uint32_t kGlRgba8              = 0x8058;
constexpr std::uint32_t kGlSrgb8Alpha8        = 0x8C43;
constexpr std::uint32_t kGlRgba16             = 0x805B;
constexpr std::uint32_t kGlCompressedRgRgtc2  = 0x8DBD;  // BC5 UNORM
constexpr std::uint32_t kGlCompressedSignedRgRgtc2 = 0x8DBE;
constexpr std::uint32_t kGlCompressedRgbaBptcUnorm = 0x8E8C;
constexpr std::uint32_t kGlCompressedSrgbAlphaBptcUnorm = 0x8E8D;
constexpr std::uint32_t kGlAstcRgbaBase       = 0x93B0;  // 14 consecutive
constexpr std::uint32_t kGlAstcSrgbBase       = 0x93D0;  // 14 consecutive

// The 2D ASTC block sizes, in the order both Vulkan and the GL KHR enums use.
// One table drives both mappings; they cannot drift apart.
struct AstcBlock { int w, h; };
constexpr std::array<AstcBlock, 14> kAstcBlocks{{
    {4, 4}, {5, 4}, {5, 5}, {6, 5}, {6, 6}, {8, 5}, {8, 6},
    {8, 8}, {10, 5}, {10, 6}, {10, 8}, {10, 10}, {12, 10}, {12, 12},
}};

FormatId astc(int blockW, int blockH, TransferFn tf) {
    return FormatId{.family = FormatFamily::Astc,
                    .blockW = blockW,
                    .blockH = blockH,
                    .bytesPerBlock = 16,
                    .channels = 4,
                    .transferFn = tf,
                    .isSigned = false};
}

// Formats we can name but deliberately do not support, so the rejection message
// is useful instead of a bare hex value (CLAUDE.md, Scope).
const char* knownUnsupportedVk(std::uint32_t f) {
    switch (f) {
        case 131: case 132: return "BC1 (DXT1)";
        case 133: case 134: return "BC2 (DXT3)";
        case 135: case 136: return "BC3 (DXT5)";
        case 139: case 140: return "BC4";
        case 143: case 144: return "BC6H";
        case 147: case 148: return "ETC2 RGB";
        case 153: case 154: return "EAC R11";
        case 97:            return "RGBA16F (HDR)";
        default:            return nullptr;
    }
}

const char* knownUnsupportedGl(std::uint32_t f) {
    switch (f) {
        case 0x83F0: case 0x83F1: case 0x8C4C: case 0x8C4D: return "BC1 (DXT1)";
        case 0x83F2: case 0x8C4E: return "BC2 (DXT3)";
        case 0x83F3: case 0x8C4F: return "BC3 (DXT5)";
        case 0x8DBB: case 0x8DBC: return "BC4 (RGTC1)";
        case 0x8E8E: case 0x8E8F: return "BC6H (BPTC float)";
        case 0x9274: case 0x9275: return "ETC2 RGB";
        case 0x9270: case 0x9271: return "EAC R11";
        case 0x881A:            return "RGBA16F (HDR)";
        default:                return nullptr;
    }
}

std::string hex(std::uint32_t v) {
    static const char* digits = "0123456789ABCDEF";
    std::string s = "0x";
    bool leading = true;
    for (int shift = 28; shift >= 0; shift -= 4) {
        const int nibble = static_cast<int>((v >> shift) & 0xF);
        if (nibble != 0 || !leading || shift == 0) {
            s += digits[nibble];
            leading = false;
        }
    }
    return s;
}

std::string astcName(const FormatId& id) {
    return "ASTC " + std::to_string(id.blockW) + "x" + std::to_string(id.blockH) +
           (id.transferFn == TransferFn::Srgb ? " sRGB" : " LDR");
}

}  // namespace

std::string formatName(const FormatId& id) {
    switch (id.family) {
        case FormatFamily::Astc:   return astcName(id);
        case FormatFamily::Bc5:    return id.isSigned ? "BC5 SNORM" : "BC5 UNORM";
        case FormatFamily::Bc7:    return id.transferFn == TransferFn::Srgb ? "BC7 sRGB" : "BC7 UNORM";
        case FormatFamily::Rgba8:  return id.transferFn == TransferFn::Srgb ? "RGBA8 sRGB" : "RGBA8 UNORM";
        case FormatFamily::Rgba16: return "RGBA16 UNORM";
    }
    return "unknown";
}

Result<FormatId> formatFromVkFormat(std::uint32_t vkFormat) {
    // ASTC occupies one contiguous run, UNORM then SRGB per block size.
    constexpr std::uint32_t astcEnd =
        kVkAstc4x4UnormBlock + static_cast<std::uint32_t>(kAstcBlocks.size()) * 2;
    if (vkFormat >= kVkAstc4x4UnormBlock && vkFormat < astcEnd) {
        const std::uint32_t offset = vkFormat - kVkAstc4x4UnormBlock;
        const AstcBlock& block = kAstcBlocks[offset / 2];
        const TransferFn tf = (offset % 2) ? TransferFn::Srgb : TransferFn::Linear;
        return astc(block.w, block.h, tf);
    }

    switch (vkFormat) {
        case kVkBc5UnormBlock:
            return FormatId{FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, false};
        case kVkBc5SnormBlock:
            return FormatId{FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, true};
        case kVkBc7UnormBlock:
            return FormatId{FormatFamily::Bc7, 4, 4, 16, 4, TransferFn::Linear, false};
        case kVkBc7SrgbBlock:
            return FormatId{FormatFamily::Bc7, 4, 4, 16, 4, TransferFn::Srgb, false};
        case kVkR8G8B8A8Unorm:
            return FormatId{FormatFamily::Rgba8, 1, 1, 4, 4, TransferFn::Linear, false};
        case kVkR8G8B8A8Srgb:
            return FormatId{FormatFamily::Rgba8, 1, 1, 4, 4, TransferFn::Srgb, false};
        case kVkR16G16B16A16Unorm:
            return FormatId{FormatFamily::Rgba16, 1, 1, 8, 4, TransferFn::Linear, false};
        default:
            break;
    }

    if (const char* name = knownUnsupportedVk(vkFormat))
        return fail(ErrorCode::UnsupportedFormat,
                    std::string(name) + " is outside the supported format set (VkFormat " +
                        std::to_string(vkFormat) + ")");
    return fail(ErrorCode::UnsupportedFormat,
                "unsupported VkFormat " + std::to_string(vkFormat));
}

Result<FormatId> formatFromGlInternalFormat(std::uint32_t glInternalFormat) {
    constexpr std::uint32_t count = static_cast<std::uint32_t>(kAstcBlocks.size());
    if (glInternalFormat >= kGlAstcRgbaBase && glInternalFormat < kGlAstcRgbaBase + count) {
        const AstcBlock& block = kAstcBlocks[glInternalFormat - kGlAstcRgbaBase];
        return astc(block.w, block.h, TransferFn::Linear);
    }
    if (glInternalFormat >= kGlAstcSrgbBase && glInternalFormat < kGlAstcSrgbBase + count) {
        const AstcBlock& block = kAstcBlocks[glInternalFormat - kGlAstcSrgbBase];
        return astc(block.w, block.h, TransferFn::Srgb);
    }

    switch (glInternalFormat) {
        case kGlCompressedRgRgtc2:
            return FormatId{FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, false};
        case kGlCompressedSignedRgRgtc2:
            return FormatId{FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, true};
        case kGlCompressedRgbaBptcUnorm:
            return FormatId{FormatFamily::Bc7, 4, 4, 16, 4, TransferFn::Linear, false};
        case kGlCompressedSrgbAlphaBptcUnorm:
            return FormatId{FormatFamily::Bc7, 4, 4, 16, 4, TransferFn::Srgb, false};
        case kGlRgba8:
            return FormatId{FormatFamily::Rgba8, 1, 1, 4, 4, TransferFn::Linear, false};
        case kGlSrgb8Alpha8:
            return FormatId{FormatFamily::Rgba8, 1, 1, 4, 4, TransferFn::Srgb, false};
        case kGlRgba16:
            return FormatId{FormatFamily::Rgba16, 1, 1, 8, 4, TransferFn::Linear, false};
        default:
            break;
    }

    if (const char* name = knownUnsupportedGl(glInternalFormat))
        return fail(ErrorCode::UnsupportedFormat,
                    std::string(name) + " is outside the supported format set (glInternalFormat " +
                        hex(glInternalFormat) + ")");
    return fail(ErrorCode::UnsupportedFormat,
                "unsupported glInternalFormat " + hex(glInternalFormat));
}

}  // namespace ktxcmp
