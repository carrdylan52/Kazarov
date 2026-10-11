#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "data/path.hpp"

TEST(DataPath, RefusesPathsThatLeaveTheRoot) {
    for (const char *path : {"..\\outside", "a\\..\\..\\b", "a/..\\b", "\\a.bin", "\\\\server\\share\\a",
                             "c:a.bin", "c:\\a.bin", "C:/a.bin", "a/c:b", "../a", "/a", "a//b", "a/./b", ".",
                             "a/", "a\\", "", "a/.. ", "a/...", "a/b."}) {
        EXPECT_FALSE(kzdata::IsSafeRelative(path)) << path;
    }
    EXPECT_FALSE(kzdata::IsSafeRelative(std::string_view("a\0b", 3)));
}

TEST(DataPath, NormalisesSeparators) {
    EXPECT_EQ(kzdata::SafeRelative("chara\\c01\\a.bin"), std::optional<std::string>("chara/c01/a.bin"));
    EXPECT_EQ(kzdata::SafeRelative("a/b.bin"), std::optional<std::string>("a/b.bin"));
    EXPECT_TRUE(kzdata::IsSafeRelative("data.hd2_"));
    EXPECT_TRUE(kzdata::IsSafeRelative("..a/b..c/.d"));
    EXPECT_EQ(kzdata::SafeRelative("..\\a"), std::nullopt);
}
