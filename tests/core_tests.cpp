#include "../fancontrol/core/controller.h"
#include "fake_backend.h"

#include <cassert>
#include <iostream>
#include <string>
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

void testCurveValidationAndHysteresis()
{
    CurveConfig config = testCurve();
    const ValidationResult valid = validateCurve(config);
    assert(valid.valid);

    CurveController controller(config);
    CurveDecision decision = controller.update(50, 0, false);
    assert(decision.changed);
    assert(decision.command == FanCommand::levelCommand(0));

    decision = controller.update(61, 100, false);
    assert(decision.heldByDwell);
    assert(decision.command == FanCommand::levelCommand(0));

    decision = controller.update(61, 1000, false);
    assert(decision.changed);
    assert(decision.command == FanCommand::levelCommand(3));

    decision = controller.update(57, 1100, false);
    assert(!decision.changed);
    assert(decision.command == FanCommand::levelCommand(3));

    decision = controller.update(54, 2000, false);
    assert(decision.changed);
    assert(decision.command == FanCommand::levelCommand(0));

    config.points[2].command = FanCommand::levelCommand(1);
    const ValidationResult invalid = validateCurve(config);
    assert(!invalid.valid);
}

void testSensorValidation()
{
    SensorPolicy policy;
    policy.minimumPlausibleC = 0;
    policy.maximumPlausibleC = 100;
    policy.maximumAgeMs = 100;

    const ValidatedTemperature valid = validateTemperature(temperature(55, 100), 100, policy);
    assert(valid.valid);
    assert(valid.valueC == 55);

    TemperatureSample sentinel = temperature(55, 100);
    sentinel.sentinel = true;
    assert(validateTemperature(sentinel, 100, policy).validity == SensorValidity::Sentinel);

    assert(validateTemperature(temperature(55, 0), 101, policy).validity == SensorValidity::Stale);
    assert(validateTemperature(temperature(101, 100), 100, policy).validity == SensorValidity::OutOfRange);
    assert(validateTemperature(temperature(55, 2000), 100, policy).validity == SensorValidity::FutureTimestamp);

    std::vector<TemperatureSample> samples = {temperature(50, 100), temperature(70, 100)};
    const ValidatedTemperature hottest = hottestValidTemperature(samples, 100, policy);
    assert(hottest.valid);
    assert(hottest.valueC == 70);
}

void testDefaultDoesNotStartControl()
{
    Controller controller(testControllerConfig());
    ControllerInput input;
    input.capabilities = eligible();
    input.nowMs = 0;
    input.temperatures = {temperature(50, 0)};

    const ControllerOutput output = controller.update(input);
    assert(output.safetyState == SafetyState::BiosAutomatic);
    assert(output.controlMode == ControlMode::BiosAutomatic);
    assert(output.command == FanCommand::biosAutomatic());
}

void testControllerGatesAndManualExpiry()
{
    Controller controller(testControllerConfig());

    ControllerInput input;
    input.capabilities = eligible();
    input.requestControl = true;
    input.nowMs = 0;
    ControllerOutput output = controller.update(input);
    assert(output.safetyState == SafetyState::Validating);
    assert(output.command.kind == CommandKind::None);

    input.nowMs = 100;
    input.temperatures = {temperature(50, 100)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::Validating);

    input.nowMs = 200;
    input.temperatures = {temperature(50, 200)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::Controlled);
    assert(output.command == FanCommand::levelCommand(0));

    input.nowMs = 300;
    input.temperatures = {temperature(55, 300)};
    input.requestManual = true;
    input.manualLevel = 3;
    output = controller.update(input);
    assert(output.safetyState == SafetyState::ManualOverride);
    assert(output.manualOverrideActive);
    assert(output.manualExpiresAtMs == 1300);
    assert(output.command == FanCommand::levelCommand(3));

    input.requestManual = false;
    input.nowMs = 1300;
    input.temperatures = {temperature(55, 1300)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::Controlled);
    assert(!output.manualOverrideActive);

    input.requestBiosAutomatic = true;
    input.nowMs = 1400;
    input.temperatures = {temperature(55, 1400)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::BiosAutomatic);
    assert(output.command == FanCommand::biosAutomatic());
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
    assert(output.safetyState == SafetyState::MonitorOnly);
    assert(output.reasonCode == "profile_unverified");
    assert(output.command.kind == CommandKind::None);

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
    assert(output.safetyState == SafetyState::FailsafeBios);
    assert(output.command == FanCommand::biosAutomatic());
}

void testBackendContract()
{
    tpfancontrol::test::FakeBackend backend;
    backend.setValue(0x62, 0x5a);

    const tpfancontrol::core::IoResult read = backend.readPort(0x62);
    assert(read.ok);
    assert(read.value == 0x5a);

    const tpfancontrol::core::IoResult write = backend.writePort(0x66, 0x80);
    assert(write.ok);
    assert(backend.operations().size() == 2);
    assert(backend.operations()[1].write);

    backend.setReadFailure(true);
    assert(!backend.readPort(0x62).ok);
    backend.setReadFailure(false);
    backend.setWriteFailure(true);
    assert(!backend.writePort(0x66, 0x81).ok);

    backend.close();
    assert(backend.state() == tpfancontrol::core::BackendState::Stopped);
    assert(!backend.readPort(0x62).ok);
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
    assert(output.fanHealth == FanHealth::Suspect);

    input.requestManual = false;
    input.nowMs = 3;
    input.temperatures = {temperature(85, 3)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::ThermalEmergency);
    assert(output.command == FanCommand::biosAutomatic());

    input.nowMs = 4;
    input.temperatures = {temperature(50, 4)};
    output = controller.update(input);
    assert(output.safetyState == SafetyState::ThermalEmergency);
    assert(output.reasonCode == "safety_latched");

    input.requestBiosAutomatic = true;
    output = controller.update(input);
    assert(output.command == FanCommand::biosAutomatic());
}

} // namespace

int main()
{
    testCurveValidationAndHysteresis();
    testSensorValidation();
    testDefaultDoesNotStartControl();
    testControllerGatesAndManualExpiry();
    testCapabilityAndFailureGates();
    testBackendContract();
    testEmergencyAndFanHealth();
    std::cout << "TPFanControl portable core tests passed\n";
    return 0;
}
