#include "../../app/src/streaming/switch/SwitchMotion.hpp"
#include "Limelight.h"
#include <switch.h>
#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <vector>

using namespace moonvibe;
using namespace std::chrono_literals;
struct Packet { unsigned slot, type; float x, y, z; };
std::mutex packetMutex;
std::vector<Packet> packets;
std::atomic<unsigned> style{HidNpadStyleTag_NpadJoyDual};
std::atomic<unsigned> attributes{21};
std::atomic<bool> handheld{false}, frozen{false}, invalid{false};
std::atomic<int> starts{0}, stops{0}, currentHandle{0};
std::atomic<uint64_t> frozenSample{0};
std::atomic<bool> customGyro{false};
std::atomic<float> gyroX{0}, gyroY{0}, gyroZ{0};

void padInitialize(PadState* pad, HidNpadIdType id) { pad->id = id; }
void padUpdate(PadState* pad) {
    pad->active_handheld = pad->id == HidNpadIdType_Handheld && handheld;
    pad->style_set = pad->active_handheld ? static_cast<unsigned>(HidNpadStyleTag_NpadHandheld) :
        pad->id == HidNpadIdType_No1 ? style.load() : 0;
    pad->attributes = attributes;
}
u32 padGetStyleSet(PadState* pad) { return pad->style_set; }
u64 padGetAttributes(PadState* pad) { return pad->attributes; }
Result hidGetSixAxisSensorHandles(HidSixAxisSensorHandle* handles, int count,
                                  HidNpadIdType id, HidNpadStyleTag tag) {
    for (int i = 0; i < count; ++i) handles[i].value = 100 * id + 10 * tag + i;
    return 0;
}
Result hidStartSixAxisSensor(HidSixAxisSensorHandle h) { ++starts; currentHandle = h.value; return 0; }
Result hidStopSixAxisSensor(HidSixAxisSensorHandle) { ++stops; return 0; }
std::atomic<int> lastDriftMode{-1};
Result hidSetGyroscopeZeroDriftMode(HidSixAxisSensorHandle, HidGyroscopeZeroDriftMode mode) {
    lastDriftMode = mode;
    return 0;
}
int hidGetSixAxisSensorStates(HidSixAxisSensorHandle h, HidSixAxisSensorState* out, int) {
    uint64_t sample = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 5000;
    if (frozen) sample = frozenSample;
    else frozenSample = sample;
    out->sampling_number = sample;
    out->attributes = invalid ? 0 : HidSixAxisSensorAttribute_IsConnected;
    out->acceleration = {0, 0, -1};
    out->angular_velocity = {h.value == 41 ? 0.2f : 0.1f, 0, 0};
    if (customGyro) out->angular_velocity = {gyroX.load(), gyroY.load(), gyroZ.load()};
    return 1;
}
int LiSendControllerMotionEvent(uint8_t slot, uint8_t type, float x, float y, float z) {
    std::lock_guard<std::mutex> lock(packetMutex);
    packets.push_back({slot, type, x, y, z});
    return 0;
}
extern "C" bool MoonvibeHostRequestedMotion(uint8_t slot, uint8_t type);
template<typename Predicate> void waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 1500ms;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    assert(predicate());
}
bool lastGyroIs(float x) {
    std::lock_guard<std::mutex> lock(packetMutex);
    for (auto it = packets.rbegin(); it != packets.rend(); ++it)
        if (it->slot == 0 && it->type == LI_MOTION_TYPE_GYRO)
            return std::abs(it->x - x) < 0.01f;
    return false;
}
bool lastMotionMatches(unsigned slot, unsigned type, float x, float y, float z) {
    std::lock_guard<std::mutex> lock(packetMutex);
    for (auto it = packets.rbegin(); it != packets.rend(); ++it)
        if (it->slot == slot && it->type == type)
            return std::abs(it->x - x) < 0.01f && std::abs(it->y - y) < 0.01f && std::abs(it->z - z) < 0.01f;
    return false;
}
bool lastGyroMatches(unsigned slot, float x, float y, float z) {
    return lastMotionMatches(slot, LI_MOTION_TYPE_GYRO, x, y, z);
}
size_t packetCount() { std::lock_guard<std::mutex> lock(packetMutex); return packets.size(); }

