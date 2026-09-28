// Tests for fancontrol/core/legacy_backend.{h,cpp}, covering T3-02 and part of
// T3-06.
//
// The privileged part of the legacy EC path cannot run here - it needs an EC, a
// port driver and a ThinkPad. What CAN be tested is the adapter around it, and
// the adapter is where the safety decisions live: whether a write is permitted
// at all, what is reported when it is not, and what happens when the driver
// goes away underneath a call.
//
// Every assertion uses CHECK, not assert. See tests/test_check.h.

#include "test_check.h"

#include "../fancontrol/core/legacy_backend.h"

#include <cstdio>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::BackendState;
using core::IoErrorCode;
using core::LegacyBackend;
using core::LegacyEcPrimitives;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

// A record of one call, so a test can assert on what reached the primitive
// rather than only on what the adapter returned.
struct Call {
    bool isWrite = false;
    int offset = 0;
    std::uint8_t value = 0;
};

struct Harness {
    std::vector<Call> calls;
    bool readResult = true;
    bool writeResult = true;
    bool haveWrite = true;
    bool haveRead = true;

    LegacyBackend build()
    {
        // Built as named std::functions and then copied into the primitives
        // struct. Building them inline and then reusing one after a
        // std::move would silently use a moved-from empty function, which
        // produced a backend with no read primitive and a test that failed for
        // the wrong reason.
        std::function<bool(int, std::uint8_t&)> read =
            [this](int offset, std::uint8_t& out) {
                if (!haveRead) {
                    return false;
                }
                calls.push_back(Call{false, offset, 0});
                out = static_cast<std::uint8_t>(offset ^ 0x5A);
                return readResult;
            };
        std::function<bool(int, std::uint8_t)> write =
            [this](int offset, std::uint8_t value) {
                calls.push_back(Call{true, offset, value});
                return writeResult;
            };

        LegacyEcPrimitives primitives;
        primitives.readByte = read;
        if (haveWrite) {
            primitives.writeByte = write;
        }
        LegacyBackend backend(std::move(primitives));
        backend.setDriverOpen(true);
        return backend;
    }
};

void testReadReachesTheLegacyPrimitive()
{
    noteTest();
    Harness h;
    LegacyBackend backend = h.build();

    const core::IoResult result = backend.readPort(0x60);
    CHECK(result.ok);
    CHECK(result.error == IoErrorCode::None);
    CHECK(backend.readCount() == 1);
    CHECK(h.calls.size() == 1);
    CHECK(!h.calls[0].isWrite);
    CHECK(h.calls[0].offset == 0x60);
    // The value the primitive produced came back out unchanged.
    CHECK(result.value == static_cast<std::uint8_t>(0x60 ^ 0x5A));
}

void testFailedReadIsTypedNotSilent()
{
    noteTest();
    // The legacy primitive returns a bool. Letting a false travel further as a
    // bool is how a failed read becomes a zero, and a zero is a plausible
    // temperature. It becomes a typed error here, at the boundary.
    Harness h;
    h.readResult = false;
    LegacyBackend backend = h.build();

    const core::IoResult result = backend.readPort(0x60);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::ReadFailure);
    CHECK(!result.message.empty());
}

void testReadFailsWhileTheDriverIsClosed()
{
    noteTest();
    // The application opens and closes the port driver over its lifetime. A
    // backend that outlives it must say so, not call through a closed handle.
    Harness h;
    LegacyBackend backend = h.build();
    CHECK(backend.state() == BackendState::Ready);

    backend.close();
    CHECK(backend.state() == BackendState::Unavailable);

    const core::IoResult result = backend.readPort(0x60);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::NotInitialized);
    // The primitive was never reached.
    CHECK(h.calls.empty());
}

void testReadOnlyBackendRefusesEveryWrite()
{
    noteTest();
    // The state a machine is in before Phase 0. This is the single most
    // important assertion in the file: a write-capable backend on an
    // unverified machine is how the 0x31 defect happened in the first place.
    Harness h;
    LegacyBackend backend = h.build();
    backend.makeReadOnly();
    CHECK(backend.readOnly());

    const core::IoResult result = backend.writePort(0x62, 0x2F);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(backend.writeCount() == 0);
    CHECK(backend.refusedWriteCount() == 1);
    // Nothing reached the primitive.
    for (const Call& call : h.calls) {
        CHECK(!call.isWrite);
    }

    // And reads still work, because monitor-only operation has to work.
    CHECK(backend.readPort(0x60).ok);
    CHECK(backend.state() == BackendState::Ready);
}

void testReadOnlyIsNotReversible()
{
    noteTest();
    // There is deliberately no makeWritable(). A class whose safety depends on
    // nobody calling a method is a weaker guarantee than one that cannot be
    // undone. This asserts the state cannot be walked back through the public
    // surface, which is what the absence of a setter means.
    Harness h;
    LegacyBackend backend = h.build();
    backend.makeReadOnly();
    backend.makeReadOnly();
    CHECK(backend.readOnly());
    CHECK(!backend.writePort(0x62, 0x00).ok);
    CHECK(backend.writeCount() == 0);
}

