// ImageCache and DecodeService harness. No files and no window: this is pure
// concurrency and bookkeeping, which is exactly the kind of thing that misbehaves
// quietly.

#include "cache/ImageCache.hpp"
#include "decode/DecodeService.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf(ok ? "  ok    %s\n" : "  FAIL  %s\n", what.c_str());
    if (!ok)
        ++g_failures;
}

using ktxcmp::CacheState;
using ktxcmp::ImageCache;
using ktxcmp::Slot;
using ktxcmp::SubresourceKey;
using ktxcmp::Surface;
using ktxcmp::SurfacePtr;

// One texel is 16 bytes, so sizes are easy to reason about in a budget test.
SurfacePtr makeSurface(int texels) {
    auto s = std::make_shared<Surface>();
    s->w = texels;
    s->h = 1;
    s->d = 1;
    s->rgba.assign(static_cast<std::size_t>(texels) * 4u, 0.5f);
    return s;
}

SubresourceKey key(int level, Slot slot = Slot::A) {
    return SubresourceKey{slot, level, 0, 0};
}

void testStoreAndGet() {
    ImageCache cache(1024 * 1024);
    const auto k = key(0);
    check(cache.state(k) == CacheState::Missing, "an unknown key is Missing");
    check(cache.get(k) == nullptr, "an unknown key returns nothing");

    const std::uint64_t gen = cache.generation(Slot::A);
    check(cache.beginDecode(k, gen), "beginDecode claims a missing key");
    check(!cache.beginDecode(k, gen), "beginDecode refuses a key already in flight");
    check(cache.state(k) == CacheState::Pending, "a claimed key is Pending");

    cache.store(k, gen, makeSurface(16));
    check(cache.state(k) == CacheState::Ready, "a stored key is Ready");
    check(cache.get(k) != nullptr, "a stored key hands back its surface");
    check(cache.bytesUsed() == 16u * 16u, "bytes used counts the surface");
}

void testErrorsAreRemembered() {
    ImageCache cache(1024 * 1024);
    const auto k = key(3);
    const std::uint64_t gen = cache.generation(Slot::A);
    cache.beginDecode(k, gen);
    cache.storeError(k, gen, ktxcmp::Error{ktxcmp::ErrorCode::Malformed, "bad block"});

    check(cache.state(k) == CacheState::Failed, "a failed key is Failed, not Missing");
    const auto err = cache.error(k);
    check(err && err->message == "bad block", "the failure message is kept");
    check(!cache.beginDecode(k, gen), "a failed key is not retried in a loop");
    check(cache.bytesUsed() == 0, "a failure costs no bytes");
}

void testEvictionRespectsBudget() {
    // Room for exactly 4 surfaces of 16 texels (256 bytes each).
    ImageCache cache(4u * 256u);
    const std::uint64_t gen = cache.generation(Slot::A);

    for (int i = 0; i < 6; ++i) {
        const auto k = key(i);
        cache.beginDecode(k, gen);
        cache.store(k, gen, makeSurface(16));
    }
    check(cache.bytesUsed() <= cache.budget(), "the cache stays inside its budget");
    check(cache.state(key(5)) == CacheState::Ready, "the newest entry survives");
    check(cache.state(key(0)) == CacheState::Missing, "the oldest entry was evicted");
}

void testEvictionIsLeastRecentlyUsed() {
    ImageCache cache(3u * 256u);
    const std::uint64_t gen = cache.generation(Slot::A);
    for (int i = 0; i < 3; ++i) {
        cache.beginDecode(key(i), gen);
        cache.store(key(i), gen, makeSurface(16));
    }
    // Touch the oldest, so the middle one becomes least recently used.
    (void)cache.get(key(0));

    cache.beginDecode(key(9), gen);
    cache.store(key(9), gen, makeSurface(16));

    check(cache.state(key(0)) == CacheState::Ready, "a touched entry is not the one evicted");
    check(cache.state(key(1)) == CacheState::Missing, "the least recently used entry goes first");
}

void testHeldSurfaceOutlivesEviction() {
    ImageCache cache(1u * 256u);
    const std::uint64_t gen = cache.generation(Slot::A);
    cache.beginDecode(key(0), gen);
    cache.store(key(0), gen, makeSurface(16));

    SurfacePtr held = cache.get(key(0));
    check(held != nullptr, "the surface is handed out");

    for (int i = 1; i < 5; ++i) {
        cache.beginDecode(key(i), gen);
        cache.store(key(i), gen, makeSurface(16));
    }
    check(cache.state(key(0)) == CacheState::Missing, "the held entry was evicted from the cache");
    // Evicting must not free a surface a panel is still drawing this frame.
    check(held.use_count() >= 1 && held->rgba.size() == 64u,
          "a surface already handed out stays alive and intact after eviction");
}

void testInvalidationDropsOnlyOneSlot() {
    ImageCache cache(1024 * 1024);
    const std::uint64_t genA = cache.generation(Slot::A);
    const std::uint64_t genB = cache.generation(Slot::B);
    cache.beginDecode(key(0, Slot::A), genA);
    cache.store(key(0, Slot::A), genA, makeSurface(16));
    cache.beginDecode(key(0, Slot::B), genB);
    cache.store(key(0, Slot::B), genB, makeSurface(16));

    const std::uint64_t newGenA = cache.invalidate(Slot::A);
    check(cache.state(key(0, Slot::A)) == CacheState::Missing, "invalidate drops that slot");
    check(cache.state(key(0, Slot::B)) == CacheState::Ready, "invalidate leaves the other slot");
    check(newGenA != genA, "invalidate advances the generation");

    // A worker finishing late must not resurrect the file that was replaced.
    cache.beginDecode(key(1, Slot::A), genA);
    check(cache.state(key(1, Slot::A)) == CacheState::Missing,
          "a stale generation cannot claim a key");
    cache.store(key(1, Slot::A), genA, makeSurface(16));
    check(cache.state(key(1, Slot::A)) == CacheState::Missing,
          "a decode from the previous file is discarded on arrival");
}

void testConcurrentStoresAreConsistent() {
    ImageCache cache(64u * 1024u * 1024u);
    const std::uint64_t gen = cache.generation(Slot::A);

    constexpr int kThreads = 4;
    constexpr int kKeys = 200;
    std::atomic<int> claimed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kKeys; ++i) {
                const auto k = key(i);
                if (cache.beginDecode(k, gen)) {
                    ++claimed;
                    cache.store(k, gen, makeSurface(4));
                }
            }
        });
    }
    for (auto& t : threads)
        t.join();

    check(claimed.load() == kKeys,
          "every key is claimed exactly once across threads (got " +
              std::to_string(claimed.load()) + ")");
    check(cache.entryCount() == static_cast<std::size_t>(kKeys),
          "no duplicate entries were created");

    bool allReady = true;
    for (int i = 0; i < kKeys; ++i)
        if (cache.state(key(i)) != CacheState::Ready)
            allReady = false;
    check(allReady, "every key ends up Ready");
}

}  // namespace

int main() {
    std::printf("store and retrieve\n");
    testStoreAndGet();
    std::printf("failures\n");
    testErrorsAreRemembered();
    std::printf("budget\n");
    testEvictionRespectsBudget();
    testEvictionIsLeastRecentlyUsed();
    testHeldSurfaceOutlivesEviction();
    std::printf("invalidation\n");
    testInvalidationDropsOnlyOneSlot();
    std::printf("concurrency\n");
    testConcurrentStoresAreConsistent();

    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
