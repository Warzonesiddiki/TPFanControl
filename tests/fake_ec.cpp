#include "fake_ec.h"

namespace tpfancontrol {
namespace test {

// ---------------------------------------------------------------------------
// FakeClock
// ---------------------------------------------------------------------------

std::uint64_t FakeClock::nowMs() const noexcept
{
    return now_;
}

void FakeClock::sleepMs(std::uint64_t milliseconds) noexcept
{
    ++sleepCalls_;
    totalSlept_ += milliseconds;
    if (!frozen_) {
        now_ += milliseconds;
    }
}

void FakeClock::advance(std::uint64_t milliseconds) noexcept
{
    now_ += milliseconds;
}

std::uint64_t FakeClock::sleepCalls() const noexcept
{
    return sleepCalls_;
}

std::uint64_t FakeClock::totalSleptMs() const noexcept
{
    return totalSlept_;
}

void FakeClock::setFrozen(bool frozen) noexcept
{
    frozen_ = frozen;
}

// ---------------------------------------------------------------------------
// FakeEcBackend
// ---------------------------------------------------------------------------

FakeEcBackend::FakeEcBackend(const core::EcBusConfig& config)
    : config_(config), state_(core::BackendState::Ready)
{
    capabilities_.name = "fake-ec";
    capabilities_.version = "test";
    capabilities_.canReadPorts = true;
    capabilities_.canWritePorts = true;
    capabilities_.canReadMsr = false;
    capabilities_.signedAndApproved = false;
    refreshStatus();
}

core::BackendState FakeEcBackend::state() const noexcept
{
    return state_;
}

core::BackendCapabilities FakeEcBackend::capabilities() const
{
    return capabilities_;
}

void FakeEcBackend::refreshStatus()
{
    statusByte_ = 0;
    if (outputBufferFull_) {
        statusByte_ = static_cast<std::uint8_t>(statusByte_ | 0x01);
    }
    if (inputBufferFull_) {
        statusByte_ = static_cast<std::uint8_t>(statusByte_ | 0x02);
    }
}

bool FakeEcBackend::consumeOperationFailure()
{
    if (pendingOperationFailures_ <= 0) {
        return false;
    }
    --pendingOperationFailures_;
    return true;
}

core::IoResult FakeEcBackend::readPort(std::uint16_t port)
{
    readLog_.push_back(port);

    if (shutdownAfter_ >= 0
        && static_cast<int>(readLog_.size() + writeLog_.size()) > shutdownAfter_) {
        shutDown_ = true;
        state_ = core::BackendState::Stopped;
    }
    if (state_ != core::BackendState::Ready) {
        return core::IoResult::failure(core::IoErrorCode::NotInitialized,
            "fake EC backend is not ready");
    }
    if (persistentReadFailure_ || consumeOperationFailure()) {
        return core::IoResult::failure(core::IoErrorCode::ReadFailure,
            "injected fake EC read failure");
    }

    if (port == config_.statusPort) {
        refreshStatus();
        return core::IoResult::success(statusByte_);
    }
    if (port == config_.dataPort) {
        if (!outputBufferFull_) {
            // Reading the data port with an empty output buffer is a protocol
            // error, not a legitimate zero. This is also how the mutex test
            // detects an interleaved transaction.
            return core::IoResult::failure(core::IoErrorCode::ReadFailure,
                "data port read while the output buffer was empty");
        }
        if (fault_ == EcFault::CorruptReadback) {
            return core::IoResult::success(static_cast<std::uint8_t>(dataByte_ ^ 0xFF));
        }
        return core::IoResult::success(dataByte_);
    }
    return core::IoResult::success(0);
}

core::IoResult FakeEcBackend::writePort(std::uint16_t port, std::uint8_t value)
{
    writeLog_.push_back(Write{port, value});

    if (shutdownAfter_ >= 0
        && static_cast<int>(readLog_.size() + writeLog_.size()) > shutdownAfter_) {
        shutDown_ = true;
        state_ = core::BackendState::Stopped;
    }
    if (state_ != core::BackendState::Ready) {
        return core::IoResult::failure(core::IoErrorCode::NotInitialized,
            "fake EC backend is not ready");
    }
    if (persistentWriteFailure_ || consumeOperationFailure()) {
        return core::IoResult::failure(core::IoErrorCode::WriteFailure,
            "injected fake EC write failure");
    }
    if (!capabilities_.canWritePorts) {
        return core::IoResult::failure(core::IoErrorCode::AccessDenied,
            "fake EC backend is read-only");
    }
    if (denyWritesAtRuntime_) {
        return core::IoResult::failure(core::IoErrorCode::AccessDenied,
            "fake EC backend denied the write despite advertising the capability");
    }

    if (port == config_.statusPort) {
        // Classified through the same config the bus uses, rather than guessed
        // from a bit: a bit heuristic cannot tell a read from a write when the
        // two command bases differ only in a low bit.
        const core::EcBusConfig::CommandKind kind = config_.classifyCommand(value);
        if (kind == core::EcBusConfig::CommandKind::Unknown) {
            return core::IoResult::failure(core::IoErrorCode::Unsupported,
                "command byte 0x" + std::to_string(value)
                + " matches neither the read nor the write command base");
        }
        committedWrites_.push_back(Write{port, value});
        pendingAddress_ = core::EcBusConfig::addressOf(value);
        pendingIsWrite_ = (kind == core::EcBusConfig::CommandKind::Write);
        awaitingDataByte_ = pendingIsWrite_;

        if (fault_ == EcFault::InputBufferStuck) {
            inputBufferFull_ = true;
            refreshStatus();
            return core::IoResult::success();
        }

        if (fault_ == EcFault::OutputBufferStuck) {
            // The command is accepted and the input buffer clears, but the EC
            // never presents a result, so the output buffer never fills.
            inputBufferFull_ = false;
            outputBufferFull_ = false;
            refreshStatus();
            return core::IoResult::success();
        }

        inputBufferFull_ = false;
        outputBufferFull_ = true;
        if (!pendingIsWrite_) {
            // A read command presents the register contents immediately.
            const auto it = registers_.find(pendingAddress_);
            dataByte_ = (it == registers_.end()) ? 0 : it->second;
            awaitingDataByte_ = false;
        } else if (fault_ == EcFault::WriteNeverCompletes) {
            // The data byte will be accepted but the EC never releases the
            // input buffer, so the write's completion check must time out.
            inputBufferFull_ = true;
        }
        refreshStatus();
        return core::IoResult::success();
    }

    if (port == config_.dataPort) {
        if (!awaitingDataByte_) {
            return core::IoResult::failure(core::IoErrorCode::WriteFailure,
                "data port written while no write was pending");
        }
        committedWrites_.push_back(Write{port, value});
        registers_[pendingAddress_] = value;
        awaitingDataByte_ = false;
        if (fault_ != EcFault::WriteNeverCompletes) {
            inputBufferFull_ = false;
        }
        outputBufferFull_ = false;
        refreshStatus();
        return core::IoResult::success();
    }

    return core::IoResult::success();
}

void FakeEcBackend::close() noexcept
{
    state_ = core::BackendState::Stopped;
}

void FakeEcBackend::setRegister(std::uint8_t address, std::uint8_t value)
{
    registers_[address] = value;
}

std::uint8_t FakeEcBackend::registerValue(std::uint8_t address) const
{
    const auto it = registers_.find(address);
    return it == registers_.end() ? 0 : it->second;
}

void FakeEcBackend::setReadOnly()
{
    capabilities_.canWritePorts = false;
}

void FakeEcBackend::setFault(EcFault fault)
{
    fault_ = fault;
}

EcFault FakeEcBackend::fault() const noexcept
{
    return fault_;
}

void FakeEcBackend::failNextOperations(int count)
{
    pendingOperationFailures_ = count;
}

int FakeEcBackend::pendingOperationFailures() const noexcept
{
    return pendingOperationFailures_;
}

void FakeEcBackend::setPersistentReadFailure(bool enabled)
{
    persistentReadFailure_ = enabled;
}

void FakeEcBackend::setPersistentWriteFailure(bool enabled)
{
    persistentWriteFailure_ = enabled;
}

void FakeEcBackend::setDenyWritesAtRuntime(bool enabled)
{
    denyWritesAtRuntime_ = enabled;
}

void FakeEcBackend::shutdownAfterOperations(int count)
{
    shutdownAfter_ = count;
    if (count <= 0) {
        shutDown_ = true;
        state_ = core::BackendState::Stopped;
    }
}

bool FakeEcBackend::hasShutDown() const noexcept
{
    return shutDown_;
}

const std::vector<FakeEcBackend::Write>& FakeEcBackend::writeLog() const noexcept
{
    return writeLog_;
}

const std::vector<FakeEcBackend::Write>& FakeEcBackend::committedWrites() const noexcept
{
    return committedWrites_;
}

const std::vector<std::uint16_t>& FakeEcBackend::readLog() const noexcept
{
    return readLog_;
}

std::size_t FakeEcBackend::operationCount() const noexcept
{
    return readLog_.size() + writeLog_.size();
}

void FakeEcBackend::clearLogs()
{
    writeLog_.clear();
    committedWrites_.clear();
    readLog_.clear();
}

std::vector<std::uint8_t> FakeEcBackend::writtenRegisters() const
{
    std::vector<std::uint8_t> addresses;
    for (const Write& write : committedWrites_) {
        if (write.port == config_.statusPort
            && config_.classifyCommand(write.value) == core::EcBusConfig::CommandKind::Write) {
            addresses.push_back(core::EcBusConfig::addressOf(write.value));
        }
    }
    return addresses;
}

} // namespace test
} // namespace tpfancontrol
