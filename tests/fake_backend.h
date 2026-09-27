#pragma once

#include "../fancontrol/core/io_backend.h"

#include <cstdint>
#include <map>
#include <vector>

namespace tpfancontrol {
namespace test {

struct FakeOperation {
    bool write = false;
    std::uint16_t port = 0;
    std::uint8_t value = 0;
};

class FakeBackend final : public core::IIoBackend {
public:
    FakeBackend();

    core::BackendState state() const noexcept override;
    core::BackendCapabilities capabilities() const override;
    core::IoResult readPort(std::uint16_t port) override;
    core::IoResult writePort(std::uint16_t port, std::uint8_t value) override;
    void close() noexcept override;

    void setReadFailure(bool enabled) noexcept;
    void setWriteFailure(bool enabled) noexcept;
    void setValue(std::uint16_t port, std::uint8_t value);
    const std::vector<FakeOperation>& operations() const noexcept;

private:
    core::BackendState state_;
    core::BackendCapabilities capabilities_;
    bool readFailure_;
    bool writeFailure_;
    std::map<std::uint16_t, std::uint8_t> values_;
    std::vector<FakeOperation> operations_;
};

} // namespace test
} // namespace tpfancontrol
