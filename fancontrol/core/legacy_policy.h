#pragma once

#include "app_bridge.h"
#include "controller.h"

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
