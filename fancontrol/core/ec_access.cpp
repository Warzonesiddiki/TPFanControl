#include "ec_access.h"

namespace tpfancontrol {
namespace core {

// ---------------------------------------------------------------------------
// EcBusConfig
// ---------------------------------------------------------------------------

bool EcBusConfig::commandEncodingIsUnambiguous() const noexcept
{
    return readCommand != writeCommand;
}

EcBusConfig::CommandKind EcBusConfig::classifyCommand(std::uint8_t command) const noexcept
{
    if (command == readCommand) {
        return CommandKind::Read;
    }
    if (command == writeCommand) {
        return CommandKind::Write;
    }
    return CommandKind::Unknown;
}

bool EcBusConfig::isSane() const noexcept
{
    return validationMessage().empty();
}

std::string EcBusConfig::validationMessage() const
{
    if (statusPort == dataPort) {
        return "EcBusConfig: statusPort and dataPort must differ, otherwise a command "
            "byte and a data byte are written to the same place";
    }
    if (timeoutMs == 0) {
        return "EcBusConfig: timeoutMs must be greater than zero or every wait would "
            "time out on its first poll";
    }
    if (pollIntervalMs == 0) {
        return "EcBusConfig: pollIntervalMs must be greater than zero or the poll loop "
            "would spin without ever advancing the clock";
    }
    if (maximumAttempts <= 0) {
        return "EcBusConfig: maximumAttempts must be positive or a failure could "
            "never be reported";
    }
    if (recoveryTimeoutMs == 0) {
        return "EcBusConfig: recoveryTimeoutMs must be greater than zero or a busy EC "
            "could never be recovered";
    }
    if (!commandEncodingIsUnambiguous()) {
        return "EcBusConfig: readCommand and writeCommand are both 0x"
            + std::to_string(readCommand)
            + ", so the EC cannot be told a read from a write and neither can this "
            "layer; they must differ";
    }
    return std::string();
}

} // namespace core
} // namespace tpfancontrol
