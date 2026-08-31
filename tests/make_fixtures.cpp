// Writes the test fixtures. Separate binary because it links libktx's writer,
// which the reading tests deliberately do not (see Corpus.hpp).

#include "Corpus.hpp"

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
    const std::filesystem::path dir =
        argc >= 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::temp_directory_path() / "ktxcmp_fixtures";

    if (!ktxcmp::test::writeFixtures(dir)) {
        std::printf("FAILED to write fixtures into %s\n", dir.string().c_str());
        return 1;
    }
    std::printf("fixtures written to %s\n", dir.string().c_str());
    return 0;
}
