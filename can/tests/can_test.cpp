#include "can/socket.hpp"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

static_assert(!std::is_default_constructible_v<can::Socket>);
static_assert(!std::is_copy_constructible_v<can::Socket>);
static_assert(std::is_nothrow_move_constructible_v<can::Socket>);

TEST(Frame, PreservesCanDomainValues) {
    can::Frame standard{0x7FF, 8, can::FrameFormat::Standard, can::FrameType::Data};
    EXPECT_EQ(standard.id, 0x7FFU);
    EXPECT_EQ(standard.size, 8U);
    EXPECT_EQ(standard.format, can::FrameFormat::Standard);
    EXPECT_EQ(standard.type, can::FrameType::Data);

    can::Frame extended{0x1FFFFFFF, 0, can::FrameFormat::Extended, can::FrameType::Remote};
    EXPECT_EQ(extended.id, 0x1FFFFFFFU);
    EXPECT_EQ(extended.format, can::FrameFormat::Extended);
    EXPECT_EQ(extended.type, can::FrameType::Remote);
}

TEST(Filter, RetainsFormatAsPartOfMatchingIntent) {
    const can::Filter standard{0x123, 0x7FF, can::FrameFormat::Standard};
    const can::Filter extended{0x123, 0x1FFFFFFF, can::FrameFormat::Extended};
    EXPECT_NE(standard.format, extended.format);
    EXPECT_EQ(standard.id, extended.id);
}

TEST(Event, RetainsRxOverflowAsDistinctV1Diagnostic) {
    const can::Event event{can::EventType::RxOverflow};
    EXPECT_EQ(event.type, can::EventType::RxOverflow);
}

TEST(Timestamp, UsesMonotonicClockDomain) {
    static_assert(std::is_same_v<can::Timestamp::clock, std::chrono::steady_clock>);
    const can::Timestamp first{std::chrono::nanoseconds{10}};
    const can::Timestamp second{std::chrono::nanoseconds{11}};
    EXPECT_LT(first, second);
}

}  // namespace
