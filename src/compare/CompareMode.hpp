#pragma once

namespace ktxcmp {

// CLAUDE.md, Compare modes.
enum class CompareMode {
    EncodeFidelity,    // 1: KTX mip 0 vs reference, no resampling
    ChainVsReference,  // 2: reference downsampled to each level
    SelfConsistency,   // 3: KTX level N-1 downsampled x2 vs level N
};

[[nodiscard]] const char* compareModeName(CompareMode m) noexcept;

}  // namespace ktxcmp
