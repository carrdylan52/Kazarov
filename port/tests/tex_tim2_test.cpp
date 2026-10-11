#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <span>
#include <vector>

#include "data/tim2.hpp"

namespace tim2 = kzdata::tim2;

namespace {

std::vector<unsigned char> Tim2File(unsigned width, unsigned height, unsigned type, std::uint32_t image_size,
                                    std::uint32_t clut_size, unsigned clut_colors) {
    std::vector<unsigned char> file(16 + 0x30 + image_size + clut_size, 0);
    std::memcpy(file.data(), "TIM2", 4);
    file[4] = 4;
    file[6] = 1;
    unsigned char *p = file.data() + 16;
    auto           put32 = [](unsigned char *at, std::uint32_t value) {
        for (int i = 0; i < 4; i++) {
            at[i] = static_cast<unsigned char>(value >> (8 * i));
        }
    };
    put32(p, 0x30 + image_size + clut_size);
    put32(p + 4, clut_size);
    put32(p + 8, image_size);
    p[0x0C] = 0x30;
    p[0x0E] = static_cast<unsigned char>(clut_colors);
    p[0x0F] = static_cast<unsigned char>(clut_colors >> 8);
    p[0x11] = 1;
    p[0x13] = static_cast<unsigned char>(type);
    p[0x14] = static_cast<unsigned char>(width);
    p[0x15] = static_cast<unsigned char>(width >> 8);
    p[0x16] = static_cast<unsigned char>(height);
    p[0x17] = static_cast<unsigned char>(height >> 8);
    return file;
}

} // namespace

TEST(TexTim2, ParsesAPictureAndItsClut) {
    auto file = Tim2File(16, 16, 5, 256, 1024, 256);
    for (int i = 0; i < 256; i++) {
        file[16 + 0x30 + 256 + i * 4] = static_cast<unsigned char>(i);
        file[16 + 0x30 + 256 + i * 4 + 3] = 0x80;
    }
    auto pictures = tim2::Parse(file);
    ASSERT_TRUE(pictures.has_value());
    ASSERT_EQ(pictures->size(), 1u);
    const tim2::Picture &picture = (*pictures)[0];
    EXPECT_EQ(picture.width, 16u);
    EXPECT_EQ(picture.image, 16u + 0x30);
    EXPECT_EQ(picture.clut, 16u + 0x30 + 256);
    EXPECT_EQ(tim2::FormatOfImageType(picture.image_type), tim2::Format::Index8);
    auto colours = tim2::Colours256(file, picture);
    ASSERT_EQ(colours.size(), 256u);
    EXPECT_EQ(colours[200], 200u);

    file.resize(file.size() - 1);
    EXPECT_FALSE(tim2::Parse(file).has_value());
    EXPECT_FALSE(tim2::Parse(std::vector<unsigned char>(8, 0)).has_value());
}

TEST(TexTim2, ClutCsm1SwapsBitsThreeAndFour) {
    EXPECT_EQ(tim2::ClutCsm1(0x08), 0x10u);
    EXPECT_EQ(tim2::ClutCsm1(0x10), 0x08u);
    EXPECT_EQ(tim2::ClutCsm1(0xE7), 0xE7u);
    for (unsigned i = 0; i < 256; i++) {
        EXPECT_EQ(tim2::ClutCsm1(tim2::ClutCsm1(i)), i);
    }
}

TEST(TexTim2, T8TexelIsAPermutationOfEachBlock) {
    std::vector<int> seen(32 * 16, 0);
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 32; x++) {
            std::size_t at = tim2::T8Texel(x, y, 32);
            ASSERT_LT(at, seen.size());
            seen[at]++;
        }
    }
    for (int count : seen) {
        EXPECT_EQ(count, 1);
    }
}

TEST(TexTim2, UnswizzleIsAPermutationOfAPage) {
    constexpr int              width = 128, height = 64;
    std::vector<unsigned char> swizzled(width * height);
    std::vector<int>           hits(swizzled.size(), 0);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            auto offset = tim2::SwizzledOffset(width, height, x, y);
            ASSERT_TRUE(offset.has_value());
            hits[*offset]++;
            swizzled[*offset] = static_cast<unsigned char>(x ^ y);
        }
    }
    for (int hit : hits) {
        EXPECT_EQ(hit, 1);
    }
    std::vector<unsigned char> linear(swizzled.size());
    ASSERT_TRUE(tim2::Unswizzle8(width, height, swizzled, linear));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            EXPECT_EQ(linear[y * width + x], static_cast<unsigned char>(x ^ y));
        }
    }
}

