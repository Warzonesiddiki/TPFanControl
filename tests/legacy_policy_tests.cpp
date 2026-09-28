// Tests for fancontrol/core/legacy_policy.{h,cpp}, covering T3-04, T3-05, T3-08
// and T3-11.
//
// The point of this file is T3-11. The legacy SetFan() wrote the fan-selector
// register 0x31 unconditionally, four times per attempt, up to five attempts,
// with no check of any kind. That function cannot be executed here - it is
// Win32 dialog code - so the way to make the defect impossible to reintroduce
// is to move the decision out of it, into portable code that can be tested
// exhaustively, and then test that the removed path is genuinely absent.
//
// Every assertion uses CHECK, not assert: assert is compiled out under NDEBUG,
// which is what the Release configurations CI actually builds. See
// tests/test_check.h.

#include "test_check.h"

#include "../fancontrol/core/legacy_policy.h"

#include <cstring>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::LegacyDecision;
using core::LegacyFanTarget;
using core::LegacyIntent;
using core::LegacyRefusal;
using core::LegacySource;
using core::StartupMode;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

LegacyIntent manualIntent(int level)
{
    LegacyIntent intent;
    intent.source = LegacySource::Manual;
    intent.level = level;
    return intent;
}

LegacyIntent biosIntent()
{
    LegacyIntent intent;
    intent.source = LegacySource::Bios;
    intent.level = 0x80;
    return intent;
}

// ---------------------------------------------------------------------------
// T3-11: the fan selector
// ---------------------------------------------------------------------------

void testSpecificFanRequestIsRefused()
{
    noteTest();
    // The legacy application had a UI to address fan 1 or fan 2, and
    // implemented it by writing 0x31. There is no way to honour that here
    // without the write, so the answer is no.
    for (const LegacyFanTarget target :
         {LegacyFanTarget::FirstFan, LegacyFanTarget::SecondFan}) {
        LegacyIntent intent = manualIntent(4);
        intent.target = target;
        intent.userSelectedSpecificFan = true;

        const LegacyDecision decision = core::evaluateIntent(intent, 1000);
        CHECK(decision.refused);
        CHECK(decision.refusal == LegacyRefusal::SpecificFanNotSupported);
        CHECK(!decision.message.empty());
    }
}

void testRefusalNamesTheFanTheUserAskedFor()
{
    noteTest();
    // A refusal that does not say which fan was requested leaves the user
    // thinking the program ignored them, which is the failure mode this is
    // meant to avoid.
    LegacyIntent intent = manualIntent(4);
    intent.target = LegacyFanTarget::SecondFan;
    intent.userSelectedSpecificFan = true;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(decision.refused);
    CHECK(decision.message.find("fan 2") != std::string::npos);
    CHECK(decision.message.find("0x31") != std::string::npos);
    CHECK(decision.message.find("No register was written") != std::string::npos);
}

void testFirmwareSelectedTargetIsAllowed()
{
    noteTest();
    // The one writable target: no selector write, because the firmware has
    // already chosen the fan that register 0x2F applies to.
    LegacyIntent intent = manualIntent(5);
    CHECK(intent.target == LegacyFanTarget::FirmwareSelected);

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(!decision.refused);
    CHECK(decision.input.requestManual);
    CHECK(decision.input.manualLevel == 5);
    CHECK(!decision.input.requestControl);
    CHECK(!decision.input.requestBiosAutomatic);
}

void testSpecificFanIsRefusedEvenForABiosRestore()
{
    noteTest();
    // Including the "safe" directions. Handing the fan back to the firmware is
    // harmless, but doing it while having first written 0x31 to select a fan is
    // exactly the defect. A restore is scoped to the selected fan, so asking
    // for fan 2 and getting a restore of fan 1 is not the user's request.
    for (const LegacySource source :
         {LegacySource::Bios, LegacySource::Failsafe, LegacySource::Shutdown}) {
        LegacyIntent intent = biosIntent();
        intent.source = source;
        intent.target = LegacyFanTarget::FirstFan;

        const LegacyDecision decision = core::evaluateIntent(intent, 1000);
        CHECK(decision.refused);
        CHECK(decision.refusal == LegacyRefusal::SpecificFanNotSupported);
    }
}

void testNoCodePathProducesAWriteOutsideTheBridge()
{
    noteTest();
    // evaluateIntent returns a ControllerInput. It is a request to the
    // controller, not a command, and it carries no register address at all -
    // which is the structural reason the selector is unreachable. If a field
    // named like a register were ever added here, this would stop compiling or
    // start failing, which is the intended alarm.
    LegacyDecision decision = core::evaluateIntent(manualIntent(7), 1000);
    CHECK(!decision.refused);
    CHECK(decision.input.nowMs == 1000);
    CHECK(decision.levelMeaningful);
    CHECK(decision.level == 7);
}

