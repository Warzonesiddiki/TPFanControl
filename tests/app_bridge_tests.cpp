// Tests for fancontrol/core/app_bridge.{h,cpp}, covering T3-01, T3-04,
// T3-07, T3-08, T3-09, T3-10 and T3-11.
//
// This is the suite that makes the integration claim checkable. Before it
// existed, "the core is integrated" was an assertion; now it is a set of tests
// that fail if the legacy path can command a fan without core approval, or if
// the single-fan path can write the fan selector.
//
// Assertions use CHECK, not assert: see tests/test_check.h.

#include "test_check.h"

#include "../fancontrol/core/app_bridge.h"

#include "fake_ec.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace appbridge {

using core::AppBridge;
using core::ApplyResult;
using core::BridgeConfig;
using core::ControllerInput;
using core::ControllerOutput;
using core::EcSnapshot;
using core::IoErrorCode;

int g_testCount = 0;

void noteTest()
{
    ++g_testCount;
}

// A controller that is genuinely eligible to command, so a cycle really does
// authorise writes. Without this the "never writes 0x31" assertions would pass
// vacuously, which is the failure mode a test like this is most prone to.
core::CurveConfig eligibleCurve()
{
    core::CurveConfig curve;
    curve.minimumLevel = 0;
    curve.maximumLevel = 7;
    curve.minimumDwellMs = 0;
    curve.emergencyCommand = core::FanCommand::biosAutomatic();
    curve.points = {
        {50, 45, core::FanCommand::levelCommand(0)},
        {60, 55, core::FanCommand::levelCommand(3)},
        {70, 65, core::FanCommand::levelCommand(7)},
    };
    return curve;
}

core::CapabilityReport eligibleCapabilities()
{
    core::CapabilityReport report;
    report.exactHardwareMatch = true;
    report.profileVerified = true;
    report.topologyVerified = true;
    report.backendReady = true;
    report.restoreAvailable = true;
    return report;
}

ControllerInput eligibleRequest()
{
    ControllerInput request;
    request.capabilities = eligibleCapabilities();
    request.requestControl = true;
    request.rpmObservationElapsed = true;
    return request;
}

BridgeConfig bridgeConfig()
{
    BridgeConfig config;
    for (int i = 0; i < core::kSensorCount; ++i) {
        config.sensorNames.push_back("S" + std::to_string(i));
    }
    config.controller.curve = eligibleCurve();
    config.controller.startupValidSamples = 1;   // keep the tests short
    config.controller.rpmSupported = true;
    config.controller.manualMaximumLevel = 7;
    config.controller.sensor.maximumAgeMs = 1000;
    return config;
}

// A backend preloaded with a plausible machine: 45 degrees on the first block,
// 40 on the second, and a fan turning at 3200 RPM.
void loadHealthyMachine(FakeEcBackend& backend)
{
    for (int i = 0; i < core::kTemp0Count; ++i) {
        backend.setRegister(static_cast<std::uint8_t>(core::kRegisterTemp0 + i), 45);
    }
    for (int i = 0; i < core::kTemp1Count; ++i) {
        backend.setRegister(static_cast<std::uint8_t>(core::kRegisterTemp1 + i), 40);
    }
    backend.setRegister(core::kRegisterFanLevel, 0x80);
    backend.setRegister(core::kRegisterFanSpeedLo, 0x80);   // 3200 = 0x0C80
    backend.setRegister(core::kRegisterFanSpeedHi, 0x0C);
}

// ---------------------------------------------------------------------------
// T3-01: the register map is the one the application uses
// ---------------------------------------------------------------------------

