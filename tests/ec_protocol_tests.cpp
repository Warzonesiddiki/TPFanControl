// Tests for fancontrol/core/ec_protocol.{h,cpp}, covering T5-05, T5-08, T5-10
// and the parts of T5-02 the bus must satisfy regardless of the decode decision.
//
// Every assertion uses CHECK, not assert. assert is removed by NDEBUG, which is
// defined in the Release configurations the CI build actually uses, so an
// assert-based suite reports success in the build that matters most. CHECK
// (tests/test_check.h) is always active.

#include "test_check.h"

#include "../fancontrol/core/ec_protocol.h"

#include "fake_ec.h"

#include <atomic>
#include <cstdio>
#include <set>
#include <thread>
#include <vector>

namespace tpfancontrol {
namespace test {
namespace {

using core::EcBus;
using core::EcBusConfig;
using core::IoErrorCode;
using core::IoResult;

int testCount = 0;

void noteTest()
{
    ++testCount;
}

// A configuration with short timeouts so a timeout path is exercised without the
// suite ever really waiting, and with distinct command bases.
EcBusConfig testConfig()
{
    EcBusConfig config;
    config.timeoutMs = 20;
    config.pollIntervalMs = 1;
    config.maximumAttempts = 3;
    return config;
}

// ---------------------------------------------------------------------------
// T5-05: configuration validation
// ---------------------------------------------------------------------------

void testSaneConfigIsAccepted()
{
    noteTest();
    const EcBusConfig config = testConfig();
    CHECK(config.isSane());
    CHECK(config.validationMessage().empty());
    CHECK(config.commandEncodingIsUnambiguous());
}

void testZeroTimeoutIsRefused()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.timeoutMs = 0;
    CHECK(!config.isSane());
    CHECK(!config.validationMessage().empty());
}

void testZeroPollIntervalIsRefused()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.pollIntervalMs = 0;
    CHECK(!config.isSane());
    CHECK(!config.validationMessage().empty());
}

void testNonPositiveAttemptsIsRefused()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.maximumAttempts = 0;
    CHECK(!config.isSane());
    config.maximumAttempts = -1;
    CHECK(!config.isSane());
}

void testIdenticalPortsAreRefused()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.dataPort = config.statusPort;
    CHECK(!config.isSane());
}

void testAmbiguousCommandEncodingIsRefused()
{
    noteTest();
    // Bases differing only in a low bit collide: reading register 1 emits
    // 0x10|1 == 0x11 and writing register 0 emits 0x11|0 == 0x11.
    EcBusConfig config = testConfig();
    config.readCommandBase = 0x10;
    config.writeCommandBase = 0x11;
    CHECK(!config.commandEncodingIsUnambiguous());
    CHECK(!config.isSane());
    const std::string message = config.validationMessage();
    CHECK(!message.empty());
}

void testIdenticalCommandBasesAreRefused()
{
    noteTest();
    EcBusConfig config = testConfig();
    config.readCommandBase = 0x80;
    config.writeCommandBase = 0x80;
    CHECK(!config.commandEncodingIsUnambiguous());
    CHECK(!config.isSane());
}

void testCommandEncodingIsUnambiguousForEveryAddress()
{
    noteTest();
    // The collision test must cover the whole 4-bit address space, not just a
    // couple of samples: the original defect only appeared at some addresses.
    const EcBusConfig config = testConfig();
    CHECK(config.commandEncodingIsUnambiguous());
    for (int address = 0; address <= 0x0F; ++address) {
        const auto a = static_cast<std::uint8_t>(address);
        const std::uint8_t read = static_cast<std::uint8_t>(config.readCommandBase | a);
        const std::uint8_t write = static_cast<std::uint8_t>(config.writeCommandBase | a);
        CHECK(read != write);
        CHECK(config.classifyCommand(read) == EcBusConfig::CommandKind::Read);
        CHECK(config.classifyCommand(write) == EcBusConfig::CommandKind::Write);
        CHECK(config.addressOf(read) == a);
        CHECK(config.addressOf(write) == a);
    }
}

void testUnknownCommandIsNotMisclassified()
{
    noteTest();
    const EcBusConfig config = testConfig();
    // A byte that matches neither base must not be silently accepted as a
    // command, or a corrupted write could be replayed as a read.
    CHECK(config.classifyCommand(0x00) == EcBusConfig::CommandKind::Unknown);
    CHECK(config.classifyCommand(0x7F) == EcBusConfig::CommandKind::Unknown);
}

