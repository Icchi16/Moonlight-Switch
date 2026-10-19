
#ifdef __SWITCH__
#include <switch.h>
#endif

#include "InputManager.hpp"
#include "Limelight.h"
#include "Settings.hpp"
#include "switch/SwitchMotion.hpp"
#include <borealis.hpp>
#include <streaming_view.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

using namespace brls;

namespace {
constexpr float STICK_SCROLL_DEADZONE = 0.2f;
constexpr float MOONLIGHT_WHEEL_DELTA = 120.0f;
constexpr auto DESKTOP_SCROLL_GESTURE_TIMEOUT =
    std::chrono::milliseconds(600);

float applyStickScrollDeadzone(float axis, float configuredDeadzone) {
    float deadzone = std::fmax(STICK_SCROLL_DEADZONE, configuredDeadzone);
    return std::fabs(axis) < deadzone ? 0.f : axis;
}

// Scaled radial deadzone: inside the deadzone the stick is centred, outside it
// the remaining travel is stretched back to 0..1 so the first movement past
// the edge is small rather than a jump to the deadzone value. Also clamps the
// magnitude to 1 so diagonals cannot overflow the 16-bit axis.
//
// antiDeadzone raises the output floor: any movement past the deadzone maps to
// antiDeadzone..1 instead of 0..1, cancelling the dead zone built into the game
// so tiny aim adjustments register. Direction is preserved (radial).
//
// outerLimit is the travel that counts as fully pushed: Joy-Con sticks often
// top out below 1.0, so travel is stretched over deadzone..outerLimit.
// curve (0..1) bends the response with an exponent of 1 + 2*curve, so the
// stick is gentle near the centre and ramps up towards the rim.
// Order: dead zone -> outer limit -> curve -> anti-dead-zone floor.
struct StickTuning {
    float deadzone = 0.f;
    float antiDeadzone = 0.f;
    float curve = 0.f;
    float outerLimit = 1.f;
};

void applyStickResponse(float& x, float& y, StickTuning t) {
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude <= 0.f)
        return;
    float deadzone = std::clamp(t.deadzone, 0.f, 0.95f);
    const float antiDeadzone = std::clamp(t.antiDeadzone, 0.f, 0.5f);
    // Sensor noise around the centre would otherwise jump straight to the
    // anti-dead-zone floor and make the camera drift.
    if (antiDeadzone > 0.f)
        deadzone = std::fmax(deadzone, 0.03f);
    if (magnitude <= deadzone) {
        x = 0;
        y = 0;
        return;
    }
    const float outer = std::fmax(std::clamp(t.outerLimit, 0.5f, 1.f), deadzone + 0.05f);
    float scaled = std::fmin((magnitude - deadzone) / (outer - deadzone), 1.f);
    const float curve = std::clamp(t.curve, 0.f, 1.f);
    if (curve > 0.f)
        scaled = std::pow(scaled, 1.f + 2.f * curve);
    scaled = antiDeadzone + scaled * (1.f - antiDeadzone);
    x = x / magnitude * scaled;
    y = y / magnitude * scaled;
}
}


MoonlightInputManager::MoonlightInputManager() {
    auto inputManager = brls::Application::getPlatform()->getInputManager();

    inputManager
        ->getMouseCusorOffsetChanged()
        ->subscribe([this](brls::Point offset) {
            if (!inputEnabled) return;

            if (offset.x != 0 || offset.y != 0) {
                float multiplier =
                        (float) Settings::instance().get_mouse_speed_multiplier() / 100.f *
                        1.5f + 0.5f;

                if (!this->inputDropped) {
                    sendRelativeMouseMove(
                        brls::Point(offset.x * multiplier,
                                    offset.y * multiplier));
                }
            }
        });

    inputManager
        ->getMouseScrollOffsetChanged()
        ->subscribe([this](brls::Point scroll) {
            if (!inputEnabled) return;
            handleDesktopMouseScroll(scroll);
        });

    inputManager
        ->getKeyboardKeyStateChanged()
        ->subscribe([this](brls::KeyState state) {
            if (!inputEnabled) return;

            int vkKey = MoonlightInputManager::glfwKeyToVKKey(state.key);
            char modifiers = state.mods;
            LiSendKeyboardEvent(vkKey,
                                state.pressed ? KEY_ACTION_DOWN : KEY_ACTION_UP,
                                modifiers);
        });

#ifndef __SWITCH__
    inputManager
        ->getControllerSensorStateChanged()
        ->subscribe([this](brls::SensorEvent event) {
            if (!inputEnabled) return;
            
            switch (event.type) {
                case brls::SensorEventType::ACCEL:
                    LiSendControllerMotionEvent((uint8_t)event.controllerIndex, LI_MOTION_TYPE_ACCEL, event.data[0], event.data[1], event.data[2]);
                    break;
                case brls::SensorEventType::GYRO:
                    // Convert rad/s to deg/s
                    LiSendControllerMotionEvent((uint8_t)event.controllerIndex, LI_MOTION_TYPE_GYRO, 
                        event.data[0] * 57.2957795f, 
                        event.data[1] * 57.2957795f, 
                        event.data[2] * 57.2957795f);
                    break;
            }
        });
#endif
}

