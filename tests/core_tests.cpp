#include "../fancontrol/core/controller.h"
#include "../fancontrol/core/events.h"
#include "../fancontrol/core/sensor_validation.h"

// assert is compiled out by NDEBUG, which Release defines, so a Release build
// would have silently run none of these 20 tests. CHECK is always active.
#include "test_check.h"
#include "fake_backend.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace tpfancontrol::core;

TemperatureSample temperature(int value, std::uint64_t timestamp)
{
    TemperatureSample sample;
    sample.valueC = value;
    sample.timestampMs = timestamp;
    sample.hasValue = true;
    sample.backendOk = true;
    sample.sourceValid = true;
    sample.source = "test";
    return sample;
}

CapabilityReport eligible()
{
    CapabilityReport result;
    result.exactHardwareMatch = true;
    result.profileVerified = true;
    result.topologyVerified = true;
    result.backendReady = true;
    result.restoreAvailable = true;
    return result;
}

CurveConfig testCurve()
{
    CurveConfig config;
    config.minimumLevel = 0;
    config.maximumLevel = 7;
    config.minimumDwellMs = 1000;
    config.emergencyCommand = FanCommand::biosAutomatic();
    config.points = {
        {50, 45, FanCommand::levelCommand(0)},
        {60, 55, FanCommand::levelCommand(3)},
        {70, 65, FanCommand::levelCommand(7)}
    };
    return config;
}

ControllerConfig testControllerConfig()
{
    ControllerConfig config;
    config.curve = testCurve();
    config.sensor.maximumAgeMs = 1000;
    config.startupValidSamples = 2;
    config.manualDurationMs = 1000;
    config.manualMaximumLevel = 7;
    config.emergencyTemperatureC = 80;
    return config;
}

class TestEventSink final : public IEventSink {
public:
    bool accepted = false;
    CoreEvent last;

    bool tryEmit(const CoreEvent& event) noexcept override
    {
        last = event;
        accepted = true;
        return accepted;
    }
};

void testEventInterfaceContract()
{
    TestEventSink sink;
    CoreEvent event;
    event.timestampMs = 1234;
    event.code = EventCode::FailsafeEntered;
    event.severity = EventSeverity::Critical;
    event.hasValue = true;
    event.value = 42;
    CHECK(sink.tryEmit(event));
    CHECK(sink.last.timestampMs == 1234);
    CHECK(sink.last.code == EventCode::FailsafeEntered);
    CHECK(sink.last.severity == EventSeverity::Critical);
    CHECK(sink.last.hasValue && sink.last.value == 42);
}

void testCurveValidationAndHysteresis()
{
    CurveConfig config = testCurve();
    const ValidationResult valid = validateCurve(config);
    CHECK(valid.valid);

    CurveController controller(config);
    CurveDecision decision = controller.update(50, 0, false);
    CHECK(decision.changed);
    CHECK(decision.command == FanCommand::levelCommand(0));

    decision = controller.update(61, 100, false);
    CHECK(decision.heldByDwell);
    CHECK(decision.command == FanCommand::levelCommand(0));

    decision = controller.update(61, 1000, false);
    CHECK(decision.changed);
    CHECK(decision.command == FanCommand::levelCommand(3));

    decision = controller.update(57, 1100, false);
    CHECK(!decision.changed);
    CHECK(decision.command == FanCommand::levelCommand(3));

    decision = controller.update(54, 2000, false);
    CHECK(decision.changed);
    CHECK(decision.command == FanCommand::levelCommand(0));

    config.points[2].command = FanCommand::levelCommand(1);
    const ValidationResult invalid = validateCurve(config);
    CHECK(!invalid.valid);
}

