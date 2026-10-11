#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "data/iso9660.hpp"
#include "data_fixture.hpp"

namespace fs = std::filesystem;
using namespace kz::test;

TEST(DataIso9660, FindsFilesByFoldedPath) {
    fs::path  dir = TempDir("iso_find");
    SmallDisc disc;
    WriteBytes(dir / "disc.iso", disc.Image());

    kzdata::Iso9660 iso(dir / "disc.iso");
    auto            movie = iso.FindPath("movie/open1.pss");
    ASSERT_TRUE(movie.has_value());
    EXPECT_FALSE(movie->directory);
    EXPECT_EQ(kzdata::ReadExtent(iso.ExtentOf(*movie)), disc.movie);
    EXPECT_FALSE(iso.FindPath("MOVIE/OPEN2.PSS").has_value());
    EXPECT_FALSE(iso.FindPath("SYSTEM.CNF/OPEN1.PSS").has_value());
    EXPECT_TRUE(iso.FindPath("MOVIE")->directory);
    EXPECT_EQ(kzdata::Iso9660::StripVersion("NOEXT.;1"), "NOEXT");
    fs::remove_all(dir);
}

TEST(DataIso9660, WalksEveryFile) {
    fs::path  dir = TempDir("iso_walk");
    SmallDisc disc;
    WriteBytes(dir / "disc.iso", disc.Image());

    kzdata::Iso9660 iso(dir / "disc.iso");
    auto            files = iso.Walk(iso.Root());
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files[0].first, "DATA.BIN");
    EXPECT_EQ(files[1].first, "MOVIE/OPEN1.PSS");
    EXPECT_EQ(files[2].first, "SYSTEM.CNF");
    EXPECT_EQ(kzdata::ReadExtent(iso.ExtentOf(files[0].second)), disc.data);
    fs::remove_all(dir);
}

TEST(DataIso9660, RefusesBrokenImages) {
    fs::path  dir = TempDir("iso_broken");
    SmallDisc disc;
    Bytes     image = disc.Image();

    Bytes cut(image.begin(), image.end() - kzdata::kSector);
    WriteBytes(dir / "cut.iso", cut);
    kzdata::Iso9660 iso(dir / "cut.iso");
    auto            movie = iso.FindPath("MOVIE/OPEN1.PSS");
    ASSERT_TRUE(movie.has_value());
    EXPECT_THROW(iso.ExtentOf(*movie), kzdata::Error);

    Bytes loop = image;
    Bytes self = DirectoryRecord("MOVIE", 18, kzdata::kSector, true);
    std::size_t movie_record = 18 * kzdata::kSector + 34 + 34 + DirectoryRecord("DATA.BIN;1", 0, 0, false).size();
    std::memcpy(&loop[movie_record], self.data(), self.size());
    WriteBytes(dir / "loop.iso", loop);
    kzdata::Iso9660 looping(dir / "loop.iso");
    EXPECT_THROW(looping.Walk(looping.Root()), kzdata::Error);

    WriteBytes(dir / "junk.iso", Bytes(40 * kzdata::kSector, 0x55));
    EXPECT_THROW(kzdata::Iso9660(dir / "junk.iso"), kzdata::Error);
    WriteBytes(dir / "tiny.iso", Bytes(100, 0));
    EXPECT_THROW(kzdata::Iso9660(dir / "tiny.iso"), kzdata::Error);
    fs::remove_all(dir);
}
