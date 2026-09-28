#include "io_backend.h"

namespace tpfancontrol {
namespace core {

const char* toText(IoErrorCode code) noexcept
{
    switch (code) {
    case IoErrorCode::None:          return "None";
    case IoErrorCode::NotInitialized: return "NotInitialized";
    case IoErrorCode::AccessDenied:  return "AccessDenied";
    case IoErrorCode::Timeout:       return "Timeout";
    case IoErrorCode::InvalidPort:   return "InvalidPort";
    case IoErrorCode::Disconnected:  return "Disconnected";
    case IoErrorCode::Unsupported:   return "Unsupported";
    case IoErrorCode::ReadFailure:   return "ReadFailure";
    case IoErrorCode::WriteFailure:  return "WriteFailure";
    }
    return "Unknown";
}

IoResult IoResult::success(std::uint8_t value)
{
    IoResult result;
    result.ok = true;
    result.error = IoErrorCode::None;
    result.value = value;
    return result;
}

IoResult IoResult::failure(IoErrorCode error, const std::string& message)
{
    IoResult result;
    result.ok = false;
    result.error = error;
    result.message = message;
    return result;
}

} // namespace core
} // namespace tpfancontrol
