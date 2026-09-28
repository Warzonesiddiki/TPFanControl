#pragma once

#include "fan_curve.h"
#include "sensor_validation.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

struct CapabilityReport {
    bool exactHardwareMatch = false;
    bool profileVerified = false;
    bool topologyVerified = false;
    bool backendReady = false;
    bool restoreAvailable = false;

    bool controlEligible() const noexcept;
    std::string blockingReason() const;
};

struct ControllerConfig {
    CurveConfig curve;
    SensorPolicy sensor;
    std::uint32_t startupValidSamples = 3;
    std::uint64_t manualDurationMs = 15U * 60U * 1000U;
    int manualMaximumLevel = 7;
    int emergencyTemperatureC = 0;
    bool rpmSupported = false;
};

struct ControllerInput {
    std::uint64_t nowMs = 0;
    std::vector<TemperatureSample> temperatures;
    CapabilityReport capabilities;

    bool requestBiosAutomatic = false;
    bool requestControl = false;
    bool requestManual = false;
    int manualLevel = -1;
    bool cancelManual = false;

    bool shutdown = false;
    bool backendFailure = false;
    bool writeFailure = false;
    bool readbackMismatch = false;
    bool fanResponseFailed = false;

    bool hasFanRpm = false;
    int fanRpm = 0;
    bool rpmObservationElapsed = false;
};

struct ControllerOutput {
    SafetyState safetyState = SafetyState::MonitorOnly;
    ControlMode controlMode = ControlMode::MonitorOnly;
    FanCommand command = FanCommand::none();
    bool commandChanged = false;
    bool commandMayBeIssued = false;

    bool manualOverrideActive = false;
    std::uint64_t manualExpiresAtMs = 0;

    bool hasValidTemperature = false;
    int maximumTemperatureC = 0;
    SensorValidity temperatureValidity = SensorValidity::Missing;
    std::string temperatureSource;

    FanHealth fanHealth = FanHealth::Unknown;
    std::string reasonCode;
};

class Controller {
public:
    explicit Controller(ControllerConfig config);

    const ValidationResult& curveValidation() const noexcept;
    ControllerOutput update(const ControllerInput& input);
    void reset() noexcept;

private:
    ControllerConfig config_;
    CurveController curve_;
    SafetyState state_;
    bool manualActive_;
    int manualLevel_;
    std::uint64_t manualExpiresAtMs_;
    std::uint32_t consecutiveValidSamples_;
    bool safetyLatched_;
    bool controlRequested_;
    FanCommand latchedCommand_;
    FanCommand lastCommand_;

    ControllerOutput makeBaseOutput(
        const ControllerInput& input,
        const ValidatedTemperature& temperature) const;
    void setCommand(ControllerOutput& output, const FanCommand& command) const;
    void enterFailsafe(
        ControllerOutput& output,
        const ControllerInput& input,
        const char* reason);
    FanHealth evaluateFanHealth(
        const ControllerInput& input,
        const FanCommand& command) const;
};

} // namespace core
} // namespace tpfancontrol
