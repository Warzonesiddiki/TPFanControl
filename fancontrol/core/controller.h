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

    // Somebody with the machine said monitor-only. It outranks the Phase 0
    // verdicts rather than being one of them: a verified machine can still be
    // held in monitor-only mode by the person using it, and nothing else may
    // override that.
    //
    // This field existed as an input to the legacy capability mapping long
    // before anything read it, which made "the user chose monitor only" a
    // comment rather than a rule. T5-04 made controlEligible() honour it.
    bool monitorOnlyForced = false;

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

    // Plausibility bounds for tachometer readings. Only consulted when
    // rpmSupported is true. See FanRpmPolicy: these are candidate values.
    FanRpmPolicy fanRpm;
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

    // When the tachometer reading was taken. A reading with no timestamp cannot
    // be shown to be fresh, and an unaged reading is exactly what a stuck or
    // replayed sample looks like, so it is required before the value is trusted.
    std::uint64_t timestampMs = 0;
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

    // Replaces the temperature curve (T3-04).
    //
    // The application lets the user switch between two configured smart
    // profiles at run time, and the legacy code did the switching by copying
    // one table over another. The curve the core runs has to follow, or the
    // selected profile would quietly stop being the one in force - a
    // behavioural difference introduced by moving the decision into the core,
    // which is exactly the kind of regression this task must not create.
    //
    // The config is installed verbatim and validated by the CurveController
    // that is built from it. An invalid curve is *not* rejected here and does
    // not fall back to the previous one: `update` refuses to control while the
    // curve is invalid, which stops the fan rather than continuing to run a
    // curve the user has replaced. The returned validation says which rows were
    // wrong, so the caller can put it in the log.
    //
    // This changes thresholds only. Capabilities, the safety state and the
    // manual override are untouched: the curve is not a safety state, and a
    // configuration change must never be able to enable control.
    ValidationResult setCurve(const CurveConfig& config);

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
