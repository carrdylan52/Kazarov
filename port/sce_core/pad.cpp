#include "pad.hpp"

#include <array>
#include <cstdint>
#include <cstring>

#include "platform/input.hpp"

namespace sce_core::pad {

namespace {

// The controller ID byte: the terminal type in the high nibble (what pad_button_read keeps as the
// mode), the payload length in halfwords in the low one.
constexpr unsigned char kDualShockId = kTerminalId << 4 | 0x3;
constexpr int           kReadLength = 2 + (kDualShockId & 0x0F) * 2;

std::array<bool, kInputPadCount> g_open;

bool Valid(int port) {
    return port >= 0 && port < kInputPadCount;
}

} // namespace

void Init() {
    g_open = {};
}

bool Open(int port) {
    if (!Valid(port)) {
        return false;
    }
    g_open[port] = true;
    return true;
}

bool Ready(int port) {
    return Valid(port) && g_open[port] && InputGetPad(port).connected;
}

int Read(int port, unsigned char *data) {
    if (!Ready(port)) {
        return 0;
    }
    const InputPadState &pad = InputGetPad(port);
    std::uint16_t        active = static_cast<std::uint16_t>(~pad.buttons);
    std::memset(data, 0, kReadBufferSize);
    data[0] = 0;
    data[1] = kDualShockId;
    data[2] = static_cast<unsigned char>(active >> 8);
    data[3] = static_cast<unsigned char>(active);
    data[4] = pad.right_x;
    data[5] = pad.right_y;
    data[6] = pad.left_x;
    data[7] = pad.left_y;
    return kReadLength;
}

int ActuatorInfo(int port, int actuator) {
    if (!Ready(port)) {
        return 0;
    }
    if (actuator < 0) {
        return kActuatorCount;
    }
    return actuator < kActuatorCount ? 1 : 0;
}

bool SetActuators(int port, const unsigned char *values) {
    if (!Valid(port) || !g_open[port]) {
        return false;
    }
    InputSetRumble(port, {values[0] != 0, values[1]});
    return true;
}

} // namespace sce_core::pad
