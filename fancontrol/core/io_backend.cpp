#include "io_backend.h"

namespace tpfancontrol {
namespace core {

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
