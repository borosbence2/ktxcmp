#pragma once

// Chain analysis on a worker. Mode 2 resamples the reference once per level and
// runs SSIM on each, which on a real chain is seconds, not milliseconds.
//
// Same shape as CompareService: one pending slot, because if the settings change
// mid-run the old answer is worthless.

#include "compare/ChainAnalysis.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace ktxcmp {

class ChainService {
public:
    ChainService();
    ~ChainService();

    ChainService(const ChainService&) = delete;
    ChainService& operator=(const ChainService&) = delete;

    void request(std::uint64_t token, ChainInput input);
    [[nodiscard]] std::optional<Result<ChainReport>> result(std::uint64_t token) const;
    [[nodiscard]] bool pending(std::uint64_t token) const;

private:
    void workerLoop();

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;

    bool m_hasPending = false;
    std::uint64_t m_pendingToken = 0;
    ChainInput m_pending;

    bool m_running = false;
    std::uint64_t m_runningToken = 0;

    std::uint64_t m_doneToken = 0;
    std::optional<Result<ChainReport>> m_done;

    bool m_stopping = false;
};

}  // namespace ktxcmp
