// Container-layer harness. Hand-rolled rather than a framework (CLAUDE.md).
//
//   container_test --synthetic [dir]   generate a corpus and check it
//   container_test <file>...           report on and check real files
//
// The synthetic mode needs no committed binaries and no local textures, so CI
// runs the same checks this machine does. Exit code is the number of failures.

#include "Corpus.hpp"

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

int expectedMipCount(int w, int h) {
    int levels = 1;
    for (int d = (w > h ? w : h); d > 1; d /= 2)
        ++levels;
    return levels;
}

// The invariants PLAN.md M1 asks for, checked against our own arithmetic rather
// than against whatever libktx reported.
void checkInvariants(const ktxcmp::KtxFile& file, const std::string& tag) {
    const ktxcmp::KtxInfo& info = file.info();

    bool dimsOk = true, sizesOk = true, bytesOk = true;
    for (int level = 0; level < info.levelCount; ++level) {
        const ktxcmp::LevelInfo& li = info.levels[static_cast<std::size_t>(level)];
        const int ew = (info.baseWidth >> level) > 1 ? (info.baseWidth >> level) : 1;
        const int eh = (info.baseHeight >> level) > 1 ? (info.baseHeight >> level) : 1;
        if (li.w != ew || li.h != eh)
            dimsOk = false;
        if (li.imageBytes != info.format.imageBytes(li.w, li.h, li.d))
            sizesOk = false;
        auto bytes = file.levelBytes(level);
        if (!bytes || bytes->size() != li.imageBytes)
            bytesOk = false;
    }
    check(dimsOk, tag + ": levels follow max(1, base >> level)");
    check(sizesOk, tag + ": level sizes match the format's block layout");
    check(bytesOk, tag + ": level bytes are retrievable at the stated length");
    check(!file.levelBytes(info.levelCount).has_value(), tag + ": level past the end is rejected");
    check(!file.levelBytes(-1).has_value(), tag + ": negative level is rejected");
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

    const int expected = expectedMipCount(info.baseWidth, info.baseHeight);
    check(info.levelCount == expected,
          "level count " + std::to_string(info.levelCount) + " matches the expected " +
              std::to_string(expected));
    checkInvariants(file, "chain");
}

int runRealFiles(int argc, char** argv) {
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
            check(!shouldOpen, shouldOpen ? std::string("a .ktx file should have opened")
                                          : std::string("a non-KTX file is refused with a reason"));
            continue;
        }
        check(shouldOpen, "opened a file that claims to be a KTX container");
        report(*opened);
    }
    return g_failures;
}

int runSynthetic(const std::filesystem::path& dir) {
    std::printf("generating corpus in %s\n", dir.string().c_str());
    const auto expectations = ktxcmp::test::generateCorpus(dir);
    if (expectations.empty()) {
        std::printf("FAIL  the corpus generator produced nothing\n");
        return 1;
    }
    std::printf("%zu files\n\n", expectations.size());

    for (const auto& want : expectations) {
        std::printf("--- %s\n", want.filename.c_str());
        auto opened = ktxcmp::KtxFile::open(dir / want.filename);

        if (!want.shouldOpen) {
            if (opened) {
                check(false, want.filename + " should have been rejected but opened as " +
                                 opened->info().formatName);
                continue;
            }
            const std::string message = opened.error().message;
            std::printf("    rejected: %s\n", message.c_str());
            if (want.messageContains.empty())
                check(true, want.filename + " is rejected");
            else
                check(message.find(want.messageContains) != std::string::npos,
                      want.filename + " rejection names \"" + want.messageContains + "\"");
            continue;
        }

        if (!opened) {
            check(false, want.filename + " should have opened but was rejected: " +
                             opened.error().message);
            continue;
        }

        const ktxcmp::KtxInfo& info = opened->info();
        check(info.formatName == want.formatName,
              want.filename + " is " + want.formatName + " (got " + info.formatName + ")");
        check(info.baseWidth == want.width && info.baseHeight == want.height,
              want.filename + " is " + std::to_string(want.width) + "x" +
                  std::to_string(want.height));
        check(info.levelCount == want.levelCount,
              want.filename + " has " + std::to_string(want.levelCount) + " levels");
        check(info.version == want.version,
              want.filename + " is " + versionName(want.version));

        // KTX1 cannot know this, and must not pretend otherwise (trap 9).
        if (want.version == ktxcmp::ContainerVersion::Ktx1)
            check(!info.hasDfd && !info.premultiplied.has_value(),
                  want.filename + ": KTX1 reports no DFD and unknown premultiplied alpha");

        checkInvariants(*opened, want.filename);
    }
    return g_failures;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--synthetic") {
        const std::filesystem::path dir =
            argc >= 3 ? std::filesystem::path(argv[2])
                      : std::filesystem::temp_directory_path() / "ktxcmp_corpus";
        runSynthetic(dir);
    } else if (argc >= 2) {
        runRealFiles(argc, argv);
    } else {
        std::printf("usage: container_test --synthetic [dir]\n"
                    "       container_test <file>...\n");
        return 2;
    }

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
