#pragma once

#include "../fancontrol/core/ec_protocol.h"
#include "../fancontrol/core/io_backend.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace test {

// A test clock that only advances when the code under test asks it to sleep, so
// a timeout path costs no wall-clock time and is still fully exercised.
class FakeClock final : public core::IClock {
public:
    explicit FakeClock(std::uint64_t startMs = 0) : now_(startMs) {}

    std::uint64_t nowMs() const noexcept override;
    void sleepMs(std::uint64_t milliseconds) noexcept override;

    void advance(std::uint64_t milliseconds) noexcept;
    std::uint64_t sleepCalls() const noexcept;
    std::uint64_t totalSleptMs() const noexcept;

    // A clock whose sleep does nothing, used to prove that a wait bounded only
    // by a deadline would hang, and that the derived poll ceiling does not.
    void setFrozen(bool frozen) noexcept;

private:
    std::uint64_t now_;
    std::uint64_t sleepCalls_ = 0;
    std::uint64_t totalSlept_ = 0;
    bool frozen_ = false;
};

enum class EcFault {
    None,
    // Input buffer never clears: a write command is issued while the EC is busy.
    InputBufferStuck,
    // Output buffer never fills: the command is lost.
    OutputBufferStuck,
    // The EC latches the command but never releases it, so a write's completion
    // check times out after the data byte was already presented.
    WriteNeverCompletes,
    // A read returns a value that does not match what was asked for.
    CorruptReadback
};

// Models a conventional 0x62/0x66 EC closely enough to exercise EcBus, and can
// be told to misbehave in the specific ways EC_REGISTER_MAP.md section 6 asks
// the read path to tolerate.
class FakeEcBackend final : public core::IIoBackend {
public:
    // Takes the same EcBusConfig the code under test uses, so the fake and the
    // bus agree on command encoding instead of the fake guessing from a bit.
    explicit FakeEcBackend(const core::EcBusConfig& config);

    core::BackendState state() const noexcept override;
    core::BackendCapabilities capabilities() const override;
    core::IoResult readPort(std::uint16_t port) override;
    core::IoResult writePort(std::uint16_t port, std::uint8_t value) override;
    void close() noexcept override;

    // --- EC model ---------------------------------------------------------
    void setRegister(std::uint8_t address, std::uint8_t value);
    std::uint8_t registerValue(std::uint8_t address) const;
    void setReadOnly();                       // capabilities().canWritePorts = false

    // --- fault injection --------------------------------------------------
    void setFault(EcFault fault);
    EcFault fault() const noexcept;

    // The real protocol is two-phase: a command byte on the status port, then
    // the address on the data port, then the value for a write. This exposes
    // which phase the bus is in, so a test can assert the sequence rather than
    // only the end result.
    enum class Phase { Idle, AwaitingAddress, AwaitingValue, DrainingStale };
    Phase phase() const noexcept;

    // Fail exactly the next `count` port operations, read or write. Models a
    // transient backend error as distinct from a persistent one.
    void failNextOperations(int count);
    int pendingOperationFailures() const noexcept;

    // Fail every read from now on: the persistent-failure case, which must not
    // be papered over by retrying.
    void setPersistentReadFailure(bool enabled);
    void setPersistentWriteFailure(bool enabled);

    // The backend still advertises canWritePorts but the write itself is denied.
    // Models a driver that passes a capability probe and then fails the real
    // I/O, which is the only way an AccessDenied can actually reach the bus.
    void setDenyWritesAtRuntime(bool enabled);

    // Shut the backend down once `count` more operations have been performed,
    // i.e. part-way through a transaction.
    void shutdownAfterOperations(int count);
    bool hasShutDown() const noexcept;

    // --- observation ------------------------------------------------------
    struct Write {
        std::uint16_t port = 0;
        std::uint8_t value = 0;
    };

    // Every write ATTEMPTED, successful or not. Use this to check that an
    // operation was attempted even when it failed.
    const std::vector<Write>& writeLog() const noexcept;

    // Writes that actually took effect. A write that failed is not in here,
    // because "we tried" is not "the register changed" - and conflating the two
    // would make the T5-10 trace evidence overstate what happened.
    const std::vector<Write>& committedWrites() const noexcept;

    const std::vector<std::uint16_t>& readLog() const noexcept;
    std::size_t operationCount() const noexcept;
    void clearLogs();

    // Registers actually written through the command port, in order.
    std::vector<std::uint8_t> writtenRegisters() const;

private:
    bool consumeOperationFailure();
    void refreshStatus();

    core::EcBusConfig config_;

    core::BackendState state_;
    core::BackendCapabilities capabilities_;
    EcFault fault_ = EcFault::None;

    std::map<std::uint8_t, std::uint8_t> registers_;
    std::uint8_t dataByte_ = 0;
    std::uint8_t statusByte_ = 0;
    bool inputBufferFull_ = false;
    bool outputBufferFull_ = false;
    Phase phase_ = Phase::Idle;
    // The real EC latches a command byte, asserts IBF while it processes it,
    // then releases IBF. Modelled as "IBF clears on the next status read",
    // which is the smallest behaviour that is faithful: the bus must actually
    // observe a clear, or every wait would time out and the transaction would
    // never be exercisable.
    bool ibfClearsOnNextStatusRead_ = false;
    std::uint8_t pendingAddress_ = 0;
    bool pendingIsWrite_ = false;

    int pendingOperationFailures_ = 0;
    bool persistentReadFailure_ = false;
    bool persistentWriteFailure_ = false;
    bool denyWritesAtRuntime_ = false;
    int shutdownAfter_ = -1;
    bool shutDown_ = false;

    std::vector<Write> writeLog_;
    std::vector<Write> committedWrites_;
    std::vector<std::uint16_t> readLog_;
};

} // namespace test
} // namespace tpfancontrol
