#pragma once

// LRU of decoded surfaces, keyed by subresource and budgeted in bytes
// (CLAUDE.md, Architecture).
//
// Shared between the UI thread and the decode workers, so every method locks.
// Surfaces are handed out as shared_ptr: eviction drops only the cache's own
// reference, so a surface being drawn this frame cannot vanish underneath it.

#include "core/Error.hpp"
#include "core/Subresource.hpp"
#include "core/Surface.hpp"

#include <cstddef>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace ktxcmp {

enum class CacheState { Missing, Pending, Ready, Failed };

class ImageCache {
public:
    explicit ImageCache(std::size_t budgetBytes) : m_budget(budgetBytes) {}

    // Bumping a slot's generation invalidates everything belonging to it. Work
    // already in flight finishes and is discarded on arrival, so closing a file
    // never has to interrupt a worker mid-decode.
    std::uint64_t invalidate(Slot slot);
    [[nodiscard]] std::uint64_t generation(Slot slot) const;

    [[nodiscard]] CacheState state(const SubresourceKey& key) const;
    [[nodiscard]] SurfacePtr get(const SubresourceKey& key);  // touches LRU order
    [[nodiscard]] std::optional<Error> error(const SubresourceKey& key) const;

    // False when the key is already present or already in flight, so callers do
    // not have to track that themselves.
    bool beginDecode(const SubresourceKey& key, std::uint64_t generation);
    void store(const SubresourceKey& key, std::uint64_t generation, SurfacePtr surface);
    void storeError(const SubresourceKey& key, std::uint64_t generation, Error error);

    [[nodiscard]] std::size_t bytesUsed() const;
    [[nodiscard]] std::size_t budget() const { return m_budget; }
    [[nodiscard]] std::size_t entryCount() const;
    [[nodiscard]] std::size_t pendingCount() const;

private:
    struct Entry {
        CacheState state = CacheState::Missing;
        SurfacePtr surface;
        std::optional<Error> error;
        std::uint64_t generation = 0;
        std::size_t bytes = 0;
        std::list<SubresourceKey>::iterator lru{};
        bool inLru = false;
    };

    void evictLocked();
    void touchLocked(const SubresourceKey& key, Entry& entry);

    mutable std::mutex m_mutex;
    std::unordered_map<SubresourceKey, Entry, SubresourceKeyHash> m_entries;
    std::list<SubresourceKey> m_lru;  // front = most recently used
    std::size_t m_budget = 0;
    std::size_t m_bytes = 0;
    std::uint64_t m_generation[2] = {1, 1};
};

}  // namespace ktxcmp
