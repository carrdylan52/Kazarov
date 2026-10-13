#pragma once

// The fiber the calling thread runs on, for a thread that is already a fiber (an embedding host runs
// the game on one). Windows only: GetCurrentFiber is an inline macro in windows.h, not a kernel32
// export, so it is read in platform/fiber.cpp rather than declared by hand. Null elsewhere.
void *CurrentFiber();
