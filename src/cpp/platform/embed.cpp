#include "platform/embed.hpp"

#include <SDL3/SDL.h>

#include "platform/clock.hpp"

namespace {

EmbedHooks g_hooks;
bool       g_embedded = false;

} // namespace

void EmbedEnable(const EmbedHooks &hooks) {
    g_hooks = hooks;
    g_embedded = true;
}

bool EmbedActive() {
    return g_embedded;
}

bool EmbedPollEvent(SDL_Event *event) {
    if (!g_embedded || g_hooks.poll_event == nullptr) {
        return false;
    }
    return g_hooks.poll_event(g_hooks.user, event);
}

bool EmbedCheckpoint(int reason) {
    // The host can only switch at a safe point: never from inside a clock tick or a pump hook.
    if (!g_embedded || g_hooks.checkpoint == nullptr || ClockPumping()) {
        return false;
    }
    if (!g_hooks.checkpoint(g_hooks.user, reason)) {
        return false;
    }
    // The game was parked: the wall time it spent away is not game time.
    ClockRebase();
    return true;
}
