// Writes the fixtures with libktx's own writer, so headers and DFDs are correct
// by construction, then deliberately damages the files that are meant to be
// broken. Generated at run time rather than committed: derived data.
//
// The app itself never writes a file (CLAUDE.md, Scope). This is test tooling,
// and it is the only binary that links the full libktx.

#include "Corpus.hpp"

#include <ktx.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

namespace ktxcmp::test {
namespace {

constexpr std::uint32_t kVkRgba8Unorm   = 37;
constexpr std::uint32_t kVkRgba8Srgb    = 43;
constexpr std::uint32_t kVkRgba16Unorm  = 91;
constexpr std::uint32_t kVkBc1RgbUnorm  = 131;
constexpr std::uint32_t kVkBc5Unorm     = 141;
constexpr std::uint32_t kVkBc5Snorm     = 142;
constexpr std::uint32_t kVkBc7Unorm     = 145;
constexpr std::uint32_t kVkBc7Srgb      = 146;
constexpr std::uint32_t kVkAstc4x4Unorm = 157;
constexpr std::uint32_t kVkAstc6x6Srgb  = 166;

constexpr std::uint32_t kGlBc7Srgb     = 0x8E8D;
constexpr std::uint32_t kGlAstc6x6Srgb = 0x93D4;
constexpr std::uint32_t kGlRgba8       = 0x8058;

void fillDeterministic(ktxTexture* texture) {
    ktx_uint8_t* data = ktxTexture_GetData(texture);
    const ktx_size_t size = ktxTexture_GetDataSize(texture);
    for (ktx_size_t i = 0; i < size; ++i)
        data[i] = static_cast<ktx_uint8_t>((i * 37u + 11u) & 0xFFu);
}

ktxTextureCreateInfo makeInfo(int w, int h, int levels) {
    ktxTextureCreateInfo ci{};
    ci.baseWidth = static_cast<ktx_uint32_t>(w);
    ci.baseHeight = static_cast<ktx_uint32_t>(h);
    ci.baseDepth = 1;
    ci.numDimensions = 2;
    ci.numLevels = static_cast<ktx_uint32_t>(levels);
    ci.numLayers = 1;
    ci.numFaces = 1;
    ci.isArray = KTX_FALSE;
    ci.generateMipmaps = KTX_FALSE;
    return ci;
}

bool writeKtx2(const std::filesystem::path& path, std::uint32_t vkFormat, int w, int h,
               int levels) {
    ktxTextureCreateInfo ci = makeInfo(w, h, levels);
    ci.vkFormat = vkFormat;
    ktxTexture2* t = nullptr;
    if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &t) != KTX_SUCCESS)
        return false;
    fillDeterministic(ktxTexture(t));
    const KTX_error_code rc = ktxTexture_WriteToNamedFile(ktxTexture(t), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(t));
    return rc == KTX_SUCCESS;
}

bool writeKtx1(const std::filesystem::path& path, std::uint32_t glFormat, int w, int h,
               int levels) {
    ktxTextureCreateInfo ci = makeInfo(w, h, levels);
    ci.glInternalformat = glFormat;
    ktxTexture1* t = nullptr;
    if (ktxTexture1_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &t) != KTX_SUCCESS)
        return false;
    fillDeterministic(ktxTexture(t));
    const KTX_error_code rc = ktxTexture_WriteToNamedFile(ktxTexture(t), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(t));
    return rc == KTX_SUCCESS;
}

