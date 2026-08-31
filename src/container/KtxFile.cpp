#include "container/KtxFile.hpp"

#include <ktx.h>

#include <algorithm>
#include <fstream>
#include <utility>

namespace ktxcmp {
namespace {

Supercompression toSupercompression(ktx_uint32_t scheme) {
    switch (scheme) {
        case KTX_SS_NONE:     return Supercompression::None;
        case KTX_SS_BASIS_LZ: return Supercompression::BasisLZ;
        case KTX_SS_ZSTD:     return Supercompression::Zstd;
        case KTX_SS_ZLIB:     return Supercompression::Zlib;
        default:              return Supercompression::Unknown;
    }
}

// Read the whole file ourselves. libktx's from-file path opens with fopen, which
// on Windows takes an ANSI path and so mangles anything outside the local code
// page; std::filesystem::path feeds MSVC's wide overload correctly.
Result<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return fail(ErrorCode::Io, "no such file: " + path.string());

    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return fail(ErrorCode::Io, "cannot stat " + path.string() + ": " + ec.message());
    if (size < 12)
        return fail(ErrorCode::NotAContainer, "file is too small to be a KTX container");

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return fail(ErrorCode::Io, "cannot open " + path.string());

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size)
        return fail(ErrorCode::Io, "short read on " + path.string());

    return bytes;
}

// Checked before handing anything to libktx so a JPEG dropped on the window
// gets a sentence rather than a libktx error code.
Result<void> checkMagic(std::span<const std::uint8_t> bytes) {
    static constexpr std::uint8_t k1[12] = {0xAB, 'K', 'T', 'X', ' ', '1',
                                            '1', 0xBB, '\r', '\n', 0x1A, '\n'};
    static constexpr std::uint8_t k2[12] = {0xAB, 'K', 'T', 'X', ' ', '2',
                                            '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    if (std::equal(std::begin(k1), std::end(k1), bytes.begin()) ||
        std::equal(std::begin(k2), std::end(k2), bytes.begin()))
        return {};
    return fail(ErrorCode::NotAContainer,
                "not a KTX file: identifier does not match KTX 11 or KTX 20");
}

}  // namespace

const char* supercompressionName(Supercompression s) noexcept {
    switch (s) {
        case Supercompression::None:    return "none";
        case Supercompression::BasisLZ: return "BasisLZ";
        case Supercompression::Zstd:    return "Zstd";
        case Supercompression::Zlib:    return "ZLIB";
        case Supercompression::Unknown: return "unknown";
    }
    return "unknown";
}

KtxFile::~KtxFile() {
    if (m_texture)
        ktxTexture_Destroy(m_texture);
}

KtxFile::KtxFile(KtxFile&& other) noexcept
    : m_texture(std::exchange(other.m_texture, nullptr)),
      m_info(std::move(other.m_info)),
      m_path(std::move(other.m_path)) {}

KtxFile& KtxFile::operator=(KtxFile&& other) noexcept {
    if (this != &other) {
        if (m_texture)
            ktxTexture_Destroy(m_texture);
        m_texture = std::exchange(other.m_texture, nullptr);
        m_info = std::move(other.m_info);
        m_path = std::move(other.m_path);
    }
    return *this;
}

