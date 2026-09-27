#include "core_types.h"

namespace tpfancontrol {
namespace core {

FanCommand FanCommand::none() noexcept
{
    return FanCommand{CommandKind::None, -1};
}

FanCommand FanCommand::levelCommand(int value) noexcept
{
    return FanCommand{CommandKind::Level, value};
}

FanCommand FanCommand::biosAutomatic() noexcept
{
    return FanCommand{CommandKind::BiosAutomatic, -1};
}

bool operator==(const FanCommand& left, const FanCommand& right) noexcept
{
    return left.kind == right.kind && left.level == right.level;
}

bool operator!=(const FanCommand& left, const FanCommand& right) noexcept
{
    return !(left == right);
}

int commandRank(const FanCommand& command) noexcept
{
    switch (command.kind) {
    case CommandKind::None:
        return -1;
    case CommandKind::Level:
        return command.level;
    case CommandKind::BiosAutomatic:
        // BIOS is an explicit terminal command, not a numeric fan level.
        return 1000000;
    }

    return -1;
}

} // namespace core
} // namespace tpfancontrol