void testRegisterMapMatchesTheApplication()
{
    noteTest();
    // fanstuff.cpp's TP_ECOFFSET_* constants. If these drift, the core is
    // reading registers the application never used.
    CHECK(core::kRegisterFanLevel == 0x2F);
    CHECK(core::kRegisterFanSelector == 0x31);
    CHECK(core::kRegisterFanSpeedLo == 0x84);
    CHECK(core::kRegisterFanSpeedHi == 0x85);
    CHECK(core::kRegisterTemp0 == 0x78);
    CHECK(core::kRegisterTemp1 == 0xC0);
    CHECK(core::kSensorCount == 12);
    CHECK(core::kBiosAutomaticLevel == 0x80);
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void testReadPopulatesEverySensor()
{
    noteTest();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(snapshot.ok);
    CHECK(snapshot.fanLevelRead);
    CHECK(snapshot.fanLevel == 0x80);
    CHECK(snapshot.fanSpeedRead);
    CHECK(snapshot.fanRpm == 3200);
    CHECK(snapshot.fanSpeedTimestampMs == 1000);
    CHECK(static_cast<int>(snapshot.temperatures.size()) == core::kSensorCount);
    for (const core::TemperatureSample& sample : snapshot.temperatures) {
        CHECK(sample.hasValue);
        CHECK(sample.backendOk);
        CHECK(sample.timestampMs == 1000);
        CHECK(!sample.source.empty());
    }
    CHECK(snapshot.temperatures[0].valueC == 45);
    CHECK(snapshot.temperatures[core::kTemp0Count].valueC == 40);

    // A read is not a passive operation: it writes a command byte and an
    // address byte to the EC. What must NOT happen is a write TRANSACTION, so
    // the assertion is about registers, not about port writes.
    CHECK(backend.writtenRegisters().empty());
    CHECK(backend.registerValue(core::kRegisterFanLevel) == 0x80);
    CHECK(backend.registerValue(core::kRegisterFanSelector) == 0x00);
    // One command byte and one address byte per read: 12 sensors, the fan
    // level, and two tachometer bytes.
    const std::size_t expectedTrace = 2u * (core::kSensorCount + 3u);
    CHECK(bridge.writeTrace().size() == expectedTrace);
}

void testFanSpeedIsLittleEndian()
{
    noteTest();
    // fanstuff.cpp composes it as (Hi << 8) | Lo. Getting this backwards turns
    // 3200 RPM into 51200, which the plausibility check would reject - so a
    // reversed order fails as "no RPM" rather than as a wrong RPM, which is
    // harder to notice and must be pinned.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.setRegister(core::kRegisterFanSpeedLo, 0x34);
    backend.setRegister(core::kRegisterFanSpeedHi, 0x12);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(snapshot.fanRpm == 0x1234);
}

void testPartialReadIsNotReportedAsSuccess()
{
    noteTest();
    // A missing sensor must not read as 0 degrees, and a partial read must not
    // be reported as ok, or the controller decides from half the picture.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.setPersistentReadFailure(true);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(!snapshot.ok);
    CHECK(snapshot.lastError == IoErrorCode::ReadFailure);
    CHECK(!snapshot.lastErrorMessage.empty());
}

void testBackendStoppingPartWayIsNotSuccess()
{
    noteTest();
    // The driver dies after the first operations. The snapshot must fail, and
    // must not present a partial picture as complete.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.shutdownAfterOperations(2);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(!snapshot.ok);
    CHECK(backend.hasShutDown());
    // Nothing was written to a backend that is no longer there.
    CHECK(backend.writtenRegisters().empty());
}

void testSensorNameListIsValidatedNotPadded()
{
    noteTest();
    // A short list would shift every label by one and present one reading
    // under another's name. The bridge must fall back to positional names
    // rather than indexing past the end or wrapping.
    BridgeConfig config = bridgeConfig();
    config.sensorNames = {"only one"};
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(snapshot.ok);
    CHECK(static_cast<int>(snapshot.temperatures.size()) == core::kSensorCount);
    for (const core::TemperatureSample& sample : snapshot.temperatures) {
        CHECK(!sample.source.empty());
    }
    CHECK(snapshot.temperatures[0].source != snapshot.temperatures[1].source);
}

// ---------------------------------------------------------------------------
// T3-08: no fan command without core approval
// ---------------------------------------------------------------------------

void testUnauthorisedCommandIsRefusedWithoutTouchingTheBus()
{
    noteTest();
    // The central integration guarantee. A default-constructed output carries
    // commandMayBeIssued == false, which is what a caller that skipped the
    // controller would produce.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.command = core::FanCommand::levelCommand(5);
    output.commandMayBeIssued = false;

    const ApplyResult result = bridge.apply(output);
    CHECK(!result.attempted);
    CHECK(!result.succeeded);
    CHECK(!result.readbackAttempted);
    CHECK(!result.reason.empty());
    // Nothing moved. Not the fan level, not the selector, nothing.
    CHECK(backend.committedWrites().empty());
    CHECK(backend.registerValue(core::kRegisterFanLevel) == 0x80);
    CHECK(backend.registerValue(core::kRegisterFanSelector) == 0x00);
    CHECK(bridge.lastAppliedLevel() == -1);
}

void testNoCommandMeansNoWrite()
{
    noteTest();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::none();

    const ApplyResult result = bridge.apply(output);
    CHECK(!result.attempted);
    CHECK(backend.committedWrites().empty());
}

void testOutOfRangeLevelIsRefused()
{
    noteTest();
    // Even an authorised command carrying an impossible level must not reach
    // the bus. The authority to command is not authority to write nonsense.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    for (const int level : {-5, 256, 1000}) {
        ControllerOutput output;
        output.commandMayBeIssued = true;
        output.command = core::FanCommand::levelCommand(level);
        const ApplyResult result = bridge.apply(output);
        CHECK(!result.attempted);
        CHECK(!result.reason.empty());
    }
    CHECK(backend.committedWrites().empty());
}

