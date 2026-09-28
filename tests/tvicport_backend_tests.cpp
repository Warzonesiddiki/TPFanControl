// Tests for fancontrol/core/tvicport_backend.{h,cpp} - the TVicPort baseline
// adapter (T5-01).
//
// The adapter is mostly lifecycle: open the driver, decide about hard access,
// close what it opened. The port policy itself (ports arrive unchanged, a
// failure is typed, every call is traceable) is LegacyBackend's and is tested
// in tests/legacy_backend_tests.cpp; what is tested here is that this class adds
// only TVicPort's own behaviour and does not quietly add policy of its own.
//
// The fake below is a fake *DLL*, not a fake EC: it counts calls to the seven
// entry points and can refuse to open, refuse hard access, or be closed by
// someone else. Nothing here models the EC protocol, because nothing at this
// layer knows what an EC register is.
//
// Every assertion uses CHECK, not assert. See tests/test_check.h.

#include "test_check.h"

#include "../fancontrol/core/tvicport_backend.h"

#include <cstdio>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::BackendState;
using core::HardAccessReport;
using core::IoErrorCode;
using core::IoResult;
using core::PortCall;
using core::TvicPortApi;
using core::TvicPortAttachResult;
using core::TvicPortBackend;
using core::TvicPortOptions;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

// A fake TVicPort DLL.
//
// `openSucceeds` and `hardAccessAvailable` are the two things a real machine can
// say no to. `driverOpen` models the driver's own state, so a test can close it
// behind the adapter's back the way approot.cpp's CloseTVicPort() does.
class FakeTvicPort {
public:
    explicit FakeTvicPort(bool openSucceeds = true, bool hardAccessAvailable = true)
        : openSucceeds_(openSucceeds), hardAccessAvailable_(hardAccessAvailable)
    {
    }

    bool openSucceeds_ = true;
    bool hardAccessAvailable_ = true;
    bool driverOpen = false;
    bool haveRead = true;
    bool haveWrite = true;
    bool haveIsDriverOpen = true;
    bool haveTestHardAccess = true;
    bool haveSetHardAccess = true;

    int openCalls = 0;
    int closeCalls = 0;
    int testHardAccessCalls = 0;
    int setHardAccessCalls = 0;
    std::vector<bool> setHardAccessValues;
    std::vector<PortCall> portCalls;
    std::uint8_t nextReadValue = 0x5A;
    std::string version = "4.0-vendor-header";

    TvicPortApi api()
    {
        TvicPortApi out;

        out.openDriver = [this]() -> bool {
            ++openCalls;
            if (!openSucceeds_) {
                return false;
            }
            driverOpen = true;
            return true;
        };

        out.closeDriver = [this]() {
            ++closeCalls;
            driverOpen = false;
        };

        if (haveIsDriverOpen) {
            out.isDriverOpen = [this]() { return driverOpen; };
        }

        if (haveTestHardAccess) {
            out.testHardAccess = [this]() {
                ++testHardAccessCalls;
                return hardAccessAvailable_;
            };
        }

        if (haveSetHardAccess) {
            out.setHardAccess = [this](bool value) {
                ++setHardAccessCalls;
                setHardAccessValues.push_back(value);
                // The real driver's switch changes what its own test reports.
                hardAccessAvailable_ = value;
            };
        }

        if (haveRead) {
            out.readPort = [this](std::uint16_t port) -> std::uint8_t {
                PortCall call;
                call.isWrite = false;
                call.port = port;
                call.value = nextReadValue;
                call.ok = true;
                portCalls.push_back(call);
                return nextReadValue;
            };
        }

        if (haveWrite) {
            out.writePort = [this](std::uint16_t port, std::uint8_t value) {
                PortCall call;
                call.isWrite = true;
                call.port = port;
                call.value = value;
                call.ok = true;
                portCalls.push_back(call);
            };
        }

        out.version = version;
        return out;
    }

