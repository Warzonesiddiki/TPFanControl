#include "simulated_ec.h"

namespace tpfancontrol {
namespace ecdiag {

namespace {

// Invented values. They are chosen to look like a machine that is idling - a
// fan command of 0x00, a tachometer reading, plausible temperatures - so that
// the report is readable. They are NOT copied from any ThinkPad, any other
// tool, or any earlier measurement, because a simulated value that happened to
// match a real machine would be the most misleading kind of invented number.
std::map<std::uint8_t, std::uint8_t> inventedRegisters()
{
    std::map<std::uint8_t, std::uint8_t> registers;
    registers[0x2F] = 0x00;          // "the fan command register says 0"
    registers[0x31] = 0x00;
    registers[0x84] = 0x24;          // low byte of a made-up tachometer value
    registers[0x85] = 0x01;
    for (std::uint8_t address = 0x78; address <= 0x7F; ++address) {
        registers[address] = static_cast<std::uint8_t>(0x28 + (address - 0x78));
    }
    for (std::uint8_t address = 0xC0; address <= 0xC3; ++address) {
        registers[address] = static_cast<std::uint8_t>(0x30 + (address - 0xC0));
    }
    return registers;
}

} // namespace

SimulatedEc::SimulatedEc() : registers_(inventedRegisters())
{
}

core::IoResult SimulatedEc::readRegister(std::uint8_t address)
{
    ++audit_.readsIssued;
    const std::map<std::uint8_t, std::uint8_t>::const_iterator it = registers_.find(address);
    if (it == registers_.end()) {
        // A failure, not a zero. An offset this simulator was not given a value
        // for must not come back as a plausible-looking idling reading: "we had
        // no value" and "this offset holds zero" are different answers, and an
        // invented number that cannot be told apart from a real one is the most
        // misleading kind of invented number.
        return core::IoResult::failure(core::IoErrorCode::Unsupported,
            "the simulated reader has no invented value for this offset");
    }
    return core::IoResult::success(it->second);
}

core::ReaderAudit SimulatedEc::audit() const
{
    // No port writes are reported, because there are no ports: this reader does
    // not implement IIoBackend and cannot reach one. The read-only invariant is
    // therefore checked against an empty history, which is the truth for a
    // simulated run and the reason a simulated run proves nothing about the
    // real read path's port traffic.
    return audit_;
}

const std::map<std::uint8_t, std::uint8_t>& SimulatedEc::registers() const noexcept
{
    return registers_;
}

} // namespace ecdiag
} // namespace tpfancontrol
