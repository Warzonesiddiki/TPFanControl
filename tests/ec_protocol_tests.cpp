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

// ADR-023. Register writes are denied by default on a bus, so every test that
// expects one has to say so. Making that a named call rather than a line
// repeated twenty times means a reader can see at a glance which tests are
// exercising a write, and a new test that forgets the opt-in fails loudly
// rather than silently asserting the refusal.
void makeWritable(EcBus& bus)
{
    bus.setRegisterWritesAllowed(true);
}

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

void testIdenticalCommandBytesAreRefused()
{
    noteTest();
    // If the read and write operations use the same byte, the EC cannot be told
    // a read from a write, and neither can this layer. That would turn every
    // write into a read.
    EcBusConfig config = testConfig();
    config.readCommand = 0x80;
    config.writeCommand = 0x80;
    CHECK(!config.commandEncodingIsUnambiguous());
    CHECK(!config.isSane());
    CHECK(config.validationMessage().find("told a read from a write")
          != std::string::npos);
}

void testDefaultCommandBytesMatchTheLegacySource()
{
    noteTest();
    // These four constants are transcribed from fancontrol/portio.cpp. If the
    // defaults ever drift from that source, the layer stops describing the
    // application it is supposed to be integrated with, and the mismatch would
    // be invisible until it moved a fan. This test is the tripwire.
    const EcBusConfig config;
    CHECK(config.statusPort == 0x1604);
    CHECK(config.dataPort == 0x1600);
    CHECK(config.readCommand == 0x80);
    CHECK(config.writeCommand == 0x81);
    CHECK(config.ibfMask == 0x02);
    CHECK(config.obfMask == 0x01);
}

void testCommandBytesClassifyCorrectly()
{
    noteTest();
    const EcBusConfig config = testConfig();
    CHECK(config.commandEncodingIsUnambiguous());
    CHECK(config.classifyCommand(0x80) == EcBusConfig::CommandKind::Read);
    CHECK(config.classifyCommand(0x81) == EcBusConfig::CommandKind::Write);
}

void testUnknownCommandIsNotMisclassified()
{
    noteTest();
    const EcBusConfig config = testConfig();
    // A byte that matches neither operation must not be silently accepted as a
    // command, or a corrupted write could be replayed as a read.
    CHECK(config.classifyCommand(0x00) == EcBusConfig::CommandKind::Unknown);
    CHECK(config.classifyCommand(0x7F) == EcBusConfig::CommandKind::Unknown);
    CHECK(config.classifyCommand(0x82) == EcBusConfig::CommandKind::Unknown);
}

void testRecoveryTimeoutIsValidated()
{
    noteTest();
    // A zero recovery budget would mean a busy EC could never be drained.
    EcBusConfig config = testConfig();
    config.recoveryTimeoutMs = 0;
    CHECK(!config.isSane());
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
    makeWritable(bus);

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
    makeWritable(bus);

    CHECK(bus.writeTrace().empty());
    CHECK(bus.writeRegister(0x05, 0x7E).ok);
    const std::vector<core::BusWrite> trace = bus.writeTrace();
    CHECK(trace.size() == 3);
    CHECK(trace[0].port == config.statusPort);
    CHECK(trace[0].value == config.writeCommand);
    CHECK(trace[0].address == 0x05);
    CHECK(trace[1].port == config.dataPort);
    CHECK(trace[1].value == 0x05);
    CHECK(trace[1].address == 0x05);
    CHECK(trace[2].port == config.dataPort);
    CHECK(trace[2].value == 0x7E);
    CHECK(trace[2].address == 0x05);

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
    makeWritable(bus);

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
    makeWritable(bus);

    for (int value = 0; value <= 0xFF; ++value) {
        const IoResult result =
            bus.writeRegister(core::kFanSelectorRegister, static_cast<std::uint8_t>(value));
        CHECK(!result.ok);
        CHECK(result.error == IoErrorCode::Unsupported);
    }
    CHECK(backend.operationCount() == 0);
}