    // The application's own OpenTVicPort(), before this adapter exists.
    void openAsTheApplication()
    {
        openSucceeds_ = true;
        ++openCalls;
        driverOpen = true;
    }

    int totalApiCalls() const
    {
        return openCalls + closeCalls + testHardAccessCalls + setHardAccessCalls +
               static_cast<int>(portCalls.size());
    }
};

// ---------------------------------------------------------------------------
// Construction and attachment
// ---------------------------------------------------------------------------

void testConstructionCallsNothing()
{
    noteTest();
    // A backend that opens a driver, or changes a driver setting, in its
    // constructor is one whose side effects appear wherever the object is
    // created - including in a test, a report tool, or a code path that was
    // only meant to ask what the backend can do.
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());

    CHECK(fake.totalApiCalls() == 0);
    CHECK(!backend.attached());
    CHECK(backend.state() == BackendState::Unavailable);
    CHECK(!backend.capabilities().name.empty());
    CHECK(fake.totalApiCalls() == 0);
}

void testAttachOpensTheDriverOnceAndIsIdempotent()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());

    const TvicPortAttachResult first = backend.attach();
    CHECK(first.ok);
    CHECK(fake.openCalls == 1);
    CHECK(backend.attached());
    CHECK(backend.state() == BackendState::Ready);
    CHECK(backend.hardAccess().openedByUs);

    const TvicPortAttachResult second = backend.attach();
    CHECK(second.ok);
    CHECK(fake.openCalls == 1);  // not opened again
    CHECK(backend.attached());
}

void testAttachFailureIsTypedAndLeavesNothingOpen()
{
    noteTest();
    // OpenTVicPort() fails on a machine with no driver. The backend must report
    // that and stop: no retry loop, no attempt to load anything, and no port
    // call it cannot honour.
    FakeTvicPort fake(/*openSucceeds=*/false);
    TvicPortBackend backend(fake.api());

    const TvicPortAttachResult result = backend.attach();
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::NotInitialized);
    CHECK(result.message.find("OpenTVicPort") != std::string::npos);
    CHECK(fake.openCalls == 1);
    CHECK(!backend.attached());
    CHECK(backend.state() == BackendState::Unavailable);

    const IoResult read = backend.readPort(0x1604);
    CHECK(!read.ok);
    CHECK(read.error == IoErrorCode::NotInitialized);
    CHECK(fake.portCalls.empty());
}

void testBackendWithoutAReadFunctionIsRefused()
{
    noteTest();
    // The vendor API's whole purpose here is reading a port. A backend that
    // cannot do it must say so at attach time, not report Ready and fail on the
    // first cycle with a fault the user then has to clear.
    FakeTvicPort fake;
    fake.haveRead = false;
    TvicPortBackend backend(fake.api());

    const TvicPortAttachResult result = backend.attach();
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(fake.openCalls == 0);  // nothing was opened for a backend that cannot read
    CHECK(backend.state() == BackendState::Unavailable);
    CHECK(!backend.capabilities().canReadPorts);
}

void testAnAlreadyOpenDriverIsNotOpenedAgain()
{
    noteTest();
    // The application opens the driver before the backend is created. Reopening
    // is unnecessary, and detaching afterwards must not close a handle the
    // application still owns.
    FakeTvicPort fake;
    fake.openAsTheApplication();
    TvicPortBackend backend(fake.api());

    const TvicPortAttachResult result = backend.attach();
    CHECK(result.ok);
    CHECK(fake.openCalls == 1);  // only the application's call
    CHECK(!backend.hardAccess().openedByUs);
    CHECK(backend.attached());

    backend.detach();
    CHECK(fake.closeCalls == 0);
    CHECK(fake.driverOpen);
}

// ---------------------------------------------------------------------------
// Hard access
// ---------------------------------------------------------------------------