void testSensorValidation()
{
    SensorPolicy policy;
    policy.minimumPlausibleC = 0;
    policy.maximumPlausibleC = 100;
    policy.maximumAgeMs = 100;

    const ValidatedTemperature valid = validateTemperature(temperature(55, 100), 100, policy);
    CHECK(valid.valid);
    CHECK(valid.valueC == 55);

    TemperatureSample sentinel = temperature(55, 100);
    sentinel.sentinel = true;
    CHECK(validateTemperature(sentinel, 100, policy).validity == SensorValidity::Sentinel);

    CHECK(validateTemperature(temperature(55, 0), 101, policy).validity == SensorValidity::Stale);
    CHECK(validateTemperature(temperature(101, 100), 100, policy).validity == SensorValidity::OutOfRange);
    CHECK(validateTemperature(temperature(55, 2000), 100, policy).validity == SensorValidity::FutureTimestamp);

    std::vector<TemperatureSample> samples = {temperature(50, 100), temperature(70, 100)};
    const ValidatedTemperature hottest = hottestValidTemperature(samples, 100, policy);
    CHECK(hottest.valid);
    CHECK(hottest.valueC == 70);
}

void testDefaultDoesNotStartControl()
{
    Controller controller(testControllerConfig());
    ControllerInput input;
    input.capabilities = eligible();
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};

    const ControllerOutput output = controller.update(input);
    CHECK(output.safetyState == SafetyState::BiosAutomatic);
    CHECK(output.controlMode == ControlMode::BiosAutomatic);
    CHECK(output.command == FanCommand::biosAutomatic());
}

void testControllerGatesAndManualExpiry()
{
    Controller controller(testControllerConfig());

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    ControllerOutput output = controller.update(input);
    CHECK(output.safetyState == SafetyState::Validating);
    CHECK(output.command.kind == CommandKind::None);

    input.nowMs = 100;
    input.temperatures = {temperature(50, 100)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::Validating);

    input.nowMs = 200;
    input.temperatures = {temperature(50, 200)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::Controlled);
    CHECK(output.command == FanCommand::levelCommand(0));

    input.nowMs = 300;
    input.temperatures = {temperature(55, 300)};
    input.requestManual = true;
    input.manualLevel = 3;
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::ManualOverride);
    CHECK(output.manualOverrideActive);
    CHECK(output.manualExpiresAtMs == 1300);
    CHECK(output.command == FanCommand::levelCommand(3));

    input.requestManual = false;
    input.nowMs = 1300;
    input.temperatures = {temperature(55, 1300)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::Controlled);
    CHECK(!output.manualOverrideActive);

    input.requestBiosAutomatic = true;
    input.nowMs = 1400;
    input.temperatures = {temperature(55, 1400)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::BiosAutomatic);
    CHECK(output.command == FanCommand::biosAutomatic());
}

void testCapabilityAndFailureGates()
{
    Controller controller(testControllerConfig());
    ControllerInput input;
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};
    input.capabilities = eligible();
    input.requestControl = true;
    input.capabilities.profileVerified = false;
    ControllerOutput output = controller.update(input);
    CHECK(output.safetyState == SafetyState::MonitorOnly);
    CHECK(output.reasonCode == "profile_unverified");
    CHECK(output.command.kind == CommandKind::None);

    Controller running(testControllerConfig());
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};
    running.update(input);
    input.nowMs = 1;
    input.temperatures = {temperature(50, 1)};
    running.update(input);
    input.nowMs = 2;
    input.temperatures = {temperature(50, 2)};
    running.update(input);
    input.backendFailure = true;
    output = running.update(input);
    CHECK(output.safetyState == SafetyState::FailsafeBios);
    CHECK(output.command == FanCommand::biosAutomatic());
}

