#include "fake_backend.h"

namespace tpfancontrol {
namespace test {

FakeBackend::FakeBackend()
    : state_(core::BackendState::Ready),
      capabilities_(),
      readFailure_(false),
      writeFailure_(false),
      values_(),
      operations_()
{
    capabilities_.name = "fake";
    capabilities_.version = "test";
    capabilities_.canReadPorts = true;
    capabilities_.canWritePorts = true;
    capabilities_.signedAndApproved = false;
}

core::BackendState FakeBackend::state() const noexcept
{
    return state_;
}

core::BackendCapabilities FakeBackend::capabilities() const
{
    return capabilities_;
}

core::IoResult FakeBackend::readPort(std::uint16_t port)
{
    operations_.push_back(FakeOperation{false, port, 0});
    if (state_ != core::BackendState::Ready) {
        return core::IoResult::failure(core::IoErrorCode::NotInitialized, "fake backend is not ready");
    }
    if (readFailure_) {
        return core::IoResult::failure(core::IoErrorCode::ReadFailure, "injected fake read failure");
    }

    const auto iterator = values_.find(port);
    return core::IoResult::success(iterator == values_.end() ? 0 : iterator->second);
}

core::IoResult FakeBackend::writePort(std::uint16_t port, std::uint8_t value)
{
    operations_.push_back(FakeOperation{true, port, value});
    if (state_ != core::BackendState::Ready) {
        return core::IoResult::failure(core::IoErrorCode::NotInitialized, "fake backend is not ready");
    }
    if (writeFailure_) {
        return core::IoResult::failure(core::IoErrorCode::WriteFailure, "injected fake write failure");
    }

    values_[port] = value;
    return core::IoResult::success();
}

void FakeBackend::close() noexcept
{
    state_ = core::BackendState::Stopped;
}

void FakeBackend::setReadFailure(bool enabled) noexcept
{
    readFailure_ = enabled;
}

void FakeBackend::setWriteFailure(bool enabled) noexcept
{
    writeFailure_ = enabled;
}

void FakeBackend::setValue(std::uint16_t port, std::uint8_t value)
{
    values_[port] = value;
}

const std::vector<FakeOperation>& FakeBackend::operations() const noexcept
{
    return operations_;
}

} // namespace test
} // namespace tpfancontrol