void MoonlightInputManager::setInputEnabled(bool enabled) {
    inputEnabled = enabled;
#ifdef __SWITCH__
    moonvibe::SwitchMotion::instance().pause(!enabled);
#endif
}

void MoonlightInputManager::resetSession() {
    std::lock_guard<std::mutex> lock(controllerMutex);
    announcedControllerCount = -1;
    std::fill(std::begin(announcedControllerIdentity), std::end(announcedControllerIdentity), 0);
    std::fill(std::begin(lastGamepadStates), std::end(lastGamepadStates), GamepadState{});
    inputDropped = false;
}

void MoonlightInputManager::handleDesktopMouseScroll(brls::Point scroll) {
    const auto now = std::chrono::steady_clock::now();
    if (lastDesktopScrollEvent.time_since_epoch().count() == 0 ||
        now - lastDesktopScrollEvent > DESKTOP_SCROLL_GESTURE_TIMEOUT) {
        desktopScrollAxis = DesktopScrollAxis::None;
        pendingHorizontalScroll = 0;
        pendingHorizontalScrollEvents = 0;
    }
    lastDesktopScrollEvent = now;

    // Trackpads frequently emit a small perpendicular component. Some virtual
    // mouse drivers quantize that component into a full wheel tick, so lock a
    // gesture to one axis instead of forwarding diagonal jitter to the host.
    if (scroll.y != 0 &&
        (scroll.x == 0 || std::fabs(scroll.y) >= std::fabs(scroll.x))) {
        desktopScrollAxis = DesktopScrollAxis::Vertical;
        pendingHorizontalScroll = 0;
        pendingHorizontalScrollEvents = 0;
        sendDesktopMouseScroll({0, scroll.y});
        return;
    }

    if (scroll.x == 0 || desktopScrollAxis == DesktopScrollAxis::Vertical)
        return;

    if (desktopScrollAxis == DesktopScrollAxis::None) {
        pendingHorizontalScroll += scroll.x;
        pendingHorizontalScrollEvents++;

        // A single isolated horizontal tick is normally diagonal jitter.
        // Buffer it until a second tick confirms an intentional gesture.
        if (pendingHorizontalScrollEvents < 2)
            return;

        desktopScrollAxis = DesktopScrollAxis::Horizontal;
        scroll.x = pendingHorizontalScroll;
        pendingHorizontalScroll = 0;
        pendingHorizontalScrollEvents = 0;
    }

    sendDesktopMouseScroll({scroll.x, 0});
}

void MoonlightInputManager::sendDesktopMouseScroll(brls::Point scroll) {
    desktopScrollRemainder.x += scroll.x * MOONLIGHT_WHEEL_DELTA;
    desktopScrollRemainder.y += scroll.y * MOONLIGHT_WHEEL_DELTA;

    const int horizontal = std::clamp(
        static_cast<int>(std::trunc(desktopScrollRemainder.x)),
        static_cast<int>(std::numeric_limits<short>::min()),
        static_cast<int>(std::numeric_limits<short>::max()));
    const int vertical = std::clamp(
        static_cast<int>(std::trunc(desktopScrollRemainder.y)),
        static_cast<int>(std::numeric_limits<short>::min()),
        static_cast<int>(std::numeric_limits<short>::max()));

    if (horizontal != 0) {
        desktopScrollRemainder.x -= horizontal;
        const int result =
            LiSendHighResHScrollEvent(static_cast<short>(horizontal));
        if (result < 0)
            brls::Logger::warning(
                "Failed to queue horizontal mouse scroll: {}", result);
    }

    if (vertical != 0) {
        desktopScrollRemainder.y -= vertical;
        const int result =
            LiSendHighResScrollEvent(static_cast<short>(vertical));
        if (result < 0)
            brls::Logger::warning(
                "Failed to queue vertical mouse scroll: {}", result);
    }
}

void MoonlightInputManager::sendRelativeMouseMove(brls::Point offset) {
    desktopMouseRemainder.x += offset.x;
    desktopMouseRemainder.y += offset.y;

    short deltaX = static_cast<short>(std::trunc(desktopMouseRemainder.x));
    short deltaY = static_cast<short>(std::trunc(desktopMouseRemainder.y));

    if (deltaX == 0 && deltaY == 0) {
        return;
    }

    desktopMouseRemainder.x -= deltaX;
    desktopMouseRemainder.y -= deltaY;

    LiSendMouseMoveEvent(deltaX, deltaY);
}

