#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

#include "platform/config_base.hpp"

namespace {

ConfigBase ParseHost(std::string_view text) {
    ConfigBase config;
    ConfigParseKeys(text, [&](std::string_view name, const ConfigJson &value) {
        return ConfigBaseApply(config, name, value);
    });
    return config;
}

std::string SerializeHost(const ConfigBase &config) {
    ConfigJson root;
    ConfigBaseSerialize(config, root);
    return root.dump();
}

} // namespace

TEST(PlatformConfigBase, EmptyAndInvalidTextsKeepTheDefaults) {
    ASSERT_TRUE(ParseHost("") == ConfigBase{});
    ASSERT_TRUE(ParseHost(" \n") == ConfigBase{});
    ASSERT_TRUE(ParseHost("[1, 2]") == ConfigBase{});
    ASSERT_TRUE(ParseHost(R"({"game": {"tick_rate": 50})") == ConfigBase{});
}

TEST(PlatformConfigBase, RefusedKeysAreReported) {
    testing::internal::CaptureStderr();
    ConfigBase config = ParseHost(R"({"game": {"tick_rate": -3, "language": "english"}, "video": 1})");
    std::string reported = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(config == ConfigBase{});
    EXPECT_NE(reported.find("ignoring game.tick_rate = -3"), std::string::npos);
    EXPECT_NE(reported.find("ignoring game.language"), std::string::npos);
    EXPECT_NE(reported.find("ignoring video = 1"), std::string::npos);
}

TEST(PlatformConfigBase, SerializeRoundTrips) {
    ConfigBase config;
    config.tick_rate = 50.0;
    config.present_mode = ConfigPresentMode::Mailbox;
    config.aspect = ConfigAspect::FourThree;
    config.ui_scale = 1.5f;
    config.window_width = 1920;
    config.surround = true;
    config.gyro = ConfigGyro::FirstPerson;
    config.glyph_device = ConfigGlyphDevice::SteamDeck;
    config.mouse_sensitivity = 0.1f;
    config.key_bindings = {{"cross", {"Space", "Z"}}, {"start", {"Return"}}};
    config.mouse_release_keys = {"F1"};
    ConfigBase again = ParseHost(SerializeHost(config));
    ASSERT_TRUE(again == config);
    ASSERT_NE(SerializeHost(config).find("\"mouse_sensitivity\":0.1,"), std::string::npos);
}

TEST(PlatformConfigBase, Sound) {
    ASSERT_TRUE(ParseHost(R"({"audio": {"sound": "surround"}})").surround);
    ASSERT_FALSE(ParseHost(R"({"audio": {"surround": true, "sound": "Stereo"}})").surround);
    ASSERT_FALSE(ParseHost(R"({"audio": {"sound": "Surround"}})").surround);
    ASSERT_TRUE(ParseHost(R"({"audio": {"surround": true}})").surround);
}
