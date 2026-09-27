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

} // namespace core
} // namespace tpfancontrol
