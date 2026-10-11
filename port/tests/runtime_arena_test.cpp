#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "runtime/arena.hpp"

TEST(RuntimeArena, AnOwnerKeepsItsBlockWhileItFitsAndClearAllZeroesIt) {
    static const char owner = 0;
    unsigned char    *block = ArenaBlock(&owner, 4096);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(block) % 64, 0u);
    std::memset(block, 0xCD, 4096);
    EXPECT_EQ(ArenaBlock(&owner, 1024), block);
    ArenaClearAll();
    EXPECT_EQ(block[0], 0);
    EXPECT_EQ(block[4095], 0);
}

TEST(RuntimeArena, WidenNearFindsTheTruncatedAddressNearTheAnchor) {
    constexpr std::uintptr_t kAnchor = std::uintptr_t{0x7F00} << 32 | 0x10000000u;
    EXPECT_EQ(WidenNear(kAnchor, 0x10001234u), (kAnchor & ~std::uintptr_t{0xFFFFFFFF}) | 0x10001234u);
    EXPECT_EQ(WidenNear(kAnchor, 0xF0000000u), (std::uintptr_t{0x7EFF} << 32) | 0xF0000000u);
    static const int image = 0;
    auto             address = reinterpret_cast<std::uintptr_t>(&image);
    EXPECT_EQ(PortImagePointer(static_cast<std::int32_t>(address)), &image);
    EXPECT_EQ(PortImagePointer(0), nullptr);
}
