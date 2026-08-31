// Synthetic test corpus.
//
// Built with libktx's own writer so the headers and DFDs are correct by
// construction, then deliberately damaged where a broken file is what we want
// to test. Generated at run time rather than committed: the files are derived
// data, and CI can then actually run the harness instead of only compiling it.
//
// The app itself never writes a file (CLAUDE.md, Scope). This is test tooling.

#include "Corpus.hpp"

#include <ktx.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace ktxcmp::test {
namespace {

// VkFormat / glInternalFormat values, same sources as container/FormatId.cpp.
constexpr std::uint32_t kVkRgba8Unorm   = 37;
constexpr std::uint32_t kVkRgba8Srgb    = 43;
constexpr std::uint32_t kVkRgba16Unorm  = 91;
constexpr std::uint32_t kVkBc1RgbUnorm  = 131;  // deliberately unsupported
constexpr std::uint32_t kVkBc5Unorm     = 141;
constexpr std::uint32_t kVkBc5Snorm     = 142;
constexpr std::uint32_t kVkBc7Unorm     = 145;
constexpr std::uint32_t kVkBc7Srgb      = 146;
constexpr std::uint32_t kVkAstc4x4Unorm = 157;
constexpr std::uint32_t kVkAstc6x6Srgb  = 166;

constexpr std::uint32_t kGlBc7Srgb      = 0x8E8D;
constexpr std::uint32_t kGlAstc6x6Srgb  = 0x93D4;
constexpr std::uint32_t kGlRgba8        = 0x8058;

int mipCount(int w, int h) {
    int levels = 1;
    for (int d = (w > h ? w : h); d > 1; d /= 2)
        ++levels;
    return levels;
}

// Deterministic filler. Content is irrelevant to the container layer, but a
// constant pattern makes a hex dump readable when something goes wrong.
void fillDeterministic(ktxTexture* texture) {
    ktx_uint8_t* data = ktxTexture_GetData(texture);
    const ktx_size_t size = ktxTexture_GetDataSize(texture);
    for (ktx_size_t i = 0; i < size; ++i)
        data[i] = static_cast<ktx_uint8_t>((i * 37u + 11u) & 0xFFu);
}

bool writeKtx2(const std::filesystem::path& path, std::uint32_t vkFormat, int w, int h,
               int levels) {
    ktxTextureCreateInfo ci{};
    ci.vkFormat = vkFormat;
    ci.baseWidth = static_cast<ktx_uint32_t>(w);
    ci.baseHeight = static_cast<ktx_uint32_t>(h);
    ci.baseDepth = 1;
    ci.numDimensions = 2;
    ci.numLevels = static_cast<ktx_uint32_t>(levels);
    ci.numLayers = 1;
    ci.numFaces = 1;
    ci.isArray = KTX_FALSE;
    ci.generateMipmaps = KTX_FALSE;

    ktxTexture2* texture = nullptr;
    if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        return false;
    fillDeterministic(ktxTexture(texture));
    const KTX_error_code rc =
        ktxTexture_WriteToNamedFile(ktxTexture(texture), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(texture));
    return rc == KTX_SUCCESS;
}

bool writeKtx1(const std::filesystem::path& path, std::uint32_t glInternalFormat, int w, int h,
               int levels) {
    ktxTextureCreateInfo ci{};
    ci.glInternalformat = glInternalFormat;
    ci.baseWidth = static_cast<ktx_uint32_t>(w);
    ci.baseHeight = static_cast<ktx_uint32_t>(h);
    ci.baseDepth = 1;
    ci.numDimensions = 2;
    ci.numLevels = static_cast<ktx_uint32_t>(levels);
    ci.numLayers = 1;
    ci.numFaces = 1;
    ci.isArray = KTX_FALSE;
    ci.generateMipmaps = KTX_FALSE;

    ktxTexture1* texture = nullptr;
    if (ktxTexture1_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &texture) != KTX_SUCCESS)
        return false;
    fillDeterministic(ktxTexture(texture));
    const KTX_error_code rc =
        ktxTexture_WriteToNamedFile(ktxTexture(texture), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(texture));
    return rc == KTX_SUCCESS;
}

std::vector<std::uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void writeAll(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

Expectation ok(std::string name, std::string format, int w, int h, int levels,
               ContainerVersion version) {
    return Expectation{std::move(name), true,  std::move(format), w,
                       h,               levels, version,          ""};
}

Expectation rejected(std::string name, std::string messageContains) {
    return Expectation{std::move(name), false, "", 0, 0, 0, ContainerVersion::Ktx2,
                       std::move(messageContains)};
}

}  // namespace

