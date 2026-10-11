#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "sce_core/host0.hpp"

namespace host0 = sce_core::host0;

TEST(SceCoreHost0, EveryDeviceIsTheSaveRootsHost0Directory) {
    auto root = std::filesystem::temp_directory_path() / ("kz_host0_" + std::to_string(kz::test::ProcessId()));
    std::filesystem::remove_all(root);
    PathsSetSaveRoot(root);
    int fd = host0::Open("host0:logs\\debug.txt", host0::kWriteOnly | host0::kCreate | host0::kTruncate);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(host0::Write(fd, "line\n", 5), 5);
    EXPECT_EQ(host0::Close(fd), 0);
    EXPECT_EQ(host0::Close(fd), -EBADF);
    EXPECT_EQ(std::filesystem::file_size(root / "host0/logs/debug.txt"), 5u);

    fd = host0::Open("cdrom0:/logs/debug.txt", host0::kReadOnly);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(host0::Lseek(fd, 1, SEEK_SET), 1);
    char text[8] = {};
    EXPECT_EQ(host0::Read(fd, text, sizeof(text)), 4);
    EXPECT_STREQ(text, "ine\n");
    EXPECT_EQ(host0::Close(fd), 0);
    std::filesystem::remove_all(root);
}

TEST(SceCoreHost0, NoPathLeavesTheDirectory) {
    EXPECT_EQ(host0::Open("host0:../outside", host0::kReadOnly), -ENOENT);
    EXPECT_EQ(host0::Open("host0:", host0::kReadOnly), -ENOENT);
    EXPECT_EQ(host0::Open(nullptr, host0::kReadOnly), -ENOENT);
    EXPECT_EQ(host0::Read(12345, nullptr, 0), -EBADF);
}
