#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "platform/input.hpp"
#include "platform/mouse.hpp"
#include "platform/settings.hpp"

namespace {
class InputHostMouse : public testing::Test {
protected:
    void SetUp() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputSetOverride(1, nullptr);
        HostSettings settings;
        settings.mouse_capture = false;
        settings.mouse_zoom = true;
        InputApplyConfig(settings);
#ifdef KZ_TOUCH_OVERLAY
        InputSetTouchOverlay({});
#endif
    }

    void TearDown() override {
        if (joystick) SDL_CloseJoystick(joystick);
        if (id) SDL_DetachVirtualJoystick(id);
        joystick = nullptr;
        id = 0;
        InputShutdown();
        InputApplyConfig(HostSettings{});
#ifdef KZ_TOUCH_OVERLAY
        InputSetTouchOverlay({});
#endif
    }

    void Mouse(Uint8 button, bool down) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = button;
        event.button.down = down;
        InputHandleEvent(event);
    }

    void Attach() {
        InputInit();
        MouseConfigure(false, {});
        SDL_VirtualJoystickDesc description{};
        SDL_INIT_INTERFACE(&description);
        description.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        description.naxes = SDL_GAMEPAD_AXIS_COUNT;
        description.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        description.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
        description.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        description.name = "Host mouse edge test";
        id = SDL_AttachVirtualJoystick(&description);
        ASSERT_NE(id, 0u) << SDL_GetError();
        joystick = SDL_OpenJoystick(id);
        ASSERT_NE(joystick, nullptr) << SDL_GetError();
        InputPoll();
    }

    SDL_JoystickID id = 0;
    SDL_Joystick *joystick = nullptr;
};
} // namespace

TEST_F(InputHostMouse, UnrelatedMouseDownDoesNotConsumePendingControllerPress) {
    Attach();
    ASSERT_NE(joystick, nullptr);
    const std::string_view sources[] = {"paddle3"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", sources));
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GetGamepadButtonFromString("paddle3"), true));
    SDL_UpdateGamepads();
    // SDL pumps controller state before dispatching mouse events, then the host polls.
    Mouse(SDL_BUTTON_LEFT, true);
    InputPoll();
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputHostMouse, UnrelatedMouseUpDoesNotConsumePendingControllerPress) {
    Attach();
    ASSERT_NE(joystick, nullptr);
    const std::string_view sources[] = {"paddle3"};
    ASSERT_TRUE(InputBindPadButtons("mode_debug_menu", sources));
    Mouse(SDL_BUTTON_LEFT, true);
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joystick, SDL_GetGamepadButtonFromString("paddle3"), true));
    SDL_UpdateGamepads();
    Mouse(SDL_BUTTON_LEFT, false);
    InputPoll();
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputHostMouse, LiveMousePressesSurvivePollsAndMultipleCompleteTaps) {
    const std::string_view sources[] = {"Mouse4"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    Mouse(SDL_BUTTON_X1, true);
    InputPoll();
    EXPECT_TRUE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    InputPoll();
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    Mouse(SDL_BUTTON_X1, false);
    InputPoll();
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    for (int tap = 0; tap < 3; ++tap) {
        Mouse(SDL_BUTTON_X1, true);
        Mouse(SDL_BUTTON_X1, false);
    }
    InputPoll();
    for (int tap = 0; tap < 3; ++tap) EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}

TEST_F(InputHostMouse, OrdinaryHostActionsKeepSharedMouseBindingsAndZoomGates) {
    const std::string_view sources[] = {"Mouse3"};
    ASSERT_TRUE(InputBindKeys("debug_menu", sources));
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    Mouse(SDL_BUTTON_MIDDLE, true);
    Mouse(SDL_BUTTON_MIDDLE, false);
    InputPoll();
    EXPECT_TRUE(InputHostPressed(InputHostAction::DebugMenu));
    EXPECT_TRUE(InputHostPressed(InputHostAction::ZoomReset));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::DebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ZoomReset));
    InputSetMenuMouse(true);
    Mouse(SDL_BUTTON_MIDDLE, true);
    Mouse(SDL_BUTTON_MIDDLE, false);
    InputPoll();
    EXPECT_TRUE(InputHostPressed(InputHostAction::DebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ZoomReset));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    InputSetMenuMouse(false);
    InputPoll();
    EXPECT_FALSE(InputHostPressed(InputHostAction::ZoomReset));
}

TEST_F(InputHostMouse, SwallowedCaptureClickDoesNotQueueHostPress) {
    const std::string_view sources[] = {"Mouse4"};
    ASSERT_TRUE(InputBindKeys("mode_debug_menu", sources));
    MouseConfigure(true, {});
    Mouse(SDL_BUTTON_X1, true);
    Mouse(SDL_BUTTON_X1, false);
    InputPoll();
    EXPECT_FALSE(InputHostHeld(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
    Mouse(SDL_BUTTON_X1, true);
    Mouse(SDL_BUTTON_X1, false);
    EXPECT_TRUE(InputHostPressed(InputHostAction::ModeDebugMenu));
    EXPECT_FALSE(InputHostPressed(InputHostAction::ModeDebugMenu));
}
