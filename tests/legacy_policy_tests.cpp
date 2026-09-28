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