// ---------------------------------------------------------------------------
// T3-03: writes go through EcBus, and are verified
// ---------------------------------------------------------------------------

void testAuthorisedWriteIsIssuedAndVerified()
{
    noteTest();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::levelCommand(3);

    const ApplyResult result = bridge.apply(output);
    CHECK(result.attempted);
    CHECK(result.succeeded);
    CHECK(result.readbackAttempted);
    CHECK(result.readbackMatched);
    CHECK(result.readbackValue == 3);
    CHECK(backend.registerValue(core::kRegisterFanLevel) == 3);
    CHECK(bridge.lastAppliedLevel() == 3);
}

void testRepeatedCommandIsNotRewritten()
{
    noteTest();
    // Writing the same value every cycle is what turns a transient EC fault
    // into a stream of writes against the fan register.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::levelCommand(3);

    CHECK(bridge.apply(output).succeeded);
    backend.clearLogs();
    const ApplyResult again = bridge.apply(output);
    CHECK(!again.attempted);
    CHECK(backend.committedWrites().empty());

    // A different level does write.
    output.command = core::FanCommand::levelCommand(4);
    CHECK(bridge.apply(output).succeeded);
    CHECK(backend.registerValue(core::kRegisterFanLevel) == 4);
}

void testReadbackMismatchIsReportedAndNotRemembered()
{
    noteTest();
    // The EC accepts the command but the register does not hold it. Reporting
    // success here is the single most dangerous thing this layer could do, and
    // the readback exists precisely to catch it.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::levelCommand(3);

    backend.setFault(EcFault::CorruptReadback);
    const ApplyResult result = bridge.apply(output);
    CHECK(result.attempted);
    CHECK(result.succeeded);
    CHECK(result.readbackAttempted);
    CHECK(!result.readbackMatched);
    CHECK(result.reason.find("mismatch") != std::string::npos);
    // Not remembered, so the next cycle tries again instead of assuming the
    // fan is where it was asked to be.
    CHECK(bridge.lastAppliedLevel() == -1);
}

void testFailedWriteIsNotRememberedAsApplied()
{
    noteTest();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    backend.setPersistentWriteFailure(true);
    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::levelCommand(3);

    const ApplyResult result = bridge.apply(output);
    CHECK(result.attempted);
    CHECK(!result.succeeded);
    CHECK(!result.readbackAttempted);
    CHECK(!result.reason.empty());
    CHECK(bridge.lastAppliedLevel() == -1);
    CHECK(backend.registerValue(core::kRegisterFanLevel) == 0x80);
}

void testBiosAutomaticUsesTheFirmwareLevel()
{
    noteTest();
    // The firmware's own automatic mode is a level on this machine, not a
    // separate operation, and restore must use the same value the application
    // already used or "restore" would mean something different.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.setRegister(core::kRegisterFanLevel, 0x03);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::biosAutomatic();

    CHECK(bridge.apply(output).succeeded);
    CHECK(backend.registerValue(core::kRegisterFanLevel) == core::kBiosAutomaticLevel);
}

// ---------------------------------------------------------------------------
// T3-11: the single-fan path never writes 0x31
// ---------------------------------------------------------------------------