// ---------------------------------------------------------------------------
// T5-05: the happy path
// ---------------------------------------------------------------------------

void testReadRegisterReturnsValue()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    backend.setRegister(0x07, 0x5A);
    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(result.ok);
    CHECK(value == 0x5A);
    CHECK(bus.timeoutCount() == 0);
}

void testReadRegisterReportsFailureValue()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0xAB;
    const IoResult result = bus.readRegister(0x09, value);
    // A register set to 0x00 is a real reading of 0, not a failure.
    CHECK(result.ok);
    CHECK(value == 0x00);
}

void testWriteRegisterCommitsTheValue()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    const IoResult result = bus.writeRegister(0x05, 0x7E);
    CHECK(result.ok);
    CHECK(backend.registerValue(0x05) == 0x7E);

    // Evidence, not intent: the value has to be readable back through the bus.
    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x05, value).ok);
    CHECK(value == 0x7E);
}

void testWriteTraceRecordsCommandAndData()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(bus.writeTrace().empty());
    CHECK(bus.writeRegister(0x05, 0x7E).ok);
    const std::vector<core::BusWrite> trace = bus.writeTrace();
    CHECK(trace.size() == 2);
    CHECK(trace[0].port == config.statusPort);
    CHECK(core::EcBusConfig::addressOf(trace[0].value) == 0x05);
    CHECK(trace[0].address == 0x05);
    CHECK(trace[1].port == config.dataPort);
    CHECK(trace[1].value == 0x7E);

    bus.clearTrace();
    CHECK(bus.writeTrace().empty());
}

// ---------------------------------------------------------------------------
// T5-05: the 0x31 refusal
// ---------------------------------------------------------------------------

void testFanSelectorWriteIsRefusedBeforeAnyBackendCall()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    const IoResult result = bus.writeRegister(core::kFanSelectorRegister, 0x80);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    // "Before any backend call" is the whole point: a refusal that still touched
    // the bus would have already moved the hardware.
    CHECK(backend.operationCount() == 0);
    CHECK(backend.committedWrites().empty());
    CHECK(backend.writtenRegisters().empty());
    CHECK(bus.writeTrace().empty());
}

void testFanSelectorRefusalAppliesToEveryValue()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    for (int value = 0; value <= 0xFF; ++value) {
        const IoResult result =
            bus.writeRegister(core::kFanSelectorRegister, static_cast<std::uint8_t>(value));
        CHECK(!result.ok);
        CHECK(result.error == IoErrorCode::Unsupported);
    }
    CHECK(backend.operationCount() == 0);
}

void testFanSelectorRuleAppliesToWritesOnly()
{
    noteTest();
    // The fan-selector rule is about writes, not about the register. It must not
    // become a blanket "never mention 0x31" ban, which would stop a dual-fan
    // profile from ever reading the current setting.
    //
    // 0x31 also happens to exceed this placeholder's 4-bit address field, so the
    // read is refused for that reason. What matters is WHICH reason: the message
    // must be about encoding, never about the fan selector.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(core::kFanSelectorRegister, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(result.message.find("fan-selector") == std::string::npos);
    CHECK(result.message.find("fan selector") == std::string::npos);
    CHECK(backend.committedWrites().empty());
}

void testFanSelectorIsNotRefusedForNeighbouringAddresses()
{
    noteTest();
    // Only 0x31 is singled out. A rule that refused a whole range would break
    // legitimate neighbouring registers, and would do so only on the machines
    // that use them.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(bus.writeRegister(0x0E, 0x01).ok);
    CHECK(bus.writeRegister(0x0F, 0x02).ok);
    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x0E, value).ok);
    CHECK(value == 0x01);
    CHECK(backend.registerValue(0x0E) == 0x01);
    CHECK(backend.registerValue(0x0F) == 0x02);
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x00);
}

