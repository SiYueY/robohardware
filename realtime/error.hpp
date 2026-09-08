#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace realtime {

enum class ErrorCode {
    InvalidArgument,
    PermissionDenied,
    SchedulingFailed,
    AffinityFailed,
    MemoryLockFailed,
    ThreadCreateFailed,
    ClockError,
    InvalidState,
};

enum class NativeErrorDomain : std::uint8_t { None, Errno, Pthread };

struct Error {
    ErrorCode code;
    NativeErrorDomain native_domain{NativeErrorDomain::None};
    int native_code{0};
};

template <typename T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(error) {}
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
    Result(Error error) noexcept : error_(error) {}
    explicit operator bool() const noexcept { return !error_.has_value(); }
    bool ok() const noexcept { return static_cast<bool>(*this); }
    const Error& error() const noexcept { return *error_; }

private:
    std::optional<Error> error_;
};

}  // namespace realtime
