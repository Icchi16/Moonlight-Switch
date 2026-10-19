#include "SwitchMotion.hpp"
#include "Limelight.h"
#include <borealis/core/logger.hpp>
#include <chrono>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace moonvibe {
SwitchMotion& SwitchMotion::instance() {
    static SwitchMotion motion;
    return motion;
}

SwitchMotion::~SwitchMotion() { stop(); }

void SwitchMotion::prepareSession(bool enabled, bool preferRight, bool vhfProCompatibility) {
    stop();
    enabled_ = enabled;
    preferRight_ = preferRight;
    vhfProCompatibility_ = vhfProCompatibility;
    paused_ = true;
    measureReset_ = true;
    for (auto& slot : rates_) for (auto& rate : slot) rate = 0;
    for (auto& source : sources_) source = MotionSource::None;
    for (auto& identity : identities_) identity = 0;
}

void SwitchMotion::start() {
#ifdef __SWITCH__
    std::lock_guard<std::mutex> lock(lifecycle_);
    if (!running_.exchange(true)) worker_ = std::thread(&SwitchMotion::run, this);
#endif
}

void SwitchMotion::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_);
    running_ = false;
    if (worker_.joinable()) worker_.join();
    for (auto& slot : rates_) for (auto& rate : slot) rate = 0;
    for (auto& source : sources_) source = MotionSource::None;
    for (auto& identity : identities_) identity = 0;
}

void SwitchMotion::pause(bool paused) { paused_ = paused; }

void SwitchMotion::request(uint16_t slot, uint8_t type, uint16_t rateHz) {
    if (slot >= slots || type < LI_MOTION_TYPE_ACCEL || type > LI_MOTION_TYPE_GYRO)
        return;
    rates_[slot][type - 1] = rateHz;
    brls::Logger::info("Motion: host requests slot {} type {} at {} Hz", slot, type, rateHz);
}

bool SwitchMotion::requested(uint8_t slot, uint8_t type) const {
    return slot < slots && type >= LI_MOTION_TYPE_ACCEL && type <= LI_MOTION_TYPE_GYRO &&
           rates_[slot][type - 1].load() != 0;
}

MotionSource SwitchMotion::source(unsigned slot) const {
    return slot < slots ? sources_[slot].load() : MotionSource::None;
}

uint64_t SwitchMotion::identity(unsigned slot) const {
    return slot < slots ? identities_[slot].load() : 0;
}

