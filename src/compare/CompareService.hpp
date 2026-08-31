#pragma once

// Metrics on a worker. SSIM alone is five Gaussian blurs over every texel, which
// on a 2048 square level is far past a frame, and M3 established that the UI
// thread does not do work like that.
//
// Only the newest request matters: if the user steps to another level while a
// comparison is running, the answer for the old one is worthless. So there is a
// single pending slot rather than a queue, and results are tagged with the token
// they were computed for.

#include "cache/ImageCache.hpp"
#include "compare/CompareEngine.hpp"
#include "compare/NormalMap.hpp"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace ktxcmp {

// One result type for both metric families. For a normal map PSNR is not
// reported at all (CLAUDE.md), so this is a choice between them rather than a
// struct carrying both sets of numbers.
struct MetricsResult {
    bool normalMode = false;
    CompareResult colour{};
    NormalMetrics normal{};
};

class CompareService {
public:
    CompareService();
    ~CompareService();

    CompareService(const CompareService&) = delete;
    CompareService& operator=(const CompareService&) = delete;

    // Cheap to call every frame: a token already computed or in flight is
    // ignored. A different token supersedes whatever was waiting.
    void request(std::uint64_t token, SurfacePtr a, SurfacePtr b, bool linearLight,
                 bool normalMode = false, bool isSigned = false);

    // The finished result, but only if it belongs to this token.
    [[nodiscard]] std::optional<Result<MetricsResult>> result(std::uint64_t token) const;
    [[nodiscard]] bool pending(std::uint64_t token) const;

private:
    void workerLoop();

    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;

    // Pending request, if any.
    bool m_hasPending = false;
    std::uint64_t m_pendingToken = 0;
    SurfacePtr m_pendingA;
    SurfacePtr m_pendingB;
    bool m_pendingLinear = false;
    bool m_pendingNormal = false;
    bool m_pendingSigned = false;

    std::uint64_t m_runningToken = 0;
    bool m_running = false;

    std::uint64_t m_doneToken = 0;
    std::optional<Result<MetricsResult>> m_done;

    bool m_stopping = false;
};

}  // namespace ktxcmp