TEST(TexTim2, DecodesEachFormatToLinearTexels) {
    std::vector<std::uint8_t> out;
    const unsigned char       four[] = {0x21, 0x43};
    tim2::DecodeLevel(tim2::Format::Index4, 4, 1, four, false, out);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{1, 2, 3, 4}));

    const unsigned char sixteen[] = {0x1F, 0x80, 0xE0, 0x03};
    tim2::DecodeLevel(tim2::Format::Rgb16, 2, 1, sixteen, false, out);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{0xF8, 0, 0, 0x80, 0, 0xF8, 0, 0}));

    const unsigned char twenty_four[] = {1, 2, 3};
    tim2::DecodeLevel(tim2::Format::Rgb24, 1, 1, twenty_four, false, out);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{1, 2, 3, 0xFF}));

    const unsigned char thirty_two[] = {1, 2, 3, 0x40};
    tim2::DecodeLevel(tim2::Format::Rgb32, 1, 1, thirty_two, false, out);
    EXPECT_EQ(out, (std::vector<std::uint8_t>{1, 2, 3, 0x40}));
}

TEST(TexTim2, ParseRefusesLevelsShortOfTheirSize) {
    EXPECT_FALSE(tim2::Parse(Tim2File(16, 16, 5, 1, 1024, 256)).has_value());
    EXPECT_FALSE(tim2::Parse(Tim2File(16, 16, 4, 127, 64, 16)).has_value());
    EXPECT_FALSE(tim2::Parse(Tim2File(16, 16, 3, 1023, 0, 0)).has_value());
    EXPECT_FALSE(tim2::Parse(Tim2File(0, 16, 5, 0, 1024, 256)).has_value());
    EXPECT_FALSE(tim2::Parse(Tim2File(16, 16, 0, 256, 0, 0)).has_value());
    auto pictures = tim2::Parse(Tim2File(16, 16, 4, 128, 64, 16));
    ASSERT_TRUE(pictures.has_value());
    ASSERT_EQ((*pictures)[0].levels.size(), 1u);
    EXPECT_EQ((*pictures)[0].levels[0].offset, 16u + 0x30);
    EXPECT_EQ((*pictures)[0].levels[0].size, 128u);
}

TEST(TexTim2, ParseFindsEachMipmapLevel) {
    // An 8x8 Index8 picture with two levels: the mipmap header's level sizes follow the GS registers.
    auto make = [](std::uint32_t second) {
        constexpr unsigned header = 0x50;
        std::uint32_t      image = 64 + second;
        std::vector<unsigned char> file(16 + header + image, 0);
        std::memcpy(file.data(), "TIM2", 4);
        file[6] = 1;
        unsigned char *p = file.data() + 16;
        p[8] = static_cast<unsigned char>(image);
        p[0x0C] = header;
        p[0x11] = 2;
        p[0x13] = 5;
        p[0x14] = 8;
        p[0x16] = 8;
        p[0x40] = 64;
        p[0x44] = static_cast<unsigned char>(second);
        return file;
    };
    auto pictures = tim2::Parse(make(16));
    ASSERT_TRUE(pictures.has_value());
    const auto &levels = (*pictures)[0].levels;
    ASSERT_EQ(levels.size(), 2u);
    EXPECT_EQ(levels[1].offset, 16u + 0x50 + 64);
    EXPECT_EQ(levels[1].width, 4u);
    EXPECT_FALSE(tim2::Parse(make(15)).has_value());
}

TEST(TexTim2, SwizzledBytesReachesTheFurthestTexel) {
    for (int width : {1, 2, 15, 16, 17, 64, 100, 128, 129, 200, 256, 300}) {
        for (int height : {1, 2, 3, 16, 31, 32, 63, 64, 65, 100, 128, 129, 192}) {
            std::uint64_t end = 0;
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    if (auto offset = tim2::SwizzledOffset(width, height, x, y)) {
                        end = std::max(end, *offset + 1);
                    }
                }
            }
            EXPECT_EQ(tim2::SwizzledBytes(width, height), end)
                << width << "x" << height;
        }
    }
}

