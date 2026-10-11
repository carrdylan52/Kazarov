#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

enum class ConfigPresentMode {
    Fifo,
    Mailbox,
    Immediate,
};

// What the FPS counter shows: the frame rate alone, with the logic ticks, or all of it with the draws.
enum class ConfigFpsDetail {
    Fps,
    Ticks,
    All,
};

enum class ConfigAspect {
    Auto,
    FourThree,
};

enum class ConfigGyro {
    Off,
    Always,
    FirstPerson,
    Held,
};

// input.glyph_device: whose button symbols the game draws. Auto follows the device in use (the
// last of keyboard/mouse or gamepad to be touched; a gamepad's own family; Xbox for a pad of unknown make), PS4 until one is.
enum class ConfigGlyphDevice {
    Auto,
    Ps3,
    Ps4,
    Ps5,
    Ps5Color,
    Xbox,
    Switch,
    SteamDeck,
    SteamController,
    Keyboard,
};

struct ConfigKeyBinding {
    std::string              action;
    std::vector<std::string> keys;

    bool operator==(const ConfigKeyBinding &) const = default;
};

// The host's part of config.json. A game's Config derives from it and adds its own settings; the
// store holds the game's, so a ConfigBase reference to the current settings is always one.
struct ConfigBase {
    virtual ~ConfigBase() = default;

    double            tick_rate = 60.0;
    ConfigPresentMode present_mode = ConfigPresentMode::Fifo;
    bool              interpolation = true;
    double            max_fps = 0.0;
    // 0: the monitor's (WindowConfig).
    int                           window_width = 0;
    int                           window_height = 0;
    bool                          fullscreen = false;
    ConfigAspect                  aspect = ConfigAspect::Auto;
    float                         ui_scale = 1.0f;
    bool                          show_fps = true;
    ConfigFpsDetail               fps_detail = ConfigFpsDetail::All;
    int                           anisotropy = 0;
    float                         master_volume = 1.0f;
    bool                          surround = false;
    std::vector<ConfigKeyBinding> key_bindings;
    float                         mouse_sensitivity = 0.1f;
    float                         stick_sensitivity = 1.33f;
    bool                          stick_invert_x = false;
    bool                          stick_invert_y = false;
    ConfigGyro                    gyro = ConfigGyro::Held;
    float                         gyro_sensitivity = 0.5f;
    bool                          gyro_invert_x = false;
    bool                          gyro_invert_y = false;
    bool                          mouse_invert_y = false;
    bool                          mouse_capture = true;
    bool                          mouse_zoom = false;
    // A DualSense's touchpad as the menus' mouse, at this speed (1 is a swipe across the pad for 1500
    // window pixels), and its lightbar showing the active character's life.
    bool                          touchpad = true;
    float                         touchpad_sensitivity = 1.0f;
    bool                          lightbar = true;
    // Scales a gamepad's rumble motors, 0 to 1.
    float                         rumble_strength = 1.0f;
    // input.glyphs "new": the button symbols of glyphs/ (see glyphs.hpp); "original": the game's own.
    bool                          glyphs_new = true;
    ConfigGlyphDevice             glyph_device = ConfigGlyphDevice::Auto;
    std::vector<std::string>      mouse_release_keys;
    bool                          discord_rich_presence = true;

    bool operator==(const ConfigBase &) const = default;
};

using ConfigJson = nlohmann::ordered_json;

// Calls apply with every "section.key" of a config.json text (comments allowed) and reports on stderr
// what it refuses, a section that is not an object, and a text that is not a JSON object, which then
// applies nothing. An empty text applies nothing.
void ConfigParseKeys(std::string_view text, const std::function<bool(std::string_view name, const ConfigJson &value)> &apply);

// Applies one key to the host's settings; false for a key that is not the host's or a bad value.
bool ConfigBaseApply(ConfigBase &config, std::string_view name, const ConfigJson &value);

// Writes the host's settings into the sections game, video, audio, input and discord of root, in
// that order, so a game's keys follow the host's in each.
void ConfigBaseSerialize(const ConfigBase &config, ConfigJson &root);

// Readers for a game's own keys, with ConfigBaseApply's rules: false leaves out untouched.
bool        ConfigReadBool(const ConfigJson &value, bool &out);
bool        ConfigReadFloat(const ConfigJson &value, float &out);
bool        ConfigReadDouble(const ConfigJson &value, double &out);
bool        ConfigReadList(const ConfigJson &value, std::vector<std::string> &out);
// value is one of the two names, in any case; out is whether it is second.
bool        ConfigReadChoice(const ConfigJson &value, std::string_view first, std::string_view second, bool &out);
std::string ConfigLower(std::string_view text);
// The double that prints as the float does: 0.1f as 0.1, not 0.10000000149011612.
double      ConfigShortest(float value);

// Writes text to path.tmp and renames it over path, so a crash leaves the old file or the new one.
bool ConfigWriteReplacing(const std::filesystem::path &path, std::string_view text);

// The running settings, as the host side reads and changes them. The game's store (config_store.hpp)
// makes itself the one in use.
class ConfigStore {
public:
    virtual ~ConfigStore() = default;
    virtual const ConfigBase &Current() const = 0;
    // config is a copy of Current(), changed: the game's own type.
    virtual bool Change(const ConfigBase &config) = 0;
    // The current settings with the host's part replaced by host's.
    virtual bool ChangeHost(const ConfigBase &host) = 0;
    virtual bool Load() = 0;
};

void ConfigUseStore(ConfigStore *store);

const ConfigBase &ConfigBaseGet();
bool              ConfigBaseChange(const ConfigBase &config);
bool              ConfigBaseChangeHost(const ConfigBase &host);
bool              ConfigBaseLoad();
