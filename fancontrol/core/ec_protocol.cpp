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
// EcBusConfig
// ---------------------------------------------------------------------------

bool EcBusConfig::commandEncodingIsUnambiguous() const noexcept
{
    // The address occupies the low nibble, so only the high nibble can carry the
    // read/write distinction. Anything else collides for at least one address.
    return (readCommandBase & 0xF0) != (writeCommandBase & 0xF0);
}

EcBusConfig::CommandKind EcBusConfig::classifyCommand(std::uint8_t command) const noexcept
{
    const std::uint8_t high = static_cast<std::uint8_t>(command & 0xF0);
    if (high == static_cast<std::uint8_t>(readCommandBase & 0xF0)) {
        return CommandKind::Read;
    }
    if (high == static_cast<std::uint8_t>(writeCommandBase & 0xF0)) {
        return CommandKind::Write;
    }
    return CommandKind::Unknown;
}

bool EcBusConfig::addressFits(std::uint8_t address) const noexcept
{
    const int limit = 1 << kAddressBits;
    return address < static_cast<std::uint8_t>(limit);
}

std::uint8_t EcBusConfig::addressOf(std::uint8_t command) noexcept
{
    return static_cast<std::uint8_t>(command & 0x0F);
}

bool EcBusConfig::isSane() const noexcept
{
    // A zero poll interval with a non-zero timeout is a busy-wait; a zero
    // timeout is a single attempt with no grace. Both are refused rather than
    // corrected, because silently "fixing" a configuration is how a caller
    // ends up believing it asked for something it did not.
    return timeoutMs > 0
        && pollIntervalMs > 0
        && maximumAttempts > 0
        && statusPort != dataPort
        && commandEncodingIsUnambiguous();
}

std::string EcBusConfig::validationMessage() const
{
    if (timeoutMs == 0) {
        return "EcBusConfig: timeoutMs must be greater than zero";
    }
    if (pollIntervalMs == 0) {
        return "EcBusConfig: pollIntervalMs must be greater than zero";
    }
    if (maximumAttempts <= 0) {
        return "EcBusConfig: maximumAttempts must be greater than zero";
    }
    if (statusPort == dataPort) {
        return "EcBusConfig: statusPort and dataPort must differ";
    }
    if (!commandEncodingIsUnambiguous()) {
        // Reported precisely: the first colliding address is the one a reader
        // needs in order to understand the failure.
        for (int address = 0; address < 16; ++address) {
            const std::uint8_t low = static_cast<std::uint8_t>(address);
            const std::uint8_t readCommand =
                static_cast<std::uint8_t>((readCommandBase & 0xF0) | low);
            const std::uint8_t writeCommand =
                static_cast<std::uint8_t>((writeCommandBase & 0xF0) | low);
            if (readCommand == writeCommand) {
                return "EcBusConfig: readCommandBase and writeCommandBase are ambiguous: "
                    "read and write of register " + std::to_string(address)
                    + " both emit 0x" + std::to_string(readCommand)
                    + "; the bases must differ in their high nibble";
            }
        }
        return "EcBusConfig: readCommandBase and writeCommandBase must differ in their high nibble";
    }
    return std::string();
}

// ---------------------------------------------------------------------------
// EcBus
// ---------------------------------------------------------------------------

EcBus::EcBus(IIoBackend& backend, EcBusConfig config, IClock& clock)
    : backend_(backend), config_(config), clock_(clock)
{
}

EcBus::~EcBus() = default;

void EcBus::setFanSelectorWritesAllowed(bool allowed) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    fanSelectorWritesAllowed_ = allowed;
}

bool EcBus::fanSelectorWritesAllowed() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return fanSelectorWritesAllowed_;
}

