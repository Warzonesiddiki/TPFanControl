#pragma once

#include "ec_access.h"
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

    // Whether writeRegister() may change a register at all. FALSE by default.
    //
    // This is the control barrier, and it belongs here rather than in a
    // backend. An earlier version put it in LegacyBackend as makeReadOnly(),
    // on the reasoning that a backend which cannot write cannot cause harm.
    // That was wrong, and wrong in a way worth recording: the EC protocol
    // writes a command byte to the status port even to READ, so EcBus rejects
    // a backend that cannot write ports - and a "read-only" backend made every
    // read fail, breaking monitor-only operation entirely. The barrier cannot
    // be "no port writes"; it has to be "no REGISTER writes", and only the
    // layer that knows which writes are register writes can enforce that.
    //
    // Reads are unaffected. That is the whole point: monitor-only operation
    // must work on a machine where control has never been enabled.
    //
    // Deny by default rather than allow by default, so that the code which can
    // change someone's fan is the code that has to be edited to do it. The
    // application does not call this until Phase 0 verifies a machine; see
    // ADR-023.
    void setRegisterWritesAllowed(bool allowed) noexcept;
    bool registerWritesAllowed() const noexcept;

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
    bool registerWritesAllowed_ = false;
    std::vector<BusWrite> writeTrace_;
    std::uint64_t timeoutCount_ = 0;
    std::uint64_t transactionCount_ = 0;
};

// ---------------------------------------------------------------------------
// The read-only view a diagnostic uses
// ---------------------------------------------------------------------------
// A register-level reader over one bus, and the only EC access ecdiag is given
// (T5-09). It exists so the diagnostic never names EcBus: the tool is handed
// something with a single read operation and no way to write, and the audit
// comes from the bus's own trace rather than from the tool's belief about what
// it asked for.
//
// It does not relax anything. A read through this class has to pass the same
// preconditions as a direct call, so it fails the same way when the
// configuration is unusable or the backend is not ready.
class EcBusReader final : public IRegisterReader {
public:
    explicit EcBusReader(EcBus& bus);

    IoResult readRegister(std::uint8_t address) override;
    ReaderAudit audit() const override;

private:
    EcBus& bus_;

    // Counted here rather than by the caller, because the comparison between
    // "reads the plan called for" and "reads that reached a reader" is only
    // meaningful when the second number comes from the reader.
    std::uint64_t readsIssued_ = 0;
};

} // namespace core
} // namespace tpfancontrol