void testHardAccessIsNeverRequestedUnlessAsked()
{
    noteTest();
    // The defect this test exists to prevent: the legacy application calls
    // SetHardAccess(true) unconditionally, and an adapter that copies that
    // silently changes a driver setting on every machine it runs on.
    FakeTvicPort fake(/*openSucceeds=*/true, /*hardAccessAvailable=*/false);
    TvicPortBackend backend(fake.api());

    const TvicPortAttachResult result = backend.attach();
    CHECK(result.ok);  // reads may still work; the state is reported, not hidden
    CHECK(fake.setHardAccessCalls == 0);
    CHECK(fake.testHardAccessCalls == 1);
    CHECK(!backend.hardAccess().reportedByDriver);
    CHECK(!backend.hardAccess().requestedByUs);
    CHECK(!backend.hardAccess().changed);
    CHECK(backend.hardAccess().message.find("NOT available") != std::string::npos);
}

void testHardAccessIsRequestedOnlyWhenTheCallerAsksAndIsRecorded()
{
    noteTest();
    TvicPortOptions options;
    options.requestHardAccess = true;

    FakeTvicPort fake(/*openSucceeds=*/true, /*hardAccessAvailable=*/false);
    TvicPortBackend backend(fake.api(), options);

    const TvicPortAttachResult result = backend.attach();
    CHECK(result.ok);
    CHECK(fake.setHardAccessCalls == 1);
    CHECK(fake.setHardAccessValues.size() == 1);
    CHECK(fake.setHardAccessValues[0]);  // true, and never false
    CHECK(fake.testHardAccessCalls == 2);  // tested before and after the change
    CHECK(backend.hardAccess().requestedByUs);
    CHECK(backend.hardAccess().changed);
    CHECK(backend.hardAccess().reportedByDriver);
}

void testAvailableHardAccessIsLeftAloneEvenWhenRequested()
{
    noteTest();
    // Asking for hard access is not the same as asserting it is needed. Where
    // the driver already reports it available, no setting is touched.
    TvicPortOptions options;
    options.requestHardAccess = true;

    FakeTvicPort fake(/*openSucceeds=*/true, /*hardAccessAvailable=*/true);
    TvicPortBackend backend(fake.api(), options);

    CHECK(backend.attach().ok);
    CHECK(fake.setHardAccessCalls == 0);
    CHECK(backend.hardAccess().reportedByDriver);
    CHECK(!backend.hardAccess().requestedByUs);
    CHECK(!backend.hardAccess().changed);
}

void testMissingHardAccessFunctionsAreReportedNotGuessed()
{
    noteTest();
    TvicPortOptions options;
    options.requestHardAccess = true;

    FakeTvicPort fake;
    fake.haveTestHardAccess = false;
    fake.haveSetHardAccess = false;
    TvicPortBackend backend(fake.api(), options);

    CHECK(backend.attach().ok);
    CHECK(fake.setHardAccessCalls == 0);
    CHECK(!backend.hardAccess().changed);
    CHECK(backend.hardAccess().message.find("unknown") != std::string::npos);
}

// ---------------------------------------------------------------------------
// The driver closing under the adapter
// ---------------------------------------------------------------------------

void testADriverClosedBehindTheAdapterStopsEveryPortCall()
{
    noteTest();
    // approot.cpp closes the driver at the end of a session. A backend that
    // keeps calling ReadPort afterwards is calling into a handle that is gone.
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);
    CHECK(backend.readPort(0x1604).ok);

    fake.driverOpen = false;  // someone else's CloseTVicPort()

    const IoResult read = backend.readPort(0x1604);
    CHECK(!read.ok);
    CHECK(read.error == IoErrorCode::NotInitialized);
    CHECK(backend.state() == BackendState::Unavailable);

    const IoResult write = backend.writePort(0x1600, 0x80);
    CHECK(!write.ok);
    CHECK(write.error == IoErrorCode::NotInitialized);

    // Only the one call made while the driver was open reached the DLL.
    CHECK(fake.portCalls.size() == 1);
}