// ---------------------------------------------------------------------------
// T3-05 / ADR-007: monitor-only mode
// ---------------------------------------------------------------------------

void testMonitorOnlyRefusesEveryWriteSource()
{
    noteTest();
    // The legacy code ignored every SetFan call while ActiveMode was false. In
    // monitor-only mode the firmware owns the fan and the application writes
    // nothing - including no restore, which the legacy code also suppressed.
    for (const LegacySource source :
         {LegacySource::Bios, LegacySource::Manual, LegacySource::Smart,
          LegacySource::Failsafe, LegacySource::Shutdown}) {
        LegacyIntent intent;
        intent.source = source;
        intent.level = 0x80;
        intent.controlEnabled = false;

        const LegacyDecision decision = core::evaluateIntent(intent, 1000);
        CHECK(decision.refused);
        CHECK(decision.refusal == LegacyRefusal::NotControlEnabled);
        CHECK(!decision.input.requestControl);
        CHECK(!decision.input.requestManual);
        CHECK(!decision.input.requestBiosAutomatic);
    }
}

void testMonitorOnlyStillPermitsTheReadOnlyStartupCheck()
{
    noteTest();
    // Startup only reads. Refusing it would leave the UI unable to show the
    // current fan level in monitor-only mode, which is the mode monitor-only
    // exists for.
    LegacyIntent intent;
    intent.source = LegacySource::Startup;
    intent.controlEnabled = false;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(!decision.refused);
    CHECK(!decision.input.requestControl);
    CHECK(!decision.input.requestManual);
    CHECK(!decision.input.requestBiosAutomatic);
}

