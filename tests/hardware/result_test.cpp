#include <hardware/result.hpp>

#include <cassert>
#include <type_traits>
#include <utility>

namespace {

enum class TestError { Failure };

struct MoveOnly final {
    explicit MoveOnly(int n) noexcept : value(n) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    ~MoveOnly() noexcept = default;

    int value;
};

struct MoveOnlyError final {
    explicit MoveOnlyError(int n) noexcept : value(n) {}
    MoveOnlyError(const MoveOnlyError&) = delete;
    MoveOnlyError(MoveOnlyError&&) noexcept = default;
    ~MoveOnlyError() noexcept = default;

    int value;
};

struct Tracked final {
    static int copies;
    static int moves;

    explicit Tracked(int n) noexcept : value(n) {}
    Tracked(const Tracked& other) noexcept : value(other.value) { ++copies; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++moves; }
    ~Tracked() noexcept = default;

    int value;
};

int Tracked::copies = 0;
int Tracked::moves = 0;

struct LifetimeTracked final {
    static int alive;

    explicit LifetimeTracked(int n) noexcept : value(n) { ++alive; }
    LifetimeTracked(const LifetimeTracked& other) noexcept : value(other.value) { ++alive; }
    LifetimeTracked(LifetimeTracked&& other) noexcept : value(other.value) { ++alive; }
    ~LifetimeTracked() noexcept { --alive; }

    int value;
};

int LifetimeTracked::alive = 0;

struct ThrowingCopy final {
    ThrowingCopy() noexcept = default;
    ThrowingCopy(const ThrowingCopy&) noexcept(false) {}
    ThrowingCopy(ThrowingCopy&&) noexcept = default;
    ~ThrowingCopy() noexcept = default;
};

template <typename R, typename Argument, typename = void>
struct has_success_factory : std::false_type {};

template <typename R, typename Argument>
struct has_success_factory<R, Argument, std::void_t<decltype(R::success(std::declval<Argument>()))>>
: std::true_type {};

template <typename R, typename Argument, typename = void>
struct has_failure_factory : std::false_type {};

template <typename R, typename Argument>
struct has_failure_factory<R, Argument, std::void_t<decltype(R::failure(std::declval<Argument>()))>>
: std::true_type {};

template <typename R, typename U, typename = void>
struct has_explicit_success_copy_factory : std::false_type {};

template <typename R, typename U>
struct has_explicit_success_copy_factory<
    R, U,
    std::void_t<decltype(R::template success<U>(std::declval<const typename R::value_type&>()))>>
: std::true_type {};

template <typename R, typename U, typename = void>
struct has_explicit_failure_copy_factory : std::false_type {};

template <typename R, typename U>
struct has_explicit_failure_copy_factory<
    R, U,
    std::void_t<decltype(R::template failure<U>(std::declval<const typename R::error_type&>()))>>
: std::true_type {};

using IntResult = hardware::Result<int, TestError>;
using VoidResult = hardware::Result<void, TestError>;
using ThrowingValueResult = hardware::Result<ThrowingCopy, TestError>;
using ThrowingErrorResult = hardware::Result<int, ThrowingCopy>;
using ThrowingVoidErrorResult = hardware::Result<void, ThrowingCopy>;

static_assert(!std::is_default_constructible_v<IntResult>);
static_assert(!std::is_copy_constructible_v<IntResult>);
static_assert(!std::is_copy_assignable_v<IntResult>);
static_assert(std::is_nothrow_move_constructible_v<IntResult>);
static_assert(!std::is_move_assignable_v<IntResult>);

static_assert(!std::is_default_constructible_v<VoidResult>);
static_assert(!std::is_copy_constructible_v<VoidResult>);
static_assert(!std::is_copy_assignable_v<VoidResult>);
static_assert(std::is_nothrow_move_constructible_v<VoidResult>);
static_assert(!std::is_move_assignable_v<VoidResult>);

static_assert(std::is_same_v<decltype(std::declval<IntResult&>().value()), int&>);
static_assert(std::is_same_v<decltype(std::declval<const IntResult&>().value()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<IntResult&&>().value()), int&&>);
static_assert(std::is_same_v<decltype(std::declval<IntResult&>().error()), TestError&>);
static_assert(std::is_same_v<decltype(std::declval<const IntResult&>().error()), const TestError&>);
static_assert(std::is_same_v<decltype(std::declval<IntResult&&>().error()), TestError&&>);

static_assert(!has_success_factory<ThrowingValueResult, const ThrowingCopy&>::value);
static_assert(has_success_factory<ThrowingValueResult, ThrowingCopy&&>::value);
static_assert(!has_failure_factory<ThrowingErrorResult, const ThrowingCopy&>::value);
static_assert(has_failure_factory<ThrowingErrorResult, ThrowingCopy&&>::value);
static_assert(!has_failure_factory<ThrowingVoidErrorResult, const ThrowingCopy&>::value);
static_assert(has_failure_factory<ThrowingVoidErrorResult, ThrowingCopy&&>::value);
static_assert(!has_explicit_success_copy_factory<ThrowingValueResult, int>::value);
static_assert(!has_explicit_failure_copy_factory<ThrowingErrorResult, int>::value);
static_assert(!has_explicit_failure_copy_factory<ThrowingVoidErrorResult, int>::value);

}  // namespace

