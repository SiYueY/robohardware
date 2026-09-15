#pragma once

#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace hardware {

/**
 * @brief Represents either a successful value or an error.
 *
 * Result<T, E> is a lightweight value type for explicit error propagation in
 * code where exceptions are undesirable, such as low-level hardware access
 * and real-time execution paths.
 *
 * A Result is always in exactly one of two states:
 *
 *   - Value: contains an object of type T.
 *   - Error: contains an object of type E.
 *
 * T and E are stored directly inside the Result object. Result itself performs
 * no dynamic allocation, locking, system calls, logging, or string formatting.
 * State inspection and value/error access are constant-time operations.
 *
 * Result intentionally makes no assumptions about the meaning of E. The error
 * type may be a Linux errno wrapper, a device-specific enum, a protocol error,
 * or any other application-defined type.
 *
 * @par Real-time usage
 *
 * Result itself does not introduce non-real-time behavior. However, this does
 * not automatically make every Result<T, E> real-time compatible.
 *
 * RT fast-path APIs must ensure that the concrete T and E types also have
 * deterministic construction, movement, destruction, and access behavior, and
 * do not perform dynamic allocation, locking, blocking operations, exceptions,
 * logging, or other unbounded work.
 *
 * Typical RT-friendly instantiations include:
 *
 * @code
 * Result<std::size_t, serial::Error>
 * Result<can::Frame, can::Error>
 * Result<void, spi::Error>
 * @endcode
 *
 * @par Usage
 *
 * Create a successful result:
 *
 * @code
 * using ReadResult = Result<std::size_t, Error>;
 *
 * return ReadResult::success(bytes_read);
 * @endcode
 *
 * Create an error result:
 *
 * @code
 * return ReadResult::failure(Error::Io);
 * @endcode
 *
 * Inspect a result:
 *
 * @code
 * const auto result = port.read_some(buffer);
 *
 * if (!result) {
 *     handle_error(result.error());
 *     return;
 * }
 *
 * const std::size_t size = result.value();
 * @endcode
 *
 * value() may only be called when has_value() is true.
 * error() may only be called when has_value() is false.
 *
 * These functions do not throw. Violating their preconditions triggers an
 * assertion in builds where assertions are enabled.
 *
 * @tparam T Successful value type.
 * @tparam E Error type.
 */
template <typename T, typename E>
class [[nodiscard]] Result final {
    static_assert(!std::is_void_v<T>, "Use Result<void, E> for void success values");

    static_assert(std::is_object_v<T>, "T must be an object type");
    static_assert(std::is_object_v<E>, "E must be an object type");

    static_assert(!std::is_array_v<T>, "T must not be an array type");
    static_assert(!std::is_array_v<E>, "E must not be an array type");

    static_assert(std::is_nothrow_move_constructible_v<T>, "T must be nothrow move constructible");
    static_assert(std::is_nothrow_move_constructible_v<E>, "E must be nothrow move constructible");

    static_assert(std::is_nothrow_destructible_v<T>, "T must have a noexcept destructor");
    static_assert(std::is_nothrow_destructible_v<E>, "E must have a noexcept destructor");

public:
    using value_type = T;
    using error_type = E;

    /**
     * @brief Creates a successful result by copying a value.
     */
    template <typename U = T, std::enable_if_t<std::is_nothrow_copy_constructible_v<U>, int> = 0>
    [[nodiscard]] static Result success(const T& value) noexcept {
        return Result(ValueTag{}, value);
    }

    /**
     * @brief Creates a successful result by moving a value.
     */
    [[nodiscard]] static Result success(T&& value) noexcept {
        return Result(ValueTag{}, std::move(value));
    }

    /**
     * @brief Creates a failed result by copying an error.
     */
    template <typename G = E, std::enable_if_t<std::is_nothrow_copy_constructible_v<G>, int> = 0>
    [[nodiscard]] static Result failure(const E& error) noexcept {
        return Result(ErrorTag{}, error);
    }

    /**
     * @brief Creates a failed result by moving an error.
     */
    [[nodiscard]] static Result failure(E&& error) noexcept {
        return Result(ErrorTag{}, std::move(error));
    }

    /**
     * @brief Copy-constructs the currently active value or error.
     */
    Result(const Result&) = delete;

