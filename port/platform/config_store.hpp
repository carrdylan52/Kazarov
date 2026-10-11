#pragma once

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "config_base.hpp"
#include "paths.hpp"

// <save root>/config.json as a game's Config, which derives from ConfigBase. One store is in use at
// a time: the last one constructed.
template <class Config>
class ConfigStoreOf final : public ConfigStore {
public:
    using Parse = Config (*)(std::string_view text);
    using Serialize = std::string (*)(const Config &config);
    // Applies a change of settings to the running game; before is what the change replaced.
    using Hook = void (*)(const Config &before, const Config &after);
    using BaseHook = void (*)(const ConfigBase &before, const ConfigBase &after);

    ConfigStoreOf(Parse parse, Serialize serialize) : parse_(parse), serialize_(serialize) { ConfigUseStore(this); }

    ~ConfigStoreOf() override { ConfigUseStore(nullptr); }

    const Config &Get() const { return config_; }

    const ConfigBase &Current() const override { return config_; }

    // Writes the current settings, creating the save root if need be, and says on stderr where it
    // saved, or that it could not.
    bool Save() {
        std::filesystem::path path = PathsSaveRoot() / "config.json";
        std::error_code       error;
        std::filesystem::create_directories(path.parent_path(), error);
        saved_ = ConfigWriteReplacing(path, serialize_(config_));
        std::fprintf(stderr, "config: %s %s\n", saved_ ? "saved" : "could not save", PathsDisplay(path).c_str());
        return saved_;
    }

    // Reads config.json; with no file there, keeps the defaults and saves them as a new one.
    bool Load() override {
        std::filesystem::path path = PathsSaveRoot() / "config.json";
        std::ifstream         file(path, std::ios::binary);
        std::error_code       error;
        if (!file || !std::filesystem::is_regular_file(path, error)) {
            config_ = Config{};
            Save();
            return false;
        }
        std::ostringstream text;
        text << file.rdbuf();
        config_ = parse_(text.str());
        saved_ = true;
        std::fprintf(stderr, "config: loaded %s\n", PathsDisplay(path).c_str());
        return true;
    }

    // Adding a hook twice runs it once. Hooks added or removed while a change is being applied take
    // part from the next change.
    void AddHook(Hook hook) { Add(hook); }

    void AddHook(BaseHook hook) { Add(hook); }

    void RemoveHook(Hook hook) { std::erase(hooks_, Entry(hook)); }

    void RemoveHook(BaseHook hook) { std::erase(hooks_, Entry(hook)); }

    // The settings as config.json would read them back (a bad value is reported and becomes its
    // default) become the current ones, are saved, and reach the running game through the hooks in
    // the order they were added. Settings equal to the current ones change nothing, but are saved if
    // the last save failed. Returns whether the settings are saved. A call from a hook is refused.
    bool Change(const Config &config) {
        if (changing_) {
            std::fprintf(stderr, "config: ignoring a change made while one is being applied\n");
            return false;
        }

        struct Guard {
            bool &flag;

            explicit Guard(bool &f) : flag(f) { flag = true; }

            ~Guard() { flag = false; }
        } guard(changing_);

        Config after = parse_(serialize_(config));
        if (after == config_) {
            return saved_ || Save();
        }
        Config before = std::exchange(config_, std::move(after));
        Save();
        const std::vector<Entry> hooks = hooks_;
        for (const Entry &hook : hooks) {
            if (const Hook *game = std::get_if<Hook>(&hook)) {
                (*game)(before, config_);
            } else {
                std::get<BaseHook>(hook)(before, config_);
            }
        }
        return saved_;
    }

    // A ConfigBase that is not the game's Config is the host's part alone.
    bool Change(const ConfigBase &config) override {
        if (const Config *game = dynamic_cast<const Config *>(&config)) {
            return Change(*game);
        }
        return ChangeHost(config);
    }

    bool ChangeHost(const ConfigBase &host) override {
        Config config = config_;
        static_cast<ConfigBase &>(config) = host;
        return Change(config);
    }

private:
    using Entry = std::variant<Hook, BaseHook>;

    void Add(Entry hook) {
        if (std::ranges::find(hooks_, hook) == hooks_.end()) {
            hooks_.push_back(hook);
        }
    }

    Parse              parse_;
    Serialize          serialize_;
    Config             config_;
    std::vector<Entry> hooks_;
    // Whether config.json holds config_, so a failed save is retried with the same settings.
    bool saved_ = false;
    bool changing_ = false;
};