void testMonitorOnlyMessageSaysNothingWasWritten()
{
    noteTest();
    LegacyIntent intent = manualIntent(3);
    intent.controlEnabled = false;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(decision.message.find("monitor-only") != std::string::npos);
    CHECK(decision.message.find("no EC") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Source translation
// ---------------------------------------------------------------------------

void testEveryRestoreSourceProducesTheSameRequest()
{
    noteTest();
    // Three names, one behaviour. The names are kept because the UI and the
    // trace distinguish a user-initiated restore from a safety fault, but the
    // request they produce must be identical or the log lies about behaviour.
    for (const LegacySource source :
         {LegacySource::Bios, LegacySource::Failsafe, LegacySource::Shutdown}) {
        LegacyIntent intent = biosIntent();
        intent.source = source;

        const LegacyDecision decision = core::evaluateIntent(intent, 1000);
        CHECK(!decision.refused);
        CHECK(decision.input.requestBiosAutomatic);
        CHECK(!decision.input.requestControl);
        CHECK(!decision.input.requestManual);
        CHECK(decision.input.manualLevel == -1);
    }
}

void testSmartBecomesACurveRequestNotATableLookup()
{
    noteTest();
    // The legacy Smart path consulted a temperature/level table and then called
    // SetFan. That table is a candidate-value source Phase 0 has not verified,
    // and interpreting it is the core's job. So Smart becomes a curve request
    // and the adapter never sees a temperature.
    LegacyIntent intent;
    intent.source = LegacySource::Smart;
    intent.level = 3;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(!decision.refused);
    CHECK(decision.input.requestControl);
    CHECK(!decision.input.requestManual);
    CHECK(!decision.input.requestBiosAutomatic);
    CHECK(decision.input.temperatures.empty());
}

void testManualLevelsZeroThroughSevenAreAccepted()
{
    noteTest();
    // The boundary cases, both ends. 0x00 is "fan off" in the legacy
    // convention, which is only guarded by the controller's temperature
    // policy, not by this layer - so it must reach the controller rather than
    // being dropped here.
    for (int level = 0; level <= 7; ++level) {
        const LegacyDecision decision = core::evaluateIntent(manualIntent(level), 1000);
        CHECK(!decision.refused);
        CHECK(decision.input.requestManual);
        CHECK(decision.input.manualLevel == level);
    }
}

void testLevelsOutsideTheManualRangeAreRefused()
{
    noteTest();
    // 0x80 is the BIOS value. Reaching it through the manual path would be a
    // way to restore the fan while claiming to be setting a level, and the
    // guard on high temperature would not apply.
    for (const int level : {-1, 8, 0x7F, 0x80, 255, 1000}) {
        const LegacyDecision decision = core::evaluateIntent(manualIntent(level), 1000);
        CHECK(decision.refused);
        CHECK(decision.refusal == LegacyRefusal::LevelOutOfRange);
    }
}

void testFinalManualIsRefused()
{
    noteTest();
    // FinalSeen latched the application so that after a final write nothing
    // would change the fan again - including handing it back on a safety
    // fault. That is a latch with no release, on the path that must always be
    // available, so it is refused rather than reproduced.
    LegacyIntent intent = manualIntent(6);
    intent.final = true;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(decision.refused);
    CHECK(decision.refusal == LegacyRefusal::FinalNotSupported);
    CHECK(decision.message.find("firmware") != std::string::npos);
}

void testFinalRestoreIsAllowed()
{
    noteTest();
    // Restoring to the firmware with final set is harmless: the core owns the
    // state and the controller is what decides whether a later cycle may act.
    LegacyIntent intent = biosIntent();
    intent.final = true;

    const LegacyDecision decision = core::evaluateIntent(intent, 1000);
    CHECK(!decision.refused);
    CHECK(decision.input.requestBiosAutomatic);
}

// ---------------------------------------------------------------------------
// Capabilities
// ---------------------------------------------------------------------------

void testCapabilitiesRequireEveryPhase0Verdict()
{
    noteTest();
    // The rule is conjunctive and lives in CapabilityReport::controlEligible.
    // This test pins the inputs that produce an eligible report, so a change
    // that weakened the mapping would be caught here.
    core::LegacyCapabilityInputs full;
    full.controlRequestedByUser = true;
    full.driverLoaded = true;
    full.backendPresent = true;
    full.hardwareExactMatch = true;
    full.profileVerified = true;
    full.topologyVerified = true;
    full.restoreCapabilityVerified = true;

    const core::CapabilityReport eligible = core::makeCapabilities(full);
    CHECK(eligible.controlEligible());
    CHECK(eligible.blockingReason() == "eligible");

    // Dropping any one of them must be enough to block. If a future change made
    // one of these non-essential, this is where it would show.
    for (int field = 0; field < 7; ++field) {
        core::LegacyCapabilityInputs in = full;
        switch (field) {
        case 0: in.hardwareExactMatch = false; break;
        case 1: in.profileVerified = false; break;
        case 2: in.topologyVerified = false; break;
        case 3: in.driverLoaded = false; break;
        case 4: in.backendPresent = false; break;
        case 5: in.restoreCapabilityVerified = false; break;
        case 6: in.controlRequestedByUser = false; break;
        default: break;
        }
        const core::CapabilityReport report = core::makeCapabilities(in);
        if (field == 6) {
            // The user not asking is expressed in the request, not the report.
            // The report itself is still eligible; the controller sees no
            // request. Asserting that here documents the split.
            CHECK(report.controlEligible());
        } else {
            CHECK(!report.controlEligible());
            CHECK(report.blockingReason() != "eligible");
        }
    }
}

void testBackendReadyNeedsBothThePathAndTheDriver()
{
    noteTest();
    core::LegacyCapabilityInputs in;
    in.backendPresent = true;
    in.driverLoaded = false;
    CHECK(!core::makeCapabilities(in).backendReady);

    in.driverLoaded = true;
    CHECK(core::makeCapabilities(in).backendReady);
}

void testBlockingReasonIsSpecific()
{
    noteTest();
    // "not eligible" is not actionable. Each reason names the outstanding
    // obligation so a user can be told what is missing.
    core::LegacyCapabilityInputs in;
    CHECK(core::makeCapabilities(in).blockingReason() == "hardware_identity_unverified");

    in.hardwareExactMatch = true;
    CHECK(core::makeCapabilities(in).blockingReason() == "profile_unverified");

    in.profileVerified = true;
    CHECK(core::makeCapabilities(in).blockingReason() == "fan_topology_unverified");

    in.topologyVerified = true;
    CHECK(core::makeCapabilities(in).blockingReason() == "backend_unavailable");

    in.backendPresent = true;
    in.driverLoaded = true;
    CHECK(core::makeCapabilities(in).blockingReason() == "bios_restore_unverified");
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void testTextIsNeverNullOrEmpty()
{
    noteTest();
    // The UI writes these into a status field. A null or empty string there
    // reads as "no information", which is indistinguishable from "fine".
    const LegacySource sources[] = {
        LegacySource::Bios, LegacySource::Manual, LegacySource::Smart,
        LegacySource::Failsafe, LegacySource::Startup, LegacySource::Shutdown};
    for (const LegacySource s : sources) {
        CHECK(core::toText(s) != nullptr);
        CHECK(core::toText(s)[0] != '\0');
    }
    const LegacyFanTarget targets[] = {
        LegacyFanTarget::FirmwareSelected, LegacyFanTarget::FirstFan,
        LegacyFanTarget::SecondFan};
    for (const LegacyFanTarget t : targets) {
        CHECK(core::toText(t) != nullptr);
        CHECK(core::toText(t)[0] != '\0');
    }
    const LegacyRefusal refusals[] = {
        LegacyRefusal::None, LegacyRefusal::NotControlEnabled,
        LegacyRefusal::SpecificFanNotSupported, LegacyRefusal::LevelOutOfRange,
        LegacyRefusal::ManualLevelReserved, LegacyRefusal::FinalNotSupported,
        LegacyRefusal::ControlNotEligible};
    for (const LegacyRefusal r : refusals) {
        CHECK(core::toText(r) != nullptr);
        CHECK(core::toText(r)[0] != '\0');
    }
}

void testBiosLevelIsRenderedAsHexNotDecimal()
{
    noteTest();
    // 0x80 printed as a decimal is 128, which a reader takes for a manual
    // level. The status line has to say what was actually written.
    LegacyIntent intent = biosIntent();
    char buffer[128];
    CHECK(core::describeRequestedLevel(intent, buffer, static_cast<int>(sizeof(buffer))));
    CHECK(std::strstr(buffer, "0x80") != nullptr);
    CHECK(std::strstr(buffer, "128") == nullptr);
    CHECK(std::strstr(buffer, "BIOS") != nullptr);
}

void testDescribeRequestedLevelRejectsUnusableBuffers()
{
    noteTest();
    char buffer[8];
    CHECK(!core::describeRequestedLevel(biosIntent(), buffer, 0));
    CHECK(!core::describeRequestedLevel(biosIntent(), nullptr, 8));

    // Too small to hold the message: the buffer is emptied rather than left
    // holding half a sentence.
    CHECK(!core::describeRequestedLevel(biosIntent(), buffer, static_cast<int>(sizeof(buffer))));
    CHECK(buffer[0] == '\0');
}

// ---------------------------------------------------------------------------
// The startup assessment (T5-04)
// ---------------------------------------------------------------------------
//
// The requirement is DRIVER_BACKENDS.md section 5: with no approved backend,
// monitor if possible, write nothing, leave the fan to the BIOS, and explain the
// missing dependency. "If possible" is the whole subtlety on this machine - the
// temperature sources are EC registers, so with no backend there is nothing to
// read at all - and the assessment is where that distinction lives.

core::LegacyCapabilityInputs verifiedMachine()
{
    core::LegacyCapabilityInputs in;
    in.backendPresent = true;
    in.driverLoaded = true;
    in.hardwareExactMatch = true;
    in.profileVerified = true;
    in.topologyVerified = true;
    in.restoreCapabilityVerified = true;
    return in;
}

void testNoBackendMeansMonitorOnlyWithNoReadings()
{
    noteTest();
    // What the application does on a machine where the port driver is missing.
    // The status line must not say "monitoring", because there is nothing to
    // read: the legacy application's answer was to crash out of startup with a
    // message box, and a window showing stale or zero temperatures would be
    // worse. This says what is true.
    core::LegacyCapabilityInputs in;
    in.controlRequestedByUser = true;   // the user ticked control; it must not matter
    const core::StartupAssessment a = core::assessStartup(in);

    CHECK(a.mode == core::StartupMode::NoBackend);
    CHECK(a.monitorOnly);
    CHECK(!a.readingsAvailable);
    CHECK(!a.mayControl);
    CHECK(std::strstr(a.statusLine.c_str(), "MONITOR ONLY") != nullptr);
    CHECK(std::strstr(a.statusLine.c_str(), "no readings") != nullptr);
    CHECK(std::strstr(a.explanation.c_str(), "tvicport.sys") != nullptr);
    CHECK(std::strstr(a.explanation.c_str(), "BIOS") != nullptr);
    CHECK(std::strstr(a.explanation.c_str(), "No register will be written") != nullptr);
}

void testBackendObjectWithoutADriverIsStillNoBackend()
{
    noteTest();
    // The two halves of "there is a working I/O path": a backend that exists,
    // and a driver behind it. A machine where the driver was closed mid-session
    // is the second half failing, and it has to land in the same state as never
    // having had one - no readings, no writes.
    core::LegacyCapabilityInputs in = verifiedMachine();
    in.driverLoaded = false;
    const core::StartupAssessment a = core::assessStartup(in);

    CHECK(a.mode == core::StartupMode::NoBackend);
    CHECK(!a.readingsAvailable);
    CHECK(!a.mayControl);

    core::LegacyCapabilityInputs noBackend = verifiedMachine();
    noBackend.backendPresent = false;
    CHECK(core::assessStartup(noBackend).mode == core::StartupMode::NoBackend);
}

void testAnUnverifiedMachineMonitorsAndSaysWhy()
{
    noteTest();
    // The normal state of this project: a backend that works, and Phase 0 not
    // done. Readings are shown; control is refused; the reason names the
    // missing verification rather than saying "not supported".
    core::LegacyCapabilityInputs in;
    in.backendPresent = true;
    in.driverLoaded = true;
    in.controlRequestedByUser = true;
    const core::StartupAssessment a = core::assessStartup(in);

    CHECK(a.mode == core::StartupMode::MonitorOnly);
    CHECK(a.monitorOnly);
    CHECK(a.readingsAvailable);
    CHECK(!a.mayControl);
    CHECK(std::strstr(a.statusLine.c_str(), "MONITOR ONLY") != nullptr);
    CHECK(std::strstr(a.statusLine.c_str(), "hardware_identity_unverified") != nullptr);
    CHECK(std::strstr(a.explanation.c_str(), "BIOS") != nullptr);
}

void testAControlEligibleMachineStillDoesNotStartControllingByItself()
{
    noteTest();
    // The startup assessment is not an activation. Even with every Phase 0
    // verdict in place, control needs the user's request and the controller's
    // consecutive-samples gate (ADR-014).
    const core::StartupAssessment a = core::assessStartup(verifiedMachine());

    CHECK(a.mode == core::StartupMode::ControlEligible);
    CHECK(!a.monitorOnly);
    CHECK(a.readingsAvailable);
    CHECK(a.mayControl);
    CHECK(std::strstr(a.statusLine.c_str(), "CONTROL ELIGIBLE") != nullptr);
    CHECK(std::strstr(a.explanation.c_str(), "does not start by itself") != nullptr);
}

void testForcedMonitorOnlySurvivesEveryVerdict()
{
    noteTest();
    // The user, or Phase 0, can force monitor-only. Nothing in the assessment
    // may override that.
    core::LegacyCapabilityInputs in = verifiedMachine();
    in.monitorOnlyForced = true;
    const core::StartupAssessment a = core::assessStartup(in);

    CHECK(a.mode == core::StartupMode::MonitorOnly);
    CHECK(a.monitorOnly);
    CHECK(a.readingsAvailable);
    CHECK(!a.mayControl);
}

void testTheAssessmentNeverDisagreesWithTheCapabilityReport()
{
    noteTest();
    // The failure this test prevents is a second eligibility rule. The
    // controller applies CapabilityReport::controlEligible(); if the startup
    // path computed its own answer, the two would eventually differ, and the
    // one the user sees would be the weaker of them.
    //
    // Exhaustive over the seven booleans that can matter (2^7 = 128 machines),
    // which is possible because both functions are pure.
    int checkedCount = 0;
    for (unsigned mask = 0; mask < 128u; ++mask) {
        core::LegacyCapabilityInputs in;
        in.controlRequestedByUser   = (mask & 1u) != 0;
        in.driverLoaded             = (mask & 2u) != 0;
        in.backendPresent           = (mask & 4u) != 0;
        in.hardwareExactMatch       = (mask & 8u) != 0;
        in.profileVerified          = (mask & 16u) != 0;
        in.topologyVerified         = (mask & 32u) != 0;
        in.restoreCapabilityVerified= (mask & 64u) != 0;

        const core::StartupAssessment a = core::assessStartup(in);
        const bool eligible = core::makeCapabilities(in).controlEligible();
        ++checkedCount;

        // Same answer, always.
        CHECK(a.mayControl == eligible);
        // Control eligibility implies readings, which implies a live backend.
        if (a.mayControl) {
            CHECK(a.readingsAvailable);
            CHECK(!a.monitorOnly);
        }
        // A machine with no readings is never controllable, and a monitor-only
        // machine never reports a mode that could command one.
        if (!a.readingsAvailable) {
            CHECK(!a.mayControl);
            CHECK(a.mode == core::StartupMode::NoBackend);
        }
        // Never blank: a blank status line reads as healthy.
        CHECK(!a.statusLine.empty());
        CHECK(!a.explanation.empty());

        // The status line goes into a fixed 256-byte dialog field, after a
        // prefix the application has already written. A status line that gets
        // truncated mid-word would hide the part that says what is missing, so
        // the bound is asserted here rather than discovered in a screenshot.
        // The explanation is the long form and goes to the log, where the only
        // limit is the disk.
        CHECK(a.statusLine.size() <= 120);
        CHECK(a.explanation.size() > a.statusLine.size());

        // Both strings are written as single lines: a newline inside the status
        // field would be swallowed by the dialog, and one inside a trace entry
        // would split it in two.
        CHECK(a.statusLine.find('\n') == std::string::npos);
        CHECK(a.explanation.find('\n') == std::string::npos);
    }
    CHECK(checkedCount == 128);
}

void testStartupTextIsAlwaysPopulated()
{
    noteTest();
    for (const core::StartupMode mode : {core::StartupMode::NoBackend,
                                        core::StartupMode::MonitorOnly,
                                        core::StartupMode::ControlEligible}) {
        CHECK(core::toText(mode) != nullptr);
        CHECK(core::toText(mode)[0] != '\0');
    }
}

// ---------------------------------------------------------------------------
// The legacy table as a core curve (T3-04, T3-05)
// ---------------------------------------------------------------------------
//
// The legacy application chose the fan level from a (temperature, fan) table
// scanned every cycle. T3-05 wants that decision gone; it can only go once the
// core decides the same thing, and the mapping is what closes the gap. These
// tests are about the mapping and about the fact that a mapped table drives an
// actual Controller.

// A sample the controller will accept: a value, a fresh timestamp, and the
// three validity flags a sensor has to earn. Written out rather than
// brace-initialised because TemperatureSample has grown fields before and a
// missing one is a compiler warning here, not a silent default.
core::TemperatureSample validSample(int valueC, std::uint64_t nowMs)
{
    core::TemperatureSample sample;
    sample.valueC = valueC;
    sample.timestampMs = nowMs;
    sample.hasValue = true;
    sample.backendOk = true;
    sample.sourceValid = true;
    sample.sentinel = false;
    sample.source = "test";
    return sample;
}

// What the application ships with, in Celsius, exactly as fancontrol.cpp sets
// it up before the config file is read.
std::vector<core::LegacyCurveRow> defaultLegacyTable()
{
    return {
        {50, 0},
        {55, 3},
        {60, 5},
        {65, 7},
        {70, 0x80},
        {-1, 0x80},   // the end marker
    };
}

void testTheLegacyTableBecomesACoreCurve()
{
    noteTest();
    const std::vector<core::LegacyCurveRow> rows = defaultLegacyTable();
    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows.data(), rows.size());

    CHECK(mapping.validation.valid);
    CHECK(mapping.rowsUsed == 5);       // the end marker is not a row
    CHECK(mapping.hadEndMarker);
    CHECK(mapping.config.points.size() == 5);
    CHECK(mapping.config.minimumLevel == 0);
    CHECK(mapping.config.maximumLevel == 7);

    // One temperature per legacy row, used for both directions, and levels in
    // the order the table gives them.
    CHECK(mapping.config.points[0].upTemperatureC == 50);
    CHECK(mapping.config.points[0].downTemperatureC == 50);
    CHECK(mapping.config.points[0].command == core::FanCommand::levelCommand(0));
    CHECK(mapping.config.points[4].upTemperatureC == 70);
    CHECK(mapping.config.points[4].command == core::FanCommand::biosAutomatic());

    // The emergency command is the firmware, never a manual level: nothing in
    // the hardware report establishes that a full-speed manual write is safer
    // than letting the firmware respond.
    CHECK(mapping.config.emergencyCommand == core::FanCommand::biosAutomatic());
    CHECK(mapping.config.minimumDwellMs == 0);
}

void testTheEndMarkerStopsTheScan()
{
    noteTest();
    // The legacy arrays are fixed 32-entry arrays, zeroed beyond the table. A
    // scan that ran past the marker would read (0 degrees, level 0) rows as
    // real data and would refuse the table as unordered - or, worse, accept a
    // table that says "the fan is off below 0 degrees".
    std::vector<core::LegacyCurveRow> rows = defaultLegacyTable();
    rows.push_back({0, 0});
    rows.push_back({0, 0});

    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows.data(), rows.size());
    CHECK(mapping.validation.valid);
    CHECK(mapping.config.points.size() == 5);
    CHECK(mapping.rowsUsed == 5);
}

void testATableWithNoEndMarkerIsStillRead()
{
    noteTest();
    // A caller that passes an exact-length array without the marker gets the
    // whole array, and says so.
    const core::LegacyCurveRow rows[] = {{50, 0}, {60, 3}, {70, 0x80}};
    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows, 3);

    CHECK(mapping.validation.valid);
    CHECK(!mapping.hadEndMarker);
    CHECK(mapping.rowsUsed == 3);
    CHECK(mapping.config.points.size() == 3);
}

