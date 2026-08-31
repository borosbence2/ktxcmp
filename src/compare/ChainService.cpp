#include "compare/ChainService.hpp"

#include <utility>

namespace ktxcmp {

ChainService::ChainService() {
    m_thread = std::thread([this] { workerLoop(); });
}

ChainService::~ChainService() {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_hasPending = false;
        m_pending = ChainInput{};
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void ChainService::request(std::uint64_t token, ChainInput input) {
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping)
            return;
        if ((m_done.has_value() && m_doneToken == token) ||
            (m_running && m_runningToken == token) || (m_hasPending && m_pendingToken == token))
            return;
        m_hasPending = true;
        m_pendingToken = token;
        m_pending = std::move(input);
    }
    m_wake.notify_one();
}

std::optional<Result<ChainReport>> ChainService::result(std::uint64_t token) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_done.has_value() || m_doneToken != token)
        return std::nullopt;
    return m_done;
}

bool ChainService::pending(std::uint64_t token) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return (m_running && m_runningToken == token) || (m_hasPending && m_pendingToken == token);
}

void ChainService::workerLoop() {
    for (;;) {
        ChainInput input;
        std::uint64_t token = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || m_hasPending; });
            if (m_stopping)
                return;
            token = m_pendingToken;
            input = std::move(m_pending);
            m_pending = ChainInput{};
            m_hasPending = false;
            m_running = true;
            m_runningToken = token;
        }

        Result<ChainReport> computed = analyseChain(input);

        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
            // Superseded while running: drop it rather than show a stale answer.
            if (!m_hasPending || m_pendingToken == token) {
                m_doneToken = token;
                m_done = std::move(computed);
            }
        }
    }
}

}  // namespace ktxcmp
