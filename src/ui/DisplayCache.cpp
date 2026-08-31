#include "ui/DisplayCache.hpp"

#include <algorithm>
#include <cmath>

namespace ktxcmp::ui {
namespace {

struct Channels {
    bool r, g, b, a;
};

Channels unpack(std::uint32_t key) {
    return Channels{(key & 1u) != 0, (key & 2u) != 0, (key & 4u) != 0, (key & 8u) != 0};
}

std::uint8_t toByte(float v) {
    if (!(v > 0.0f))  // also catches NaN
        return 0;
    if (v >= 1.0f)
        return 255;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

float linearToSrgb(float v) {
    if (!(v > 0.0f))
        return 0.0f;
    if (v <= 0.0031308f)
        return v * 12.92f;
    return 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

// Clamping happens here and only here: the Surface itself is never clamped
// (CLAUDE.md, rule 2).
std::vector<std::uint8_t> makeDisplayPixels(const Surface& s, const Channels& c) {
    std::vector<std::uint8_t> out(s.texelCount() * 4u, 255);

    const int rgbCount = (c.r ? 1 : 0) + (c.g ? 1 : 0) + (c.b ? 1 : 0);
    const bool alphaOnly = c.a && rgbCount == 0;
    const bool singleRgb = rgbCount == 1 && !c.a;
    const bool srgbEncode = s.tf == TransferFn::Linear;

    const std::size_t texels = s.texelCount();
    for (std::size_t i = 0; i < texels; ++i) {
        const float* src = s.rgba.data() + i * 4u;
        std::uint8_t* dst = out.data() + i * 4u;

        // An undecodable ASTC block decodes to NaN, by astcenc's design
        // (CLAUDE.md, trap 10). Show it rather than letting it become an
        // arbitrary colour, so a corrupt level looks wrong instead of plausible.
        if (!std::isfinite(src[0]) || !std::isfinite(src[1]) || !std::isfinite(src[2]) ||
            !std::isfinite(src[3])) {
            dst[0] = 255;
            dst[1] = 0;
            dst[2] = 255;
            dst[3] = 255;
            continue;
        }

        float v[4];
        for (int k = 0; k < 4; ++k)
            v[k] = srgbEncode ? linearToSrgb(src[k]) : src[k];

        if (alphaOnly) {
            const std::uint8_t g = toByte(v[3]);
            dst[0] = dst[1] = dst[2] = g;
        } else if (singleRgb) {
            const std::uint8_t g = toByte(c.r ? v[0] : (c.g ? v[1] : v[2]));
            dst[0] = dst[1] = dst[2] = g;
        } else {
            dst[0] = c.r ? toByte(v[0]) : 0;
            dst[1] = c.g ? toByte(v[1]) : 0;
            dst[2] = c.b ? toByte(v[2]) : 0;
        }
        dst[3] = 255;
    }
    return out;
}

// Box filter, halving both axes. Odd dimensions drop the last row or column;
// this is only ever a display mip, never a measured one.
std::vector<std::uint8_t> halve(const std::vector<std::uint8_t>& src, int w, int h, int& outW,
                                int& outH) {
    outW = w > 1 ? w / 2 : 1;
    outH = h > 1 ? h / 2 : 1;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(outW) * outH * 4);
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const int x0 = (w > 1) ? x * 2 : 0;
            const int y0 = (h > 1) ? y * 2 : 0;
            const int x1 = (w > 1) ? x0 + 1 : x0;
            const int y1 = (h > 1) ? y0 + 1 : y0;
            for (int c = 0; c < 4; ++c) {
                const int sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] +
                                src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                                src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] +
                                src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                out[(static_cast<std::size_t>(y) * outW + x) * 4 + c] =
                    static_cast<std::uint8_t>(sum / 4);
            }
        }
    }
    return out;
}