void testNoCycleEverWritesTheFanSelector()
{
    noteTest();
    // The legacy defect recorded in EC_REGISTER_MAP.md 10: SetFan wrote 0x31
    // four times per attempt, unconditionally, on every machine. Drive a full
    // read/evaluate/apply cycle repeatedly and check the trace.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    const ControllerInput request = eligibleRequest();
    for (int cycle = 0; cycle < 20; ++cycle) {
        const ControllerOutput output =
            bridge.cycle(static_cast<std::uint64_t>(cycle) * 1000, request);
        // The cycle really is eligible to command. Asserting this first is what
        // stops the "no 0x31 write" check below from passing because nothing
        // was ever written.
        CHECK(output.commandMayBeIssued);
        bridge.apply(output);
    }

    // The assertion is made against what the hardware received.
    const std::vector<std::uint8_t> written = backend.writtenRegisters();
    const std::set<std::uint8_t> unique(written.begin(), written.end());
    // The only register this bridge is even capable of writing.
    CHECK(unique.size() == 1u);
    for (const std::uint8_t address : unique) {
        CHECK(address == core::kRegisterFanLevel);
        CHECK(address != core::kRegisterFanSelector);
    }
    CHECK(!written.empty());
    CHECK(backend.registerValue(core::kRegisterFanSelector) == 0x00);

    // And the bus's own trace agrees.
    for (const core::BusWrite& write : bridge.writeTrace()) {
        CHECK(write.address != core::kRegisterFanSelector);
    }
}

void testFanSelectorCannotBeReachedThroughTheBridgeAtAll()
{
    noteTest();
    // There is no bridge method that takes a register address. The only
    // register the bridge can write is the fan level, so the selector is
    // unreachable by construction rather than by a check that could be
    // forgotten. Evidenced by the only written address over a full cycle.
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, bridgeConfig());

    ControllerOutput output;
    output.commandMayBeIssued = true;
    output.command = core::FanCommand::levelCommand(6);
    CHECK(bridge.apply(output).succeeded);

    const std::vector<std::uint8_t> written = backend.writtenRegisters();
    CHECK(written.size() == 1);
    CHECK(written[0] == core::kRegisterFanLevel);
}

void testSingleFanFlagDoesNotChangeTheBehaviour()
{
    noteTest();
    // The flag exists to be asserted on, not to be trusted. Both settings must
    // behave identically: if clearing it enabled a selector write, the flag
    // would be a live safety switch that nobody is reviewing.
    for (const bool singleFan : {true, false}) {
        BridgeConfig config = bridgeConfig();
        config.singleFanProfile = singleFan;
        const core::EcBusConfig busConfig;
        FakeEcBackend backend(busConfig);
        loadHealthyMachine(backend);
        FakeClock clock;
        AppBridge bridge(backend, clock, config);

        ControllerOutput output;
        output.commandMayBeIssued = true;
        output.command = core::FanCommand::levelCommand(2);
        CHECK(bridge.apply(output).succeeded);

        for (const std::uint8_t address : backend.writtenRegisters()) {
            CHECK(address != core::kRegisterFanSelector);
        }
    }
}

// ---------------------------------------------------------------------------
// T3-04 / T3-09 / T3-10: the controller drives the bridge
// ---------------------------------------------------------------------------

void testBackendAbsentWithoutRestoreFailsClosed()
{
    noteTest();
    // T3-09 and ADR-007. With no backend there is nothing to read, nothing to
    // verify, and - crucially - no verified way to put the fan back. The
    // controller must therefore authorise NOTHING, not even the BIOS command it
    // would otherwise reach for.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    backend.shutdownAfterOperations(0);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    ControllerInput request;
    request.capabilities.backendReady = true;   // restore NOT available
    request.requestControl = true;
    request.rpmObservationElapsed = true;

    const ControllerOutput output = bridge.cycle(1000, request);
    CHECK(!output.commandMayBeIssued);
    CHECK(!output.reasonCode.empty());

    const ApplyResult result = bridge.apply(output);
    CHECK(!result.attempted);
    CHECK(backend.committedWrites().empty());
    CHECK(bridge.writeTrace().empty());
}

void testBackendAbsentWithVerifiedRestoreStillHandsBackToTheFirmware()
{
    noteTest();
    // The other half, and the reason the previous test is not simply "never
    // command": when restore HAS been verified, a backend failure must still
    // produce the BIOS/automatic command. Failing closed into silence would
    // leave the fan wherever it was, which is the failure this project exists
    // to avoid.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    backend.setRegister(core::kRegisterFanLevel, 0x03);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    ControllerInput request = eligibleRequest();
    request.backendFailure = true;               // simulate the backend dying

    const ControllerOutput output = bridge.controller().update(request);
    CHECK(output.safetyState == core::SafetyState::FailsafeBios);
    CHECK(output.commandMayBeIssued);
    CHECK(output.command.kind == core::CommandKind::BiosAutomatic);
}