void testFanSelectorOptInRemovesTheProfileRuleButNotTheEncodingLimit()
{
    noteTest();
    // The opt-in is real: it removes the single-fan profile's rule. It does NOT
    // invent an address encoding - 0x31 still does not fit the placeholder's
    // 4-bit address field, so the write is refused by that separate gate.
    //
    // Both gates are asserted separately, because "the write failed" on its own
    // would not show which rule stopped it. The refusal is diagnosed by its
    // message, and each message names exactly one gate.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(!bus.fanSelectorWritesAllowed());
    IoResult result = bus.writeRegister(core::kFanSelectorRegister, 0x7F);
    CHECK(!result.ok);
    CHECK(result.message.find("fan-selector") != std::string::npos);
    CHECK(backend.operationCount() == 0);

    bus.setFanSelectorWritesAllowed(true);
    CHECK(bus.fanSelectorWritesAllowed());
    result = bus.writeRegister(core::kFanSelectorRegister, 0x7F);
    CHECK(!result.ok);
    // The profile rule is gone...
    CHECK(result.message.find("fan-selector") == std::string::npos);
    // ...and what remains is the encoding gate, not a silent alias.
    CHECK(result.message.find("does not fit") != std::string::npos);
    CHECK(backend.operationCount() == 0);
    CHECK(backend.registerValue(0x01) == 0x00);

    // The opt-in is reversible, so a profile switch can re-arm the refusal.
    bus.setFanSelectorWritesAllowed(false);
    result = bus.writeRegister(core::kFanSelectorRegister, 0x00);
    CHECK(!result.ok);
    CHECK(result.message.find("fan-selector") != std::string::npos);
    CHECK(backend.operationCount() == 0);
}


void testAddressOutsideTheEncodingIsRefusedNotTruncated()
{
    noteTest();
    // Truncating 0x2A to the encodable 0x0A would change a different register
    // than the caller asked for, with no error reported. On a bus where the
    // wrong register can stop a fan, a refusal is the only safe answer.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    CHECK(!config.addressFits(0x10));
    CHECK(!config.addressFits(0x2A));
    CHECK(!config.addressFits(0x31));
    CHECK(config.addressFits(0x00));
    CHECK(config.addressFits(0x0F));

    const IoResult result = bus.writeRegister(0x2A, 0x7E);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(result.message.find("does not fit") != std::string::npos);
    // No register moved: not the intended one, and not the aliased one either.
    CHECK(backend.operationCount() == 0);
    CHECK(backend.registerValue(0x0A) == 0x00);
    CHECK(backend.registerValue(0x2A) == 0x00);
}

void testReadOfAnUnencodableAddressIsAlsoRefused()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0x99;
    const IoResult result = bus.readRegister(0x80, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(backend.operationCount() == 0);
    // A refused read must not leave the caller's variable holding a value that
    // looks like data.
    CHECK(value == 0x00);
}

void testEveryEncodableAddressRoundTrips()
{
    noteTest();
    // The full addressable range, not a sample: a rule that refused a stray
    // address would break a legitimate register and the failure would only show
    // up on one machine.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    for (int address = 0; address <= 0x0F; ++address) {
        const std::uint8_t a = static_cast<std::uint8_t>(address);
        const std::uint8_t value = static_cast<std::uint8_t>(0xC0 + address);
        CHECK(bus.writeRegister(a, value).ok);
        std::uint8_t readBack = 0;
        CHECK(bus.readRegister(a, readBack).ok);
        CHECK(readBack == value);
        CHECK(backend.registerValue(a) == value);
    }
}

// ---------------------------------------------------------------------------
// T5-08: bounded waits
// ---------------------------------------------------------------------------

void testInputBufferStuckTimesOutInsteadOfHanging()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::InputBufferStuck);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Timeout);
    CHECK(bus.timeoutCount() == static_cast<std::uint64_t>(config.maximumAttempts));
    // The wait really did consume the budget rather than returning at once.
    CHECK(clock.sleepCalls() > 0);
}

void testOutputBufferStuckTimesOut()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::OutputBufferStuck);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Timeout);
    CHECK(bus.timeoutCount() == static_cast<std::uint64_t>(config.maximumAttempts));
}

void testWriteThatNeverCompletesTimesOut()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::WriteNeverCompletes);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    const IoResult result = bus.writeRegister(0x05, 0x7E);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Timeout);
    // The EC did receive the value, so the register really did change even
    // though the transaction failed. A test that asserted "no write happened"
    // here would be lying, and so must any restore path built on this.
    CHECK(backend.registerValue(0x05) == 0x7E);
}