Result<KtxFile> KtxFile::open(const std::filesystem::path& path) {
    auto bytes = readWholeFile(path);
    if (!bytes)
        return std::unexpected(bytes.error());
    if (auto ok = checkMagic(*bytes); !ok)
        return std::unexpected(ok.error());

    ktxTexture* texture = nullptr;
    const KTX_error_code rc =
        ktxTexture_CreateFromMemory(bytes->data(), bytes->size(),
                                    KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
    if (rc != KTX_SUCCESS || texture == nullptr)
        return fail(ErrorCode::Malformed,
                    std::string("libktx could not read the file: ") + ktxErrorString(rc));

    KtxFile file;
    file.m_texture = texture;
    file.m_path = path;

    KtxInfo& info = file.m_info;
    info.baseWidth   = static_cast<int>(texture->baseWidth);
    info.baseHeight  = static_cast<int>(texture->baseHeight);
    info.baseDepth   = static_cast<int>(texture->baseDepth);
    info.levelCount  = static_cast<int>(texture->numLevels);
    info.layerCount  = static_cast<int>(texture->numLayers);
    info.faceCount   = static_cast<int>(texture->numFaces);
    info.isArray     = texture->isArray;
    info.isCubemap   = texture->isCubemap;
    info.dataSize    = ktxTexture_GetDataSize(texture);

    // The one place the two container versions differ. Everything downstream
    // reads FormatId and cannot tell them apart (CLAUDE.md, trap 9).
    Result<FormatId> format = fail(ErrorCode::Internal, "unreached");
    if (texture->classId == ktxTexture1_c) {
        auto* t1 = reinterpret_cast<ktxTexture1*>(texture);
        info.version = ContainerVersion::Ktx1;
        info.supercompression = Supercompression::None;  // KTX1 has no supercompression
        info.hasDfd = false;
        info.premultiplied = std::nullopt;  // unknown, not false
        format = formatFromGlInternalFormat(t1->glInternalformat);
    } else if (texture->classId == ktxTexture2_c) {
        auto* t2 = reinterpret_cast<ktxTexture2*>(texture);
        info.version = ContainerVersion::Ktx2;
        info.supercompression = toSupercompression(t2->supercompressionScheme);
        info.needsTranscoding = ktxTexture2_NeedsTranscoding(t2);
        info.hasDfd = t2->pDfd != nullptr;
        if (info.hasDfd)
            info.premultiplied = ktxTexture2_GetPremultipliedAlpha(t2) != 0;
        format = formatFromVkFormat(t2->vkFormat);
    } else {
        return fail(ErrorCode::Internal, "libktx returned an unknown texture class");
    }

    // A BasisLZ/UASTC payload has no final texel format until it is transcoded,
    // so refusing on format here would report the wrong problem.
    if (!format && !info.needsTranscoding)
        return std::unexpected(format.error());
    if (format) {
        info.format = *format;
        info.formatName = formatName(info.format);
    } else {
        info.formatName = "awaiting transcode";
    }

    info.levels.reserve(static_cast<std::size_t>(info.levelCount));
    for (int level = 0; level < info.levelCount; ++level) {
        LevelInfo li;
        li.w = std::max(1, info.baseWidth >> level);
        li.h = std::max(1, info.baseHeight >> level);
        li.d = std::max(1, info.baseDepth >> level);

        ktx_size_t offset = 0;
        const KTX_error_code orc =
            ktxTexture_GetImageOffset(texture, static_cast<ktx_uint32_t>(level), 0, 0, &offset);
        if (orc != KTX_SUCCESS)
            return fail(ErrorCode::Malformed,
                        "no image offset for level " + std::to_string(level) + ": " +
                            ktxErrorString(orc));
        li.byteOffset = offset;
        li.imageBytes = ktxTexture_GetImageSize(texture, static_cast<ktx_uint32_t>(level));

        // Independent check (PLAN.md M1): derive the size from our own format
        // description and the halving rule, and refuse to trust libktx when the
        // two disagree. A silent mismatch here poisons every later metric.
        if (format && !info.needsTranscoding) {
            const std::uint64_t expected = info.format.imageBytes(li.w, li.h, li.d);
            if (expected != li.imageBytes)
                return fail(ErrorCode::Malformed,
                            "level " + std::to_string(level) + " is " +
                                std::to_string(li.imageBytes) + " bytes but " +
                                info.formatName + " at " + std::to_string(li.w) + "x" +
                                std::to_string(li.h) + " requires " + std::to_string(expected));

            // The check above compares two numbers derived from the same header,
            // so a header that lies about its dimensions satisfies both. This one
            // measures against the payload that is actually present.
            if (li.byteOffset + li.imageBytes > info.dataSize)
                return fail(ErrorCode::Malformed,
                            "level " + std::to_string(level) + " needs " +
                                std::to_string(li.byteOffset + li.imageBytes) +
                                " bytes but the file holds " + std::to_string(info.dataSize) +
                                "; the header's dimensions do not match its payload");
        }
        info.levels.push_back(li);
    }

    return file;
}

Result<std::span<const std::uint8_t>> KtxFile::levelBytes(int level, int layer, int face) const {
    if (!m_texture)
        return fail(ErrorCode::Internal, "no texture loaded");
    if (level < 0 || level >= m_info.levelCount)
        return fail(ErrorCode::Internal, "level " + std::to_string(level) + " out of range");
    if (layer < 0 || layer >= m_info.layerCount)
        return fail(ErrorCode::Internal, "layer " + std::to_string(layer) + " out of range");
    if (face < 0 || face >= m_info.faceCount)
        return fail(ErrorCode::Internal, "face " + std::to_string(face) + " out of range");

    ktx_size_t offset = 0;
    const KTX_error_code rc = ktxTexture_GetImageOffset(
        m_texture, static_cast<ktx_uint32_t>(level), static_cast<ktx_uint32_t>(layer),
        static_cast<ktx_uint32_t>(face), &offset);
    if (rc != KTX_SUCCESS)
        return fail(ErrorCode::Malformed,
                    std::string("no image offset for that subresource: ") + ktxErrorString(rc));

    const ktx_uint8_t* data = ktxTexture_GetData(m_texture);
    const ktx_size_t total = ktxTexture_GetDataSize(m_texture);
    const std::uint64_t length = m_info.levels[static_cast<std::size_t>(level)].imageBytes;
    if (data == nullptr || offset + length > total)
        return fail(ErrorCode::Malformed, "subresource runs past the end of the texture data");

    return std::span<const std::uint8_t>(data + offset, static_cast<std::size_t>(length));
}

}  // namespace ktxcmp
