#pragma once

#include "app_bridge.h"
#include "controller.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// The legacy application's intent, expressed without touching the EC.
//
// fanstuff.cpp used to carry intent and action in the same function: SetFan
// knew both what the UI wanted and which registers to write, so there was no
// place to stand between "the user asked for a level" and "write it to the
// hardware". That is the gap this file closes. It converts intent into a
// ControllerInput and, for the cases that cannot be honoured safely, into a
// refusal with a reason the UI can print.
//
// Everything here is portable and testable, which the legacy function was not.
// ---------------------------------------------------------------------------

// Where an intent came from. fanstuff.cpp passed a `const char *source` and
// compared it by pointer identity (`source == "Smart"`), which is undefined
// behaviour unless the string is a literal in the same translation unit; it
// happened to work only because every call site passed a literal. An enum
// cannot be compared wrongly.
enum class LegacySource {
    Bios,          // hand the fan back to the firmware
    Manual,        // the user picked a level
    Smart,         // the legacy temperature/level table
    Failsafe,      // a safety fault wants the firmware back
    Startup,       // initial state check
    Shutdown,      // restore before exit
};

// Which fan the legacy UI believes it is addressing.
//
// Only FirmwareSelected is writable, and it is writable because it needs no
// selector write: it means "whatever fan the firmware has already selected",
// which is the fan register 0x2F applies to. The other two exist so the
// refusal can name the fan the user asked for. They are refused, not ignored,
// because silently applying a level to the wrong fan looks like success.
enum class LegacyFanTarget {
    FirmwareSelected,   // the only writable value
    FirstFan,           // would require writing 0x31 = 0x00
    SecondFan,          // would require writing 0x31 = 0x01
};

struct LegacyIntent {
    LegacySource source = LegacySource::Manual;

    // The legacy application's ActiveMode. While false the application is in
    // monitor-only mode: it reads, it displays, and it writes nothing at all -
    // not even a BIOS restore, which the legacy code also ignored. Letting a
    // restore through here would be a behaviour change nobody asked for, and in
    // monitor-only mode the firmware already owns the fan.
    bool controlEnabled = true;
    LegacyFanTarget target = LegacyFanTarget::FirmwareSelected;
    int level = 0;             // 0x00-0x07 manual, 0x80 BIOS automatic
    bool final = false;        // the legacy "no further changes" flag
    // The user chose a specific fan in the UI. Even when target is
    // FirmwareSelected, this records what the UI said, so a refusal can report
    // it truthfully instead of claiming the other fan was addressed.
    bool userSelectedSpecificFan = false;
};

// Why an intent was not turned into a command. Kept as a value rather than a
// string so tests can assert on it and the UI cannot mistype it.
enum class LegacyRefusal {
    None,
    NotControlEnabled,        // the application is in passive/monitor mode
    SpecificFanNotSupported,  // would require the 0x31 selector write
    LevelOutOfRange,          // outside the controller's manual range
    ManualLevelReserved,      // a manual level that collides with a special value
    FinalNotSupported,        // the legacy FinalSeen latch is not honoured
    ControlNotEligible,       // the controller refused; see its reasonCode
};

const char* toText(LegacySource source) noexcept;
const char* toText(LegacyFanTarget target) noexcept;
const char* toText(LegacyRefusal refusal) noexcept;

struct LegacyDecision {
    // Exactly one of these is true. A refusal is not a failure: it is the
    // correct answer for monitor-only mode and for a specific-fan request, and
    // the UI has to be able to show it.
    bool refused = false;
    LegacyRefusal refusal = LegacyRefusal::None;
    std::string message;      // never empty when refused

    // The input to hand to Controller::update. Only meaningful when !refused.
    // Built even when refused, so a caller that wants to log the request can.
    ControllerInput input;

    // A level worth checking against the controller's manual range.
    bool levelMeaningful = false;
    int level = 0;
};

// ---------------------------------------------------------------------------
// Capabilities from the legacy application's own state.
//
// The legacy application decided eligibility by looking at ActiveMode, driver
// state and a flag the user ticked. This takes those facts as data and does the
// arithmetic in one place, so the rule is testable instead of being spread
// across five call sites.
struct LegacyCapabilityInputs {
    bool controlRequestedByUser = false;   // ActiveMode
    bool driverLoaded = false;             // the WinIO port driver is open
    bool backendPresent = false;           // IIoBackend is non-null
    bool hardwareExactMatch = false;       // Phase 0, per machine
    bool profileVerified = false;          // Phase 0
    bool topologyVerified = false;         // Phase 0
    bool restoreCapabilityVerified = false;// Phase 0: can we put it back?
    bool monitorOnlyForced = false;        // the user or Phase 0 forced monitor-only
};

// Builds the report. The mapping is deliberately conjunctive: Phase 0 verdicts
// are not a formality, and a control path that needs four independent things
// to be true is much harder to enable by accident than one that needs a
// boolean the UI set.
CapabilityReport makeCapabilities(const LegacyCapabilityInputs& in);