int main() {
    assert(selectMotionSource(PadKind::JoyDual, true, true, true) == MotionSource::JoyRight);
    assert(selectMotionSource(PadKind::JoyDual, true, true, false) == MotionSource::JoyLeft);
    assert(selectMotionSource(PadKind::JoyDual, true, false, true) == MotionSource::JoyLeft);
    assert(selectMotionSource(PadKind::JoyDual, false, true, false) == MotionSource::JoyRight);
    assert(selectMotionSource(PadKind::JoyDual, false, false, false) == MotionSource::None);
    assert(selectMotionSource(PadKind::Handheld, true, true, false) == MotionSource::Console);
    assert(selectMotionSource(PadKind::Pro, true, true, true) == MotionSource::Pro);
    assert(selectMotionSource(PadKind::JoyLeft, false, false, true) == MotionSource::JoyLeft);
    const auto accel = normalizeAcceleration(0, 0, -1, PadKind::Pro);
    assert(std::abs(accel[1] - 9.80665f) < 0.001f);
    assert(normalizeGyro(1, 0, 0, PadKind::Pro)[0] == 360);
    assert(normalizeGyro(1, 0, 0, PadKind::JoyLeft)[2] == -360);
    assert(normalizeGyro(1, 0, 0, PadKind::JoyRight)[2] == 360);
    // Independent consumer contract: SDL's Pro parser reads {-rawY, rawZ, -rawX}.
    const auto proReader = [](std::array<float, 3> raw) {
        return std::array<float, 3>{-raw[1], raw[2], -raw[0]};
    };
    for (const auto kind : {PadKind::Handheld, PadKind::Pro, PadKind::JoyDual,
                            PadKind::JoyLeft, PadKind::JoyRight}) {
        for (unsigned axis = 0; axis < 3; ++axis) for (float sign : {-1.0f, 1.0f}) {
            std::array<float, 3> raw{};
            raw[axis] = sign;
            for (const auto sdl : {normalizeGyro(raw[0], raw[1], raw[2], kind),
                                  normalizeAcceleration(raw[0], raw[1], raw[2], kind)}) {
                assert(encodeMotionAxes(sdl, false) == sdl);
                assert(proReader(encodeMotionAxes(sdl, true)) == sdl);
            }
        }
    }
    // Pointer mode: a turn about the world vertical stays a full yaw however far
    // the Joy-Con is tipped up; pitch passes through and roll is dropped.
    const auto near = [](std::array<float, 3> a, std::array<float, 3> b) {
        return std::abs(a[0] - b[0]) < 0.05f && std::abs(a[1] - b[1]) < 0.05f &&
               std::abs(a[2] - b[2]) < 0.05f;
    };
    for (const float degrees : {0.0f, 30.0f, 45.0f, 80.0f}) {
        const float t = degrees * 3.14159265f / 180.0f;
        const std::array<float, 3> up{0, 9.80665f * std::cos(t), 9.80665f * std::sin(t)};
        const std::array<float, 3> turn{0, 50.0f * std::cos(t), 50.0f * std::sin(t)};
        assert(near(pointerGyro(turn, up), {0, 50.0f, 0}));
        assert(near(pointerGyro({-turn[0], -turn[1], -turn[2]}, up), {0, -50.0f, 0}));
    }
    assert(near(pointerGyro({20, 0, 0}, {0, 9.8f, 0}), {20, 0, 0}));  // pitch kept
    assert(near(pointerGyro({0, 0, 40}, {0, 9.8f, 0}), {0, 0, 0}));   // roll dropped
    assert(near(pointerGyro({1, 2, 3}, {0, 0, 0}), {1, 2, 0}));       // no gravity: local
    assert(motionIntervalUs(0) == 0 && motionIntervalUs(100) == 10000);
    assert(motionIntervalUs(60) == 16666 && motionIntervalUs(1000) == 5000);

    auto& motion = SwitchMotion::instance();
    motion.prepareSession(true, true);
    motion.start();
    waitFor([&] { return motion.source() == MotionSource::JoyRight; });
    assert(starts == 0); // No sensors before an actual host request.
    motion.request(0, LI_MOTION_TYPE_ACCEL, 100);
    motion.request(0, LI_MOTION_TYPE_GYRO, 100);
    assert(MoonvibeHostRequestedMotion(0, LI_MOTION_TYPE_GYRO));
    assert(!MoonvibeHostRequestedMotion(5, LI_MOTION_TYPE_GYRO));
    assert(!MoonvibeHostRequestedMotion(0, 0));
    motion.pause(false);
    waitFor([] { return lastGyroIs(72); });
    motion.setPreferRight(false);
    waitFor([] { return currentHandle == 40 && lastGyroIs(36); });
    attributes = HidNpadAttribute_IsConnected | HidNpadAttribute_IsRightConnected;
    waitFor([] { return currentHandle == 41 && lastGyroIs(72); });
    const auto dualIdentity = motion.identity(0);
    style = HidNpadStyleTag_NpadFullKey;
    waitFor([&] { return motion.source() == MotionSource::Pro && lastGyroIs(36); });
    assert(motion.identity(0) != dualIdentity);
    style = HidNpadStyleTag_NpadJoyLeft;
    motion.setPreferRight(true);
    waitFor([&] { return motion.source() == MotionSource::JoyLeft && lastGyroMatches(0, 0, 0, -36); });
    style = HidNpadStyleTag_NpadJoyRight;
    motion.setPreferRight(false);
    waitFor([&] { return motion.source() == MotionSource::JoyRight && lastGyroMatches(0, 0, 0, 36); });
    style = HidNpadStyleTag_NpadFullKey;
    waitFor([&] { return motion.source() == MotionSource::Pro && lastGyroIs(36); });
    frozen = true;
    waitFor([] { return lastGyroIs(0); }); // Stale samples can't keep rotating.
    frozen = false;
    waitFor([] { return lastGyroIs(36); });
    invalid = true;
    waitFor([] { return lastGyroIs(0); });
    invalid = false;
    waitFor([] { return lastGyroIs(36); });
    const int previousStops = stops;
    motion.pause(true);
    waitFor([&] { return stops > previousStops; });
    const auto pausedCount = packetCount();
    std::this_thread::sleep_for(40ms);
    assert(packetCount() == pausedCount);
    motion.pause(false);
    waitFor([] { return lastGyroIs(36); });
    motion.request(0, LI_MOTION_TYPE_ACCEL, 0);
    motion.request(0, LI_MOTION_TYPE_GYRO, 0);
    assert(!MoonvibeHostRequestedMotion(0, LI_MOTION_TYPE_GYRO));
    std::this_thread::sleep_for(20ms);
    const auto stoppedCount = packetCount();
    std::this_thread::sleep_for(40ms);
    assert(packetCount() == stoppedCount);
    motion.stop();
    motion.prepareSession(true, true);
    handheld = true;
    style = 0;
    motion.start();
    waitFor([&] { return motion.source() == MotionSource::Console; });
    assert(!motion.requested(0, LI_MOTION_TYPE_GYRO)); // Reconnect resets rates.
    motion.request(0, LI_MOTION_TYPE_GYRO, 100);
    motion.pause(false);
    waitFor([] { return currentHandle == 3220 && lastGyroIs(36); });
    style = HidNpadStyleTag_NpadFullKey;
    waitFor([&] { return motion.source(0) == MotionSource::Console && motion.source(1) == MotionSource::Pro; });
    motion.request(1, LI_MOTION_TYPE_GYRO, 100);
    waitFor([] { return lastGyroMatches(1, 36, 0, 0); });
    assert(motion.source(0) == MotionSource::Console && motion.identity(0) != motion.identity(1));
    motion.stop();
    // Test the real worker/send boundary for every source, including live toggles.
    for (const auto tag : {HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadFullKey,
                          HidNpadStyleTag_NpadJoyDual, HidNpadStyleTag_NpadJoyLeft,
                          HidNpadStyleTag_NpadJoyRight}) {
        handheld = tag == HidNpadStyleTag_NpadHandheld;
        style = handheld ? 0 : static_cast<unsigned>(tag);
        motion.prepareSession(true, true, true);
        motion.start();
        motion.request(0, LI_MOTION_TYPE_GYRO, 100);
        motion.request(0, LI_MOTION_TYPE_ACCEL, 100);
        motion.pause(false);
        const float speed = tag == HidNpadStyleTag_NpadJoyDual ? 72.0f : 36.0f;
        float x = 0, y = -speed;
        if (tag == HidNpadStyleTag_NpadJoyLeft) { x = speed; y = 0; }
        if (tag == HidNpadStyleTag_NpadJoyRight) { x = -speed; y = 0; }
        waitFor([&] { return lastGyroMatches(0, x, y, 0) &&
            lastMotionMatches(0, LI_MOTION_TYPE_ACCEL, 0, 0, 9.80665f); });
        motion.setVhfProCompatibility(false);
        waitFor([&] { return lastGyroMatches(0, -y, 0, -x) &&
            lastMotionMatches(0, LI_MOTION_TYPE_ACCEL, 0, 9.80665f, 0); });
        motion.setVhfProCompatibility(true);
        waitFor([&] { return lastGyroMatches(0, x, y, 0); });
        frozen = true;
        waitFor([] { return lastGyroMatches(0, 0, 0, 0); });
        frozen = false;
        waitFor([&] { return lastGyroMatches(0, x, y, 0); });
        motion.stop();
    }
    // Exercise all signed Npad gyro axes in actual packets, not only the X stub.
    handheld = false;
    style = HidNpadStyleTag_NpadFullKey;
    customGyro = true;
    motion.prepareSession(true, true, true);
    motion.start();
    motion.request(0, LI_MOTION_TYPE_GYRO, 100);
    motion.pause(false);
    for (unsigned axis = 0; axis < 3; ++axis) for (float sign : {-1.0f, 1.0f}) {
        gyroX = axis == 0 ? 0.1f * sign : 0;
        gyroY = axis == 1 ? 0.1f * sign : 0;
        gyroZ = axis == 2 ? 0.1f * sign : 0;
        const float x = axis == 1 ? 36.0f * sign : 0;
        const float y = axis == 0 ? -36.0f * sign : 0;
        const float z = axis == 2 ? 36.0f * sign : 0;
        waitFor([&] { return lastGyroMatches(0, x, y, z); });
    }
    motion.stop();
    customGyro = false;
    motion.prepareSession(true, true); // An ordinary session resets compatibility to off.
    motion.start();
    motion.request(0, LI_MOTION_TYPE_GYRO, 100);
    motion.pause(false);
    waitFor([] { return lastGyroMatches(0, 36, 0, 0); });
    // Gyro gain scales sent gyro live and is clamped; accel is untouched.
    assert(clampGyroGain(10.f) == 3.0f && clampGyroGain(0.1f) == 0.5f);
    motion.setGyroGain(2.0f);
    waitFor([] { return lastGyroMatches(0, 72, 0, 0); });
    motion.setGyroGain(1.0f);
    waitFor([] { return lastGyroMatches(0, 36, 0, 0); });
    // Zero drift mode is applied on start (Standard) and re-applied live.
    waitFor([] { return lastDriftMode == HidGyroscopeZeroDriftMode_Standard; });
    motion.setZeroDriftMode(0);
    waitFor([] { return lastDriftMode == HidGyroscopeZeroDriftMode_Loose; });
    motion.setZeroDriftMode(1);
    waitFor([] { return lastDriftMode == HidGyroscopeZeroDriftMode_Standard; });
    // Integrated angle follows rate x time: 36 deg/s for ~0.25 s is ~9 deg.
    motion.resetMeasurement();
    std::this_thread::sleep_for(250ms);
    const float measured = motion.measuredAngle(0);
    assert(measured > 5.0f && measured < 14.0f);
    assert(std::abs(motion.measuredPeakRate() - 36.0f) < 0.5f);
    motion.stop();
    // Pointer mode applies to a detached Joy-Con pair only, live.
    handheld = false;
    style = HidNpadStyleTag_NpadJoyDual;
    attributes = HidNpadAttribute_IsConnected | HidNpadAttribute_IsRightConnected;
    customGyro = true;
    gyroX = 0; gyroY = 0.1f; gyroZ = 0;  // raw Y is SDL roll (-36)
    motion.prepareSession(true, true);
    motion.setPointerMode(true);
    motion.start();
    motion.request(0, LI_MOTION_TYPE_GYRO, 100);
    motion.pause(false);
    waitFor([] { return lastGyroMatches(0, 0, 0, 0); });     // roll dropped
    gyroY = 0; gyroZ = 0.1f;                                  // raw Z is SDL yaw
    waitFor([] { return lastGyroMatches(0, 0, 36, 0); });
    motion.setPointerMode(false);
    gyroY = 0.1f; gyroZ = 0;
    waitFor([] { return lastGyroMatches(0, 0, 0, -36); });   // roll back when off
    motion.setPointerMode(true);
    waitFor([] { return lastGyroMatches(0, 0, 0, 0); });
    style = HidNpadStyleTag_NpadFullKey;                      // Pro is never pointer
    waitFor([] { return lastGyroMatches(0, 0, 0, -36); });
    motion.stop();
    motion.setPointerMode(false);
    customGyro = false;
    assert(starts == stops);
    std::cout << "Motion routing, VHF Pro axes (all sources/signed axes/live toggle), host requests, stale samples, reconnect, gyro gain, zero drift, angle measurement and Joy-Con pair pointer mode: PASS\n";
}
