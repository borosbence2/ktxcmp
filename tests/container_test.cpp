// Container-layer harness. Hand-rolled rather than a framework (CLAUDE.md).
//
//   container_test <file>...
//
// Prints what the container layer made of each file and checks the invariants
// that PLAN.md M1 asks for. Exit code is the number of failures.

#include "container/KtxFile.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) {
        std::printf("    ok    %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("    FAIL  %s\n", what.c_str());
    }
}

const char* versionName(ktxcmp::ContainerVersion v) {
    return v == ktxcmp::ContainerVersion::Ktx1 ? "KTX1" : "KTX2";
}

void report(const ktxcmp::KtxFile& file) {
    const ktxcmp::KtxInfo& info = file.info();
    std::printf("  container   %s\n", versionName(info.version));
    std::printf("  format      %s\n", info.formatName.c_str());
    std::printf("  block       %dx%d, %d bytes, %d channels\n", info.format.blockW,
                info.format.blockH, info.format.bytesPerBlock, info.format.channels);
    std::printf("  transfer    %s\n",
                info.format.transferFn == ktxcmp::TransferFn::Srgb ? "sRGB" : "linear");
    std::printf("  dimensions  %dx%dx%d\n", info.baseWidth, info.baseHeight, info.baseDepth);
    std::printf("  levels      %d   layers %d   faces %d\n", info.levelCount, info.layerCount,
                info.faceCount);
    std::printf("  supercomp   %s%s\n", ktxcmp::supercompressionName(info.supercompression),
                info.needsTranscoding ? " (needs transcode)" : "");
    std::printf("  dfd         %s\n", info.hasDfd ? "present" : "absent");
    std::printf("  premul      %s\n",
                info.premultiplied.has_value() ? (*info.premultiplied ? "yes" : "no") : "unknown");
    std::printf("  data        %llu bytes\n", static_cast<unsigned long long>(info.dataSize));

    // Expected chain length for a complete mip pyramid.
    int expectedLevels = 1;
    for (int d = (info.baseWidth > info.baseHeight ? info.baseWidth : info.baseHeight); d > 1;
         d /= 2)
        ++expectedLevels;

    check(info.levelCount == expectedLevels,
          "level count " + std::to_string(info.levelCount) + " matches the expected " +
              std::to_string(expectedLevels) + " for " + std::to_string(info.baseWidth) + "x" +
              std::to_string(info.baseHeight));

    bool dimsOk = true;
    bool sizesOk = true;
    bool bytesOk = true;
    for (int level = 0; level < info.levelCount; ++level) {
        const ktxcmp::LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
        const int ew = info.baseWidth >> level > 1 ? info.baseWidth >> level : 1;
        const int eh = info.baseHeight >> level > 1 ? info.baseHeight >> level : 1;
        if (li.w != ew || li.h != eh)
            dimsOk = false;
        if (li.imageBytes != info.format.imageBytes(li.w, li.h, li.d))
            sizesOk = false;

        auto bytes = file.levelBytes(level);
        if (!bytes || bytes->size() != li.imageBytes)
            bytesOk = false;
    }
    check(dimsOk, "every level matches max(1, base >> level)");
    check(sizesOk, "every level size matches the format's block layout");
    check(bytesOk, "every level's bytes are retrievable at the stated length");

    // Out-of-range access must be refused, not clamped.
    check(!file.levelBytes(info.levelCount).has_value(), "level past the end is rejected");
    check(!file.levelBytes(-1).has_value(), "negative level is rejected");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: container_test <file>...\n");
        return 2;
    }

    for (int i = 1; i < argc; ++i) {
        const std::filesystem::path path(argv[i]);
        std::printf("\n=== %s\n", path.filename().string().c_str());

        std::string ext = path.extension().string();
        for (char& c : ext)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool shouldOpen = (ext == ".ktx" || ext == ".ktx2");

        auto opened = ktxcmp::KtxFile::open(path);
        if (!opened) {
            std::printf("  rejected: [%s] %s\n", ktxcmp::categoryName(opened.error().code),
                        opened.error().message.c_str());
            // Refusing a file that is not a KTX is the correct outcome, so it
            // counts as a passing check rather than a failure.
            check(!shouldOpen, shouldOpen ? std::string("a .ktx file should have opened")
                                          : std::string("a non-KTX file is refused with a reason"));
            continue;
        }
        check(shouldOpen, "opened a file that claims to be a KTX container");
        report(*opened);
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