// Encodes the shared gradient to ASTC 6x6 sRGB. The decode test compares its
// own copy of that gradient against what our decoder produces, which is what
// makes the ASTC profile choice (trap 1) testable rather than assumed.
bool writeAstcRoundTrip(const std::filesystem::path& path, int w, int h) {
    ktxTextureCreateInfo ci = makeInfo(w, h, 1);
    ci.vkFormat = kVkRgba8Srgb;

    ktxTexture2* t = nullptr;
    if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &t) != KTX_SUCCESS)
        return false;

    const std::vector<std::uint8_t> source = gradient(w, h);
    std::memcpy(ktxTexture_GetData(ktxTexture(t)), source.data(), source.size());

    ktxAstcParams params{};
    params.structSize = sizeof(params);
    params.blockDimension = KTX_PACK_ASTC_BLOCK_DIMENSION_6x6;
    params.mode = KTX_PACK_ASTC_ENCODER_MODE_LDR;
    params.qualityLevel = KTX_PACK_ASTC_QUALITY_LEVEL_THOROUGH;
    params.threadCount = 1;

    if (ktxTexture2_CompressAstcEx(t, &params) != KTX_SUCCESS) {
        ktxTexture_Destroy(ktxTexture(t));
        return false;
    }
    const KTX_error_code rc = ktxTexture_WriteToNamedFile(ktxTexture(t), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(t));
    return rc == KTX_SUCCESS;
}