void MoonlightInputManager::reloadButtonMappingLayout() {
    std::lock_guard<std::mutex> lock(controllerMutex);
    KeyMappingLayout layout = (*Settings::instance().get_mapping_laouts())
        [Settings::instance().get_current_mapping_layout()];
    for (int i = 0; i < _BUTTON_MAX; i++) {
        if (layout.mapping.count(i) == 1) {
            mappingButtons[i] = (brls::ControllerButton)layout.mapping.at(i);
        } else {
            mappingButtons[i] = (brls::ControllerButton)i;
        }
    }
}

void MoonlightInputManager::updateTouchScreenPanDelta(
    brls::PanGestureStatus panStatus) {
    this->panStatus = panStatus;
}

void MoonlightInputManager::handleRumble(unsigned short controller,
                                         unsigned short lowFreqMotor,
                                         unsigned short highFreqMotor) {
    brls::Logger::debug("Rumble {} {}", lowFreqMotor, highFreqMotor);

    if (controller >= GAMEPADS_MAX) {
        brls::Logger::warning("Ignoring rumble for out-of-range controller {}", controller);
        return;
    }

    float rumbleMultiplier = Settings::instance().get_rumble_force();

    rumbleCache[controller].lowFreqMotor = lowFreqMotor * rumbleMultiplier;
    rumbleCache[controller].highFreqMotor = highFreqMotor * rumbleMultiplier;

    brls::Application::getPlatform()->getInputManager()->sendRumble(
        controller, 
        rumbleCache[controller].lowFreqMotor,
        rumbleCache[controller].highFreqMotor);
}

void MoonlightInputManager::handleRumbleTriggers(uint16_t controllerNumber, 
                                                  uint16_t leftTriggerMotor, 
                                                  uint16_t rightTriggerMotor) {
    brls::Logger::debug("Rumble Trigger {} {}", leftTriggerMotor, rightTriggerMotor);

    if (controllerNumber >= GAMEPADS_MAX) {
        brls::Logger::warning("Ignoring trigger rumble for out-of-range controller {}", controllerNumber);
        return;
    }

    float rumbleMultiplier = Settings::instance().get_rumble_force();

    rumbleCache[controllerNumber].leftTriggerMotor = leftTriggerMotor * rumbleMultiplier;
    rumbleCache[controllerNumber].rightTriggerMotor = rightTriggerMotor * rumbleMultiplier;

    brls::Application::getPlatform()->getInputManager()->sendRumble(
        controllerNumber, 
        rumbleCache[controllerNumber].lowFreqMotor,
        rumbleCache[controllerNumber].highFreqMotor,
        rumbleCache[controllerNumber].leftTriggerMotor,
        rumbleCache[controllerNumber].rightTriggerMotor);
}

void MoonlightInputManager::dropInput() {
#ifdef __SWITCH__
    moonvibe::SwitchMotion::instance().pause(true);
#endif
    // Wait for an in-flight poller send, and keep it from sending until the
    // UI loop clears inputDropped again.
    std::lock_guard<std::mutex> lock(controllerMutex);
    if (inputDropped)
        return;
    inputDropped = true;

    desktopMouseRemainder = {0, 0};
    desktopScrollRemainder = {0, 0};
    pendingHorizontalScroll = 0;
    pendingHorizontalScrollEvents = 0;
    desktopScrollAxis = DesktopScrollAxis::None;
    lastDesktopScrollEvent = {};

    bool res = true;
    // Drop gamepad state
    GamepadState gamepadState;
    auto controllersCount = brls::Application::getPlatform()
                            ->getInputManager()
                            ->getControllersConnectedCount();

    if (controllersCount > GAMEPADS_MAX) {
        brls::Logger::warning("Clamping controller count from {} to {} while dropping input", controllersCount, GAMEPADS_MAX);
        controllersCount = GAMEPADS_MAX;
    }

    for (short i = 0; i < controllersCount; i++) {
        res &= LiSendMultiControllerEvent(
                   i, controllersToMap(), gamepadState.buttonFlags,
                   gamepadState.leftTrigger, gamepadState.rightTrigger,
                   gamepadState.leftStickX, gamepadState.leftStickY,
                   gamepadState.rightStickX, gamepadState.rightStickY) == 0;
    }

    // Drop touchscreen mouse state
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE,BUTTON_MOUSE_LEFT);

    // Drop touchscreen state
    for (auto id: activeTouchIDs) {
        LiSendTouchEvent(LI_TOUCH_EVENT_CANCEL, id.first, 0, 0, 0, 0, 0, LI_ROT_UNKNOWN);
    }
    activeTouchIDs.clear();

    // Drop keyboard state
    for (int i = BRLS_KBD_KEY_SPACE; i < BrlsKeyboardScancode::BRLS_KBD_KEY_LAST; i++)  {
        short vkKey = MoonlightInputManager::glfwKeyToVKKey((BrlsKeyboardScancode)i);
        LiSendKeyboardEvent(vkKey, KEY_ACTION_UP, 0);
    }

    // The host now sees neutral pads; resend whatever is held on resume.
    std::fill(std::begin(lastGamepadStates), std::end(lastGamepadStates), GamepadState{});
    inputDropped = res;
}

