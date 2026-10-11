#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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

// Threads opening, using and closing their own files at once: the open set stays consistent and
// no call reaches a descriptor another thread has closed.
TEST(SceCoreHost0, ConcurrentCallsKeepTheirDescriptors) {
    auto root = std::filesystem::temp_directory_path() / ("kz_host0_threads_" + std::to_string(kz::test::ProcessId()));
    std::filesystem::remove_all(root);
    PathsSetSaveRoot(root);
    std::atomic<int>         failures = 0;
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++) {
        threads.emplace_back([t, &failures] {
            std::string name = "host0:t" + std::to_string(t) + ".bin";
            for (int i = 0; i < 400; i++) {
                int fd = host0::Open(name.c_str(), host0::kReadWrite | host0::kCreate | host0::kTruncate);
                char byte = static_cast<char>(i);
                char back = 0;
                if (fd < 0 || host0::Write(fd, &byte, 1) != 1 || host0::Lseek(fd, 0, SEEK_SET) != 0 ||
                    host0::Read(fd, &back, 1) != 1 || back != byte || host0::Close(fd) != 0) {
                    failures++;
                }
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }
    EXPECT_EQ(failures, 0);
    std::filesystem::remove_all(root);
}

TEST(SceCoreHost0, ASeekPastTwoGigabytesOverflows) {
    auto root = std::filesystem::temp_directory_path() / ("kz_host0_seek_" + std::to_string(kz::test::ProcessId()));
    std::filesystem::remove_all(root);
    PathsSetSaveRoot(root);
    int fd = host0::Open("host0:big", host0::kReadWrite | host0::kCreate);
    ASSERT_GE(fd, 0);
    EXPECT_EQ(host0::Lseek(fd, 0x7FFFFFFF, SEEK_SET), 0x7FFFFFFF);
    EXPECT_EQ(host0::Lseek(fd, 1, SEEK_CUR), -EOVERFLOW);
    EXPECT_EQ(host0::Close(fd), 0);
    std::filesystem::remove_all(root);
}
