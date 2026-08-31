#pragma once

// Container layer: the only place that knows libktx exists.
//
// CLAUDE.md: layers depend downward only, and no ktx_error_code_e is allowed to
// leak upward. Everything here reports through Result<T>.

#include "container/FormatId.hpp"
#include "core/Error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct ktxTexture;

namespace ktxcmp {

enum class ContainerVersion { Ktx1, Ktx2 };

enum class Supercompression { None, BasisLZ, Zstd, Zlib, Unknown };

[[nodiscard]] const char* supercompressionName(Supercompression s) noexcept;

struct LevelInfo {
    int w = 0, h = 0, d = 1;
    std::uint64_t imageBytes = 0;   // one image: no layers, no faces
    std::uint64_t byteOffset = 0;   // of layer 0, face 0, within the data block
};

struct KtxInfo {
    ContainerVersion version = ContainerVersion::Ktx2;
    FormatId format{};
    std::string formatName;

    int baseWidth = 0, baseHeight = 0, baseDepth = 1;
    int levelCount = 1, layerCount = 1, faceCount = 1;
    bool isArray = false;
    bool isCubemap = false;

    Supercompression supercompression = Supercompression::None;
    bool needsTranscoding = false;  // BasisLZ / UASTC payload, not yet decodable

    // KTX1 has no DFD (CLAUDE.md, trap 9). Anything sourced from one must say so
    // rather than reporting a default as though it were read from the file.
    bool hasDfd = false;
    std::optional<bool> premultiplied;

    std::uint64_t dataSize = 0;
    std::vector<LevelInfo> levels;
};

// Owns one decoded-nothing container. Move-only: it holds a libktx allocation.
class KtxFile {
public:
    KtxFile() = default;
    ~KtxFile();

    KtxFile(KtxFile&& other) noexcept;
    KtxFile& operator=(KtxFile&& other) noexcept;
    KtxFile(const KtxFile&) = delete;
    KtxFile& operator=(const KtxFile&) = delete;

    [[nodiscard]] static Result<KtxFile> open(const std::filesystem::path& path);

    [[nodiscard]] const KtxInfo& info() const { return m_info; }
    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    // Still-encoded bytes for one subresource. Valid while this object lives.
    [[nodiscard]] Result<std::span<const std::uint8_t>> levelBytes(int level, int layer = 0,
                                                                   int face = 0) const;

private:
    ktxTexture* m_texture = nullptr;
    KtxInfo m_info;
    std::filesystem::path m_path;
};

}  // namespace ktxcmp