GamepadState MoonlightInputManager::getControllerState(int controllerNum,
                                                       bool specialKey,
                                                       bool fromPoller) {
    brls::ControllerState rawController{};
    brls::ControllerState controller{};

    brls::Application::setSwapHalfJoyconStickToDpad(Settings::instance().swap_joycon_stick_to_dpad());
    if (fromPoller)
        padReader->read(&rawController, controllerNum);
    else
        brls::Application::getPlatform()->getInputManager()->updateControllerState(
            &rawController, controllerNum);
    controller = mapController(rawController);

    // Use axis or button if axis is not available (equals 0)
    float lzAxis = controller.axes[LEFT_Z] > 0 ? controller.axes[LEFT_Z] : (controller.buttons[brls::BUTTON_LT] ? 1.f : 0.f);
    float rzAxis = controller.axes[RIGHT_Z] > 0 ? controller.axes[RIGHT_Z] : (controller.buttons[brls::BUTTON_RT] ? 1.f : 0.f);

    // Truncate dead zones
    float leftStickDeadzone = Settings::instance().get_deadzone_stick_left();
    float rightStickDeadzone = Settings::instance().get_deadzone_stick_right();

    float leftXAxis = controller.axes[brls::LEFT_X];
    float leftYAxis = controller.axes[brls::LEFT_Y];
    float rightXAxis = controller.axes[brls::RIGHT_X];
    float rightYAxis = controller.axes[brls::RIGHT_Y];

    // Curve and outer limit are meant for the short Joy-Con sticks; keep a
    // Pro Controller or third-party pad linear unless the user opts out.
    bool smallSticks = true;
#ifdef __SWITCH__
    if (!padReader)
        padReader = std::make_unique<moonvibe::SwitchPadReader>();
    smallSticks = padReader->hasSmallSticks(controllerNum);
#endif
    const bool tune = smallSticks || !Settings::instance().stick_tuning_joycon_only();

    if (controllerNum == 0) {
        // Player 1 only: raw travel before any processing, for the debug view.
        const float leftRaw = std::sqrt(leftXAxis * leftXAxis + leftYAxis * leftYAxis);
        const float rightRaw = std::sqrt(rightXAxis * rightXAxis + rightYAxis * rightYAxis);
        if (leftRaw > stickPeakLeft.load()) stickPeakLeft = leftRaw;
        if (rightRaw > stickPeakRight.load()) stickPeakRight = rightRaw;
        padHasSmallSticks = smallSticks;
    }

    applyStickResponse(leftXAxis, leftYAxis,
        {leftStickDeadzone,
         Settings::instance().get_anti_deadzone_stick_left(),
         tune ? Settings::instance().get_stick_curve_left() : 0.f,
         tune ? Settings::instance().get_stick_outer_limit_left() : 1.f});
    applyStickResponse(rightXAxis, rightYAxis,
        {rightStickDeadzone,
         Settings::instance().get_anti_deadzone_stick_right(),
         tune ? Settings::instance().get_stick_curve_right() : 0.f,
         tune ? Settings::instance().get_stick_outer_limit_right() : 1.f});

    GamepadState gamepadState{
        .buttonFlags = 0,
        .leftTrigger = static_cast<unsigned char>(
            0xFF * (!specialKey ? lzAxis : 0)),
        .rightTrigger = static_cast<unsigned char>(
            0xFF * (!specialKey ? rzAxis : 0)),
        .leftStickX = static_cast<short>(
            0x7FFF * (!specialKey ? leftXAxis : 0)),
        .leftStickY = static_cast<short>(
            -0x7FFF * (!specialKey ? leftYAxis : 0)),
        .rightStickX = static_cast<short>(
            0x7FFF * (!specialKey ? rightXAxis : 0)),
        .rightStickY = static_cast<short>(
            -0x7FFF * (!specialKey ? rightYAxis : 0)),
    };

    brls::ControllerButton a = brls::BUTTON_A;
    brls::ControllerButton b = brls::BUTTON_B;
    brls::ControllerButton x = brls::BUTTON_X;
    brls::ControllerButton y = brls::BUTTON_Y;

#define SET_GAME_PAD_STATE(LIMELIGHT_KEY, GAMEPAD_BUTTON)                      \
    controller.buttons[GAMEPAD_BUTTON]                                         \
        ? (gamepadState.buttonFlags |= LIMELIGHT_KEY)                          \
        : (gamepadState.buttonFlags &= ~LIMELIGHT_KEY);

    SET_GAME_PAD_STATE(UP_FLAG, brls::BUTTON_UP);
    SET_GAME_PAD_STATE(DOWN_FLAG, brls::BUTTON_DOWN);
    SET_GAME_PAD_STATE(LEFT_FLAG, brls::BUTTON_LEFT);
    SET_GAME_PAD_STATE(RIGHT_FLAG, brls::BUTTON_RIGHT);

#ifdef __SWITCH__
    SET_GAME_PAD_STATE(A_FLAG, b);
    SET_GAME_PAD_STATE(B_FLAG, a);
    SET_GAME_PAD_STATE(X_FLAG, y);
    SET_GAME_PAD_STATE(Y_FLAG, x);
#else
    SET_GAME_PAD_STATE(A_FLAG, a);
    SET_GAME_PAD_STATE(B_FLAG, b);
    SET_GAME_PAD_STATE(X_FLAG, x);
    SET_GAME_PAD_STATE(Y_FLAG, y);
#endif

    SET_GAME_PAD_STATE(BACK_FLAG, brls::BUTTON_BACK);
    SET_GAME_PAD_STATE(PLAY_FLAG, brls::BUTTON_START);

    SET_GAME_PAD_STATE(LB_FLAG, brls::BUTTON_LB);
    SET_GAME_PAD_STATE(RB_FLAG, brls::BUTTON_RB);

    SET_GAME_PAD_STATE(LS_CLK_FLAG, brls::BUTTON_LSB);
    SET_GAME_PAD_STATE(RS_CLK_FLAG, brls::BUTTON_RSB);

    auto guideKeys = Settings::instance().guide_key_options().buttons;
    bool guideCombo = !guideKeys.empty();
    for (auto key : guideKeys)
        guideCombo &= controller.buttons[key];

    if (guideCombo ||
        lastGamepadStates[controllerNum].buttonFlags & SPECIAL_FLAG)
        gamepadState.buttonFlags = 0;

    bool guidePressed = guideCombo || controller.buttons[brls::BUTTON_GUIDE];
    guidePressed ? (gamepadState.buttonFlags |= SPECIAL_FLAG)
               : (gamepadState.buttonFlags &= ~SPECIAL_FLAG);

    return gamepadState;
}

