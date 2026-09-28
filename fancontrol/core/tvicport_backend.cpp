#include "tvicport_backend.h"

#include <utility>

namespace tpfancontrol {
namespace core {

// The port primitives the one port-backend implementation runs on.
//
// Both capture the TVicPort callables by value, so nothing here depends on the
// lifetime of the adapter that made them. A missing callable leaves the
// primitive empty, which is how `LegacyBackend` reports an unavailable
// capability instead of calling nothing and reporting success.
LegacyPortPrimitives makeTvicPortPrimitives(const TvicPortApi& api)
{
    LegacyPortPrimitives primitives;

    if (api.readPort) {
        const std::function<std::uint8_t(std::uint16_t)> read = api.readPort;
        primitives.readPort = [read](std::uint16_t port, std::uint8_t& out) -> bool {
            // ReadPort returns a byte and cannot report failure. Returning true
            // is not a claim that the read happened - this API cannot make that
            // claim - it is the absence of a failure signal to forward. See the
            // header: correlation with an independent measurement is Phase 0's
            // step, and this adapter does not invent confidence it does not have.
            out = read(port);
            return true;
        };
    }

    if (api.writePort) {
        const std::function<void(std::uint16_t, std::uint8_t)> write = api.writePort;
        primitives.writePort = [write](std::uint16_t port, std::uint8_t value) -> bool {
            write(port, value);
            return true;
        };
    }

    return primitives;
}

TvicPortAttachResult TvicPortAttachResult::success(std::string message)
{
    TvicPortAttachResult result;
    result.ok = true;
    result.error = IoErrorCode::None;
    result.message = std::move(message);
    return result;
}

TvicPortAttachResult TvicPortAttachResult::failure(IoErrorCode error, std::string message)
{
    TvicPortAttachResult result;
    result.ok = false;
    result.error = error;
    result.message = std::move(message);
    return result;
}

TvicPortBackend::TvicPortBackend(TvicPortApi api, TvicPortOptions options)
    : api_(std::move(api)), options_(options), ports_(makeTvicPortPrimitives(api_))
{
    // Deliberately empty. See the header: the constructor is not allowed to
    // open the driver, and a test asserts that no API call happens here.
}

TvicPortBackend::~TvicPortBackend()
{
    // A destructor that closes a driver is a hidden lifecycle action, and this
    // adapter does not take them. detach() is the caller's move. Closing the
    // handle here could also close one the application opened and still uses.
    //
    // `ports_` is left holding a possibly-open driver flag, which is harmless:
    // it is destroyed with this object.
}

TvicPortAttachResult TvicPortBackend::attach()
{
    if (attached_) {
        return TvicPortAttachResult::success(
            "already attached; the driver was not opened again");
    }

    if (!api_.readPort) {
        return TvicPortAttachResult::failure(
            IoErrorCode::Unsupported,
            "the TVicPort API is incomplete: no port read function was supplied, "
            "so this backend would be unable to do the one thing it exists for");
    }

    // Was the driver already open? If so it is not ours to close, and it is not
    // ours to reconfigure either.
    bool alreadyOpen = false;
    if (api_.isDriverOpen) {
        if (api_.isDriverOpen()) {
            alreadyOpen = true;
        }
    }

    if (!alreadyOpen) {
        if (!api_.openDriver) {
            return TvicPortAttachResult::failure(
                IoErrorCode::Unsupported,
                "no way to open the TVicPort driver was supplied");
        }
        if (!api_.openDriver()) {
            return TvicPortAttachResult::failure(
                IoErrorCode::NotInitialized,
                "OpenTVicPort() failed. The vendor driver is not loaded, or it "
                "refused to load: on Windows, the message in the legacy "
                "application names tvicport.sys, and the reason is recorded in "
                "docs/DEPENDENCIES.md rather than worked around. This backend "
                "does not retry, and does not load anything itself");
        }
    }

    // Attached from here: the driver is open and this adapter may use it.
    // `weOpened_` records whether detach() must close it.
    attached_ = true;
    weOpened_ = !alreadyOpen;
    ports_.setDriverOpen(true);

    hardAccess_ = HardAccessReport();
    hardAccess_.openedByUs = weOpened_;
    hardAccess_.message =
        alreadyOpen ? "the driver was already open" : "the driver was opened by this adapter";

    if (!api_.testHardAccess) {
        hardAccess_.message +=
            "; the TVicPort API supplies no TestHardAccess, so the driver's "
            "hard-access state is unknown and was not changed";
        return TvicPortAttachResult::success(hardAccess_.message);
    }

    hardAccess_.reportedByDriver = api_.testHardAccess();

    if (hardAccess_.reportedByDriver) {
        hardAccess_.message +=
            "; the driver reports hard access is available, so nothing was changed";
        return TvicPortAttachResult::success(hardAccess_.message);
    }

    // The driver says port I/O is not available on this machine as configured.
    if (!options_.requestHardAccess) {
        hardAccess_.message +=
            "; the driver reports hard access is NOT available, and this backend "
            "was not asked to request it. The legacy application calls "
            "SetHardAccess(true) unconditionally at this point; that is a "
            "machine-specific decision, so it is reproduced only when "
            "TvicPortOptions::requestHardAccess is set, and it is recorded in "
            "the hardware report";
        return TvicPortAttachResult::success(hardAccess_.message);
    }

    if (!api_.setHardAccess) {
        hardAccess_.message +=
            "; hard access was requested but the TVicPort API supplies no "
            "SetHardAccess, so the driver was left as it is";
        return TvicPortAttachResult::success(hardAccess_.message);
    }

    api_.setHardAccess(true);
    hardAccess_.requestedByUs = true;
    hardAccess_.changed = true;
    hardAccess_.reportedByDriver = api_.testHardAccess();
    hardAccess_.message +=
        hardAccess_.reportedByDriver
            ? "; hard access was requested explicitly and the driver now reports "
              "it available"
            : "; hard access was requested explicitly and the driver still "
              "reports it unavailable. Port I/O may not work, and the hardware "
              "report must record this rather than presenting the run as a "
              "measurement";
    return TvicPortAttachResult::success(hardAccess_.message);
}

void TvicPortBackend::detach() noexcept
{
    if (!attached_) {
        return;
    }

    const bool open = ports_.driverOpen();
    attached_ = false;
    ports_.setDriverOpen(false);

    // Only a driver this adapter opened is closed here. If the application
    // opened it, closing it would take a handle away from the code that owns
    // it.
    if (weOpened_ && open && api_.closeDriver) {
        try {
            api_.closeDriver();
        } catch (...) {
            // A close that throws is recorded as a failure to close, not
            // propagated from a noexcept path. The driver is left as it is and
            // the process state is unchanged.
            hardAccess_.message +=
                "; CloseTVicPort threw while detaching, so the driver may still "
                "be open";
        }
    } else if (!weOpened_ && open) {
        hardAccess_.message +=
            "; the driver was already open when this adapter attached, so it "
            "was left open - it belongs to whoever opened it";
    }

    weOpened_ = false;
}

bool TvicPortBackend::attached() const noexcept
{
    return attached_;
}

const HardAccessReport& TvicPortBackend::hardAccess() const noexcept
{
    return hardAccess_;
}

std::string TvicPortBackend::unavailableReason() const
{
    if (state() == BackendState::Ready) {
        return std::string();
    }
    if (!attached_) {
        return "attach() has not been called, or the driver was closed after it was";
    }
    if (!api_.readPort) {
        return "the TVicPort API supplies no port read function";
    }
    return "the TVicPort driver is not open";
}

std::vector<PortCall> TvicPortBackend::trace() const
{
    return ports_.trace();
}

std::uint64_t TvicPortBackend::readCount() const noexcept
{
    return ports_.readCount();
}

std::uint64_t TvicPortBackend::writeCount() const noexcept
{
    return ports_.writeCount();
}

bool TvicPortBackend::driverIsOpenNow()
{
    if (!api_.isDriverOpen) {
        // Nothing to ask. The cached state stands, and the first port call will
        // produce whatever the driver does with it.
        return ports_.driverOpen();
    }

    const bool open = api_.isDriverOpen();
    if (!open && attached_) {
        // Closed behind this adapter's back - typically the application
        // shutting the driver down. Stop using it rather than calling into a
        // handle that is gone, and remember that we no longer own a close.
        attached_ = false;
        ports_.setDriverOpen(false);
    }
    return open;
}

BackendState TvicPortBackend::state() const noexcept
{
    // Pure, so it can be called from a report or a noexcept path. The live
    // check happens before every operation, in driverIsOpenNow().
    if (!attached_ || !api_.readPort) {
        return BackendState::Unavailable;
    }
    return ports_.driverOpen() ? BackendState::Ready : BackendState::Unavailable;
}

BackendCapabilities TvicPortBackend::capabilities() const
{
    BackendCapabilities caps;
    caps.name = "TVicPort";
    caps.version = api_.version.empty()
        ? "unknown (the DLL exposes no version through this API)"
        : api_.version;
    caps.canReadPorts = api_.readPort != nullptr;
    caps.canWritePorts = api_.writePort != nullptr;
    caps.canReadMsr = false;

    // False, and deliberately not a claim this adapter could make even in
    // principle. Whether this driver is acceptable on this machine - signature,
    // HVCI, Secure Boot, the vulnerable-driver blocklist - is measured, recorded
    // in docs/DEPENDENCIES.md and the Phase 0 report, and none of it is
    // discoverable by asking the DLL.
    caps.signedAndApproved = false;
    return caps;
}

IoResult TvicPortBackend::readPort(std::uint16_t port)
{
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized, unavailableReason());
    }
    if (!driverIsOpenNow()) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the TVicPort driver was closed after attach(), so "
                                 "no port call was made");
    }
    return ports_.readPort(port);
}

IoResult TvicPortBackend::writePort(std::uint16_t port, std::uint8_t value)
{
    if (state() != BackendState::Ready) {
        return IoResult::failure(IoErrorCode::NotInitialized, unavailableReason());
    }
    if (!driverIsOpenNow()) {
        return IoResult::failure(IoErrorCode::NotInitialized,
                                 "the TVicPort driver was closed after attach(), so "
                                 "no port call was made");
    }
    return ports_.writePort(port, value);
}

void TvicPortBackend::close() noexcept
{
    detach();
}

} // namespace core
} // namespace tpfancontrol
