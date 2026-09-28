#include "legacy_policy.h"

#include <cstdio>
#include <cstring>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

const char* toText(LegacySource source) noexcept
{
    switch (source) {
    case LegacySource::Bios:      return "BIOS";
    case LegacySource::Manual:    return "Manual";
    case LegacySource::Smart:     return "Smart";
    case LegacySource::Failsafe:  return "Failsafe";
    case LegacySource::Startup:   return "Startup";
    case LegacySource::Shutdown:  return "Shutdown";
    }
    return "Unknown";
}

const char* toText(LegacyFanTarget target) noexcept
{
    switch (target) {
    case LegacyFanTarget::FirmwareSelected: return "the selected fan";
    case LegacyFanTarget::FirstFan:         return "fan 1";
    case LegacyFanTarget::SecondFan:        return "fan 2";
    }
    return "an unknown fan";
}

const char* toText(LegacyRefusal refusal) noexcept
{
    switch (refusal) {
    case LegacyRefusal::None:                   return "none";
    case LegacyRefusal::NotControlEnabled:      return "control is not enabled";
    case LegacyRefusal::SpecificFanNotSupported:
        return "selecting an individual fan is not supported";
    case LegacyRefusal::LevelOutOfRange:        return "level out of range";
    case LegacyRefusal::ManualLevelReserved:    return "level is reserved";
    case LegacyRefusal::FinalNotSupported:      return "final mode is not supported";
    case LegacyRefusal::ControlNotEligible:     return "control is not eligible";
    }
    return "unknown";
}