void testPortCallsAreRefusedBeforeAttachWithoutTouchingTheDll()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());

    CHECK(!backend.readPort(0x1604).ok);
    CHECK(!backend.writePort(0x1600, 0x80).ok);
    CHECK(fake.portCalls.empty());
    CHECK(fake.totalApiCalls() == 0);
}

// ---------------------------------------------------------------------------
// The layering: ports arrive unchanged, registers are not this layer's business
// ---------------------------------------------------------------------------

void testPortsAndValuesReachTheDllUnchanged()
{
    noteTest();
    // 0x1604 is a 16-bit port. If this layer truncated it to 0x04, the adapter
    // would be writing to an entirely different part of the machine and the
    // trace would look correct while doing it.
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);

    CHECK(backend.readPort(0x1604).ok);
    CHECK(fake.portCalls.back().port == 0x1604);
    CHECK(!fake.portCalls.back().isWrite);

    CHECK(backend.writePort(0x1600, 0x80).ok);
    CHECK(fake.portCalls.back().port == 0x1600);
    CHECK(fake.portCalls.back().value == 0x80);
    CHECK(fake.portCalls.back().isWrite);

    // Every byte value survives, including the values that a signed char would
    // turn into something else.
    for (unsigned value = 0; value <= 0xFF; ++value) {
        CHECK(backend.writePort(0x1600, static_cast<std::uint8_t>(value)).ok);
        CHECK(fake.portCalls.back().value == static_cast<std::uint8_t>(value));
    }
}

void testTheAdapterFiltersNoRegisterValue()
{
    noteTest();
    // 0x31 and 0x2F are register-level policy. Those refusals live in EcBus,
    // before any port is touched (ADR-021, ADR-023). This layer must forward the
    // bytes: a backend that filtered them would be a second place where the
    // decision is made, and the two would eventually disagree.
    //
    // The write below is a *port* write with the value 0x31 - it is not a write
    // to register 0x31, which EcBus refuses and which this layer never sees.
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);

    CHECK(backend.writePort(0x1600, 0x31).ok);
    CHECK(fake.portCalls.back().value == 0x31);

    CHECK(backend.writePort(0x1600, 0x2F).ok);
    CHECK(fake.portCalls.back().value == 0x2F);
}

void testTheTraceRecordsEveryCallItMade()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);

    CHECK(backend.readPort(0x1604).ok);
    CHECK(backend.writePort(0x1610, 0x20).ok);
    CHECK(backend.readPort(0x1604).ok);

    const std::vector<PortCall> trace = backend.trace();
    CHECK(trace.size() == 3);
    CHECK(trace.size() == fake.portCalls.size());
    CHECK(trace[0].port == 0x1604 && !trace[0].isWrite);
    CHECK(trace[1].port == 0x1610 && trace[1].isWrite && trace[1].value == 0x20);
    CHECK(trace[2].port == 0x1604 && !trace[2].isWrite);
    CHECK(backend.readCount() == 2);
    CHECK(backend.writeCount() == 1);
    for (const PortCall& call : trace) {
        CHECK(call.ok);
    }
}

// ---------------------------------------------------------------------------
// Capabilities and the attach report
// ---------------------------------------------------------------------------

void testCapabilitiesClaimNothingUnestablished()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());

    const core::BackendCapabilities caps = backend.capabilities();
    CHECK(caps.name == "TVicPort");
    CHECK(caps.version == fake.version);
    CHECK(caps.canReadPorts);
    CHECK(caps.canWritePorts);
    CHECK(!caps.canReadMsr);
    // Whether this driver is acceptable on this machine is a Phase 0 verdict.
    // A capability report that says "signed and approved" would be answering a
    // question it has no way to answer.
    CHECK(!caps.signedAndApproved);

    FakeTvicPort bare;
    bare.haveRead = false;
    bare.haveWrite = false;
    bare.version.clear();
    TvicPortBackend bareBackend(bare.api());
    const core::BackendCapabilities bareCaps = bareBackend.capabilities();
    CHECK(!bareCaps.canReadPorts);
    CHECK(!bareCaps.canWritePorts);
    CHECK(bareCaps.version.find("unknown") != std::string::npos);
}