void testBackendContract()
{
    tpfancontrol::test::FakeBackend backend;
    backend.setValue(0x62, 0x5a);

    const tpfancontrol::core::IoResult read = backend.readPort(0x62);
    CHECK(read.ok);
    CHECK(read.value == 0x5a);

    const tpfancontrol::core::IoResult write = backend.writePort(0x66, 0x80);
    CHECK(write.ok);
    CHECK(backend.operations().size() == 2);
    CHECK(backend.operations()[1].write);

    backend.setReadFailure(true);
    CHECK(!backend.readPort(0x62).ok);
    backend.setReadFailure(false);
    backend.setWriteFailure(true);
    CHECK(!backend.writePort(0x66, 0x81).ok);

    backend.close();
    CHECK(backend.state() == tpfancontrol::core::BackendState::Stopped);
    CHECK(!backend.readPort(0x62).ok);
}

void testEmergencyAndFanHealth()
{
    ControllerConfig config = testControllerConfig();
    config.rpmSupported = true;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};
    controller.update(input);
    input.nowMs = 1;
    input.temperatures = {temperature(50, 1)};
    controller.update(input);

    input.requestManual = true;
    input.manualLevel = 3;
    input.hasFanRpm = true;
    input.rpmObservationElapsed = true;
    input.fanRpm = 0;
    input.nowMs = 2;
    input.temperatures = {temperature(55, 2)};
    ControllerOutput output = controller.update(input);
    CHECK(output.fanHealth == FanHealth::Suspect);

    input.requestManual = false;
    input.nowMs = 3;
    input.temperatures = {temperature(85, 3)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::ThermalEmergency);
    CHECK(output.command == FanCommand::biosAutomatic());

    input.nowMs = 4;
    input.temperatures = {temperature(50, 4)};
    output = controller.update(input);
    CHECK(output.safetyState == SafetyState::ThermalEmergency);
    CHECK(output.reasonCode == "safety_latched");

    input.requestBiosAutomatic = true;
    output = controller.update(input);
    CHECK(output.command == FanCommand::biosAutomatic());
}

void testFanRpmValidation()
{
    const FanRpmPolicy policy;
    // Large enough that the stale cases below subtract without wrapping: an
    // unsigned underflow here would be reported as a future timestamp and the
    // test would pass for the wrong reason.
    const std::uint64_t now = 100000;

    // A reading in range and fresh is valid.
    ValidatedFanRpm result = validateFanRpm(true, 3400, now, now, policy);
    CHECK(result.valid);
    CHECK(result.rpm == 3400);
    CHECK(result.validity == SensorValidity::Valid);
    CHECK(result.reason.empty());

    // 0 RPM is a REAL reading. A stopped fan reads 0, and treating that as "no
    // measurement" would hide the exact condition the monitor exists to catch.
    result = validateFanRpm(true, 0, now, now, policy);
    CHECK(result.valid);
    CHECK(result.rpm == 0);

    // No reading at all is Missing, which is not the same as a bad reading.
    result = validateFanRpm(false, 0, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::Missing);

    // A negative value cannot come from an unsigned register.
    result = validateFanRpm(true, -1, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::ProviderInvalid);

    // The classic defect: a raw byte pair read unsigned. 0xFFFF is also a
    // documented "no measurement" sentinel on some firmware, so it is rejected
    // as a sentinel rather than as a wild speed.
    result = validateFanRpm(true, 65535, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::Sentinel);
    result = validateFanRpm(true, 32768, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::Sentinel);

    // 0xFF00 is not a sentinel but is far above any plausible fan speed: this is
    // the mis-scaled-value case SAFETY.md 6.1 is about.
    result = validateFanRpm(true, 65280, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::OutOfRange);

    // Above the plausible maximum but not a sentinel.
    result = validateFanRpm(true, 20000, now, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::OutOfRange);

    // Age and clock skew.
    result = validateFanRpm(true, 3000, now - 60000, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::Stale);
    result = validateFanRpm(true, 3000, now + 60000, now, policy);
    CHECK(!result.valid);
    CHECK(result.validity == SensorValidity::FutureTimestamp);

    // A small amount of skew is normal clock noise, not an error.
    result = validateFanRpm(true, 3000, now + 100, now, policy);
    CHECK(result.valid);

    // Every rejection explains itself. A reason-less rejection cannot be acted
    // on by anything reading the log.
    // 0 is deliberately absent: it is a valid reading, asserted above.
    const int badValues[] = {65535, 32768, 65280, 20000, -1};
    for (const int raw : badValues) {
        result = validateFanRpm(true, raw, now, now, policy);
        CHECK(!result.valid);
        CHECK(!result.reason.empty());
    }
}