void MoonlightInputManager::handleControllers(bool specialKey, bool fromPoller) {
    short controllersCount = fromPoller
        ? padReader->connectedCount()
        : brls::Application::getPlatform()->getInputManager()->getControllersConnectedCount();

    if (controllersCount > GAMEPADS_MAX) {
        brls::Logger::warning("Clamping controller count from {} to {} while handling input", controllersCount, GAMEPADS_MAX);
        controllersCount = GAMEPADS_MAX;
    }

    short mappedControllersCount = controllersToMap(controllersCount);

    for (int i = 0; i < controllersCount; i++) {
        GamepadState gamepadState = getControllerState(i, specialKey, fromPoller);
        uint64_t identity = 1;
        uint8_t type = LI_CTYPE_UNKNOWN;
#ifdef __SWITCH__
        identity = moonvibe::SwitchMotion::instance().identity(i);
        type = LI_CTYPE_NINTENDO;
#endif
        const bool announce = announcedControllerCount != controllersCount ||
                              announcedControllerIdentity[i] != identity;
        if (announce) {
            const int result = LiSendControllerArrivalEvent(i, mappedControllersCount, type, 0,
                LI_CCAP_RUMBLE | LI_CCAP_ACCEL | LI_CCAP_GYRO);
            if (!result) announcedControllerIdentity[i] = identity;
            else Logger::warning("Controller arrival #{} failed {}", i, result);
        }
        // Arrival sends a neutral button packet. Resend held buttons even when
        // their values didn't change, including reconnect and topology changes.
        if (announce || !gamepadState.is_equal(lastGamepadStates[i])) {
            lastGamepadStates[i] = gamepadState;

            if (LiSendMultiControllerEvent(
                    i, mappedControllersCount, gamepadState.buttonFlags,
                    gamepadState.leftTrigger, gamepadState.rightTrigger,
                    gamepadState.leftStickX, gamepadState.leftStickY,
                    gamepadState.rightStickX, gamepadState.rightStickY) != 0)
                brls::Logger::info("StreamingView: error sending input data");
        }
    }
    if (controllersCount < announcedControllerCount) {
        for (int i = controllersCount; i < announcedControllerCount; ++i) {
            LiSendMultiControllerEvent(i, mappedControllersCount, 0, 0, 0, 0, 0, 0, 0);
            lastGamepadStates[i] = {};
            announcedControllerIdentity[i] = 0;
        }
    }
    announcedControllerCount = controllersCount;
}

