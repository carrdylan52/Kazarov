#include "config_base.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace {

ConfigStore *g_store = nullptr;

std::string_view Trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r\n";
    std::size_t                first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kSpace) - first + 1);
}

void Ignore(std::string_view name, const ConfigJson &value) {
    std::fprintf(stderr, "config.json: ignoring %.*s = %s\n", static_cast<int>(name.size()), name.data(),
                 value.dump().c_str());
}

template <class T>
bool ReadNumber(const ConfigJson &value, T &out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<T>();
    return true;
}

bool ReadPresentMode(const ConfigJson &value, ConfigPresentMode &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string mode = ConfigLower(value.get<std::string>());
    if (mode == "fifo") {
        out = ConfigPresentMode::Fifo;
    } else if (mode == "mailbox") {
        out = ConfigPresentMode::Mailbox;
    } else if (mode == "immediate") {
        out = ConfigPresentMode::Immediate;
    } else {
        return false;
    }
    return true;
}

const char *PresentModeName(ConfigPresentMode mode) {
    switch (mode) {
        case ConfigPresentMode::Mailbox:
            return "mailbox";
        case ConfigPresentMode::Immediate:
            return "immediate";
        default:
            return "fifo";
    }
}

bool ReadFpsDetail(const ConfigJson &value, ConfigFpsDetail &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string detail = ConfigLower(value.get<std::string>());
    if (detail == "fps") {
        out = ConfigFpsDetail::Fps;
    } else if (detail == "ticks") {
        out = ConfigFpsDetail::Ticks;
    } else if (detail == "all") {
        out = ConfigFpsDetail::All;
    } else {
        return false;
    }
    return true;
}

const char *FpsDetailName(ConfigFpsDetail detail) {
    switch (detail) {
        case ConfigFpsDetail::Fps:
            return "fps";
        case ConfigFpsDetail::Ticks:
            return "ticks";
        default:
            return "all";
    }
}

bool ReadAspect(const ConfigJson &value, ConfigAspect &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string aspect = ConfigLower(value.get<std::string>());
    if (aspect == "auto") {
        out = ConfigAspect::Auto;
    } else if (aspect == "4:3") {
        out = ConfigAspect::FourThree;
    } else {
        return false;
    }
    return true;
}

constexpr const char *kGlyphDeviceNames[] = {"auto", "ps3", "ps4", "ps5", "ps5color", "xbox", "switch", "steamdeck", "steamcontroller", "keyboard"};

bool ReadGlyphDevice(const ConfigJson &value, ConfigGlyphDevice &out) {
    if (!value.is_string()) {
        return false;
    }
    const std::string &name = value.get_ref<const std::string &>();
    for (std::size_t i = 0; i < std::size(kGlyphDeviceNames); ++i) {
        if (name == kGlyphDeviceNames[i]) {
            out = static_cast<ConfigGlyphDevice>(i);
            return true;
        }
    }
    return false;
}

constexpr const char *kGyroNames[] = {"off", "always", "first_person", "held"};

bool ReadGyro(const ConfigJson &value, ConfigGyro &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string gyro = ConfigLower(value.get<std::string>());
    for (std::size_t i = 0; i < std::size(kGyroNames); ++i) {
        if (gyro == kGyroNames[i]) {
            out = static_cast<ConfigGyro>(i);
            return true;
        }
    }
    return false;
}

bool ApplyBindings(ConfigBase &config, const ConfigJson &bindings) {
    if (!bindings.is_object()) {
        return false;
    }
    for (const auto &[action, keys] : bindings.items()) {
        ConfigKeyBinding binding = {ConfigLower(action), {}};
        if (ConfigReadList(keys, binding.keys)) {
            config.key_bindings.push_back(std::move(binding));
        } else {
            Ignore("input.bindings." + action, keys);
        }
    }
    return true;
}

bool ReadSensitivity(const ConfigJson &value, float &out) {
    float sensitivity = 0.0f;
    if (!ReadNumber(value, sensitivity) || !(sensitivity > 0.0f) || !std::isfinite(sensitivity)) {
        return false;
    }
    out = sensitivity;
    return true;
}

} // namespace

