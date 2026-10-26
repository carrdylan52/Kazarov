#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "platform/input.hpp"
#include "platform/mouse.hpp"

namespace {
class InputDebugBinding : public testing::Test {
protected:
    void SetUp() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputResetBindings();
        MouseConfigure(false, {});
    }

    void TearDown() override { SetUp(); }

    void Key(SDL_Scancode scancode, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = scancode;
        event.key.down = down;
        InputHandleEvent(event);
    }
};
} // namespace

TEST_F(InputDebugBinding, BindingNamesPreserveExistingLabelsAndEmptyStates) {
    EXPECT_EQ(InputBindingNames("r3"), (std::vector<std::string>{"Mouse3", "B"}));
    EXPECT_EQ(InputBindingLabel("r3"), "Mouse3, B");
    EXPECT_TRUE(InputBindingNames("unknown").empty());
    EXPECT_EQ(InputBindingLabel("unknown"), "");
    EXPECT_TRUE(InputBindingNames("developer_menu").empty());
    EXPECT_EQ(InputBindingLabel("developer_menu"), "None");
}

TEST_F(InputDebugBinding, ConflictedR3KeyNeverOpensDebugButIndependentF2StillDoes) {
    const std::string_view sources[] = {"B", "F2"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"R3"});
    InputKeyboardMouse held{.keys = {SDL_SCANCODE_B}};
    InputSetScriptedDevices(held);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_NE(InputApplyKeyboardMouse({}, held).buttons & kInputR3, 0);
    InputSetScriptedDevices({});

    // A complete physical tap between logic polls uses KeyBound's filter too.
    Key(SDL_SCANCODE_B, true);
    Key(SDL_SCANCODE_B, false);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    Key(SDL_SCANCODE_F2, true);
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    Key(SDL_SCANCODE_F2, false);
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputDebugBinding, RightAndMiddleMouseRetainTheirGameplayActions) {
    // Binding names number right Mouse2 and middle Mouse3, independently of SDL's numbering.
    for (int button : {2, 3}) {
        const std::string source = "Mouse" + std::to_string(button);
        const std::string_view sources[] = {source};
        ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
        InputKeyboardMouse held{.keys = {}, .mouse_buttons = 1u << (button - 1)};
        InputSetScriptedDevices(held);
        EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
        EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
        EXPECT_NE(InputApplyKeyboardMouse({}, held).buttons & (button == 2 ? kInputCircle : kInputR3), 0);
        InputSetScriptedDevices({});
    }
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "Mouse2"), std::vector<std::string>{"Circle"});
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "Mouse3"),
              (std::vector<std::string>{"R3", "Zoom reset"}));
}

TEST_F(InputDebugBinding, MovementAndOtherHostBindingsKeepPriorityWithoutBeingUnmapped) {
    const std::string_view sources[] = {"W", "F3"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    InputKeyboardMouse held{.keys = {SDL_SCANCODE_W, SDL_SCANCODE_F3}};
    InputSetScriptedDevices(held);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_EQ(InputApplyKeyboardMouse({}, held).left_y, InputStickByte(-1.0f));
    EXPECT_TRUE(InputHostHeld(InputHostAction::FpsToggle));
    EXPECT_TRUE(InputHostPressed(InputHostAction::FpsToggle));
    EXPECT_EQ(InputBindingLabel("ly-"), "W");
    EXPECT_EQ(InputBindingLabel("fps_toggle"), "F3");

    // This rule is specific to ModeDebugMenu: existing shared host actions remain shared.
    InputSetScriptedDevices({});
    const std::string_view shared[] = {"F3"};
    ASSERT_TRUE(InputBindKeys("debug_menu", shared));
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_F3}});
    EXPECT_TRUE(InputHostHeld(InputHostAction::DebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::DebugMenu));
    EXPECT_TRUE(InputHostHeld(InputHostAction::FpsToggle));
}

TEST_F(InputDebugBinding, AGameplayRebindAlsoDisablesOnlyTheConflictingDebugSource) {
    const std::string_view sources[] = {"F2"};
    ASSERT_TRUE(InputBindKeys("square", sources));
    InputKeyboardMouse held{.keys = {SDL_SCANCODE_F2}};
    InputSetScriptedDevices(held);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_NE(InputApplyKeyboardMouse({}, held).buttons & kInputSquare, 0);
    EXPECT_EQ(InputBindingConflicts("mode_debug_menu"), std::vector<std::string>{"Square"});
}

TEST_F(InputDebugBinding, ControllerCaptureCanExplainR3AndHostConflictsBeforeChangingAnything) {
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "rightstick", true), std::vector<std::string>{"R3"});
    EXPECT_EQ(InputBindingSourceConflicts("mode_debug_menu", "paddle1", true), std::vector<std::string>{"Debug menu"});
    EXPECT_TRUE(InputBindingSourceConflicts("mode_debug_menu", "paddle3", true).empty());
    EXPECT_EQ(InputPadBindingNames("r3"), std::vector<std::string>{"rightstick"});
    const std::string_view unsupported[] = {"paddle5"};
    EXPECT_FALSE(InputBindPadButtons("mode_debug_menu", unsupported));
    EXPECT_TRUE(InputPadBindingNames("mode_debug_menu").empty());
}

TEST_F(InputDebugBinding, ControllerR3RemainsGameplayWhileAnUnusedPaddleOpensDebug) {
    InputInit();
    ASSERT_NE(SDL_WasInit(SDL_INIT_GAMEPAD), 0u);
    SDL_VirtualJoystickDesc description{};
    SDL_INIT_INTERFACE(&description);
    description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    description.naxes = SDL_GAMEPAD_AXIS_COUNT;
    description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
    description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    description.name = "Debug binding test";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&description);
    ASSERT_NE(id, 0u) << SDL_GetError();
    SDL_Joystick *joystick = SDL_OpenJoystick(id);
    ASSERT_NE(joystick, nullptr) << SDL_GetError();
    const std::string_view r3[] = {"rightstick"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", r3));
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, true));
    InputPoll();
    EXPECT_NE(InputGetPad(0).buttons & kInputR3, 0);
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));

    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_RIGHT_STICK, false));
    InputPoll();
    const std::string_view paddle[] = {"paddle3"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", paddle));
    const SDL_GamepadButton button = SDL_GetGamepadButtonFromString("paddle3");
    ASSERT_NE(button, SDL_GAMEPAD_BUTTON_INVALID);
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, button, true));
    InputPoll();
    EXPECT_EQ(InputGetPad(0).buttons & kInputR3, 0);
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    SDL_CloseJoystick(joystick);
    SDL_DetachVirtualJoystick(id);
}

TEST_F(InputDebugBinding, CompleteMouseTapQueuesOnlyTheDistinctAllowedHostAction) {
    const std::string_view sources[] = {"Mouse4"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_X1;
    down.button.down = true;
    SDL_Event up = down;
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.down = false;
    InputHandleEvent(down);
    InputHandleEvent(up);
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));

    const std::string_view conflicting[] = {"Mouse3"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", conflicting));
    down.button.button = up.button.button = SDL_BUTTON_MIDDLE;
    InputHandleEvent(down);
    InputHandleEvent(up);
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}