void testReadOnlyReportsUnsupportedRatherThanNotInitialised()
{
    noteTest();
    // On a read-only backend with the driver also closed, the reason a write
    // fails is the policy, not the driver. Reporting "not initialised" would
    // send a user looking for a driver problem that does not exist.
    Harness h;
    LegacyBackend backend = h.build();
    backend.makeReadOnly();
    backend.close();

    const core::IoResult result = backend.writePort(0x62, 0x00);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
}

void testWriteFailsWhenTheDriverIsClosed()
{
    noteTest();
    Harness h;
    LegacyBackend backend = h.build();
    backend.close();

    const core::IoResult result = backend.writePort(0x62, 0x2F);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::NotInitialized);
    CHECK(backend.writeCount() == 0);
}

void testFailedWriteIsTyped()
{
    noteTest();
    Harness h;
    h.writeResult = false;
    LegacyBackend backend = h.build();

    const core::IoResult result = backend.writePort(0x62, 0x2F);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::WriteFailure);
    CHECK(backend.writeCount() == 1);
    // The call was attempted, so the count reflects an attempt, not a success.
    CHECK(h.calls.size() == 1);
    CHECK(h.calls[0].isWrite);
}

void testWriteReachesTheLegacyPrimitive()
{
    noteTest();
    Harness h;
    LegacyBackend backend = h.build();

    const core::IoResult result = backend.writePort(0x62, 0x2F);
    CHECK(result.ok);
    CHECK(backend.writeCount() == 1);
    CHECK(h.calls.size() == 1);
    CHECK(h.calls[0].isWrite);
    CHECK(h.calls[0].offset == 0x62);
    CHECK(h.calls[0].value == 0x2F);
}

void testBackendWithoutAWritePrimitiveRefusesWrites()
{
    noteTest();
    // Constructed with no write primitive at all. capabilities() must not claim
    // it can write, and a write must not reach anything.
    Harness h;
    h.haveWrite = false;
    LegacyBackend backend = h.build();

    CHECK(!backend.capabilities().canWritePorts);
    const core::IoResult result = backend.writePort(0x62, 0x2F);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
}

void testBackendWithoutAReadPrimitiveIsNotReady()
{
    noteTest();
    // Reporting Ready with no way to read would let the controller believe it
    // has a backend and discover otherwise on the first cycle, with a fault
    // latch the user then has to clear.
    LegacyEcPrimitives primitives;
    primitives.writeByte = [](int, std::uint8_t) { return true; };
    LegacyBackend backend(std::move(primitives));
    backend.setDriverOpen(true);

    CHECK(backend.state() == BackendState::Unavailable);
    CHECK(!backend.readPort(0x60).ok);
}

void testCapabilitiesDoNotClaimSignedApproval()
{
    noteTest();
    // The core must not be able to discover that control is permitted by asking
    // the backend. signedAndApproved stays false; eligibility is Phase 0's
    // verdict, carried in the capability report, not a driver property.
    Harness h;
    LegacyBackend backend = h.build();
    CHECK(!backend.capabilities().signedAndApproved);
    CHECK(!backend.capabilities().canReadMsr);
    CHECK(backend.capabilities().canReadPorts);
    CHECK(backend.capabilities().canWritePorts);
    CHECK(!backend.capabilities().name.empty());
    CHECK(!backend.capabilities().version.empty());

    // And read-only flips the write claim.
    backend.makeReadOnly();
    CHECK(!backend.capabilities().canWritePorts);
}

void testCountsAreIndependent()
{
    noteTest();
    // A count that conflates attempts with successes, or reads with writes,
    // makes "this cycle wrote nothing" unprovable.
    Harness h;
    LegacyBackend backend = h.build();

    backend.readPort(0x60);
    backend.readPort(0x62);
    backend.writePort(0x62, 0x2F);
    backend.makeReadOnly();
    backend.writePort(0x62, 0x2F);

    CHECK(backend.readCount() == 2);
    CHECK(backend.writeCount() == 1);
    CHECK(backend.refusedWriteCount() == 1);
}

void runAll()
{
    testReadReachesTheLegacyPrimitive();
    testFailedReadIsTypedNotSilent();
    testReadFailsWhileTheDriverIsClosed();
    testReadOnlyBackendRefusesEveryWrite();
    testReadOnlyIsNotReversible();
    testReadOnlyReportsUnsupportedRatherThanNotInitialised();
    testWriteFailsWhenTheDriverIsClosed();
    testFailedWriteIsTyped();
    testWriteReachesTheLegacyPrimitive();
    testBackendWithoutAWritePrimitiveRefusesWrites();
    testBackendWithoutAReadPrimitiveIsNotReady();
    testCapabilitiesDoNotClaimSignedApproval();
    testCountsAreIndependent();
}

} // namespace
} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAll();
    std::printf("TPFanControl legacy backend tests passed (%d tests)\n", testCount);
    return 0;
}
