#include "controller.h"

namespace tpfancontrol {
namespace core {

bool CapabilityReport::controlEligible() const noexcept
{
    return exactHardwareMatch &&
           profileVerified &&
           topologyVerified &&
           backendReady &&
           restoreAvailable;
}

std::string CapabilityReport::blockingReason() const
{
    if (!exactHardwareMatch) {
        return "hardware_identity_unverified";
    }
    if (!profileVerified) {
        return "profile_unverified";
    }
    if (!topologyVerified) {
        return "fan_topology_unverified";
    }
    if (!backendReady) {
        return "backend_unavailable";
    }
    if (!restoreAvailable) {
        return "bios_restore_unverified";
    }
    return "eligible";
}

Controller::Controller(ControllerConfig config)
    : config_(config),
      curve_(config_.curve),
      state_(SafetyState::MonitorOnly),
      manualActive_(false),
      manualLevel_(-1),
      manualExpiresAtMs_(0),
      consecutiveValidSamples_(0),
      safetyLatched_(false),
      controlRequested_(false),
      latchedCommand_(FanCommand::none()),
      lastCommand_(FanCommand::none())
{
}

const ValidationResult& Controller::curveValidation() const noexcept
{
    return curve_.validation();
}

void Controller::reset() noexcept
{
    curve_.reset();
    state_ = SafetyState::MonitorOnly;
    manualActive_ = false;
    manualLevel_ = -1;
    manualExpiresAtMs_ = 0;
    consecutiveValidSamples_ = 0;
    safetyLatched_ = false;
    controlRequested_ = false;
    latchedCommand_ = FanCommand::none();
    lastCommand_ = FanCommand::none();
}

ControllerOutput Controller::makeBaseOutput(
    const ControllerInput& input,
    const ValidatedTemperature& temperature) const
{
    ControllerOutput output;
    output.hasValidTemperature = temperature.valid;
    output.maximumTemperatureC = temperature.valueC;
    output.temperatureValidity = temperature.validity;
    output.temperatureSource = temperature.source;
    output.manualOverrideActive = manualActive_;
    output.manualExpiresAtMs = manualExpiresAtMs_;
    output.fanHealth = FanHealth::Unknown;
    (void)input;
    return output;
}

void Controller::setCommand(ControllerOutput& output, const FanCommand& command) const
{
    output.command = command;
    output.commandChanged = command != lastCommand_;
    output.commandMayBeIssued = command.kind != CommandKind::None;
}

void Controller::enterFailsafe(
    ControllerOutput& output,
    const ControllerInput& input,
    const char* reason)
{
    manualActive_ = false;
    manualLevel_ = -1;
    manualExpiresAtMs_ = 0;
    safetyLatched_ = true;
    latchedCommand_ = input.capabilities.restoreAvailable
        ? FanCommand::biosAutomatic()
        : FanCommand::none();
    state_ = SafetyState::FailsafeBios;
    output.safetyState = state_;
    output.controlMode = ControlMode::BiosAutomatic;
    output.manualOverrideActive = false;
    output.manualExpiresAtMs = 0;
    output.reasonCode = reason;

    setCommand(output, latchedCommand_);
    if (latchedCommand_.kind == CommandKind::None) {
        output.commandMayBeIssued = false;
    }
}

FanHealth Controller::evaluateFanHealth(
    const ControllerInput& input,
    const FanCommand& command) const
{
    if (!config_.rpmSupported) {
        return FanHealth::Unsupported;
    }

    if (!input.hasFanRpm || !input.rpmObservationElapsed) {
        return FanHealth::Unknown;
    }

    if (input.fanRpm < 0) {
        return FanHealth::Suspect;
    }

    if (command.kind == CommandKind::Level && command.level > 0 && input.fanRpm == 0) {
        return FanHealth::Suspect;
    }

    return FanHealth::Healthy;
}

ControllerOutput Controller::update(const ControllerInput& input)
{
    const ValidatedTemperature temperature = hottestValidTemperature(
        input.temperatures,
        input.nowMs,
        config_.sensor);
    ControllerOutput output = makeBaseOutput(input, temperature);

    if (input.requestBiosAutomatic || input.cancelManual) {
        controlRequested_ = false;
    } else if (input.requestControl || input.requestManual) {
        controlRequested_ = true;
    }

    const bool wasControlling =
        state_ == SafetyState::Controlled ||
        state_ == SafetyState::ManualOverride ||
        state_ == SafetyState::ThermalEmergency;

    if (input.shutdown) {
        controlRequested_ = false;
        manualActive_ = false;
        manualLevel_ = -1;
        manualExpiresAtMs_ = 0;
        state_ = SafetyState::Stopping;
        output.safetyState = state_;
        output.controlMode = ControlMode::BiosAutomatic;
        output.reasonCode = "shutdown_restore";
        if (input.capabilities.restoreAvailable) {
            setCommand(output, FanCommand::biosAutomatic());
        } else {
            setCommand(output, FanCommand::none());
            output.commandMayBeIssued = false;
        }
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (!curve_.validation().valid) {
        state_ = SafetyState::MonitorOnly;
        output.safetyState = state_;
        output.controlMode = ControlMode::MonitorOnly;
        output.reasonCode = "invalid_curve";
        setCommand(output, FanCommand::none());
        lastCommand_ = output.command;
        return output;
    }

    if (input.backendFailure || input.writeFailure || input.readbackMismatch || input.fanResponseFailed) {
        enterFailsafe(output, input,
            input.backendFailure ? "backend_failure" :
            input.writeFailure ? "write_failure" :
            input.readbackMismatch ? "readback_mismatch" :
            "fan_response_failure");
        output.fanHealth = input.fanResponseFailed ? FanHealth::Failed :
                           evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (!input.capabilities.controlEligible()) {
        if (wasControlling && input.capabilities.restoreAvailable) {
            enterFailsafe(output, input, input.capabilities.blockingReason().c_str());
        } else {
            manualActive_ = false;
            manualLevel_ = -1;
            manualExpiresAtMs_ = 0;
            consecutiveValidSamples_ = 0;
            state_ = SafetyState::MonitorOnly;
            output.safetyState = state_;
            output.controlMode = ControlMode::MonitorOnly;
            output.reasonCode = input.capabilities.blockingReason();
            setCommand(output, FanCommand::none());
        }
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (safetyLatched_) {
        output.safetyState = state_;
        output.manualOverrideActive = false;
        output.manualExpiresAtMs = 0;
        output.controlMode = latchedCommand_.kind == CommandKind::BiosAutomatic
            ? ControlMode::BiosAutomatic
            : ControlMode::MonitorOnly;
        output.reasonCode = input.requestBiosAutomatic || input.cancelManual
            ? "safety_latched_bios"
            : "safety_latched";
        const FanCommand command = (input.requestBiosAutomatic || input.cancelManual) &&
                                   input.capabilities.restoreAvailable
            ? FanCommand::biosAutomatic()
            : latchedCommand_;
        setCommand(output, command);
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (!controlRequested_) {
        manualActive_ = false;
        manualLevel_ = -1;
        manualExpiresAtMs_ = 0;
        state_ = SafetyState::BiosAutomatic;
        output.safetyState = state_;
        output.controlMode = ControlMode::BiosAutomatic;
        output.manualOverrideActive = false;
        output.manualExpiresAtMs = 0;
        output.reasonCode = input.cancelManual
            ? "manual_cancelled"
            : input.requestBiosAutomatic
            ? "bios_requested"
            : "control_not_requested";
        setCommand(output, FanCommand::biosAutomatic());
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (!temperature.valid) {
        consecutiveValidSamples_ = 0;
        if (wasControlling && input.capabilities.restoreAvailable) {
            enterFailsafe(output, input, "temperature_invalid");
        } else {
            manualActive_ = false;
            manualLevel_ = -1;
            manualExpiresAtMs_ = 0;
            state_ = SafetyState::Validating;
            output.safetyState = state_;
            output.controlMode = ControlMode::MonitorOnly;
            output.reasonCode = "temperature_invalid";
            setCommand(output, FanCommand::none());
        }
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    ++consecutiveValidSamples_;
    if (consecutiveValidSamples_ < config_.startupValidSamples) {
        state_ = SafetyState::Validating;
        output.safetyState = state_;
        output.controlMode = ControlMode::MonitorOnly;
        output.reasonCode = "startup_validation";
        setCommand(output, FanCommand::none());
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    const bool emergency = config_.emergencyTemperatureC > 0 &&
                           temperature.valueC >= config_.emergencyTemperatureC;
    if (emergency) {
        manualActive_ = false;
        manualLevel_ = -1;
        manualExpiresAtMs_ = 0;
        state_ = SafetyState::ThermalEmergency;
        output.safetyState = state_;
        output.controlMode = ControlMode::AutomaticCurve;
        output.reasonCode = "emergency_temperature";
        const CurveDecision decision = curve_.update(temperature.valueC, input.nowMs, true);
        safetyLatched_ = true;
        latchedCommand_ = decision.command;
        setCommand(output, decision.command);
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return output;
    }

    if (manualActive_ && input.nowMs >= manualExpiresAtMs_) {
        manualActive_ = false;
        manualLevel_ = -1;
        manualExpiresAtMs_ = 0;
        output.reasonCode = "manual_expired";
    }

    if (input.requestManual) {
        const bool levelValid =
            input.manualLevel >= config_.curve.minimumLevel &&
            input.manualLevel <= config_.manualMaximumLevel &&
            input.manualLevel <= config_.curve.maximumLevel;
        if (!levelValid) {
            enterFailsafe(output, input, "manual_level_invalid");
            output.fanHealth = evaluateFanHealth(input, output.command);
            lastCommand_ = output.command;
            return output;
        }

        manualActive_ = true;
        manualLevel_ = input.manualLevel;
        manualExpiresAtMs_ = input.nowMs + config_.manualDurationMs;
    }

    if (manualActive_) {
        state_ = SafetyState::ManualOverride;
        output.safetyState = state_;
        output.controlMode = ControlMode::ManualOverride;
        output.reasonCode = "manual_override";
        setCommand(output, FanCommand::levelCommand(manualLevel_));
        output.manualOverrideActive = true;
        output.manualExpiresAtMs = manualExpiresAtMs_;
    } else {
        state_ = SafetyState::Controlled;
        output.safetyState = state_;
        output.controlMode = ControlMode::AutomaticCurve;
        output.manualOverrideActive = false;
        output.manualExpiresAtMs = 0;
        if (output.reasonCode.empty()) {
            output.reasonCode = "curve_control";
        }
        const CurveDecision decision = curve_.update(temperature.valueC, input.nowMs, false);
        setCommand(output, decision.command);
    }

    output.fanHealth = evaluateFanHealth(input, output.command);
    lastCommand_ = output.command;
    return output;
}

} // namespace core
} // namespace tpfancontrol
