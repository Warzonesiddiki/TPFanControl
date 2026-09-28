#include "app_bridge.h"

#include <utility>

namespace tpfancontrol {
namespace core {

namespace {

// The application clamps an implausible tachometer reading to the last good one
// (fanstuff.cpp: `if (this->fanspeed > 0x1fff) fanspeed = lastfanspeed;`), which
// is evidence of what a real reading looks like even though it is not a
// measurement. 0x1FFF is carried forward rather than an invented round number.
// Still a candidate value: Phase 0 must confirm it.
constexpr int kLegacyPlausibleRpmCeiling = 0x1FFF;

} // namespace

AppBridge::AppBridge(IIoBackend& backend, IClock& clock, BridgeConfig config)
    : config_(std::move(config))
    , bus_(backend, EcBusConfig(), clock)
    , controller_(config_.controller)
{
    // ADR-023. Applied from the configuration in the constructor and not
    // afterwards, so the bus's write policy is always exactly what the
    // configuration said. There is no method to flip it later.
    bus_.setRegisterWritesAllowed(config_.allowRegisterWrites);

    // A sensor-name list of the wrong length would shift every label by one and
    // present one reading under another's name, so it is validated once, here,
    // rather than clamped at the point of use.
    sensorNamesValid_ = config_.sensorNames.size() == static_cast<std::size_t>(kSensorCount);

    // Apply the legacy application's own tachometer plausibility bound, so the
    // core cannot accept a value the application itself would have discarded.
    // Narrowing only: a stricter caller-supplied ceiling is respected.
    if (config_.controller.fanRpm.maximumPlausible > kLegacyPlausibleRpmCeiling) {
        config_.controller.fanRpm.maximumPlausible = kLegacyPlausibleRpmCeiling;
    }
}

ValidationResult AppBridge::setCurve(const CurveConfig& config)
{
    return controller_.setCurve(config);
}

const EcBus& AppBridge::bus() const noexcept
{
    return bus_;
}

const Controller& AppBridge::controller() const noexcept
{
    return controller_;
}

Controller& AppBridge::controller() noexcept
{
    return controller_;
}

const BridgeConfig& AppBridge::config() const noexcept
{
    return config_;
}

int AppBridge::lastAppliedLevel() const noexcept
{
    return lastAppliedLevel_;
}

void AppBridge::resetLastAppliedLevel() noexcept
{
    lastAppliedLevel_ = -1;
}

std::vector<BusWrite> AppBridge::writeTrace() const
{
    return bus_.writeTrace();
}

IoResult AppBridge::readTemperatureBlock(
    std::uint8_t baseRegister,
    int count,
    int firstSensor,
    std::vector<TemperatureSample>& out)
{
    for (int i = 0; i < count; ++i) {
        const int index = firstSensor + i;
        const std::uint8_t address = static_cast<std::uint8_t>(baseRegister + i);

        TemperatureSample sample;
        sample.hasValue = false;
        sample.backendOk = false;
        sample.sourceValid = false;
        sample.sentinel = false;
        if (sensorNamesValid_) {
            sample.source = config_.sensorNames[static_cast<std::size_t>(index)];
        } else {
            // No usable names. The reading is still taken, but it is not
            // attributed to a named source, because attributing it to the wrong
            // one is worse than admitting the label is missing.
            sample.source = "sensor " + std::to_string(index + 1);
        }

        std::uint8_t raw = 0;
        const IoResult read = bus_.readRegister(address, raw);
        if (read.ok) {
            sample.valueC = raw;
            sample.hasValue = true;
            sample.backendOk = true;
            sample.sourceValid = true;
            sample.timestampMs = 0;   // filled in by read(), which knows nowMs
            out.push_back(sample);
            continue;
        }
        out.push_back(sample);
        return read;
    }
    return IoResult::success();
}

EcSnapshot AppBridge::read(std::uint64_t nowMs)
{
    EcSnapshot snapshot;
    snapshot.temperatures.reserve(static_cast<std::size_t>(kSensorCount));

    // 1. The fan level. Without it there is nothing to compare a command
    //    against, so a failure here fails the whole snapshot.
    std::uint8_t level = 0;
    IoResult read = bus_.readRegister(kRegisterFanLevel, level);
    if (!read.ok) {
        snapshot.lastError = read.error;
        snapshot.lastErrorMessage = read.message;
        return snapshot;
    }
    snapshot.fanLevelRead = true;
    snapshot.fanLevel = static_cast<int>(level);

    // 2. The tachometer. A failure here is not fatal - RPM is optional - but it
    //    must be reported as missing rather than as 0, or a stopped fan and a
    //    dead sensor look identical.
    std::uint8_t rpmLo = 0;
    std::uint8_t rpmHi = 0;
    read = bus_.readRegister(kRegisterFanSpeedLo, rpmLo);
    if (read.ok) {
        read = bus_.readRegister(kRegisterFanSpeedHi, rpmHi);
    }
    if (read.ok) {
        snapshot.fanSpeedRead = true;
        snapshot.fanRpm = (static_cast<int>(rpmHi) << 8) | static_cast<int>(rpmLo);
        snapshot.fanSpeedTimestampMs = nowMs;
    } else {
        snapshot.lastError = read.error;
        snapshot.lastErrorMessage = read.message;
    }

    // 3. Temperatures.
    read = readTemperatureBlock(kRegisterTemp0, kTemp0Count, 0, snapshot.temperatures);
    if (read.ok) {
        read = readTemperatureBlock(kRegisterTemp1, kTemp1Count, kTemp0Count,
            snapshot.temperatures);
    }
    if (!read.ok) {
        snapshot.lastError = read.error;
        snapshot.lastErrorMessage = read.message;
    }

    // Stamp every sample, including the ones that failed, so a stale or
    // future-dated check has something to reason about rather than a zero.
    for (TemperatureSample& sample : snapshot.temperatures) {
        sample.timestampMs = nowMs;
    }

    // A snapshot is only "ok" when the parts the controller acts on are present.
    // Reporting partial success as success is how a control loop ends up
    // deciding from half the picture.
    const bool allSensors = static_cast<int>(snapshot.temperatures.size()) == kSensorCount;
    bool allSensorsValid = allSensors;
    for (const TemperatureSample& sample : snapshot.temperatures) {
        if (!sample.hasValue) {
            allSensorsValid = false;
            break;
        }
    }
    snapshot.ok = snapshot.fanLevelRead
        && (!config_.requireAllSensors || allSensorsValid);
    return snapshot;
}

ControllerInput AppBridge::makeInput(
    const EcSnapshot& snapshot,
    const ControllerInput& request,
    std::uint64_t nowMs,
    std::uint64_t rpmObservationElapsedMs)
{
    ControllerInput input = request;
    input.nowMs = nowMs;
    input.temperatures = snapshot.temperatures;
    input.hasFanRpm = snapshot.fanSpeedRead;
    input.fanRpm = snapshot.fanRpm;
    input.timestampMs = snapshot.fanSpeedTimestampMs;
    // RPM is only meaningful once the fan has had a chance to respond to a
    // command. Reporting a speed observed in the same instant as the command
    // would judge the fan against a level it has not reached.
    input.rpmObservationElapsed = rpmObservationElapsedMs > 0;

    if (!snapshot.ok) {
        // A read that did not complete is a backend failure, and the controller
        // must be told. Silence here would let the core believe it still has
        // fresh data.
        input.backendFailure = true;
    }
    return input;
}

ControllerOutput AppBridge::cycle(std::uint64_t nowMs, const ControllerInput& request)
{
    const EcSnapshot snapshot = read(nowMs);
    // The caller's own rpmObservationElapsed is carried through: the bridge
    // cannot know how long a command has been in effect, only the application
    // that issued it can.
    const ControllerInput input = makeInput(
        snapshot, request, nowMs, request.rpmObservationElapsed ? nowMs : 0);
    return controller_.update(input);
}

ApplyResult AppBridge::apply(const ControllerOutput& output)
{
    ApplyResult result;

    // T3-08, enforced structurally: the core decides, this class only carries
    // out. Anything not authorised stops here, before the bus is touched.
    if (!output.commandMayBeIssued) {
        result.reason = std::string("the core did not authorise a command (control mode ")
            + toText(output.controlMode)
            + ", safety state " + toText(output.safetyState) + ")";
        return result;
    }

    int level = -1;
    switch (output.command.kind) {
    case CommandKind::Level:
        level = output.command.level;
        break;
    case CommandKind::BiosAutomatic:
        // The firmware's own automatic mode is expressed as a level on this
        // machine, not as a separate operation. The value is the one
        // fanstuff.cpp already uses, so restore and normal BIOS mode agree.
        level = kBiosAutomaticLevel;
        break;
    case CommandKind::None:
        result.reason = "the core produced no command";
        return result;
    }

    if (level < 0 || level > 0xFF) {
        result.reason = "refusing to write an out-of-range fan level";
        return result;
    }

    // Nothing to do when the register already holds the requested value. This
    // is not merely an optimisation: writing every cycle is what turns a
    // transient EC fault into a stream of writes.
    if (lastAppliedLevel_ == level) {
        result.reason = "the fan level is already the requested value";
        return result;
    }

    result.attempted = true;
    result.write = bus_.writeRegister(kRegisterFanLevel, static_cast<std::uint8_t>(level));
    if (!result.write.ok) {
        result.reason = "the EC rejected the write: " + result.write.message;
        return result;
    }
    result.succeeded = true;

    // Verify by readback. A write the bus accepted is not the same as a write
    // the EC honoured, and SAFETY.md 6 step 3 requires the result to be checked
    // rather than assumed.
    result.readbackAttempted = true;
    std::uint8_t readback = 0;
    result.readback = bus_.readRegister(kRegisterFanLevel, readback);
    if (!result.readback.ok) {
        result.reason = "the write was issued but the readback failed: "
            + result.readback.message;
        // Deliberately NOT remembered as applied: the desired state was not
        // confirmed, so the next cycle must try again rather than assume success.
        return result;
    }
    result.readbackValue = static_cast<int>(readback);
    result.readbackMatched = result.readbackValue == level;
    if (!result.readbackMatched) {
        result.reason = "readback mismatch: wrote " + std::to_string(level)
            + " but read " + std::to_string(result.readbackValue);
        return result;
    }

    lastAppliedLevel_ = level;
    result.reason = "fan level confirmed";
    return result;
}

// ---------------------------------------------------------------------------
// UI text
// ---------------------------------------------------------------------------

const char* toText(SafetyState state) noexcept
{
    switch (state) {
    case SafetyState::MonitorOnly:      return "monitor-only";
    case SafetyState::Validating:       return "validating";
    case SafetyState::BiosAutomatic:    return "BIOS automatic";
    case SafetyState::Controlled:       return "controlled";
    case SafetyState::ManualOverride:   return "manual override";
    case SafetyState::FailsafeBios:     return "failsafe: BIOS automatic";
    case SafetyState::ThermalEmergency: return "thermal emergency";
    case SafetyState::Stopping:         return "stopping";
    }
    return "unknown";
}

const char* toText(ControlMode mode) noexcept
{
    switch (mode) {
    case ControlMode::MonitorOnly:     return "monitor-only";
    case ControlMode::BiosAutomatic:   return "BIOS automatic";
    case ControlMode::AutomaticCurve:  return "automatic curve";
    case ControlMode::ManualOverride:  return "manual override";
    }
    return "unknown";
}

const char* toText(FanHealth health) noexcept
{
    switch (health) {
    case FanHealth::Unsupported: return "not measured";
    case FanHealth::Unknown:    return "unknown";
    case FanHealth::Healthy:    return "healthy";
    case FanHealth::Suspect:    return "suspect";
    case FanHealth::Failed:     return "failed";
    }
    return "unknown";
}

const char* toText(SensorValidity validity) noexcept
{
    switch (validity) {
    case SensorValidity::Valid:           return "valid";
    case SensorValidity::Missing:         return "missing";
    case SensorValidity::BackendError:    return "backend error";
    case SensorValidity::ProviderInvalid: return "provider error";
    case SensorValidity::Sentinel:        return "sentinel";
    case SensorValidity::OutOfRange:      return "out of range";
    case SensorValidity::Stale:           return "stale";
    case SensorValidity::FutureTimestamp: return "future timestamp";
    }
    return "unknown";
}

std::string describe(const ControllerOutput& output)
{
    std::string text = toText(output.controlMode);
    text += " / ";
    text += toText(output.safetyState);
    if (!output.reasonCode.empty()) {
        text += " (";
        text += output.reasonCode;
        text += ")";
    }
    // The UI must never be able to render an empty status. A blank field reads
    // as "nothing to report", which is the opposite of the truth in every case
    // where the reason code is empty.
    if (text == "unknown / unknown") {
        text = "monitor-only / monitor-only";
    }
    return text;
}

} // namespace core
} // namespace tpfancontrol