// The difference image the Diff view mode shows. Built here rather than as a
// separate Surface so the per-texel pass happens once, on this worker.
std::vector<std::uint8_t> makeDiffPixels(const Surface& a, const Surface& b, int gain,
                                         const Channels& c) {
    std::vector<std::uint8_t> out(a.texelCount() * 4u, 255);
    const float scale = static_cast<float>(gain > 0 ? gain : 1);
    const std::size_t texels = a.texelCount();
    for (std::size_t i = 0; i < texels; ++i) {
        const float* pa = a.rgba.data() + i * 4u;
        const float* pb = b.rgba.data() + i * 4u;
        std::uint8_t* dst = out.data() + i * 4u;

        float d[3];
        bool bad = false;
        for (int k = 0; k < 3; ++k) {
            d[k] = std::fabs(pa[k] - pb[k]) * scale;
            if (!std::isfinite(d[k]))
                bad = true;
        }
        if (bad) {
            dst[0] = 255;
            dst[1] = 0;
            dst[2] = 255;
            dst[3] = 255;
            continue;
        }
        dst[0] = c.r ? toByte(d[0]) : 0;
        dst[1] = c.g ? toByte(d[1]) : 0;
        dst[2] = c.b ? toByte(d[2]) : 0;
        dst[3] = 255;
    }
    return out;
}

DisplayImagePtr build(const Surface& surface, const Surface* other, std::uint32_t channelKey,
                      int maxSize, int diffGain) {
    auto image = std::make_shared<DisplayImage>();
    std::vector<std::uint8_t> level =
        (diffGain > 0 && other != nullptr)
            ? makeDiffPixels(surface, *other, diffGain, unpack(channelKey))
            : makeDisplayPixels(surface, unpack(channelKey));
    int w = surface.w;
    int h = surface.h;

    // Thumbnails are box-halved down rather than point sampled, so a 2048 square
    // level still shows what it actually contains at 96 pixels.
    while (maxSize > 0 && (w > maxSize || h > maxSize) && (w > 1 || h > 1)) {
        int nw = 0, nh = 0;
        level = halve(level, w, h, nw, nh);
        w = nw;
        h = nh;
    }

    image->w = w;
    image->h = h;
    for (;;) {
        image->mips.push_back(level);
        if (w == 1 && h == 1)
            break;
        int nw = 0, nh = 0;
        level = halve(level, w, h, nw, nh);
        w = nw;
        h = nh;
    }
    return image;
}

}  // namespace

DisplayCache::DisplayCache(unsigned threadCount) {
    const unsigned count = threadCount > 0 ? threadCount : 1;
    m_threads.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        m_threads.emplace_back([this] { workerLoop(); });
}

DisplayCache::~DisplayCache() {
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

void DisplayCache::request(const DisplayKey& key, SurfacePtr surface, int priority,
                           SurfacePtr other) {
    if (!surface)
        return;
    if (key.diffGain > 0 &&
        (!other || other->w != surface->w || other->h != surface->h))
        return;  // a diff of mismatched sizes is not a picture of anything
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping || m_ready.count(key) != 0 || m_inFlight.count(key) != 0)
            return;
        m_inFlight[key] = true;
        Job job;
        job.key = key;
        job.cost = static_cast<std::uint64_t>(surface->w) * surface->h;
        job.surface = std::move(surface);
        job.other = std::move(other);
        job.priority = priority;
        m_queue.push_back(std::move(job));
    }
    m_wake.notify_one();
}

DisplayImagePtr DisplayCache::get(const DisplayKey& key) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_ready.find(key);
    return it == m_ready.end() ? nullptr : it->second;
}

bool DisplayCache::pending(const DisplayKey& key) const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_inFlight.count(key) != 0;
}

void DisplayCache::invalidate(Slot slot) {
    const std::lock_guard<std::mutex> lock(m_mutex);
    std::erase_if(m_queue, [slot](const Job& j) { return j.key.subresource.slot == slot; });
    std::erase_if(m_ready, [slot](const auto& e) { return e.first.subresource.slot == slot; });
    // In-flight jobs are left marked so a late result is dropped rather than
    // stored against the new file.
}

std::size_t DisplayCache::queued() const {
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

void DisplayCache::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_queue.empty(); });
            if (m_stopping)
                return;
            // Same ordering as decode: what the user is looking at first, then
            // the cheapest, so the strip fills while the big one works.
            auto best =
                std::min_element(m_queue.begin(), m_queue.end(), [](const Job& a, const Job& b) {
                    if (a.priority != b.priority)
                        return a.priority < b.priority;
                    return a.cost < b.cost;
                });
            job = std::move(*best);
            m_queue.erase(best);
        }

        DisplayImagePtr image = build(*job.surface, job.other.get(), job.key.channels,
                                      job.key.maxSize, job.key.diffGain);

        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_inFlight.erase(job.key);
            if (!m_stopping)
                m_ready[job.key] = std::move(image);
        }
    }
}

}  // namespace ktxcmp::ui
