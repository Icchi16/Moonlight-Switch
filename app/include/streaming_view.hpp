//
//  streaming_view.hpp
//  Moonlight
//
//  Created by Даниил Виноградов on 27.05.2021.
//

#pragma once

#include "gestures/fingers_gesture_recognizer.hpp"
#include "keyboard_view.hpp"
#include "loading_overlay.hpp"
#include <Settings.hpp>
#include <borealis.hpp>
#include <optional>
#include "GameStreamClient.hpp"
#include "MoonlightSession.hpp"
#include "two_finger_scroll_recognizer.hpp"

class StreamingView : public brls::Box {
  public:
    StreamingView(const Host& host, const AppInfo& app);
    ~StreamingView();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;
    void onLayout() override;

    void terminate(bool terminateApp);

  private:
    /**
     * Handles applet focus changes (HOME menu, console sleep).
     *
     * By default the stream stays connected, like XITRIX/Moonlight-Switch:
     * held input is released and no input is sent until focus returns. The
     * app runs with AppletFocusHandlingMode_NoSuspend, so decoding continues
     * in the background. If sleep drops the network, the connection listener
     * ends the stream as for any other disconnect.
     *
     * The "end stream on HOME" setting restores the frame-rate-sync behaviour
     * of ending the session on focus loss, matching Moonlight on Android
     * (Game.onStop calls stopConnection and finish). That avoids carrying
     * decoder and GPU state across a suspend (issue #306) at the cost of a
     * reconnect.
     */
    void onWindowFocusChanged(bool focused);

    /** False while the HOME menu or sleep owns applet focus. */
    bool appletFocused = true;

    brls::Event<bool>::Subscription windowFocusSubscription;

    /**
     * Set by onWindowFocusChanged, acted on in draw().
     *
     * The focus callback runs inside Event<bool>::fire, which iterates its
     * callback list by value:
     *
     *     for (Callback cb : this->callbacks)
     *         cb(args...);
     *
     * terminate() calls dismiss(), which pops this view and runs its
     * destructor, and the destructor unsubscribes. Doing that from inside the
     * callback mutates the list fire() is walking, and the iteration then
     * runs off a destroyed object.
     *
     * So the callback only records the intent and draw() performs it, where
     * the view is known to be alive and nothing is iterating the event.
     */
    bool pendingSuspendTerminate = false;

  public:

    bool draw_stats = false;

    Host getHost() { return host; }

    AppInfo getApp() { return app; }

  private:
    Host host;
    AppInfo app;
    MoonlightSession* session = nullptr;
    LoadingOverlay* loader = nullptr;
    Box* keyboardHolder = nullptr;
    KeyboardView* keyboard = nullptr;
    bool blocked = false;
    bool terminated = false;
    bool tempInputLock = false;
    brls::Event<brls::KeyState>::Subscription keysSubscription;
    int touchScrollCounter = 0;
    size_t bottombarDelayTask = -1;
    bool m_use_hdr = false;
    TwoFingerScrollGestureRecognizer* scrollTouchRecognizer = nullptr;

    void handleInput();
    void handleOverlayCombo();
    void handleMouseInputCombo();
    void addKeyboard();
    void removeKeyboard();
};
