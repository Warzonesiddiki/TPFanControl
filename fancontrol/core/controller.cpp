#include "controller.h"

#include "sensor_validation.h"

namespace tpfancontrol {
namespace core {

bool CapabilityReport::controlEligible() const noexcept
{
    // First, because it is a decision rather than an outstanding obligation:
    // a machine somebody has put in monitor-only mode is not "nearly eligible".
    if (monitorOnlyForced) {
        return false;
    }
    return exactHardwareMatch &&
           profileVerified &&
           topologyVerified &&
           backendReady &&
           restoreAvailable;
}

ValidationResult Controller::setCurve(const CurveConfig& config)
{
    config_.curve = config;
    curve_ = CurveController(config);

    // A fresh curve starts uninitialised, so the next update selects an index
    // from the temperature in front of it rather than carrying an index that
    // belonged to the previous thresholds.
    curve_.reset();

    return curve_.validation();
}

std::string CapabilityReport::blockingReason() const
{
    if (monitorOnlyForced) {
        return "monitor_only_forced";
    }
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
    recentMaxTemps_.clear();
    failsafeAcked_ = false;
    failsafeAckTimeMs_ = 0;
    failsafeCooldownStartMs_ = 0;
    consecutiveSuspectSamples_ = 0;
    hasReportedMaximumTemperature_ = false;
    reportedMaximumTemperatureC_ = 0;
    hasReportedSafetyState_ = false;
    reportedSafetyState_ = SafetyState::MonitorOnly;
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
    failsafeAcked_ = false;
    failsafeAckTimeMs_ = 0;
    failsafeCooldownStartMs_ = 0;
    consecutiveValidSamples_ = 0;
    recentMaxTemps_.clear();
    consecutiveSuspectSamples_ = 0;
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
    const FanCommand& command)
{
    if (!config_.rpmSupported) {
        consecutiveSuspectSamples_ = 0;
        return FanHealth::Unsupported;
    }

    if (!input.hasFanRpm || !input.rpmObservationElapsed) {
        // No observation yet, do not count as suspect.
        return FanHealth::Unknown;
    }

    // A reading that cannot be a real measurement is rejected before any
    // comparison against the commanded level, so a mis-scaled value cannot be
    // reported as a healthy fan. SAFETY.md 6.1.
    const ValidatedFanRpm rpm = validateFanRpm(
        input.hasFanRpm, input.fanRpm, input.timestampMs, input.nowMs, config_.fanRpm);

    if (!rpm.valid) {
        // Missing is the absence of information, not a fault. An implausible
        // value is a fault in the measurement path.
        if (rpm.validity == SensorValidity::Missing) {
            return FanHealth::Unknown;
        }
        consecutiveSuspectSamples_ = 0;
        return FanHealth::Failed;
    }

    if (command.kind == CommandKind::Level && command.level > 0 && rpm.rpm == 0) {
        ++consecutiveSuspectSamples_;
        if (consecutiveSuspectSamples_ >= config_.suspectThreshold) {
            return FanHealth::Failed;
        }
        return FanHealth::Suspect;
    }

    consecutiveSuspectSamples_ = 0;
    return FanHealth::Healthy;
}

ControllerOutput Controller::update(const ControllerInput& input)
{
    const ValidatedTemperature temperature = hottestValidTemperature(
        input.temperatures,
        input.nowMs,
        config_.sensor);
    ControllerOutput output = makeBaseOutput(input, temperature);
    const auto finish = [&]() {
        const bool stoppingTransition = !hasReportedSafetyState_ ||
            reportedSafetyState_ != SafetyState::Stopping;
        if (temperature.valid && (!hasReportedMaximumTemperature_ ||
            temperature.valueC > reportedMaximumTemperatureC_)) {
            CoreEvent event;
            event.timestampMs = input.nowMs;
            event.code = EventCode::MaximumTemperatureObserved;
            event.severity = EventSeverity::Notice;
            event.hasValue = true;
            event.value = temperature.valueC;
            output.events.push(event);
            hasReportedMaximumTemperature_ = true;
            reportedMaximumTemperatureC_ = temperature.valueC;
        }
        if (!hasReportedSafetyState_ || output.safetyState != reportedSafetyState_) {
            if (output.safetyState == SafetyState::FailsafeBios) {
                CoreEvent event;
                event.timestampMs = input.nowMs;
                event.code = EventCode::FailsafeEntered;
                event.severity = EventSeverity::Critical;
                event.hasValue = true;
                event.value = static_cast<std::int32_t>(
                    input.backendFailure ? EventCause::BackendFailure :
                    input.writeFailure ? EventCause::WriteFailure :
                    input.readbackMismatch ? EventCause::ReadbackMismatch :
                    input.fanResponseFailed ? EventCause::FanResponseFailure :
                    output.reasonCode == "repeated_suspect_fan" ? EventCause::RepeatedSuspectFan :
                    output.reasonCode == "temperature_invalid" ? EventCause::InvalidTemperature :
                    output.reasonCode == "manual_level_invalid" ? EventCause::InvalidManualLevel :
                    EventCause::HardwareIneligible);
                output.events.push(event);
            } else if (output.safetyState == SafetyState::ThermalEmergency) {
                CoreEvent event;
                event.timestampMs = input.nowMs;
                event.code = EventCode::ThermalEmergency;
                event.severity = EventSeverity::Critical;
                event.hasValue = temperature.valid;
                event.value = temperature.valueC;
                output.events.push(event);
            }
            reportedSafetyState_ = output.safetyState;
            hasReportedSafetyState_ = true;
        }
        if (output.commandChanged && output.commandMayBeIssued) {
            CoreEvent event;
            event.timestampMs = input.nowMs;
            event.code = EventCode::FanCommand;
            event.severity = EventSeverity::Info;
            event.hasValue = true;
            event.value = static_cast<std::int32_t>(output.command.kind);
            event.detail = output.command.level;
            output.events.push(event);
        }
        if (input.shutdown && stoppingTransition) {
            CoreEvent event;
            event.timestampMs = input.nowMs;
            event.code = EventCode::ShutdownRestore;
            event.severity = output.commandMayBeIssued ? EventSeverity::Notice : EventSeverity::Error;
            event.hasValue = output.commandMayBeIssued;
            event.value = static_cast<std::int32_t>(output.command.kind);
            output.events.push(event);
        }
        return output;
    };

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
        return finish();
    }

