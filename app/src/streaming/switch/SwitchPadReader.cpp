#include "SwitchPadReader.hpp"

#ifdef __SWITCH__
#include <borealis/platforms/switch/switch_input.hpp>
#endif

namespace moonvibe {
#ifdef __SWITCH__
namespace {
using namespace brls;

constexpr uint64_t HidNpadButton_None = ((uint64_t)1 << (63));

// Copied from Borealis' switch_input.cpp, which keeps these tables private.
constexpr uint64_t FULL_MAPPING[_BUTTON_MAX] = {
    HidNpadButton_ZL, // BUTTON_LT
    HidNpadButton_L, // BUTTON_LB
    HidNpadButton_StickL, // BUTTON_LSB
    HidNpadButton_Up, // BUTTON_UP
    HidNpadButton_Right, // BUTTON_RIGHT
    HidNpadButton_Down, // BUTTON_DOWN
    HidNpadButton_Left, // BUTTON_LEFT
    HidNpadButton_Minus, // BUTTON_BACK
    HidNpadButton_None, // BUTTON_GUIDE
    HidNpadButton_Plus, // BUTTON_START
    HidNpadButton_StickR, // BUTTON_RSB
    HidNpadButton_Y, // BUTTON_Y
    HidNpadButton_B, // BUTTON_B
    HidNpadButton_A, // BUTTON_A
    HidNpadButton_X, // BUTTON_X
    HidNpadButton_R, // BUTTON_RB
    HidNpadButton_ZR, // BUTTON_RT
    HidNpadButton_AnyUp, // BUTTON_NAV_UP
    HidNpadButton_AnyRight, // BUTTON_NAV_RIGHT
    HidNpadButton_AnyDown, // BUTTON_NAV_DOWN
    HidNpadButton_AnyLeft, // BUTTON_NAV_LEFT
};

constexpr uint64_t HALF_MAPPING[_BUTTON_MAX] = {
    HidNpadButton_None, // BUTTON_LT
    HidNpadButton_AnySL, // BUTTON_LB
    HidNpadButton_StickL | HidNpadButton_StickR, // BUTTON_LSB
    HidNpadButton_None, // BUTTON_UP
    HidNpadButton_None, // BUTTON_RIGHT
    HidNpadButton_None, // BUTTON_DOWN
    HidNpadButton_None, // BUTTON_LEFT
    HidNpadButton_None, // BUTTON_BACK
    HidNpadButton_None, // BUTTON_GUIDE
    HidNpadButton_Plus | HidNpadButton_Minus, // BUTTON_START
    HidNpadButton_None, // BUTTON_RSB
    HidNpadButton_B | HidNpadButton_Up, // BUTTON_Y
    HidNpadButton_A | HidNpadButton_Left, // BUTTON_B
    HidNpadButton_X | HidNpadButton_Down, // BUTTON_A
    HidNpadButton_Y | HidNpadButton_Right, // BUTTON_X
    HidNpadButton_AnySR, // BUTTON_RB
    HidNpadButton_None, // BUTTON_RT
    HidNpadButton_StickLRight | HidNpadButton_StickRLeft, // BUTTON_NAV_UP
    HidNpadButton_StickLDown | HidNpadButton_StickRUp, // BUTTON_NAV_RIGHT
    HidNpadButton_StickLLeft | HidNpadButton_StickRRight, // BUTTON_NAV_DOWN
    HidNpadButton_StickLUp | HidNpadButton_StickRDown, // BUTTON_NAV_LEFT
};
} // namespace

SwitchPadReader::SwitchPadReader() {
    // padConfigureInput() was already called by Borealis; these only attach
    // independent readers to the same shared-memory Npad entries.
    padInitialize(&handheld, HidNpadIdType_Handheld);
    for (int i = 0; i < GAMEPADS_MAX; i++)
        padInitialize(&pads[i], (HidNpadIdType)i);
}

short SwitchPadReader::connectedCount() {
    padUpdate(&handheld);
    int extra = handheld.active_handheld ? 1 : 0;
    int controllers = extra;
    for (int i = 0; i < GAMEPADS_MAX - extra; i++) {
        padUpdate(&pads[i]);
        if (pads[i].style_set == 0)
            break;
        controllers++;
    }
    return controllers;
}

void SwitchPadReader::read(brls::ControllerState* state, int controller) {
    padUpdate(&handheld);
    if (controller == 0 && handheld.active_handheld) {
        readPad(state, &handheld);
        return;
    }
    int local = handheld.active_handheld ? controller - 1 : controller;
    if (local < 0 || local >= GAMEPADS_MAX) {
        *state = {};
        return;
    }
    readPad(state, &pads[local]);
}

bool SwitchPadReader::hasSmallSticks(int controller) {
    padUpdate(&handheld);
    PadState* pad = &handheld;
    if (!(controller == 0 && handheld.active_handheld)) {
        int local = handheld.active_handheld ? controller - 1 : controller;
        if (local < 0 || local >= GAMEPADS_MAX)
            return false;
        pad = &pads[local];
        padUpdate(pad);
    }
    const u32 style = padGetStyleSet(pad);
    return (style & (HidNpadStyleTag_NpadHandheld | HidNpadStyleTag_NpadJoyDual |
                     HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight)) != 0 &&
           !(style & HidNpadStyleTag_NpadFullKey);
}

void SwitchPadReader::readPad(brls::ControllerState* state, PadState* pad) {
    padUpdate(pad);
    uint64_t keysDown = padGetButtons(pad);
    bool full = pad->style_set & HidNpadStyleSet_NpadFullCtrl;

    for (size_t i = 0; i < _BUTTON_MAX; i++) {
        uint64_t switchKey = full ? FULL_MAPPING[i] : HALF_MAPPING[i];
        state->buttons[i] = keysDown & switchKey;
    }

    // HOME/capture overrides are tracked by Borealis' button thread.
    auto* input = static_cast<SwitchInputManager*>(
        Application::getPlatform()->getInputManager());
    state->buttons[BUTTON_GUIDE] = false;
    if (input->screenshotButtonOverrideMode() == ButtonOverrideMode::GUIDE_BUTTON)
        state->buttons[BUTTON_GUIDE] |= input->isScreenshotButtonPressed();
    if (input->homeButtonOverrideMode() == ButtonOverrideMode::GUIDE_BUTTON)
        state->buttons[BUTTON_GUIDE] |= input->isHomeButtonPressed();

    HidAnalogStickState stickL = padGetStickPos(pad, 0);
    HidAnalogStickState stickR = padGetStickPos(pad, 1);

    if (full) {
        state->axes[LEFT_X] = (float)stickL.x / (float)0x7FFF;
        state->axes[LEFT_Y] = (float)stickL.y / (float)0x7FFF * -1.0f;
        state->axes[RIGHT_X] = (float)stickR.x / (float)0x7FFF;
        state->axes[RIGHT_Y] = (float)stickR.y / (float)0x7FFF * -1.0f;
    } else {
        bool swap = Application::isSwapHalfJoyconStickToDpad();
        float x = (float)stickL.y / (float)0x7FFF * -1.0f + (float)stickR.y / (float)0x7FFF;
        float y = (float)stickL.x / (float)0x7FFF * -1.0f + (float)stickR.x / (float)0x7FFF;
        if (!swap) {
            state->axes[LEFT_X] = x;
            state->axes[LEFT_Y] = y;
        } else {
            state->axes[LEFT_X] = 0;
            state->axes[LEFT_Y] = 0;
            state->buttons[BUTTON_UP] = y < -0.3f;
            state->buttons[BUTTON_DOWN] = y > 0.3f;
            state->buttons[BUTTON_RIGHT] = x > 0.3f;
            state->buttons[BUTTON_LEFT] = x < -0.3f;
        }
        state->axes[RIGHT_X] = 0;
        state->axes[RIGHT_Y] = 0;
    }

    state->axes[LEFT_Z] = 0;
    state->axes[RIGHT_Z] = 0;
}
#else
SwitchPadReader::SwitchPadReader() = default;
short SwitchPadReader::connectedCount() { return 0; }
void SwitchPadReader::read(brls::ControllerState* state, int) { *state = {}; }
bool SwitchPadReader::hasSmallSticks(int) { return true; }
#endif
} // namespace moonvibe
