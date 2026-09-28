#pragma once

#include "io_backend.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// Time source
// ---------------------------------------------------------------------------
// The EC protocol is defined by timeouts, so the clock has to be injectable:
// otherwise a test that exercises a timeout would have to actually wait for it,
// and a suite of such tests would take seconds of wall clock each. The
// production implementation is SteadyClock; tests supply a clock they advance
// by hand.
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::uint64_t nowMs() const noexcept = 0;
    virtual void sleepMs(std::uint64_t milliseconds) noexcept = 0;
};

// Monotonic, never derived from the wall clock: a wall-clock step backwards
// during NTP correction must not turn a bounded wait into an unbounded one.
class SteadyClock final : public IClock {
public:
    std::uint64_t nowMs() const noexcept override;
    void sleepMs(std::uint64_t milliseconds) noexcept override;
};

// ---------------------------------------------------------------------------
// Bus configuration
// ---------------------------------------------------------------------------
//
// The defaults here are transcribed from the legacy application's own EC
// layer, `fancontrol/portio.cpp`, which is the only in-repo record of how this
// machine family is actually addressed:
//
//     EC_CTRLPORT  0x1604      EC_DATAPORT  0x1600
//     EC_STAT_OBF  0x01        EC_STAT_IBF  0x02
//     EC_CTRLPORT_READ  0x80   EC_CTRLPORT_WRITE  0x81
//
// That makes them better evidence than a guess, but it is still evidence of
// WHAT THE SHIPPED APPLICATION DOES, not proof that it is correct on any
// particular machine. Generic EC documentation describes a 0x62/0x66 interface
// instead, and EC_REGISTER_MAP.md 3 records both. Phase 0 must confirm the
// mapping read-only before any write is issued.
struct EcBusConfig {
    std::uint16_t statusPort = 0x1604;
    std::uint16_t dataPort = 0x1600;

    // Status-register bits: input buffer full and output buffer full.
    std::uint8_t ibfMask = 0x02;
    std::uint8_t obfMask = 0x01;

    // These are OPERATION CODES, not a base to be OR'd with an address.
    //
    // The command byte says only whether the following data-port byte is a
    // register number to read from or a register number to write to. The
    // address itself is a full byte, written to the data port in a second
    // step, which is why this layer can reach registers such as 0x31 and 0x84.
    //
    // An earlier design encoded the address into the command byte instead, which
    // left only four addressable registers and contradicted every call site in
    // the legacy source. See ADR-020.
    std::uint8_t readCommand = 0x80;
    std::uint8_t writeCommand = 0x81;

    // Bounded by construction. Every wait is capped by timeoutMs AND by a
    // derived poll-count ceiling, so a clock that fails to advance still
    // terminates the loop instead of spinning.
    std::uint64_t timeoutMs = 50;
    std::uint64_t pollIntervalMs = 1;
    int maximumAttempts = 3;

    // Bounded by construction: the whole pre-transaction recovery wait, which
    // the legacy code allows a full second.
    std::uint64_t recoveryTimeoutMs = 100;

    // Rejects a configuration that could loop unboundedly or could not tell a
    // read from a write. A zero timeout, a zero poll interval, a non-positive
    // attempt count, identical ports, and an ambiguous command encoding are all
    // treated as configuration errors rather than silently normalised.
    bool isSane() const noexcept;
    std::string validationMessage() const;

    // True when the read and write operations are distinguishable at all.
    bool commandEncodingIsUnambiguous() const noexcept;

    // Classifies a command byte as a read or a write, or neither.
    enum class CommandKind { Unknown, Read, Write };
    CommandKind classifyCommand(std::uint8_t command) const noexcept;
};

// The fan-selector register. EC_REGISTER_MAP.md section 2 records it as
// "never write for the T14 single-fan profile" and forbids using it as an
// automatic topology probe; section 7 requires the single-fan path not to write
// it at all. Writing 0x80 to it is the conventional "stop the fan" idiom on
// some dual-fan machines, so an accidental write here is a thermal event, not a
// cosmetic bug.
inline constexpr std::uint8_t kFanSelectorRegister = 0x31;

