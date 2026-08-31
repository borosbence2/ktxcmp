// Decode-layer harness.
//
// The interesting test is the round trip: make_fixtures encodes a known
// gradient to ASTC with libktx's encoder, and this reads it back through our
// container layer and decodes it with our decoder. Picking the wrong astcenc
// profile (CLAUDE.md, trap 1) applies a whole transfer function to every value,
// so a round trip catches it where an eyeball would not.
//
// This binary links ktx_read, never the full libktx: the encoder inside libktx
// is another copy of astc-encoder and its symbols collide with ours.

#include "Corpus.hpp"

#include "container/KtxFile.hpp"
#include "decode/Decoder.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    if (!ok)
        ++g_failures;
}

using ktxcmp::FormatFamily;
using ktxcmp::FormatId;
using ktxcmp::Surface;
using ktxcmp::TransferFn;

FormatId fmt(FormatFamily family, int bw, int bh, int bpb, int channels, TransferFn tf,
             bool isSigned = false) {
    return FormatId{family, bw, bh, bpb, channels, tf, isSigned};
}

bool allFinite(const Surface& s) {
    for (float v : s.rgba)
        if (!std::isfinite(v))
            return false;
    return true;
}

// ---------------------------------------------------------- basic cases ----

void testUncompressedExact() {
    const int w = 5, h = 3;  // deliberately not a multiple of anything
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < src.size(); ++i)
        src[i] = static_cast<std::uint8_t>(i * 7 + 3);

    auto out = ktxcmp::decode(fmt(FormatFamily::Rgba8, 1, 1, 4, 4, TransferFn::Srgb), src, w, h);
    if (!out) {
        check(false, "RGBA8 decode: " + out.error().message);
        return;
    }
    check(out->w == w && out->h == h, "RGBA8 decodes to exactly 5x3");

    bool exact = true;
    for (std::size_t i = 0; i < src.size(); ++i)
        if (out->rgba[i] != static_cast<float>(src[i]) / 255.0f)
            exact = false;
    check(exact, "RGBA8 round-trips every byte exactly");
    check(out->tf == TransferFn::Srgb, "RGBA8 sRGB records its transfer function");
}

void testRgba16NotTruncated() {
    const int w = 2, h = 2;
    // Values that collide if either byte is dropped (CLAUDE.md, trap 5).
    const std::uint16_t values[] = {0x0101, 0x0100, 0xFFFF, 0x00FF};
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 4 * 2);
    for (std::size_t i = 0; i < src.size() / 2; ++i) {
        const std::uint16_t v = values[i % 4];
        src[i * 2] = static_cast<std::uint8_t>(v & 0xFF);
        src[i * 2 + 1] = static_cast<std::uint8_t>(v >> 8);
    }

    auto out = ktxcmp::decode(fmt(FormatFamily::Rgba16, 1, 1, 8, 4, TransferFn::Linear), src, w, h);
    if (!out) {
        check(false, "RGBA16 decode: " + out.error().message);
        return;
    }
    check(out->rgba[0] != out->rgba[1], "RGBA16 keeps 16 bits: 0x0101 and 0x0100 stay distinct");
    check(std::fabs(out->rgba[2] - 1.0f) < 1e-6f, "RGBA16 0xFFFF maps to 1.0");
}

void testBc5SignednessMatters() {
    std::vector<std::uint8_t> block(16);
    for (std::size_t i = 0; i < block.size(); ++i)
        block[i] = static_cast<std::uint8_t>(0x10 + i * 13);

    auto unorm =
        ktxcmp::decode(fmt(FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, false), block, 4, 4);
    auto snorm =
        ktxcmp::decode(fmt(FormatFamily::Bc5, 4, 4, 16, 2, TransferFn::Linear, true), block, 4, 4);
    if (!unorm || !snorm) {
        check(false, "BC5 decode failed");
        return;
    }

    bool differ = false, snormHasNegative = false, unormAllPositive = true;
    for (int i = 0; i < 4 * 4; ++i) {
        for (int c = 0; c < 2; ++c) {
            const float u = unorm->rgba[static_cast<std::size_t>(i) * 4 + c];
            const float s = snorm->rgba[static_cast<std::size_t>(i) * 4 + c];
            if (u != s)
                differ = true;
            if (s < 0.0f)
                snormHasNegative = true;
            if (u < 0.0f)
                unormAllPositive = false;
        }
    }
    check(differ, "BC5 SNORM and UNORM unpack the same bytes differently (trap 2)");
    check(snormHasNegative, "BC5 SNORM produces negative values");
    check(unormAllPositive, "BC5 UNORM produces no negative values");
    check(allFinite(*unorm) && allFinite(*snorm), "BC5 output is finite");
}

// ------------------------------------------------------- ASTC round trip ----

