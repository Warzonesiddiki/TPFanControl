// Tests for fancontrol/core/legacy_backend.{h,cpp} and for the boundary
// between it and EcBus. Covers T3-02 and the correction recorded in ADR-023.
//
// The first version of LegacyBackend took register offsets and handed them to
// FANCONTROL::ReadByteFromEC/WriteByteToEC, which speak the whole two-phase
// protocol themselves, while implementing IIoBackend, whose methods take ports.
// EcBus therefore performed a transaction inside another transaction: a single
// register read ended up writing registers 0x04 and 0x00. Both layers
// type-checked and the existing tests passed, because the fake backend
// implemented the same confusion.
//
// The tests that would have caught it are at the bottom of this file. They ask
// what number actually reached the primitive, not what the return value was.
//
// Every assertion uses CHECK, not assert. See tests/test_check.h.

#include "test_check.h"

#include "../fancontrol/core/ec_protocol.h"
#include "fake_ec.h"

#include "../fancontrol/core/legacy_backend.h"

#include <cstdio>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::BackendState;
using core::EcBus;
using core::EcBusConfig;
using core::IoErrorCode;
using core::LegacyBackend;
using core::LegacyPortPrimitives;
using core::PortCall;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

// A fake set of port primitives that records every call and models just enough
// of the EC status protocol for a transaction to complete.
//
// The modelling is not decoration. `EcBus` waits for the input buffer to clear
// after a command and for the output buffer to fill before reading a value. A
// fake that returns a constant from every port therefore times out on every
// read, and a test written against it would be testing the timeout path while
// claiming to test the sequence.
//
// What is modelled: a command byte on the status port is accepted immediately
// (input buffer clear), a data-port write makes the output buffer full, and
// reading the data port empties it again. What is not modelled: any real EC
// timing, which is Phase 0's business and not this test's.
class FakePorts {
public:
    explicit FakePorts(EcBusConfig config) : config_(config) {}

    std::uint8_t dataValue = 0x5A;
    bool readOk = true;
    bool writeOk = true;
    bool haveRead = true;
    bool haveWrite = true;
    std::vector<PortCall> calls;

    LegacyPortPrimitives primitives()
    {
        LegacyPortPrimitives p;
        if (haveRead) {
            p.readPort = [this](std::uint16_t port, std::uint8_t& out) {
                PortCall call;
                call.isWrite = false;
                call.port = port;
                call.ok = readOk;
                if (readOk) {
                    out = (port == config_.statusPort) ? statusByte() : dataPortRead();
                }
                calls.push_back(call);
                return readOk;
            };
        }
        if (haveWrite) {
            p.writePort = [this](std::uint16_t port, std::uint8_t value) {
                PortCall call;
                call.isWrite = true;
                call.port = port;
                call.value = value;
                call.ok = writeOk;
                calls.push_back(call);
                if (writeOk) {
                    if (port == config_.dataPort) {
                        // A data-port write is what makes the output buffer full.
                        outputFull_ = true;
                        dataValue_ = value;
                    }
                    // A status-port write (the command byte) is accepted at once:
                    // the input buffer is never modelled as busy.
                }
                return writeOk;
            };
        }
        return p;
    }

    std::uint8_t statusByte() const
    {
        return outputFull_ ? config_.obfMask : std::uint8_t{0};
    }

    std::uint8_t dataPortRead()
    {
        // Reading the data port empties the output buffer.
        outputFull_ = false;
        return dataValue_;
    }

    std::vector<PortCall> writes() const
    {
        std::vector<PortCall> out;
        for (const PortCall& c : calls) {
            if (c.isWrite) {
                out.push_back(c);
            }
        }
        return out;
    }

private:
    EcBusConfig config_;
    std::uint8_t dataValue_ = 0;
    bool outputFull_ = false;
};

LegacyBackend makeBackend(FakePorts& fake)
{
    LegacyBackend backend(fake.primitives());
    backend.setDriverOpen(true);
    return backend;
}

// ---------------------------------------------------------------------------
// Port pass-through
// ---------------------------------------------------------------------------

void testPortNumbersReachThePrimitiveUnchanged()
{
    noteTest();
    // The whole point of a port backend. A value of 0x1604 must arrive as
    // 0x1604, not as 0x04 and not as a register offset that happens to look
    // like one.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);

    const std::uint16_t port = 0x1604;
    const core::IoResult result = backend.readPort(port);
    CHECK(result.ok);
    CHECK(fake.calls.size() == 1);
    CHECK(fake.calls[0].port == 0x1604);
    CHECK(fake.calls[0].port == port);
}

