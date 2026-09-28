#include "legacy_backend.h"

namespace tpfancontrol {
namespace core {

LegacyBackend::LegacyBackend(LegacyPortPrimitives primitives)
    : primitives_(std::move(primitives))
{
}

void LegacyBackend::setDriverOpen(bool open) noexcept
{
    driverOpen_ = open;
}

bool LegacyBackend::driverOpen() const noexcept
{
    return driverOpen_;
}

std::vector<PortCall> LegacyBackend::trace() const
{
    return trace_;
}

void LegacyBackend::clearTrace() noexcept
{
    trace_.clear();
}

std::uint64_t LegacyBackend::readCount() const noexcept
{
    return readCount_;
}

std::uint64_t LegacyBackend::writeCount() const noexcept
{
    return writeCount_;
}

BackendState LegacyBackend::state() const noexcept
{
    if (!driverOpen_) {
        return BackendState::Unavailable;
    }
    if (!primitives_.readPort) {
        // The caller supplied no way to read. Reporting Ready would let the
        // controller believe it has a backend and discover otherwise on the
        // first cycle, with a fault latch the user then has to clear.
        return BackendState::Unavailable;
    }
    return BackendState::Ready;
}

BackendCapabilities LegacyBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.name = "LegacyPorts";
    caps.version = "1";
    caps.canReadPorts = primitives_.readPort != nullptr;
    caps.canWritePorts = primitives_.writePort != nullptr;
    caps.canReadMsr = false;

    // The EC path goes through the WinIO driver. The driver is signed and
    // approved for the I/O ports it exposes, but the application must still be
    // reviewed before it ships, so that is not claimed here.
    //
    // Leaving it false is deliberate: nothing in the core should be able to
    // discover that control is permitted by asking the backend. Control
    // eligibility is a Phase 0 verdict carried in CapabilityReport, and the
    // ability to change a register is EcBus::setRegisterWritesAllowed. Neither
    // is discoverable from here.
    caps.signedAndApproved = false;
    return caps;
}

IoResult LegacyBackend::readPort(std::uint16_t port)
{
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the EC port driver is not open");
    }

    std::uint8_t value = 0;
    const bool ok = primitives_.readPort(port, value);
    // Zero on failure: a value that was not read is recorded as absent, not
    // as 0, because 0 is a plausible reading and a reader of this trace
    // should not have to know that convention to avoid being misled.
    trace_.push_back(PortCall{false, port, ok ? value : std::uint8_t{0}, ok});
    if (!ok) {
        ++readCount_;
        return IoResult::failure(IoErrorCode::ReadFailure,
                                 "the port read did not complete");
    }
    ++readCount_;
    return IoResult::success(value);
}

IoResult LegacyBackend::writePort(std::uint16_t port, std::uint8_t value)
{
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the EC port driver is not open");
    }
    if (!primitives_.writePort) {
        return IoResult::failure(IoErrorCode::Unsupported,
                                 "this backend has no write primitive");
    }

    // Recorded even on failure. A write that was attempted and did not land is
    // evidence; a write that was not attempted is only visible by its absence,
    // which is a much weaker claim to be able to make.
    const bool ok = primitives_.writePort(port, value);
    trace_.push_back(PortCall{true, port, value, ok});
    ++writeCount_;
    if (!ok) {
        return IoResult::failure(IoErrorCode::WriteFailure,
                                 "the port write did not complete");
    }
    return IoResult::success(value);
}

void LegacyBackend::close() noexcept
{
    // The driver belongs to the application, not to this adapter, so close()
    // only stops the adapter using it. Destroying the driver's handle is the
    // application's business, and doing it here would close a handle this class
    // never opened.
    driverOpen_ = false;
}

} // namespace core
} // namespace tpfancontrol
