#pragma once

#include "core_types.h"

#include <vector>

namespace tpfancontrol {
namespace core {

ValidatedTemperature validateTemperature(
    const TemperatureSample& sample,
    std::uint64_t nowMs,
    const SensorPolicy& policy);

ValidatedTemperature hottestValidTemperature(
    const std::vector<TemperatureSample>& samples,
    std::uint64_t nowMs,
    const SensorPolicy& policy);

// Rejects a tachometer reading that cannot be a real measurement, so a
// mis-scaled or unsigned-misread value cannot be reported as a healthy fan.
// SAFETY.md 6.1 requires impossible RPM behaviour to be distinguishable from a
// stopped fan, and 8 requires a suspect result not to trigger blind rewrites.
ValidatedFanRpm validateFanRpm(
    bool hasReading,
    int rawRpm,
    std::uint64_t timestampMs,
    std::uint64_t nowMs,
    const FanRpmPolicy& policy);

} // namespace core
} // namespace tpfancontrol