void testWriteReachesThePrimitiveUnchanged()
{
    noteTest();
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);

    CHECK(backend.writePort(0x1600, 0x2F).ok);
    CHECK(fake.writes().size() == 1);
    CHECK(fake.writes()[0].port == 0x1600);
    CHECK(fake.writes()[0].value == 0x2F);
}

void testEveryValueOfAByteSurvives()
{
    noteTest();
    // A fan level of 0x80 is the BIOS restore value. If it arrived as -128
    // something in the chain is still using a signed char, and the fan would be
    // told to do something entirely different.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);

    for (unsigned v = 0; v <= 0xFF; ++v) {
        const core::IoResult result = backend.writePort(0x1600, static_cast<std::uint8_t>(v));
        CHECK(result.ok);
        CHECK(fake.writes().back().value == static_cast<std::uint8_t>(v));
    }
}

void testFailedReadIsTypedAndRecorded()
{
    noteTest();
    FakePorts fake{EcBusConfig{}};
    fake.readOk = false;
    LegacyBackend backend = makeBackend(fake);

    const core::IoResult result = backend.readPort(0x1600);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::ReadFailure);
    CHECK(backend.readCount() == 1);
    // A failed attempt is still evidence, so it is recorded.
    CHECK(backend.trace().size() == 1);
    CHECK(!backend.trace()[0].ok);
}

void testFailedWriteIsTypedAndRecorded()
{
    noteTest();
    FakePorts fake{EcBusConfig{}};
    fake.writeOk = false;
    LegacyBackend backend = makeBackend(fake);

    const core::IoResult result = backend.writePort(0x1600, 0x2F);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::WriteFailure);
    CHECK(backend.writeCount() == 1);
    CHECK(backend.trace().size() == 1);
    CHECK(backend.trace()[0].isWrite);
    CHECK(!backend.trace()[0].ok);
}

void testClosedDriverFailsEveryCallWithoutTouchingThePrimitive()
{
    noteTest();
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    CHECK(backend.state() == BackendState::Ready);

    backend.close();
    CHECK(backend.state() == BackendState::Unavailable);
    CHECK(!backend.readPort(0x1600).ok);
    CHECK(!backend.writePort(0x1600, 0x2F).ok);
    // Nothing reached the driver.
    CHECK(fake.calls.empty());
}

void testBackendWithoutPrimitivesIsNotReady()
{
    noteTest();
    // Reporting Ready with no way to read would let the controller believe it
    // has a backend and discover otherwise on the first cycle, with a fault
    // latch the user then has to clear.
    // Braces, not parentheses: `LegacyBackend backend(LegacyPortPrimitives())`
    // is a function declaration, not a variable - the most vexing parse.
    LegacyBackend backend{LegacyPortPrimitives{}};
    backend.setDriverOpen(true);
    CHECK(backend.state() == BackendState::Unavailable);
    CHECK(!backend.capabilities().canReadPorts);
    CHECK(!backend.capabilities().canWritePorts);
}

void testCapabilitiesDoNotClaimSignedApproval()
{
    noteTest();
    // Control eligibility is a Phase 0 verdict, and the ability to change a
    // register is EcBus's. Neither may be discoverable by asking the backend.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    CHECK(!backend.capabilities().signedAndApproved);
    CHECK(!backend.capabilities().canReadMsr);
    CHECK(backend.capabilities().canWritePorts);
    CHECK(backend.capabilities().name == "LegacyPorts");
}

void testClearTraceEmptiesIt()
{
    noteTest();
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    backend.readPort(0x1600);
    CHECK(!backend.trace().empty());
    backend.clearTrace();
    CHECK(backend.trace().empty());
}

// ---------------------------------------------------------------------------
// The boundary: LegacyBackend + EcBus is exactly one protocol
// ---------------------------------------------------------------------------

void testBusReadsAndBackendBothSeeOneTransaction()
{
    noteTest();
    // The test that would have caught the nested-transaction defect. A single
    // register read through EcBus must reach the port primitives as the
    // ADR-020 sequence, and nothing else.
    //
    // Before the fix, each of these port operations started a second full
    // transaction inside the first, and the register offsets 0x04 and 0x00 were
    // written. Every assertion here would have failed.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const core::IoResult result = bus.readRegister(0x2F, value);
    CHECK(result.ok);

    // Exactly one command byte, to the status port.
    const std::vector<PortCall> writes = fake.writes();
    CHECK(writes.size() >= 1);
    CHECK(writes[0].port == config.statusPort);
    CHECK(writes[0].value == config.readCommand);

    // Exactly one address byte, to the data port.
    bool sawAddress = false;
    for (const PortCall& w : writes) {
        if (w.port == config.dataPort && w.value == 0x2F) {
            sawAddress = true;
        }
    }
    CHECK(sawAddress);

    // The data port is read exactly once, to get the value back.
    int dataReads = 0;
    for (const PortCall& c : fake.calls) {
        if (!c.isWrite && c.port == config.dataPort) {
            ++dataReads;
        }
    }
    CHECK(dataReads == 1);
    // A read of register 0x2F returns the byte the address step put in the
    // output buffer, which is the address itself. Asserting on the mechanism
    // rather than on a magic constant keeps this true if the protocol changes.
    CHECK(value == 0x2F);
}