void SwitchMotion::run() {
#ifdef __SWITCH__
    using Clock = std::chrono::steady_clock;
    auto nowUs = [] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            Clock::now().time_since_epoch()).count());
    };
    struct Sensor {
        HidSixAxisSensorHandle handle{};
        bool started = false;
        HidNpadIdType id = HidNpadIdType_No1;
        PadKind kind = PadKind::None;
        MotionSource source = MotionSource::None;
        uint64_t sampling[2]{};
        uint64_t nextDue[2]{};
        uint64_t lastFresh = 0;
        uint64_t observedSampling = 0;
        uint64_t retryAt = 0;
        bool sentGyro = false;
        int lastSendError[2]{};
        int driftMode = -1;
        std::array<float, 3> gravity{};
        bool gravityValid = false;
        uint64_t gravitySample = 0;
    };
    // Applies the configured zero-drift mode whenever it differs from the
    // sensor's; a failure is logged once per mode and not retried.
    auto applyDriftMode = [this](unsigned slot, Sensor& sensor) {
        const int mode = zeroDriftMode_.load();
        if (sensor.driftMode == mode) return;
        const Result rc = hidSetGyroscopeZeroDriftMode(sensor.handle,
            static_cast<HidGyroscopeZeroDriftMode>(mode));
        brls::Logger::info("Motion: slot {} zero drift mode {} rc={:#x}", slot, mode, rc);
        sensor.driftMode = mode;
    };
    uint64_t measureLastUs = 0;
    uint64_t measureLastSample = 0;
    std::array<Sensor, slots> sensors{};
    PadState handheld{};
    std::array<PadState, slots> pads{};
    padInitialize(&handheld, HidNpadIdType_Handheld);
    for (unsigned i = 0; i < slots; ++i)
        padInitialize(&pads[i], static_cast<HidNpadIdType>(i));

    auto neutralize = [this](unsigned slot, Sensor& sensor) {
        if (sensor.sentGyro && requested(slot, LI_MOTION_TYPE_GYRO))
            LiSendControllerMotionEvent(slot, LI_MOTION_TYPE_GYRO, 0, 0, 0);
        sensor.sentGyro = false;
    };
    auto close = [&neutralize](unsigned slot, Sensor& sensor) {
        neutralize(slot, sensor);
        if (sensor.started) hidStopSixAxisSensor(sensor.handle);
        sensor = Sensor{};
    };

    bool previousCompatibility = vhfProCompatibility_.load();
    brls::Logger::info("Motion: VHF Pro axis compatibility {}", previousCompatibility);
    while (running_) {
        // Snapshot once so gyro and gravity use the same frame in this poll.
        const bool compatibility = vhfProCompatibility_.load();
        if (compatibility != previousCompatibility) {
            for (unsigned slot = 0; slot < slots; ++slot) {
                neutralize(slot, sensors[slot]);
                for (unsigned type = 0; type < 2; ++type) {
                    sensors[slot].sampling[type] = 0;
                    sensors[slot].nextDue[type] = 0;
                }
            }
            brls::Logger::info("Motion: VHF Pro axis compatibility {}", compatibility);
            previousCompatibility = compatibility;
        }
        const uint64_t now = nowUs();
        if (measureReset_.exchange(false)) {
            for (auto& angle : measuredAngle_) angle = 0.f;
            measuredPeak_ = 0.f;
            measureLastUs = 0;
        }
        padUpdate(&handheld);
        const bool hasHandheld = handheld.active_handheld;
        for (auto& pad : pads) padUpdate(&pad);
        // Mirror Borealis' button-slot mapping exactly: handheld first, then
        // numbered Npads, stopping at the first empty pad. Never steal another pad's gyro.
        unsigned connected = hasHandheld ? 1 : 0;
        for (unsigned i = 0; i < slots - (hasHandheld ? 1 : 0); ++i) {
            if (!padGetStyleSet(&pads[i])) break;
            ++connected;
        }
        for (unsigned slot = 0; slot < slots; ++slot) {
            Sensor& sensor = sensors[slot];
            PadKind kind = PadKind::None;
            MotionSource selected = MotionSource::None;
            HidNpadIdType id = HidNpadIdType_No1;
            u64 attributes = 0;
            if (slot < connected) {
                const bool isHandheld = hasHandheld && slot == 0;
                PadState& pad = isHandheld ? handheld : pads[slot - (hasHandheld ? 1 : 0)];
                id = isHandheld ? HidNpadIdType_Handheld :
                     static_cast<HidNpadIdType>(slot - (hasHandheld ? 1 : 0));
                const u32 style = padGetStyleSet(&pad);
                if (isHandheld) kind = PadKind::Handheld;
                else if (style & HidNpadStyleTag_NpadFullKey) kind = PadKind::Pro;
                else if (style & HidNpadStyleTag_NpadJoyDual) kind = PadKind::JoyDual;
                else if (style & HidNpadStyleTag_NpadJoyLeft) kind = PadKind::JoyLeft;
                else if (style & HidNpadStyleTag_NpadJoyRight) kind = PadKind::JoyRight;
                const u64 attrs = padGetAttributes(&pad);
                attributes = attrs;
                selected = selectMotionSource(kind, attrs & HidNpadAttribute_IsLeftConnected,
                    attrs & HidNpadAttribute_IsRightConnected, preferRight_);
            }
            sources_[slot] = selected;
            identities_[slot] = slot < connected ?
                (static_cast<uint64_t>(id) + 1) << 32 |
                static_cast<uint64_t>(kind) << 16 |
                (attributes & (HidNpadAttribute_IsConnected | HidNpadAttribute_IsLeftConnected |
                               HidNpadAttribute_IsRightConnected)) : 0;
            const bool wanted = enabled_ && !paused_ &&
                                (requested(slot, LI_MOTION_TYPE_ACCEL) ||
                                 requested(slot, LI_MOTION_TYPE_GYRO));
            if (!wanted || selected == MotionSource::None) {
                if (sensor.started) close(slot, sensor);
                continue;
            }
            if (sensor.started && (id != sensor.id || kind != sensor.kind || selected != sensor.source))
                close(slot, sensor);
            if (!sensor.started) {
                if (now < sensor.retryAt) continue;
                HidNpadStyleTag tag = HidNpadStyleTag_NpadHandheld;
                if (kind == PadKind::Pro) tag = HidNpadStyleTag_NpadFullKey;
                else if (kind == PadKind::JoyDual) tag = HidNpadStyleTag_NpadJoyDual;
                else if (kind == PadKind::JoyLeft) tag = HidNpadStyleTag_NpadJoyLeft;
                else if (kind == PadKind::JoyRight) tag = HidNpadStyleTag_NpadJoyRight;
                HidSixAxisSensorHandle handles[2]{};
                Result rc = hidGetSixAxisSensorHandles(handles, kind == PadKind::JoyDual ? 2 : 1, id, tag);
                const unsigned index = kind == PadKind::JoyDual && selected == MotionSource::JoyRight ? 1 : 0;
                if (R_SUCCEEDED(rc)) rc = hidStartSixAxisSensor(handles[index]);
                if (R_FAILED(rc)) {
                    brls::Logger::warning("Motion: sensor start slot {} failed {:#x}", slot, rc);
                    sensor.retryAt = now + 1000000;
                    continue;
                }
                sensor.handle = handles[index];
                sensor.started = true;
                sensor.id = id;
                sensor.kind = kind;
                sensor.source = selected;
                brls::Logger::info("Motion: slot {} source {} Npad {}", slot, static_cast<int>(selected), static_cast<int>(id));
            }
            applyDriftMode(slot, sensor);
            HidSixAxisSensorState sample{};
            const bool valid = hidGetSixAxisSensorStates(sensor.handle, &sample, 1) == 1 &&
                (sample.attributes & HidSixAxisSensorAttribute_IsConnected) &&
                std::isfinite(sample.acceleration.x) && std::isfinite(sample.acceleration.y) &&
                std::isfinite(sample.acceleration.z) && std::isfinite(sample.angular_velocity.x) &&
                std::isfinite(sample.angular_velocity.y) && std::isfinite(sample.angular_velocity.z);
            if (valid && sample.sampling_number != sensor.observedSampling) {
                sensor.observedSampling = sample.sampling_number;
                sensor.lastFresh = now;
            }
            if (!valid || !sensor.lastFresh || now - sensor.lastFresh > 250000) {
                neutralize(slot, sensor);
                continue;
            }
            const auto accel = normalizeAcceleration(sample.acceleration.x,
                sample.acceleration.y, sample.acceleration.z, kind);
            if (sample.sampling_number != sensor.gravitySample) {
                // Low-pass the accelerometer so hand movement does not swing
                // the gravity estimate used by pointer mode.
                for (unsigned axis = 0; axis < 3; ++axis)
                    sensor.gravity[axis] = sensor.gravityValid ?
                        sensor.gravity[axis] + (accel[axis] - sensor.gravity[axis]) * 0.1f : accel[axis];
                sensor.gravityValid = true;
                sensor.gravitySample = sample.sampling_number;
            }
            auto gyro = normalizeGyro(sample.angular_velocity.x, sample.angular_velocity.y,
                                      sample.angular_velocity.z, kind);
            if (kind == PadKind::JoyDual && pointerMode_.load())
                gyro = pointerGyro(gyro, sensor.gravity);
            gyro = applyGyroGain(gyro, gyroGain_.load());
            if (slot == 0 && sample.sampling_number != measureLastSample) {
                // Integrate every fresh sample (not only sent ones) by wall
                // clock; gaps over 50 ms (pauses, stalls) are not integrated.
                const auto& rate = gyro;
                if (measureLastUs && now - measureLastUs <= 50000) {
                    const float dt = static_cast<float>(now - measureLastUs) / 1e6f;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        measuredAngle_[axis] = measuredAngle_[axis].load() + rate[axis] * dt;
                }
                const float magnitude = std::sqrt(rate[0] * rate[0] + rate[1] * rate[1] + rate[2] * rate[2]);
                if (magnitude > measuredPeak_.load()) measuredPeak_ = magnitude;
                measureLastUs = now;
                measureLastSample = sample.sampling_number;
            }
            for (unsigned type = 0; type < 2; ++type) {
                const auto interval = motionIntervalUs(rates_[slot][type]);
                if (!interval) {
                    sensor.nextDue[type] = 0;
                    sensor.sampling[type] = 0;
                    if (type == 1) sensor.sentGyro = false;
                    continue;
                }
                if (now < sensor.nextDue[type] || sample.sampling_number == sensor.sampling[type]) continue;
                const auto value = encodeMotionAxes(type == 0 ? accel : gyro, compatibility);
                const int result = LiSendControllerMotionEvent(slot, type + 1, value[0], value[1], value[2]);
                if (result != sensor.lastSendError[type]) {
                    if (result) brls::Logger::warning("Motion: send slot {} type {} failed {}", slot, type + 1, result);
                    else if (sensor.lastSendError[type]) brls::Logger::info("Motion: send slot {} recovered", slot);
                    sensor.lastSendError[type] = result;
                }
                if (!result) {
                    sensor.sampling[type] = sample.sampling_number;
                    if (type == 1) sensor.sentGyro = true;
                }
                // Use an absolute schedule without accumulating loop/scheduler drift.
                sensor.nextDue[type] = sensor.nextDue[type] ? sensor.nextDue[type] + interval : now + interval;
                if (sensor.nextDue[type] <= now) sensor.nextDue[type] = now + interval;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (unsigned slot = 0; slot < slots; ++slot) close(slot, sensors[slot]);
#endif
}
} // namespace moonvibe

// The library's motion-only compatibility path requires an actual host motion
// request. It never changes touchpad flags or enables motion for all hosts.
extern "C" bool MoonvibeHostRequestedMotion(uint8_t slot, uint8_t type) {
    return moonvibe::SwitchMotion::instance().requested(slot, type);
}
