#include "compare/CompareService.hpp"

#include <utility>

namespace ktxcmp {

CompareService::CompareService() {
    m_thread = std::thread([this] { workerLoop(); });
}

CompareService::~CompareService() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_hasPending = false;
        m_pendingA.reset();
        m_pendingB.reset();
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void CompareService::request(std::uint64_t token, SurfacePtr a, SurfacePtr b, bool linearLight) {
    if (!a || !b)
        return;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        if ((m_done.has_value() && m_doneToken == token) || (m_running && m_runningToken == token) ||
            (m_hasPending && m_pendingToken == token))
            return;

        m_hasPending = true;
        m_pendingToken = token;
        m_pendingA = std::move(a);
        m_pendingB = std::move(b);
        m_pendingLinear = linearLight;
    }
    m_wake.notify_one();
}

std::optional<Result<CompareResult>> CompareService::result(std::uint64_t token) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_done.has_value() || m_doneToken != token)
        return std::nullopt;
    return m_done;
}

bool CompareService::pending(std::uint64_t token) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return (m_running && m_runningToken == token) || (m_hasPending && m_pendingToken == token);
}

void CompareService::workerLoop() {
    for (;;) {
        SurfacePtr a, b;
        bool linear = false;
        std::uint64_t token = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || m_hasPending; });
            if (m_stopping)
                return;
            token = m_pendingToken;
            a = std::move(m_pendingA);
            b = std::move(m_pendingB);
            linear = m_pendingLinear;
            m_hasPending = false;
            m_running = true;
            m_runningToken = token;
        }

        Result<CompareResult> computed = compare(*a, *b, linear);

        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            // A newer request arrived while this ran, so this answer is already
            // stale; drop it rather than showing it for a frame.
            if (!m_hasPending || m_pendingToken == token) {
                m_doneToken = token;
                m_done = std::move(computed);
            }
        }
    }
}

}  // namespace ktxcmp
