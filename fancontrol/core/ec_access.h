#pragma once

#include "io_backend.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// EC access: the configuration, and the read-only view of it
// ---------------------------------------------------------------------------
//
// This header exists so that one guarantee is structural rather than a
// convention someone has to remember.
//
// `ec_protocol.h` owns the EC *transactions*. It can write a register, so a
// translation unit that includes it can, in principle, name a write. The
// diagnostic tool (T5-09) must not be able to, and "do not call writeRegister"
// is precisely the kind of rule that EC_REGISTER_MAP.md section 10 shows people
// can read in full and still break.
//
// So the bus configuration and the read-only seam live here, in a header with
// no write operation anywhere in it, and `ec_protocol.h` includes this one
// rather than the other way round. `ecdiag` includes this header and
// `io_backend.h` and nothing else, which is what makes its read-only-ness a
// fact about the code rather than a promise about it.
//
// See docs/ECDIAG.md and ADR-024.

// ---------------------------------------------------------------------------
// Bus configuration
// ---------------------------------------------------------------------------
//
// The defaults here are transcribed from the legacy application's own EC
// layer, `fancontrol/portio.cpp`, which is the only in-repo record of how this
// machine family is actually addressed:
//
//     EC_CTRLPORT  0x1604      EC_DATAPORT  0x1600
//     EC_STAT_OBF  0x01        EC_STAT_IBF  0x02
//     EC_CTRLPORT_READ  0x80   EC_CTRLPORT_WRITE  0x81
//
// That makes them better evidence than a guess, but it is still evidence of
// WHAT THE SHIPPED APPLICATION DOES, not proof that it is correct on any
// particular machine. Generic EC documentation describes a 0x62/0x66 interface
// instead, and EC_REGISTER_MAP.md 3 records both. Phase 0 must confirm the
// mapping read-only before any write is issued.
struct EcBusConfig {
    std::uint16_t statusPort = 0x1604;
    std::uint16_t dataPort = 0x1600;

    // Status-register bits: input buffer full and output buffer full.
    std::uint8_t ibfMask = 0x02;
    std::uint8_t obfMask = 0x01;

    // These are OPERATION CODES, not a base to be OR'd with an address.
    //
    // The command byte says only whether the following data-port byte is a
    // register number to read from or a register number to write to. The
    // address itself is a full byte, written to the data port in a second
    // step, which is why this layer can reach registers such as 0x31 and 0x84.
    //
    // An earlier design encoded the address into the command byte instead, which
    // left only four addressable registers and contradicted every call site in
    // the legacy source. See ADR-020.
    std::uint8_t readCommand = 0x80;
    std::uint8_t writeCommand = 0x81;

    // Bounded by construction. Every wait is capped by timeoutMs AND by a
    // derived poll-count ceiling, so a clock that fails to advance still
    // terminates the loop instead of spinning.
    std::uint64_t timeoutMs = 50;
    std::uint64_t pollIntervalMs = 1;
    int maximumAttempts = 3;

    // Bounded by construction: the whole pre-transaction recovery wait, which
    // the legacy code allows a full second.
    std::uint64_t recoveryTimeoutMs = 100;

    // Rejects a configuration that could loop unboundedly or could not tell a
    // read from a write. A zero timeout, a zero poll interval, a non-positive
    // attempt count, identical ports, and an ambiguous command encoding are all
    // treated as configuration errors rather than silently normalised.
    bool isSane() const noexcept;
    std::string validationMessage() const;

    // True when the read and write operations are distinguishable at all.
    bool commandEncodingIsUnambiguous() const noexcept;

    // Classifies a command byte as a read or a write, or neither.
    enum class CommandKind { Unknown, Read, Write };
    CommandKind classifyCommand(std::uint8_t command) const noexcept;
};


// ---------------------------------------------------------------------------
// The read-only seam
// ---------------------------------------------------------------------------

// What a reader did, reported by the reader itself rather than inferred by the
// caller.
//
// The distinction matters more than it looks. `readsIssued` is the reader's own
// count of the calls it received, so it can disagree with the caller's plan -
// which is exactly what makes it worth comparing. The port write vectors are
// the bytes that reached the port primitives, as recorded by the layer that
// issued them. "We tried" and "it reached the port" are different claims, and
// only the second one is a claim about the machine.
struct ReaderAudit {
    std::uint64_t readsIssued = 0;

    // The bus's own counters, when the reader is bus-backed. Zero when there is
    // no bus (a simulated reader), which is the truth rather than an omission.
    std::uint64_t transactionCount = 0;
    std::uint64_t timeoutCount = 0;

    // Every value successfully written to the status port, in order. A read
    // contributes the read command byte; a write would contribute the write
    // command byte. This vector is how a caller can tell the difference.
    std::vector<std::uint8_t> statusPortWriteValues;

    // Every value successfully written to the data port, in order. A read
    // contributes the register address; a write contributes the address and
    // then the value.
    std::vector<std::uint8_t> dataPortWriteValues;
};

// The only EC access a read-only consumer is allowed to perform.
//
// It has no write member, and that is the point: a diagnostic cannot write a
// register because there is nothing here to call. Adding a write to this
// interface would be visible as a change to the contract rather than as one
// more line in an implementation file.
class IRegisterReader {
public:
    virtual ~IRegisterReader() = default;

    virtual IoResult readRegister(std::uint8_t address) = 0;

    // The reader's account of what it did. A reader could return an optimistic
    // constant - it is code - but the tests drive a real EcBus and read the
    // bus's own trace, so a reader that lied would have to lie somewhere the
    // tests can see.
    virtual ReaderAudit audit() const = 0;
};

} // namespace core
} // namespace tpfancontrol