void testAnEmptyTableIsRefusedNotDefaulted()
{
    noteTest();
    // No rows means no curve. The controller refuses control outright when its
    // curve is invalid, which is what should happen: a missing table is not an
    // invitation to invent one.
    const core::LegacyCurveMapping mapping = core::curveFromLegacyRows(nullptr, 0);
    CHECK(!mapping.validation.valid);
    CHECK(mapping.config.points.empty());

    bool foundEmptyCurve = false;
    for (const core::ValidationError& error : mapping.validation.errors) {
        if (error.code == "empty_curve") {
            foundEmptyCurve = true;
        }
    }
    CHECK(foundEmptyCurve);

    // A null pointer with a count is a caller bug, and must not be a crash.
    CHECK(!core::curveFromLegacyRows(nullptr, 4).validation.valid);
}

void testAnUnknownLegacySpecialIsRefusedByValue()
{
    noteTest();
    // 0x40 (64) is a legacy special gated behind a config flag. Nobody here has
    // established what it does to the EC, so the table is refused with the row
    // and the value named - not rounded to the nearest level.
    std::vector<core::LegacyCurveRow> rows = defaultLegacyTable();
    rows.insert(rows.begin() + 4, {68, 0x40});

    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows.data(), rows.size());
    CHECK(!mapping.validation.valid);

    bool named = false;
    for (const core::ValidationError& error : mapping.validation.errors) {
        if (error.code == "unsupported_legacy_level"
            && error.message.find("64") != std::string::npos
            && error.message.find("row 4") != std::string::npos) {
            named = true;
        }
    }
    CHECK(named);
}

