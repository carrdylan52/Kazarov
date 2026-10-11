#pragma once

// libpad over the host's input: every opened port with a connected device is a DualShock 2 in
// analog mode, stable from the first call.

namespace sce_core::pad {

constexpr int kTerminalId = 7;
constexpr int kActuatorCount = 2;
constexpr int kReadBufferSize = 32;

void Init();
bool Open(int port);
bool Ready(int port);
// The reply's length, or 0 if the port is not ready.
int  Read(int port, unsigned char *data);
// actuator -1 asks for the count. The answer is 0 for a port that is not ready.
int  ActuatorInfo(int port, int actuator);
// values: the small motor's on/off byte, then the large one's speed.
bool SetActuators(int port, const unsigned char *values);

} // namespace sce_core::pad
