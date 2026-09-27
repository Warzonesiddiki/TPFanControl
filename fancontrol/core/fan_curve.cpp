#include "fan_curve.h"

#include <sstream>

namespace tpfancontrol {
namespace core {
namespace {

void addError(ValidationResult& result, const char* code, const std::string& message)
{
    result.errors.push_back(ValidationError{code, message});
}

bool isValidCommand(const FanCommand& command, int minimumLevel, int maximumLevel)
{
    if (command.kind == CommandKind::Level) {
        return command.level >= minimumLevel && command.level <= maximumLevel;
    }

    return command.kind == CommandKind::BiosAutomatic;
}

} // namespace

ValidationResult validateCurve(const CurveConfig& config)
{
    ValidationResult result;
    result.valid = true;

    if (config.minimumLevel > config.maximumLevel) {
        addError(result, "level_range", "minimum fan level exceeds maximum fan level");
    }

    if (config.points.empty()) {
        addError(result, "empty_curve", "curve must contain at least one point");
    }

    if (config.emergencyCommand.kind == CommandKind::None) {
        addError(result, "missing_emergency_command", "curve must define an emergency command");
    } else if (!isValidCommand(config.emergencyCommand, config.minimumLevel, config.maximumLevel)) {
        addError(result, "invalid_emergency_command", "emergency command is outside the profile range");
    }

    for (std::size_t i = 0; i < config.points.size(); ++i) {
        const CurvePoint& point = config.points[i];

        if (point.downTemperatureC > point.upTemperatureC) {
            addError(result, "invalid_hysteresis", "down threshold must not exceed up threshold");
        }

        if (!isValidCommand(point.command, config.minimumLevel, config.maximumLevel)) {
            addError(result, "invalid_command", "curve contains a command outside the profile range");
        }

        if (i > 0) {
            const CurvePoint& previous = config.points[i - 1];
            if (point.upTemperatureC <= previous.upTemperatureC) {
                addError(result, "unordered_temperature", "curve up thresholds must increase strictly");
            }

            if (commandRank(point.command) < commandRank(previous.command)) {
                addError(result, "descending_command", "curve commands must not decrease as temperature rises");
            }
        }

        if (point.command.kind == CommandKind::BiosAutomatic && i + 1 < config.points.size()) {
            addError(result, "bios_not_terminal", "BIOS automatic command must be the terminal curve command");
        }
    }

    result.valid = result.errors.empty();
    return result;
}

CurveController::CurveController(CurveConfig config)
    : config_(config),
      validation_(validateCurve(config_)),
      currentIndex_(0),
      currentCommand_(FanCommand::none()),
      lastChangeMs_(0),
      initialized_(false),
      emergencyActive_(false)
{
}

const ValidationResult& CurveController::validation() const noexcept
{
    return validation_;
}

void CurveController::reset() noexcept
{
    currentIndex_ = 0;
    currentCommand_ = FanCommand::none();
    lastChangeMs_ = 0;
    initialized_ = false;
    emergencyActive_ = false;
}

bool CurveController::initialized() const noexcept
{
    return initialized_;
}

FanCommand CurveController::currentCommand() const noexcept
{
    return currentCommand_;
}

CurveDecision CurveController::update(int temperatureC, std::uint64_t nowMs, bool emergency)
{
    CurveDecision decision;

    if (!validation_.valid) {
        return decision;
    }

    if (emergency) {
        decision.command = config_.emergencyCommand;
        decision.usedEmergency = true;
        decision.changed = !initialized_ || currentCommand_ != decision.command;
        currentCommand_ = decision.command;
        initialized_ = true;
        emergencyActive_ = true;
        lastChangeMs_ = nowMs;
        return decision;
    }

    if (emergencyActive_) {
        // Leave the emergency latch to the safety/controller layer, but restart
        // curve selection when that layer explicitly provides a safe sample.
        initialized_ = false;
        emergencyActive_ = false;
    }

    if (!initialized_) {
        currentIndex_ = 0;
        for (std::size_t i = 0; i < config_.points.size(); ++i) {
            if (temperatureC >= config_.points[i].upTemperatureC) {
                currentIndex_ = i;
            } else {
                break;
            }
        }

        currentCommand_ = config_.points[currentIndex_].command;
        initialized_ = true;
        lastChangeMs_ = nowMs;
        decision.command = currentCommand_;
        decision.changed = true;
        return decision;
    }

    std::size_t candidateIndex = currentIndex_;
    while (candidateIndex + 1 < config_.points.size() &&
           temperatureC >= config_.points[candidateIndex + 1].upTemperatureC) {
        ++candidateIndex;
    }

    while (candidateIndex > 0 &&
           temperatureC <= config_.points[candidateIndex].downTemperatureC) {
        --candidateIndex;
    }

    if (candidateIndex != currentIndex_) {
        const std::uint64_t elapsed = nowMs >= lastChangeMs_ ? nowMs - lastChangeMs_ : 0;
        if (elapsed < config_.minimumDwellMs) {
            decision.command = currentCommand_;
            decision.heldByDwell = true;
            return decision;
        }

        currentIndex_ = candidateIndex;
        currentCommand_ = config_.points[currentIndex_].command;
        lastChangeMs_ = nowMs;
        decision.changed = true;
    }

    decision.command = currentCommand_;
    return decision;
}

} // namespace core
} // namespace tpfancontrol
