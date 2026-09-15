#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
namespace can {
struct ClassicalFrame final {
    std::uint32_t id{};
    std::array<std::byte, 8> data{};
    std::uint8_t size{};
};
struct FdFrame final {
    std::uint32_t id{};
    std::array<std::byte, 64> data{};
    std::uint8_t size{};
    std::uint8_t flags{};
};
struct ErrorFrame final {
    std::uint32_t flags{};
    std::array<std::byte, 8> data{};
};
class ReceivedFrame final {
public:
    enum class Kind : std::uint8_t { Classical, Fd, Error };
    [[nodiscard]] static ReceivedFrame classical(ClassicalFrame value) noexcept {
        return ReceivedFrame{Kind::Classical, value, {}, {}};
    }
    [[nodiscard]] static ReceivedFrame fd(FdFrame value) noexcept {
        return ReceivedFrame{Kind::Fd, {}, value, {}};
    }
    [[nodiscard]] static ReceivedFrame error(ErrorFrame value) noexcept {
        return ReceivedFrame{Kind::Error, {}, {}, value};
    }
    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] const ClassicalFrame& classical() const noexcept {
        assert(kind_ == Kind::Classical);
        return classical_;
    }
    [[nodiscard]] const FdFrame& fd() const noexcept {
        assert(kind_ == Kind::Fd);
        return fd_;
    }
    [[nodiscard]] const ErrorFrame& error() const noexcept {
        assert(kind_ == Kind::Error);
        return error_;
    }

private:
    ReceivedFrame(Kind kind, ClassicalFrame classical, FdFrame fd, ErrorFrame error) noexcept
    : kind_(kind), classical_(classical), fd_(fd), error_(error) {}
    Kind kind_;
    ClassicalFrame classical_{};
    FdFrame fd_{};
    ErrorFrame error_{};
};
}  // namespace can
