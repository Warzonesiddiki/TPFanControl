#include "ec_protocol.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace tpfancontrol {
namespace core {

namespace {

// Recomputing a ceiling from the timeout keeps the wait bounded even if the
// injected clock never advances, which a buggy clock or a test double must not
// be able to turn into an infinite loop.
constexpr std::uint64_t kMinimumCeiling = 1;

// A retry is only worth attempting when the same attempt could plausibly
// succeed. "The backend is not there / not permitted / unsupported" is a
// standing condition, not a transient one, so repeating it just multiplies the
// failure and delays the report. A failed read or write, by contrast, may well
// be transient.
bool isRetryable(IoErrorCode code) noexcept
{
    switch (code) {
    case IoErrorCode::Timeout:
    case IoErrorCode::ReadFailure:
    case IoErrorCode::WriteFailure:
        return true;
    case IoErrorCode::None:
    case IoErrorCode::NotInitialized:
    case IoErrorCode::AccessDenied:
    case IoErrorCode::InvalidPort:
    case IoErrorCode::Disconnected:
    case IoErrorCode::Unsupported:
        return false;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// SteadyClock
// ---------------------------------------------------------------------------

std::uint64_t SteadyClock::nowMs() const noexcept
{
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void SteadyClock::sleepMs(std::uint64_t milliseconds) noexcept
{
    if (milliseconds == 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// ---------------------------------------------------------------------------
// EcBus
// ---------------------------------------------------------------------------

EcBus::EcBus(IIoBackend& backend, EcBusConfig config, IClock& clock)
    : backend_(backend), config_(config), clock_(clock)
{
}

EcBus::~EcBus() = default;

const EcBusConfig& EcBus::config() const noexcept
{
    return config_;
}

void EcBus::setFanSelectorWritesAllowed(bool allowed) noexcept
{
    fanSelectorWritesAllowed_ = allowed;
}

void EcBus::setRegisterWritesAllowed(bool allowed) noexcept
{
    registerWritesAllowed_ = allowed;
}

bool EcBus::registerWritesAllowed() const noexcept
{
    return registerWritesAllowed_;
}

bool EcBus::fanSelectorWritesAllowed() const noexcept
{
    return fanSelectorWritesAllowed_;
}

std::vector<BusWrite> EcBus::writeTrace() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return writeTrace_;
}

void EcBus::clearTrace() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    writeTrace_.clear();
}

std::uint64_t EcBus::timeoutCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return timeoutCount_;
}

std::uint64_t EcBus::transactionCount() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return transactionCount_;
}

std::uint64_t EcBus::pollCeiling() const noexcept
{
    const std::uint64_t derived = config_.timeoutMs / config_.pollIntervalMs + 1;
    return std::max<std::uint64_t>(derived, kMinimumCeiling);
}

// Waits until the status bit reaches the expected state. Bounded twice: by the
// deadline, and by a poll count derived from the same timeout, so the loop
// cannot spin forever even if the clock is frozen.
IoResult EcBus::waitForStatus(std::uint8_t mask, bool expectSet, const char* what)
{
    const std::uint64_t deadline = clock_.nowMs() + config_.timeoutMs;
    const std::uint64_t ceiling = pollCeiling();

    for (std::uint64_t poll = 0; poll < ceiling; ++poll) {
        const IoResult read = backend_.readPort(config_.statusPort);
        if (!read.ok) {
            return read;
        }

        if (((read.value & mask) != 0) == expectSet) {
            return read;
        }

        if (clock_.nowMs() >= deadline) {
            break;
        }
        clock_.sleepMs(config_.pollIntervalMs);
    }

    std::string message = std::string("EC ") + what + " did not reach the expected state within "
        + std::to_string(config_.timeoutMs) + "ms";
    message += expectSet ? " (bit never set)" : " (bit never cleared)";
    return IoResult::failure(IoErrorCode::Timeout, message);
}

IoResult EcBus::writeStatusPort(std::uint8_t value, std::uint8_t address)
{
    const IoResult result = backend_.writePort(config_.statusPort, value);
    if (result.ok) {
        // The address is recorded explicitly rather than recovered from the
        // command byte, because the command byte no longer carries it. The
        // T5-10 check walks this trace looking for 0x31, so a trace whose
        // address field were always zero would make that check vacuous.
        writeTrace_.push_back(BusWrite{config_.statusPort, value, address});
    }
    return result;
}

IoResult EcBus::writeDataPort(std::uint8_t value, std::uint8_t address)
{
    const IoResult result = backend_.writePort(config_.dataPort, value);
    if (result.ok) {
        writeTrace_.push_back(BusWrite{config_.dataPort, value, address});
    }
    return result;
}

IoResult EcBus::checkPreconditions(const char* description) const
{
    if (!config_.isSane()) {
        return IoResult::failure(IoErrorCode::Unsupported, config_.validationMessage());
    }
    if (backend_.state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
            std::string("backend is not ready; refusing to ") + description + " the EC");
    }
    if (!backend_.capabilities().canReadPorts) {
        return IoResult::failure(IoErrorCode::Unsupported,
            std::string("backend cannot read ports; a ") + description
            + " needs the status port to check the buffer state");
    }
    // Both directions need a writable port. A read writes a command byte, and a
    // write obviously does, so a read-only backend cannot serve either. Saying
    // so plainly is better than failing later on a timeout that looks like a
    // dead EC.
    if (!backend_.capabilities().canWritePorts) {
        return IoResult::failure(IoErrorCode::Unsupported,
            std::string("backend cannot write ports, which a ") + description + " requires");
    }
    return IoResult::success();
}

IoResult EcBus::recoverBus()
{
    // Wait for the input buffer to clear, so a command is not issued into an EC
    // that is still busy with the previous one.
    IoResult ready = waitForStatus(config_.ibfMask, false, "input buffer");
    if (!ready.ok) {
        return ready;
    }

    // A result left over from an earlier transaction is not this transaction's
    // answer. Draining it is what stops a stale byte from being returned as a
    // fresh reading.
    if ((ready.value & config_.obfMask) != 0) {
        const IoResult drained = backend_.readPort(config_.dataPort);
        if (!drained.ok) {
            return drained;
        }
    }
    return IoResult::success();
}

IoResult EcBus::runTransaction(const char* description, const Attempt& attempt)
{
    std::string lastError = "no attempt was made";
    IoErrorCode lastCode = IoErrorCode::Timeout;

    for (int n = 0; n < config_.maximumAttempts; ++n) {
        const IoResult result = attempt();
        if (result.ok) {
            return result;
        }
        if (!isRetryable(result.error)) {
            // A standing condition. Retrying it cannot succeed and only delays
            // the report, which is the delay the failsafe path is waiting on.
            return result;
        }
        if (result.error == IoErrorCode::Timeout) {
            ++timeoutCount_;
        }
        lastCode = result.error;
        lastError = result.message;
    }

    // The specific cause is preserved rather than collapsed into Timeout, so a
    // caller can still tell a permissions problem from a dead EC.
    return IoResult::failure(lastCode,
        std::string("EC ") + description + " failed after "
            + std::to_string(config_.maximumAttempts) + " attempt(s): " + lastError);
}

IoResult EcBus::readRegister(std::uint8_t address, std::uint8_t& value)
{
    value = 0;

    std::lock_guard<std::mutex> lock(mutex_);

    const IoResult ready = checkPreconditions("read");
    if (!ready.ok) {
        return ready;
    }

    ++transactionCount_;

    return runTransaction("read", [this, address, &value]() -> IoResult {
        IoResult step = recoverBus();
        if (!step.ok) {
            return step;
        }

        step = writeStatusPort(config_.readCommand, address);
        if (!step.ok) {
            return step;
        }

        step = waitForStatus(config_.ibfMask, false, "input buffer after read command");
        if (!step.ok) {
            return step;
        }

        step = writeDataPort(address, address);
        if (!step.ok) {
            return step;
        }

        step = waitForStatus(config_.obfMask, true, "output buffer");
        if (!step.ok) {
            return step;
        }

        const IoResult data = backend_.readPort(config_.dataPort);
        if (!data.ok) {
            return data;
        }
        value = data.value;
        return IoResult::success(value);
    });
}

IoResult EcBus::writeRegister(std::uint8_t address, std::uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const IoResult ready = checkPreconditions("write");
    if (!ready.ok) {
        return ready;
    }

    // The control barrier, enforced before the transaction is attempted and
    // before the fan-selector refusal, so a caller that ignored the error has
    // still touched nothing. Ordering matters for the same reason the selector
    // refusal below does: after the first port write it would be too late, and
    // the caller would already have stopped the fan.
    if (!registerWritesAllowed_) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "refusing to write EC register: register writes are not enabled on "
            "this bus. Reads are unaffected, so monitor-only operation still "
            "works. Enabling writes requires a Phase 0 hardware report for this "
            "machine (ADR-023).");
    }

