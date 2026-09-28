#pragma once

// ---------------------------------------------------------------------------
// T5-01 - the TVicPort backend, for baseline comparison.
//
// The legacy application reaches the EC through the TVicPort DLL. This adapter
// is an `IIoBackend` over that DLL, so the baseline can be compared with a
// modern backend on the same machine, in the same run, producing the same kind
// of report.
//
// What this class adds, and what it deliberately does not
// -------------------------------------------------------
// The port-level policy - forwarding port numbers unchanged, typing a failure,
// recording a trace, refusing to call anything when the driver is closed - is
// `LegacyBackend`, and there is exactly one implementation of it (ADR-023).
// This adapter wraps that implementation with the parts that are TVicPort's
// own:
//
//   1. the driver lifecycle: `OpenTVicPort`, `IsDriverOpened`, `CloseTVicPort`;
//   2. the hard-access decision: `TestHardAccess` / `SetHardAccess`;
//   3. a capability report that names the backend and claims nothing it has
//      not established.
//
// It knows nothing about EC registers. It cannot: its methods take ports, and a
// byte written to a port is forwarded, including 0x31 and 0x2F. Refusing those
// is register-level policy and belongs to `EcBus` (ADR-021, ADR-023). A test
// asserts this forwarding, so the layering cannot drift back.
//
// The hard-access decision is the one place this adapter is deliberately not
// the legacy application.
// ---------------------------------------------------------------------------
//
// What the legacy application does, and why this is different
// -----------------------------------------------------------
// `fancontrol/approot.cpp` waits up to 180 seconds for `OpenTVicPort()`, then
// calls `TestHardAccess()`, then `SetHardAccess(true)` unconditionally, then
// tests again and ignores the result.
//
// TVicPort's hard access is a mode in which the driver performs port I/O even
// when its own probe says the I/O is not safe to perform - the vendor's
// documentation describes it as bypassing the driver's check, and the legacy
// comment next to the call records that the mode exists to make the application
// work on machines where the checked path fails. Turning it on is therefore a
// decision about a specific machine, made by a person who has looked at that
// machine.
//
// So this adapter does not make it. `attach()` reports what the driver says,
// requests hard access only when the caller asked for it in `TvicPortOptions`,
// and records all three facts in `HardAccessReport`. The default is not to ask,
// which means a run on a machine where hard access is unavailable reports that
// state and continues in monitor-only operation, rather than silently changing
// a driver setting nobody reviewed.
//
// A failed port read cannot be detected through this API
// -----------------------------------------------------
// `ReadPort` returns a byte. There is no error channel, so a read that the
// driver did not really perform is indistinguishable from a byte it read
// successfully - typically 0xFF. The four bytes in this adapter's own trace are
// not a claim that the value was measured; that is Phase 0's correlation step
// (docs/HARDWARE_VERIFICATION.md section 7). What the adapter does guarantee is
// that a call is never *made* when the driver is closed, and that every call
// made is recorded.

#include "io_backend.h"
#include "legacy_backend.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// The TVicPort entry points this adapter uses, as callables.
//
// A seam rather than direct calls, for the same reason `LegacyPortPrimitives`
// is one: the class can then be tested on a machine that has no vendor DLL, no
// Windows, and no driver. The Windows binding that fills this struct from
// `TVicPort.lib` is `fancontrol/tvicport_dll.{h,cpp}`.
//
// Every member is optional. A missing member is reported as an unavailable
// capability, never called.
struct TvicPortApi {
    // OpenTVicPort: false when the driver is missing or would not load.
    std::function<bool()> openDriver;

    // CloseTVicPort. Called only for a driver this adapter opened itself.
    std::function<void()> closeDriver;

    // IsDriverOpened: whether the driver handle is currently open. Checked
    // before every port call, so an externally closed driver is noticed rather
    // than assumed.
    std::function<bool()> isDriverOpen;

    // TestHardAccess: the driver's answer to "may I do port I/O here".
    std::function<bool()> testHardAccess;

    // SetHardAccess: the driver's bypass switch. See the header comment above.
    std::function<void(bool)> setHardAccess;

