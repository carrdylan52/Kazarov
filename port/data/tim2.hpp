#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace kzdata::tim2 {

// The order of the games' own texture classes' bpp field, not TIM2's image type numbering.
enum class Format : int {
    Index4 = 0,
    Index8 = 1,
    Rgb16 = 2,
    Rgb24 = 3,
    Rgb32 = 4,
};

inline std::optional<Format> FormatOfImageType(int image_type) {
    switch (image_type) {
        case 1: return Format::Rgb16;
        case 2: return Format::Rgb24;
        case 3: return Format::Rgb32;
        case 4: return Format::Index4;
        case 5: return Format::Index8;
        default: return std::nullopt;
    }
}

inline unsigned Read16(const unsigned char *p) {
    return p[0] | p[1] << 8;
}

inline std::uint32_t Read32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

// One level of a picture's image data, in the file.
struct Level {
    std::size_t offset = 0;
    std::size_t size = 0;
    unsigned    width = 0;
    unsigned    height = 0;
};

// The bytes a linear level of the format holds.
inline std::uint64_t LevelBytes(Format format, unsigned width, unsigned height) {
    std::uint64_t texels = static_cast<std::uint64_t>(width) * height;
    switch (format) {
        case Format::Index4: return (texels + 1) / 2;
        case Format::Index8: return texels;
        case Format::Rgb16: return texels * 2;
        case Format::Rgb24: return texels * 3;
        case Format::Rgb32: return texels * 4;
    }
    return 0;
}

struct Picture {
    std::size_t   offset = 0; // of its header, in the file
    std::uint32_t total_size = 0;
    std::uint32_t clut_size = 0;
    std::uint32_t image_size = 0;
    unsigned      header_size = 0;
    unsigned      clut_colors = 0;
    unsigned      mipmaps = 0;
    unsigned      clut_type = 0;
    unsigned      image_type = 0;
    unsigned      width = 0;
    unsigned      height = 0;
    std::size_t   image = 0; // offset of the base level, in the file
    std::size_t   clut = 0;  // offset of the CLUT, in the file
    // Base level first, each inside the image data and large enough for its size and format.
    std::vector<Level> levels;
};

// The levels of a picture whose image data starts at `image` in `file`, or none when one does not
// fit. A picture with mipmaps lists each level's size after the GS registers in its header.
inline std::optional<std::vector<Level>> ParseLevels(std::span<const unsigned char> file, const Picture &picture) {
    std::optional<Format> format = FormatOfImageType(static_cast<int>(picture.image_type));
    unsigned              count = std::max(picture.mipmaps, 1u);
    if (!format || picture.width == 0 || picture.height == 0 || count > 7 ||
        (count > 1 && picture.header_size < 0x40 + 4 * count)) {
        return std::nullopt;
    }
    std::vector<Level> levels;
    std::uint64_t      at = picture.image;
    std::uint64_t      end = static_cast<std::uint64_t>(picture.image) + picture.image_size;
    for (unsigned i = 0; i < count; i++) {
        Level level;
        level.width = std::max(picture.width >> i, 1u);
        level.height = std::max(picture.height >> i, 1u);
        std::uint64_t needed = LevelBytes(*format, level.width, level.height);
        std::uint64_t size = count > 1 ? Read32(file.data() + picture.offset + 0x40 + 4 * i) : picture.image_size;
        if (size < needed || size > end - at) {
            return std::nullopt;
        }
        level.offset = static_cast<std::size_t>(at);
        level.size = static_cast<std::size_t>(size);
        levels.push_back(level);
        at += size;
    }
    return levels;
}

// The pictures of a TIM2 file. A picture's total size is not trusted (the games' files get it
// wrong); the header, image and CLUT sizes are, and each must lie inside the file, as must every
// level the image data holds.
inline std::optional<std::vector<Picture>> Parse(std::span<const unsigned char> file) {
    if (file.size() < 16 || std::memcmp(file.data(), "TIM2", 4) != 0) {
        return std::nullopt;
    }
    unsigned             count = Read16(file.data() + 6);
    std::size_t          at = file[5] == 1 ? 128 : 16;
    std::vector<Picture> pictures;
    for (unsigned i = 0; i < count; i++) {
        if (at > file.size() || file.size() - at < 0x30) {
            return std::nullopt;
        }
        const unsigned char *p = file.data() + at;
        Picture              picture;
        picture.offset = at;
        picture.total_size = Read32(p);
        picture.clut_size = Read32(p + 4);
        picture.image_size = Read32(p + 8);
        picture.header_size = Read16(p + 0x0C);
        picture.clut_colors = Read16(p + 0x0E);
        picture.mipmaps = p[0x11];
        picture.clut_type = p[0x12];
        picture.image_type = p[0x13];
        picture.width = Read16(p + 0x14);
        picture.height = Read16(p + 0x16);
        picture.image = at + picture.header_size;
        picture.clut = picture.image + picture.image_size;
        if (picture.header_size < 0x30 || picture.clut > file.size() ||
            picture.clut_size > file.size() - picture.clut) {
            return std::nullopt;
        }
        std::optional<std::vector<Level>> levels = ParseLevels(file, picture);
        if (!levels) {
            return std::nullopt;
        }
        picture.levels = std::move(*levels);
        at = picture.clut + picture.clut_size;
        pictures.push_back(std::move(picture));
    }
    return pictures;
}

