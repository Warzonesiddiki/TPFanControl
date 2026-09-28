#pragma once

#include "controller.h"
#include "ec_protocol.h"
#include "io_backend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// The register map the application actually uses.
//
// Transcribed from fancontrol/fanstuff.cpp. These are the constants the
// shipped program uses; they are not measurements, and Phase 0 must confirm
// them read-only. See EC_REGISTER_MAP.md 3.1 and 9.1, and ADR-020.
// ---------------------------------------------------------------------------
inline constexpr std::uint8_t kRegisterFanLevel    = 0x2F;
inline constexpr std::uint8_t kRegisterFanSelector = 0x31;   // never written here
inline constexpr std::uint8_t kRegisterFanSpeedLo  = 0x84;
inline constexpr std::uint8_t kRegisterFanSpeedHi  = 0x85;
inline constexpr std::uint8_t kRegisterTemp0       = 0x78;  // 8 sensors
inline constexpr std::uint8_t kRegisterTemp1       = 0xC0;  // 4 sensors
inline constexpr int kTemp0Count = 8;
inline constexpr int kTemp1Count = 4;
inline constexpr int kSensorCount = kTemp0Count + kTemp1Count;

// The value the application writes to hand the fan back to the firmware.
// fanstuff.cpp uses 0x80 for "BIOS automatic".
inline constexpr int kBiosAutomaticLevel = 0x80;

// ---------------------------------------------------------------------------
// One cycle's worth of EC readings.
// ---------------------------------------------------------------------------
struct EcSnapshot {
    bool ok = false;

    bool fanLevelRead = false;
    int fanLevel = -1;

    bool fanSpeedRead = false;
    int fanRpm = 0;
    std::uint64_t fanSpeedTimestampMs = 0;

    // Exactly kSensorCount entries, in register order. A source that could not
    // be read keeps hasValue = false rather than being silently zero, because a
    // zero is a plausible-looking temperature.
    std::vector<TemperatureSample> temperatures;

    IoErrorCode lastError = IoErrorCode::None;
    std::string lastErrorMessage;
};

// ---------------------------------------------------------------------------
// Applying a command
// ---------------------------------------------------------------------------
struct ApplyResult {
    bool attempted = false;         // the bus was touched at all
    bool succeeded = false;         // the write was accepted by the bus
    bool readbackAttempted = false;
    bool readbackMatched = false;   // only meaningful when readbackAttempted
    int readbackValue = -1;
    IoResult write;
    IoResult readback;
    std::string reason;
};

// ---------------------------------------------------------------------------
// Bridge configuration
// ---------------------------------------------------------------------------
struct BridgeConfig {
    ControllerConfig controller;

    // Names for the kSensorCount sources, in register order. A list of the
    // wrong length is not padded: a shifted name would label one reading with
    // another's name, which is worse than admitting the label is missing.
    std::vector<std::string> sensorNames;

    // The T14 profile is single-fan. There is deliberately no flag that turns
    // on a fan-selector write: the bridge has no code path that performs one,
    // so the guarantee does not depend on a setting nobody is reviewing.
    bool singleFanProfile = true;

    // Whether a partial read (some sensors missing) is still "ok". It is not:
    // the snapshot is marked not-ok and the controller is told the backend
    // failed, so it fails safe rather than acting on half the picture.
    bool requireAllSensors = true;
};

// ---------------------------------------------------------------------------
// AppBridge
// ---------------------------------------------------------------------------
//
// The seam between the portable core and the legacy application. Everything the
// application used to decide for itself now happens here or in the Controller
// it drives, so there is one decision path rather than two.
//
// The central guarantee is T3-08: a fan command reaches the EC only by going
// through apply(), and apply() refuses anything the core did not authorise.
// There is deliberately no other method here that writes a register, so the
// fan-selector register is unreachable by construction rather than by a check
// that could be forgotten.
//
// Thread safety: this class is NOT synchronised. The caller must serialise the
// worker thread's reads against the UI thread's writes. EcBus serialises its
// own transactions against other EcBus users, but the application also has its
// own code paths, and those need the application's own mutex. That is the
// caller's responsibility and is exercised by the legacy integration.
class AppBridge {
public:
    AppBridge(IIoBackend& backend, IClock& clock, BridgeConfig config);

    // Reads the fan level, tachometer and every temperature sensor.
    EcSnapshot read(std::uint64_t nowMs);

    // Convenience: read, then build the controller input, then update.
    ControllerOutput cycle(std::uint64_t nowMs, const ControllerInput& request);

    // Writes the command the core produced, and verifies it by readback.
    //
    // Refuses, without touching the bus, when the core did not authorise a
    // command. This is the assertion that no fan command happens without core
    // approval, enforced structurally rather than by convention.
    ApplyResult apply(const ControllerOutput& output);

    // Builds a controller input from a snapshot plus the application's request.
    // Exposed so the integration can be tested without a live backend.
    static ControllerInput makeInput(
        const EcSnapshot& snapshot,
        const ControllerInput& request,
        std::uint64_t nowMs,
        std::uint64_t rpmObservationElapsedMs);

    const EcBus& bus() const noexcept;
    const Controller& controller() const noexcept;

    // The controller is stateful and the bridge drives it, so the integration
    // needs a mutable handle to run the cycle. Exposed deliberately: the
    // alternative is a second private update path, and two update paths are
    // exactly what T3-05 exists to remove.
    Controller& controller() noexcept;

    const BridgeConfig& config() const noexcept;

    // The last command level this bridge actually CONFIRMED by readback, or -1.
    // Used to decide whether a write is needed at all, so the application is
    // not writing the same value every cycle.
    int lastAppliedLevel() const noexcept;
    void resetLastAppliedLevel() noexcept;

    // Every write this bridge issued, for tests and for the T3-11 assertion
    // that the single-fan path never touches the fan selector.
    std::vector<BusWrite> writeTrace() const;

private:
    // Non-const because reading a register is an EC transaction, and the
    // transaction is what must be serialised. Marking this const would only
    // hide that.
    IoResult readTemperatureBlock(
        std::uint8_t baseRegister,
        int count,
        int firstSensor,
        std::vector<TemperatureSample>& out);

    // Declaration order matters: config_ is initialised first, then the
    // Controller is built from it. The reverse order would construct the
    // controller from a pre-adjustment copy of the configuration.
    BridgeConfig config_;
    EcBus bus_;
    Controller controller_;

    int lastAppliedLevel_ = -1;
    bool sensorNamesValid_ = false;
};

// ---------------------------------------------------------------------------
// UI text
// ---------------------------------------------------------------------------
// Presentation only. Mapping the state to a string here rather than in the
// dialog code means the mapping is testable, and it cannot be used to infer a
// state transition - a label describes a state, it never selects one.
const char* toText(SafetyState state) noexcept;
const char* toText(ControlMode mode) noexcept;
const char* toText(FanHealth health) noexcept;
const char* toText(SensorValidity validity) noexcept;

// A single line suitable for a status field, combining the mode, the safety
// state and the reason. Never empty, so the UI cannot show a blank status and
// look healthy.
std::string describe(const ControllerOutput& output);

} // namespace core
} // namespace tpfancontrol
