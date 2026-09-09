#include "can/socket.hpp"

#include <gtest/gtest.h>

#include <poll.h>

namespace {

constexpr const char* kInterface = "vcan0";

can::Result<can::Socket> open_vcan(can::Socket::Options options = {}) {
    options.interface = kInterface;
    return can::Socket::open(std::move(options));
}

bool receive_one(can::Socket& socket, can::Frame& frame, can::RxInfo& info) {
    pollfd descriptor{socket.fd(), POLLIN, 0};
    if (::poll(&descriptor, 1, 100) != 1) return false;
    const auto result = socket.receive(frame, info);
    return result && result.value();
}

TEST(Socket, RejectsInvalidOptionsAndUnknownInterface) {
    EXPECT_EQ(can::Socket::open({}).error().code, can::ErrorCode::InvalidArgument);
    EXPECT_EQ(
        can::Socket::open({"does-not-exist", true, false, {}}).error().code,
        can::ErrorCode::OpenFailed);
}

TEST(SocketVcan, SendsAndReceivesStandardExtendedAndRemoteFrames) {
    auto sender_result = open_vcan();
    if (!sender_result) GTEST_SKIP() << "vcan0 is unavailable";
    auto receiver_result = open_vcan();
    if (!receiver_result) GTEST_SKIP() << "vcan0 is unavailable";
    auto sender = std::move(sender_result.value());
    auto receiver = std::move(receiver_result.value());

    can::Frame frames[] = {
        {0x123, 0, can::FrameFormat::Standard, can::FrameType::Data},
        {0x1ABCDE, 8, can::FrameFormat::Extended, can::FrameType::Data},
        {0x456, 4, can::FrameFormat::Standard, can::FrameType::Remote},
    };
    frames[1].data[0] = std::byte{0x12};
    frames[1].data[7] = std::byte{0x34};
    for (const can::Frame& sent : frames) {
        ASSERT_TRUE(sender.send(sent));
        can::Frame received{};
        can::RxInfo info{};
        ASSERT_TRUE(receive_one(receiver, received, info));
        EXPECT_EQ(received.id, sent.id);
        EXPECT_EQ(received.size, sent.size);
        EXPECT_EQ(received.format, sent.format);
        EXPECT_EQ(received.type, sent.type);
        EXPECT_EQ(received.data, sent.data);
    }
    EXPECT_EQ(sender.stats().tx_frames, 3U);
    EXPECT_EQ(receiver.stats().rx_frames, 3U);
}

TEST(SocketVcan, IsNonBlockingAndHonorsFilters) {
    auto sender_result = open_vcan();
    if (!sender_result) GTEST_SKIP() << "vcan0 is unavailable";
    auto receiver_result =
        open_vcan({"", true, false, {{0x123, 0x7FF, can::FrameFormat::Standard}}});
    if (!receiver_result) GTEST_SKIP() << "vcan0 is unavailable";
    auto sender = std::move(sender_result.value());
    auto receiver = std::move(receiver_result.value());

    can::Frame frame{};
    can::RxInfo info{};
    const auto empty = receiver.receive(frame, info);
    ASSERT_TRUE(empty);
    EXPECT_FALSE(empty.value());

    ASSERT_TRUE(sender.send({0x123, 0, can::FrameFormat::Extended, can::FrameType::Data}));
    pollfd descriptor{receiver.fd(), POLLIN, 0};
    EXPECT_EQ(::poll(&descriptor, 1, 20), 0);

    ASSERT_TRUE(sender.send({0x123, 0, can::FrameFormat::Standard, can::FrameType::Data}));
    ASSERT_TRUE(receive_one(receiver, frame, info));
    EXPECT_EQ(frame.format, can::FrameFormat::Standard);
}

TEST(SocketVcan, SupportsReceiveOwnAndMoveOwnership) {
    auto socket_result = open_vcan({"", true, true, {}});
    if (!socket_result) GTEST_SKIP() << "vcan0 is unavailable";
    auto socket = std::move(socket_result.value());
    const int original_fd = socket.fd();
    auto moved = std::move(socket);
    EXPECT_EQ(socket.fd(), -1);
    EXPECT_EQ(moved.fd(), original_fd);
    ASSERT_TRUE(moved.send({0x321, 0, can::FrameFormat::Standard, can::FrameType::Data}));
    can::Frame frame{};
    can::RxInfo info{};
    ASSERT_TRUE(receive_one(moved, frame, info));
    EXPECT_EQ(frame.id, 0x321U);
}

}  // namespace
