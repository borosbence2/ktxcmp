#include "cache/ImageCache.hpp"

#include <utility>

namespace ktxcmp {
namespace {

std::size_t slotIndex(Slot slot) {
    return slot == Slot::A ? 0u : 1u;
}

std::size_t surfaceBytes(const Surface& s) {
    return s.rgba.size() * sizeof(float);
}

}  // namespace

std::uint64_t ImageCache::invalidate(Slot slot) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const std::uint64_t next = ++m_generation[slotIndex(slot)];

    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->first.slot != slot) {
            ++it;
            continue;
        }
        m_bytes -= it->second.bytes;
        if (it->second.inLru)
            m_lru.erase(it->second.lru);
        it = m_entries.erase(it);
    }
    return next;
}

std::uint64_t ImageCache::generation(Slot slot) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_generation[slotIndex(slot)];
}

CacheState ImageCache::state(const SubresourceKey& key) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(key);
    return it == m_entries.end() ? CacheState::Missing : it->second.state;
}

SurfacePtr ImageCache::get(const SubresourceKey& key) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(key);
    if (it == m_entries.end() || it->second.state != CacheState::Ready)
        return nullptr;
    touchLocked(key, it->second);
    return it->second.surface;
}

std::optional<Error> ImageCache::error(const SubresourceKey& key) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return std::nullopt;
    return it->second.error;
}

bool ImageCache::beginDecode(const SubresourceKey& key, std::uint64_t generation) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (generation != m_generation[slotIndex(key.slot)])
        return false;

    auto [it, inserted] = m_entries.try_emplace(key);
    if (!inserted && it->second.state != CacheState::Missing)
        return false;  // ready, failed, or already in flight

    it->second.state = CacheState::Pending;
    it->second.generation = generation;
    return true;
}

void ImageCache::store(const SubresourceKey& key, std::uint64_t generation, SurfacePtr surface) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (generation != m_generation[slotIndex(key.slot)])
        return;  // the file was replaced while this was decoding

    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;

    Entry& entry = it->second;
    m_bytes -= entry.bytes;
    entry.state = CacheState::Ready;
    entry.surface = std::move(surface);
    entry.error.reset();
    entry.bytes = entry.surface ? surfaceBytes(*entry.surface) : 0;
    m_bytes += entry.bytes;
    touchLocked(key, entry);
    evictLocked();
}

void ImageCache::storeError(const SubresourceKey& key, std::uint64_t generation, Error error) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (generation != m_generation[slotIndex(key.slot)])
        return;

    const auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;

    Entry& entry = it->second;
    m_bytes -= entry.bytes;
    entry.bytes = 0;
    entry.surface.reset();
    entry.state = CacheState::Failed;
    entry.error = std::move(error);
}

void ImageCache::touchLocked(const SubresourceKey& key, Entry& entry) {
    if (entry.inLru)
        m_lru.erase(entry.lru);
    m_lru.push_front(key);
    entry.lru = m_lru.begin();
    entry.inLru = true;
}

void ImageCache::evictLocked() {
    // Walk back from the least recently used. Pending entries are skipped: a
    // worker is about to write to them, and dropping one would strand the job.
    auto it = m_lru.end();
    while (m_bytes > m_budget && it != m_lru.begin()) {
        --it;
        const auto found = m_entries.find(*it);
        if (found == m_entries.end()) {
            it = m_lru.erase(it);
            continue;
        }
        if (found->second.state == CacheState::Pending)
            continue;

        m_bytes -= found->second.bytes;
        m_entries.erase(found);
        it = m_lru.erase(it);
    }
}

std::size_t ImageCache::bytesUsed() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_bytes;
}

std::size_t ImageCache::entryCount() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
}

std::size_t ImageCache::pendingCount() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::size_t n = 0;
    for (const auto& [key, entry] : m_entries)
        if (entry.state == CacheState::Pending)
            ++n;
    return n;
}

}  // namespace ktxcmp
