#include "app/AppState.hpp"

#include <algorithm>
#include <utility>

namespace ktxcmp {
namespace {

// Lower-cased extension, so ".KTX2" and ".ktx2" are one case.
std::string extensionOf(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

}  // namespace

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
    target.clear();
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

    target.ktx = std::move(*opened);

    // The previous selection may not exist in this file.
    const KtxInfo& info = target.ktx->info();
    view.level = std::clamp(view.level, 0, info.levelCount - 1);
    view.layer = std::clamp(view.layer, 0, info.layerCount - 1);
    view.face  = std::clamp(view.face, 0, info.faceCount - 1);
}

}  // namespace ktxcmp
