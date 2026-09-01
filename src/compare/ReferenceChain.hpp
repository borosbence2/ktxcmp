#pragma once

// Matching a set of reference PNGs to mip levels by dimension.
//
// An explicit chain removes the filter ambiguity from compare mode 2: when the
// reference for a level was supplied rather than generated, its numbers no
// longer depend on which kernel we happened to downsample with (CLAUDE.md,
// Compare modes).
//
// Matching is by dimension and nothing else. Filenames are not parsed: a name
// like "_mip3" is a convention, not a fact, and trusting it would silently
// compare the wrong pair.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace ktxcmp {

struct Extent {
    int w = 0;
    int h = 0;
    friend bool operator==(const Extent&, const Extent&) = default;
};

struct ChainMatch {
    // Index into the file list for each level, or -1 where nothing matched.
    // A level without a reference is reported, never filled in with a generated
    // one (PLAN.md M7).
    std::vector<int> fileForLevel;

    // Files that matched no level at all.
    std::vector<int> unmatchedFiles;

    // Levels that got no file, and files whose size collides with another file.
    std::vector<int> unmatchedLevels;
    std::vector<std::string> problems;

    [[nodiscard]] bool complete() const { return unmatchedLevels.empty() && problems.empty(); }
    [[nodiscard]] std::size_t matchedCount() const;
};

[[nodiscard]] ChainMatch matchByDimension(const std::vector<Extent>& files,
                                          const std::vector<Extent>& levels);

}  // namespace ktxcmp