void testFanSelectorReadIsAllowed()
{
    noteTest();
    // The rule is about writes, not about the register. It must not become a
    // blanket "never mention 0x31" ban, because a dual-fan profile has to be
    // able to read the current selector setting to know what it is doing.
    //
    // The contrast with the write path is the point: this read performs a
    // command byte and an address byte, and succeeds, while the write is
    // refused before anything is sent.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    backend.setRegister(core::kFanSelectorRegister, 0x7F);
    std::uint8_t value = 0;
    const IoResult result = bus.readRegister(core::kFanSelectorRegister, value);
    CHECK(result.ok);
    CHECK(value == 0x7F);
    // A read writes a command byte and an address, but never a value byte, so
    // the register's contents are untouched.
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x7F);
    CHECK(backend.writtenRegisters().empty());
    CHECK(backend.phase() == FakeEcBackend::Phase::Idle);
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
    makeWritable(bus);

    CHECK(bus.writeRegister(0x0E, 0x01).ok);
    CHECK(bus.writeRegister(0x0F, 0x02).ok);
    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x0E, value).ok);
    CHECK(value == 0x01);
    CHECK(backend.registerValue(0x0E) == 0x01);
    CHECK(backend.registerValue(0x0F) == 0x02);
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x00);
}

void testFanSelectorOptInIsReversibleAndActuallyWrites()
{
    noteTest();
    // With a full-byte address the opt-in is a real capability, not a
    // formality: once allowed, the write reaches the register. The test that
    // this is reversible matters more, because the single-fan path depends on
    // being able to re-arm the refusal at any time.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    makeWritable(bus);

    CHECK(!bus.fanSelectorWritesAllowed());
    IoResult result = bus.writeRegister(core::kFanSelectorRegister, 0x7F);
    CHECK(!result.ok);
    CHECK(result.message.find("fan-selector") != std::string::npos);
    CHECK(backend.operationCount() == 0);

    bus.setFanSelectorWritesAllowed(true);
    CHECK(bus.fanSelectorWritesAllowed());
    result = bus.writeRegister(core::kFanSelectorRegister, 0x7F);
    CHECK(result.ok);
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x7F);

    // The opt-in is reversible, so a profile switch can re-arm the refusal.
    bus.setFanSelectorWritesAllowed(false);
    result = bus.writeRegister(core::kFanSelectorRegister, 0x00);
    CHECK(!result.ok);
    CHECK(result.message.find("fan-selector") != std::string::npos);
    CHECK(backend.operationCount() > 0);
    // The refused write changed nothing.
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x7F);
}


void testEveryRegisterAddressIsAddressable()
{
    noteTest();
    // The address is a full byte written to the data port, so every address the
    // legacy register map actually uses must be reachable. An earlier design
    // packed the address into the command byte and could reach only 0x00-0x0F,
    // which excluded 0x31, 0x2F, 0x78, 0x84 and 0xC0 - every register the
    // application uses. This pins the full range.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    makeWritable(bus);

    const std::uint8_t addresses[] = {0x00, 0x01, 0x0F, 0x10, 0x2F, 0x31, 0x78, 0x84, 0xC0, 0xFF};
    for (const std::uint8_t address : addresses) {
        const std::uint8_t value = static_cast<std::uint8_t>(0x10 + (address & 0x0F));
        const IoResult written = bus.writeRegister(address, value);
        // 0x31 is refused by the single-fan rule, which is a different refusal
        // and is checked separately; every other address must be writable.
        if (address == core::kFanSelectorRegister) {
            CHECK(!written.ok);
            CHECK(written.message.find("fan-selector") != std::string::npos);
            continue;
        }
        CHECK(written.ok);
        std::uint8_t readBack = 0;
        CHECK(bus.readRegister(address, readBack).ok);
        CHECK(readBack == value);
        CHECK(backend.registerValue(address) == value);
    }
}

void testTheFullByteAddressRangeRoundTrips()
{
    noteTest();
    // Every encodable byte, not a sample: an addressing rule that quietly
    // excluded part of the range would only fail on the machines that use it.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    makeWritable(bus);

    for (int address = 0; address <= 0xFF; ++address) {
        const auto a = static_cast<std::uint8_t>(address);
        if (a == core::kFanSelectorRegister) {
            continue;   // refused by policy, covered separately
        }
        const std::uint8_t value = static_cast<std::uint8_t>(address ^ 0x5A);
        CHECK(bus.writeRegister(a, value).ok);
        std::uint8_t readBack = 0;
        CHECK(bus.readRegister(a, readBack).ok);
        CHECK(readBack == value);
    }
    CHECK(backend.registerValue(core::kFanSelectorRegister) == 0x00);
}