TEST(TexTim2, DecodeRefusesShortSources) {
    std::vector<std::uint8_t>  out;
    std::vector<unsigned char> src(16 * 16 * 4, 0);
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Index8, 16, 16, std::span(src).first(255), false, out));
    EXPECT_TRUE(out.empty());
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Index4, 16, 16, std::span(src).first(127), false, out));
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Rgb16, 16, 16, std::span(src).first(511), false, out));
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Rgb24, 16, 16, std::span(src).first(767), false, out));
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Rgb32, 16, 16, std::span(src).first(1023), false, out));
    EXPECT_TRUE(tim2::DecodeLevel(tim2::Format::Rgb32, 16, 16, src, false, out));

    EXPECT_EQ(tim2::SourceBytes(tim2::Format::Index8, 128, 64, true), tim2::SwizzledBytes(128, 64));
    EXPECT_EQ(tim2::SourceBytes(tim2::Format::Rgb16, 128, 64, true), 128u * 64 * 2);
    std::vector<unsigned char> page(tim2::SwizzledBytes(128, 64));
    EXPECT_TRUE(tim2::DecodeLevel(tim2::Format::Index8, 128, 64, page, true, out));
    EXPECT_FALSE(tim2::DecodeLevel(tim2::Format::Index8, 128, 64, std::span(page).first(page.size() - 1), true, out));
    std::vector<unsigned char> linear(128 * 64);
    EXPECT_FALSE(tim2::Unswizzle8(128, 64, std::span(page).first(page.size() - 1), linear));
    EXPECT_FALSE(tim2::Unswizzle8(128, 64, page, std::span(linear).first(linear.size() - 1)));
}

// Malformed files: whatever Parse accepts decodes from exactly the bytes it names.
TEST(TexTim2, MalformedHeadersNeverReachPastTheFile) {
    std::mt19937 random(2026);
    auto         valid = Tim2File(16, 16, 5, 256, 1024, 256);
    std::vector<std::uint8_t> out;
    for (int round = 0; round < 20000; round++) {
        std::vector<unsigned char> file = valid;
        int                        changes = 1 + static_cast<int>(random() % 4);
        for (int i = 0; i < changes; i++) {
            std::size_t at = 4 + random() % (16 + 0x30 - 4);
            file[at] = static_cast<unsigned char>(random());
        }
        if (random() % 4 == 0) {
            file.resize(random() % (file.size() + 1));
        }
        auto pictures = tim2::Parse(file);
        if (!pictures) {
            continue;
        }
        for (const tim2::Picture &picture : *pictures) {
            ASSERT_LE(picture.clut + picture.clut_size, file.size());
            for (const tim2::Level &level : picture.levels) {
                ASSERT_LE(level.offset + level.size, file.size());
                // A copy of exactly the level, so a read past it leaves the allocation.
                std::vector<unsigned char> bytes(file.begin() + static_cast<std::ptrdiff_t>(level.offset),
                                                 file.begin() + static_cast<std::ptrdiff_t>(level.offset + level.size));
                auto format = *tim2::FormatOfImageType(static_cast<int>(picture.image_type));
                EXPECT_TRUE(tim2::DecodeLevel(format, level.width, level.height, bytes, false, out));
                tim2::DecodeLevel(format, level.width, level.height, bytes, true, out);
            }
            tim2::Colours256(file, picture);
        }
    }
}

// TIM2 dimensions are 16-bit; past that a level's byte count and offsets are refused, and up to it
// they are 64-bit, so a 65535-square level's offsets pass 2^31 on every system.
TEST(TexTim2, HugeDimensionsAreBoundedOrRefused) {
    EXPECT_EQ(tim2::SwizzledBytes(1 << 20, 1 << 20), 0u);
    EXPECT_EQ(tim2::SwizzledBytes(0x10000, 64), 0u);
    std::vector<unsigned char> tiny(16);
    std::vector<unsigned char> linear(16);
    EXPECT_FALSE(tim2::Unswizzle8(0x10000, 1, tiny, linear));
    EXPECT_FALSE(tim2::Unswizzle8(1, 0x7FFFFFFF, tiny, linear));

    std::uint64_t furthest = tim2::SwizzledBytes(0xFFFF, 0xFFFF);
    EXPECT_GT(furthest, 0x7FFFFFFFu);
    EXPECT_LE(furthest, std::uint64_t{1} << 32);
    auto corner = tim2::SwizzledOffset(0xFFFF, 0xFFFF, 0, 0xFFC0);
    ASSERT_TRUE(corner.has_value());
    EXPECT_EQ(*corner, std::uint64_t{1023} * 512 * 256 * 32);

    auto pictures = tim2::Parse(Tim2File(0xFFFF, 0xFFFF, 5, 16, 1024, 256));
    EXPECT_FALSE(pictures.has_value());
}