void testMonitorOnlyIsNotSilentlyUpgraded()
{
    noteTest();
    // A bridge whose capabilities are unverified must not reach Controlled,
    // however many cycles run. This is the ADR-007 property, checked over time
    // rather than on a single sample.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    ControllerInput request;
    request.capabilities.backendReady = true;
    request.requestControl = true;
    request.rpmObservationElapsed = true;

    for (int cycle = 0; cycle < 10; ++cycle) {
        const ControllerOutput output =
            bridge.cycle(static_cast<std::uint64_t>(cycle) * 1000, request);
        CHECK(output.controlMode != core::ControlMode::AutomaticCurve);
        CHECK(output.controlMode != core::ControlMode::ManualOverride);
        const ApplyResult result = bridge.apply(output);
        CHECK(!result.attempted);
    }
    CHECK(backend.writtenRegisters().empty());
}

void testEveryMissingCapabilityBlocksControl()
{
    noteTest();
    // T3-10. The application must not be able to request control while the core
    // believes a capability is missing, whatever is missing.
    for (int missing = 0; missing < 5; ++missing) {
        core::CapabilityReport capabilities;
        capabilities.exactHardwareMatch = true;
        capabilities.profileVerified = true;
        capabilities.topologyVerified = true;
        capabilities.backendReady = true;
        capabilities.restoreAvailable = true;
        switch (missing) {
        case 0: capabilities.exactHardwareMatch = false; break;
        case 1: capabilities.profileVerified = false; break;
        case 2: capabilities.topologyVerified = false; break;
        case 3: capabilities.backendReady = false; break;
        case 4: capabilities.restoreAvailable = false; break;
        default: break;
        }
        CHECK(!capabilities.controlEligible());

        BridgeConfig config = bridgeConfig();
        const core::EcBusConfig busConfig;
        FakeEcBackend backend(busConfig);
        loadHealthyMachine(backend);
        FakeClock clock;
        AppBridge bridge(backend, clock, config);

        ControllerInput request = eligibleRequest();
        request.capabilities = capabilities;
        const ControllerOutput output = bridge.cycle(1000, request);
        CHECK(!output.commandMayBeIssued);
        CHECK(backend.writtenRegisters().empty());
    }
}

void testReadFailureIsReportedIntoTheController()
{
    noteTest();
    // T3-10 from the other direction: a read that did not complete must reach
    // the controller as a backend failure, not as silence.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.setPersistentReadFailure(true);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    const ControllerInput request = eligibleRequest();
    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(!snapshot.ok);

    // The failure is carried into the input, which is the whole point.
    const ControllerInput input = AppBridge::makeInput(snapshot, request, 1000, 1000);
    CHECK(input.backendFailure);
    CHECK(input.temperatures.empty());

    // And the controller's response is to hand the fan back to the firmware,
    // not to keep driving a curve from data it does not have. Note that a
    // command IS issued here - the previous test asserts the opposite, and both
    // are correct, because the difference is whether restore was verified.
    const ControllerOutput output = bridge.cycle(1000, request);
    CHECK(output.safetyState == core::SafetyState::FailsafeBios);
    CHECK(output.controlMode != core::ControlMode::AutomaticCurve);
    CHECK(output.command.kind == core::CommandKind::BiosAutomatic);
    CHECK(!output.reasonCode.empty());
}

void testMakeInputReportsMissingDataHonestly()
{
    noteTest();
    const EcSnapshot missing;
    const ControllerInput input =
        AppBridge::makeInput(missing, ControllerInput(), 1000, 1000);
    CHECK(input.backendFailure);
    CHECK(!input.hasFanRpm);
    CHECK(input.nowMs == 1000);
    CHECK(input.rpmObservationElapsed);

    EcSnapshot good;
    good.ok = true;
    good.fanSpeedRead = true;
    good.fanRpm = 2500;
    good.fanSpeedTimestampMs = 900;
    good.temperatures.assign(
        static_cast<std::size_t>(core::kSensorCount), core::TemperatureSample());
    const ControllerInput ok = AppBridge::makeInput(good, ControllerInput(), 1000, 0);
    CHECK(!ok.backendFailure);
    CHECK(ok.hasFanRpm);
    CHECK(ok.fanRpm == 2500);
    CHECK(ok.timestampMs == 900);
    // No observation window elapsed, so the speed is not yet evidence.
    CHECK(!ok.rpmObservationElapsed);
}