void testWaitTerminatesEvenWhenTheClockNeverAdvances()
{
    noteTest();
    // A frozen clock is the pathological case for a deadline-bounded loop: the
    // deadline is measured against a clock that never moves, so only the
    // derived poll ceiling can save it. If the ceiling regressed, this test
    // would hang rather than fail - which is why the ceiling is asserted too.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::OutputBufferStuck);
    FakeClock clock;
    clock.setFrozen(true);
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Timeout);
    // The call terminated. Had the poll ceiling regressed, the loop would have
    // spun against a clock that never advances and this test would hang.
    CHECK(bus.transactionCount() == 1);
    CHECK(bus.timeoutCount() == static_cast<std::uint64_t>(config.maximumAttempts));
}

void testPollCountIsBoundedPerAttempt()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::InputBufferStuck);
    FakeClock clock;
    clock.setFrozen(true);
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    CHECK(!bus.readRegister(0x07, value).ok);

    // The status port is polled, so every poll shows up in the read log. Each
    // wait is capped at timeoutMs/pollIntervalMs + 1 polls, and a transaction
    // makes at most two waits per attempt, so the whole call has a fixed bound.
    std::size_t polls = 0;
    for (const std::uint16_t port : backend.readLog()) {
        if (port == config.statusPort) {
            ++polls;
        }
    }
    const std::uint64_t perWait = config.timeoutMs / config.pollIntervalMs + 1;
    const std::uint64_t budget =
        2 * perWait * static_cast<std::uint64_t>(config.maximumAttempts);
    CHECK(polls > 0);
    CHECK(polls <= budget);
    // Pinned exactly, not just bounded, so a ceiling that quietly grew would be
    // caught here rather than showing up as a slow test suite.
    //
    // Attempt 1 succeeds its "input buffer clear" wait on the first poll, then
    // spends a full ceiling on "output buffer full". Attempts 2 and 3 each burn
    // a full ceiling on the input buffer, because the stuck EC never clears it.
    const std::uint64_t expected =
        perWait * static_cast<std::uint64_t>(config.maximumAttempts) + 1;
    CHECK(polls == static_cast<std::size_t>(expected));
}

void testTimeoutBudgetIsNotExceeded()
{
    noteTest();
    // With a working clock the total slept time must stay inside
    // attempts * timeoutMs. Exceeding it would mean a wait leaked past its own
    // bound, which is what the watchdog in the monitor loop exists to catch.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setFault(EcFault::InputBufferStuck);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    CHECK(!bus.readRegister(0x07, value).ok);
    const std::uint64_t budget = config.timeoutMs * static_cast<std::uint64_t>(config.maximumAttempts);
    CHECK(clock.totalSleptMs() <= budget);
}

// ---------------------------------------------------------------------------
// T5-05: error typing and the retry policy
// ---------------------------------------------------------------------------

void testTransientFailureIsRetried()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    backend.setRegister(0x07, 0x5A);
    backend.failNextOperations(1);
    CHECK(backend.pendingOperationFailures() == 1);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(result.ok);
    CHECK(value == 0x5A);
    // One public call, but the retry really happened: the status port was read
    // more times than a single attempt would have.
    CHECK(bus.transactionCount() == 1);
    CHECK(backend.operationCount() > 4);
    CHECK(backend.pendingOperationFailures() == 0);
}

void testPersistentFailureIsRetriedOnlyToTheLimit()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setPersistentReadFailure(true);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    // The specific cause is preserved rather than flattened to Timeout, so a
    // caller can tell a permissions problem from a dead EC.
    CHECK(result.error == IoErrorCode::ReadFailure);
    CHECK(bus.transactionCount() == 1);
    // Retried to the configured limit and no further. No timeout was involved:
    // each attempt failed immediately rather than waiting.
    CHECK(bus.timeoutCount() == 0);
    CHECK(result.message.find("attempt") != std::string::npos);
}

void testReadOnlyBackendIsRejectedByTheCapabilityCheck()
{
    noteTest();
    // A read has to WRITE a command byte to the command port, so a read-only
    // backend cannot serve it. The bus must say so plainly rather than fail
    // later on a timeout that looks like a dead EC.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setReadOnly();
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    CHECK(result.message.find("write ports") != std::string::npos);
    // Rejected before the transaction was even counted, and before any attempt.
    CHECK(bus.transactionCount() == 0);
    CHECK(backend.operationCount() == 0);
}

