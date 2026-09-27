#include "sensor_validation.h"

#include <algorithm>
#include <limits>

namespace tpfancontrol {
namespace core {
namespace {

ValidatedTemperature invalidTemperature(
    const TemperatureSample& sample,
    SensorValidity validity,
    const char* reason)
{
    ValidatedTemperature result;
    result.validity = validity;
    result.source = sample.source;
    result.reason = reason;
    result.timestampMs = sample.timestampMs;
    return result;
}

} // namespace

ValidatedTemperature validateTemperature(
    const TemperatureSample& sample,
    std::uint64_t nowMs,
    const SensorPolicy& policy)
{
    if (!sample.hasValue) {
        return invalidTemperature(sample, SensorValidity::Missing, "sample has no value");
    }

    if (!sample.backendOk) {
        return invalidTemperature(sample, SensorValidity::BackendError, "backend read failed");
    }

    if (!sample.sourceValid) {
        return invalidTemperature(sample, SensorValidity::ProviderInvalid, "provider marked sample invalid");
    }

    if (sample.sentinel) {
        return invalidTemperature(sample, SensorValidity::Sentinel, "provider sentinel value");
    }

    if (sample.timestampMs > nowMs + policy.maximumFutureSkewMs) {
        return invalidTemperature(sample, SensorValidity::FutureTimestamp, "sample timestamp is in the future");
    }

    if (nowMs >= sample.timestampMs && nowMs - sample.timestampMs > policy.maximumAgeMs) {
        return invalidTemperature(sample, SensorValidity::Stale, "sample is stale");
    }

    if (sample.valueC < policy.minimumPlausibleC || sample.valueC > policy.maximumPlausibleC) {
        return invalidTemperature(sample, SensorValidity::OutOfRange, "sample is outside plausible range");
    }

    ValidatedTemperature result;
    result.valid = true;
    result.valueC = sample.valueC;
    result.timestampMs = sample.timestampMs;
    result.validity = SensorValidity::Valid;
    result.source = sample.source;
    result.reason = "valid";
    return result;
}

ValidatedTemperature hottestValidTemperature(
    const std::vector<TemperatureSample>& samples,
    std::uint64_t nowMs,
    const SensorPolicy& policy)
{
    ValidatedTemperature hottest;
    hottest.validity = SensorValidity::Missing;
    hottest.reason = "no valid temperature samples";

    for (const TemperatureSample& sample : samples) {
        const ValidatedTemperature current = validateTemperature(sample, nowMs, policy);
        if (!current.valid) {
            if (!hottest.valid && hottest.validity == SensorValidity::Missing) {
                hottest = current;
            }
            continue;
        }

        if (!hottest.valid || current.valueC > hottest.valueC) {
            hottest = current;
        }
    }

    return hottest;
}

} // namespace core
} // namespace tpfancontrol
