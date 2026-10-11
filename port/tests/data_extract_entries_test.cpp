#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "data/extract.hpp"
#include "data_fixture.hpp"

namespace fs = std::filesystem;
using namespace kz::test;

namespace {

std::vector<kzdata::Entry> Entries(const fs::path &source) {
    return {
        {"system.cnf",      {source, 0, 100}  },
        {"movie/open1.pss", {source, 100, 900}},
        {"empty.bin",       {source, 1000, 0} },
    };
}

kzdata::Result Extract(const std::vector<kzdata::Entry> &entries, const fs::path &out) {
    kzdata::Manifest next;
    kzdata::Result   result = kzdata::ExtractEntries(entries, out, next);
    kzdata::WriteManifest(out, next);
    return result;
}

bool NoTemporaries(const fs::path &out) {
    for (const auto &entry : fs::recursive_directory_iterator(out)) {
        if (entry.path().extension() == ".kztmp") {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(DataExtractEntries, WritesTheExtentsAndAManifest) {
    fs::path dir = TempDir("entries_write");
    Bytes    source = Pattern(1000, 4);
    WriteBytes(dir / "source.bin", source);

    kzdata::Result result = Extract(Entries(dir / "source.bin"), dir / "out");
    EXPECT_EQ(result.written, 3u);
    EXPECT_EQ(result.bytes, 1000u);
    EXPECT_EQ(ReadBytes(dir / "out/movie/open1.pss"), Bytes(source.begin() + 100, source.end()));
    EXPECT_TRUE(fs::is_regular_file(dir / "out/empty.bin"));
    EXPECT_TRUE(NoTemporaries(dir / "out"));

    kzdata::Manifest manifest = kzdata::ReadManifest(dir / "out");
    ASSERT_EQ(manifest.size(), 3u);
    EXPECT_EQ(manifest["system.cnf"].sha256, kzdata::Sha256Hex(std::span(source).first(100)));
    EXPECT_EQ(manifest["movie/open1.pss"].offset, 100u);
    EXPECT_TRUE(kzdata::Verify(dir / "out").empty());
    fs::remove_all(dir);
}

TEST(DataExtractEntries, KeepsWhatIsAlreadyThere) {
    fs::path dir = TempDir("entries_keep");
    WriteBytes(dir / "source.bin", Pattern(1000, 5));
    auto entries = Entries(dir / "source.bin");
    Extract(entries, dir / "out");

    kzdata::Result again = Extract(entries, dir / "out");
    EXPECT_EQ(again.written, 0u);
    EXPECT_EQ(again.kept, 3u);

    // Without a manifest a file of the right size is kept only if its bytes are the extent's.
    fs::remove(dir / "out" / kzdata::kManifestFile);
    WriteBytes(dir / "out/system.cnf", Pattern(100, 9));
    kzdata::Result checked = Extract(entries, dir / "out");
    EXPECT_EQ(checked.written, 1u);
    EXPECT_EQ(checked.kept, 2u);
    EXPECT_TRUE(kzdata::Verify(dir / "out").empty());
    fs::remove_all(dir);
}

TEST(DataExtractEntries, VerifyFindsChangedFiles) {
    fs::path dir = TempDir("entries_verify");
    WriteBytes(dir / "source.bin", Pattern(1000, 6));
    Extract(Entries(dir / "source.bin"), dir / "out");

    WriteBytes(dir / "out/system.cnf", Pattern(100, 7));
    fs::remove(dir / "out/movie/open1.pss");
    std::vector<std::string> problems = kzdata::Verify(dir / "out");
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_NE(problems[0].find("movie/open1.pss"), std::string::npos);
    EXPECT_NE(problems[1].find("system.cnf"), std::string::npos);

    EXPECT_THROW(kzdata::Verify(dir / "nothing"), kzdata::Error);
    fs::remove_all(dir);
}

TEST(DataExtractEntries, RefusesCollisionsBeforeWriting) {
    fs::path dir = TempDir("entries_refuse");
    WriteBytes(dir / "source.bin", Pattern(1000, 8));
    fs::path source = dir / "source.bin";
    fs::path out = dir / "out";

    auto refused = [&](std::vector<kzdata::Entry> entries) {
        kzdata::Manifest next;
        try {
            kzdata::ExtractEntries(entries, out, next);
        } catch (const kzdata::Error &) {
            return !fs::exists(out);
        }
        return false;
    };
    EXPECT_TRUE(refused({{"a.bin", {source, 0, 10}}, {"a.bin", {source, 10, 10}}}));
    EXPECT_TRUE(refused({{"a", {source, 0, 10}}, {"a/b.bin", {source, 10, 10}}}));
    EXPECT_TRUE(refused({{"../a.bin", {source, 0, 10}}}));
    EXPECT_TRUE(refused({{"/a.bin", {source, 0, 10}}}));
    EXPECT_TRUE(refused({{"c:/a.bin", {source, 0, 10}}}));
    EXPECT_TRUE(refused({{"a\tb.bin", {source, 0, 10}}}));
    EXPECT_TRUE(refused({{std::string(kzdata::kManifestFile), {source, 0, 10}}}));
    EXPECT_TRUE(refused({{"a.bin", {source, 995, 10}}}));
    EXPECT_TRUE(refused({{"a.bin", {dir / "missing.bin", 0, 10}}}));

    WriteBytes(out / "a", Pattern(3, 0));
    kzdata::Manifest next;
    EXPECT_THROW(kzdata::ExtractEntries(std::vector<kzdata::Entry>{{"a/b.bin", {source, 0, 10}}}, out, next),
                 kzdata::Error);
    EXPECT_EQ(ReadBytes(out / "a"), Pattern(3, 0));
    fs::remove_all(dir);
}

TEST(DataExtractEntries, CancelLeavesNoPartialFile) {
    fs::path dir = TempDir("entries_cancel");
    WriteBytes(dir / "source.bin", Pattern(1000, 10));
    kzdata::Manifest next;
    int              calls = 0;
    EXPECT_THROW(kzdata::ExtractEntries(Entries(dir / "source.bin"), dir / "out", next,
                                        [&](const kzdata::Progress &) { return ++calls < 3; }),
                 kzdata::Error);
    EXPECT_TRUE(fs::is_regular_file(dir / "out/system.cnf"));
    EXPECT_FALSE(fs::exists(dir / "out/movie/open1.pss"));
    EXPECT_TRUE(NoTemporaries(dir / "out"));
    fs::remove_all(dir);
}

TEST(DataExtractEntries, ManifestRejectsDamage) {
    fs::path dir = TempDir("entries_manifest");
    WriteBytes(dir / kzdata::kManifestFile, Bytes{'x', '\t', 'y', '\n'});
    EXPECT_THROW(kzdata::ReadManifest(dir), kzdata::Error);
    std::string row = std::string(64, 'a') + "\t1\ts@1\t0\t../escape\n";
    WriteBytes(dir / kzdata::kManifestFile, Bytes(row.begin(), row.end()));
    EXPECT_THROW(kzdata::ReadManifest(dir), kzdata::Error);
    fs::remove_all(dir);
}

TEST(DataExtractEntries, RefusesBackslashesThatLeaveTheRoot) {
    fs::path dir = TempDir("entries_backslash");
    WriteBytes(dir / "source.bin", Pattern(100, 11));
    kzdata::Manifest next;
    for (const char *path : {"..\\a.bin", "a\\b.bin", "c:a.bin"}) {
        EXPECT_THROW(kzdata::ExtractEntries(std::vector<kzdata::Entry>{{path, {dir / "source.bin", 0, 10}}},
                                            dir / "out", next),
                     kzdata::Error)
            << path;
    }
    EXPECT_FALSE(fs::exists(dir / "out"));
    std::string row = std::string(64, 'a') + "\t1\ts@1\t0\t..\\escape\n";
    fs::create_directories(dir / "m");
    WriteBytes(dir / "m" / kzdata::kManifestFile, Bytes(row.begin(), row.end()));
    EXPECT_THROW(kzdata::ReadManifest(dir / "m"), kzdata::Error);
    fs::remove_all(dir);
}

namespace {

// Creating a link needs a privilege Windows does not give everyone.
bool Linked(const fs::path &target, const fs::path &link, bool directory) {
    std::error_code error;
    directory ? fs::create_directory_symlink(target, link, error) : fs::create_symlink(target, link, error);
    return !error;
}

} // namespace

TEST(DataExtractEntries, RefusesLinksUnderTheRoot) {
    fs::path dir = TempDir("entries_links");
    WriteBytes(dir / "source.bin", Pattern(1000, 12));
    fs::create_directories(dir / "elsewhere");
    fs::create_directories(dir / "out");
    if (!Linked(dir / "elsewhere", dir / "out/movie", true)) {
        fs::remove_all(dir);
        GTEST_SKIP() << "cannot create links here";
    }
    kzdata::Manifest next;
    EXPECT_THROW(kzdata::ExtractEntries(Entries(dir / "source.bin"), dir / "out", next), kzdata::Error);
    EXPECT_TRUE(fs::is_empty(dir / "elsewhere"));
    EXPECT_FALSE(fs::exists(dir / "out/system.cnf"));

    fs::remove(dir / "out/movie");
    WriteBytes(dir / "elsewhere/kept", Pattern(100, 1));
    ASSERT_TRUE(Linked(dir / "elsewhere/kept", dir / "out/system.cnf", false));
    EXPECT_THROW(kzdata::ExtractEntries(Entries(dir / "source.bin"), dir / "out", next), kzdata::Error);
    EXPECT_EQ(ReadBytes(dir / "elsewhere/kept"), Pattern(100, 1));
    fs::remove_all(dir);
}

TEST(DataExtractEntries, WritesNoTemporaryThroughALink) {
    fs::path dir = TempDir("entries_temporary_link");
    WriteBytes(dir / "source.bin", Pattern(1000, 13));
    WriteBytes(dir / "victim", Pattern(10, 2));
    fs::create_directories(dir / "out/movie");
    if (!Linked(dir / "victim", dir / "out/.system.cnf.kztmp", false) ||
        !Linked(dir / "victim", dir / "out/movie/.open1.pss.kztmp", false)) {
        fs::remove_all(dir);
        GTEST_SKIP() << "cannot create links here";
    }
    Extract(Entries(dir / "source.bin"), dir / "out");
    EXPECT_EQ(ReadBytes(dir / "victim"), Pattern(10, 2));
    EXPECT_TRUE(kzdata::Verify(dir / "out").empty());
    EXPECT_TRUE(NoTemporaries(dir / "out"));

    ASSERT_TRUE(Linked(dir / "victim", dir / "out/.manifest.sha256.kztmp", false));
    kzdata::WriteFile(dir / "out" / kzdata::kManifestFile, Bytes{'#', '\n'});
    EXPECT_EQ(ReadBytes(dir / "victim"), Pattern(10, 2));
    fs::remove_all(dir);
}

TEST(DataExtractEntries, VerifyReportsLinks) {
    fs::path dir = TempDir("entries_verify_link");
    WriteBytes(dir / "source.bin", Pattern(1000, 14));
    Extract(Entries(dir / "source.bin"), dir / "out");
    fs::rename(dir / "out/system.cnf", dir / "moved");
    if (!Linked(dir / "moved", dir / "out/system.cnf", false)) {
        fs::remove_all(dir);
        GTEST_SKIP() << "cannot create links here";
    }
    EXPECT_EQ(kzdata::Verify(dir / "out").size(), 1u);
    fs::remove_all(dir);
}

TEST(DataExtractEntries, KeepsOnlyFilesHoldingTheExtentsBytes) {
    fs::path dir = TempDir("entries_prove");
    Bytes    source = Pattern(1000, 15);
    WriteBytes(dir / "source.bin", source);
    auto entries = Entries(dir / "source.bin");
    Extract(entries, dir / "out");

    // An output edited in place, its size and the manifest unchanged.
    WriteBytes(dir / "out/system.cnf", Pattern(100, 16));
    kzdata::Result edited = Extract(entries, dir / "out");
    EXPECT_EQ(edited.written, 1u);
    EXPECT_EQ(ReadBytes(dir / "out/system.cnf"), Bytes(source.begin(), source.begin() + 100));

    // A different disc under the same name and size.
    Bytes other = Pattern(1000, 17);
    WriteBytes(dir / "source.bin", other);
    kzdata::Result replaced = Extract(entries, dir / "out");
    EXPECT_EQ(replaced.written, 2u);
    EXPECT_EQ(replaced.kept, 1u);
    EXPECT_EQ(ReadBytes(dir / "out/movie/open1.pss"), Bytes(other.begin() + 100, other.end()));
    EXPECT_TRUE(kzdata::Verify(dir / "out").empty());
    fs::remove_all(dir);
}
