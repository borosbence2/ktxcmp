#pragma once

// Identity of one decodable subresource. The cache is keyed by this and nothing
// else, so two panels asking for "the same image" always agree.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ktxcmp {

enum class Slot { A, B };

struct SubresourceKey {
    Slot slot = Slot::A;
    int level = 0;
    int layer = 0;
    int face = 0;

    friend bool operator==(const SubresourceKey&, const SubresourceKey&) = default;
};

struct SubresourceKeyHash {
    std::size_t operator()(const SubresourceKey& k) const noexcept {
        // Every field is small and bounded, so packing them into one integer is
        // exact rather than a mix of independently hashed members.
        const std::uint64_t packed = (static_cast<std::uint64_t>(k.slot == Slot::B) << 48) |
                                     (static_cast<std::uint64_t>(k.face & 0xFFFF) << 32) |
                                     (static_cast<std::uint64_t>(k.layer & 0xFFFF) << 16) |
                                     static_cast<std::uint64_t>(k.level & 0xFFFF);
        return std::hash<std::uint64_t>{}(packed);
    }
};

}  // namespace ktxcmp
