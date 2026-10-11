#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <string_view>

#include "data_fixture.hpp"
#include "platform/config_store.hpp"
#include "platform/display.hpp"
#include "platform/paths.hpp"

namespace {

struct GameConfig : ConfigBase {
    bool game_flag = false;

    bool operator==(const GameConfig &) const = default;
};

GameConfig ParseGame(std::string_view text) {
    GameConfig config;
    ConfigParseKeys(text, [&](std::string_view name, const ConfigJson &value) {
        if (name == "game.flag") {
            return ConfigReadBool(value, config.game_flag);
        }
        return ConfigBaseApply(config, name, value);
    });
    return config;
}

std::string SerializeGame(const GameConfig &config) {
    ConfigJson root;
    ConfigBaseSerialize(config, root);
    root["game"]["flag"] = config.game_flag;
    return root.dump();
}

WindowModeResult TakeMode(const WindowConfig &config) {
    WindowMode mode{config.fullscreen, config.width, config.height};
    return {mode, mode, mode, std::nullopt};
}

bool NoMode(WindowMode &) {
    return false;
}

bool NoEvent() {
    return false;
}

} // namespace

// The host's part of the settings, as a ConfigBase: the game's own fields stay as they are.
TEST(PlatformDisplay, ApplyTakesAPlainConfigBase) {
    std::filesystem::path dir = kz::test::TempDir("display_base");
    PathsSetSaveRoot(dir);
    ConfigStoreOf<GameConfig> store(ParseGame, SerializeGame);
    store.Load();
    GameConfig game = store.Get();
    game.game_flag = true;
    ASSERT_TRUE(store.Change(game));
    DisplayUseWindow({TakeMode, NoMode, NoEvent});
    DisplaySetOverrides(0, 0, false);

    ConfigBase host = ConfigBaseGet();
    host.window_width = 1024;
    host.window_height = 768;
    EXPECT_NO_THROW(EXPECT_TRUE(DisplayApply(host)));
    EXPECT_EQ(store.Get().window_width, 1024);
    EXPECT_TRUE(store.Get().game_flag);
    DisplayUseWindow({});
    std::filesystem::remove_all(dir);
}