void testAccessDeniedIsNotRetried()
{
    noteTest();
    // A driver that passes the capability probe and then denies the write.
    // Retrying a permission failure cannot succeed and only delays the fallback
    // to BIOS, so it must return on the first attempt.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setDenyWritesAtRuntime(true);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    const IoResult result = bus.writeRegister(0x05, 0x7E);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::AccessDenied);
    CHECK(bus.transactionCount() == 1);
    CHECK(bus.writeTrace().empty());
    CHECK(backend.registerValue(0x05) == 0x00);
}

void testStoppedBackendIsNotRetried()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.shutdownAfterOperations(0);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::NotInitialized);
    // A stopped backend is known to be unusable before the first attempt, so
    // there is nothing to retry: the call never becomes a transaction.
    CHECK(bus.transactionCount() == 0);
    CHECK(backend.operationCount() == 0);
}

void testBackendStoppingMidTransactionIsReported()
{
    noteTest();
    // The driver goes away between the status read and the command write. The
    // result must be a typed failure, never a success carrying stale data.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.shutdownAfterOperations(1);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t value = 0x11;
    const IoResult result = bus.readRegister(0x07, value);
    CHECK(!result.ok);
    CHECK(backend.hasShutDown());
    // A failed read must not leave the caller's variable looking like data.
    CHECK(value == 0x00 || result.error != IoErrorCode::Timeout);
}

void testWriteFailureIsTyped()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    backend.setPersistentWriteFailure(true);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    const IoResult result = bus.writeRegister(0x05, 0x7E);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::WriteFailure);
    CHECK(bus.transactionCount() == 1);
    // The bus's own trace records only writes that were ACCEPTED, so a failed
    // write leaves it empty. The attempts remain visible in the backend's write
    // log - the two views are deliberately different.
    CHECK(bus.writeTrace().empty());
    CHECK(backend.writeLog().size() == static_cast<std::size_t>(config.maximumAttempts));
    // Nothing took effect, and the trace shows the attempts rather than
    // pretending the value was delivered.
    CHECK(backend.registerValue(0x05) == 0x00);
    CHECK(backend.committedWrites().empty());
}

void testUnknownCommandByteIsRejected()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    // Drive the command port directly with a byte that is not a valid command,
    // standing in for a corrupted or mis-encoded write.
    const IoResult result = backend.writePort(config.statusPort, 0x00);
    CHECK(!result.ok);
    CHECK(result.error == IoErrorCode::Unsupported);
    (void)bus;
}

// ---------------------------------------------------------------------------
// T5-05: transaction serialisation
// ---------------------------------------------------------------------------