    /**
     * @brief Move-constructs the currently active value or error.
     *
     * The source Result remains in the same logical state but contains a
     * moved-from T or E, following the normal move semantics of that type.
     */
    Result(Result&& other) noexcept : state_(other.state_) {
        if (other.has_value()) {
            construct_value(std::move(other).value_unchecked());
        } else {
            construct_error(std::move(other).error_unchecked());
        }
    }

    /**
     * Assignment is intentionally unsupported in V1.
     *
     * Supporting assignment requires handling value->error and error->value
     * lifetime transitions and significantly increases implementation
     * complexity. Result is intended primarily as a function return value,
     * where construction and inspection are the common operations.
     */
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;

    /**
     * @brief Destroys the currently active value or error.
     */
    ~Result() noexcept { destroy_active(); }

    /**
     * @brief Returns true if this Result contains a successful value.
     */
    [[nodiscard]] constexpr bool has_value() const noexcept { return state_ == State::Value; }

    /**
     * @brief Equivalent to has_value().
     *
     * Enables the common pattern:
     *
     * @code
     * if (!result) {
     *     handle_error(result.error());
     * }
     * @endcode
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    /**
     * @brief Returns the contained value.
     *
     * @pre has_value() == true.
     *
     * This function does not throw. A violated precondition is treated as a
     * programming error and is checked with assert() when assertions are
     * enabled.
     */
    [[nodiscard]] T& value() & noexcept {
        assert(has_value());
        return value_unchecked();
    }

    /**
     * @copydoc value()
     */
    [[nodiscard]] const T& value() const& noexcept {
        assert(has_value());
        return value_unchecked();
    }

    /**
     * @brief Moves the contained value out of this Result.
     *
     * @pre has_value() == true.
     */
    [[nodiscard]] T&& value() && noexcept {
        assert(has_value());
        return std::move(value_unchecked());
    }

    /**
     * @brief Returns the contained error.
     *
     * @pre has_value() == false.
     */
    [[nodiscard]] E& error() & noexcept {
        assert(!has_value());
        return error_unchecked();
    }

    /**
     * @copydoc error()
     */
    [[nodiscard]] const E& error() const& noexcept {
        assert(!has_value());
        return error_unchecked();
    }

    /**
     * @brief Moves the contained error out of this Result.
     *
     * @pre has_value() == false.
     */
    [[nodiscard]] E&& error() && noexcept {
        assert(!has_value());
        return std::move(error_unchecked());
    }

private:
    // Tags make the private value/error constructors unambiguous even when
    // T and E are identical or implicitly convertible to one another.
    struct ValueTag final {};
    struct ErrorTag final {};

    enum class State : std::uint8_t {
        Value,
        Error,
    };

    /**
     * Storage for exactly one active object.
     *
     * The union keeps T and E inline and avoids dynamic allocation.
     * Result is responsible for explicitly constructing and destroying the
     * active member.
     */
    union Storage {
        unsigned char empty;
        T value;
        E error;

        constexpr Storage() noexcept : empty{0} {}

        // The active member is destroyed explicitly by Result.
        ~Storage() noexcept {}
    };