float srgbToLinearF(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgbF(float v) {
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// A black-and-white checkerboard with a full mip chain built either the wrong
// way (averaging sRGB values directly) or the right way (averaging in linear
// light). The two differ by about sixty units at every level below the base.
bool writeCheckerChain(const std::filesystem::path& path, int size, bool linearLight) {
    int levels = 1;
    for (int d = size; d > 1; d /= 2)
        ++levels;

    ktxTextureCreateInfo ci = makeInfo(size, size, levels);
    ci.vkFormat = kVkRgba8Srgb;
    ktxTexture2* t = nullptr;
    if (ktxTexture2_Create(&ci, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &t) != KTX_SUCCESS)
        return false;

    std::vector<std::uint8_t> level(static_cast<std::size_t>(size) * size * 4);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            std::uint8_t* p = level.data() + (static_cast<std::size_t>(y) * size + x) * 4;
            const std::uint8_t v = ((x + y) % 2 == 0) ? 255 : 0;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }

    int w = size, h = size;
    for (int i = 0; i < levels; ++i) {
        ktx_size_t offset = 0;
        if (ktxTexture_GetImageOffset(ktxTexture(t), static_cast<ktx_uint32_t>(i), 0, 0,
                                      &offset) != KTX_SUCCESS) {
            ktxTexture_Destroy(ktxTexture(t));
            return false;
        }
        std::memcpy(ktxTexture_GetData(ktxTexture(t)) + offset, level.data(), level.size());

        if (w == 1 && h == 1)
            break;
        const int nw = w > 1 ? w / 2 : 1;
        const int nh = h > 1 ? h / 2 : 1;
        std::vector<std::uint8_t> next(static_cast<std::size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    const int x0 = (w > 1) ? x * 2 : 0, x1 = (w > 1) ? x0 + 1 : x0;
                    const int y0 = (h > 1) ? y * 2 : 0, y1 = (h > 1) ? y0 + 1 : y0;
                    const std::uint8_t s0 = level[(static_cast<std::size_t>(y0) * w + x0) * 4 + c];
                    const std::uint8_t s1 = level[(static_cast<std::size_t>(y0) * w + x1) * 4 + c];
                    const std::uint8_t s2 = level[(static_cast<std::size_t>(y1) * w + x0) * 4 + c];
                    const std::uint8_t s3 = level[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                    float avg;
                    if (linearLight && c < 3) {
                        avg = (srgbToLinearF(s0 / 255.0f) + srgbToLinearF(s1 / 255.0f) +
                               srgbToLinearF(s2 / 255.0f) + srgbToLinearF(s3 / 255.0f)) /
                              4.0f;
                        avg = linearToSrgbF(avg);
                    } else {
                        avg = (s0 + s1 + s2 + s3) / 4.0f / 255.0f;
                    }
                    next[(static_cast<std::size_t>(y) * nw + x) * 4 + c] =
                        static_cast<std::uint8_t>(avg * 255.0f + 0.5f);
                }
        level.swap(next);
        w = nw;
        h = nh;
    }

    const KTX_error_code rc = ktxTexture_WriteToNamedFile(ktxTexture(t), path.string().c_str());
    ktxTexture_Destroy(ktxTexture(t));
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

// Keyed by the filenames the spec declares, so the two cannot drift apart
// silently: writeFixtures checks that every expected file was produced.
struct Recipe {
    std::uint32_t format;
    int w, h, levels;
    bool ktx1;
};

const std::map<std::string, Recipe>& recipes() {
    static const std::map<std::string, Recipe> table = {
        {"bc7_srgb_64.ktx2",          {kVkBc7Srgb, 64, 64, 7, false}},
        {"bc7_unorm_64.ktx2",         {kVkBc7Unorm, 64, 64, 7, false}},
        {"bc5_unorm_32.ktx2",         {kVkBc5Unorm, 32, 32, 6, false}},
        {"bc5_snorm_32.ktx2",         {kVkBc5Snorm, 32, 32, 6, false}},
        {"astc4x4_ldr_32.ktx2",       {kVkAstc4x4Unorm, 32, 32, 6, false}},
        {"rgba8_unorm_16.ktx2",       {kVkRgba8Unorm, 16, 16, 5, false}},
        {"rgba8_srgb_16.ktx2",        {kVkRgba8Srgb, 16, 16, 5, false}},
        {"rgba16_unorm_8.ktx2",       {kVkRgba16Unorm, 8, 8, 4, false}},
        {"astc6x6_npot_68x36.ktx2",   {kVkAstc6x6Srgb, 68, 36, 7, false}},
        {"bc7_single_level_64.ktx2",  {kVkBc7Srgb, 64, 64, 1, false}},
        {"bc1_unsupported.ktx2",      {kVkBc1RgbUnorm, 32, 32, 6, false}},
        {"bc7_srgb_64.ktx",           {kGlBc7Srgb, 64, 64, 7, true}},
        {"astc6x6_npot_68x36.ktx",    {kGlAstc6x6Srgb, 68, 36, 7, true}},
        {"rgba8_16.ktx",              {kGlRgba8, 16, 16, 5, true}},
    };
    return table;
}

}  // namespace

bool writeFixtures(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    for (const auto& [name, r] : recipes()) {
        const bool ok = r.ktx1 ? writeKtx1(dir / name, r.format, r.w, r.h, r.levels)
                               : writeKtx2(dir / name, r.format, r.w, r.h, r.levels);
        if (!ok)
            return false;
    }

    // Not a container at all.
    {
        std::vector<std::uint8_t> junk(256);
        for (std::size_t i = 0; i < junk.size(); ++i)
            junk[i] = static_cast<std::uint8_t>(i);
        writeAll(dir / "not_a_texture.ktx2", junk);
    }

    // Valid identifier, body cut in half.
    {
        auto bytes = readAll(dir / "bc7_srgb_64.ktx2");
        if (bytes.size() <= 64)
            return false;
        bytes.resize(bytes.size() / 2);
        writeAll(dir / "truncated.ktx2", bytes);
    }

    // Header claims a wider base level than the payload can hold. KTX2 layout:
    // identifier(12) vkFormat(4) typeSize(4) then pixelWidth.
    {
        auto bytes = readAll(dir / "bc7_srgb_64.ktx2");
        if (bytes.size() <= 32)
            return false;
        const std::uint32_t doubled = 128;
        std::memcpy(bytes.data() + 20, &doubled, sizeof(doubled));
        writeAll(dir / "lying_dimensions.ktx2", bytes);
    }

    for (const AstcFixture& f : astcFixtures())
        if (!writeAstcRoundTrip(dir / f.filename, f.w, f.h))
            return false;

    if (!writePngFixtures(dir))
        return false;

    if (!writeCheckerChain(dir / kGammaMipsFixture, kCheckerSize, false))
        return false;
    if (!writeCheckerChain(dir / kLinearMipsFixture, kCheckerSize, true))
        return false;

    // Every file the spec promises must now exist, or the two have drifted.
    for (const Expectation& e : corpusExpectations())
        if (!std::filesystem::exists(dir / e.filename))
            return false;
    return true;
}

}  // namespace ktxcmp::test