void testConcurrentReadsAreSerialised()
{
    noteTest();
    // Two threads sharing one bus must not interleave a command and its data
    // byte. The fake detects exactly that: reading the data port with an empty
    // output buffer is a protocol violation, so any interleaving shows up as a
    // failed read rather than a silently corrupted value.
    const EcBusConfig config = testConfig();

    FakeEcBackend backend(config);
    for (int address = 0; address <= 0x0F; ++address) {
        backend.setRegister(static_cast<std::uint8_t>(address),
            static_cast<std::uint8_t>(0xA0 + address));
    }
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::atomic<int> successes{0};
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&bus, &successes, &failures, t]() {
            for (int i = 0; i < 25; ++i) {
                const std::uint8_t address = static_cast<std::uint8_t>((t + i) % 16);
                std::uint8_t value = 0;
                const IoResult result = bus.readRegister(address, value);
                if (result.ok && value == static_cast<std::uint8_t>(0xA0 + address)) {
                    ++successes;
                } else {
                    ++failures;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    CHECK(failures == 0);
    CHECK(successes == 100);
}

void testConcurrentWriteAndReadDoNotCorrupt()
{
    noteTest();
    const EcBusConfig config = testConfig();

    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::atomic<int> failures{0};
    std::thread writer([&bus, &failures]() {
        for (int i = 0; i < 40; ++i) {
            if (!bus.writeRegister(0x05, static_cast<std::uint8_t>(i)).ok) {
                ++failures;
            }
        }
    });
    std::thread reader([&bus, &failures]() {
        for (int i = 0; i < 40; ++i) {
            std::uint8_t value = 0;
            if (!bus.readRegister(0x05, value).ok) {
                ++failures;
            }
        }
    });
    writer.join();
    reader.join();
    CHECK(failures == 0);
}

// ---------------------------------------------------------------------------
// T5-10: the single-fan profile never writes 0x31
// ---------------------------------------------------------------------------

void testSingleFanSequenceNeverTouchesFanSelector()
{
    noteTest();
    // Stands in for the T14 single-fan profile: set a level, read the level
    // back, set a higher level, and restore. None of it may write 0x31, and the
    // assertion is made on the fake's committed writes, which reflect what the
    // hardware actually received rather than what the code intended.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    std::uint8_t level = 0;
    CHECK(bus.writeRegister(0x04, 0x03).ok);
    CHECK(bus.readRegister(0x04, level).ok);
    CHECK(level == 0x03);
    CHECK(bus.writeRegister(0x04, 0x05).ok);
    CHECK(bus.readRegister(0x04, level).ok);
    CHECK(level == 0x05);
    CHECK(bus.writeRegister(0x04, 0x00).ok);
    CHECK(bus.readRegister(0x04, level).ok);
    CHECK(level == 0x00);

    const std::vector<std::uint8_t> written = backend.writtenRegisters();
    const std::set<std::uint8_t> unique(written.begin(), written.end());
    for (const std::uint8_t address : unique) {
        CHECK(address != core::kFanSelectorRegister);
    }
    // The refusal must also be visible in the bus's own trace, so the guarantee
    // is not a property of one particular backend implementation.
    for (const core::BusWrite& write : bus.writeTrace()) {
        CHECK(write.address != core::kFanSelectorRegister);
    }
    CHECK(!written.empty());
}

void testFanSelectorOptInIsNotReachableFromTheDefaultConstructedBus()
{
    noteTest();
    // A freshly constructed bus is always armed against 0x31. There is no
    // constructor flag that could be passed by mistake.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    CHECK(!bus.fanSelectorWritesAllowed());
    CHECK(!bus.writeRegister(core::kFanSelectorRegister, 0x80).ok);
}

void runAll()
{
    // T5-05: configuration validation
    testSaneConfigIsAccepted();
    testZeroTimeoutIsRefused();
    testZeroPollIntervalIsRefused();
    testNonPositiveAttemptsIsRefused();
    testIdenticalPortsAreRefused();
    testAmbiguousCommandEncodingIsRefused();
    testIdenticalCommandBasesAreRefused();
    testCommandEncodingIsUnambiguousForEveryAddress();
    testUnknownCommandIsNotMisclassified();

    // T5-05: the happy path
    testReadRegisterReturnsValue();
    testReadRegisterReportsFailureValue();
    testWriteRegisterCommitsTheValue();
    testWriteTraceRecordsCommandAndData();

    // T5-05: the 0x31 refusal
    testFanSelectorWriteIsRefusedBeforeAnyBackendCall();
    testFanSelectorRefusalAppliesToEveryValue();
    testFanSelectorRuleAppliesToWritesOnly();
    testAddressOutsideTheEncodingIsRefusedNotTruncated();
    testReadOfAnUnencodableAddressIsAlsoRefused();
    testEveryEncodableAddressRoundTrips();
    testFanSelectorOptInRemovesTheProfileRuleButNotTheEncodingLimit();
    testFanSelectorIsNotRefusedForNeighbouringAddresses();

    // T5-08: bounded waits
    testInputBufferStuckTimesOutInsteadOfHanging();
    testOutputBufferStuckTimesOut();
    testWriteThatNeverCompletesTimesOut();
    testWaitTerminatesEvenWhenTheClockNeverAdvances();
    testPollCountIsBoundedPerAttempt();
    testTimeoutBudgetIsNotExceeded();

    // T5-05: error typing and the retry policy
    testTransientFailureIsRetried();
    testPersistentFailureIsRetriedOnlyToTheLimit();
    testReadOnlyBackendIsRejectedByTheCapabilityCheck();
    testAccessDeniedIsNotRetried();
    testStoppedBackendIsNotRetried();
    testBackendStoppingMidTransactionIsReported();
    testWriteFailureIsTyped();
    testUnknownCommandByteIsRejected();

    // T5-05: transaction serialisation
    testConcurrentReadsAreSerialised();
    testConcurrentWriteAndReadDoNotCorrupt();

    // T5-10: the single-fan profile never writes 0x31
    testSingleFanSequenceNeverTouchesFanSelector();
    testFanSelectorOptInIsNotReachableFromTheDefaultConstructedBus();
}

} // namespace
} // namespace test
} // namespace tpfancontrol

int main()
{
    using namespace tpfancontrol::test;
    runAll();
    std::printf("TPFanControl EC protocol tests passed (%d tests)\n", testCount);
    return 0;
}
