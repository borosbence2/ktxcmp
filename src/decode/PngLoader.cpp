#include "decode/PngLoader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO  // we read the bytes ourselves, for wide paths
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4505 4996)  // vendored header
#endif
#include <stb_image.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <fstream>
#include <memory>
#include <vector>

namespace ktxcmp {
namespace {

Result<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return fail(ErrorCode::Io, "no such file: " + path.string());
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return fail(ErrorCode::Io, "cannot stat " + path.string() + ": " + ec.message());

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return fail(ErrorCode::Io, "cannot open " + path.string());
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size)
        return fail(ErrorCode::Io, "short read on " + path.string());
    return bytes;
}

}  // namespace

Result<Surface> loadPng(const std::filesystem::path& path, TransferFn assume, PngInfo* infoOut) {
    auto bytes = readWholeFile(path);
    if (!bytes)
        return std::unexpected(bytes.error());

    const int len = static_cast<int>(bytes->size());
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes->data(), len, &w, &h, &channels))
        return fail(ErrorCode::NotAContainer,
                    std::string("not a readable PNG: ") + stbi_failure_reason());

    const bool is16 = stbi_is_16_bit_from_memory(bytes->data(), len) != 0;
    if (infoOut) {
        infoOut->w = w;
        infoOut->h = h;
        infoOut->channels = channels;
        infoOut->bitDepth = is16 ? 16 : 8;
    }

    Surface out;
    out.w = w;
    out.h = h;
    out.d = 1;
    out.tf = assume;
    out.rgba.assign(static_cast<std::size_t>(w) * h * 4u, 0.0f);

    // 16-bit goes through the 16-bit entry point, never the 8-bit one: reading
    // it via stbi_load would quietly discard the low byte (CLAUDE.md, trap 5).
    if (is16) {
        int gw = 0, gh = 0, gc = 0;
        std::unique_ptr<stbi_us, void (*)(void*)> pixels(
            stbi_load_16_from_memory(bytes->data(), len, &gw, &gh, &gc, 4), stbi_image_free);
        if (!pixels)
            return fail(ErrorCode::Malformed,
                        std::string("cannot decode PNG: ") + stbi_failure_reason());
        for (std::size_t i = 0; i < out.rgba.size(); ++i)
            out.rgba[i] = static_cast<float>(pixels.get()[i]) / 65535.0f;
    } else {
        int gw = 0, gh = 0, gc = 0;
        std::unique_ptr<stbi_uc, void (*)(void*)> pixels(
            stbi_load_from_memory(bytes->data(), len, &gw, &gh, &gc, 4), stbi_image_free);
        if (!pixels)
            return fail(ErrorCode::Malformed,
                        std::string("cannot decode PNG: ") + stbi_failure_reason());
        for (std::size_t i = 0; i < out.rgba.size(); ++i)
            out.rgba[i] = static_cast<float>(pixels.get()[i]) / 255.0f;
    }
    return out;
}

}  // namespace ktxcmp