void testImplausibleRpmIsNotReportedHealthy()
{
    // The defect this pins: evaluateFanHealth compared the commanded level
    // against an implausible reading, and a raw 0xFFFF came back Healthy, which
    // is the one answer that must never be reported for a value that cannot be
    // a measurement.
    ControllerConfig config = testControllerConfig();
    config.rpmSupported = true;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};
    controller.update(input);

    input.nowMs = 1;
    input.temperatures = {temperature(50, 1)};
    controller.update(input);

    input.requestManual = true;
    input.manualLevel = 3;
    input.hasFanRpm = true;
    input.rpmObservationElapsed = true;
    input.timestampMs = 1;
    input.nowMs = 2;
    input.temperatures = {temperature(55, 2)};

    const int implausible[] = {65535, 32768, 65280, 20000, -1};
    for (const int rpm : implausible) {
        input.fanRpm = rpm;
        input.temperatures = {temperature(55, static_cast<int>(input.nowMs))};
        const ControllerOutput output = controller.update(input);
        CHECK(output.fanHealth == FanHealth::Failed);
        CHECK(output.fanHealth != FanHealth::Healthy);
    }

    // A plausible reading is Healthy, so the check is not simply refusing
    // everything and reporting Failed.
    input.fanRpm = 3400;
    ControllerOutput output = controller.update(input);
    CHECK(output.fanHealth == FanHealth::Healthy);

    // A genuinely stopped fan under a commanded level stays Suspect, not
    // Failed: it is a real reading of a real problem.
    input.fanRpm = 0;
    input.timestampMs = input.nowMs;
    input.temperatures = {temperature(55, static_cast<int>(input.nowMs))};
    output = controller.update(input);
    CHECK(output.fanHealth == FanHealth::Suspect);

    // A stale reading is not evidence about the fan right now.
    input.fanRpm = 3400;
    input.timestampMs = 0;
    input.nowMs = 60000;
    input.temperatures = {temperature(55, 60000)};
    output = controller.update(input);
    CHECK(output.fanHealth == FanHealth::Failed);
}

// A temperature source that has stopped changing. Models a register that the
// EC no longer updates, which is the classic stuck-sensor failure: the reading
// stays plausible forever, so nothing about the VALUE looks wrong.
class StuckTemperatureSource {
public:
    explicit StuckTemperatureSource(std::string name, int frozenC)
        : name_(std::move(name)), frozenC_(frozenC) {}

    // Each call returns a fresh sample carrying the same value. Freshness is
    // real - only the value is stuck - so a staleness check cannot catch it.
    TemperatureSample sample(std::uint64_t nowMs)
    {
        TemperatureSample sample;
        sample.valueC = frozenC_;
        sample.timestampMs = nowMs;
        sample.hasValue = true;
        sample.backendOk = true;
        sample.sourceValid = true;
        sample.source = name_;
        return sample;
    }

    void unfreeze(int c) { frozenC_ = c; }
    int value() const { return frozenC_; }

private:
    std::string name_;
    int frozenC_;
};

