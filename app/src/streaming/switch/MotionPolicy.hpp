#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace moonvibe {
enum class MotionSource { None, Console, Pro, JoyLeft, JoyRight };
enum class PadKind { None, Handheld, Pro, JoyDual, JoyLeft, JoyRight };

inline MotionSource selectMotionSource(PadKind kind, bool leftConnected,
                                      bool rightConnected, bool preferRight) {
    switch (kind) {
    case PadKind::Handheld: return MotionSource::Console;
    case PadKind::Pro: return MotionSource::Pro;
    case PadKind::JoyLeft: return MotionSource::JoyLeft;
    case PadKind::JoyRight: return MotionSource::JoyRight;
    case PadKind::JoyDual:
        if (preferRight && rightConnected) return MotionSource::JoyRight;
        if (leftConnected) return MotionSource::JoyLeft;
        if (rightConnected) return MotionSource::JoyRight;
        return MotionSource::None;
    default: return MotionSource::None;
    }
}

// Single Joy-Cons are held horizontally, matching Borealis' button/stick map.
inline std::array<float, 3> rotateSingleJoyCon(std::array<float, 3> value,
                                             PadKind kind) {
    if (kind == PadKind::JoyLeft) return {value[2], value[1], -value[0]};
    if (kind == PadKind::JoyRight) return {-value[2], value[1], value[0]};
    return value;
}

inline std::array<float, 3> normalizeAcceleration(float x, float y, float z,
                                                 PadKind kind) {
    constexpr float gravity = 9.80665f;
    return rotateSingleJoyCon({-x * gravity, -z * gravity, y * gravity}, kind);
}

inline std::array<float, 3> normalizeGyro(float x, float y, float z,
                                        PadKind kind) {
    return rotateSingleJoyCon({x * 360.0f, z * 360.0f, -y * 360.0f}, kind);
}

// VibeShine 2.0's VHF Pro encoder copies SDL axes into raw Nintendo IMU
// fields. Invert the Pro reader's {-rawY, rawZ, -rawX} transform at the send
// boundary, for both sensors. Keep standard SDL coordinates for other hosts.
inline std::array<float, 3> encodeMotionAxes(std::array<float, 3> sdl,
                                            bool vhfProCompatibility) {
    return vhfProCompatibility ? std::array<float, 3>{-sdl[2], -sdl[0], sdl[1]} : sdl;
}

// Pointer mode for a detached Joy-Con pair (Wii-remote style): pointing the
// tip left/right turns the aim left/right however the Joy-Con is tilted.
// Yaw is the rotation about the world vertical, taken from gravity (the
// "player space" technique): world yaw = gyro . up, relaxed by sqrt(2) and
// capped by the local yaw/roll magnitude so tilting cannot exaggerate it.
// Pitch stays the local pitch; roll is dropped so twisting the wrist does not
// move the aim. Inputs and output are SDL axes (x pitch, y yaw, z roll), gyro
// in deg/s and gravity as measured acceleration (points up at rest).
inline std::array<float, 3> pointerGyro(std::array<float, 3> gyro,
                                       std::array<float, 3> gravity) {
    const float length = std::sqrt(gravity[0] * gravity[0] + gravity[1] * gravity[1] +
                                   gravity[2] * gravity[2]);
    if (!std::isfinite(length) || length < 1e-3f)
        return {gyro[0], gyro[1], 0.f};
    const float worldYaw = (gyro[0] * gravity[0] + gyro[1] * gravity[1] +
                            gyro[2] * gravity[2]) / length;
    constexpr float relax = 1.41f;
    const float localYawRoll = std::sqrt(gyro[1] * gyro[1] + gyro[2] * gyro[2]);
    const float yaw = std::copysign(std::fmin(std::fabs(worldYaw) * relax, localYawRoll), worldYaw);
    return {gyro[0], yaw, 0.f};
}

inline float clampGyroGain(float gain) {
    return std::isfinite(gain) ? std::clamp(gain, 0.5f, 3.0f) : 1.0f;
}

inline std::array<float, 3> applyGyroGain(std::array<float, 3> dps, float gain) {
    gain = clampGyroGain(gain);
    return {dps[0] * gain, dps[1] * gain, dps[2] * gain};
}

inline uint64_t motionIntervalUs(unsigned rateHz) {
    return rateHz ? 1000000ULL / std::clamp(rateHz, 1U, 200U) : 0;
}
} // namespace moonvibe
