#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include "platform/input.hpp"
#include "platform/mouse.hpp"

namespace {

void Button(Uint32 type, Uint8 button) {
    SDL_Event event{};
    event.type = type;
    event.button.button = button;
    event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    InputHandleEvent(event);
}

void Click(Uint8 button) {
    Button(SDL_EVENT_MOUSE_BUTTON_DOWN, button);
    Button(SDL_EVENT_MOUSE_BUTTON_UP, button);
}

void FocusLost() {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    InputHandleEvent(event);
}

class MenuMouseEdges : public testing::Test {
protected:
    void SetUp() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputSetLookOnLeftStick(false);
        InputResetBindings();
        MouseConfigure(false, {});
    }

    void TearDown() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputSetLookOnLeftStick(false);
        InputResetBindings();
        MouseConfigure(true, {});
    }
};

} // namespace

TEST_F(MenuMouseEdges, CompleteClickBetweenReadsIsEmittedOnce) {
    InputSetMenuMouse(true);
    Click(SDL_BUTTON_LEFT);

    const auto click = InputTakeMenuMouse();
    EXPECT_EQ(click.pressed, 1u);
    EXPECT_EQ(click.released, 1u);
    EXPECT_EQ(click.buttons, 0u);

    const auto drained = InputTakeMenuMouse();
    EXPECT_EQ(drained.pressed, 0u);
    EXPECT_EQ(drained.released, 0u);
    EXPECT_EQ(drained.buttons, 0u);
}

TEST_F(MenuMouseEdges, HoldingButtonDoesNotRepeatItsPressEdge) {
    InputSetMenuMouse(true);
    Button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    const auto down = InputTakeMenuMouse();
    EXPECT_EQ(down.pressed, 1u);
    EXPECT_EQ(down.released, 0u);
    EXPECT_EQ(down.buttons, 1u);

    const auto held = InputTakeMenuMouse();
    EXPECT_EQ(held.pressed, 0u);
    EXPECT_EQ(held.released, 0u);
    EXPECT_EQ(held.buttons, 1u);

    Button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    const auto up = InputTakeMenuMouse();
    EXPECT_EQ(up.pressed, 0u);
    EXPECT_EQ(up.released, 1u);
    EXPECT_EQ(up.buttons, 0u);
    EXPECT_EQ(InputTakeMenuMouse().released, 0u);
}

TEST_F(MenuMouseEdges, GameplayClicksDoNotQueueMenuEdges) {
    InputSetMenuMouse(false);
    Click(SDL_BUTTON_LEFT);
    const auto gameplay = InputTakeMenuMouse();
    EXPECT_EQ(gameplay.pressed, 0u);
    EXPECT_EQ(gameplay.released, 0u);

    Click(SDL_BUTTON_RIGHT);
    InputSetMenuMouse(true);
    const auto opened = InputTakeMenuMouse();
    EXPECT_EQ(opened.pressed, 0u);
    EXPECT_EQ(opened.released, 0u);
    EXPECT_EQ(opened.buttons, 0u);
}

TEST_F(MenuMouseEdges, CaptureClickAndItsReleaseAreSwallowed) {
    MouseConfigure(true, {});
    FocusLost();
    ASSERT_FALSE(MouseInputLive());
    InputSetMenuMouse(true);

    Button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    ASSERT_TRUE(MouseInputLive());
    const auto capture = InputTakeMenuMouse();
    EXPECT_EQ(capture.pressed, 0u);
    EXPECT_EQ(capture.released, 0u);
    EXPECT_EQ(capture.buttons, 0u);

    Button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    const auto release = InputTakeMenuMouse();
    EXPECT_EQ(release.pressed, 0u);
    EXPECT_EQ(release.released, 0u);
    EXPECT_EQ(release.buttons, 0u);

    Click(SDL_BUTTON_LEFT);
    const auto click = InputTakeMenuMouse();
    EXPECT_EQ(click.pressed, 1u);
    EXPECT_EQ(click.released, 1u);
}