    if (!curve_.validation().valid) {
        state_ = SafetyState::MonitorOnly;
        output.safetyState = state_;
        output.controlMode = ControlMode::MonitorOnly;
        output.reasonCode = "invalid_curve";
        setCommand(output, FanCommand::none());
        lastCommand_ = output.command;
        return finish();
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
        return finish();
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
        return finish();
    }

    if (safetyLatched_) {
        // T2-03 / ADR-030: failsafe recovery requires explicit ack + cooldown + re-request.
        const bool ack = input.requestBiosAutomatic || input.cancelManual;
        if (ack && !failsafeAcked_) {
            failsafeAcked_ = true;
            failsafeAckTimeMs_ = input.nowMs;
            failsafeCooldownStartMs_ = 0;
        }

        if (failsafeAcked_) {
            if (temperature.valid) {
                if (failsafeCooldownStartMs_ == 0) {
                    failsafeCooldownStartMs_ = input.nowMs;
                }
                const bool cooldownDone = (input.nowMs >= failsafeCooldownStartMs_) &&
                    (input.nowMs - failsafeCooldownStartMs_ >= config_.failsafeCooldownMs);
                if (cooldownDone) {
                    if (input.requestControl || input.requestManual) {
                        // Clear latch and start fresh validation.
                        safetyLatched_ = false;
                        failsafeAcked_ = false;
                        failsafeAckTimeMs_ = 0;
                        failsafeCooldownStartMs_ = 0;
                        consecutiveValidSamples_ = 0;
                        recentMaxTemps_.clear();
                        consecutiveSuspectSamples_ = 0;
                        state_ = SafetyState::Validating;
                        // Fall through to normal handling.
                    } else {
                        output.safetyState = state_;
                        output.manualOverrideActive = false;
                        output.manualExpiresAtMs = 0;
                        output.controlMode = ControlMode::BiosAutomatic;
                        output.reasonCode = "safety_latched_cooldown_done_awaiting_request";
                        const FanCommand cmd = input.capabilities.restoreAvailable
                            ? FanCommand::biosAutomatic()
                            : FanCommand::none();
                        setCommand(output, cmd);
                        if (latchedCommand_.kind == CommandKind::None) {
                            output.commandMayBeIssued = false;
                        }
                        output.fanHealth = evaluateFanHealth(input, output.command);
                        lastCommand_ = output.command;
                        return finish();
                    }
                } else {
                    output.safetyState = state_;
                    output.manualOverrideActive = false;
                    output.manualExpiresAtMs = 0;
                    output.controlMode = latchedCommand_.kind == CommandKind::BiosAutomatic
                        ? ControlMode::BiosAutomatic
                        : ControlMode::MonitorOnly;
                    output.reasonCode = "safety_latched_cooldown";
                    const FanCommand command = input.capabilities.restoreAvailable
                        ? FanCommand::biosAutomatic()
                        : latchedCommand_;
                    setCommand(output, command);
                    if (latchedCommand_.kind == CommandKind::None) {
                        output.commandMayBeIssued = false;
                    }
                    output.fanHealth = evaluateFanHealth(input, output.command);
                    lastCommand_ = output.command;
                    return finish();
                }
            } else {
                // Acked but no valid temp yet, reset cooldown start.
                failsafeCooldownStartMs_ = 0;
                output.safetyState = state_;
                output.manualOverrideActive = false;
                output.manualExpiresAtMs = 0;
                output.controlMode = latchedCommand_.kind == CommandKind::BiosAutomatic
                    ? ControlMode::BiosAutomatic
                    : ControlMode::MonitorOnly;
                output.reasonCode = "safety_latched_awaiting_valid";
                const FanCommand command = input.capabilities.restoreAvailable
                    ? FanCommand::biosAutomatic()
                    : latchedCommand_;
                setCommand(output, command);
                if (latchedCommand_.kind == CommandKind::None) {
                    output.commandMayBeIssued = false;
                }
                output.fanHealth = evaluateFanHealth(input, output.command);
                lastCommand_ = output.command;
                return finish();
            }
        }

        // No ack yet, or ack handling fell through after clearing latch? If still latched, report.
        if (safetyLatched_) {
            output.safetyState = state_;
            output.manualOverrideActive = false;
            output.manualExpiresAtMs = 0;
            output.controlMode = latchedCommand_.kind == CommandKind::BiosAutomatic
                ? ControlMode::BiosAutomatic
                : ControlMode::MonitorOnly;
            output.reasonCode = ack
                ? "safety_latched_bios"
                : "safety_latched";
            const FanCommand command = ack && input.capabilities.restoreAvailable
                ? FanCommand::biosAutomatic()
                : latchedCommand_;
            setCommand(output, command);
            if (latchedCommand_.kind == CommandKind::None) {
                output.commandMayBeIssued = false;
            }
            output.fanHealth = evaluateFanHealth(input, output.command);
            lastCommand_ = output.command;
            return finish();
        }
        // Latch cleared, continue to normal handling below.
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
        return finish();
    }

