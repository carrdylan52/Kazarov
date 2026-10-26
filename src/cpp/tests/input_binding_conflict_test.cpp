#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "platform/input.hpp"
#include "platform/mouse.hpp"
#include "platform/settings.hpp"

namespace {
class VirtualPad {
public:
    ~VirtualPad() { Detach(); }

    bool Attach(const char *name, SDL_GamepadType type) {
        SDL_VirtualJoystickDesc description{};
        SDL_INIT_INTERFACE(&description);
        description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        description.naxes = SDL_GAMEPAD_AXIS_COUNT;
        description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        description.name = name;
        id_ = SDL_AttachVirtualJoystick(&description);
        if (id_ == 0) return false;
        joystick_ = SDL_OpenJoystick(id_);
        return joystick_ != nullptr && SetType(type);
    }

    bool SetType(SDL_GamepadType type) {
        char *raw = SDL_GetGamepadMappingForID(id_);
        if (raw == nullptr) return false;
        std::string mapping(raw);
        SDL_free(raw);
        const auto start = mapping.find("type:");
        if (start != std::string::npos) {
            const auto end = mapping.find(',', start);
            mapping.erase(start, end == std::string::npos ? end : end - start + 1);
        }
        mapping += std::string("type:") + SDL_GetGamepadStringForType(type) + ",";
        return SDL_SetGamepadMapping(id_, mapping.c_str()) && SDL_GetGamepadTypeForID(id_) == type;
    }

    bool Button(SDL_GamepadButton button, bool down) {
        return SDL_SetJoystickVirtualButton(joystick_, button, down);
    }

    void Detach() {
        if (joystick_ != nullptr) SDL_CloseJoystick(joystick_);
        if (id_ != 0) SDL_DetachVirtualJoystick(id_);
        joystick_ = nullptr;
        id_ = 0;
    }

private:
    SDL_JoystickID id_ = 0;
    SDL_Joystick *joystick_ = nullptr;
};

class InputBindingConflict : public testing::Test {
protected:
    void SetUp() override {
        Reset();
        InputInit();
        ASSERT_NE(SDL_WasInit(SDL_INIT_GAMEPAD), 0u) << SDL_GetError();
        HostSettings config;
        config.mouse_capture = false;
        InputApplyConfig(config);
    }

    void TearDown() override { Reset(); }

    void Reset() {
        InputShutdown();
        for (int pad = 0; pad < kInputPadCount; ++pad) InputSetOverride(pad, nullptr);
        InputResetBindings();
        MouseConfigure(false, {});
#ifdef KZ_TOUCH_OVERLAY
        InputSetTouchOverlay({});
#endif
    }

    static bool Contains(const std::vector<std::string> &names, std::string_view name) {
        return std::ranges::find(names, name) != names.end();
    }
};
} // namespace

TEST_F(InputBindingConflict, LegacyAndSeparateControllerBindingsUseTheSameSourceIdentity) {
    const std::string_view legacy[] = {"Gamepad:paddle3"};
    const std::string_view separate[] = {"paddle3"};
    ASSERT_TRUE(InputBindKeys("debug_menu", legacy));
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", separate));
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"Debug menu"});
    EXPECT_TRUE(Contains(InputBindingConflicts("debug_menu"), "Debug tools"));
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "Gamepad:lefttrigger"),
              std::vector<std::string>{"L2"});
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "lefttrigger", true),
              std::vector<std::string>{"L2"});
    ASSERT_TRUE(InputBindKeys("debug_menu", {}));
    EXPECT_TRUE(InputBindingConflicts("mode_debug_menu").empty());
    EXPECT_TRUE(InputBindingSourceConflicts("unknown", "paddle3", true).empty());
    EXPECT_TRUE(InputBindingSourceConflicts("mode_debug_menu", "paddle5", true).empty());
}

