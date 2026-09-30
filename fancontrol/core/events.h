#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace tpfancontrol {
namespace core {

// Stable machine-readable event identifiers. These describe decisions and
// lifecycle outcomes; they are not user-facing strings and carry no identity.
enum class EventCode : std::uint16_t {
    BackendInitialization,
    ProfileActivation,
    FanCommand,
    FanReadback,
    FailsafeEntered,
    MaximumTemperatureObserved,
    ShutdownRestore,
    ConfigurationValidation,
    ThermalEmergency
};

enum class EventCause : std::int32_t {
    None,
    BackendFailure,
    WriteFailure,
    ReadbackMismatch,
    FanResponseFailure,
    HardwareIneligible,
    InvalidTemperature,
    InvalidManualLevel,
    RepeatedSuspectFan
};

enum class EventSeverity : std::uint8_t {
    Info,
    Notice,
    Warning,
    Error,
    Critical
};

// A deliberately fixed-size, privacy-safe event value. `timestampMs` is
// monotonic time from the host's clock (not wall time). `value` and `detail`
// are interpreted by EventCode and use the documented field mapping; unknown
// values are represented by `hasValue == false`, never by a numeric sentinel.
// No strings, paths, serials, usernames, raw EC bytes, or arbitrary payloads.
struct CoreEvent {
    std::uint64_t timestampMs = 0;
    EventCode code = EventCode::ConfigurationValidation;
    EventSeverity severity = EventSeverity::Info;
    bool hasValue = false;
    std::int32_t value = 0;
    std::int32_t detail = 0;
};

// A sink accepts events without blocking the decision path. Implementations
// MUST have a fixed capacity and MUST NOT perform file/network I/O inline.
// false means the event was dropped because the sink could not accept it;
// producers must not retry synchronously or change a control decision based
// on logging pressure. Concrete sinks own and expose their dropped-event count.
// Bounded per-update event output. Four slots cover the maximum simultaneous
// controller observations (new temperature peak, state transition, command,
// and shutdown result); overflow is explicitly counted rather than allocating.
struct CoreEventBatch {
    static constexpr std::size_t Capacity = 4;
    std::array<CoreEvent, Capacity> events{};
    std::size_t count = 0;
    std::size_t dropped = 0;

    bool push(const CoreEvent& event) noexcept
    {
        if (count == Capacity) {
            ++dropped;
            return false;
        }
        events[count++] = event;
        return true;
    }
};

class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual bool tryEmit(const CoreEvent& event) noexcept = 0;
};

} // namespace core
} // namespace tpfancontrol