// ---------------------------------------------------------------------------
// The startup assessment (T5-04).
//
// What the application should do when it starts, expressed as a decision rather
// than as a sequence of ifs in Win32 code that nothing here can execute.
//
// The requirement is in DRIVER_BACKENDS.md section 5: with no approved backend,
// monitor temperatures if possible, issue no manual EC writes, leave the fan to
// the BIOS, and explain the missing dependency. "If possible" is doing real work
// in that sentence, because on this machine the temperature sources are EC
// registers: with no backend there is nothing to read, and saying "monitoring"
// would promise readings that cannot arrive.
//
// So the assessment distinguishes the two monitor-only situations that the
// legacy code collapsed into one:
//
//   * no backend          - nothing can be read and nothing may be written. The
//                           window can show nothing, and the only honest thing
//                           to show is why, plus the fact that the fan is on
//                           firmware control.
//   * backend, unverified - readings work and are shown; control is not
//                           approved (Phase 0), so the fan is still the
//                           firmware's.
//
// It never decides eligibility by itself. "May this machine be controlled" is
// CapabilityReport::controlEligible(), which the controller also applies; a
// second answer here would be a second rule, and the two would diverge. The
// tests assert the two agree for every combination of inputs.
// ---------------------------------------------------------------------------
enum class StartupMode {
    NoBackend,        // monitor-only, and not even readings are available
    MonitorOnly,      // readings available, control not approved
    ControlEligible,  // every Phase 0 verdict holds and control is approved
};

const char* toText(StartupMode mode) noexcept;

struct StartupAssessment {
    StartupMode mode = StartupMode::NoBackend;

    // Whether this mode is monitor-only in the safety sense: no register may be
    // written. True for NoBackend and MonitorOnly, and the application must not
    // treat either as a fault - the machine is being used exactly as intended.
    bool monitorOnly = true;

    // Whether anything can be displayed. False in NoBackend: there is no way to
    // reach the EC, so a temperature pane would be a pane of lies.
    bool readingsAvailable = false;

    // The same answer the controller will give, taken from the capability
    // report rather than recomputed. False in every monitor-only mode.
    bool mayControl = false;

    // One line for the status field. Never empty: a blank status line reads as
    // healthy, which is the failure mode the UI text functions exist to prevent.
    std::string statusLine;

    // The longer explanation, naming the missing dependency or the unverified
    // condition and saying who has the fan. Never empty.
    std::string explanation;
};

// Pure: no I/O, no globals, so it can be tested exhaustively.
StartupAssessment assessStartup(const LegacyCapabilityInputs& in);

// ---------------------------------------------------------------------------
// The legacy temperature table, as a curve for the core (T3-04, T3-05)
// ---------------------------------------------------------------------------
//
// The legacy application decided the fan level with a table: a list of
// (temperature, fan) rows, scanned on every data cycle, whose result was
// written to the EC. T3-05 wants that decision deleted or quarantined, and
// deleting it is only correct once the core can make the same decision.
//
// The core can: `CurveController` is a curve with up/down thresholds, a dwell
// time and validation. What was missing was the mapping from the application's
// own configured table into a `CurveConfig`, and that mapping is the whole of
// this section. It is a pure function over rows, so it is tested the same way
// the rest of the policy is.
//
// The table is *configuration*, not evidence: it comes from the user's
// TPFanControl.ini and it is exactly as unverified as the rest of the profile.
// Mapping it does not enable anything - the controller still refuses control
// until every Phase 0 verdict holds - it only means that when control does
// become possible, the decision is made in one place instead of two.
struct LegacyCurveRow {
    int temperatureC = 0;   // Celsius. The parser converts a Fahrenheit table
                            // before this ever sees it (misc.cpp), so this is
                            // the same scale as a sensor reading.
    int command = 0;        // 0x80 for "BIOS automatic", otherwise a fan level
};

struct LegacyCurveMapping {
    CurveConfig config;

    // validateCurve(config), plus this mapping's own errors. A mapping that is
    // not valid must not be installed: the controller refuses control outright
    // when its curve is invalid, which is the safe direction.
    ValidationResult validation;

    int rowsUsed = 0;         // rows before the end marker
    bool hadEndMarker = false;// whether a negative temperature was found
};

// Builds the curve from consecutive rows. Iteration stops at the first row with
// a negative temperature: the legacy table is terminated by `-1`, and reading
// past it would read the zero-initialised tail of a fixed 32-entry array as
// real data (row 0 of that tail is a level-0 threshold at 0 degrees).
//
// Rules, each of which the legacy code did not have:
//
//   - a row with command 0x80 becomes FanCommand::biosAutomatic() and must be
//     terminal, because the core's curve requires it to be (``bios_not_terminal``
//     otherwise). This matches what the legacy code meant by 0x80.
//   - a row with a command outside 0-7 that is not 0x80 is refused with
//     ``unsupported_legacy_level``, naming the row and the value. The legacy
//     code had specials - 0x40, gated behind a config flag - whose meaning this
//     project has not established, and inventing a mapping for them would be
//     guessing with the fan.
//   - up and down thresholds are the same value, because that is what the
//     legacy table has: one temperature per row, used for both directions. The
//     core's hysteresis is then explicit rather than implicit in the scan
//     order.
//   - the table must be strictly increasing in temperature and non-decreasing
//     in fan level. The legacy scan could be handed a table that rises in
//     temperature while falling in fan and would apply it; the core refuses it
//     (``unordered_temperature``, ``descending_command``).
LegacyCurveMapping curveFromLegacyRows(const LegacyCurveRow* rows, std::size_t count);

// Translates an intent into a controller input. Pure: it performs no I/O and
// consults no global state, so the decision can be tested exhaustively.
//
// A refusal here means the intent cannot even be expressed as a request. A
// request that is expressible may still be refused by the controller; that is
// reported through the output, not through LegacyDecision::refused.
LegacyDecision evaluateIntent(const LegacyIntent& intent, std::uint64_t nowMs);

// Convenience: the level the user asked for, whether or not it is safe.
// Used by the UI to show what was requested when the answer was a refusal.
bool describeRequestedLevel(const LegacyIntent& intent, char* buffer, int size);

} // namespace core
} // namespace tpfancontrol