void MoonlightInputManager::handleInput(bool ignoreTouch) {
    if (!inputEnabled) return;
#ifdef __SWITCH__
    moonvibe::SwitchMotion::instance().pause(false);
#endif
    static brls::ControllerState rawController;
    static brls::ControllerState controller;
    static brls::RawMouseState mouse;
    std::unique_lock<std::mutex> controllerLock(controllerMutex);
    inputDropped = false;

    brls::Application::getPlatform()
            ->getInputManager()
            ->updateUnifiedControllerState(&rawController);
    brls::Application::getPlatform()->getInputManager()->updateMouseStates(
            &mouse);
    controller = mapController(rawController);

    std::vector<brls::RawTouchState> states;
    brls::Application::getPlatform()->getInputManager()->updateTouchStates(&states);

    //Do not use gamepad for mouse controll assist if touchscreen mode enabled
    bool specialKey = !ignoreTouch && !Settings::instance().touchscreen_mouse_mode() && states.size() == 1;

    if (controllerThreadRunning) {
        // The poller thread sends gamepad state; renew its lease.
        pollSpecialKey = specialKey;
        lastMainPoll = std::chrono::steady_clock::now().time_since_epoch().count();
    } else {
        handleControllers(specialKey, false);
    }
    controllerLock.unlock();

    float stickScrolling = 0;
    if (specialKey) {
        stickScrolling =
                applyStickScrollDeadzone(controller.axes[brls::LEFT_Y],
                                         Settings::instance().get_deadzone_stick_left()) +
                applyStickScrollDeadzone(controller.axes[brls::RIGHT_Y],
                                         Settings::instance().get_deadzone_stick_right());
    }

    static MouseStateS lastMouseState;

    MouseStateS mouseState;
    if (!Settings::instance().touchscreen_mouse_mode()) {
        mouseState = {
                .scroll_y = stickScrolling,
                .l_pressed = (specialKey && controller.buttons[brls::BUTTON_RT]) || mouse.leftButton,
                .m_pressed = mouse.middleButton,
                .r_pressed = (specialKey && controller.buttons[brls::BUTTON_LT]) || mouse.rightButton
        };
    } else {
        mouseState = {
                .scroll_y = 0,
                .l_pressed = mouse.leftButton,
                .m_pressed = mouse.middleButton,
                .r_pressed = mouse.rightButton
        };
    }

    if (Settings::instance().swap_mouse_scroll())
        mouseState.scroll_y *= -1;

    if (mouseState.l_pressed != lastMouseState.l_pressed) {
        lastMouseState.l_pressed = mouseState.l_pressed;
        auto lb = Settings::instance().swap_mouse_keys() ? BUTTON_MOUSE_RIGHT
                                                         : BUTTON_MOUSE_LEFT;
        LiSendMouseButtonEvent(mouseState.l_pressed ? BUTTON_ACTION_PRESS
                                                    : BUTTON_ACTION_RELEASE,
                               lb);
        if (!mouseState.l_pressed)
            Logger::debug("Release key lmb");
    }

    if (mouseState.m_pressed != lastMouseState.m_pressed) {
        lastMouseState.m_pressed = mouseState.m_pressed;
        LiSendMouseButtonEvent(mouseState.m_pressed ? BUTTON_ACTION_PRESS
                                                    : BUTTON_ACTION_RELEASE,
                               BUTTON_MOUSE_MIDDLE);
    }

    if (mouseState.r_pressed != lastMouseState.r_pressed) {
        lastMouseState.r_pressed = mouseState.r_pressed;
        auto rb = Settings::instance().swap_mouse_keys() ? BUTTON_MOUSE_LEFT
                                                         : BUTTON_MOUSE_RIGHT;
        LiSendMouseButtonEvent(mouseState.r_pressed ? BUTTON_ACTION_PRESS
                                                    : BUTTON_ACTION_RELEASE,
                               rb);
    }

    std::chrono::high_resolution_clock::time_point timeNow =
            std::chrono::high_resolution_clock::now();
    static std::chrono::high_resolution_clock::time_point timeStamp = timeNow;

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            timeNow - timeStamp)
            .count();
    if (mouseState.scroll_y != 0 &&
        (float) duration > 550 - std::fabs(mouseState.scroll_y) * 500) {
        timeStamp = timeNow;
        brls::Logger::info("Scroll sended: {}", mouseState.scroll_y);
        lastMouseState.scroll_y = mouseState.scroll_y;
        LiSendScrollEvent(mouseState.scroll_y > 0 ? 1 : -1);
    }

    if (!Settings::instance().touchscreen_mouse_mode()) {
        // Do not process touch events, useful if onscreen keyboard is presented
        if (ignoreTouch) { return; }

        if (panStatus.has_value()) {
            float multiplier =
                    Settings::instance().get_mouse_speed_multiplier() / 100.f * 1.5f +
                    0.5f;
            LiSendMouseMoveEvent(-panStatus->delta.x * multiplier,
                                 -panStatus->delta.y * multiplier);
            panStatus.reset();
        }
    } else {
        uint8_t eventType;

        auto touches = brls::Application::currentTouchState;
        for (int i = 0; i < touches.size(); i++) {
            auto touch = touches[i];
            if (touch.view && touch.view->hasParent() && dynamic_cast<StreamingView*>(touch.view->getParent()) == nullptr) return;

            switch (touch.phase) {
                case TouchPhase::START:
                    eventType = LI_TOUCH_EVENT_DOWN;
                    break;
                case TouchPhase::STAY:
                    eventType = LI_TOUCH_EVENT_MOVE;
                    break;
                case TouchPhase::END:
                    eventType = LI_TOUCH_EVENT_UP;
                    break;
                case TouchPhase::NONE:
                    eventType = LI_TOUCH_EVENT_CANCEL;
                    break;
            }

            if (touch.phase != TouchPhase::NONE) {
                activeTouchIDs.insert(std::pair(touch.fingerId, true));
            } else {
                activeTouchIDs.erase(touch.fingerId);
            }

            if (LiSendTouchEvent(eventType, touch.fingerId, touch.position.x / (float) Application::contentWidth,
                                 touch.position.y / (float) Application::contentHeight, 0, 0, 0, LI_ROT_UNKNOWN) ==
                LI_ERR_UNSUPPORTED && i == 0) {
                // Fallback to move cursor and click if touch unsupported
                if (touch.phase != TouchPhase::NONE) {
                    LiSendMousePositionEvent(touch.position.x, touch.position.y, Application::contentWidth,
                                             Application::contentHeight);
                }
                if (touch.phase == TouchPhase::START) {
                    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_MOUSE_LEFT);
                }
                if (touch.phase == TouchPhase::END) {
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MOUSE_LEFT);
                }
            }
        }
    }
}

