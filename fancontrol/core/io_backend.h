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

// A stable name for an error code, so a report or a log can carry the
// classification rather than only a sentence about it.
const char* toText(IoErrorCode code) noexcept;

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

// ---------------------------------------------------------------------------
// One call to the privileged I/O primitive, recorded for evidence.
//
// The EC transaction is a sequence of port writes and reads, and a claim like
// "this cycle wrote nothing" is only checkable if the calls are recorded.
// Without a trace, the only way to find out what reached the hardware is to
// read the hardware.
//
// It lives here rather than beside one backend because it is a property of the
// port layer, not of the legacy application: any backend that talks to real
// ports has to be able to answer "what did you do to the machine?".
// ---------------------------------------------------------------------------
struct PortCall {
    bool isWrite = false;
    std::uint16_t port = 0;
    std::uint8_t value = 0;
    bool ok = false;
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
