#pragma once

#include "io_backend.h"

#include <cstdint>
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
struct EcBusConfig {
    // CANDIDATE VALUES, not verified evidence. EC_REGISTER_MAP.md section 3 is
    // explicit that the project must not switch between the 0x62/0x66 mapping
    // and the legacy 0x1600 mapping based on guesswork, and section 9 requires
    // every value here to be replaced by a read-only measurement on the target
    // machine during Phase 0. They are defaults, not facts.
    std::uint16_t statusPort = 0x62;
    std::uint16_t dataPort = 0x66;

    // Command byte is the command base OR'd with the 4-bit register address.
    //
    // The two bases MUST differ in their high nibble. Bases that differ only in
    // a low bit are ambiguous: with 0x10/0x11, reading register 1 emits 0x11
    // and writing register 0 emits 0x11, so the EC cannot tell them apart and
    // neither can this layer. That is not a theoretical concern - it was the
    // default in an earlier draft of this file, and it silently turned every
    // read into a write in the fake backend before the test caught it.
    //
    // The values below are a non-colliding PLACEHOLDER chosen so the layer is
    // well-defined. They are NOT a claim about the target machine's encoding,
    // which Phase 0 must establish before any write is attempted.
    std::uint8_t readCommandBase = 0x80;
    std::uint8_t writeCommandBase = 0xC0;

    // Status-register bits: input buffer full and output buffer full.
    std::uint8_t ibfMask = 0x02;
    std::uint8_t obfMask = 0x01;

    // Bounded by construction. Every wait is capped by timeoutMs AND by a
    // derived poll-count ceiling, so a clock that fails to advance still
    // terminates the loop instead of spinning.
    std::uint64_t timeoutMs = 50;
    std::uint64_t pollIntervalMs = 1;
    int maximumAttempts = 3;

    // Rejects a configuration that could loop unboundedly or could not tell a
    // read from a write. A zero timeout, a zero poll interval, a non-positive
    // attempt count, and an ambiguous command encoding are all treated as
    // configuration errors rather than silently normalised.
    bool isSane() const noexcept;
    std::string validationMessage() const;

    // The address occupies the low nibble of the command byte, so this
    // placeholder encoding can only address 0x00-0x0F. That is narrower than
    // the real register map, which includes 0x31 - one of the reasons the
    // encoding is a placeholder and Phase 0 must replace it before any write.
    //
    // An address that does not fit is REFUSED, not truncated. Truncating would
    // turn a write to 0x2A into a write to 0x0A: a different register than the
    // caller asked for, changed silently, on a bus where the wrong register can
    // stop a fan. Refusing is the only safe answer while the encoding is a guess.
    static constexpr int kAddressBits = 4;
    bool addressFits(std::uint8_t address) const noexcept;

    // True when no register address produces the same command byte for a read
    // and for a write.
    bool commandEncodingIsUnambiguous() const noexcept;

    // Classifies a command byte as a read or a write, or neither.
    enum class CommandKind { Unknown, Read, Write };
    CommandKind classifyCommand(std::uint8_t command) const noexcept;
    static std::uint8_t addressOf(std::uint8_t command) noexcept;
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
// Thread safety: EC_REGISTER_MAP.md section 5 step 1 requires the EC access
// mutex. The portable equivalent is the mutex below, which serialises whole
// transactions so two threads cannot interleave a command and its data byte.
// An OS-level mutex and the Windows backend are out of scope here.
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

    // Internal attempts that were abandoned on timeout, summed over every call.
    // One call can contribute several, because each attempt is retried. A
    // non-zero value is the signal that the EC is not responding, not merely
    // that a read returned a value the caller disliked.
    std::uint64_t timeoutCount() const noexcept;

    // Public readRegister/writeRegister calls made, counting each call once
    // regardless of how many internal attempts it took. Use timeoutCount() to
    // see the retry load. Keeping the two separate is what makes a retried
    // failure distinguishable from many separate failures.
    std::uint64_t transactionCount() const noexcept;

private:
    IoResult waitForStatus(std::uint8_t mask, bool expectSet, const char* what);
    IoResult readStatus(std::uint8_t& status);
    IoResult writeCommand(std::uint8_t command);
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
