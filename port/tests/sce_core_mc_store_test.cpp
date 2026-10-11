#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "sce_core/mc_store.hpp"

namespace mc = sce_core::mc;

namespace {

std::filesystem::path UseTempSaveRoot(const char *name) {
    auto root = std::filesystem::temp_directory_path() / (std::string(name) + std::to_string(kz::test::ProcessId()));
    std::filesystem::remove_all(root);
    PathsSetSaveRoot(root);
    return root;
}

// The function number and result of the command just issued.
std::pair<int, int> Synced() {
    int function = -1;
    int result = -1;
    EXPECT_EQ(mc::Sync(&function, &result), 1);
    return {function, result};
}

} // namespace

TEST(SceCoreMcStore, ACommandFinishesInTheCallThatIssuesIt) {
    UseTempSaveRoot("kz_mc_sync_");
    ASSERT_EQ(mc::Init(), 0);
    EXPECT_EQ(mc::Sync(nullptr, nullptr), -1);
    int type = 0, free_clusters = 0, formatted = 0;
    ASSERT_EQ(mc::GetInfo(0, &type, &free_clusters, &formatted), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x01, int{mc::kSucceed}));
    EXPECT_EQ(type, mc::kTypePs2);
    EXPECT_EQ(formatted, 1);
    EXPECT_EQ(mc::Sync(nullptr, nullptr), -1);
    ASSERT_EQ(mc::GetInfo(1, &type, &free_clusters, &formatted), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x01, int{mc::kNoCard}));
    EXPECT_EQ(type, mc::kTypeNoCard);
}

TEST(SceCoreMcStore, AFileIsAFileUnderTheCardsDirectory) {
    auto root = UseTempSaveRoot("kz_mc_file_");
    mc::Init();
    ASSERT_EQ(mc::Mkdir(0, "SAVE"), 0);
    ASSERT_EQ(Synced().second, mc::kSucceed);
    ASSERT_EQ(mc::Open(0, "/SAVE/data", mc::kOpenWrite | mc::kOpenCreate), 0);
    int fd = Synced().second;
    ASSERT_GE(fd, 0);
    ASSERT_EQ(mc::Write(fd, "save", 4), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x06, 4));
    ASSERT_EQ(mc::Read(fd, nullptr, 0), 0);
    EXPECT_EQ(Synced().second, mc::kDeniedPermit);
    ASSERT_EQ(mc::Close(fd), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x03, int{mc::kSucceed}));

    std::ifstream file(root / "mc0/SAVE/data", std::ios::binary);
    std::string   text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    EXPECT_EQ(text, "save");

    ASSERT_EQ(mc::Chdir(0, "SAVE", nullptr), 0);
    unsigned char table[4][mc::kDirEntrySize] = {};
    ASSERT_EQ(mc::GetDir(0, "*", 0, 4, table), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x0D, 3));
    EXPECT_STREQ(reinterpret_cast<char *>(table[2] + 0x20), "data");
    std::filesystem::remove_all(root);
}

TEST(SceCoreMcStore, CloseAllWritesOutWhatIsBuffered) {
    auto root = UseTempSaveRoot("kz_mc_close_");
    mc::Init();
    ASSERT_EQ(mc::Open(0, "pending", mc::kOpenWrite | mc::kOpenCreate), 0);
    int fd = Synced().second;
    ASSERT_GE(fd, 0);
    ASSERT_EQ(mc::Write(fd, "unflushed", 9), 0);
    mc::CloseAll();
    EXPECT_EQ(std::filesystem::file_size(root / "mc0/pending"), 9u);
    ASSERT_EQ(mc::Close(fd), 0);
    EXPECT_EQ(Synced().second, mc::kNoEntry);
    std::filesystem::remove_all(root);
}

TEST(SceCoreMcStore, ANegativeSizeIsRefused) {
    auto root = UseTempSaveRoot("kz_mc_negative_");
    mc::Init();
    ASSERT_EQ(mc::Open(0, "/data", mc::kOpenRead | mc::kOpenWrite | mc::kOpenCreate), 0);
    int fd = Synced().second;
    ASSERT_GE(fd, 0);
    ASSERT_EQ(mc::Write(fd, "save", 4), 0);
    ASSERT_EQ(Synced().second, 4);
    ASSERT_EQ(mc::Close(fd), 0);
    Synced();

    ASSERT_EQ(mc::Open(0, "/data", mc::kOpenRead | mc::kOpenWrite), 0);
    fd = Synced().second;
    ASSERT_GE(fd, 0);
    char buffer[2] = {'x', 'x'};
    ASSERT_EQ(mc::Read(fd, buffer, -1), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x05, int{mc::kDeniedPermit}));
    EXPECT_EQ(buffer[0], 'x');
    ASSERT_EQ(mc::Write(fd, buffer, -1), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x06, int{mc::kDeniedPermit}));
    ASSERT_EQ(mc::Read(fd, buffer, 2), 0);
    EXPECT_EQ(Synced(), std::make_pair(0x05, 2));
    EXPECT_EQ(std::string(buffer, 2), "sa");
    mc::Close(fd);
    Synced();
    std::filesystem::remove_all(root);
}