void testTheAttachReportSaysWhoOpenedTheDriver()
{
    noteTest();
    // A hardware report has to be able to say whether the run opened the driver
    // or found it open, because the two mean different things about the state
    // the machine was in.
    {
        FakeTvicPort fake;
        TvicPortBackend backend(fake.api());
        CHECK(backend.attach().ok);
        const HardAccessReport& report = backend.hardAccess();
        CHECK(report.openedByUs);
        CHECK(report.message.find("opened by this adapter") != std::string::npos);
    }
    {
        FakeTvicPort fake;
        fake.openAsTheApplication();
        TvicPortBackend backend(fake.api());
        CHECK(backend.attach().ok);
        CHECK(!backend.hardAccess().openedByUs);
        CHECK(backend.hardAccess().message.find("already open") != std::string::npos);
    }
}

// ---------------------------------------------------------------------------
// Detaching
// ---------------------------------------------------------------------------

void testDetachClosesWhatItOpened()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);

    CHECK(backend.readPort(0x1604).ok);

    backend.detach();
    CHECK(fake.closeCalls == 1);
    CHECK(!fake.driverOpen);
    CHECK(!backend.attached());
    CHECK(backend.state() == BackendState::Unavailable);

    // The trace is evidence of what happened, not a view of the driver's
    // current state, so detaching does not erase it: a report written after a
    // session ends still has to be able to say what the session did.
    CHECK(backend.trace().size() == 1);
    CHECK(backend.trace()[0].port == 0x1604);
}

void testDetachAndCloseAreIdempotent()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);

    backend.detach();
    backend.detach();
    backend.close();
    CHECK(fake.closeCalls == 1);

    // A second attach after a detach is allowed and opens the driver again.
    CHECK(backend.attach().ok);
    CHECK(fake.openCalls == 2);
    backend.close();
    CHECK(fake.closeCalls == 2);
}

void testNoPortCallIsMadeAfterDetach()
{
    noteTest();
    FakeTvicPort fake;
    TvicPortBackend backend(fake.api());
    CHECK(backend.attach().ok);
    CHECK(backend.readPort(0x1604).ok);
    const std::size_t before = fake.portCalls.size();

    backend.detach();
    CHECK(!backend.readPort(0x1604).ok);
    CHECK(!backend.writePort(0x1600, 0x80).ok);
    CHECK(fake.portCalls.size() == before);
}

void runAll()
{
    testConstructionCallsNothing();
    testAttachOpensTheDriverOnceAndIsIdempotent();
    testAttachFailureIsTypedAndLeavesNothingOpen();
    testBackendWithoutAReadFunctionIsRefused();
    testAnAlreadyOpenDriverIsNotOpenedAgain();
    testHardAccessIsNeverRequestedUnlessAsked();
    testHardAccessIsRequestedOnlyWhenTheCallerAsksAndIsRecorded();
    testAvailableHardAccessIsLeftAloneEvenWhenRequested();
    testMissingHardAccessFunctionsAreReportedNotGuessed();
    testADriverClosedBehindTheAdapterStopsEveryPortCall();
    testPortCallsAreRefusedBeforeAttachWithoutTouchingTheDll();
    testPortsAndValuesReachTheDllUnchanged();
    testTheAdapterFiltersNoRegisterValue();
    testTheTraceRecordsEveryCallItMade();
    testCapabilitiesClaimNothingUnestablished();
    testTheAttachReportSaysWhoOpenedTheDriver();
    testDetachClosesWhatItOpened();
    testDetachAndCloseAreIdempotent();
    testNoPortCallIsMadeAfterDetach();
}

} // namespace
} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAll();
    std::printf("TPFanControl TVicPort backend tests passed (%d tests)\n", testCount);
    return 0;
}