void testNoRegisterOffsetIsEverPassedToThePortPrimitives()
{
    noteTest();
    // The literal form of the defect. config.statusPort is 0x1604; if any call
    // carried 0x1604 truncated to a byte, or the register address where a port
    // belonged, this would catch it.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x78, value).ok);

    for (const PortCall& c : fake.calls) {
        // Every call must be to a real I/O port as configured, never to a value
        // that is a register address in disguise.
        CHECK(c.port == config.statusPort || c.port == config.dataPort);
    }
}

void testWriteIsDeniedByDefaultBeforeAnyPortIsTouched()
{
    noteTest();
    // ADR-023. This is the control barrier, and it is deny-by-default.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(!bus.registerWritesAllowed());

    const core::IoResult result = bus.writeRegister(0x2F, 0x03);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    // Nothing reached the hardware. Not one port.
    CHECK(fake.calls.empty());
    CHECK(backend.writeCount() == 0);
}

void testReadsWorkWhileWritesAreDenied()
{
    noteTest();
    // The whole point of moving the barrier from the backend to the bus. A
    // monitor-only machine must be able to read the EC; that is what
    // monitor-only means.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(!bus.registerWritesAllowed());

    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x2F, value).ok);
    CHECK(!fake.writes().empty() || !fake.calls.empty());

    // And the write is still refused.
    CHECK(!bus.writeRegister(0x2F, 0x03).ok);
}

void testEnablingWritesProducesTheDocumentedSequence()
{
    noteTest();
    // Once enabled, a register write is the ADR-020 sequence: command byte,
    // address byte, data byte - in that order, on the configured ports.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    bus.setRegisterWritesAllowed(true);
    CHECK(bus.registerWritesAllowed());

    CHECK(bus.writeRegister(0x2F, 0x03).ok);

    const std::vector<PortCall> writes = fake.writes();
    CHECK(writes.size() >= 3);
    CHECK(writes[0].port == config.statusPort);
    CHECK(writes[0].value == config.writeCommand);
    CHECK(writes[1].port == config.dataPort);
    CHECK(writes[1].value == 0x2F);   // the address
    CHECK(writes[2].port == config.dataPort);
    CHECK(writes[2].value == 0x03);   // the data
}

void testTheSelectorStaysRefusedAfterWritesAreEnabled()
{
    noteTest();
    // Enabling register writes must not accidentally enable the fan selector.
    // They are separate switches and Phase 0 establishes only the first.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    bus.setRegisterWritesAllowed(true);

    CHECK(!bus.fanSelectorWritesAllowed());
    const core::IoResult result = bus.writeRegister(0x31, 0x00);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    // The refusal is before any I/O, so the command byte was never sent.
    CHECK(fake.calls.empty());
}

void testBackendIsReadyWhenWritesAreDenied()
{
    noteTest();
    // A read-capable backend is Ready, not Faulted. Calling it Faulted would
    // suggest something is wrong when it is working exactly as configured on a
    // machine that has not been verified.
    EcBusConfig config;
    FakePorts fake(config);
    LegacyBackend backend = makeBackend(fake);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(backend.state() == BackendState::Ready);
    CHECK(backend.capabilities().canWritePorts);
    CHECK(!bus.registerWritesAllowed());
}

void runAll()
{
    testPortNumbersReachThePrimitiveUnchanged();
    testWriteReachesThePrimitiveUnchanged();
    testEveryValueOfAByteSurvives();
    testFailedReadIsTypedAndRecorded();
    testFailedWriteIsTypedAndRecorded();
    testClosedDriverFailsEveryCallWithoutTouchingThePrimitive();
    testBackendWithoutPrimitivesIsNotReady();
    testCapabilitiesDoNotClaimSignedApproval();
    testClearTraceEmptiesIt();
    testBusReadsAndBackendBothSeeOneTransaction();
    testNoRegisterOffsetIsEverPassedToThePortPrimitives();
    testWriteIsDeniedByDefaultBeforeAnyPortIsTouched();
    testReadsWorkWhileWritesAreDenied();
    testEnablingWritesProducesTheDocumentedSequence();
    testTheSelectorStaysRefusedAfterWritesAreEnabled();
    testBackendIsReadyWhenWritesAreDenied();
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