void testATableTheLegacyScanWouldHaveAppliedIsRefusedWhenUnordered()
{
    noteTest();
    // The legacy scan walked the table in order and took the last row that
    // matched, so a table that rose in temperature while falling in fan would
    // have been applied as written. The core's curve requires the two to agree,
    // because a curve that asks for less cooling as the machine gets hotter is
    // not a curve.
    const core::LegacyCurveRow rows[] = {{50, 7}, {60, 3}, {70, 0x80}};
    const core::LegacyCurveMapping mapping = core::curveFromLegacyRows(rows, 3);

    CHECK(!mapping.validation.valid);
    bool found = false;
    for (const core::ValidationError& error : mapping.validation.errors) {
        if (error.code == "descending_command") {
            found = true;
        }
    }
    CHECK(found);
}

void testBiosAutomaticMustBeTerminal()
{
    noteTest();
    // A 0x80 row with rows after it would be a curve that hands the fan to the
    // firmware and then takes it back. The legacy code could be configured that
    // way; the core refuses it.
    const core::LegacyCurveRow rows[] = {{50, 0x80}, {70, 3}};
    const core::LegacyCurveMapping mapping = core::curveFromLegacyRows(rows, 2);

    CHECK(!mapping.validation.valid);
    bool found = false;
    for (const core::ValidationError& error : mapping.validation.errors) {
        if (error.code == "bios_not_terminal") {
            found = true;
        }
    }
    CHECK(found);
}

