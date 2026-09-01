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

void AppState::enqueueOpen(std::filesystem::path path, float dropX, float dropY) {
    const std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pending.push_back(PendingOpen{std::move(path), dropX, dropY});
}

void AppState::processPendingOpens() {
    std::vector<PendingOpen> opens;
    {
        const std::lock_guard<std::mutex> lock(m_pendingMutex);
        opens.swap(m_pending);
    }

    for (const auto& open : opens) {
        // A drop onto a slot card goes to that slot; a drop anywhere else fills
        // the first empty one (PLAN.md M4).
        Slot target = Slot::A;
        bool decided = false;
        if (open.x >= 0.0f) {
            for (int i = 0; i < 2 && !decided; ++i) {
                if (slotRect[i].valid() && slotRect[i].contains(open.x, open.y)) {
                    target = i == 0 ? Slot::A : Slot::B;
                    decided = true;
                }
            }
        }
        if (!decided) {
            // Dropped on the body. A PNG is only ever a reference and a KTX only
            // ever a source, so the file type decides this more reliably than
            // "first empty" would.
            target = extensionOf(open.path) == ".png" ? Slot::B : Slot::A;
        }
        loadIntoSlot(target, open.path);
    }
}

void AppState::loadIntoSlot(Slot which, const std::filesystem::path& path) {
    SlotState& target = slot(which);

    // Drop queued work and invalidate cached levels before the file changes.
    // Anything already running finishes and is discarded on its generation check.
    decoder.cancel(which);
    cache.invalidate(which);

    target.ktx.reset();
    target.reference.reset();
    target.referenceInfo = PngInfo{};
    target.error.reset();
    target.path = path;
    m_compareAvailable = false;

    if (extensionOf(path) == ".png") {
        auto loaded = loadPng(path, target.referenceTf, &target.referenceInfo);
        if (!loaded) {
            target.error = loaded.error();
            return;
        }
        target.reference = std::make_shared<const Surface>(std::move(*loaded));
        m_compareAvailable = false;
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

void AppState::setReferenceTransfer(Slot which, TransferFn tf) {
    SlotState& target = slot(which);
    if (target.referenceTf == tf)
        return;
    target.referenceTf = tf;
    if (!target.isReference())
        return;

    // Reload rather than re-tag: the stored values are the same either way, but
    // going back through the loader keeps one path responsible for the mapping.
    auto reloaded = loadPng(target.path, tf, &target.referenceInfo);
    if (!reloaded) {
        target.error = reloaded.error();
        target.reference.reset();
        return;
    }
    target.reference = std::make_shared<const Surface>(std::move(*reloaded));
    m_compareAvailable = false;
}

void AppState::requestCompare() {
    m_compareAvailable = false;
    if (view.compareMode != CompareMode::EncodeFidelity)
        return;  // modes 2 and 3 arrive at M5
    if (!slotA.loaded() || !slotB.isReference())
        return;

    // Mode 1 is mip 0 against the reference, with no resampling: the only
    // unconfounded measurement (CLAUDE.md, Compare modes).
    const SubresourceKey key{Slot::A, 0, view.layer, view.face};
    SurfacePtr a = cache.get(key);
    if (!a)
        return;  // still decoding; the panel shows the pending state

    // The token has to change whenever anything the answer depends on changes.
    m_compareToken = (cache.generation(Slot::A) * 1000003ull) ^
                     (reinterpret_cast<std::uintptr_t>(slotB.reference.get()) * 31ull) ^
                     (view.metricLinearLight ? 0x5bf03635ull : 0ull);
    const FormatId& format = slotA.ktx->info().format;
    const bool normalMode = looksLikeNormalMap(format) && !view.rawRgOverride;
    m_compareToken ^= (normalMode ? 0x9e3779b9ull : 0ull);

    m_compareAvailable = true;
    comparer.request(m_compareToken, a, slotB.reference, view.metricLinearLight, normalMode,
                     format.isSigned);
}

void AppState::clearReferenceChain() {
    slotB.chain.clear();
    slotB.chainPaths.clear();
    slotB.chainProblems.clear();
    slotB.chainSource.clear();
    m_chainAvailable = false;
}

void AppState::loadReferenceChain(const std::vector<std::filesystem::path>& paths) {
    clearReferenceChain();
    if (!slotA.loaded()) {
        slotB.error = Error{ErrorCode::Internal,
                            "load a texture into slot A first: the chain is matched to its levels"};
        return;
    }

    // A single directory means every PNG inside it.
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (paths.size() == 1 && std::filesystem::is_directory(paths.front(), ec)) {
        slotB.chainSource = paths.front().filename().string() + "/";
        for (const auto& entry : std::filesystem::directory_iterator(paths.front(), ec))
            if (entry.is_regular_file(ec) && extensionOf(entry.path()) == ".png")
                files.push_back(entry.path());
        std::sort(files.begin(), files.end());
    } else {
        files = paths;
        slotB.chainSource = std::to_string(files.size()) + " files";
    }

    if (files.empty()) {
        slotB.error = Error{ErrorCode::Io, "no PNG files found"};
        return;
    }

    // Load each file, then match on dimension alone.
    std::vector<SurfacePtr> loaded;
    std::vector<Extent> fileDims;
    loaded.reserve(files.size());
    fileDims.reserve(files.size());
    for (const auto& path : files) {
        PngInfo info;
        auto surface = loadPng(path, slotB.referenceTf, &info);
        if (!surface) {
            slotB.chainProblems.push_back(path.filename().string() + ": " +
                                          surface.error().message);
            loaded.push_back(nullptr);
            fileDims.push_back(Extent{-1, -1});
            continue;
        }
        loaded.push_back(std::make_shared<const Surface>(std::move(*surface)));
        fileDims.push_back(Extent{loaded.back()->w, loaded.back()->h});
    }

    const KtxInfo& info = slotA.ktx->info();
    std::vector<Extent> levelDims;
    levelDims.reserve(static_cast<std::size_t>(info.levelCount));
    for (const LevelInfo& li : info.levels)
        levelDims.push_back(Extent{li.w, li.h});

    const ChainMatch match = matchByDimension(fileDims, levelDims);

    slotB.chain.assign(levelDims.size(), nullptr);
    slotB.chainPaths.assign(levelDims.size(), {});
    for (std::size_t level = 0; level < levelDims.size(); ++level) {
        const int f = match.fileForLevel[level];
        if (f < 0)
            continue;
        slotB.chain[level] = loaded[static_cast<std::size_t>(f)];
        slotB.chainPaths[level] = files[static_cast<std::size_t>(f)];
    }

    for (const std::string& p : match.problems)
        slotB.chainProblems.push_back(p);
    for (int level : match.unmatchedLevels)
        slotB.chainProblems.push_back("level " + std::to_string(level) + " (" +
                                      std::to_string(levelDims[static_cast<std::size_t>(level)].w) +
                                      "x" +
                                      std::to_string(levelDims[static_cast<std::size_t>(level)].h) +
                                      ") has no reference image");
    for (int f : match.unmatchedFiles)
        slotB.chainProblems.push_back(files[static_cast<std::size_t>(f)].filename().string() +
                                      " matches no level");

    slotB.path = files.front();
    m_chainAvailable = false;
}

void AppState::requestChain() {
    m_chainAvailable = false;
    if (view.compareMode == CompareMode::EncodeFidelity)
        return;
    if (!slotA.loaded())
        return;
    if (view.compareMode == CompareMode::ChainVsReference && !slotB.isReference() &&
        !slotB.hasChain())
        return;

    // Every level has to be decoded before the chain can be analysed. They are
    // already being requested for the strip, so this just waits for them.
    const KtxInfo& info = slotA.ktx->info();
    ChainInput input;
    input.levels.reserve(static_cast<std::size_t>(info.levelCount));
    for (int level = 0; level < info.levelCount; ++level) {
        SurfacePtr s = cache.get(SubresourceKey{Slot::A, level, view.layer, view.face});
        if (!s)
            return;  // still decoding; the panel shows the pending state
        input.levels.push_back(std::move(s));
    }

    input.reference = view.compareMode == CompareMode::ChainVsReference ? slotB.reference : nullptr;
    if (view.compareMode == CompareMode::ChainVsReference && slotB.hasChain() &&
        slotB.chain.size() == input.levels.size())
        input.referenceChain = slotB.chain;
    input.mode = view.compareMode;
    input.filter = view.filter;
    input.resampleLinearLight = view.resampleLinearLight;
    input.metricLinearLight = view.metricLinearLight;
    input.normalMode = looksLikeNormalMap(info.format) && !view.rawRgOverride;
    input.isSigned = info.format.isSigned;

    // Anything that changes the answer has to change the token.
    m_chainToken = (cache.generation(Slot::A) * 2654435761ull) ^
                   (static_cast<std::uint64_t>(view.compareMode) * 7919ull) ^
                   (static_cast<std::uint64_t>(view.filter) * 104729ull) ^
                   (view.resampleLinearLight ? 0x1000ull : 0ull) ^
                   (view.metricLinearLight ? 0x2000ull : 0ull) ^
                   (reinterpret_cast<std::uintptr_t>(input.reference.get()) * 31ull) ^
                   (static_cast<std::uint64_t>(view.layer) << 20) ^
                   (static_cast<std::uint64_t>(view.face) << 28) ^
                   (input.normalMode ? 0x4d2ull : 0ull) ^
                   (input.referenceChain.empty()
                        ? 0ull
                        : reinterpret_cast<std::uintptr_t>(input.referenceChain.front().get()) *
                              17ull);
    m_chainAvailable = true;
    chainAnalyser.request(m_chainToken, std::move(input));
}

std::string AppState::buildCsv() const {
    if (!m_chainAvailable)
        return {};
    auto report = chainAnalyser.result(m_chainToken);
    if (!report || !*report)
        return {};
    std::string slotBDescription;
    if (slotB.hasChain())
        slotBDescription = "chain: " + slotB.chainSource;
    else if (slotB.occupied())
        slotBDescription = slotB.path.string();
    else
        slotBDescription = "(none)";
    return toCsv(**report, slotA.path.string(), slotBDescription);
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
