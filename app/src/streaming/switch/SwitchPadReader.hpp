#pragma once

#include <borealis.hpp>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace moonvibe {
// Reads Npad state with its own PadState objects, so a worker thread can poll
// controllers without touching the PadState that Borealis' UI loop updates.
// The slot order and button/axis mapping mirror Borealis' SwitchInputManager:
// handheld first when active, then numbered Npads until the first empty one.
class SwitchPadReader {
public:
    SwitchPadReader();
    short connectedCount();
    void read(brls::ControllerState* state, int controller);
    // True for Joy-Con class sticks (handheld, dual/single Joy-Con); false for
    // Pro Controller and other full-size pads.
    bool hasSmallSticks(int controller);

private:
#ifdef __SWITCH__
    void readPad(brls::ControllerState* state, PadState* pad);
    PadState handheld{};
    PadState pads[GAMEPADS_MAX]{};
#endif
};
} // namespace moonvibe