std::vector<Expectation> generateCorpus(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    std::vector<Expectation> expectations;
    auto ktx2 = [&](const char* name, std::uint32_t fmt, int w, int h, const char* formatName,
                    int levels = -1) {
        const int n = levels < 0 ? mipCount(w, h) : levels;
        if (writeKtx2(dir / name, fmt, w, h, n))
            expectations.push_back(ok(name, formatName, w, h, n, ContainerVersion::Ktx2));
    };
    auto ktx1 = [&](const char* name, std::uint32_t fmt, int w, int h, const char* formatName) {
        const int n = mipCount(w, h);
        if (writeKtx1(dir / name, fmt, w, h, n))
            expectations.push_back(ok(name, formatName, w, h, n, ContainerVersion::Ktx1));
    };

    // Every supported format, so a mapping typo cannot hide.
    ktx2("bc7_srgb_64.ktx2", kVkBc7Srgb, 64, 64, "BC7 sRGB");
    ktx2("bc7_unorm_64.ktx2", kVkBc7Unorm, 64, 64, "BC7 UNORM");
    ktx2("bc5_unorm_32.ktx2", kVkBc5Unorm, 32, 32, "BC5 UNORM");
    ktx2("bc5_snorm_32.ktx2", kVkBc5Snorm, 32, 32, "BC5 SNORM");
    ktx2("astc4x4_ldr_32.ktx2", kVkAstc4x4Unorm, 32, 32, "ASTC 4x4 LDR");
    ktx2("rgba8_unorm_16.ktx2", kVkRgba8Unorm, 16, 16, "RGBA8 UNORM");
    ktx2("rgba8_srgb_16.ktx2", kVkRgba8Srgb, 16, 16, "RGBA8 sRGB");
    ktx2("rgba16_unorm_8.ktx2", kVkRgba16Unorm, 8, 8, "RGBA16 UNORM");

    // 68x36 is neither a power of two nor a multiple of the 6x6 block, so the
    // block grid overhangs the logical edge on both axes.
    ktx2("astc6x6_npot_68x36.ktx2", kVkAstc6x6Srgb, 68, 36, "ASTC 6x6 sRGB");

    // A short chain is a legal file; M5 decides whether to complain about it.
    ktx2("bc7_single_level_64.ktx2", kVkBc7Srgb, 64, 64, "BC7 sRGB", 1);

    // KTX1, including the same awkward geometry.
    ktx1("bc7_srgb_64.ktx", kGlBc7Srgb, 64, 64, "BC7 sRGB");
    ktx1("astc6x6_npot_68x36.ktx", kGlAstc6x6Srgb, 68, 36, "ASTC 6x6 sRGB");
    ktx1("rgba8_16.ktx", kGlRgba8, 16, 16, "RGBA8 UNORM");

    // In scope for the container, out of scope for us: the message must name it.
    if (writeKtx2(dir / "bc1_unsupported.ktx2", kVkBc1RgbUnorm, 32, 32, mipCount(32, 32)))
        expectations.push_back(rejected("bc1_unsupported.ktx2", "BC1"));

    // Not a container at all.
    {
        std::vector<std::uint8_t> junk(256);
        for (std::size_t i = 0; i < junk.size(); ++i)
            junk[i] = static_cast<std::uint8_t>(i);
        writeAll(dir / "not_a_texture.ktx2", junk);
        expectations.push_back(rejected("not_a_texture.ktx2", "not a KTX file"));
    }

    // Valid identifier, body cut in half.
    {
        auto bytes = readAll(dir / "bc7_srgb_64.ktx2");
        if (bytes.size() > 64) {
            bytes.resize(bytes.size() / 2);
            writeAll(dir / "truncated.ktx2", bytes);
            expectations.push_back(rejected("truncated.ktx2", ""));
        }
    }

    // Header claims a bigger base level than the payload can hold. This is the
    // case the independent size check exists for: libktx is happy, our own
    // block arithmetic is not.
    {
        auto bytes = readAll(dir / "bc7_srgb_64.ktx2");
        if (bytes.size() > 32) {
            // KTX2 header: identifier(12) vkFormat(4) typeSize(4) then width.
            const std::uint32_t doubled = 128;
            std::memcpy(bytes.data() + 20, &doubled, sizeof(doubled));
            writeAll(dir / "lying_dimensions.ktx2", bytes);
            expectations.push_back(rejected("lying_dimensions.ktx2", ""));
        }
    }

    return expectations;
}

}  // namespace ktxcmp::test