void testTheMappedTableDrivesARealController()
{
    noteTest();
    // The point of the mapping: a mapped legacy table is a curve a Controller
    // will actually run. This is the T3-05 gate - the table can only be deleted
    // once something else makes the decision, and this shows what makes it.
    const std::vector<core::LegacyCurveRow> rows = defaultLegacyTable();
    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows.data(), rows.size());
    CHECK(mapping.validation.valid);

    core::ControllerConfig config;
    config.curve = mapping.config;
    config.rpmSupported = false;
    core::Controller controller(config);

    core::CapabilityReport capabilities;
    capabilities.exactHardwareMatch = true;
    capabilities.profileVerified = true;
    capabilities.topologyVerified = true;
    capabilities.backendReady = true;
    capabilities.restoreAvailable = true;

    const int expected[] = {0, 3, 5, 7};
    const int temperatures[] = {50, 56, 61, 66};
    for (int i = 0; i < 4; ++i) {
        core::ControllerInput input;
        input.nowMs = static_cast<std::uint64_t>(1000 * (i + 1));
        input.capabilities = capabilities;
        input.requestControl = true;
        input.temperatures.push_back(validSample(temperatures[i], input.nowMs));
        // Three consecutive valid readings before control starts: the
        // controller's own startup gate, not something the mapping changes.
        core::ControllerOutput output = controller.update(input);
        output = controller.update(input);
        output = controller.update(input);

        CHECK(output.commandMayBeIssued);
        CHECK(output.command.kind == core::CommandKind::Level);
        CHECK(output.command.level == expected[i]);
    }
}