    Result(ValueTag, const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
    : state_(State::Value) {
        construct_value(value);
    }

    Result(ValueTag, T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
    : state_(State::Value) {
        construct_value(std::move(value));
    }

    Result(ErrorTag, const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
    : state_(State::Error) {
        construct_error(error);
    }

    Result(ErrorTag, E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
    : state_(State::Error) {
        construct_error(std::move(error));
    }

    template <typename U>
    void construct_value(U&& value) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
        // Placement new starts T's lifetime inside Result-owned storage.
        // It performs no dynamic memory allocation.
        ::new (static_cast<void*>(&storage_.value)) T(std::forward<U>(value));
    }

    template <typename G>
    void construct_error(G&& error) noexcept(std::is_nothrow_constructible_v<E, G&&>) {
        ::new (static_cast<void*>(&storage_.error)) E(std::forward<G>(error));
    }

    void destroy_active() noexcept {
        if (has_value()) {
            value_unchecked().~T();
        } else {
            error_unchecked().~E();
        }
    }

    [[nodiscard]] T& value_unchecked() noexcept { return *std::launder(&storage_.value); }

    [[nodiscard]] const T& value_unchecked() const noexcept {
        return *std::launder(&storage_.value);
    }

    [[nodiscard]] E& error_unchecked() noexcept { return *std::launder(&storage_.error); }

    [[nodiscard]] const E& error_unchecked() const noexcept {
        return *std::launder(&storage_.error);
    }

    Storage storage_;
    State state_;
};

/**
 * @brief Result specialization for operations with no success payload.
 *
 * Result<void, E> represents either:
 *
 *   - successful completion, or
 *   - an error of type E.
 *
 * Typical usage:
 *
 * @code
 * using OpenResult = Result<void, serial::Error>;
 *
 * if (fd < 0) {
 *     return OpenResult::failure(serial::Error::OpenFailed);
 * }
 *
 * return OpenResult::success();
 * @endcode
 *
 * The same real-time constraints as Result<T, E> apply.
 *
 * @tparam E Error type.
 */
template <typename E>
class [[nodiscard]] Result<void, E> final {
    static_assert(std::is_object_v<E>, "E must be an object type");

    static_assert(!std::is_array_v<E>, "E must not be an array type");

    static_assert(std::is_nothrow_move_constructible_v<E>, "E must be nothrow move constructible");

    static_assert(std::is_nothrow_destructible_v<E>, "E must have a noexcept destructor");

public:
    using value_type = void;
    using error_type = E;

    /**
     * @brief Creates a successful result.
     */
    [[nodiscard]] static Result success() noexcept { return Result(ValueTag{}); }

    /**
     * @brief Creates a failed result by copying an error.
     */
    template <typename G = E, std::enable_if_t<std::is_nothrow_copy_constructible_v<G>, int> = 0>
    [[nodiscard]] static Result failure(const E& error) noexcept {
        return Result(ErrorTag{}, error);
    }

    /**
     * @brief Creates a failed result by moving an error.
     */
    [[nodiscard]] static Result failure(E&& error) noexcept {
        return Result(ErrorTag{}, std::move(error));
    }

    Result(const Result&) = delete;

    Result(Result&& other) noexcept : state_(other.state_) {
        if (!other.has_value()) {
            construct_error(std::move(other).error_unchecked());
        }
    }

    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;

    ~Result() noexcept {
        if (!has_value()) {
            error_unchecked().~E();
        }
    }

    /**
     * @brief Returns true if the operation completed successfully.
     */
    [[nodiscard]] constexpr bool has_value() const noexcept { return state_ == State::Value; }

    /**
     * @brief Equivalent to has_value().
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    /**
     * @brief Returns the contained error.
     *
     * @pre has_value() == false.
     */
    [[nodiscard]] E& error() & noexcept {
        assert(!has_value());
        return error_unchecked();
    }

    /**
     * @copydoc error()
     */
    [[nodiscard]] const E& error() const& noexcept {
        assert(!has_value());
        return error_unchecked();
    }

    /**
     * @brief Moves the contained error out of this Result.
     *
     * @pre has_value() == false.
     */
    [[nodiscard]] E&& error() && noexcept {
        assert(!has_value());
        return std::move(error_unchecked());
    }

private:
    struct ValueTag final {};
    struct ErrorTag final {};

    enum class State : std::uint8_t {
        Value,
        Error,
    };

    union Storage {
        unsigned char empty;
        E error;

        constexpr Storage() noexcept : empty{0} {}

        ~Storage() noexcept {}
    };

    explicit Result(ValueTag) noexcept : state_(State::Value) {}

    Result(ErrorTag, const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
    : state_(State::Error) {
        construct_error(error);
    }

    Result(ErrorTag, E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
    : state_(State::Error) {
        construct_error(std::move(error));
    }

    template <typename G>
    void construct_error(G&& error) noexcept(std::is_nothrow_constructible_v<E, G&&>) {
        ::new (static_cast<void*>(&storage_.error)) E(std::forward<G>(error));
    }

    [[nodiscard]] E& error_unchecked() noexcept { return *std::launder(&storage_.error); }

    [[nodiscard]] const E& error_unchecked() const noexcept {
        return *std::launder(&storage_.error);
    }

    Storage storage_;
    State state_;
};

}  // namespace hardware