// Where texel (x, y) of an 8-bit picture `width` texels wide lies in pixel data kept in the GS's
// PSMT8 block order.
inline std::size_t T8Texel(int x, int y, int width) {
    return (y & ~0xF) * width + (x & ~0xF) * 2 + ((((y & ~3) >> 1) + (y & 1)) & 7) * width * 2 +
           ((x + (((y + 2) >> 2) & 1) * 4) & 7) * 4 + ((y >> 1) & 1) + ((x >> 2) & 2);
}

// The palette entry a CSM1 CLUT image position holds (bits 3 and 4 swapped); its own inverse.
constexpr unsigned ClutCsm1(unsigned position) {
    return (position & ~0x18u) | ((position & 0x08u) << 1) | ((position & 0x10u) >> 1);
}

// The colours of an 8-bit picture's 256-entry CLUT as RGB, in file order, or none.
inline std::vector<std::uint32_t> Colours256(std::span<const unsigned char> file, const Picture &picture) {
    if (picture.image_type != 5 || picture.clut_colors != 256 || picture.clut_size < 1024 ||
        picture.clut + 1024 > file.size()) {
        return {};
    }
    std::vector<std::uint32_t> colours(256);
    for (int i = 0; i < 256; i++) {
        colours[i] = Read32(file.data() + picture.clut + i * 4) & 0xFFFFFF;
    }
    return colours;
}

namespace detail {

// The 8-bit page swizzle's lookup table inverted, per column parity.
struct InverseLut {
    std::uint8_t to_output[2][64];

    constexpr InverseLut() : to_output{} {
        constexpr std::uint8_t lut[128] = {
            0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47, 4, 32, 12, 40, 5, 33, 13, 41,
            6, 34, 14, 42, 7, 35, 15, 43, 16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63,
            20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 4, 32, 12, 40, 5, 33, 13, 41,
            6, 34, 14, 42, 7, 35, 15, 43, 0, 36, 8, 44, 1, 37, 9, 45, 2, 38, 10, 46, 3, 39, 11, 47,
            20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 16, 52, 24, 60, 17, 53, 25, 61,
            18, 54, 26, 62, 19, 55, 27, 63};
        for (unsigned parity = 0; parity < 2; parity++) {
            for (unsigned m = 0; m < 64; m++) {
                to_output[parity][lut[parity * 64 + m]] = static_cast<std::uint8_t>(m);
            }
        }
    }
};

inline constexpr InverseLut kInverseLut;

} // namespace detail

// The largest width or height a TIM2 header holds; past it a level's sizes are refused, and up to
// it they and every offset fit 64 bits.
inline constexpr std::uint64_t kMaxDimension = 0xFFFF;

inline bool DimensionsFit(std::int64_t width, std::int64_t height) {
    return width > 0 && height > 0 && static_cast<std::uint64_t>(width) <= kMaxDimension &&
           static_cast<std::uint64_t>(height) <= kMaxDimension;
}

// Where an 8-bit-to-PSMCT32 page swizzle puts texel (x, y) of a width x height image, or none
// where its page-sized work buffers drop it (images narrower than a block).
inline std::optional<std::uint64_t> SwizzledOffset(std::uint64_t width, std::uint64_t height, std::uint64_t x,
                                                   std::uint64_t y) {
    std::uint64_t pages_x = ((width - 1) >> 7) + 1;
    std::uint64_t page_width = pages_x == 1 ? width : 128;
    std::uint64_t row_bytes = pages_x == 1 ? width * 2 : 256;
    std::uint64_t row_count = ((height - 1) >> 6) == 0 ? height >> 1 : 32;
    std::uint64_t page_x = x / page_width;
    std::uint64_t page_y = y >> 6;
    std::uint64_t local_x = x - page_x * page_width;
    std::uint64_t local_y = y & 63;
    std::uint64_t source = (local_y & 15) * 16 + (local_x & 15);
    std::uint64_t column = source >> 6;
    std::uint64_t output = column * 64 + detail::kInverseLut.to_output[column & 1][source & 63];
    std::uint64_t page_row = (local_y >> 4) * 8 + (output >> 5);
    std::uint64_t page_column = (local_x >> 4) * 32 + (output & 31);
    if (page_row >= row_count || page_column >= row_bytes) {
        return std::nullopt;
    }
    return page_y * pages_x * row_bytes * row_count + row_bytes * page_x + page_row * row_bytes * pages_x +
           page_column;
}

