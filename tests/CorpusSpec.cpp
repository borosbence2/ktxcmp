// The fixture spec: what exists and what it should produce. No libktx here, so
// the reading tests can link it (see Corpus.hpp).

#include "Corpus.hpp"

namespace ktxcmp::test {
namespace {

Expectation ok(const char* name, const char* format, int w, int h, int levels,
               ContainerVersion version) {
    Expectation e;
    e.filename = name;
    e.shouldOpen = true;
    e.formatName = format;
    e.width = w;
    e.height = h;
    e.levelCount = levels;
    e.version = version;
    return e;
}

Expectation rejected(const char* name, const char* messageContains) {
    Expectation e;
    e.filename = name;
    e.shouldOpen = false;
    e.messageContains = messageContains;
    return e;
}

int mips(int w, int h) {
    int levels = 1;
    for (int d = (w > h ? w : h); d > 1; d /= 2)
        ++levels;
    return levels;
}

}  // namespace

std::vector<Expectation> corpusExpectations() {
    const auto k2 = ContainerVersion::Ktx2;
    const auto k1 = ContainerVersion::Ktx1;
    return {
        // Every supported format, so a mapping typo cannot hide.
        ok("bc7_srgb_64.ktx2", "BC7 sRGB", 64, 64, mips(64, 64), k2),
        ok("bc7_unorm_64.ktx2", "BC7 UNORM", 64, 64, mips(64, 64), k2),
        ok("bc5_unorm_32.ktx2", "BC5 UNORM", 32, 32, mips(32, 32), k2),
        ok("bc5_snorm_32.ktx2", "BC5 SNORM", 32, 32, mips(32, 32), k2),
        ok("astc4x4_ldr_32.ktx2", "ASTC 4x4 LDR", 32, 32, mips(32, 32), k2),
        ok("rgba8_unorm_16.ktx2", "RGBA8 UNORM", 16, 16, mips(16, 16), k2),
        ok("rgba8_srgb_16.ktx2", "RGBA8 sRGB", 16, 16, mips(16, 16), k2),
        ok("rgba16_unorm_8.ktx2", "RGBA16 UNORM", 8, 8, mips(8, 8), k2),

        // 68x36 is neither a power of two nor a multiple of the 6x6 block, so
        // the block grid overhangs the logical edge on both axes.
        ok("astc6x6_npot_68x36.ktx2", "ASTC 6x6 sRGB", 68, 36, mips(68, 36), k2),

        // A short chain is a legal file; M5 decides whether to complain.
        ok("bc7_single_level_64.ktx2", "BC7 sRGB", 64, 64, 1, k2),

        // KTX1, including the same awkward geometry.
        ok("bc7_srgb_64.ktx", "BC7 sRGB", 64, 64, mips(64, 64), k1),
        ok("astc6x6_npot_68x36.ktx", "ASTC 6x6 sRGB", 68, 36, mips(68, 36), k1),
        ok("rgba8_16.ktx", "RGBA8 UNORM", 16, 16, mips(16, 16), k1),

        // Readable container, format we deliberately do not support.
        rejected("bc1_unsupported.ktx2", "BC1"),
        rejected("not_a_texture.ktx2", "not a KTX file"),
        rejected("truncated.ktx2", ""),
        rejected("lying_dimensions.ktx2", ""),
    };
}

std::vector<AstcFixture> astcFixtures() {
    return {
        {"roundtrip_66x36.ktx2", 66, 36},  // exact multiple of the 6x6 block
        {"roundtrip_68x37.ktx2", 68, 37},  // overhangs on both axes
    };
}

// A smooth gradient plus a hard edge, so both the easy and the awkward parts of
// a block encoder are represented.
std::vector<std::uint8_t> gradient(int w, int h) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = out.data() + (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = static_cast<std::uint8_t>(x * 255 / (w - 1));
            p[1] = static_cast<std::uint8_t>(y * 255 / (h - 1));
            p[2] = static_cast<std::uint8_t>((x > w / 2) ? 220 : 40);
            p[3] = 255;
        }
    }
    return out;
}

}  // namespace ktxcmp::test