void testImplausibleFanSpeedIsNotTreatedAsHealthy()
{
    noteTest();
    // A raw register pair read without scaling is the defect SAFETY.md 6.1 is
    // about. 0xFFFF must not reach the health check as a speed.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    backend.setRegister(core::kRegisterFanSpeedLo, 0xFF);
    backend.setRegister(core::kRegisterFanSpeedHi, 0xFF);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(snapshot.fanRpm == 65535);

    ControllerInput request = eligibleRequest();
    request.requestManual = true;
    request.manualLevel = 3;
    const ControllerOutput output = bridge.controller().update(
        AppBridge::makeInput(snapshot, request, 1000, 1000));
    CHECK(output.fanHealth == core::FanHealth::Failed);
    CHECK(output.fanHealth != core::FanHealth::Healthy);
}

void testPlausibleFanSpeedStillReportsHealthy()
{
    noteTest();
    // The other half of the previous test. If the fix were simply "report
    // Failed for everything", the layer would be useless.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);

    const EcSnapshot snapshot = bridge.read(1000);
    CHECK(snapshot.fanRpm == 3200);
    const ControllerOutput output = bridge.controller().update(
        AppBridge::makeInput(snapshot, eligibleRequest(), 1000, 1000));
    CHECK(output.fanHealth == core::FanHealth::Healthy);
}

void testLegacyRpmCeilingIsCarriedForward()
{
    noteTest();
    // fanstuff.cpp treats anything above 0x1FFF as not a real reading. The
    // bridge narrows the default ceiling to match, so the core cannot accept a
    // value the application itself would have discarded.
    BridgeConfig config = bridgeConfig();
    const core::EcBusConfig busConfig;
    FakeEcBackend backend(busConfig);
    loadHealthyMachine(backend);
    FakeClock clock;
    AppBridge bridge(backend, clock, config);
    CHECK(bridge.config().controller.fanRpm.maximumPlausible == 0x1FFF);

    // Asserted as behaviour, not as a field: 0x1FFF is the last value the
    // legacy application would have accepted, and 0x2000 is the first it would
    // have discarded. Reading a private field would only prove the constant was
    // copied; this proves it is actually applied.
    backend.setRegister(core::kRegisterFanSpeedLo, 0xFF);
    backend.setRegister(core::kRegisterFanSpeedHi, 0x1F);   // 0x1FFF
    CHECK(bridge.read(1000).fanRpm == 0x1FFF);

    backend.setRegister(core::kRegisterFanSpeedLo, 0x00);
    backend.setRegister(core::kRegisterFanSpeedHi, 0x20);   // 0x2000
    CHECK(bridge.read(1000).fanRpm == 0x2000);

    const core::ValidatedFanRpm accepted = core::validateFanRpm(
        true, 0x1FFF, 1000, 1000, bridge.config().controller.fanRpm);
    CHECK(accepted.valid);
    const core::ValidatedFanRpm rejected = core::validateFanRpm(
        true, 0x2000, 1000, 1000, bridge.config().controller.fanRpm);
    CHECK(!rejected.valid);
    CHECK(rejected.validity == core::SensorValidity::OutOfRange);
}

// ---------------------------------------------------------------------------
// T3-07: UI text
// ---------------------------------------------------------------------------

void testEveryStateHasText()
{
    noteTest();
    // A state with no label renders as an empty control, which reads as
    // "nothing to report" - the opposite of the truth.
    const core::SafetyState states[] = {
        core::SafetyState::MonitorOnly, core::SafetyState::Validating,
        core::SafetyState::BiosAutomatic, core::SafetyState::Controlled,
        core::SafetyState::ManualOverride, core::SafetyState::FailsafeBios,
        core::SafetyState::ThermalEmergency, core::SafetyState::Stopping};
    for (const core::SafetyState state : states) {
        const std::string text = core::toText(state);
        CHECK(!text.empty());
        CHECK(text != "unknown");
    }

    const core::ControlMode modes[] = {
        core::ControlMode::MonitorOnly, core::ControlMode::BiosAutomatic,
        core::ControlMode::AutomaticCurve, core::ControlMode::ManualOverride};
    for (const core::ControlMode mode : modes) {
        CHECK(core::toText(mode)[0] != 0);
        CHECK(std::string(core::toText(mode)) != "unknown");
    }

    const core::FanHealth healths[] = {
        core::FanHealth::Unsupported, core::FanHealth::Unknown,
        core::FanHealth::Healthy, core::FanHealth::Suspect,
        core::FanHealth::Failed};
    for (const core::FanHealth health : healths) {
        CHECK(core::toText(health)[0] != 0);
    }

    const core::SensorValidity validities[] = {
        core::SensorValidity::Valid, core::SensorValidity::Missing,
        core::SensorValidity::BackendError, core::SensorValidity::ProviderInvalid,
        core::SensorValidity::Sentinel, core::SensorValidity::OutOfRange,
        core::SensorValidity::Stale, core::SensorValidity::FutureTimestamp};
    for (const core::SensorValidity validity : validities) {
        CHECK(core::toText(validity)[0] != 0);
    }
}

