#pragma once

// Test fixtures.
//
// Writing needs libktx's writer; reading must not link it, because libktx's
// full build also carries an ASTC encoder whose internal symbols collide with
// our decoder-only astc-encoder. So the declarations are split: the spec is
// pure data that any test can link, and the writer lives in its own binary.

#include "container/KtxFile.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ktxcmp::test {

// What the container layer is expected to make of one corpus file.
struct Expectation {
    std::string filename;
    bool shouldOpen = true;

    // Only meaningful when shouldOpen.
    std::string formatName;
    int width = 0;
    int height = 0;
    int levelCount = 0;
    ContainerVersion version = ContainerVersion::Ktx2;

    // Only meaningful when !shouldOpen. Empty means "any rejection will do".
    std::string messageContains;
};

// One ASTC file encoded from a known image, for the decode round trip.
struct AstcFixture {
    const char* filename;
    int w;
    int h;
};

// A PNG reference, written by our own minimal encoder so 8- and 16-bit paths
// are both exercised in CI without vendoring an image writer.
struct PngFixture {
    const char* filename;
    int w;
    int h;
    int bitDepth;  // 8 or 16
};

// Pure: no libktx, safe to link into a reader.
[[nodiscard]] std::vector<Expectation> corpusExpectations();
[[nodiscard]] std::vector<AstcFixture> astcFixtures();
[[nodiscard]] std::vector<PngFixture> pngFixtures();

// The 16-bit fixture stores values that collide if either byte is dropped, so a
// truncating loader cannot pass (CLAUDE.md, trap 5).
[[nodiscard]] std::vector<std::uint16_t> gradient16(int w, int h);

// The image the ASTC fixtures are encoded from. Both the writer and the decode
// test build it, so neither has to trust a file to carry it.
[[nodiscard]] std::vector<std::uint8_t> gradient(int w, int h);

// Needs libktx's writer; only make_fixtures links this.
bool writeFixtures(const std::filesystem::path& dir);

// PngWriter.cpp. No dependency beyond the standard library.
bool writePngFixtures(const std::filesystem::path& dir);

}  // namespace ktxcmp::test
