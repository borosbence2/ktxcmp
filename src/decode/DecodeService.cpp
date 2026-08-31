#include "decode/DecodeService.hpp"

#include "decode/Decoder.hpp"

#include <algorithm>
#include <utility>

namespace ktxcmp {

DecodeService::DecodeService(ImageCache& cache, unsigned threadCount) : m_cache(cache) {
    const unsigned count = threadCount > 0 ? threadCount : 1;
    m_threads.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        m_threads.emplace_back([this] { workerLoop(); });
}

DecodeService::~DecodeService() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_queue.clear();
    }
    m_wake.notify_all();
    for (std::thread& t : m_threads)
        if (t.joinable())
            t.join();
}

void DecodeService::request(const SubresourceKey& key, std::shared_ptr<KtxFile> file,
                            int priority) {
    if (!file)
        return;

    const std::uint64_t generation = m_cache.generation(key.slot);
    // beginDecode both reserves the slot and tells us whether anyone already
    // has, so the queue cannot accumulate duplicates of the same key.
    if (!m_cache.beginDecode(key, generation))
        return;

    const KtxInfo& info = file->info();
    if (key.level < 0 || key.level >= info.levelCount) {
        m_cache.storeError(key, generation,
                           Error{ErrorCode::Internal, "level out of range for this file"});
        return;
    }
    const LevelInfo& li = info.levels[static_cast<std::size_t>(key.level)];

    Job job;
    job.key = key;
    job.file = std::move(file);
    job.generation = generation;
    job.priority = priority;
    job.cost = static_cast<std::uint64_t>(li.w) * li.h * li.d;

    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        m_queue.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void DecodeService::cancel(Slot slot) {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        std::erase_if(m_queue, [slot](const Job& j) { return j.key.slot == slot; });
    }
    m_wake.notify_all();
}

bool DecodeService::takeJob(Job& out) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
    if (m_stopping)
        return false;

    // Lowest priority number first, then smallest image. That is what makes the
    // mip strip fill from the cheap end while level 0 is still decoding
    // (PLAN.md M3, progressive population).
    auto best = std::min_element(m_queue.begin(), m_queue.end(), [](const Job& a, const Job& b) {
        if (a.priority != b.priority)
            return a.priority < b.priority;
        return a.cost < b.cost;
    });
    out = std::move(*best);
    m_queue.erase(best);
    return true;
}

void DecodeService::workerLoop() {
    for (;;) {
        Job job;
        if (!takeJob(job))
            return;

        const KtxInfo& info = job.file->info();
        auto bytes = job.file->levelBytes(job.key.level, job.key.layer, job.key.face);
        if (!bytes) {
            m_cache.storeError(job.key, job.generation, bytes.error());
            continue;
        }

        const LevelInfo& li = info.levels[static_cast<std::size_t>(job.key.level)];
        auto decoded = decode(info.format, *bytes, li.w, li.h, li.d);
        if (!decoded) {
            m_cache.storeError(job.key, job.generation, decoded.error());
            continue;
        }
        decoded->premultiplied = info.premultiplied.value_or(false);
        m_cache.store(job.key, job.generation,
                      std::make_shared<const Surface>(std::move(*decoded)));
    }
}

std::size_t DecodeService::queued() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

}  // namespace ktxcmp