int main() {
    {
        auto value = IntResult::success(7);
        assert(value && value.value() == 7);
        auto error = IntResult::failure(TestError::Failure);
        assert(!error && error.error() == TestError::Failure);
    }

    {
        auto source = hardware::Result<MoveOnly, TestError>::success(MoveOnly{42});
        auto destination = std::move(source);
        assert(destination && destination.value().value == 42);
        assert(source.has_value());
    }

    {
        auto source = hardware::Result<int, MoveOnlyError>::failure(MoveOnlyError{42});
        auto destination = std::move(source);
        assert(!destination && destination.error().value == 42);
        assert(!source.has_value());
    }

    {
        auto source = hardware::Result<void, MoveOnlyError>::failure(MoveOnlyError{42});
        auto destination = std::move(source);
        assert(!destination && destination.error().value == 42);
        assert(!source.has_value());
    }

    {
        Tracked::copies = 0;
        Tracked::moves = 0;
        auto source = hardware::Result<Tracked, TestError>::success(Tracked{42});
        Tracked::copies = 0;
        Tracked::moves = 0;
        auto destination = std::move(source);
        assert(destination.value().value == 42);
        assert(Tracked::copies == 0);
        assert(Tracked::moves == 1);
        assert(source.has_value());
    }

    {
        Tracked::copies = 0;
        Tracked::moves = 0;
        auto source = hardware::Result<int, Tracked>::failure(Tracked{42});
        Tracked::copies = 0;
        Tracked::moves = 0;
        auto destination = std::move(source);
        assert(destination.error().value == 42);
        assert(Tracked::copies == 0);
        assert(Tracked::moves == 1);
        assert(!source.has_value());
    }

    {
        LifetimeTracked::alive = 0;
        {
            auto source = hardware::Result<LifetimeTracked, TestError>::success(LifetimeTracked{1});
            auto destination = std::move(source);
            assert(LifetimeTracked::alive == 2);
            assert(destination.has_value());
        }
        assert(LifetimeTracked::alive == 0);
    }

    {
        LifetimeTracked::alive = 0;
        {
            auto source = hardware::Result<int, LifetimeTracked>::failure(LifetimeTracked{1});
            auto destination = std::move(source);
            assert(LifetimeTracked::alive == 2);
            assert(!destination.has_value());
        }
        assert(LifetimeTracked::alive == 0);
    }

    {
        LifetimeTracked::alive = 0;
        {
            auto source = hardware::Result<void, LifetimeTracked>::failure(LifetimeTracked{1});
            auto destination = std::move(source);
            assert(LifetimeTracked::alive == 2);
            assert(!destination.has_value());
        }
        assert(LifetimeTracked::alive == 0);
    }

    {
        using SameTypeResult = hardware::Result<int, int>;
        auto value = SameTypeResult::success(1);
        auto error = SameTypeResult::failure(2);
        assert(value && value.value() == 1);
        assert(!error && error.error() == 2);
        auto moved = std::move(error);
        assert(!moved && moved.error() == 2);
        assert(!error.has_value());
    }

    {
        auto source = VoidResult::success();
        auto destination = std::move(source);
        assert(source.has_value());
        assert(destination.has_value());
    }
}