void testStuckTemperatureSource()
{
    SensorPolicy policy;
    policy.minimumPlausibleC = 0;
    policy.maximumPlausibleC = 100;
    policy.maximumAgeMs = 100;

    StuckTemperatureSource stuck("cpu", 45);

    // Every sample is fresh and in range, so validation alone cannot tell this
    // source is stuck. This is the honest position and is asserted explicitly,
    // because pretending otherwise would be the dangerous claim.
    for (std::uint64_t t = 0; t <= 500; t += 50) {
        const ValidatedTemperature validated = validateTemperature(stuck.sample(t), t, policy);
        CHECK(validated.valid);
        CHECK(validated.valueC == 45);
    }

    // The invariant that DOES hold, and the one worth having: a stuck source
    // reporting a moderate temperature cannot mask a genuinely hot source
    // reporting a higher one, because the aggregate is a maximum over sources
    // rather than a reading of any single source.
    StuckTemperatureSource hot("gpu", 88);
    std::vector<TemperatureSample> mixed = {stuck.sample(1000), hot.sample(1000)};
    ValidatedTemperature hottest = hottestValidTemperature(mixed, 1000, policy);
    CHECK(hottest.valid);
    CHECK(hottest.valueC == 88);

    // A stuck source that freezes LOW must not be able to hold the aggregate
    // down either, for the same reason.
    StuckTemperatureSource stuckCold("cpu", 30);
    StuckTemperatureSource warm("gpu", 75);
    mixed = {stuckCold.sample(1000), warm.sample(1000)};
    hottest = hottestValidTemperature(mixed, 1000, policy);
    CHECK(hottest.valid);
    CHECK(hottest.valueC == 75);

    // Once the hot source starts moving again, the aggregate follows it, which
    // is the behaviour the control loop depends on.
    hot.unfreeze(91);
    mixed = {stuck.sample(1100), hot.sample(1100)};
    hottest = hottestValidTemperature(mixed, 1100, policy);
    CHECK(hottest.valueC == 91);

    // The stuck source is still stuck, and still looks perfectly healthy. This
    // is the documented limitation: uniformity across every source cannot be
    // distinguished from a genuinely steady machine using one reading stream.
    CHECK(stuck.sample(2000).valueC == 45);
    CHECK(validateTemperature(stuck.sample(2000), 2000, policy).valid);
}

void testStaleAndFutureTimestampsAreRejected()
{
    SensorPolicy policy;
    policy.minimumPlausibleC = 0;
    policy.maximumPlausibleC = 100;
    policy.maximumAgeMs = 100;
    policy.maximumFutureSkewMs = 50;

    const std::uint64_t now = 100000;

    // Just inside the age limit, then just outside it. Both sides of the
    // boundary are checked, because a boundary that is off by one is a boundary
    // that has never been tested.
    CHECK(validateTemperature(temperature(55, now - 100), now, policy).valid);
    CHECK(validateTemperature(temperature(55, now - 101), now, policy).validity
          == SensorValidity::Stale);

    // Same for clock skew, which comes from the host clock disagreeing with the
    // sample clock rather than from the sensor.
    CHECK(validateTemperature(temperature(55, now + 50), now, policy).valid);
    CHECK(validateTemperature(temperature(55, now + 51), now, policy).validity
          == SensorValidity::FutureTimestamp);

    // A sample with no value at all is Missing, which is not an error state but
    // an absence of information - the distinction the fallback path relies on.
    TemperatureSample empty;
    empty.timestampMs = now;
    empty.backendOk = true;
    empty.source = "cpu";
    CHECK(validateTemperature(empty, now, policy).validity == SensorValidity::Missing);

    // A backend that reported failure is likewise not a valid reading, and must
    // not be mistaken for 0 degrees, which would look like a very cold CPU.
    TemperatureSample failed = temperature(0, now);
    failed.backendOk = false;
    failed.hasValue = true;
    CHECK(validateTemperature(failed, now, policy).validity != SensorValidity::Valid);
    CHECK(!validateTemperature(failed, now, policy).valid);
}

