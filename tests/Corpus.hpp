#pragma once

#include "container/KtxFile.hpp"

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

// Writes the corpus into dir and returns what each file should produce.
std::vector<Expectation> generateCorpus(const std::filesystem::path& dir);

}  // namespace ktxcmp::test
