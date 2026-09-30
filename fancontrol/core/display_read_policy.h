#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tpfancontrol {
namespace core {

// Pure description of the legacy display-sensor layout. This does not read
// hardware and does not assert that these offsets are correct for any model.
enum class DisplayReadVariant {
    RegisterSensors,
    RegisterSensorsWithoutExternal,
    TwrBlock
};

struct DisplayReadOptions {
    bool useTwr = false;
    bool noExternalSensors = false;
    bool showBiasedTemperatures = false;
    std::array<int, 12> sensorOffsets{};
};

struct DisplayTemperature {
    std::uint8_t registerAddress = 0;
    int valueC = 0;
    bool available = false;
};

struct DisplayReadPlan {
    DisplayReadVariant variant = DisplayReadVariant::RegisterSensors;
    std::array<std::uint8_t, 15> registerAddresses{};
    std::size_t registerCount = 0;
    std::uint32_t maximumWholeReadAttempts = 3;
    std::uint32_t maximumTwrAttempts = 0;
    std::uint32_t retryDelayMs = 200;
};

struct DisplayRegisterReadResult {
    bool ok = false;
    std::uint8_t fanLevel = 0;
    std::uint8_t fanSpeedLow = 0;
    std::uint8_t fanSpeedHigh = 0;
    std::array<std::uint8_t, 12> rawTemperatures{};
    std::size_t registersRead = 0;
    std::uint8_t failedAddress = 0;
};

// Builds the normal register-read sequence from ReadEcRaw: fan control, the
// two tachometer bytes, then eight internal and (unless disabled) four
// external temperature registers. TWR supplies temperatures through its
// separate block protocol, so its register list contains only fan and RPM.
inline DisplayReadPlan makeDisplayReadPlan(const DisplayReadOptions& options)
{
    DisplayReadPlan plan;
    plan.registerAddresses[0] = 0x2F;
    plan.registerAddresses[1] = 0x84;
    plan.registerAddresses[2] = 0x85;
    plan.registerCount = 3;
    if (options.useTwr) {
        plan.variant = DisplayReadVariant::TwrBlock;
        plan.maximumWholeReadAttempts = 3;
        // ReadEcRaw's TWR goto loop performs two attempts before reporting
        // failure (the third entry reaches the failure guard).
        plan.maximumTwrAttempts = 2;
        return plan;
    }

    const bool noExternal = options.noExternalSensors;
    plan.variant = noExternal
        ? DisplayReadVariant::RegisterSensorsWithoutExternal
        : DisplayReadVariant::RegisterSensors;
    for (std::uint8_t address = 0x78; address <= 0x7F; ++address) {
        plan.registerAddresses[plan.registerCount++] = address;
    }
    if (!noExternal) {
        for (std::uint8_t address = 0xC0; address <= 0xC3; ++address) {
            plan.registerAddresses[plan.registerCount++] = address;
        }
    }
    return plan;
}

// Map the twelve bytes filled by the normal register path into displayed
// temperatures. `NoExtSensor` leaves the last four entries unavailable (the
// legacy code labels them n/a); bias subtraction is applied only when the UI
// option is enabled, matching the legacy order after a successful byte read.
inline std::array<DisplayTemperature, 12> mapRegisterDisplayTemperatures(
    const std::array<std::uint8_t, 12>& raw,
    const DisplayReadOptions& options)
{
    std::array<DisplayTemperature, 12> mapped{};
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        mapped[i].registerAddress = i < 8
            ? static_cast<std::uint8_t>(0x78U + i)
            : static_cast<std::uint8_t>(0xC0U + (i - 8U));
        mapped[i].available = i < 8 || !options.noExternalSensors;
        if (mapped[i].available) {
            mapped[i].valueC = static_cast<int>(raw[i]);
            if (options.showBiasedTemperatures) {
                mapped[i].valueC -= options.sensorOffsets[i];
            }
        }
    }
    return mapped;
}

// The TWR path's legacy block-slot mapping is intentionally non-contiguous:
// sensors 5, 6, and 7 come from slots 6, 8, and 9. The slots are preserved
// here as observed behavior; they are not hardware-validated facts. The legacy
// TWR branch also ignores NoExtSensor and ShowBiasedTemps, unlike the
// register-sensor branch.
inline std::array<DisplayTemperature, 12> mapTwrDisplayTemperatures(
    const std::array<std::uint8_t, 16>& block,
    const DisplayReadOptions& options)
{
    // The legacy TWR branch does not consult these normal-register options.
    (void)options;
    static constexpr std::array<std::size_t, 12> slots = {
        0, 1, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13
    };
    std::array<DisplayTemperature, 12> mapped{};
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        mapped[i].registerAddress = i < 8
            ? static_cast<std::uint8_t>(0x78U + i)
            : static_cast<std::uint8_t>(0xC0U + (i - 8U));
        mapped[i].available = true;
        mapped[i].valueC = static_cast<int>(block[slots[i]]);
    }
    return mapped;
}

} // namespace core
} // namespace tpfancontrol
