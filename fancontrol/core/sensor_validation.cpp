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

ValidatedFanRpm validateFanRpm(
    bool hasReading,
    int rawRpm,
    std::uint64_t timestampMs,
    std::uint64_t nowMs,
    const FanRpmPolicy& policy)
{
    ValidatedFanRpm result;

    if (!hasReading) {
        result.validity = SensorValidity::Missing;
        result.reason = "no tachometer reading was supplied";
        return result;
    }

    // A negative value cannot come from an unsigned tachometer register. It is
    // either a sign error or a sentinel that was widened into the negative
    // range, and either way it is not a measurement.
    if (rawRpm < 0) {
        result.validity = SensorValidity::ProviderInvalid;
        result.reason = "tachometer reported a negative value, which no register can produce";
        return result;
    }

    // Sentinels mean "no measurement", so they must not be reported as a
    // stopped fan. Zero is checked by the range instead, because 0 RPM is a
    // real and important measurement.
    for (const std::uint16_t sentinel : policy.sentinelValues) {
        if (static_cast<std::uint16_t>(rawRpm) == sentinel) {
            result.validity = SensorValidity::Sentinel;
            result.reason = "tachometer returned sentinel 0x" + std::to_string(sentinel)
                + ", which means no measurement rather than a fan speed";
            result.rpm = 0;
            return result;
        }
    }

    if (rawRpm > policy.maximumPlausible) {
        result.validity = SensorValidity::OutOfRange;
        result.reason = "tachometer reported " + std::to_string(rawRpm)
            + " RPM, above the plausible maximum of " + std::to_string(policy.maximumPlausible)
            + "; this is the signature of a raw register byte read as unsigned";
        return result;
    }

    if (rawRpm < policy.minimumPlausible) {
        result.validity = SensorValidity::OutOfRange;
        result.reason = "tachometer reported " + std::to_string(rawRpm)
            + " RPM, below the plausible minimum of " + std::to_string(policy.minimumPlausible);
        return result;
    }

    if (timestampMs > nowMs && (timestampMs - nowMs) > policy.maximumFutureSkewMs) {
        result.validity = SensorValidity::FutureTimestamp;
        result.reason = "tachometer timestamp is in the future";
        return result;
    }

    if (nowMs >= timestampMs && (nowMs - timestampMs) > policy.maximumAgeMs) {
        result.validity = SensorValidity::Stale;
        result.reason = "tachometer reading is older than the permitted maximum age";
        return result;
    }

    result.valid = true;
    result.rpm = rawRpm;
    result.validity = SensorValidity::Valid;
    return result;
}

} // namespace core
} // namespace tpfancontrol