TEST_F(InputBindingConflict, ApplyingStoredBindingsPreservesSourcesAndRecomputesConflicts) {
    HostSettings stored;
    stored.mouse_capture = false;
    stored.key_bindings = {{"mode_debug_menu", {"F2", "B"}}, {"debug_menu", {"Gamepad:paddle3"}}};
    stored.pad_bindings = {{"mode_debug_menu", {"paddle3"}}};
    const HostSettings original = stored;
    for (int reload = 0; reload < 2; ++reload) {
        InputApplyConfig(stored);
        EXPECT_EQ(InputBindingNames("mode_debug_menu"), (std::vector<std::string>{"F2", "B"}));
        EXPECT_EQ(InputPadBindingNames("mode_debug_menu"), std::vector<std::string>{"paddle3"});
        EXPECT_EQ(InputBindingNames("r3"), (std::vector<std::string>{"Mouse3", "B"}));
        EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), (std::vector<std::string>{"R3", "Debug menu"}));
        EXPECT_EQ(stored, original);
    }
    stored.key_bindings[1].keys.clear();
    InputApplyConfig(stored);
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"R3"});
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_F2}});
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputBindingConflict, ConflictListsIncludeTheSecondControllersDefaultSelectSource) {
    VirtualPad primary;
    ASSERT_TRUE(primary.Attach("Binding conflict PS4", SDL_GAMEPAD_TYPE_PS4)) << SDL_GetError();
    InputPoll();
    VirtualPad secondary;
    ASSERT_TRUE(secondary.Attach("Binding conflict standard", SDL_GAMEPAD_TYPE_STANDARD)) << SDL_GetError();
    InputPoll();
    const std::string_view back[] = {"back"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", back));
    EXPECT_EQ(InputPadBindingNames("select"), std::vector<std::string>{"touchpad"});
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"Select"});
    EXPECT_TRUE(Contains(InputBindingConflicts("select"), "Debug tools"));
    ASSERT_TRUE(secondary.Button(SDL_GAMEPAD_BUTTON_BACK, true));
    InputPoll();
    EXPECT_NE(InputGetPad(1).buttons & kInputSelect, 0);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    secondary.Detach();
    InputPoll();
    EXPECT_TRUE(InputBindingConflicts("mode_debug_menu").empty());
    EXPECT_FALSE(Contains(InputBindingConflicts("select"), "Debug tools"));
}

TEST_F(InputBindingConflict, ControllerTypeChangesRecomputeConflictsWithoutChangingBindings) {
    VirtualPad pad;
    ASSERT_TRUE(pad.Attach("Binding conflict changing type", SDL_GAMEPAD_TYPE_PS4)) << SDL_GetError();
    InputPoll();
    const std::string_view back[] = {"back"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", back));
    EXPECT_TRUE(InputBindingConflicts("mode_debug_menu").empty());
    ASSERT_TRUE(pad.SetType(SDL_GAMEPAD_TYPE_STANDARD)) << SDL_GetError();
    InputPoll();
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"Select"});
    EXPECT_TRUE(Contains(InputBindingConflicts("select"), "Debug tools"));
    ASSERT_TRUE(pad.SetType(SDL_GAMEPAD_TYPE_PS4)) << SDL_GetError();
    InputPoll();
    EXPECT_TRUE(InputBindingConflicts("mode_debug_menu").empty());
    EXPECT_EQ(InputPadBindingNames("mode_debug_menu"), std::vector<std::string>{"back"});
}

#ifdef KZ_TOUCH_OVERLAY
TEST_F(InputBindingConflict, OverlaySelectKeepsItsConflictAfterPlayStationDisconnects) {
    VirtualPad pad;
    ASSERT_TRUE(pad.Attach("Binding conflict overlay PS4", SDL_GAMEPAD_TYPE_PS4)) << SDL_GetError();
    InputPoll();
    pad.Detach();
    InputPoll();
    ASSERT_TRUE(InputTouchOverlayVisible());
    const std::string_view back[] = {"back"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", back));
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "back", true), std::vector<std::string>{"Select"});
    EXPECT_TRUE(Contains(InputBindingConflicts("select"), "Debug tools"));
    InputSetTouchOverlay({.buttons = kInputSelect});
    InputPoll();
    EXPECT_NE(InputGetPad(0).buttons & kInputSelect, 0);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputBindingConflict, OverlayLogicalR3RemainsReservedAfterPhysicalR3IsRebound) {
    InputPoll();
    ASSERT_TRUE(InputTouchOverlayVisible());
    const std::string_view paddle[] = {"paddle3"};
    const std::string_view rightstick[] = {"rightstick"};
    ASSERT_TRUE(InputBindPadButtons("r3", paddle));
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", rightstick));
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "rightstick", true), std::vector<std::string>{"R3"});
    EXPECT_TRUE(Contains(InputBindingConflicts("r3"), "Debug tools"));
    InputSetTouchOverlay({.buttons = kInputR3});
    InputPoll();
    EXPECT_NE(InputGetPad(0).buttons & kInputR3, 0);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}
#endif
