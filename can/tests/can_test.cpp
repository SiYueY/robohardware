#include "can/native.hpp"

#include <gtest/gtest.h>

#include <linux/can/error.h>

namespace {

TEST(Frame, ValidatesFormatSpecificIdsAndClassicLength) {
    can::Frame standard{0x7FF, 8, can::FrameFormat::Standard};
    EXPECT_TRUE(can::native::validate(standard));
    standard.id = 0x800;
    EXPECT_EQ(can::native::validate(standard).error().code, can::ErrorCode::InvalidFrame);

    can::Frame extended{0x1FFFFFFF, 0, can::FrameFormat::Extended};
    EXPECT_TRUE(can::native::validate(extended));
    extended.id = 0x20000000;
    EXPECT_EQ(can::native::validate(extended).error().code, can::ErrorCode::InvalidFrame);

    can::Frame oversized{0, 9};
    EXPECT_EQ(can::native::validate(oversized).error().code, can::ErrorCode::InvalidFrame);
}

TEST(Frame, ConvertsDataAndRemoteFramesWithoutLeakingFlags) {
    can::Frame input{0x1ABCDE, 3, can::FrameFormat::Extended, can::FrameType::Data};
    input.data[0] = std::byte{0x12};
    input.data[1] = std::byte{0x34};
    input.data[2] = std::byte{0x56};
    const ::can_frame native = can::native::to_native(input);
    EXPECT_NE(native.can_id & CAN_EFF_FLAG, 0U);
    EXPECT_EQ(native.can_id & CAN_RTR_FLAG, 0U);
    ASSERT_TRUE(can::native::from_native(native));
    EXPECT_EQ(can::native::from_native(native).value().id, input.id);
    EXPECT_EQ(can::native::from_native(native).value().data, input.data);

    input.type = can::FrameType::Remote;
    const ::can_frame remote = can::native::to_native(input);
    EXPECT_NE(remote.can_id & CAN_RTR_FLAG, 0U);
    ASSERT_TRUE(can::native::from_native(remote));
    EXPECT_EQ(can::native::from_native(remote).value().type, can::FrameType::Remote);
}

TEST(Frame, RejectsErrorFramesFromNormalFramePath) {
    ::can_frame native{};
    native.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
    EXPECT_EQ(can::native::from_native(native).error().code, can::ErrorCode::InvalidFrame);
}

TEST(Filter, PreservesFormatInNativeMatch) {
    const can::Filter standard{0x123, 0x7FF, can::FrameFormat::Standard};
    const ::can_filter standard_native = can::native::to_native(standard);
    EXPECT_EQ(standard_native.can_id, 0x123U);
    EXPECT_EQ(standard_native.can_mask, 0x7FFU | CAN_EFF_FLAG);

    const can::Filter extended{0x123, 0x1FFFFFFF, can::FrameFormat::Extended};
    const ::can_filter extended_native = can::native::to_native(extended);
    EXPECT_EQ(extended_native.can_id, 0x123U | CAN_EFF_FLAG);
    EXPECT_EQ(extended_native.can_mask, 0x1FFFFFFFU | CAN_EFF_FLAG);
}

TEST(Filter, ValidatesIdsAndMasks) {
    EXPECT_TRUE(can::native::validate(can::Filter{0x7FF, 0x7FF, can::FrameFormat::Standard}));
    EXPECT_EQ(can::native::validate(can::Filter{0x800, 0, can::FrameFormat::Standard}).error().code,
              can::ErrorCode::InvalidArgument);
    EXPECT_EQ(can::native::validate(can::Filter{0, 0x800, can::FrameFormat::Standard}).error().code,
              can::ErrorCode::InvalidArgument);
    EXPECT_EQ(can::native::validate(can::Filter{0x20000000, 0, can::FrameFormat::Extended}).error().code,
              can::ErrorCode::InvalidArgument);
}

TEST(Event, DecodesErrorFramesAndUpdatesState) {
    const can::Timestamp timestamp{};
    struct Case {
        canid_t id;
        std::uint8_t control;
        can::EventType type;
        can::State state;
    };
    const Case cases[] = {
        {CAN_ERR_CRTL, CAN_ERR_CRTL_TX_WARNING, can::EventType::Warning, can::State::Warning},
        {CAN_ERR_CRTL, CAN_ERR_CRTL_RX_PASSIVE, can::EventType::Passive, can::State::Passive},
        {CAN_ERR_BUSOFF, 0, can::EventType::BusOff, can::State::BusOff},
        {CAN_ERR_RESTARTED, 0, can::EventType::Restarted, can::State::Active},
        {CAN_ERR_LOSTARB, 0, can::EventType::ArbitrationLost, can::State::Unknown},
        {CAN_ERR_CRTL, 0, can::EventType::ControllerError, can::State::Unknown},
        {CAN_ERR_PROT, 0, can::EventType::ProtocolError, can::State::Unknown},
        {0, 0, can::EventType::Unknown, can::State::Unknown},
    };

    for (const auto& test : cases) {
        ::can_frame native{};
        native.can_id = CAN_ERR_FLAG | test.id;
        native.data[1] = test.control;
        const can::Event event = can::native::decode_error_frame(native, timestamp);
        EXPECT_EQ(event.type, test.type);
        EXPECT_EQ(can::native::apply_event(can::State::Unknown, event), test.state);
    }
}

TEST(Event, RecognizesRxOverflow) {
    ::can_frame native{};
    native.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
    native.data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
    const can::Event event = can::native::decode_error_frame(native, {});
    EXPECT_EQ(event.type, can::EventType::RxOverflow);

    can::Stats stats{};
    can::native::update_stats(stats, event);
    EXPECT_EQ(stats.error_frames, 1U);
    EXPECT_EQ(stats.rx_overruns, 1U);
}

TEST(Timestamp, UsesMonotonicClockDomain) {
    static_assert(std::is_same_v<can::Timestamp::clock, std::chrono::steady_clock>);
    const can::Timestamp first{std::chrono::nanoseconds{10}};
    const can::Timestamp second{std::chrono::nanoseconds{11}};
    EXPECT_LT(first, second);
}

}  // namespace
