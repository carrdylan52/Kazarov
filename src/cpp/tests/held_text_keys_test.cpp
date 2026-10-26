#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>

#include "platform/input.hpp"
#include "platform/mouse.hpp"

namespace {
void Key(SDL_Scancode scancode, bool down, SDL_Keymod mod = SDL_KMOD_NONE) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.mod = mod;
    InputHandleEvent(event);
}

// Restores a key's SDL name at the end of the scope.
struct ScancodeName {
    SDL_Scancode scancode;
    const char *original = SDL_GetScancodeName(scancode);
    ~ScancodeName() { SDL_SetScancodeName(scancode, original); }
};

class HeldTextKeys : public testing::Test {
protected:
    void SetUp() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputResetBindings();
        MouseConfigure(false, {});
    }
    void TearDown() override {
        InputShutdown();
        InputSetOverride(0, nullptr);
        InputResetBindings();
    }
};
} // namespace

TEST_F(HeldTextKeys, LiveAndScriptedSourcesShareOnePhysicalReleaseIdentity) {
    Key(SDL_SCANCODE_W, true);
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_S}});
    const auto names = InputHeldTextKeys();
    EXPECT_EQ(names, (std::vector<std::string>{"S", "W"}));
    Key(SDL_SCANCODE_W, false);
    EXPECT_TRUE(InputBindSourceHeld("W"));
    EXPECT_EQ(InputHeldTextKeys(), names);
    InputSetScriptedDevices({});
    EXPECT_FALSE(InputBindSourceHeld("W"));
    EXPECT_FALSE(InputBindSourceHeld("S"));
    EXPECT_TRUE(InputHeldTextKeys().empty());
}

TEST_F(HeldTextKeys, TextEntryTransitionsKeepHeldCharactersAndEditingKeysObservable) {
    InputSetTextEntry(true);
    Key(SDL_SCANCODE_W, true, SDL_KMOD_LSHIFT);
    Key(SDL_SCANCODE_SPACE, true);
    Key(SDL_SCANCODE_BACKSPACE, true);
    Key(SDL_SCANCODE_DELETE, true);
    Key(SDL_SCANCODE_RETURN, true);
    const std::vector<std::string> expected{"W", "Return", "Backspace", "Space", "Delete"};
    EXPECT_EQ(InputHeldTextKeys(), expected);
    EXPECT_EQ(InputTakeText(), "W \b\x7f\r");
    EXPECT_NE(InputApplyKeyboardMouse({}, {.keys = {SDL_SCANCODE_RETURN}}).buttons & kInputCross, 0);
    EXPECT_EQ(InputApplyKeyboardMouse({}, {.keys = {SDL_SCANCODE_W}}).left_y, 128);
    InputSetTextEntry(false);
    EXPECT_EQ(InputHeldTextKeys(), expected);
    EXPECT_LT(InputApplyKeyboardMouse({}, {.keys = {SDL_SCANCODE_W}}).left_y, 128);
    InputSetTextEntry(true);
    for (int i = 0; i < 3; ++i) InputLatchPad(0);
    EXPECT_EQ(InputHeldTextKeys(), expected);
    for (const auto &name : expected) {
        ASSERT_TRUE(InputBindSourceHeld(name));
        Key(static_cast<SDL_Scancode>(InputScancodeFromName(name)), false);
        EXPECT_FALSE(InputBindSourceHeld(name));
    }
    EXPECT_TRUE(InputHeldTextKeys().empty());
}

TEST_F(HeldTextKeys, ModifierChangesDoNotHideLettersOrIncludeNonTextKeys) {
    const std::vector<int> nontext{SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT,
        SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL, SDL_SCANCODE_LALT, SDL_SCANCODE_RALT,
        SDL_SCANCODE_LGUI, SDL_SCANCODE_RGUI, SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_NUMLOCKCLEAR,
        SDL_SCANCODE_MODE, SDL_SCANCODE_TAB, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1,
        SDL_SCANCODE_UP, SDL_SCANCODE_KP_ENTER, SDL_SCANCODE_KP_1};
    InputSetScriptedDevices({.keys = nontext});
    EXPECT_TRUE(InputHeldTextKeys().empty());
    Key(SDL_SCANCODE_W, true, SDL_KMOD_RSHIFT | SDL_KMOD_RALT);
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"W"}));
    Key(SDL_SCANCODE_RSHIFT, false);
    Key(SDL_SCANCODE_RALT, false);
    InputSetScriptedDevices({});
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"W"}));
    Key(SDL_SCANCODE_W, false);
    EXPECT_TRUE(InputHeldTextKeys().empty());
}

TEST_F(HeldTextKeys, ObservingDoesNotDrainTextConsumeBindEdgesOrAdvancePadHistory) {
    InputBeginBindCapture();
    InputSetTextEntry(true);
    Key(SDL_SCANCODE_W, true);
    InputPadState pad{.connected = true, .buttons = kInputSquare, .left_x = 211};
    InputSetOverride(0, &pad);
    InputLatchPad(0);
    const auto read = InputGetMouseLook().read;
    const bool stick_live = InputLeftStickLive();
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"W"}));
    }
    EXPECT_EQ(InputGetMouseLook().read, read);
    EXPECT_EQ(InputLeftStickLive(), stick_live);
    EXPECT_EQ(InputGetPad(0).buttons, pad.buttons);
#ifndef KZ_TOUCH_OVERLAY
    EXPECT_EQ(InputGetPad(0).left_x, pad.left_x);
#endif
    EXPECT_EQ(InputTakeText(), "w");
    EXPECT_EQ(InputTakeBindKey(false), "W");
    EXPECT_TRUE(InputTakeBindKey(false).empty());
    Key(SDL_SCANCODE_S, true);
    EXPECT_EQ(InputTakeText(), "s"); // The observer did not expire text capture.
    InputShutdown();
    Key(SDL_SCANCODE_D, true);
    EXPECT_TRUE(InputTakeBindKey(false).empty()); // Shutdown closed the capture.
}

