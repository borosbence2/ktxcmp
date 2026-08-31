#pragma once

// Turning a float Surface into the bytes a monitor wants is per-texel work over
// the whole image: 117 ms for a 2048 square level, measured. That cannot happen
// on the UI thread (PLAN.md M3: the UI never blocks), and it is not decode work
// either - it depends on the channel mask and the transfer function, which are
// display concerns. So it gets its own small pool here in the UI layer.
//
// GL calls stay on the UI thread. This produces plain byte arrays.

#include "cache/ImageCache.hpp"
#include "core/Subresource.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ktxcmp::ui {

// A base image plus its box-filtered mip chain, ready for glTexImage2D.
struct DisplayImage {
    int w = 0;
    int h = 0;
    std::vector<std::vector<std::uint8_t>> mips;  // mips[0] is the base
};

using DisplayImagePtr = std::shared_ptr<const DisplayImage>;

struct DisplayKey {
    SubresourceKey subresource;
    std::uint32_t channels = 0;
    int maxSize = 0;   // 0 = full resolution
    int diffGain = 0;  // 0 = show the surface; >0 = show |A-B| amplified

    friend bool operator==(const DisplayKey&, const DisplayKey&) = default;
};

struct DisplayKeyHash {
    std::size_t operator()(const DisplayKey& k) const noexcept {
        const std::size_t a = SubresourceKeyHash{}(k.subresource);
        const std::uint64_t rest = (static_cast<std::uint64_t>(k.channels) << 40) |
                                   (static_cast<std::uint64_t>(k.maxSize & 0xFFFF) << 8) |
                                   static_cast<std::uint64_t>(k.diffGain & 0xFF);
        return a ^ (std::hash<std::uint64_t>{}(rest) * 0x9E3779B97F4A7C15ull);
    }
};

class DisplayCache {
public:
    explicit DisplayCache(unsigned threadCount);
    ~DisplayCache();

    DisplayCache(const DisplayCache&) = delete;
    DisplayCache& operator=(const DisplayCache&) = delete;

    // No-op if already built or in flight. The surface is held for the job's
    // lifetime, so cache eviction cannot pull it out from under a worker.
    // `other` is only used when key.diffGain > 0, where the image built is the
    // amplified absolute difference between the two.
    void request(const DisplayKey& key, SurfacePtr surface, int priority,
                 SurfacePtr other = nullptr);
    [[nodiscard]] DisplayImagePtr get(const DisplayKey& key) const;
    [[nodiscard]] bool pending(const DisplayKey& key) const;

    // Drops everything for a slot, for when its file is replaced.
    void invalidate(Slot slot);
    [[nodiscard]] std::size_t queued() const;

private:
    struct Job {
        DisplayKey key;
        SurfacePtr surface;
        SurfacePtr other;
        int priority = 0;
        std::uint64_t cost = 0;
    };

    void workerLoop();

    std::vector<std::thread> m_threads;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<Job> m_queue;
    std::unordered_map<DisplayKey, DisplayImagePtr, DisplayKeyHash> m_ready;
    std::unordered_map<DisplayKey, bool, DisplayKeyHash> m_inFlight;
    bool m_stopping = false;
};

}  // namespace ktxcmp::ui