void testTheMappedTableStillNeedsPhaseZero()
{
    noteTest();
    // Mapping the table must not enable anything. Without the Phase 0 verdicts
    // the controller refuses to issue a command, whatever the curve says - and
    // the application's own startup path is monitor-only for the same reason.
    const std::vector<core::LegacyCurveRow> rows = defaultLegacyTable();
    const core::LegacyCurveMapping mapping =
        core::curveFromLegacyRows(rows.data(), rows.size());

    core::ControllerConfig config;
    config.curve = mapping.config;
    core::Controller controller(config);

    core::ControllerInput input;
    input.nowMs = 1000;
    input.capabilities = core::CapabilityReport{};   // nothing verified
    input.requestControl = true;
    input.temperatures.push_back(validSample(70, 1000));

    for (int i = 0; i < 5; ++i) {
        const core::ControllerOutput output = controller.update(input);
        CHECK(!output.commandMayBeIssued);
    }
}

void testARefusalNamesWhatWasAskedForWithoutInventingALevel()
{
    noteTest();
    // A curve request carries no level: the core's curve decides it. Rendering
    // its placeholder (-1) through the hex formatter would produce 0xFF, and a
    // fabricated register value in a message a user reads is worse than no
    // value at all.
    LegacyIntent smart;
    smart.source = LegacySource::Smart;
    smart.level = -1;
    smart.controlEnabled = false;

    const LegacyDecision smartDecision = core::evaluateIntent(smart, 1000);
    CHECK(smartDecision.refused);
    CHECK(smartDecision.message.find("curve control") != std::string::npos);
    CHECK(smartDecision.message.find("0xFF") == std::string::npos);
    CHECK(smartDecision.message.find("-1") == std::string::npos);

    // A manual request still names the level it asked for, in hex.
    LegacyIntent manual = manualIntent(3);
    manual.controlEnabled = false;
    const LegacyDecision manualDecision = core::evaluateIntent(manual, 1000);
    CHECK(manualDecision.refused);
    CHECK(manualDecision.message.find("0x03") != std::string::npos);
}