    // The single-fan refusal, enforced before the transaction is attempted.
    // Ordering matters: if this ran after the first port write, a caller that
    // ignored the error would already have stopped the fan.
    if (address == kFanSelectorRegister && !fanSelectorWritesAllowed_) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "refusing to write the fan-selector register 0x31: the T14 single-fan "
            "profile must never write it (EC_REGISTER_MAP.md sections 2 and 7)");
    }

    ++transactionCount_;

    return runTransaction("write", [this, address, value]() -> IoResult {
        IoResult step = recoverBus();
        if (!step.ok) {
            return step;
        }

        step = writeStatusPort(config_.writeCommand, address);
        if (!step.ok) {
            return step;
        }

        step = waitForStatus(config_.ibfMask, false, "input buffer after write command");
        if (!step.ok) {
            return step;
        }

        step = writeDataPort(address, address);
        if (!step.ok) {
            return step;
        }

        step = waitForStatus(config_.ibfMask, false, "input buffer after address");
        if (!step.ok) {
            return step;
        }

        step = writeDataPort(value, address);
        if (!step.ok) {
            return step;
        }

        // A write is not complete until the EC has taken the byte. Returning
        // success here without this wait would report a write that may never
        // have landed, and the caller would skip its readback.
        return waitForStatus(config_.ibfMask, false, "input buffer after data");
    });
}

// ---------------------------------------------------------------------------
// EcBusReader
// ---------------------------------------------------------------------------

EcBusReader::EcBusReader(EcBus& bus) : bus_(bus)
{
}

IoResult EcBusReader::readRegister(std::uint8_t address)
{
    ++readsIssued_;
    std::uint8_t value = 0;
    return bus_.readRegister(address, value);
}

ReaderAudit EcBusReader::audit() const
{
    ReaderAudit out;
    out.readsIssued = readsIssued_;
    out.transactionCount = bus_.transactionCount();
    out.timeoutCount = bus_.timeoutCount();

    // The trace, not a guess. Every successful port write the bus issued is
    // classified by port, so a value that a read would never produce - the
    // write command byte - is visible rather than assumed absent.
    const EcBusConfig& config = bus_.config();
    for (const BusWrite& write : bus_.writeTrace()) {
        if (write.port == config.statusPort) {
            out.statusPortWriteValues.push_back(write.value);
        } else if (write.port == config.dataPort) {
            out.dataPortWriteValues.push_back(write.value);
        }
    }
    return out;
}

} // namespace core
} // namespace tpfancontrol
