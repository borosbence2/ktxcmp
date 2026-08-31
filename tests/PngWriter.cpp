// A minimal PNG encoder, for test fixtures only.
//
// stb_image reads PNG but does not write it, and CLAUDE.md fixes the dependency
// list, so rather than vendor a writer the fixtures are emitted here. Deflate
// uses stored (uncompressed) blocks, which is a valid zlib stream and keeps this
// to something readable.
//
// This is test tooling. The application never writes a file (CLAUDE.md, Scope).

#include "Corpus.hpp"

#include <cstdint>
#include <fstream>
#include <vector>

namespace ktxcmp::test {
namespace {

std::uint32_t crcTable(std::uint32_t n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k)
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
    return c;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    static std::vector<std::uint32_t> table = [] {
        std::vector<std::uint32_t> t(256);
        for (std::uint32_t n = 0; n < 256; ++n)
            t[n] = crcTable(n);
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
    std::uint32_t a = 1, b = 0;
    for (std::uint8_t byte : data) {
        a = (a + byte) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void putChunk(std::vector<std::uint8_t>& out, const char type[5],
              const std::vector<std::uint8_t>& data) {
    put32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed(type, type + 4);
    typed.insert(typed.end(), data.begin(), data.end());
    out.insert(out.end(), typed.begin(), typed.end());
    put32(out, crc32(typed.data(), typed.size()));
}

// zlib stream wrapping deflate stored blocks.
std::vector<std::uint8_t> zlibStore(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> out;
    out.push_back(0x78);  // CM = deflate, CINFO = 32K window
    out.push_back(0x01);  // FCHECK so the header is a multiple of 31, no dict

    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t chunk = std::min<std::size_t>(raw.size() - offset, 65535u);
        const bool last = offset + chunk >= raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(chunk & 0xFF));
        out.push_back(static_cast<std::uint8_t>(chunk >> 8));
        const std::uint16_t nlen = static_cast<std::uint16_t>(~chunk);
        out.push_back(static_cast<std::uint8_t>(nlen & 0xFF));
        out.push_back(static_cast<std::uint8_t>(nlen >> 8));
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                   raw.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;
    }
    put32(out, adler32(raw));
    return out;
}

bool writeFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

}  // namespace

// RGBA, 8 or 16 bits per channel, no interlacing, filter type 0 on every row.
bool writePng(const std::filesystem::path& path, int w, int h, int bitDepth,
              const std::vector<std::uint8_t>& samples) {
    const int bytesPerSample = bitDepth / 8;
    const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u * bytesPerSample;
    if (samples.size() != rowBytes * static_cast<std::size_t>(h))
        return false;

    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), samples.begin() + static_cast<std::ptrdiff_t>(y * rowBytes),
                   samples.begin() + static_cast<std::ptrdiff_t>((y + 1) * rowBytes));
    }

    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> ihdr;
    put32(ihdr, static_cast<std::uint32_t>(w));
    put32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.push_back(static_cast<std::uint8_t>(bitDepth));
    ihdr.push_back(6);  // colour type: RGBA
    ihdr.push_back(0);  // compression: deflate
    ihdr.push_back(0);  // filter method 0
    ihdr.push_back(0);  // no interlace
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", zlibStore(raw));
    putChunk(png, "IEND", {});

    return writeFile(path, png);
}

bool writePngFixtures(const std::filesystem::path& dir) {
    for (const PngFixture& f : pngFixtures()) {
        if (f.bitDepth == 8) {
            const std::vector<std::uint8_t> pixels = gradient(f.w, f.h);
            if (!writePng(dir / f.filename, f.w, f.h, 8, pixels))
                return false;
        } else {
            const std::vector<std::uint16_t> values = gradient16(f.w, f.h);
            // PNG stores 16-bit samples big-endian.
            std::vector<std::uint8_t> bytes(values.size() * 2);
            for (std::size_t i = 0; i < values.size(); ++i) {
                bytes[i * 2] = static_cast<std::uint8_t>(values[i] >> 8);
                bytes[i * 2 + 1] = static_cast<std::uint8_t>(values[i] & 0xFF);
            }
            if (!writePng(dir / f.filename, f.w, f.h, 16, bytes))
                return false;
        }
    }
    return true;
}

}  // namespace ktxcmp::test