TEST_F(MenuMouseEdges, FocusLossDiscardsPendingEdges) {
    InputSetMenuMouse(true);
    Click(SDL_BUTTON_LEFT);
    FocusLost();

    const auto lost = InputTakeMenuMouse();
    EXPECT_EQ(lost.pressed, 0u);
    EXPECT_EQ(lost.released, 0u);
    EXPECT_EQ(lost.buttons, 0u);
}

TEST_F(MenuMouseEdges, MenuOwnerTransitionDiscardsPendingEdges) {
    InputSetMenuMouse(true);
    Click(SDL_BUTTON_LEFT);
    InputSetMenuMouse(false);
    InputSetMenuMouse(true);

    const auto reopened = InputTakeMenuMouse();
    EXPECT_EQ(reopened.pressed, 0u);
    EXPECT_EQ(reopened.released, 0u);
    EXPECT_EQ(reopened.buttons, 0u);
}

TEST_F(MenuMouseEdges, RightAndMiddleUseGameButtonOrder) {
    InputSetMenuMouse(true);
    const Uint8 buttons[] = {SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};
    for (const auto button : buttons) {
        SCOPED_TRACE(button);
        const std::uint32_t bit = button == SDL_BUTTON_RIGHT ? 2u : 4u;
        Button(SDL_EVENT_MOUSE_BUTTON_DOWN, button);
        const auto down = InputTakeMenuMouse();
        EXPECT_EQ(down.pressed, bit);
        EXPECT_EQ(down.released, 0u);
        EXPECT_EQ(down.buttons, bit);

        Button(SDL_EVENT_MOUSE_BUTTON_UP, button);
        const auto up = InputTakeMenuMouse();
        EXPECT_EQ(up.pressed, 0u);
        EXPECT_EQ(up.released, bit);
        EXPECT_EQ(up.buttons, 0u);
    }
}

TEST_F(MenuMouseEdges, CaptureReleaseDiscardsPendingEdges) {
    const int release[] = {SDL_SCANCODE_F12};
    MouseConfigure(true, release);
    FocusLost();
    InputSetMenuMouse(true);
    Click(SDL_BUTTON_LEFT); // First click captures; the next click belongs to the menu.
    ASSERT_TRUE(MouseInputLive());
    Click(SDL_BUTTON_LEFT);

    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_F12;
    event.key.down = true;
    InputHandleEvent(event);
    ASSERT_FALSE(MouseInputLive());

    const auto released = InputTakeMenuMouse();
    EXPECT_EQ(released.pressed, 0u);
    EXPECT_EQ(released.released, 0u);
    EXPECT_EQ(released.buttons, 0u);
}

TEST_F(MenuMouseEdges, CaptureButtonReleasePreservesAnotherButtonClick) {
    MouseConfigure(true, {});
    FocusLost();
    InputSetMenuMouse(true);
    Button(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT);
    ASSERT_TRUE(MouseInputLive());

    // The left press only captures the mouse; a right click after it is live input.
    Click(SDL_BUTTON_RIGHT);
    Button(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    const auto click = InputTakeMenuMouse();
    EXPECT_EQ(click.pressed, 2u);
    EXPECT_EQ(click.released, 2u);
    EXPECT_EQ(click.buttons, 0u);
    EXPECT_EQ(InputTakeMenuMouse().pressed, 0u);
    EXPECT_EQ(InputTakeMenuMouse().released, 0u);
}

TEST_F(MenuMouseEdges, ExtraButtonsKeepTheirBitsAndInvalidButtonsAreIgnored) {
    InputSetMenuMouse(true);
    Click(SDL_BUTTON_X1);
    Click(SDL_BUTTON_X2);
    Click(0);
    Click(6);
    const auto clicks = InputTakeMenuMouse();
    EXPECT_EQ(clicks.pressed, 24u);
    EXPECT_EQ(clicks.released, 24u);
    EXPECT_EQ(clicks.buttons, 0u);
}
