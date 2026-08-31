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

// Pure: no libktx, safe to link into a reader.
[[nodiscard]] std::vector<Expectation> corpusExpectations();
[[nodiscard]] std::vector<AstcFixture> astcFixtures();

// The image the ASTC fixtures are encoded from. Both the writer and the decode
// test build it, so neither has to trust a file to carry it.
[[nodiscard]] std::vector<std::uint8_t> gradient(int w, int h);

// Needs libktx's writer; only make_fixtures links this.
bool writeFixtures(const std::filesystem::path& dir);

}  // namespace ktxcmp::test
