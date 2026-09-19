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
 *   - Success: contains an object of type T.
 *   - Error: contains an object of type E.
 *
 * T and E are stored directly inside the Result object. Result itself performs
 * no dynamic allocation, locking, system calls, logging, or string formatting.
 * State inspection and value/error access are constant-time operations.
 *
 * Result intentionally makes no assumptions about the meaning of E. The error
 * type has no semantic requirements beyond this class's compile-time type
 * constraints. Module APIs define their own error types and meanings.
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
 * Result<std::size_t, ModuleError>
 * Result<void, ModuleError>
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
 * const auto result = read_device();
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
     *
     * This overload participates only when T is nothrow copy constructible.
     *
     * @param value Value to copy into the success state.
     * @return A Result in the success state.
     */
    template <
        typename U = T,
        std::enable_if_t<std::is_same_v<U, T> && std::is_nothrow_copy_constructible_v<T>, int> = 0>
    [[nodiscard]] static Result success(const T& value) noexcept {
        return Result(SuccessTag{}, value);
    }

    /**
     * @brief Creates a successful result by moving a value.
     *
     * @param value Value to move into the success state.
     * @return A Result in the success state.
     */
    [[nodiscard]] static Result success(T&& value) noexcept {
        return Result(SuccessTag{}, std::move(value));
    }

    /**
     * @brief Creates a failed result by copying an error.
     *
     * This overload participates only when E is nothrow copy constructible.
     *
     * @param error Error to copy into the error state.
     * @return A Result in the error state.
     */
    template <
        typename U = E,
        std::enable_if_t<std::is_same_v<U, E> && std::is_nothrow_copy_constructible_v<E>, int> = 0>
    [[nodiscard]] static Result failure(const E& error) noexcept {
        return Result(ErrorTag{}, error);
    }

    /**
     * @brief Creates a failed result by moving an error.
     *
     * @param error Error to move into the error state.
     * @return A Result in the error state.
     */
    [[nodiscard]] static Result failure(E&& error) noexcept {
        return Result(ErrorTag{}, std::move(error));
    }

    /**
     * @brief Copy construction is intentionally unsupported.
     */
    Result(const Result&) = delete;

    /**
     * @brief Move-constructs the currently active value or error.
     *
     * The source Result remains in the same logical state but contains a
     * moved-from T or E, following the normal move semantics of that type.
     *
     * @param other Result from which to move the active payload.
     */
    Result(Result&& other) noexcept : state_(other.state_) {
        if (other.has_value()) {
            construct_value(std::move(other.value_unchecked()));
        } else {
            construct_error(std::move(other.error_unchecked()));
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
     *
     * @return true when this Result is in the success state; otherwise false.
     */
    [[nodiscard]] constexpr bool has_value() const noexcept { return state_ == State::Success; }

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
     *
     * @return A reference to the successful value.
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
     * @return An rvalue reference to the successful value.
     */
    [[nodiscard]] T&& value() && noexcept {
        assert(has_value());
        return std::move(value_unchecked());
    }

    /**
     * @brief Returns the contained error.
     *
     * @pre has_value() == false.
     * @return A reference to the error.
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
     * @return An rvalue reference to the error.
     */
    [[nodiscard]] E&& error() && noexcept {
        assert(!has_value());
        return std::move(error_unchecked());
    }

private:
    // Tags make the private value/error constructors unambiguous even when
    // T and E are identical or implicitly convertible to one another.
    struct SuccessTag final {};
    struct ErrorTag final {};

    enum class State : std::uint8_t {
        Success,
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
        unsigned char dummy;
        T value;
        E error;

        constexpr Storage() noexcept : dummy{0} {}

        // The active member is destroyed explicitly by Result.
        ~Storage() noexcept {}
    };

    Result(SuccessTag, const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
    : state_(State::Success) {
        construct_value(value);
    }

    Result(SuccessTag, T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
    : state_(State::Success) {
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

    template <typename U>
    void construct_error(U&& error) noexcept(std::is_nothrow_constructible_v<E, U&&>) {
        ::new (static_cast<void*>(&storage_.error)) E(std::forward<U>(error));
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
 * using OperationResult = Result<void, ModuleError>;
 *
 * if (operation_failed()) {
 *     return OperationResult::failure(ModuleError::Failure);
 * }
 *
 * return OperationResult::success();
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
     *
     * @return A Result in the success state.
     */
    [[nodiscard]] static Result success() noexcept { return Result(SuccessTag{}); }

    /**
     * @brief Creates a failed result by copying an error.
     *
     * This overload participates only when E is nothrow copy constructible.
     *
     * @param error Error to copy into the error state.
     * @return A Result in the error state.
     */
    template <
        typename U = E,
        std::enable_if_t<std::is_same_v<U, E> && std::is_nothrow_copy_constructible_v<E>, int> = 0>
    [[nodiscard]] static Result failure(const E& error) noexcept {
        return Result(ErrorTag{}, error);
    }

    /**
     * @brief Creates a failed result by moving an error.
     *
     * @param error Error to move into the error state.
     * @return A Result in the error state.
     */
    [[nodiscard]] static Result failure(E&& error) noexcept {
        return Result(ErrorTag{}, std::move(error));
    }

    /** @brief Copy construction is intentionally unsupported. */
    Result(const Result&) = delete;

    /**
     * @brief Moves the error payload while preserving the source logical state.
     *
     * @param other Result from which to move the active error.
     */
    Result(Result&& other) noexcept : state_(other.state_) {
        if (!other.has_value()) {
            construct_error(std::move(other.error_unchecked()));
        }
    }

    /** @brief Copy assignment is intentionally unsupported. */
    Result& operator=(const Result&) = delete;
    /** @brief Move assignment is intentionally unsupported. */
    Result& operator=(Result&&) = delete;

    /** @brief Destroys the active error, if any. */
    ~Result() noexcept { destroy_active(); }

    /**
     * @brief Returns true if the operation completed successfully.
     *
     * @return true when this Result is in the success state; otherwise false.
     */
    [[nodiscard]] constexpr bool has_value() const noexcept { return state_ == State::Success; }

    /**
     * @brief Equivalent to has_value().
     */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return has_value(); }

    /**
     * @brief Returns the contained error.
     *
     * @pre has_value() == false.
     * @return A reference to the error.
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
     * @return An rvalue reference to the error.
     */
    [[nodiscard]] E&& error() && noexcept {
        assert(!has_value());
        return std::move(error_unchecked());
    }

private:
    struct SuccessTag final {};
    struct ErrorTag final {};

    enum class State : std::uint8_t {
        Success,
        Error,
    };

    union Storage {
        unsigned char dummy;
        E error;

        constexpr Storage() noexcept : dummy{0} {}

        ~Storage() noexcept {}
    };

    explicit Result(SuccessTag) noexcept : state_(State::Success) {}

    Result(ErrorTag, const E& error) noexcept(std::is_nothrow_copy_constructible_v<E>)
    : state_(State::Error) {
        construct_error(error);
    }

    Result(ErrorTag, E&& error) noexcept(std::is_nothrow_move_constructible_v<E>)
    : state_(State::Error) {
        construct_error(std::move(error));
    }

    template <typename U>
    void construct_error(U&& error) noexcept(std::is_nothrow_constructible_v<E, U&&>) {
        ::new (static_cast<void*>(&storage_.error)) E(std::forward<U>(error));
    }

    void destroy_active() noexcept {
        if (!has_value()) error_unchecked().~E();
    }

    [[nodiscard]] E& error_unchecked() noexcept { return *std::launder(&storage_.error); }

    [[nodiscard]] const E& error_unchecked() const noexcept {
        return *std::launder(&storage_.error);
    }

    Storage storage_;
    State state_;
};

}  // namespace hardware
