#pragma once

// Texel format identity, reachable from either container version.
//
// CLAUDE.md trap 9: KTX2 carries a VkFormat and KTX1 carries a glInternalFormat.
// Both converge here, and nothing above the container layer is allowed to care
// which file the texture came out of.

#include "core/Error.hpp"

#include <cstdint>
#include <string>

namespace ktxcmp {

// Recorded, never applied at decode time (CLAUDE.md, Surface rule 3).
enum class TransferFn { Linear, Srgb };

enum class FormatFamily { Astc, Bc5, Bc7, Rgba8, Rgba16 };

struct FormatId {
    FormatFamily family = FormatFamily::Rgba8;
    int blockW = 1;
    int blockH = 1;
    int bytesPerBlock = 4;
    int channels = 4;
    TransferFn transferFn = TransferFn::Srgb;
    bool isSigned = false;

    [[nodiscard]] bool isCompressed() const { return blockW > 1 || blockH > 1; }

    // Block grid covering w x h. Compressed levels are stored padded up to this,
    // which is exactly why decode has to crop back down (CLAUDE.md, trap 3).
    [[nodiscard]] int blocksAcross(int w) const { return (w + blockW - 1) / blockW; }
    [[nodiscard]] int blocksDown(int h) const { return (h + blockH - 1) / blockH; }

    // Stored size of one w x h x d image, block padding included.
    [[nodiscard]] std::uint64_t imageBytes(int w, int h, int d) const {
        return static_cast<std::uint64_t>(blocksAcross(w)) * blocksDown(h) *
               static_cast<std::uint64_t>(bytesPerBlock) * (d > 0 ? d : 1);
    }
};

[[nodiscard]] std::string formatName(const FormatId& id);

// Both name the offending format in the error message rather than printing a
// bare number, so an unsupported file says what it actually is (PLAN.md M1).
[[nodiscard]] Result<FormatId> formatFromVkFormat(std::uint32_t vkFormat);
[[nodiscard]] Result<FormatId> formatFromGlInternalFormat(std::uint32_t glInternalFormat);

}  // namespace ktxcmp