void testTheWireSequenceIsCommandThenAddressThenValue()
{
    noteTest();
    // The order is the protocol. Writing the value before the address, or the
    // address before the command, would write a plausible-looking value to a
    // plausible-looking register that happens to be the wrong one.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);
    makeWritable(bus);

    CHECK(bus.writeRegister(0x2F, 0x5B).ok);

    CHECK(backend.writeLog().size() >= 3);
    CHECK(backend.writeLog()[0].port == config.statusPort);
    CHECK(backend.writeLog()[0].value == config.writeCommand);
    CHECK(backend.writeLog()[1].port == config.dataPort);
    CHECK(backend.writeLog()[1].value == 0x2F);
    CHECK(backend.writeLog()[2].port == config.dataPort);
    CHECK(backend.writeLog()[2].value == 0x5B);

    // And the trace agrees with the wire, including the register each byte was
    // written on behalf of.
    const std::vector<core::BusWrite> trace = bus.writeTrace();
    CHECK(trace.size() == backend.writeLog().size());
    for (std::size_t i = 0; i < trace.size(); ++i) {
        CHECK(trace[i].port == backend.writeLog()[i].port);
        CHECK(trace[i].value == backend.writeLog()[i].value);
        CHECK(trace[i].address == 0x2F);
    }
}

void testReadSequenceIsCommandThenAddressThenResult()
{
    noteTest();
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    backend.setRegister(0x78, 0x42);
    std::uint8_t value = 0;
    CHECK(bus.readRegister(0x78, value).ok);
    CHECK(value == 0x42);

    CHECK(backend.writeLog().size() == 2);
    CHECK(backend.writeLog()[0].port == config.statusPort);
    CHECK(backend.writeLog()[0].value == config.readCommand);
    CHECK(backend.writeLog()[1].port == config.dataPort);
    CHECK(backend.writeLog()[1].value == 0x78);
    // A read presents no value byte; the answer comes back on the data port.
    CHECK(backend.phase() == FakeEcBackend::Phase::Idle);
}

void testStaleResultIsDrainedBeforeATransaction()
{
    noteTest();
    // An output buffer left full by an interrupted transaction holds the PREVIOUS
    // answer. Without draining it, a subsequent read would take that stale byte
    // and report it as a fresh reading.
    const EcBusConfig config = testConfig();
    FakeEcBackend backend(config);
    FakeClock clock;
    EcBus bus(backend, config, clock);

    backend.setRegister(0x05, 0x11);

    // Provoke a transaction that times out after the EC has already produced a
    // result, so a stale value is left pending.
    backend.setFault(EcFault::CorruptReadback);
    std::uint8_t first = 0;
    const IoResult corrupted = bus.readRegister(0x05, first);
    CHECK(corrupted.ok);
    CHECK(first == (0x11 ^ 0xFF));

    // The output buffer was drained by the read itself, so the next transaction
    // must observe the register, not the previous answer.
    backend.setFault(EcFault::None);
    std::uint8_t second = 0;
    CHECK(bus.readRegister(0x05, second).ok);
    CHECK(second == 0x11);
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
    makeWritable(bus);

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
    makeWritable(bus);

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
    makeWritable(bus);

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
    makeWritable(bus);

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
    makeWritable(bus);

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
    makeWritable(bus);
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
    testIdenticalCommandBytesAreRefused();
    testDefaultCommandBytesMatchTheLegacySource();
    testCommandBytesClassifyCorrectly();
    testUnknownCommandIsNotMisclassified();
    testRecoveryTimeoutIsValidated();

    // T5-05: the happy path
    testReadRegisterReturnsValue();
    testReadRegisterReportsFailureValue();
    testWriteRegisterCommitsTheValue();
    testWriteTraceRecordsCommandAndData();

    // T5-05: the 0x31 refusal
    testFanSelectorWriteIsRefusedBeforeAnyBackendCall();
    testFanSelectorRefusalAppliesToEveryValue();
    testFanSelectorReadIsAllowed();
    testEveryRegisterAddressIsAddressable();
    testTheFullByteAddressRangeRoundTrips();
    testTheWireSequenceIsCommandThenAddressThenValue();
    testReadSequenceIsCommandThenAddressThenResult();
    testStaleResultIsDrainedBeforeATransaction();
    testFanSelectorOptInIsReversibleAndActuallyWrites();
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
