#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>

#include "platform/paths.hpp"
#include "platform_fixture.hpp"
#include "runtime/mw_runtime.hpp"
#include "sce_core/mc_store.hpp"

namespace {

struct ConstructedBeforeMain {
    ConstructedBeforeMain() { value = 0x5A5A; }
    int value = 0;
};

ConstructedBeforeMain g_constructed;

constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

} // namespace

TEST(RuntimeMw, StaticConstructorsRunBeforeMain) {
    EXPECT_EQ(g_constructed.value, 0x5A5A);
}

TEST(RuntimeMw, FptosiTruncatesAndSaturates) {
    struct Case {
        float value;
        int   expected;
    };
    const Case cases[] = {
        {0.0f, 0},
        {-0.0f, 0},
        {0.99f, 0},
        {-0.99f, 0},
        {1.5f, 1},
        {-1.5f, -1},
        {16777216.0f, 16777216},
        {2147483520.0f, 2147483520},
        {2147483648.0f, INT32_MAX},
        {-2147483648.0f, INT32_MIN},
        {-3.0e10f, INT32_MIN},
        {kInf, INT32_MAX},
        {-kInf, INT32_MIN},
        {kNan, 0},
        {1.0e-40f, 0},
    };
    for (const Case &c : cases) {
        EXPECT_EQ(runtime::Fptosi(c.value), c.expected) << c.value;
    }
}

TEST(RuntimeMw, FptouiTakesNegativesToZeroAndSaturates) {
    struct Case {
        float        value;
        unsigned int expected;
    };
    const Case cases[] = {
        {0.0f, 0u},
        {0.99f, 0u},
        {-0.5f, 0u},
        {-1.0f, 0u},
        {-3.0e10f, 0u},
        {2.5f, 2u},
        {2147483648.0f, 2147483648u},
        {4294967040.0f, 4294967040u},
        {4294967296.0f, UINT32_MAX},
        {kInf, UINT32_MAX},
        {-kInf, 0u},
        {kNan, 0u},
    };
    for (const Case &c : cases) {
        EXPECT_EQ(runtime::Fptoui(c.value), c.expected) << c.value;
    }
}

#ifndef _WIN32
namespace {

std::filesystem::path g_marker;

void WriteMarker() {
    std::FILE *file = std::fopen(g_marker.c_str(), "w");
    std::fputs("handler", file);
    std::fclose(file);
}

} // namespace

TEST(RuntimeMw, ExitClosesTheCardsRunsHandlersAndReturnsTheStatus) {
    auto root = std::filesystem::temp_directory_path() / ("kz_runtime_exit_" + std::to_string(kz::test::ProcessId()));
    std::filesystem::remove_all(root);
    PathsSetSaveRoot(root);
    g_marker = root / "marker";
    KZ_ASSERT_EXIT(
        [] {
            sce_core::mc::Init();
            sce_core::mc::Open(0, "save", sce_core::mc::kOpenWrite | sce_core::mc::kOpenCreate);
            int function = -1;
            int fd = -1;
            sce_core::mc::Sync(&function, &fd);
            sce_core::mc::Write(fd, "game", 4);
            runtime::AtExit(WriteMarker);
            runtime::Exit(7);
        },
        7);
    EXPECT_EQ(std::filesystem::file_size(root / "mc0/save"), 4u);
    EXPECT_TRUE(std::filesystem::exists(g_marker));
    std::filesystem::remove_all(root);
}

TEST(RuntimeMw, AFailedAssertionEndsWithTheGamesStatus) {
    KZ_ASSERT_EXIT([] { runtime::AssertFailed("unit.cpp", 12, "x != 0", 4); }, 4);
}
#endif
