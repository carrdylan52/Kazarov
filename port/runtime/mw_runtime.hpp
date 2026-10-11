#pragma once

namespace runtime {

// libgcc's fp-bit conversions, which the Metrowerks code calls by name: toward zero, NaN to 0,
// saturated at the type's range; Fptoui takes every negative value to 0, not just those above -1.
int          Fptosi(float value);
unsigned int Fptoui(float value);

// The game prints its own context on stdout before it asserts (LoadFile names the file), so stdout
// is flushed ahead of the message.
[[noreturn]] void AssertFailed(const char *file, int line, const char *expression, int status);

// Handlers run last added first, ahead of what Exit closes itself.
void AtExit(void (*handler)());

// The game's own exit: what it wrote to the memory cards and a WAV capture is closed and the audio
// device stopped before the process ends with its status.
[[noreturn]] void Exit(int status);

} // namespace runtime
