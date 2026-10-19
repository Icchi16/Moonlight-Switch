//
//  InputManager.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#pragma once

#include "Singleton.hpp"
#include "keyboard_view.hpp"
#include "switch/SwitchPadReader.hpp"
#include <borealis.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

// Moonlight ready gamepad
struct GamepadState {
    short buttonFlags = 0;
    unsigned char leftTrigger = 0;
    unsigned char rightTrigger = 0;
    short leftStickX = 0;
    short leftStickY = 0;
    short rightStickX = 0;
    short rightStickY = 0;

    bool is_equal(GamepadState other) {
        return buttonFlags == other.buttonFlags &&
               leftTrigger == other.leftTrigger &&
               rightTrigger == other.rightTrigger &&
               leftStickX == other.leftStickX &&
               leftStickY == other.leftStickY &&
               rightStickX == other.rightStickX &&
               rightStickY == other.rightStickY;
    }
};

struct MouseStateS {
    float scroll_y = 0;
    bool l_pressed = 0;
    bool m_pressed = 0;
    bool r_pressed = 0;
};

struct RumbleValues {
    unsigned short lowFreqMotor;
    unsigned short highFreqMotor;
    uint16_t leftTriggerMotor;
    uint16_t rightTriggerMotor;
};

class MoonlightInputManager : public Singleton<MoonlightInputManager> {
  public:
    MoonlightInputManager();
    void dropInput();
    void handleInput(bool ignoreTouch = false);
    void handleRumble(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor);
    void handleRumbleTriggers(unsigned short controller, unsigned short lowFreqMotor, unsigned short highFreqMotor);
    void updateTouchScreenPanDelta(brls::PanGestureStatus panStatus);
    void reloadButtonMappingLayout();
    void setInputEnabled(bool enabled);
    void resetSession();
    // Sends gamepad state from a ~250 Hz worker instead of once per UI frame.
    void startControllerPoller();
    void stopControllerPoller();

    // Largest raw stick travel (0..1, before dead zone/curve) seen on player 1
    // since the last reset; shown in the debug view to calibrate the outer limit.
    [[nodiscard]] float peakStickLeft() const { return stickPeakLeft.load(); }
    [[nodiscard]] float peakStickRight() const { return stickPeakRight.load(); }
    [[nodiscard]] bool player1HasSmallSticks() const { return padHasSmallSticks.load(); }
    void resetStickPeaks() { stickPeakLeft = 0.f; stickPeakRight = 0.f; }
    static void leftMouseClick();
    static void rightMouseClick();

  private:
    enum class DesktopScrollAxis {
        None,
        Horizontal,
        Vertical,
    };

    RumbleValues rumbleCache[GAMEPADS_MAX];
    GamepadState lastGamepadStates[GAMEPADS_MAX];
    brls::ControllerButton mappingButtons[brls::_BUTTON_MAX];
    std::optional<brls::PanGestureStatus> panStatus;
    std::map<uint32_t, bool> activeTouchIDs;
    brls::Point desktopMouseRemainder = {0, 0};
    brls::Point desktopScrollRemainder = {0, 0};
    float pendingHorizontalScroll = 0;
    unsigned pendingHorizontalScrollEvents = 0;
    DesktopScrollAxis desktopScrollAxis = DesktopScrollAxis::None;
    std::chrono::steady_clock::time_point lastDesktopScrollEvent;
    std::atomic<bool> inputDropped = false;
    std::atomic<bool> inputEnabled = true;
    int announcedControllerCount = -1;
    uint64_t announcedControllerIdentity[GAMEPADS_MAX]{};

    // Guards gamepad send state (lastGamepadStates, announced*, mapping)
    // shared between the UI loop and the controller poller.
    std::mutex controllerMutex;
    std::thread controllerThread;
    std::atomic<bool> controllerThreadRunning = false;
    std::atomic<bool> pollSpecialKey = false;
    // steady_clock ticks of the last UI-loop handleInput(); 0 = no lease.
    std::atomic<int64_t> lastMainPoll = 0;
    std::unique_ptr<moonvibe::SwitchPadReader> padReader;
    std::atomic<float> stickPeakLeft{0.f};
    std::atomic<float> stickPeakRight{0.f};
    std::atomic<bool> padHasSmallSticks{true};
    void controllerPollerLoop();

    brls::ControllerState mapController(brls::ControllerState controller);
    static short glfwKeyToVKKey(brls::BrlsKeyboardScancode key);
    void sendRelativeMouseMove(brls::Point offset);
    void handleDesktopMouseScroll(brls::Point scroll);
    void sendDesktopMouseScroll(brls::Point scroll);

    GamepadState getControllerState(int controllerNum, bool specialKey, bool fromPoller);
    void handleControllers(bool specialKey, bool fromPoller);

    static short controllersToMap();
    static short controllersToMap(int connectedCount);
};