void testConsecutiveSamplesMustAgree()
{
    ControllerConfig config = testControllerConfig();
    config.startupValidSamples = 3;
    config.maximumSensorAgreementDeltaC = 10;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;

    // Three valid samples that do NOT agree: 10, 70, 20 -> diffs 60,50 >10
    input.nowMs = 0;
    input.temperatures = {temperature(10, 0)};
    ControllerOutput out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Validating);

    input.nowMs = 100;
    input.temperatures = {temperature(70, 100)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Validating);

    input.nowMs = 200;
    input.temperatures = {temperature(20, 200)};
    out = controller.update(input);
    // Agreement failed, should reset and stay validating
    CHECK(out.safetyState == SafetyState::Validating);
    CHECK(out.reasonCode == "sensor_agreement_failed");

    // Now three agreeing samples: 40,42,44 -> diffs 2,2 <=10
    input.nowMs = 300;
    input.temperatures = {temperature(40, 300)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Validating);

    input.nowMs = 400;
    input.temperatures = {temperature(42, 400)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Validating);

    input.nowMs = 500;
    input.temperatures = {temperature(44, 500)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Controlled);
}

void testOscillationDoesNotPumpFan()
{
    ControllerConfig config = testControllerConfig();
    config.startupValidSamples = 3;
    config.maximumSensorAgreementDeltaC = 10;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;

    const int sequence[] = {10, 70, 20, 75, 40};
    std::uint64_t now = 0;
    for (int temp : sequence) {
        input.nowMs = now;
        input.temperatures = {temperature(temp, now)};
        ControllerOutput out = controller.update(input);
        // Must never leave Validating, so no fan command
        CHECK(out.safetyState == SafetyState::Validating);
        CHECK(out.command.kind == CommandKind::None);
        now += 100;
    }
}

void testSingleSourceLossContinuesOnOthers()
{
    // hottestValidTemperature takes max over valid sources.
    SensorPolicy policy;
    policy.minimumPlausibleC = 0;
    policy.maximumPlausibleC = 110;
    policy.maximumAgeMs = 10000;
    policy.maximumFutureSkewMs = 1000;

    const std::uint64_t now = 1000;
    TemperatureSample valid1 = temperature(60, now);
    valid1.source = "sensor1";
    TemperatureSample invalid;
    invalid.hasValue = false;
    invalid.source = "sensor2";
    invalid.timestampMs = now;

    std::vector<TemperatureSample> samples = {valid1, invalid};
    ValidatedTemperature hottest = hottestValidTemperature(samples, now, policy);
    CHECK(hottest.valid);
    CHECK(hottest.valueC == 60);

    // Both invalid -> not valid
    samples = {invalid, invalid};
    hottest = hottestValidTemperature(samples, now, policy);
    CHECK(!hottest.valid);
}

void testFailsafeLatchRequiresAckAndCooldown()
{
    ControllerConfig config = testControllerConfig();
    config.startupValidSamples = 2;
    config.failsafeCooldownMs = 1000;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};
    controller.update(input);
    input.nowMs = 100;
    input.temperatures = {temperature(50, 100)};
    ControllerOutput out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Controlled);

    // Trigger failsafe via backend failure
    input.backendFailure = true;
    input.nowMs = 200;
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::FailsafeBios);
    CHECK(out.reasonCode == "backend_failure");

    // Try to clear without ack -> stays latched
    input.backendFailure = false;
    input.nowMs = 300;
    input.temperatures = {temperature(50, 300)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::FailsafeBios);
    CHECK(out.reasonCode == "safety_latched");

    // Ack but no cooldown yet -> cooldown
    input.requestBiosAutomatic = true;
    input.nowMs = 400;
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::FailsafeBios);
    // After ack, reason should be bios or cooldown
    CHECK(out.reasonCode == "safety_latched_bios" || out.reasonCode == "safety_latched_cooldown" || out.reasonCode == "safety_latched_awaiting_valid");

    // Ack + valid temp, but cooldown not elapsed
    input.requestBiosAutomatic = false;
    input.nowMs = 500;
    input.temperatures = {temperature(50, 500)};
    out = controller.update(input);
    // Should be in cooldown
    CHECK(out.safetyState == SafetyState::FailsafeBios);
    CHECK(out.reasonCode == "safety_latched_cooldown" || out.reasonCode == "safety_latched_cooldown_done_awaiting_request" || out.reasonCode == "safety_latched_bios");

    // Advance time past cooldown, but no re-request yet -> awaiting request
    // Clear control request to test awaiting state
    input.requestControl = false;
    input.nowMs = 1600; // 500 + 1100 > 1000 cooldown
    input.temperatures = {temperature(50, 1600)};
    out = controller.update(input);
    CHECK(out.safetyState == SafetyState::FailsafeBios);
    CHECK(out.reasonCode == "safety_latched_cooldown_done_awaiting_request");

    // Now re-request control after cooldown -> should clear latch and go to validating
    input.requestControl = true;
    input.nowMs = 1700;
    input.temperatures = {temperature(50, 1700)};
    out = controller.update(input);
    // After clearing latch, it should be Validating (needs consecutive samples)
    CHECK(out.safetyState == SafetyState::Validating || out.safetyState == SafetyState::Controlled);
}