TEST_F(HeldTextKeys, PointerCaptureAndFocusFollowExistingPhysicalSourceLifecycle) {
    int release[] = {SDL_SCANCODE_F12};
    MouseConfigure(true, release);
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_LEFT;
    InputHandleEvent(click);
    ASSERT_TRUE(MouseCaptured());
    Key(SDL_SCANCODE_W, true);
    Key(SDL_SCANCODE_F12, true);
    ASSERT_FALSE(MouseCaptured());
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"W"}));
    InputHandleEvent(click);
    ASSERT_TRUE(MouseCaptured());
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"W"}));
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_S}});
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    InputHandleEvent(focus);
    EXPECT_FALSE(InputBindSourceHeld("W"));
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"S"}));
    InputShutdown();
    EXPECT_TRUE(InputHeldTextKeys().empty());
}

TEST_F(HeldTextKeys, ScancodeEnumerationIsBoundedAndEachNameReleasesWithItsOwnKey) {
    InputKeyboardMouse held;
    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code) held.keys.push_back(code);
    held.keys.insert(held.keys.end(), {-1, SDL_SCANCODE_COUNT, 1000000, SDL_SCANCODE_W});
    InputSetScriptedDevices(held);
    const auto names = InputHeldTextKeys();
    EXPECT_LE(names.size(), SDL_SCANCODE_COUNT);
    EXPECT_EQ(std::set<std::string>(names.begin(), names.end()).size(), names.size());
    // Held alone, each key's name must read back as that key, not another one of the same name.
    std::size_t text_keys = 0;
    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code) {
        InputSetScriptedDevices({.keys = {code}});
        const auto alone = InputHeldTextKeys();
        ASSERT_LE(alone.size(), 1u) << code;
        if (alone.empty()) continue;
        ++text_keys;
        EXPECT_TRUE(InputBindSourceHeld(alone[0])) << alone[0];
        InputSetScriptedDevices({});
        EXPECT_FALSE(InputBindSourceHeld(alone[0])) << alone[0];
    }
    EXPECT_EQ(text_keys, names.size());
}

TEST_F(HeldTextKeys, KeysWithoutTheirOwnNameAreNumbered) {
    // SDL resolves a shared name to its first key, as with "Menu" once SDL's desktop drivers give
    // that name to Application too. Renaming A to "Space" makes Space such a later key here.
    ScancodeName shared{SDL_SCANCODE_A}, unnamed{SDL_SCANCODE_W};
    ASSERT_TRUE(SDL_SetScancodeName(SDL_SCANCODE_A, "Space"));
    ASSERT_TRUE(SDL_SetScancodeName(SDL_SCANCODE_W, ""));
    Key(SDL_SCANCODE_SPACE, true);
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_W}});
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"Scancode 26", "Scancode 44"}));
    EXPECT_FALSE(InputBindSourceHeld("Space")); // Read back as A, which is up.
    EXPECT_TRUE(InputBindSourceHeld("scancode 44"));
    Key(SDL_SCANCODE_SPACE, false);
    EXPECT_FALSE(InputBindSourceHeld("Scancode 44"));
    EXPECT_TRUE(InputBindSourceHeld("Scancode 26"));
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_A}});
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"Space"}));
    EXPECT_TRUE(InputBindSourceHeld("Space"));
    EXPECT_FALSE(InputBindSourceHeld("Scancode 26"));
    InputKeyboardMouse all;
    for (int code = 0; code < SDL_SCANCODE_COUNT; ++code) all.keys.push_back(code);
    InputSetScriptedDevices(all);
    for (const char *bad : {"Scancode", "Scancode ", "Scancode -1", "Scancode 512", "Scancode 26x",
                            "Scancode  26", "Scancode +26"}) {
        EXPECT_FALSE(InputBindSourceHeld(bad)) << bad;
    }
    EXPECT_EQ(InputScancodeFromName("Scancode 26"), -1); // Not a binding name.
}

TEST_F(HeldTextKeys, KeysNamedLikeAnotherSourceAreNumbered) {
    // SDL_SetScancodeName accepts any name, including one InputBindSourceHeld reads as another
    // key's number, a mouse button or a gamepad button.
    ScancodeName numbered{SDL_SCANCODE_S}, mouse{SDL_SCANCODE_D}, pad{SDL_SCANCODE_F};
    ASSERT_TRUE(SDL_SetScancodeName(SDL_SCANCODE_S, "Scancode 26"));
    ASSERT_TRUE(SDL_SetScancodeName(SDL_SCANCODE_D, "Mouse1"));
    ASSERT_TRUE(SDL_SetScancodeName(SDL_SCANCODE_F, "Gamepad:a"));
    for (SDL_Scancode code : {SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_S}) {
        const std::string name = "Scancode " + std::to_string(code);
        InputSetScriptedDevices({.keys = {code}});
        EXPECT_EQ(InputHeldTextKeys(), std::vector<std::string>{name});
        EXPECT_TRUE(InputBindSourceHeld(name)) << name;
        InputSetScriptedDevices({});
        EXPECT_FALSE(InputBindSourceHeld(name)) << name;
    }
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_S, SDL_SCANCODE_W}});
    EXPECT_EQ(InputHeldTextKeys(), (std::vector<std::string>{"Scancode 22", "W"}));
    InputSetScriptedDevices({.keys = {SDL_SCANCODE_W}});
    EXPECT_FALSE(InputBindSourceHeld("Scancode 22"));
    EXPECT_TRUE(InputBindSourceHeld("W"));
}
