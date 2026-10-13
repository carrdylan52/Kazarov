#include "embed.hpp"

#include "clock.hpp"

namespace {

EmbedHooks g_hooks;
bool       g_active = false;

} // namespace

void EmbedEnable(const EmbedHooks &hooks) {
    g_hooks = hooks;
    g_active = true;
}

bool EmbedActive() {
    return g_active;
}

bool EmbedPollEvent(SDL_Event *event) {
    if (!g_active || g_hooks.poll_event == nullptr) {
        return false;
    }
    return g_hooks.poll_event(g_hooks.user, event);
}

bool EmbedCheckpoint(int reason) {
    if (!g_active || g_hooks.checkpoint == nullptr || ClockPumping()) {
        return false;
    }
    if (!g_hooks.checkpoint(g_hooks.user, reason)) {
        return false;
    }
    // The clock was anchored before the park: re-anchor it at now, keeping the count, so the
    // parked time is not replayed as ticks.
    ClockRebase();
    return true;
}
