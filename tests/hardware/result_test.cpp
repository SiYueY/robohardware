#include <hardware/result.hpp>

#include <cassert>
#include <memory>
#include <type_traits>

enum class TestError { Failure };
struct MoveOnly final {
    explicit MoveOnly(int n) noexcept : value(n) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    ~MoveOnly() noexcept = default;
    int value;
};
static_assert(!std::is_copy_constructible_v<hardware::Result<MoveOnly, TestError>>);
static_assert(std::is_nothrow_move_constructible_v<hardware::Result<MoveOnly, TestError>>);

int main() {
    auto value = hardware::Result<int, TestError>::success(7);
    assert(value && value.value() == 7);
    auto error = hardware::Result<int, TestError>::failure(TestError::Failure);
    assert(!error && error.error() == TestError::Failure);
    auto moved = hardware::Result<MoveOnly, TestError>::success(MoveOnly{3});
    assert(moved.value().value == 3);
    auto void_value = hardware::Result<void, TestError>::success();
    assert(void_value);
    auto void_error = hardware::Result<void, TestError>::failure(TestError::Failure);
    assert(!void_error && void_error.error() == TestError::Failure);
}
