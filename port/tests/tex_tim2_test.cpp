#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
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
            long offset = tim2::SwizzledOffset(width, height, x, y);
            ASSERT_GE(offset, 0);
            hits[static_cast<std::size_t>(offset)]++;
            swizzled[static_cast<std::size_t>(offset)] = static_cast<unsigned char>(x ^ y);
        }
    }
    for (int hit : hits) {
        EXPECT_EQ(hit, 1);
    }
    std::vector<unsigned char> linear(swizzled.size());
    tim2::Unswizzle8(width, height, swizzled.data(), linear.data());
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
