#pragma once

#include "MotionPolicy.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace moonvibe {
// Owns the Switch motion sensors for the lifetime of a stream. All HID reads
// and start/stop calls run on one worker, independent of the rendering loop.
class SwitchMotion {
public:
    static SwitchMotion& instance();
    ~SwitchMotion();
    void prepareSession(bool enabled, bool preferRight, bool vhfProCompatibility = false);
    void start();
    void stop();
    void pause(bool paused);
    void setEnabled(bool enabled) { enabled_.store(enabled); }
    void setPreferRight(bool right) { preferRight_.store(right); }
    void setVhfProCompatibility(bool enabled) { vhfProCompatibility_.store(enabled); }
    void request(uint16_t slot, uint8_t type, uint16_t rateHz);
    bool requested(uint8_t slot, uint8_t type) const;
    MotionSource source(unsigned slot = 0) const;
    bool active() const { return running_.load(); }
    uint64_t identity(unsigned slot) const;

    // Multiplies gyro (not accel) before sending; 1.0 = unchanged.
    void setGyroGain(float gain) { gyroGain_.store(clampGyroGain(gain)); }
    float gyroGain() const { return gyroGain_.load(); }
    // HidGyroscopeZeroDriftMode: 0 Loose, 1 Standard (system reset value), 2 Tight.
    // Applied to running sensors on the next poll.
    void setZeroDriftMode(int mode) { zeroDriftMode_.store(std::clamp(mode, 0, 2)); }
    int zeroDriftMode() const { return zeroDriftMode_.load(); }
    // Wii-remote style aiming for a detached Joy-Con pair only (see pointerGyro).
    void setPointerMode(bool enabled) { pointerMode_.store(enabled); }
    bool pointerMode() const { return pointerMode_.load(); }

    // Player 1 measurement for the debug view: gyro angle integrated from the
    // sent (gain-applied) rate, in degrees on SDL axes (x pitch, y yaw, z roll),
    // and the peak rate in deg/s since the last reset.
    float measuredAngle(unsigned axis) const { return axis < 3 ? measuredAngle_[axis].load() : 0.f; }
    float measuredPeakRate() const { return measuredPeak_.load(); }
    void resetMeasurement() { measureReset_.store(true); }

private:
    SwitchMotion() = default;
    void run();
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{true};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> preferRight_{true};
    std::atomic<bool> vhfProCompatibility_{false};
    std::atomic<float> gyroGain_{1.f};
    std::atomic<int> zeroDriftMode_{1};
    std::atomic<bool> pointerMode_{false};
    std::array<std::atomic<float>, 3> measuredAngle_{};
    std::atomic<float> measuredPeak_{0.f};
    std::atomic<bool> measureReset_{true};
    // Borealis configures five Npad slots (including handheld when present).
    static constexpr unsigned slots = 5;
    std::array<std::array<std::atomic<uint16_t>, 2>, slots> rates_{};
    std::array<std::atomic<MotionSource>, slots> sources_{};
    std::array<std::atomic<uint64_t>, slots> identities_{};
    std::mutex lifecycle_;
    std::thread worker_;
};
} // namespace moonvibe
