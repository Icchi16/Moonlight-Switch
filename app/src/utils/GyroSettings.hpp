#pragma once
#include "Settings.hpp"
#include "../streaming/switch/SwitchMotion.hpp"
#include <borealis.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

inline std::string gyroSourceText() {
    using namespace brls::literals;
    if (!Settings::instance().gyro_enabled()) return "hints/off"_i18n;
    if (!moonvibe::SwitchMotion::instance().active()) return "settings/gyro_waiting_stream"_i18n;
    using moonvibe::MotionSource;
    switch (moonvibe::SwitchMotion::instance().source()) {
    case MotionSource::Console: return "Console";
    case MotionSource::Pro: return "settings/gyro_pro"_i18n;
    case MotionSource::JoyLeft: return "settings/gyro_left"_i18n;
    case MotionSource::JoyRight: return "settings/gyro_right"_i18n;
    default: return "settings/gyro_none"_i18n;
    }
}

inline void setupGyroSettings(brls::BooleanCell* enabled,
                              brls::SelectorCell* joycon,
                              brls::BooleanCell* compatibility,
                              brls::Label* compatibilityHint,
                              brls::DetailCell* source,
                              brls::SelectorCell* gain,
                              brls::SelectorCell* zeroDrift,
                              brls::BooleanCell* pointer) {
#ifdef __SWITCH__
    using namespace brls::literals;
    pointer->init("settings/joycon_pointer_mode"_i18n, Settings::instance().joycon_pointer_mode(),
        [](bool value) {
            Settings::instance().set_joycon_pointer_mode(value);
            moonvibe::SwitchMotion::instance().setPointerMode(value);
        });
    // Gain 0.5x..3.0x in 0.1 steps; applies live to the running stream.
    std::vector<std::string> gains;
    for (int tenth = 5; tenth <= 30; ++tenth)
        gains.push_back(fmt::format("{:.1f}x", tenth / 10.0f));
    const int gainIndex = std::clamp(
        static_cast<int>(std::lround(Settings::instance().gyro_gain() * 10.f)) - 5, 0, 25);
    gain->init("settings/gyro_gain"_i18n, gains, gainIndex, [](int choice) {
        const float value = (choice + 5) / 10.0f;
        Settings::instance().set_gyro_gain(value);
        moonvibe::SwitchMotion::instance().setGyroGain(value);
    });
    zeroDrift->init("settings/gyro_zero_drift"_i18n,
        {"settings/gyro_zero_drift_loose"_i18n, "settings/gyro_zero_drift_standard"_i18n,
         "settings/gyro_zero_drift_tight"_i18n},
        std::clamp(Settings::instance().gyro_zero_drift(), 0, 2), [](int choice) {
            Settings::instance().set_gyro_zero_drift(choice);
            moonvibe::SwitchMotion::instance().setZeroDriftMode(choice);
        });
    enabled->init("settings/gyro"_i18n, Settings::instance().gyro_enabled(), [](bool value) {
        Settings::instance().set_gyro_enabled(value);
        moonvibe::SwitchMotion::instance().setEnabled(value);
    });
    joycon->init("settings/joycon_gyro"_i18n,
        {"settings/gyro_left"_i18n, "settings/gyro_right"_i18n},
        Settings::instance().joycon_gyro_right() ? 1 : 0, [](int choice) {
            Settings::instance().set_joycon_gyro_right(choice == 1);
            moonvibe::SwitchMotion::instance().setPreferRight(choice == 1);
        });
    compatibility->init("settings/vhf_pro_compatibility"_i18n,
        Settings::instance().vhf_pro_compatibility(), [](bool value) {
            Settings::instance().set_vhf_pro_compatibility(value);
            moonvibe::SwitchMotion::instance().setVhfProCompatibility(value);
        });
    compatibilityHint->setText("settings/vhf_pro_compatibility_hint"_i18n);
    source->setText("settings/gyro_source"_i18n);
    source->setDetailText(gyroSourceText());
#else
    enabled->removeFromSuperView(true);
    joycon->removeFromSuperView(true);
    compatibility->removeFromSuperView(true);
    compatibilityHint->removeFromSuperView(true);
    source->removeFromSuperView(true);
    gain->removeFromSuperView(true);
    zeroDrift->removeFromSuperView(true);
    pointer->removeFromSuperView(true);
#endif
}