const EcBusConfig& EcBus::config() const noexcept
{
    return config_;
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

IoResult EcBus::readStatus(std::uint8_t& status)
{
    IoResult result = backend_.readPort(config_.statusPort);
    if (!result.ok) {
        return result;
    }
    status = result.value;
    return IoResult::success(status);
}

IoResult EcBus::writeCommand(std::uint8_t command)
{
    IoResult result = backend_.writePort(config_.statusPort, command);
    if (result.ok) {
        // Record the address actually encoded in the command, not a fixed zero.
        // The T5-10 check walks the trace looking for 0x31, and a trace whose
        // address field is always zero would make that check vacuous.
        writeTrace_.push_back(
            BusWrite{config_.statusPort, command, EcBusConfig::addressOf(command)});
    }
    return result;
}

// Waits until the status bit reaches the expected state. Bounded twice: by the
// deadline, and by a poll count derived from the same timeout, so the loop
// cannot spin forever even if the clock is frozen.
IoResult EcBus::waitForStatus(std::uint8_t mask, bool expectSet, const char* what)
{
    const std::uint64_t deadline = clock_.nowMs() + config_.timeoutMs;
    const std::uint64_t ceiling = pollCeiling();

    for (std::uint64_t poll = 0; poll < ceiling; ++poll) {
        std::uint8_t status = 0;
        IoResult read = readStatus(status);
        if (!read.ok) {
            return read;
        }

        const bool isSet = (status & mask) != 0;
        if (isSet == expectSet) {
            return IoResult::success(status);
        }

        const std::uint64_t now = clock_.nowMs();
        if (now >= deadline) {
            break;
        }
        clock_.sleepMs(config_.pollIntervalMs);
    }

    std::string message = std::string("EC ") + what + " did not reach the expected state within "
        + std::to_string(config_.timeoutMs) + "ms";
    if (expectSet) {
        message += " (bit never set)";
    } else {
        message += " (bit never cleared)";
    }
    return IoResult::failure(IoErrorCode::Timeout, message);
}

IoResult EcBus::readRegister(std::uint8_t address, std::uint8_t& value)
{
    value = 0;

    std::lock_guard<std::mutex> lock(mutex_);

    if (!config_.isSane()) {
        return IoResult::failure(IoErrorCode::Unsupported, config_.validationMessage());
    }
    if (!config_.addressFits(address)) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "register address 0x" + std::to_string(address) + " does not fit the "
            + std::to_string(EcBusConfig::kAddressBits)
            + "-bit address field of the current command encoding; refusing to "
            "truncate it to a different register");
    }
    if (backend_.state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
            "backend is not ready; refusing to read the EC");
    }
    if (!backend_.capabilities().canReadPorts) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "backend cannot read ports; a read needs the command port");
    }
    // A read writes the command port. A read-only backend cannot serve it, and
    // reporting that plainly is better than failing later on a timeout.
    if (!backend_.capabilities().canWritePorts) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "backend cannot write ports, which a read command requires");
    }

    ++transactionCount_;

    std::string lastError = "no attempt was made";
    IoErrorCode lastCode = IoErrorCode::Timeout;
    for (int attempt = 0; attempt < config_.maximumAttempts; ++attempt) {
        // 1. The input buffer must be empty before a command is issued.
        IoResult ready = waitForStatus(config_.ibfMask, false, "input buffer");
        if (!ready.ok) {
            if (!isRetryable(ready.error)) {
                return ready;
            }
            if (ready.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = ready.error;
            lastError = ready.message;
            continue;
        }

        // 2. Issue the read command.
        IoResult issued = writeCommand(
            static_cast<std::uint8_t>(config_.readCommandBase | address));
        if (!issued.ok) {
            // A failed command write follows the same retry policy as a
            // failed status read. Returning here would make WriteFailure the
            // one retryable code that is never retried, which is exactly the
            // inconsistency that hides a transient backend fault.
            if (!isRetryable(issued.error)) {
                return issued;
            }
            if (issued.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = issued.error;
            lastError = issued.message;
            continue;
        }

        // 3. Wait for the output buffer to fill.
        IoResult filled = waitForStatus(config_.obfMask, true, "output buffer");
        if (!filled.ok) {
            if (!isRetryable(filled.error)) {
                return filled;
            }
            if (filled.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = filled.error;
            lastError = filled.message;
            continue;
        }

        // 4. Read the data byte.
        IoResult data = backend_.readPort(config_.dataPort);
        if (!data.ok) {
            return data;
        }
        value = data.value;
        return IoResult::success(value);
    }

    return IoResult::failure(lastCode,
        "EC read of register 0x" + std::to_string(address) + " failed after "
            + std::to_string(config_.maximumAttempts) + " attempt(s): " + lastError);
}

IoResult EcBus::writeRegister(std::uint8_t address, std::uint8_t value)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!config_.isSane()) {
        return IoResult::failure(IoErrorCode::Unsupported, config_.validationMessage());
    }

    // The single-fan refusal, enforced before the backend is even consulted.
    // Ordering matters: if this ran after the first port write, a caller that
    // ignored the error would already have stopped the fan.
    if (address == kFanSelectorRegister && !fanSelectorWritesAllowed_) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "refusing to write the fan-selector register 0x31: the T14 single-fan "
            "profile must never write it (EC_REGISTER_MAP.md sections 2 and 7)");
    }

    if (!config_.addressFits(address)) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "register address 0x" + std::to_string(address) + " does not fit the "
            + std::to_string(EcBusConfig::kAddressBits)
            + "-bit address field of the current command encoding; refusing to "
            "truncate it to a different register");
    }

    if (backend_.state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
            "backend is not ready; refusing to write the EC");
    }
    if (!backend_.capabilities().canWritePorts) {
        return IoResult::failure(IoErrorCode::Unsupported, "backend cannot write ports");
    }
    if (!backend_.capabilities().canReadPorts) {
        return IoResult::failure(IoErrorCode::Unsupported,
            "backend cannot read ports, which a write completion check requires");
    }

    ++transactionCount_;

    std::string lastError = "no attempt was made";
    IoErrorCode lastCode = IoErrorCode::Timeout;
    for (int attempt = 0; attempt < config_.maximumAttempts; ++attempt) {
        // 1. Input buffer must be empty.
        IoResult ready = waitForStatus(config_.ibfMask, false, "input buffer");
        if (!ready.ok) {
            if (!isRetryable(ready.error)) {
                return ready;
            }
            if (ready.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = ready.error;
            lastError = ready.message;
            continue;
        }

        // 2. Issue the write command.
        IoResult issued = writeCommand(
            static_cast<std::uint8_t>(config_.writeCommandBase | address));
        if (!issued.ok) {
            // A failed command write follows the same retry policy as a
            // failed status read. Returning here would make WriteFailure the
            // one retryable code that is never retried, which is exactly the
            // inconsistency that hides a transient backend fault.
            if (!isRetryable(issued.error)) {
                return issued;
            }
            if (issued.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = issued.error;
            lastError = issued.message;
            continue;
        }

        // 3. Wait for the EC to acknowledge before presenting data.
        IoResult filled = waitForStatus(config_.obfMask, true, "output buffer");
        if (!filled.ok) {
            if (!isRetryable(filled.error)) {
                return filled;
            }
            if (filled.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = filled.error;
            lastError = filled.message;
            continue;
        }

        // 4. Present the data byte.
        IoResult written = backend_.writePort(config_.dataPort, value);
        if (written.ok) {
            writeTrace_.push_back(BusWrite{config_.dataPort, value, address});
        } else {
            return written;
        }

        // 5. The write is only complete once the EC has taken the byte.
        IoResult completed = waitForStatus(config_.ibfMask, false, "input buffer after write");
        if (!completed.ok) {
            if (!isRetryable(completed.error)) {
                return completed;
            }
            if (completed.error == IoErrorCode::Timeout) {
                ++timeoutCount_;
            }
            lastCode = completed.error;
            lastError = completed.message;
            continue;
        }

        return IoResult::success();
    }

    return IoResult::failure(lastCode,
        "EC write of register 0x" + std::to_string(address) + " failed after "
            + std::to_string(config_.maximumAttempts) + " attempt(s): " + lastError);
}

} // namespace core
} // namespace tpfancontrol
