#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

enum class CommandKind {
    None,
    Level,
    BiosAutomatic
};

struct FanCommand {
    CommandKind kind = CommandKind::None;
    int level = -1;

    static FanCommand none() noexcept;
    static FanCommand levelCommand(int value) noexcept;
    static FanCommand biosAutomatic() noexcept;
};

bool operator==(const FanCommand& left, const FanCommand& right) noexcept;
bool operator!=(const FanCommand& left, const FanCommand& right) noexcept;
int commandRank(const FanCommand& command) noexcept;

// These states are deliberately hardware-independent. A Windows adapter may map
// them to UI text, but it must not bypass the controller's transitions.
enum class SafetyState {
    MonitorOnly,
    Validating,
    BiosAutomatic,
    Controlled,
    ManualOverride,
    FailsafeBios,
    ThermalEmergency,
    Stopping
};

enum class ControlMode {
    MonitorOnly,
    BiosAutomatic,
    AutomaticCurve,
    ManualOverride
};

enum class SensorValidity {
    Valid,
    Missing,
    BackendError,
    ProviderInvalid,
    Sentinel,
    FutureTimestamp,
    Stale,
    OutOfRange
};

enum class FanHealth {
    Unsupported,
    Unknown,
    Healthy,
    Suspect,
    Failed
};

enum class FanTopology {
    Unknown,
    Single,
    Dual
};

struct TemperatureSample {
    int valueC = 0;
    std::uint64_t timestampMs = 0;
    bool hasValue = false;
    bool backendOk = false;
    bool sourceValid = false;
    bool sentinel = false;
    std::string source;
};

struct ValidatedTemperature {
    bool valid = false;
    int valueC = 0;
    std::uint64_t timestampMs = 0;
    SensorValidity validity = SensorValidity::Missing;
    std::string source;
    std::string reason;
};

// Tachometer readings get the same treatment as temperatures, for the same
// reason: a raw EC byte read as unsigned is plausible-looking garbage, and an
// impossible reading must be rejected rather than reported as a healthy fan.
struct FanRpmPolicy {
    // CANDIDATE VALUES, not verified evidence. A ThinkPad cooling fan tops out
    // far below 12000 RPM; the bound exists to reject sentinels and sign errors,
    // not to describe a particular machine. Phase 0 must derive the real range.
    int minimumPlausible = 0;
    int maximumPlausible = 12000;

    // Raw values that mean "no measurement" on some firmware rather than a
    // measurement. 0 is deliberately absent: a stopped fan genuinely reads 0,
    // and conflating the two would hide a stopped fan.
    std::uint16_t sentinelValues[2] = {0xFFFF, 0x8000};

    std::uint64_t maximumAgeMs = 5000;
    std::uint64_t maximumFutureSkewMs = 1000;
};

struct ValidatedFanRpm {
    bool valid = false;
    int rpm = 0;
    SensorValidity validity = SensorValidity::Missing;
    std::string reason;
};

struct SensorPolicy {
    int minimumPlausibleC = -40;
    int maximumPlausibleC = 125;
    std::uint64_t maximumAgeMs = 5000;
    std::uint64_t maximumFutureSkewMs = 1000;
};

struct CurvePoint {
    int upTemperatureC = 0;
    int downTemperatureC = 0;
    FanCommand command = FanCommand::none();
};

struct CurveConfig {
    std::vector<CurvePoint> points;
    int minimumLevel = 0;
    int maximumLevel = 7;
    FanCommand emergencyCommand = FanCommand::biosAutomatic();
    std::uint64_t minimumDwellMs = 0;
};

struct ValidationError {
    std::string code;
    std::string message;
};

struct ValidationResult {
    bool valid = false;
    std::vector<ValidationError> errors;
};

} // namespace core
} // namespace tpfancontrol
