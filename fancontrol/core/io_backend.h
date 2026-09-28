#pragma once

#include <cstdint>
#include <string>

namespace tpfancontrol {
namespace core {

enum class BackendState {
    Unavailable,
    Ready,
    Faulted,
    Stopped
};

enum class IoErrorCode {
    None,
    NotInitialized,
    AccessDenied,
    Timeout,
    InvalidPort,
    Disconnected,
    Unsupported,
    ReadFailure,
    WriteFailure
};

struct BackendCapabilities {
    std::string name;
    std::string version;
    bool canReadPorts = false;
    bool canWritePorts = false;
    bool canReadMsr = false;
    bool signedAndApproved = false;
};

struct IoResult {
    bool ok = false;
    IoErrorCode error = IoErrorCode::NotInitialized;
    std::uint8_t value = 0;
    std::string message;

    static IoResult success(std::uint8_t value = 0);
    static IoResult failure(IoErrorCode error, const std::string& message);
};

// The interface describes privileged I/O only. It intentionally knows nothing
// about EC offsets, fan levels, profiles, or temperature curves.
class IIoBackend {
public:
    virtual ~IIoBackend() = default;

    virtual BackendState state() const noexcept = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual IoResult readPort(std::uint16_t port) = 0;
    virtual IoResult writePort(std::uint16_t port, std::uint8_t value) = 0;
    virtual void close() noexcept = 0;
};

} // namespace core
} // namespace tpfancontrol
