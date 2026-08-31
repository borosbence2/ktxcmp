#include "app/AppState.hpp"

#include <algorithm>
#include <cctype>
#include <thread>
#include <utility>

namespace ktxcmp {
namespace {

// A 2048 square RGBA32F level is 64 MB, so a full 12-level chain is about 85 MB.
// This holds a couple of chains without letting a folder of large textures grow
// without bound.
constexpr std::size_t kCacheBudget = 384u * 1024u * 1024u;

unsigned workerCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware <= 1)
        return 1;
    // Leave the UI thread a core of its own, and do not spawn more workers than
    // a mip chain has levels worth decoding in parallel.
    return std::min(hardware - 1u, 4u);
}

std::string extensionOf(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace

AppState::AppState() : cache(kCacheBudget), decoder(cache, workerCount()) {}

void AppState::enqueueOpen(std::filesystem::path path) {
    const std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pending.push_back(std::move(path));
}

void AppState::processPendingOpens() {
    std::vector<std::filesystem::path> paths;
    {
        const std::lock_guard<std::mutex> lock(m_pendingMutex);
        paths.swap(m_pending);
    }
    // Everything routes to slot A until slot B means something at M4.
    for (const auto& path : paths)
        loadIntoSlot(Slot::A, path);
}

void AppState::loadIntoSlot(Slot which, const std::filesystem::path& path) {
    SlotState& target = slot(which);

    // Drop queued work and invalidate cached levels before the file changes.
    // Anything already running finishes and is discarded on its generation check.
    decoder.cancel(which);
    cache.invalidate(which);

    target.ktx.reset();
    target.error.reset();
    target.path = path;

    // A dropped PNG is a reasonable thing for someone to try, and "not a KTX
    // file" would be a true but unhelpful answer to it.
    if (extensionOf(path) == ".png") {
        target.error = Error{ErrorCode::UnsupportedFormat,
                             "PNG reference images are not supported yet"};
        return;
    }

    auto opened = KtxFile::open(path);
    if (!opened) {
        target.error = opened.error();
        return;
    }
    target.ktx = std::make_shared<KtxFile>(std::move(*opened));

    view.level = 0;
    clampSelection();
}

void AppState::clampSelection() {
    const SlotState& target = slot(view.slot);
    if (!target.loaded())
        return;
    const KtxInfo& info = target.ktx->info();
    view.level = std::clamp(view.level, 0, info.levelCount - 1);
    view.layer = std::clamp(view.layer, 0, info.layerCount - 1);
    view.face = std::clamp(view.face, 0, info.faceCount - 1);
}

SubresourceKey AppState::selectionKey(Slot which) const {
    return SubresourceKey{which, view.level, view.layer, view.face};
}

void AppState::requestVisible() {
    clampSelection();

    for (const Slot which : {Slot::A, Slot::B}) {
        SlotState& target = slot(which);
        if (!target.loaded())
            continue;

        // What the user is looking at goes first.
        decoder.request(selectionKey(which), target.ktx, DecodeService::kVisible);

        // Then every level, for the strip. The queue orders these smallest
        // first, so the strip fills from the cheap end while level 0 works.
        const KtxInfo& info = target.ktx->info();
        for (int level = 0; level < info.levelCount; ++level) {
            const SubresourceKey key{which, level, view.layer, view.face};
            decoder.request(key, target.ktx, DecodeService::kThumbnail);
        }
    }
}

}  // namespace ktxcmp
