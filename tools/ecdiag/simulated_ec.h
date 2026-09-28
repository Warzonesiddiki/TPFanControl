#pragma once

#include "ec_access.h"

#include <cstdint>
#include <map>

namespace tpfancontrol {
namespace ecdiag {

// ---------------------------------------------------------------------------
// A simulated register source for `ecdiag --simulate`
// ---------------------------------------------------------------------------
//
// This is not a backend and deliberately not an IIoBackend. It implements the
// reader seam directly, which is all the diagnostic engine can use, so the
// simulator cannot be handed to anything that writes - it has no port methods
// to be given.
//
// It exists so that the tool's argument parsing, plan handling, report writing
// and exit codes can be exercised end to end without hardware, and so that
// someone about to lean over a laptop can see the shape of the report first.
//
// Everything it returns is invented. Every report it produces is stamped
// `"grade": "simulated"` with a reason a machine can read, and the text form
// prints SIMULATED above the values. A simulated run is a contract check. It is
// not evidence, and no part of this repository may present it as such.
class SimulatedEc final : public core::IRegisterReader {
public:
    SimulatedEc();

    core::IoResult readRegister(std::uint8_t address) override;
    core::ReaderAudit audit() const override;

    // The invented value table, so a caller could inspect it. There is no
    // setter: nothing should be able to make the simulator look like a
    // measurement of something.
    const std::map<std::uint8_t, std::uint8_t>& registers() const noexcept;

private:
    std::map<std::uint8_t, std::uint8_t> registers_;
    core::ReaderAudit audit_;
};

} // namespace ecdiag
} // namespace tpfancontrol