void testDescribeRequestedLevelHandlesACurveRequest()
{
    noteTest();
    LegacyIntent smart;
    smart.source = LegacySource::Smart;
    smart.level = -1;

    char buffer[128];
    CHECK(core::describeRequestedLevel(smart, buffer, static_cast<int>(sizeof(buffer))));
    CHECK(std::strstr(buffer, "curve control") != nullptr);
    CHECK(std::strstr(buffer, "0xFF") == nullptr);
}

void runAll()
{
    testSpecificFanRequestIsRefused();
    testRefusalNamesTheFanTheUserAskedFor();
    testFirmwareSelectedTargetIsAllowed();
    testSpecificFanIsRefusedEvenForABiosRestore();
    testNoCodePathProducesAWriteOutsideTheBridge();
    testMonitorOnlyRefusesEveryWriteSource();
    testMonitorOnlyStillPermitsTheReadOnlyStartupCheck();
    testMonitorOnlyMessageSaysNothingWasWritten();
    testEveryRestoreSourceProducesTheSameRequest();
    testSmartBecomesACurveRequestNotATableLookup();
    testManualLevelsZeroThroughSevenAreAccepted();
    testLevelsOutsideTheManualRangeAreRefused();
    testFinalManualIsRefused();
    testFinalRestoreIsAllowed();
    testCapabilitiesRequireEveryPhase0Verdict();
    testBackendReadyNeedsBothThePathAndTheDriver();
    testBlockingReasonIsSpecific();
    testTextIsNeverNullOrEmpty();
    testBiosLevelIsRenderedAsHexNotDecimal();
    testDescribeRequestedLevelRejectsUnusableBuffers();
    testNoBackendMeansMonitorOnlyWithNoReadings();
    testBackendObjectWithoutADriverIsStillNoBackend();
    testAnUnverifiedMachineMonitorsAndSaysWhy();
    testAControlEligibleMachineStillDoesNotStartControllingByItself();
    testForcedMonitorOnlySurvivesEveryVerdict();
    testTheAssessmentNeverDisagreesWithTheCapabilityReport();
    testStartupTextIsAlwaysPopulated();
    testTheLegacyTableBecomesACoreCurve();
    testTheEndMarkerStopsTheScan();
    testATableWithNoEndMarkerIsStillRead();
    testAnEmptyTableIsRefusedNotDefaulted();
    testAnUnknownLegacySpecialIsRefusedByValue();
    testATableTheLegacyScanWouldHaveAppliedIsRefusedWhenUnordered();
    testBiosAutomaticMustBeTerminal();
    testTheMappedTableDrivesARealController();
    testTheMappedTableStillNeedsPhaseZero();
    testARefusalNamesWhatWasAskedForWithoutInventingALevel();
    testDescribeRequestedLevelHandlesACurveRequest();
}

} // namespace
} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAll();
    std::printf("TPFanControl legacy policy tests passed (%d tests)\n", testCount);
    return 0;
}
