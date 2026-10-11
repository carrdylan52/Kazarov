#pragma once

[[noreturn]] void Ps2Unimplemented(const char *function, const char *file, int line);

#define PS2_UNIMPLEMENTED() Ps2Unimplemented(__func__, __FILE__, __LINE__)
