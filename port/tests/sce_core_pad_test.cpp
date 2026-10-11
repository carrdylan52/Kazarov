#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>

#include "platform/input.hpp"
#include "sce_core/pad.hpp"

namespace pad = sce_core::pad;

namespace {

void Plug(int port, std::uint16_t buttons, std::uint8_t lx, std::uint8_t ly, std::uint8_t rx, std::uint8_t ry) {
    InputPadState state;
    state.connected = true;
    state.buttons = buttons;
    state.left_x = lx;
    state.left_y = ly;
    state.right_x = rx;
    state.right_y = ry;
    InputSetOverride(port, &state);
}

} // namespace

TEST(SceCorePad, ReadsTheDualShockReply) {
    pad::Init();
    ASSERT_TRUE(pad::Open(0));
    Plug(0, kInputCross | kInputUp, 255, 0, 10, 200);
    unsigned char data[pad::kReadBufferSize];
    std::fill(std::begin(data), std::end(data), 0xAA);
    ASSERT_EQ(pad::Read(0, data), 8);
    EXPECT_EQ(data[0], 0);
    EXPECT_EQ(data[1], 0x73);
    EXPECT_EQ(((data[2] << 8) | data[3]) ^ 0xFFFF, kInputCross | kInputUp);
    EXPECT_EQ(data[4], 10);
    EXPECT_EQ(data[5], 200);
    EXPECT_EQ(data[6], 255);
    EXPECT_EQ(data[7], 0);
    EXPECT_EQ(data[pad::kReadBufferSize - 1], 0);
    InputSetOverride(0, nullptr);
}

TEST(SceCorePad, OnlyAnOpenConnectedPortIsReady) {
    pad::Init();
    Plug(1, 0, 128, 128, 128, 128);
    unsigned char data[pad::kReadBufferSize];
    EXPECT_FALSE(pad::Ready(1));
    EXPECT_EQ(pad::Read(1, data), 0);
    EXPECT_EQ(pad::ActuatorInfo(1, -1), 0);
    ASSERT_TRUE(pad::Open(1));
    EXPECT_TRUE(pad::Ready(1));
    EXPECT_EQ(pad::ActuatorInfo(1, -1), pad::kActuatorCount);
    EXPECT_EQ(pad::ActuatorInfo(1, 1), 1);
    EXPECT_EQ(pad::ActuatorInfo(1, 2), 0);
    InputPadState unplugged;
    InputSetOverride(1, &unplugged);
    EXPECT_FALSE(pad::Ready(1));
    EXPECT_FALSE(pad::Open(kInputPadCount));
    InputSetOverride(1, nullptr);
}

TEST(SceCorePad, ActuatorsDriveTheRumble) {
    pad::Init();
    const unsigned char values[2] = {1, 200};
    EXPECT_FALSE(pad::SetActuators(0, values));
    ASSERT_TRUE(pad::Open(0));
    ASSERT_TRUE(pad::SetActuators(0, values));
    InputRumble rumble = InputGetRumble(0);
    EXPECT_TRUE(rumble.small_motor);
    EXPECT_EQ(rumble.large_motor, 200);
}