// The bytes an 8-bit level in the page swizzle's order spans: one past the furthest texel's, or 0
// for dimensions a TIM2 header cannot hold.
inline std::uint64_t SwizzledBytes(std::int64_t width, std::int64_t height) {
    if (!DimensionsFit(width, height)) {
        return 0;
    }
    // Each row of pages lies above the last, so the furthest texel is in the bottom one.
    std::uint64_t end = 0;
    for (std::int64_t y = (height - 1) & ~std::int64_t{63}; y < height; y++) {
        for (std::int64_t x = 0; x < width; x++) {
            if (std::optional<std::uint64_t> offset = SwizzledOffset(width, height, x, y)) {
                end = std::max(end, *offset + 1);
            }
        }
    }
    return end;
}

// False, leaving linear untouched, when either span is short of the level or the dimensions are
// past a TIM2 header's.
inline bool Unswizzle8(std::int64_t width, std::int64_t height, std::span<const unsigned char> swizzled,
                       std::span<unsigned char> linear) {
    if (!DimensionsFit(width, height) || swizzled.size() < SwizzledBytes(width, height) ||
        linear.size() < static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height)) {
        return false;
    }
    for (std::int64_t y = 0; y < height; y++) {
        for (std::int64_t x = 0; x < width; x++) {
            std::optional<std::uint64_t> offset = SwizzledOffset(width, height, x, y);
            linear[static_cast<std::size_t>(y * width + x)] = offset ? swizzled[static_cast<std::size_t>(*offset)] : 0;
        }
    }
    return true;
}

// The bytes DecodeLevel reads for a level: only 8-bit pixels come swizzled.
inline std::uint64_t SourceBytes(Format format, unsigned width, unsigned height, bool swizzled) {
    if (!DimensionsFit(width, height)) {
        return 0;
    }
    return swizzled && format == Format::Index8 ? SwizzledBytes(width, height) : LevelBytes(format, width, height);
}

// One level as linear texels: an index byte per texel for the index formats, r,g,b,a bytes
// otherwise, alpha still in GS units (0x80 opaque). `swizzled`: 8-bit pixels in the page order
// Unswizzle8 undoes. False, with `out` empty, when `src` is short of the level.
inline bool DecodeLevel(Format format, unsigned width, unsigned height, std::span<const unsigned char> src,
                        bool swizzled, std::vector<std::uint8_t> &out) {
    out.clear();
    std::size_t texels = static_cast<std::size_t>(width) * height;
    if (!DimensionsFit(width, height) || src.size() < SourceBytes(format, width, height, swizzled)) {
        return false;
    }
    switch (format) {
        case Format::Index4:
            out.resize(texels);
            for (std::size_t i = 0; i < texels; i++) {
                unsigned char pair = src[i >> 1];
                out[i] = (i & 1) ? pair >> 4 : pair & 0xF;
            }
            break;
        case Format::Index8:
            out.resize(texels);
            if (swizzled) {
                Unswizzle8(width, height, src, out);
            } else {
                std::memcpy(out.data(), src.data(), texels);
            }
            break;
        case Format::Rgb16:
            // The GS widens a channel by shifting it and takes alpha from TEXA by the A bit; the
            // games never program TEXA for 16-bit images, so the usual TA0 0 / TA1 0x80 is baked in.
            out.resize(texels * 4);
            for (std::size_t i = 0; i < texels; i++) {
                unsigned pixel = src[i * 2] | src[i * 2 + 1] << 8;
                out[i * 4 + 0] = static_cast<std::uint8_t>((pixel & 0x1F) << 3);
                out[i * 4 + 1] = static_cast<std::uint8_t>(((pixel >> 5) & 0x1F) << 3);
                out[i * 4 + 2] = static_cast<std::uint8_t>(((pixel >> 10) & 0x1F) << 3);
                out[i * 4 + 3] = (pixel & 0x8000) ? 0x80 : 0;
            }
            break;
        case Format::Rgb24:
            out.resize(texels * 4);
            for (std::size_t i = 0; i < texels; i++) {
                out[i * 4 + 0] = src[i * 3 + 0];
                out[i * 4 + 1] = src[i * 3 + 1];
                out[i * 4 + 2] = src[i * 3 + 2];
                out[i * 4 + 3] = 0xFF;
            }
            break;
        case Format::Rgb32:
            out.assign(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(texels * 4));
            break;
        default:
            return false;
    }
    return true;
}

} // namespace kzdata::tim2