    if (!temperature.valid) {
        consecutiveValidSamples_ = 0;
        recentMaxTemps_.clear();
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
        return finish();
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
        failsafeAcked_ = false;
        failsafeAckTimeMs_ = 0;
        failsafeCooldownStartMs_ = 0;
        consecutiveValidSamples_ = 0;
        recentMaxTemps_.clear();
        latchedCommand_ = decision.command;
        setCommand(output, decision.command);
        output.fanHealth = evaluateFanHealth(input, output.command);
        lastCommand_ = output.command;
        return finish();
    }

    // T2-01 / ADR-030: sensor agreement - consecutive valid samples must agree.
    recentMaxTemps_.push_back(temperature.valueC);
    if (recentMaxTemps_.size() > config_.startupValidSamples) {
        recentMaxTemps_.erase(recentMaxTemps_.begin());
    }
    if (recentMaxTemps_.size() == config_.startupValidSamples) {
        bool agreement = true;
        for (std::size_t i = 1; i < recentMaxTemps_.size(); ++i) {
            const int diff = recentMaxTemps_[i] - recentMaxTemps_[i - 1];
            const int absDiff = diff >= 0 ? diff : -diff;
            if (absDiff > config_.maximumSensorAgreementDeltaC) {
                agreement = false;
                break;
            }
        }
        if (!agreement) {
            consecutiveValidSamples_ = 0;
            recentMaxTemps_.clear();
            manualActive_ = false;
            manualLevel_ = -1;
            manualExpiresAtMs_ = 0;
            state_ = SafetyState::Validating;
            output.safetyState = state_;
            output.controlMode = ControlMode::MonitorOnly;
            output.reasonCode = "sensor_agreement_failed";
            setCommand(output, FanCommand::none());
            output.fanHealth = evaluateFanHealth(input, output.command);
            lastCommand_ = output.command;
            return finish();
        }
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
        return finish();
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
            return finish();
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
    if (output.fanHealth == FanHealth::Failed && consecutiveSuspectSamples_ >= config_.suspectThreshold) {
        enterFailsafe(output, input, "repeated_suspect_fan");
        output.fanHealth = FanHealth::Failed;
        lastCommand_ = output.command;
        return finish();
    }
    lastCommand_ = output.command;
    return finish();
}

} // namespace core
} // namespace tpfancontrol
