#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace canopen {
enum class ErrorCode : std::uint8_t {
    InvalidArgument,
    InvalidNodeId,
    InvalidState,
    InvalidCobId,
    InvalidPdoMapping,
    UnsupportedTransmissionType,
    InvalidObject,
    TransportError,
    Timeout,
    SdoAbort,
    SdoBusy,
    InvalidSdoResponse,
    NmtError,
    HeartbeatTimeout,
    PdoDecodeError,
    PdoEncodeError,
    SyncError
};
struct Error {
    ErrorCode code{ErrorCode::InvalidState};
    std::uint8_t node_id{0};
    std::uint16_t index{0};
    std::uint8_t subindex{0};
    std::uint32_t protocol_code{0};
};
template <class T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error e) noexcept : error_(e) {}
    explicit operator bool() const noexcept { return value_.has_value(); }
    bool ok() const noexcept { return static_cast<bool>(*this); }
    T& value() noexcept { return *value_; }
    const T& value() const noexcept { return *value_; }
    const Error& error() const noexcept { return *error_; }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};
template <>
class Result<void> {
public:
    Result() noexcept = default;
    Result(Error e) noexcept : error_(e) {}
    explicit operator bool() const noexcept { return !error_; }
    bool ok() const noexcept { return static_cast<bool>(*this); }
    const Error& error() const noexcept { return *error_; }

private:
    std::optional<Error> error_;
};
}  // namespace canopen