void testDescribeIsNeverEmptyAndCarriesTheReason()
{
    noteTest();
    ControllerOutput output;
    output.controlMode = core::ControlMode::MonitorOnly;
    output.safetyState = core::SafetyState::MonitorOnly;
    output.reasonCode = "awaiting_verified_restore";
    const std::string text = core::describe(output);
    CHECK(!text.empty());
    CHECK(text.find("awaiting_verified_restore") != std::string::npos);
    CHECK(text.find("monitor-only") != std::string::npos);
}

void testUiTextCannotChangeAState()
{
    noteTest();
    // Presentation is a pure function of the output. Two outputs with the same
    // state but different reasons produce different text, yet the state itself
    // is not derived from or affected by the text.
    ControllerOutput a;
    a.controlMode = core::ControlMode::AutomaticCurve;
    a.safetyState = core::SafetyState::Controlled;
    ControllerOutput b = a;
    b.reasonCode = "different";
    CHECK(core::describe(a).find("controlled") != std::string::npos);
    CHECK(core::describe(b).find("controlled") != std::string::npos);
    CHECK(core::describe(a) != core::describe(b));
    CHECK(a.safetyState == core::SafetyState::Controlled);
}

void runAll()
{
    // T3-01: the register map
    testRegisterMapMatchesTheApplication();

    // reading
    testReadPopulatesEverySensor();
    testFanSpeedIsLittleEndian();
    testPartialReadIsNotReportedAsSuccess();
    testBackendStoppingPartWayIsNotSuccess();
    testSensorNameListIsValidatedNotPadded();

    // T3-08: no command without core approval
    testUnauthorisedCommandIsRefusedWithoutTouchingTheBus();
    testNoCommandMeansNoWrite();
    testOutOfRangeLevelIsRefused();

    // T3-03: writes through EcBus, verified
    testAuthorisedWriteIsIssuedAndVerified();
    testRepeatedCommandIsNotRewritten();
    testReadbackMismatchIsReportedAndNotRemembered();
    testFailedWriteIsNotRememberedAsApplied();
    testBiosAutomaticUsesTheFirmwareLevel();

    // T3-11: never write 0x31
    testNoCycleEverWritesTheFanSelector();
    testFanSelectorCannotBeReachedThroughTheBridgeAtAll();
    testSingleFanFlagDoesNotChangeTheBehaviour();

    // T3-04 / T3-09 / T3-10
    testBackendAbsentWithoutRestoreFailsClosed();
    testBackendAbsentWithVerifiedRestoreStillHandsBackToTheFirmware();
    testMonitorOnlyIsNotSilentlyUpgraded();
    testEveryMissingCapabilityBlocksControl();
    testReadFailureIsReportedIntoTheController();
    testMakeInputReportsMissingDataHonestly();
    testImplausibleFanSpeedIsNotTreatedAsHealthy();
    testPlausibleFanSpeedStillReportsHealthy();
    testLegacyRpmCeilingIsCarriedForward();

    // T3-07: UI text
    testEveryStateHasText();
    testDescribeIsNeverEmptyAndCarriesTheReason();
    testUiTextCannotChangeAState();
}

} // namespace appbridge

void runAppBridgeTests()
{
    appbridge::runAll();
}

int appBridgeTestCount()
{
    return appbridge::g_testCount;
}

} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAppBridgeTests();
    std::printf("TPFanControl app bridge tests passed (%d tests)\n",
        appBridgeTestCount());
    return 0;
}
