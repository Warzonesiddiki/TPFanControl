#include "legacy_backend.h"

namespace tpfancontrol {
namespace core {

namespace {

// The port numbers carry no meaning here. EcBus turns a register offset into
// ports using the configured map; this backend's port parameters exist only to
// satisfy IIoBackend, and are validated for range so a caller cannot pass a
// value that could not be an I/O port.
constexpr std::uint16_t kMaxPort = 0xFFFF;

} // namespace

LegacyBackend::LegacyBackend(LegacyEcPrimitives primitives)
    : primitives_(std::move(primitives))
{
}

void LegacyBackend::makeReadOnly() noexcept
{
    readOnly_ = true;
}

bool LegacyBackend::readOnly() const noexcept
{
    return readOnly_;
}

void LegacyBackend::setDriverOpen(bool open) noexcept
{
    driverOpen_ = open;
}

bool LegacyBackend::driverOpen() const noexcept
{
    return driverOpen_;
}

std::uint64_t LegacyBackend::readCount() const noexcept
{
    return readCount_;
}

std::uint64_t LegacyBackend::writeCount() const noexcept
{
    return writeCount_;
}

std::uint64_t LegacyBackend::refusedWriteCount() const noexcept
{
    return refusedWriteCount_;
}

BackendState LegacyBackend::state() const noexcept
{
    if (!driverOpen_) {
        return BackendState::Unavailable;
    }
    if (!primitives_.readByte) {
        // The caller supplied no way to read. Reporting Ready would let the
        // controller believe it has a backend and only discover otherwise on
        // the first cycle, with a fault latch the user then has to clear.
        return BackendState::Unavailable;
    }
    // A read-only backend is Ready, not Degraded. It can do everything the
    // monitor-only path needs. Calling it Faulted would suggest something is
    // wrong, when in fact it is working exactly as configured.
    return BackendState::Ready;
}

BackendCapabilities LegacyBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.name = "LegacyEC";
    caps.version = "1";
    caps.canReadPorts = true;
    // False when read-only, which is what tells the truth: this backend will
    // not write, so it does not claim to.
    caps.canWritePorts = !readOnly_ && primitives_.writeByte != nullptr;
    caps.canReadMsr = false;

    // The legacy EC path goes through the WinIO driver. The driver is signed
    // and approved for the I/O ports it exposes, but the application must still
    // be reviewed before it is shipped, so this is not claimed here. Leaving it
    // false is deliberate: nothing in the core should be able to discover that
    // control is permitted by asking the backend.
    caps.signedAndApproved = false;
    return caps;
}

IoResult LegacyBackend::readPort(std::uint16_t port)
{
    if (port > kMaxPort) {
        return IoResult::failure(IoErrorCode::InvalidPort,
                                 "port out of range");
    }
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the EC port driver is not open");
    }

    ++readCount_;

    std::uint8_t value = 0;
    // The offset is truncated to int because the legacy primitive takes one.
    // Truncation is safe here and only here: EcBus has already rejected an
    // unencodable address before it reaches a backend (ADR-019), and a port
    // number wider than int is not a register address at all.
    if (!primitives_.readByte(static_cast<int>(port), value)) {
        return IoResult::failure(IoErrorCode::ReadFailure,
                                 "the EC read did not complete");
    }
    return IoResult::success(value);
}

IoResult LegacyBackend::writePort(std::uint16_t port, std::uint8_t value)
{
    if (port > kMaxPort) {
        return IoResult::failure(IoErrorCode::InvalidPort,
                                 "port out of range");
    }

    // Checked in this order, and the order matters: a read-only backend on a
    // closed driver reports "unsupported" rather than "not initialised",
    // because the reason a write can never succeed here is the policy, not the
    // driver state. A user told the driver is the problem would go looking for
    // a driver problem that does not exist.
    if (readOnly_) {
        ++refusedWriteCount_;
        return IoResult::failure(
            IoErrorCode::Unsupported,
            "this backend is read-only: the register map has not been verified "
            "on this machine, so no EC write is permitted");
    }
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the EC port driver is not open");
    }
    if (!primitives_.writeByte) {
        ++refusedWriteCount_;
        return IoResult::failure(IoErrorCode::Unsupported,
                                 "this backend has no write primitive");
    }

    ++writeCount_;

    if (!primitives_.writeByte(static_cast<int>(port), value)) {
        return IoResult::failure(IoErrorCode::WriteFailure,
                                 "the EC write did not complete");
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