    // ReadPort: returns a byte, and has no error channel.
    std::function<std::uint8_t(std::uint16_t port)> readPort;

    // WritePort.
    std::function<void(std::uint16_t port, std::uint8_t value)> writePort;

    // What the caller could establish about the DLL's version. Empty when
    // nothing was established, which is reported as unknown rather than filled
    // with a guess: the DLL exposes no version through this API, and the vendor
    // header's "4.0" describes the package, not the loaded file.
    std::string version;
};

struct TvicPortOptions {
    // Whether attach() may call SetHardAccess(true) when the driver reports
    // that hard access is unavailable.
    //
    // False, deliberately. The legacy application sets it unconditionally; that
    // is a behaviour to be measured and compared against (T4-06), not one to be
    // reproduced silently. Turning it on is a reviewed decision about a
    // specific machine.
    bool requestHardAccess = false;
};

struct TvicPortAttachResult {
    bool ok = false;
    IoErrorCode error = IoErrorCode::NotInitialized;
    std::string message;

    static TvicPortAttachResult success(std::string message);
    static TvicPortAttachResult failure(IoErrorCode error, std::string message);
};

// What happened with hard access, in the driver's words and in this adapter's.
// Recorded rather than decided quietly: a hardware report that omits this cannot
// say whether the baseline ran in the same mode as the legacy application.
struct HardAccessReport {
    bool openedByUs = false;        // whether attach() opened the driver itself
    bool reportedByDriver = false;  // TestHardAccess() after attach()
    bool requestedByUs = false;     // whether this adapter asked for hard access
    bool changed = false;           // whether SetHardAccess was called
    std::string message;
};

// The port primitives of a `TvicPortApi`, for a caller that owns the driver
// lifecycle itself.
//
// The application is such a caller: `approot.cpp` opens the driver for the
// whole session, and the legacy EC path uses the same handle, so the backend
// cannot own it. This function is what both use, so the mapping from the DLL's
// entry points to the port layer exists once (ADR-023).
LegacyPortPrimitives makeTvicPortPrimitives(const TvicPortApi& api);

class TvicPortBackend final : public IIoBackend {
public:
    // Nothing happens here. Constructing a backend must not open a driver, load
    // a kernel component, or change a driver setting: the caller decides that,
    // by calling attach().
    explicit TvicPortBackend(TvicPortApi api, TvicPortOptions options = {});
    ~TvicPortBackend() override;

    TvicPortBackend(const TvicPortBackend&) = delete;
    TvicPortBackend& operator=(const TvicPortBackend&) = delete;

    // Opens the driver if it is not already open, then records the hard-access
    // state. Returns the reason on failure; the backend stays Unavailable.
    TvicPortAttachResult attach();

    // Closes the driver if - and only if - this adapter opened it. A driver
    // that was already open belongs to whoever opened it.
    void detach() noexcept;

    bool attached() const noexcept;
    const HardAccessReport& hardAccess() const noexcept;

    // Why this backend is not Ready, when it is not. Empty when it is, or when
    // the reason is the ordinary one (attach() was never called).
    std::string unavailableReason() const;

    // The port calls that reached the TVicPort API, in order, from the one
    // implementation of the port policy.
    std::vector<PortCall> trace() const;
    std::uint64_t readCount() const noexcept;
    std::uint64_t writeCount() const noexcept;

    BackendState state() const noexcept override;
    BackendCapabilities capabilities() const override;
    IoResult readPort(std::uint16_t port) override;
    IoResult writePort(std::uint16_t port, std::uint8_t value) override;
    void close() noexcept override;

private:
    // The live driver check, refreshing the cached flag. Not noexcept: it calls
    // into the DLL. Called before every port operation so that a driver closed
    // behind this adapter's back is noticed at the next call rather than
    // assumed open.
    bool driverIsOpenNow();

    TvicPortApi api_;
    TvicPortOptions options_;
    LegacyBackend ports_;
    HardAccessReport hardAccess_;
    bool attached_ = false;
    bool weOpened_ = false;
};

} // namespace core
} // namespace tpfancontrol
