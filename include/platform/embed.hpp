#pragma once

// Embedded mode: the game runs inside a host program that owns SDL and the event queue and that
// may park the game at checkpoints while something else runs on the same thread (Ixion runs two
// games in one process this way). Without EmbedEnable everything here is a no-op and the runtime
// behaves exactly as a standalone game.
//
// While embedded:
// - WindowInit leaves SDL's app metadata and driver hints to the host and starts only the video
//   subsystem it needs (SDL_InitSubSystem); WindowShutdown destroys the window but does not call
//   SDL_Quit.
// - WindowPollEvents reads events from the host (EmbedPollEvent) instead of SDL_PollEvent.
// - EmbedCheckpoint hands control to the host; when the game was parked it rebases the clock
//   (ClockRebase) before returning, so the parked time is not replayed as ticks.
// - On Windows the PS2 thread scheduler adopts the fiber the host runs the game on instead of
//   converting the thread to a fiber itself.

union SDL_Event;

struct EmbedHooks {
    void *user = nullptr;
    // Fills *event with the next event the host routed to this game; false when there is none.
    bool (*poll_event)(void *user, SDL_Event *event) = nullptr;
    // Lets the host switch away; returns once the game is resumed. True if the game was parked.
    bool (*checkpoint)(void *user, int reason) = nullptr;
};

enum EmbedCheckpointReason {
    kEmbedFrameEnd = 1,
};

// Turns embedded mode on. Call before WindowInit.
void EmbedEnable(const EmbedHooks &hooks);
bool EmbedActive();
// Takes the next host event; false when there is none or when not embedded.
bool EmbedPollEvent(SDL_Event *event);
// Gives the host a chance to switch games. Call only at a safe point: outside graphics recording
// and outside ClockPump. Returns true if the game was parked, after the clock has been rebased;
// the caller then resets any pacing it measures against wall time. False when not embedded.
bool EmbedCheckpoint(int reason);
