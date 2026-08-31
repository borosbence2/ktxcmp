#pragma once

// Worker pool for decode. The UI thread never blocks on one (CLAUDE.md,
// Conventions): it requests a subresource, and reads it out of the cache on some
// later frame.

#include "cache/ImageCache.hpp"
#include "container/KtxFile.hpp"
#include "core/Subresource.hpp"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace ktxcmp {

class DecodeService {
public:
    // Priority 0 is what the user is looking at; higher numbers wait.
    static constexpr int kVisible = 0;
    static constexpr int kThumbnail = 1;

    DecodeService(ImageCache& cache, unsigned threadCount);
    ~DecodeService();

    DecodeService(const DecodeService&) = delete;
    DecodeService& operator=(const DecodeService&) = delete;

    // The file is held by shared_ptr for the life of the job, so replacing a
    // slot's file cannot free the bytes a worker is still reading.
    void request(const SubresourceKey& key, std::shared_ptr<KtxFile> file, int priority);

    // Drops queued work for a slot. Jobs already running finish and are
    // discarded by the cache on the generation check.
    void cancel(Slot slot);

    [[nodiscard]] std::size_t queued() const;
    [[nodiscard]] unsigned threadCount() const { return static_cast<unsigned>(m_threads.size()); }

private:
    struct Job {
        SubresourceKey key;
        std::shared_ptr<KtxFile> file;
        std::uint64_t generation = 0;
        int priority = 0;
        std::uint64_t cost = 0;  // texels; smaller first within a priority
    };

    void workerLoop();
    bool takeJob(Job& out);

    ImageCache& m_cache;
    std::vector<std::thread> m_threads;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    std::vector<Job> m_queue;
    bool m_stopping = false;
};

}  // namespace ktxcmp
