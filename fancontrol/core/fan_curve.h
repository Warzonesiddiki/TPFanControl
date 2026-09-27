#pragma once

#include "core_types.h"

#include <cstddef>

namespace tpfancontrol {
namespace core {

struct CurveDecision {
    FanCommand command = FanCommand::none();
    bool changed = false;
    bool heldByDwell = false;
    bool usedEmergency = false;
};

ValidationResult validateCurve(const CurveConfig& config);

class CurveController {
public:
    explicit CurveController(CurveConfig config);

    const ValidationResult& validation() const noexcept;
    void reset() noexcept;
    bool initialized() const noexcept;
    FanCommand currentCommand() const noexcept;

    CurveDecision update(int temperatureC, std::uint64_t nowMs, bool emergency);

private:
    CurveConfig config_;
    ValidationResult validation_;
    std::size_t currentIndex_;
    FanCommand currentCommand_;
    std::uint64_t lastChangeMs_;
    bool initialized_;
    bool emergencyActive_;
};

} // namespace core
} // namespace tpfancontrol