const char* toText(StartupMode mode) noexcept
{
    switch (mode) {
    case StartupMode::NoBackend:       return "no backend";
    case StartupMode::MonitorOnly:     return "monitor only";
    case StartupMode::ControlEligible: return "control eligible";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

CapabilityReport makeCapabilities(const LegacyCapabilityInputs& in)
{
    // Kept as a straight transcription of the inputs. The eligibility rule is
    // CapabilityReport::controlEligible(), which the controller applies. If this
    // function also computed an answer, there would be two answers, and the
    // legacy application would be asking the weaker one.
    CapabilityReport report;
    report.exactHardwareMatch = in.hardwareExactMatch;
    report.profileVerified = in.profileVerified;
    report.topologyVerified = in.topologyVerified;

    // A backend is only "ready" when both halves are true: the I/O path exists
    // and the port driver is loaded. Reporting ready with the driver closed
    // would let the controller believe it can read, and the first thing it
    // would do is fail - with a fault latch the user then has to clear.
    report.backendReady = in.backendPresent && in.driverLoaded;

    // Restore is the safety net, so it is a precondition of control rather than
    // a fallback. An unverifiable restore counts as no restore.
    report.restoreAvailable = in.restoreCapabilityVerified;

    // And a person can hold the machine in monitor-only mode regardless of what
    // the verdicts say. Reported through the same structure so that there is one
    // answer to "may this machine be controlled".
    report.monitorOnlyForced = in.monitorOnlyForced;

    return report;
}

// ---------------------------------------------------------------------------
// The startup assessment (T5-04)
// ---------------------------------------------------------------------------
namespace {

// The dependency the user has to install, in the words the user needs. Named
// here rather than in the dialog code so the message cannot drift between the
// startup path and the status line.
const char* kMissingBackendDependency =
    "the TVicPort port driver (tvicport.sys) is not loaded";

} // namespace

StartupAssessment assessStartup(const LegacyCapabilityInputs& in)
{
    StartupAssessment assessment;

    // One source of truth for eligibility. If this function recomputed the
    // rule, the application would be asking the weaker of two answers.
    const CapabilityReport capabilities = makeCapabilities(in);
    assessment.mayControl = capabilities.controlEligible();

    // Is there an I/O path at all? Both halves matter: a backend object with no
    // driver behind it cannot read anything, and a driver with no backend is
    // not something the core can use.
    const bool haveBackend = in.backendPresent && in.driverLoaded;

    if (!haveBackend) {
        assessment.mode = StartupMode::NoBackend;
        assessment.monitorOnly = true;
        assessment.readingsAvailable = false;
        assessment.statusLine = "MONITOR ONLY - no readings available";
        assessment.explanation =
            "No backend is available: " + std::string(kMissingBackendDependency) +
            ". Temperatures and fan speed come from the embedded controller, so "
            "with no backend there is nothing to read and nothing to display. "
            "No register will be written. The fan remains under firmware (BIOS) "
            "control.";
        return assessment;
    }

    assessment.readingsAvailable = true;

    if (assessment.mayControl) {
        assessment.mode = StartupMode::ControlEligible;
        assessment.monitorOnly = false;
        assessment.statusLine = "CONTROL ELIGIBLE - awaiting consecutive valid readings";
        assessment.explanation =
            "The backend is ready and every Phase 0 verdict for this machine "
            "holds. Control still does not start by itself: it needs an explicit "
            "request and the controller's own consecutive-samples gate.";
        return assessment;
    }

    assessment.mode = StartupMode::MonitorOnly;
    assessment.monitorOnly = true;
    assessment.statusLine = "MONITOR ONLY - " + capabilities.blockingReason();
    assessment.explanation =
        "Readings are available and are shown. Control is not approved for this "
        "machine, so no register will be written: " + capabilities.blockingReason() +
        ". The fan remains under firmware (BIOS) control.";
    return assessment;
}

// ---------------------------------------------------------------------------
// The legacy temperature table, as a curve for the core (T3-04, T3-05)
// ---------------------------------------------------------------------------

namespace {

// The legacy manual range. Level commands in the application were 0x00-0x07;
// the values above it were specials, and 0x80 meant "give the fan back to the
// firmware". The core's profile range has to be the same, or a table that the
// legacy code would have applied is silently reinterpreted.
constexpr int kLegacyMaximumLevel = 7;

// The legacy table's terminator. A negative temperature ends the list.
bool isEndMarker(const LegacyCurveRow& row)
{
    return row.temperatureC < 0;
}

} // namespace

LegacyCurveMapping curveFromLegacyRows(const LegacyCurveRow* rows, std::size_t count)
{
    LegacyCurveMapping mapping;

    // The safe direction is the firmware, and the legacy table says the same
    // thing with 0x80. An emergency command that turned the fan *up* would be
    // an invented behaviour: nothing in the hardware report establishes that a
    // full-speed manual write is safer than the firmware's own response.
    mapping.config.minimumLevel = 0;
    mapping.config.maximumLevel = kLegacyMaximumLevel;
    mapping.config.emergencyCommand = FanCommand::biosAutomatic();
    // No dwell. The application evaluates the table once per data cycle, so the
    // curve is already paced by the cycle timer; a second, invented time limit
    // would change when the fan responds without anything to justify it.
    mapping.config.minimumDwellMs = 0;

    // A null array with a non-zero count is a caller bug, and the honest answer
    // is an empty curve that fails validation rather than a crash.
    const std::size_t usable = rows == nullptr ? 0 : count;

    for (std::size_t i = 0; i < usable; ++i) {
        const LegacyCurveRow& row = rows[i];

        if (isEndMarker(row)) {
            mapping.hadEndMarker = true;
            break;
        }

        CurvePoint point;
        point.upTemperatureC = row.temperatureC;
        // One temperature per row in the legacy table, used for the up scan and
        // the down scan alike. Keeping them equal is what makes the mapping
        // faithful rather than approximate.
        point.downTemperatureC = row.temperatureC;

        if (row.command == 0x80) {
            point.command = FanCommand::biosAutomatic();
        } else if (row.command >= 0 && row.command <= kLegacyMaximumLevel) {
            point.command = FanCommand::levelCommand(row.command);
        } else {
            // 0x40 and the other specials. Refused rather than guessed: the
            // core has no such command, and mapping it onto the nearest level
            // would be a claim about a register value nobody here measured.
            mapping.validation.errors.push_back(ValidationError{
                "unsupported_legacy_level",
                "smart table row " + std::to_string(i) + " asks for fan value "
                    + std::to_string(row.command)
                    + ", which is neither a level in 0-"
                    + std::to_string(kLegacyMaximumLevel)
                    + " nor the BIOS value 0x80. This project has not established"
                      " what the other legacy specials mean, so the table is refused"
                      " rather than reinterpreted."});
            mapping.rowsUsed = static_cast<int>(i);
            mapping.validation.valid = false;
            return mapping;
        }

        mapping.config.points.push_back(point);
        mapping.rowsUsed = static_cast<int>(i + 1);
    }

    // validateCurve is the same predicate the CurveController applies when it
    // refuses to run an invalid curve, so asking it here means the application
    // can report *why* the table was refused instead of only that control did
    // not start.
    mapping.validation = validateCurve(mapping.config);
    return mapping;
}

// ---------------------------------------------------------------------------
// Intent evaluation
// ---------------------------------------------------------------------------

namespace {

LegacyDecision refuse(LegacyRefusal reason, std::string message)
{
    LegacyDecision decision;
    decision.refused = true;
    decision.refusal = reason;
    decision.message = std::move(message);
    return decision;
}

// "0x" plus exactly two hex digits. A level is one byte; printing it with %d
// would make 0x80 look like 128, which is how a BIOS restore gets mistaken for
// a manual level in a log.
std::string hex2(int value)
{
    char buffer[8];
    const int written = std::snprintf(
        buffer, sizeof(buffer), "0x%02X", static_cast<unsigned>(value) & 0xFFU);
    if (written < 0 || written >= static_cast<int>(sizeof(buffer))) {
        return "0x??";
    }
    return std::string(buffer, static_cast<std::size_t>(written));
}

// What the request asked for, in the words a message needs. A curve request
// carries no level - the core's curve decides it - so it must not be rendered
// as one: hex2(-1) is "0xFF", and a fabricated register value in a log is worse
// than no value at all.
std::string describeRequest(const LegacyIntent& intent)
{
    if (intent.source == LegacySource::Smart) {
        return "curve control";
    }
    return "level " + hex2(intent.level);
}

std::string fanName(const LegacyIntent& intent)
{
    std::string text = toText(intent.target);
    if (intent.userSelectedSpecificFan && intent.target == LegacyFanTarget::FirmwareSelected) {
        text = toText(intent.target);
        text += " (the UI asked for a specific fan)";
    }
    return text;
}

} // namespace

LegacyDecision evaluateIntent(const LegacyIntent& intent, std::uint64_t nowMs)
{
    ControllerInput input;
    input.nowMs = nowMs;

    // --- Monitor-only mode ---------------------------------------------------
    //
    // fanstuff.cpp ignored every SetFan call while ActiveMode was false and
    // printed "IGNORED!(passive mode". The same rule is applied here, as a
    // refusal rather than a skipped write, so the caller has one thing to
    // report instead of a write that silently did not happen. Startup is
    // exempt because it is read-only and produces no command to suppress.
    if (!intent.controlEnabled && intent.source != LegacySource::Startup) {
        return refuse(
            LegacyRefusal::NotControlEnabled,
            std::string("Refused: ") + toText(intent.source) + " "
                + describeRequest(intent)
                + " not applied. The application is in monitor-only mode, so no EC"
                  " register was written. Enable control in the UI to apply levels.");
    }

    // --- The fan selector ----------------------------------------------------
    //
    // This is T3-11. The legacy application set the target fan by writing 0x31
    // with no check of any kind, five times per attempt, before and after every
    // level write, on every cycle. The bridge has no code path that writes
    // 0x31, so a request to address a specific fan cannot be honoured here at
    // all - and the correct response is to say so rather than to apply the
    // level to whichever fan happens to be selected.
    if (intent.target != LegacyFanTarget::FirmwareSelected) {
        return refuse(
            LegacyRefusal::SpecificFanNotSupported,
            std::string("Refused: addressing ") + fanName(intent)
                + " would require writing the fan-selector register (0x31), which this"
                  " build never writes. No register was written. Use the manual level"
                  " control, which applies to the fan the firmware has selected.");
    }

    // --- Source-specific translation -----------------------------------------
    switch (intent.source) {
    case LegacySource::Bios:
    case LegacySource::Failsafe:
    case LegacySource::Shutdown:
        // All three are the same request: give the fan back to the firmware.
        // Keeping them distinct in the enum preserves what the UI called the
        // operation for the log without producing three behaviours.
        input.requestBiosAutomatic = true;
        break;

    case LegacySource::Startup:
        // The legacy startup check only read the current level. Expressing it
        // as a request would manufacture a command that was never asked for, so
        // it is a read-only intent and produces no request at all.
        break;

    case LegacySource::Manual: {
        // ManualMaximumLevel defaults to 7, which is the legacy manual range
        // (0x00-0x07). Anything above that is either a special value or a
        // configuration error, and must not be passed through as a level.
        if (intent.level < 0 || intent.level > 7) {
            return refuse(
                LegacyRefusal::LevelOutOfRange,
                "Refused: manual level " + std::to_string(intent.level)
                    + " is outside the manual range 0-7.");
        }
        input.requestManual = true;
        input.manualLevel = intent.level;
        break;
    }

    case LegacySource::Smart:
        // The legacy Smart path turned a temperature table into a level and then
        // called SetFan. The table itself is a candidate-value source that
        // Phase 0 has not verified, and a curve is the core's job, not the
        // adapter's. So Smart is translated into the core's own curve request
        // and the legacy table is not consulted here.
        input.requestControl = true;
        break;
    }

    // --- The legacy FinalSeen latch ------------------------------------------
    //
    // fanstuff.cpp latched FinalSeen so that after a final BIOS write no
    // further change would be made. The core expresses the same intent through
    // ControlMode::BiosAutomatic with the controller owning the state, so the
    // latch is not duplicated. A final BIOS restore is honoured as an ordinary
    // BIOS request. What must not happen is the legacy behaviour where a
    // *manual* level was marked final and then permanently blocked the
    // application from ever changing the fan again, including to fail safe -
    // so a final manual request is refused rather than silently latched.
    if (intent.final && intent.source != LegacySource::Bios
        && intent.source != LegacySource::Shutdown) {
        return refuse(
            LegacyRefusal::FinalNotSupported,
            std::string("Refused: a final ") + toText(intent.source)
                + " request is not supported. Latching the fan to a manual level"
                  " would prevent the application from ever handing the fan back"
                  " to the firmware, including on a safety fault. No register was"
                  " written.");
    }

    LegacyDecision decision;
    decision.input = input;
    decision.levelMeaningful =
        intent.source == LegacySource::Manual || intent.source == LegacySource::Smart;
    decision.level = intent.level;
    return decision;
}

bool describeRequestedLevel(const LegacyIntent& intent, char* buffer, int size)
{
    if (buffer == nullptr || size <= 0) {
        return false;
    }
    const int written = std::snprintf(
        buffer, static_cast<std::size_t>(size), "%s requested %s for %s",
        toText(intent.source), describeRequest(intent).c_str(), fanName(intent).c_str());
    if (written < 0 || written >= size) {
        // Truncated: say so rather than returning a half sentence that could be
        // mistaken for the whole message.
        buffer[0] = '\0';
        return false;
    }
    return true;
}

} // namespace core
} // namespace tpfancontrol