void testRepeatedSuspectEscalation()
{
    ControllerConfig config = testControllerConfig();
    config.startupValidSamples = 2;
    config.maximumSensorAgreementDeltaC = 20;
    config.rpmSupported = true;
    config.suspectThreshold = 3;
    config.fanRpm.maximumPlausible = 10000;
    config.fanRpm.minimumPlausible = 0;
    config.fanRpm.maximumAgeMs = 10000;
    config.fanRpm.maximumFutureSkewMs = 1000;
    Controller controller(config);

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    input.temperatures = {temperature(60, 0)};
    controller.update(input);
    input.nowMs = 100;
    input.temperatures = {temperature(60, 100)};
    ControllerOutput out = controller.update(input);
    CHECK(out.safetyState == SafetyState::Controlled);
    // At 60C, curve gives level 3 (>0)

    // Now simulate fan commanded >0 but RPM 0, with observation elapsed
    input.hasFanRpm = true;
    input.fanRpm = 0;
    input.timestampMs = input.nowMs;
    input.rpmObservationElapsed = true;

    // First suspect
    input.nowMs = 200;
    input.temperatures = {temperature(60, 200)};
    input.timestampMs = input.nowMs;
    out = controller.update(input);
    CHECK(out.fanHealth == FanHealth::Suspect);

    // Second suspect
    input.nowMs = 300;
    input.temperatures = {temperature(60, 300)};
    input.timestampMs = input.nowMs;
    out = controller.update(input);
    CHECK(out.fanHealth == FanHealth::Suspect);

    // Third suspect -> should escalate to Failed and trigger failsafe
    input.nowMs = 400;
    input.temperatures = {temperature(60, 400)};
    input.timestampMs = input.nowMs;
    out = controller.update(input);
    CHECK(out.fanHealth == FanHealth::Failed);
    CHECK(out.safetyState == SafetyState::FailsafeBios);
}

} // namespace

int main()
{
    testEventInterfaceContract();
    testCurveValidationAndHysteresis();
    testSensorValidation();
    testDefaultDoesNotStartControl();
    testControllerGatesAndManualExpiry();
    testCapabilityAndFailureGates();
    testBackendContract();
    testEmergencyAndFanHealth();
    testFanRpmValidation();
    testImplausibleRpmIsNotReportedHealthy();
    testStuckTemperatureSource();
    testStaleAndFutureTimestampsAreRejected();
    testConsecutiveSamplesMustAgree();
    testOscillationDoesNotPumpFan();
    testSingleSourceLossContinuesOnOthers();
    testFailsafeLatchRequiresAckAndCooldown();
    testRepeatedSuspectEscalation();
    std::cout << "TPFanControl portable core tests passed\n";
    return 0;
}
