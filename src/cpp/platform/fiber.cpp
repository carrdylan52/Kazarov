#include "platform/fiber.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void *CurrentFiber() {
    return GetCurrentFiber();
}
#else
void *CurrentFiber() {
    return nullptr;
}
#endif