short MoonlightInputManager::controllersToMap(int connectedCount) {
    const int count = std::clamp<int>(connectedCount, 0, GAMEPADS_MAX);
    return static_cast<short>((1U << count) - 1U);
}

short MoonlightInputManager::controllersToMap() {
    return controllersToMap(brls::Application::getPlatform()
                ->getInputManager()
                ->getControllersConnectedCount());
}

void MoonlightInputManager::startControllerPoller() {
#ifdef __SWITCH__
    if (!Settings::instance().fast_controller_polling() || controllerThreadRunning)
        return;
    if (!padReader)
        padReader = std::make_unique<moonvibe::SwitchPadReader>();
    lastMainPoll = 0;
    controllerThreadRunning = true;
    controllerThread = std::thread(&MoonlightInputManager::controllerPollerLoop, this);
    brls::Logger::info("Input: controller poller started");
#endif
}

void MoonlightInputManager::stopControllerPoller() {
    if (!controllerThreadRunning && !controllerThread.joinable())
        return;
    controllerThreadRunning = false;
    if (controllerThread.joinable())
        controllerThread.join();
    brls::Logger::info("Input: controller poller stopped");
}

void MoonlightInputManager::controllerPollerLoop() {
#ifdef __SWITCH__
    // Same priority as the UI thread: the work is tiny but should not wait
    // behind decoding when it wakes.
    svcSetThreadPriority(CUR_THREAD_HANDLE, 0x20);

    // HID updates Npads every few milliseconds; poll faster than the 60 Hz UI
    // loop so a stick change reaches the host without waiting for a frame.
    constexpr auto pollPeriod = std::chrono::microseconds(4000);
    // The UI loop renews this lease while streaming input is allowed (not in
    // the overlay, the HOME menu or the post-focus input lock).
    constexpr int64_t leaseNs = 100'000'000;

    while (controllerThreadRunning) {
        {
            std::lock_guard<std::mutex> lock(controllerMutex);
            const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
            if (inputEnabled && !inputDropped && lastMainPoll != 0 &&
                now - lastMainPoll < leaseNs) {
                handleControllers(pollSpecialKey, true);
            }
        }
        svcSleepThread(std::chrono::duration_cast<std::chrono::nanoseconds>(pollPeriod).count());
    }
#endif
}

