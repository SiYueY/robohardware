#include "../event.cpp"
#include "../socket.cpp"

#include <gtest/gtest.h>

#include <linux/can/error.h>

#include <type_traits>

namespace {

static_assert(!std::is_default_constructible_v<can::Socket>);
static_assert(!std::is_copy_constructible_v<can::Socket>);
static_assert(std::is_nothrow_move_constructible_v<can::Socket>);

TEST(Frame, ValidatesFormatSpecificIdsAndClassicLength) {
    can::Frame standard{0x7FF, 8, can::FrameFormat::Standard};
    EXPECT_TRUE(validate(standard));
    standard.id = 0x800;
    EXPECT_EQ(validate(standard).error().code, can::ErrorCode::InvalidFrame);

    can::Frame extended{0x1FFFFFFF, 0, can::FrameFormat::Extended};
    EXPECT_TRUE(validate(extended));
    extended.id = 0x20000000;
    EXPECT_EQ(validate(extended).error().code, can::ErrorCode::InvalidFrame);

    can::Frame oversized{0, 9};
    EXPECT_EQ(validate(oversized).error().code, can::ErrorCode::InvalidFrame);
}

TEST(Frame, ConvertsDataAndRemoteFramesWithoutLeakingFlags) {
    can::Frame standard{0x7FF, 0, can::FrameFormat::Standard, can::FrameType::Data};
    const ::can_frame standard_native = to_native(standard);
    EXPECT_EQ(standard_native.can_id, standard.id);

    can::Frame input{0x1ABCDE, 3, can::FrameFormat::Extended, can::FrameType::Data};
    input.data[0] = std::byte{0x12};
    input.data[1] = std::byte{0x34};
    input.data[2] = std::byte{0x56};
    const ::can_frame native = to_native(input);
    EXPECT_NE(native.can_id & CAN_EFF_FLAG, 0U);
    EXPECT_EQ(native.can_id & CAN_RTR_FLAG, 0U);
    const auto decoded = from_native(native);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded.value().id, input.id);
    EXPECT_EQ(decoded.value().data, input.data);

    input.type = can::FrameType::Remote;
    const ::can_frame remote = to_native(input);
    EXPECT_NE(remote.can_id & CAN_RTR_FLAG, 0U);
    const auto decoded_remote = from_native(remote);
    ASSERT_TRUE(decoded_remote);
    EXPECT_EQ(decoded_remote.value().type, can::FrameType::Remote);
}

TEST(Frame, RejectsErrorFramesFromNormalFramePath) {
    ::can_frame native{};
    native.can_id = CAN_ERR_FLAG | CAN_ERR_BUSOFF;
    EXPECT_EQ(from_native(native).error().code, can::ErrorCode::InvalidFrame);
}

bool matches(const ::can_filter& filter, canid_t received) {
    return (received & filter.can_mask) == (filter.can_id & filter.can_mask);
}

TEST(Filter, MatchesIdsAndFormatsExactly) {
    const can::Filter standard{0x123, 0x7FF, can::FrameFormat::Standard};
    const ::can_filter standard_native = to_native(standard);
    EXPECT_TRUE(matches(standard_native, 0x123));
    EXPECT_FALSE(matches(standard_native, CAN_EFF_FLAG | 0x123));

    const can::Filter extended{0x123, 0x1FFFFFFF, can::FrameFormat::Extended};
    const ::can_filter extended_native = to_native(extended);
    EXPECT_TRUE(matches(extended_native, CAN_EFF_FLAG | 0x123));
    EXPECT_FALSE(matches(extended_native, 0x123));
}

TEST(Filter, AppliesMaskedMatchingWithoutCrossFormatMatches) {
    const ::can_filter filter = to_native(can::Filter{0x120, 0x7F0, can::FrameFormat::Standard});
    EXPECT_TRUE(matches(filter, 0x12F));
    EXPECT_FALSE(matches(filter, 0x130));
    EXPECT_FALSE(matches(filter, CAN_EFF_FLAG | 0x12F));
}

TEST(Filter, ValidatesIdsAndMasks) {
    EXPECT_TRUE(validate(can::Filter{0x7FF, 0x7FF, can::FrameFormat::Standard}));
    EXPECT_EQ(
        validate(can::Filter{0x800, 0, can::FrameFormat::Standard}).error().code,
        can::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        validate(can::Filter{0, 0x800, can::FrameFormat::Standard}).error().code,
        can::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        validate(can::Filter{0x20000000, 0, can::FrameFormat::Extended}).error().code,
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
        const can::Event event = decode_error_frame(native, timestamp);
        EXPECT_EQ(event.type, test.type);
        EXPECT_EQ(apply_event(can::State::Unknown, event), test.state);
    }
}

TEST(Event, RecognizesRxOverflow) {
    ::can_frame native{};
    native.can_id = CAN_ERR_FLAG | CAN_ERR_CRTL;
    native.data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
    native.data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
    const can::Event event = decode_error_frame(native, {});
    EXPECT_EQ(event.type, can::EventType::Passive);
    EXPECT_EQ(event.detail, CAN_ERR_CRTL);

    can::Stats stats{};
    update_error_stats(stats, native);
    EXPECT_EQ(stats.error_frames, 1U);
    EXPECT_EQ(stats.rx_overruns, 1U);
}

TEST(Event, KeepsBusOffStateForNonStateDiagnostics) {
    const can::Event event{can::EventType::ProtocolError};
    EXPECT_EQ(apply_event(can::State::BusOff, event), can::State::BusOff);
}

TEST(Timestamp, UsesMonotonicClockDomain) {
    static_assert(std::is_same_v<can::Timestamp::clock, std::chrono::steady_clock>);
    const can::Timestamp first{std::chrono::nanoseconds{10}};
    const can::Timestamp second{std::chrono::nanoseconds{11}};
    EXPECT_LT(first, second);
}

}  // namespace