std::string ConfigLower(std::string_view text) {
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

double ConfigShortest(float value) {
    return std::stod(std::format("{}", value));
}

bool ConfigReadBool(const ConfigJson &value, bool &out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

bool ConfigReadFloat(const ConfigJson &value, float &out) {
    return ReadNumber(value, out);
}

bool ConfigReadDouble(const ConfigJson &value, double &out) {
    return ReadNumber(value, out);
}

bool ConfigReadList(const ConfigJson &value, std::vector<std::string> &out) {
    std::vector<std::string> items;
    if (value.is_string()) {
        items.push_back(value.get<std::string>());
    } else if (value.is_array()) {
        for (const ConfigJson &item : value) {
            if (!item.is_string()) {
                return false;
            }
            items.push_back(item.get<std::string>());
        }
    } else {
        return false;
    }
    out = std::move(items);
    return true;
}

bool ConfigReadChoice(const ConfigJson &value, std::string_view first, std::string_view second, bool &out) {
    if (!value.is_string()) {
        return false;
    }
    std::string choice = ConfigLower(value.get<std::string>());
    if (choice != first && choice != second) {
        return false;
    }
    out = choice == second;
    return true;
}

void ConfigParseKeys(std::string_view text,
                     const std::function<bool(std::string_view name, const ConfigJson &value)> &apply) {
    if (Trim(text).empty()) {
        return;
    }
    ConfigJson root = ConfigJson::parse(text, nullptr, false, true);
    if (!root.is_object()) {
        std::fprintf(stderr, "config.json: %s; using the defaults\n",
                     root.is_discarded() ? "not valid JSON" : "expected an object");
        return;
    }
    for (const auto &[section, keys] : root.items()) {
        if (!keys.is_object()) {
            Ignore(section, keys);
            continue;
        }
        for (const auto &[key, value] : keys.items()) {
            std::string name = section + "." + key;
            if (!apply(name, value)) {
                Ignore(name, value);
            }
        }
    }
}

bool ConfigBaseApply(ConfigBase &config, std::string_view name, const ConfigJson &value) {
    if (name == "input.mouse_sensitivity") {
        return ReadSensitivity(value, config.mouse_sensitivity);
    }
    if (name == "input.stick_sensitivity") {
        return ReadSensitivity(value, config.stick_sensitivity);
    }
    if (name == "input.gyro") {
        return ReadGyro(value, config.gyro);
    }
    if (name == "input.stick_invert_x") {
        return ConfigReadBool(value, config.stick_invert_x);
    }
    if (name == "input.stick_invert_y") {
        return ConfigReadBool(value, config.stick_invert_y);
    }
    if (name == "input.gyro_invert_x") {
        return ConfigReadBool(value, config.gyro_invert_x);
    }
    if (name == "input.gyro_invert_y") {
        return ConfigReadBool(value, config.gyro_invert_y);
    }
    if (name == "input.gyro_sensitivity") {
        return ReadSensitivity(value, config.gyro_sensitivity);
    }
    if (name == "input.mouse_invert_y") {
        return ConfigReadBool(value, config.mouse_invert_y);
    }
    if (name == "input.mouse_capture") {
        return ConfigReadBool(value, config.mouse_capture);
    }
    if (name == "input.touchpad") {
        return ConfigReadBool(value, config.touchpad);
    }
    if (name == "input.lightbar") {
        return ConfigReadBool(value, config.lightbar);
    }
    if (name == "input.touchpad_sensitivity") {
        return ReadSensitivity(value, config.touchpad_sensitivity);
    }
    if (name == "input.rumble_strength") {
        float strength = 0.0f;
        if (!ReadNumber(value, strength) || !std::isfinite(strength) || strength < 0.0f || strength > 1.0f) {
            return false;
        }
        config.rumble_strength = strength;
        return true;
    }
    if (name == "input.glyphs") {
        if (!value.is_string() || (value.get<std::string>() != "new" && value.get<std::string>() != "original")) {
            return false;
        }
        config.glyphs_new = value.get<std::string>() == "new";
        return true;
    }
    if (name == "input.glyph_device") {
        return ReadGlyphDevice(value, config.glyph_device);
    }
    if (name == "input.mouse_zoom") {
        return ConfigReadBool(value, config.mouse_zoom);
    }
    if (name == "input.mouse_release") {
        return ConfigReadList(value, config.mouse_release_keys);
    }
    if (name == "input.bindings") {
        return ApplyBindings(config, value);
    }
    if (name == "game.tick_rate") {
        double rate = 0.0;
        if (!ReadNumber(value, rate) || !(rate > 0.0) || !std::isfinite(rate)) {
            return false;
        }
        config.tick_rate = rate;
        return true;
    }
    if (name == "video.present_mode") {
        return ReadPresentMode(value, config.present_mode);
    }
    if (name == "video.interpolation") {
        return ConfigReadBool(value, config.interpolation);
    }
    if (name == "video.max_fps") {
        double fps = 0.0;
        if (!ReadNumber(value, fps) || !(fps >= 0.0) || !std::isfinite(fps)) {
            return false;
        }
        config.max_fps = fps;
        return true;
    }
    if (name == "video.vsync") {
        bool vsync = true;
        if (!ConfigReadBool(value, vsync)) {
            return false;
        }
        config.present_mode = vsync ? ConfigPresentMode::Fifo : ConfigPresentMode::Immediate;
        return true;
    }
    if (name == "video.width" || name == "video.height") {
        if (!value.is_number_integer() || value.get<std::int64_t>() < 0 ||
            value.get<std::int64_t>() > std::numeric_limits<int>::max()) {
            return false;
        }
        (name == "video.width" ? config.window_width : config.window_height) = value.get<int>();
        return true;
    }
    if (name == "video.fullscreen") {
        return ConfigReadBool(value, config.fullscreen);
    }
    if (name == "video.aspect") {
        return ReadAspect(value, config.aspect);
    }
    if (name == "video.ui_scale") {
        float scale = 0.0f;
        if (!ReadNumber(value, scale) || !(scale >= 0.25f && scale <= 4.0f)) {
            return false;
        }
        config.ui_scale = scale;
        return true;
    }
    if (name == "video.show_fps") {
        return ConfigReadBool(value, config.show_fps);
    }
    if (name == "video.fps_detail") {
        return ReadFpsDetail(value, config.fps_detail);
    }
    if (name == "video.anisotropy") {
        if (!value.is_number_integer()) {
            return false;
        }
        int samples = value.get<int>();
        if (samples != 0 && samples != 2 && samples != 4 && samples != 8 && samples != 16) {
            return false;
        }
        config.anisotropy = samples;
        return true;
    }
    if (name == "discord.rich_presence") {
        return ConfigReadBool(value, config.discord_rich_presence);
    }
    if (name == "audio.sound") {
        if (value == "surround") {
            config.surround = true;
            return true;
        }
        if (!value.is_string() || ConfigLower(value.get<std::string>()) != "stereo") {
            return false;
        }
        config.surround = false;
        return true;
    }
    // Written before audio.sound took "surround".
    if (name == "audio.surround") {
        return ConfigReadBool(value, config.surround);
    }
    if (name == "audio.master_volume") {
        float volume = 0.0f;
        if (!ReadNumber(value, volume) || !std::isfinite(volume)) {
            return false;
        }
        config.master_volume = std::clamp(volume, 0.0f, 1.0f);
        return true;
    }
    return false;
}

void ConfigBaseSerialize(const ConfigBase &config, ConfigJson &root) {
    ConfigJson bindings = ConfigJson::object();
    for (const ConfigKeyBinding &binding : config.key_bindings) {
        bindings[binding.action] = binding.keys;
    }
    root["game"]["tick_rate"] = config.tick_rate;
    root["video"]["present_mode"] = PresentModeName(config.present_mode);
    root["video"]["interpolation"] = config.interpolation;
    root["video"]["max_fps"] = config.max_fps;
    root["video"]["width"] = config.window_width;
    root["video"]["height"] = config.window_height;
    root["video"]["fullscreen"] = config.fullscreen;
    root["video"]["aspect"] = config.aspect == ConfigAspect::FourThree ? "4:3" : "auto";
    root["video"]["ui_scale"] = ConfigShortest(config.ui_scale);
    root["video"]["show_fps"] = config.show_fps;
    root["video"]["fps_detail"] = FpsDetailName(config.fps_detail);
    root["video"]["anisotropy"] = config.anisotropy;
    root["audio"]["master_volume"] = ConfigShortest(config.master_volume);
    root["audio"]["sound"] = config.surround ? "surround" : "stereo";
    root["input"]["mouse_sensitivity"] = ConfigShortest(config.mouse_sensitivity);
    root["input"]["stick_sensitivity"] = ConfigShortest(config.stick_sensitivity);
    root["input"]["stick_invert_x"] = config.stick_invert_x;
    root["input"]["stick_invert_y"] = config.stick_invert_y;
    root["input"]["gyro"] = kGyroNames[static_cast<std::size_t>(config.gyro)];
    root["input"]["gyro_sensitivity"] = ConfigShortest(config.gyro_sensitivity);
    root["input"]["gyro_invert_x"] = config.gyro_invert_x;
    root["input"]["gyro_invert_y"] = config.gyro_invert_y;
    root["input"]["mouse_invert_y"] = config.mouse_invert_y;
    root["input"]["mouse_capture"] = config.mouse_capture;
    root["input"]["mouse_zoom"] = config.mouse_zoom;
    root["input"]["touchpad"] = config.touchpad;
    root["input"]["touchpad_sensitivity"] = ConfigShortest(config.touchpad_sensitivity);
    root["input"]["lightbar"] = config.lightbar;
    root["input"]["rumble_strength"] = ConfigShortest(config.rumble_strength);
    root["input"]["glyphs"] = config.glyphs_new ? "new" : "original";
    root["input"]["glyph_device"] = kGlyphDeviceNames[static_cast<std::size_t>(config.glyph_device)];
    root["input"]["mouse_release"] = config.mouse_release_keys;
    root["input"]["bindings"] = std::move(bindings);
    root["discord"]["rich_presence"] = config.discord_rich_presence;
}

bool ConfigWriteReplacing(const std::filesystem::path &path, std::string_view text) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    std::error_code error;
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << text;
        file.close();
        if (!file) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

void ConfigUseStore(ConfigStore *store) {
    g_store = store;
}

const ConfigBase &ConfigBaseGet() {
    return g_store->Current();
}

bool ConfigBaseChange(const ConfigBase &config) {
    return g_store->Change(config);
}

bool ConfigBaseChangeHost(const ConfigBase &host) {
    return g_store->ChangeHost(host);
}

bool ConfigBaseLoad() {
    return g_store->Load();
}
