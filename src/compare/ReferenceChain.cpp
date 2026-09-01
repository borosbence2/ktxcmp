#include "compare/ReferenceChain.hpp"

namespace ktxcmp {

std::size_t ChainMatch::matchedCount() const {
    std::size_t n = 0;
    for (int f : fileForLevel)
        if (f >= 0)
            ++n;
    return n;
}

ChainMatch matchByDimension(const std::vector<Extent>& files, const std::vector<Extent>& levels) {
    ChainMatch out;
    out.fileForLevel.assign(levels.size(), -1);
    std::vector<bool> used(files.size(), false);

    auto describe = [](const Extent& e) {
        return std::to_string(e.w) + "x" + std::to_string(e.h);
    };

    for (std::size_t level = 0; level < levels.size(); ++level) {
        int found = -1;
        int duplicates = 0;
        for (std::size_t f = 0; f < files.size(); ++f) {
            if (files[f] != levels[level])
                continue;
            if (found < 0)
                found = static_cast<int>(f);
            else
                ++duplicates;
        }

        if (found < 0) {
            out.unmatchedLevels.push_back(static_cast<int>(level));
            continue;
        }
        if (duplicates > 0)
            out.problems.push_back("level " + std::to_string(level) + " at " +
                                   describe(levels[level]) + " matches " +
                                   std::to_string(duplicates + 1) +
                                   " files of the same size; the first was used");

        out.fileForLevel[level] = found;
        used[static_cast<std::size_t>(found)] = true;
    }

    for (std::size_t f = 0; f < files.size(); ++f)
        if (!used[f])
            out.unmatchedFiles.push_back(static_cast<int>(f));

    return out;
}

}  // namespace ktxcmp
