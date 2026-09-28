#pragma once

#include "io_backend.h"

#include <cstdint>
#include <functional>
#include <string>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// An IIoBackend over the legacy application's own EC primitives.
//
// T3-02. The legacy application reaches the EC through
// FANCONTROL::ReadByteFromEC and ::WriteByteToEC, which speak the two-phase
// protocol directly against the I/O ports through the WinIO driver. Rather than
// reimplement that (and end up with two implementations of the same protocol,
// which is how they diverge), this class adapts it.
//
// Two consequences worth stating plainly:
//
//  - It is portable code. The privileged part is the two callbacks the caller
//    supplies, so this class can be tested against fakes, which is what
//    legacy_backend_tests.cpp does. The thing it adapts cannot be tested on a
//    machine with no EC.
//
//  - It can be made read-only, and must be. Before Phase 0 a machine has no
//    verified register map, so a backend that can write is a way to damage the
//    embedded controller. A read-only backend fails every write with
//    IoErrorCode::Unsupported, which the controller reports as a backend
//    failure and therefore fails safe on.
// ---------------------------------------------------------------------------

// The legacy primitives, as plain callables.
//
// Return conventions, chosen to match the legacy code rather than to be tidy:
// a read returns true on success and fills `out`; a write returns true on
// success. Anything else is a failure, and the adapter turns it into a typed
// IoErrorCode rather than letting a bool travel any further.
struct LegacyEcPrimitives {
    std::function<bool(int offset, std::uint8_t& out)> readByte;
    std::function<bool(int offset, std::uint8_t value)> writeByte;
};

class LegacyBackend final : public IIoBackend {
public:
    explicit LegacyBackend(LegacyEcPrimitives primitives);
    ~LegacyBackend() override = default;

    // Makes every write fail. This is the state a machine is in before Phase 0
    // has verified its register map, and the state the application should be in
    // on any machine that has not been verified.
    //
    // Idempotent, and not reversible from here on purpose: a class that can be
    // read-only until someone flips a flag is a class whose safety depends on
    // nobody flipping the flag.
    void makeReadOnly() noexcept;
    bool readOnly() const noexcept;

    // Whether the underlying primitives are usable at all. The legacy
    // application opens and closes the port driver over its lifetime, so a
    // backend outliving the driver must report itself unavailable rather than
    // call through a closed one.
    void setDriverOpen(bool open) noexcept;
    bool driverOpen() const noexcept;

    // Calls made so far. Used by tests, and by the trace, to make a claim like
    // "this cycle wrote nothing" checkable rather than asserted.
    std::uint64_t readCount() const noexcept;
    std::uint64_t writeCount() const noexcept;
    std::uint64_t refusedWriteCount() const noexcept;

    BackendState state() const noexcept override;
    BackendCapabilities capabilities() const override;
    IoResult readPort(std::uint16_t port) override;
    IoResult writePort(std::uint16_t port, std::uint8_t value) override;
    void close() noexcept override;

private:
    LegacyEcPrimitives primitives_;
    bool readOnly_ = false;
    bool driverOpen_ = false;
    std::uint64_t readCount_ = 0;
    std::uint64_t writeCount_ = 0;
    std::uint64_t refusedWriteCount_ = 0;
};

} // namespace core
} // namespace tpfancontrol
