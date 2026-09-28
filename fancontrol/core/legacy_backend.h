#pragma once

#include "io_backend.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// One call to the privileged I/O primitive, recorded for evidence.
//
// The trace is the point of this class. The EC transaction is a sequence of
// port writes and reads, and a claim like "this cycle wrote nothing" is only
// checkable if the calls are recorded. Without a trace, the only way to find
// out what reached the hardware is to read the hardware.
// ---------------------------------------------------------------------------
struct PortCall {
    bool isWrite = false;
    std::uint16_t port = 0;
    std::uint8_t value = 0;
    bool ok = false;
};

// ---------------------------------------------------------------------------
// An IIoBackend over the legacy driver's port primitives.
//
// T3-02. The legacy application reaches the EC through TVicPort's ReadPort and
// WritePort, and `EcBus` speaks the two-phase protocol on top of those
// (ADR-020). So this class is a PORT backend: it forwards port numbers
// unchanged and interprets nothing.
//
// Why that distinction is load-bearing
// ------------------------------------
// The first version of this class took register offsets and handed them to
// `FANCONTROL::ReadByteFromEC` / `WriteByteToEC`, which speak the whole
// two-phase protocol themselves. It implemented `IIoBackend`, whose methods
// take *ports*. So `EcBus` performed a transaction, and each of its port
// operations started a second, nested transaction at the register level.
//
// The result would have been, for a single register read: EcBus writes 0x80 to
// port 0x1604, and the legacy layer turns that into a full transaction writing
// register 0x1604 & 0xFF = 0x04; EcBus writes 0x2F to port 0x1600, and the
// legacy layer writes register 0x00. Two arbitrary EC registers, on every
// read, on a machine whose register map is unverified.
//
// Nothing in the interface said so. Both layers type-checked, both layers were
// individually correct, and the tests passed because the fake backend
// implemented the same confusion. It was found by asking what number actually
// reached the primitive, which is why `trace()` exists.
//
// There is now exactly one implementation of the EC protocol in this codebase,
// and it is `EcBus`.
// ---------------------------------------------------------------------------

// The driver's port primitives, as plain callables.
//
// A read returns true on success and fills `out`; a write returns true on
// success. A false becomes a typed IoErrorCode at this boundary, so a boolean
// does not travel further than it has to.
struct LegacyPortPrimitives {
    std::function<bool(std::uint16_t port, std::uint8_t& out)> readPort;
    std::function<bool(std::uint16_t port, std::uint8_t value)> writePort;
};

class LegacyBackend final : public IIoBackend {
public:
    explicit LegacyBackend(LegacyPortPrimitives primitives);
    ~LegacyBackend() override = default;

    // Whether the underlying primitives are usable at all. The application
    // opens and closes the port driver over its lifetime, so a backend that
    // outlives the driver must report itself unavailable rather than call
    // through a closed one.
    void setDriverOpen(bool open) noexcept;
    bool driverOpen() const noexcept;

    // Every call made so far, in order. This is the evidence that a cycle
    // touched the hardware, and it is the only way to verify the property this
    // class was written to guarantee.
    std::vector<PortCall> trace() const;
    void clearTrace() noexcept;

    std::uint64_t readCount() const noexcept;
    std::uint64_t writeCount() const noexcept;

    BackendState state() const noexcept override;
    BackendCapabilities capabilities() const override;
    IoResult readPort(std::uint16_t port) override;
    IoResult writePort(std::uint16_t port, std::uint8_t value) override;
    void close() noexcept override;

private:
    LegacyPortPrimitives primitives_;
    bool driverOpen_ = false;
    std::vector<PortCall> trace_;
    std::uint64_t readCount_ = 0;
    std::uint64_t writeCount_ = 0;
};

} // namespace core
} // namespace tpfancontrol