void testAstcRoundTrip(const std::filesystem::path& dir, const ktxcmp::test::AstcFixture& f) {
    const std::vector<std::uint8_t> source = ktxcmp::test::gradient(f.w, f.h);
    const std::string label = f.filename;

    auto file = ktxcmp::KtxFile::open(dir / f.filename);
    if (!file) {
        check(false, label + ": open failed: " + file.error().message);
        return;
    }
    check(file->info().format.family == FormatFamily::Astc &&
              file->info().format.transferFn == TransferFn::Srgb,
          label + ": comes back as ASTC sRGB (" + file->info().formatName + ")");

    auto bytes = file->levelBytes(0);
    if (!bytes) {
        check(false, label + ": no level bytes");
        return;
    }
    auto decoded = ktxcmp::decode(file->info().format, *bytes, f.w, f.h);
    if (!decoded) {
        check(false, label + ": decode failed: " + decoded.error().message);
        return;
    }

    check(decoded->w == f.w && decoded->h == f.h,
          label + ": decodes to exactly " + std::to_string(f.w) + "x" + std::to_string(f.h) +
              " (block padding cropped)");
    check(allFinite(*decoded), label + ": output is finite");

    double sum = 0.0, worst = 0.0;
    for (int y = 0; y < f.h; ++y) {
        for (int x = 0; x < f.w; ++x) {
            const std::uint8_t* s = source.data() + (static_cast<std::size_t>(y) * f.w + x) * 4;
            const float* t = decoded->at(x, y);
            for (int c = 0; c < 3; ++c) {
                const double e = std::fabs(static_cast<double>(s[c]) - t[c] * 255.0);
                sum += e;
                if (e > worst)
                    worst = e;
            }
        }
    }
    const double mae = sum / (static_cast<double>(f.w) * f.h * 3.0);
    std::printf("        mean abs error %.2f / 255, worst %.1f\n", mae, worst);

    // A correct 6x6 sRGB decode of this image lands within a couple of levels.
    // The wrong profile applies a transfer function to every texel and lands
    // tens of levels out, so this threshold separates the two decisively.
    check(mae < 6.0, label + ": mean abs error under 6/255");
}

void testAstcProfileFollowsTheFormat(const std::filesystem::path& dir,
                                     const ktxcmp::test::AstcFixture& f) {
    auto file = ktxcmp::KtxFile::open(dir / f.filename);
    if (!file) {
        check(false, "profile test: " + file.error().message);
        return;
    }
    auto bytes = file->levelBytes(0);
    if (!bytes) {
        check(false, "profile test: no bytes");
        return;
    }

    FormatId srgb = file->info().format;
    FormatId linear = srgb;
    linear.transferFn = TransferFn::Linear;

    auto a = ktxcmp::decode(srgb, *bytes, f.w, f.h);
    auto b = ktxcmp::decode(linear, *bytes, f.w, f.h);
    if (!a || !b) {
        check(false, "profile test: decode failed");
        return;
    }
    bool differ = false;
    for (std::size_t i = 0; i < a->rgba.size(); ++i)
        if (a->rgba[i] != b->rgba[i]) {
            differ = true;
            break;
        }
    check(differ, "ASTC profile follows the format's sRGB flag, not a constant (trap 1)");
}

// Every corpus file that our layer can open must also decode, with the right
// dimensions and no NaN or Inf reaching the output.
void testCorpusDecodes(const std::filesystem::path& dir) {
    for (const auto& want : ktxcmp::test::corpusExpectations()) {
        if (!want.shouldOpen)
            continue;
        auto file = ktxcmp::KtxFile::open(dir / want.filename);
        if (!file) {
            check(false, want.filename + ": open failed: " + file.error().message);
            continue;
        }
        const ktxcmp::KtxInfo& info = file->info();

        // These payloads are deterministic filler, not real encoded texels. Any
        // block format must still survive them and produce correctly sized
        // output, but only some can promise finite values: astcenc returns NaN
        // for an undecodable ASTC block, by documented design. BC and
        // uncompressed have no such escape hatch and must always be finite.
        const bool astc = info.format.family == FormatFamily::Astc;

        bool allOk = true;
        std::string why;
        std::size_t nonFinite = 0;
        for (int level = 0; level < info.levelCount; ++level) {
            const ktxcmp::LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
            auto bytes = file->levelBytes(level);
            if (!bytes) {
                allOk = false;
                why = "no bytes for level " + std::to_string(level);
                break;
            }
            auto surface = ktxcmp::decode(info.format, *bytes, li.w, li.h, li.d);
            if (!surface) {
                allOk = false;
                why = surface.error().message;
                break;
            }
            if (surface->w != li.w || surface->h != li.h) {
                allOk = false;
                why = "level " + std::to_string(level) + " decoded to the wrong size";
                break;
            }
            if (surface->rgba.size() != surface->texelCount() * 4u) {
                allOk = false;
                why = "level " + std::to_string(level) + " has the wrong buffer length";
                break;
            }
            for (float v : surface->rgba)
                if (!std::isfinite(v))
                    ++nonFinite;
            if (!astc && nonFinite != 0) {
                allOk = false;
                why = "level " + std::to_string(level) + " contains NaN or Inf";
                break;
            }
        }
        check(allOk, want.filename + ": every level decodes at the right size" +
                         (astc && nonFinite ? " (" + std::to_string(nonFinite) +
                                                  " NaN from invalid ASTC blocks, as designed)"
                                            : "") +
                         (allOk ? "" : " - " + why));
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: decode_test <fixture dir>\n");
        return 2;
    }
    const std::filesystem::path dir(argv[1]);

    std::printf("uncompressed\n");
    testUncompressedExact();
    testRgba16NotTruncated();

    std::printf("BC5\n");
    testBc5SignednessMatters();

    std::printf("ASTC round trip\n");
    const auto fixtures = ktxcmp::test::astcFixtures();
    for (const auto& f : fixtures)
        testAstcRoundTrip(dir, f);

    std::printf("ASTC profile\n");
    if (!fixtures.empty())
        testAstcProfileFollowsTheFormat(dir, fixtures.front());

    std::printf("corpus decode\n");
    testCorpusDecodes(dir);

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
