#include <can/frame.hpp>
#include <can/error.hpp>
#include <type_traits>
static_assert(std::is_trivially_destructible_v<can::ReceivedFrame>);
int main() {
    can::ClassicalFrame frame{};
    auto received = can::ReceivedFrame::classical(frame);
    return received.kind() == can::ReceivedFrame::Kind::Classical && received.classical().size == 0
               ? 0
               : 1;
}