brls::ControllerState
MoonlightInputManager::mapController(brls::ControllerState controller) {
    brls::ControllerState result{};

    std::fill(result.buttons, result.buttons + sizeof(result.buttons), false);

    for (int i = 0; i < _AXES_MAX; i++)
        result.axes[i] = controller.axes[i];

    for (int i = 0; i < _BUTTON_MAX; i++) {
        result.buttons[mappingButtons[i]] |= controller.buttons[i];
    }

    return result;
}

void MoonlightInputManager::leftMouseClick() {
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_MOUSE_LEFT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MOUSE_LEFT);
}

void MoonlightInputManager::rightMouseClick() {
    LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_MOUSE_RIGHT);
    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MOUSE_RIGHT);
}

short MoonlightInputManager::glfwKeyToVKKey(BrlsKeyboardScancode key) {
    if (BRLS_KBD_KEY_F1 <= key && key <= BRLS_KBD_KEY_F12)
        return key - BRLS_KBD_KEY_F1 + 0x70;

    if (BRLS_KBD_KEY_KP_0 <= key && key <= BRLS_KBD_KEY_KP_9)
        return key - BRLS_KBD_KEY_KP_0 + 0x60;

    switch (key) {
    case BRLS_KBD_KEY_BACKSPACE:
        return 0x08;
    case BRLS_KBD_KEY_SEMICOLON:
        return 0xBA;
    case BRLS_KBD_KEY_EQUAL:
        return 0xBB;
    case BRLS_KBD_KEY_COMMA:
        return 0xBC;
    case BRLS_KBD_KEY_MINUS:
        return 0xBD;
    case BRLS_KBD_KEY_PERIOD:
        return 0xBE;
    case BRLS_KBD_KEY_WORLD_1: // OEM_102 (> <)
        return 0xE2;
    case BRLS_KBD_KEY_SLASH:
        return 0xBF;
    case BRLS_KBD_KEY_GRAVE_ACCENT:
        return 0xC0;
    case BRLS_KBD_KEY_LEFT_BRACKET:
        return 0xDB;
    case BRLS_KBD_KEY_BACKSLASH:
        return 0xDC;
    case BRLS_KBD_KEY_RIGHT_BRACKET:
        return 0xDD;
    case BRLS_KBD_KEY_APOSTROPHE:
        return 0xDE;
    // case FIND_PROPER_NAME: // OEM_8 (§ !)
    //     return 0xDF;
    case BRLS_KBD_KEY_TAB:
        return 0x09;
    case BRLS_KBD_KEY_CAPS_LOCK:
        return 0x14;
    case BRLS_KBD_KEY_LEFT_SHIFT:
        return 0xA0;
    case BRLS_KBD_KEY_RIGHT_SHIFT:
        return 0xA1;
    case BRLS_KBD_KEY_LEFT_CONTROL:
        return 0xA2;
    case BRLS_KBD_KEY_RIGHT_CONTROL:
        return 0xA3;
    case BRLS_KBD_KEY_LEFT_ALT:
        return 0xA4;
    case BRLS_KBD_KEY_RIGHT_ALT:
        return 0xA5;
    case BRLS_KBD_KEY_ENTER:
        return 0x0D;
    case BRLS_KBD_KEY_LEFT_SUPER:
        return 0x5B;
    case BRLS_KBD_KEY_RIGHT_SUPER:
        return 0x5C;
    case BRLS_KBD_KEY_ESCAPE:
        return 0x1B;
    case BRLS_KBD_KEY_KP_ADD:
        return 0x6B;
    case BRLS_KBD_KEY_KP_DECIMAL:
        return 0x6E;
    case BRLS_KBD_KEY_KP_DIVIDE:
        return 0x6F;
    case BRLS_KBD_KEY_KP_MULTIPLY:
        return 0x6A;
    case BRLS_KBD_KEY_KP_ENTER:
        return 0x0D;
    case BRLS_KBD_KEY_NUM_LOCK:
        return 0x90;
    case BRLS_KBD_KEY_SCROLL_LOCK:
        return 0x91;
    case BRLS_KBD_KEY_PAGE_UP:
        return 0x21;
    case BRLS_KBD_KEY_PAGE_DOWN:
        return 0x22;
    case BRLS_KBD_KEY_END:
        return 0x23;
    case BRLS_KBD_KEY_HOME:
        return 0x24;
    case BRLS_KBD_KEY_LEFT:
        return 0x25;
    case BRLS_KBD_KEY_UP:
        return 0x26;
    case BRLS_KBD_KEY_RIGHT:
        return 0x27;
    case BRLS_KBD_KEY_DOWN:
        return 0x28;
    case BRLS_KBD_KEY_PRINT_SCREEN:
        return 0x2C;
    case BRLS_KBD_KEY_INSERT:
        return 0x2D;
    case BRLS_KBD_KEY_DELETE:
        return 0x2E;

    default:
        return key;
    }
}