// A record of one port write, retained so a test can assert what the code under
// test actually did rather than what it intended to do.
struct BusWrite {
    std::uint16_t port = 0;
    std::uint8_t value = 0;
    std::uint8_t address = 0;   // register addressed, or 0 for a non-register write
};

// ---------------------------------------------------------------------------
// EC bus
// ---------------------------------------------------------------------------
// A typed, timeout-bounded transaction layer over IIoBackend. It implements
// EC_REGISTER_MAP.md section 5: wait for buffer state with a bounded timeout,
// issue the command, verify completion, and return a typed error.
//
// The wire sequence is the one the legacy application uses, made bounded and
// typed. For a read:
//
//   1. wait for the input buffer to clear
//   2. drain the output buffer if a stale result is pending
//   3. write the read command to the status port
//   4. wait for the input buffer to clear
//   5. write the register address to the data port
//   6. wait for the output buffer to fill
//   7. read the value from the data port
//
// A write is the same up to step 5, then presents the value and waits for the
// input buffer to clear again, which is what confirms the EC accepted it.
//
// Thread safety: EC_REGISTER_MAP.md section 5 step 1 requires the EC access
// mutex. The portable equivalent is the mutex below, which serialises whole
// transactions so two threads cannot interleave a command and its data byte.
// It does NOT serialise against the legacy application's own thread; that is
// T3-06.
class EcBus {
public:
    EcBus(IIoBackend& backend, EcBusConfig config, IClock& clock);
    ~EcBus();

    EcBus(const EcBus&) = delete;
    EcBus& operator=(const EcBus&) = delete;

    // A read requires a writable command port even though it transfers no data,
    // so a read-only backend is a hard error rather than a silent success.
    IoResult readRegister(std::uint8_t address, std::uint8_t& value);

    // Refuses kFanSelectorRegister unless fanSelectorWritesAllowed() is set.
    // The T14 single-fan profile never sets it, so the refusal is structural
    // rather than a rule a caller can forget.
    IoResult writeRegister(std::uint8_t address, std::uint8_t value);

    // Dual-fan profiles only. Deliberately a separate opt-in that the
    // single-fan profile has no code path to call during normal control.
    void setFanSelectorWritesAllowed(bool allowed) noexcept;
    bool fanSelectorWritesAllowed() const noexcept;

    const EcBusConfig& config() const noexcept;

    // Every write this bus issued, in order, for evidence and for the T5-10
    // assertion that the single-fan path never touches 0x31.
    //
    // Returned by value: handing out a reference would let a caller read the
    // trace while another thread is still appending to it.
    std::vector<BusWrite> writeTrace() const;
    void clearTrace() noexcept;

    // Internal attempts abandoned on timeout, summed over every call. One call
    // can contribute several, because each attempt is retried.
    std::uint64_t timeoutCount() const noexcept;

    // Public readRegister/writeRegister calls made, counting each call once
    // regardless of how many internal attempts it took. Keeping the two
    // counters separate is what makes a retried failure distinguishable from
    // many separate failures.
    std::uint64_t transactionCount() const noexcept;

private:
    using Attempt = std::function<IoResult()>;

    // Applies the retry policy to one transaction. `description` appears in the
    // final failure message so a log identifies which operation failed.
    IoResult runTransaction(const char* description, const Attempt& attempt);

    // All of the state checks that must pass before a transaction is attempted.
    // Separated from readRegister/writeRegister so the two cannot drift.
    IoResult checkPreconditions(const char* description) const;

    // Recovers the bus to an idle state: input buffer clear, output buffer
    // drained. Bounded by recoveryTimeoutMs.
    IoResult recoverBus();

    IoResult waitForStatus(std::uint8_t mask, bool expectSet, const char* what);
    IoResult writeStatusPort(std::uint8_t value, std::uint8_t address);
    IoResult writeDataPort(std::uint8_t value, std::uint8_t address);
    std::uint64_t pollCeiling() const noexcept;

    IIoBackend& backend_;
    EcBusConfig config_;
    IClock& clock_;
    mutable std::mutex mutex_;
    bool fanSelectorWritesAllowed_ = false;
    std::vector<BusWrite> writeTrace_;
    std::uint64_t timeoutCount_ = 0;
    std::uint64_t transactionCount_ = 0;
};

} // namespace core
} // namespace tpfancontrol
