#include "canopen/sdo.hpp"
#include "canopen/master.hpp"
#include "canopen/slave.hpp"
#include <gtest/gtest.h>

#include <deque>

namespace {
class LoopbackCan final : public can::Interface {
public:
    can::Result<void> send(const can::Frame& frame) noexcept override {
        frames.push_back(frame);
        return {};
    }
    can::Result<bool> receive(can::Frame& frame, can::RxInfo& info) noexcept override {
        if (frames.empty()) return false;
        frame = frames.front();
        frames.pop_front();
        info.received_at = std::chrono::steady_clock::now();
        return true;
    }
    bool try_pop_event(can::Event&) noexcept override { return false; }
    std::deque<can::Frame> frames;
};

class EndpointCan final : public can::Interface {
public:
    can::Result<void> send(const can::Frame& frame) noexcept override {
        peer_->frames.push_back(frame);
        if (peer_->on_frame) peer_->on_frame();
        return {};
    }
    can::Result<bool> receive(can::Frame& frame, can::RxInfo& info) noexcept override {
        if (frames.empty()) return false;
        frame = frames.front();
        frames.pop_front();
        info.received_at = std::chrono::steady_clock::now();
        return true;
    }
    bool try_pop_event(can::Event&) noexcept override { return false; }
    EndpointCan* peer_{nullptr};
    std::function<void()> on_frame;
    std::deque<can::Frame> frames;
};
}  // namespace
TEST(SDO, ServerExpeditedReadWriteAndAbort) {
    canopen::Node node({1, {}, {}, {}, {}});
    ASSERT_TRUE(node.dictionary().add(
        {{0x2000, 0}, 16, false, canopen::ObjectAccess::ReadWrite, {std::byte{1}, std::byte{2}}}));
    ASSERT_TRUE(node.initialize());
    canopen::SdoServer server(node);
    can::Frame req{};
    req.size = 8;
    req.data[0] = std::byte{0x40};
    req.data[1] = std::byte{0};
    req.data[2] = std::byte{0x20};
    can::Frame res;
    ASSERT_TRUE(server.process(req, res));
    EXPECT_EQ(static_cast<unsigned char>(res.data[0]), 0x4B);
}

TEST(SDO, ClientUsesSegmentedTransferOverCanInterface) {
    auto bus = std::make_shared<LoopbackCan>();
    canopen::Network network(bus);
    auto node = std::make_unique<canopen::Node>(canopen::NodeConfig{1, {}, {}, {}, {}});
    ASSERT_TRUE(node->dictionary().add(
        {{0x2001, 0},
         64,
         false,
         canopen::ObjectAccess::ReadWrite,
         {std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5},
          std::byte{6}, std::byte{7}}}));
    ASSERT_TRUE(network.add_slave_node(std::move(node)));
    ASSERT_TRUE(network.initialize());
    canopen::SdoClient client(network);
    const std::vector<std::byte> expected{std::byte{7}, std::byte{6}, std::byte{5}, std::byte{4},
                                          std::byte{3}, std::byte{2}, std::byte{1}, std::byte{0}};
    ASSERT_TRUE(client.download(1, {0x2001, 0}, expected, std::chrono::milliseconds{20}));
    auto actual = client.upload(1, {0x2001, 0}, std::chrono::milliseconds{20});
    ASSERT_TRUE(actual);
    EXPECT_EQ(actual.value(), expected);
}

TEST(MasterSlave, SharesProtocolComponentsAcrossAnInMemoryCanBus) {
    auto master_bus = std::make_shared<EndpointCan>();
    auto slave_bus = std::make_shared<EndpointCan>();
    master_bus->peer_ = slave_bus.get();
    slave_bus->peer_ = master_bus.get();

    canopen::Master master(master_bus);
    const canopen::PdoConfig feedback{0x181, 1, {{{0x3000, 0}, 16, false}}};
    auto remote = std::make_unique<canopen::Node>(canopen::NodeConfig{
        1, {}, {feedback}, canopen::HeartbeatConsumerConfig{std::chrono::milliseconds{10}}, {}});
    ASSERT_TRUE(remote->dictionary().add(
        {{0x3000, 0}, 16, false, canopen::ObjectAccess::ReadWrite, {std::byte{0}, std::byte{0}}}));
    ASSERT_TRUE(master.add_node(std::move(remote)));

    canopen::Network slave_network(slave_bus);
    canopen::Slave slave(canopen::NodeConfig{1, {}, {feedback}, {}, std::chrono::milliseconds{5}});
    auto* slave_node = &slave.node();
    ASSERT_TRUE(slave_node->dictionary().add(
        {{0x2002, 0},
         16,
         false,
         canopen::ObjectAccess::ReadWrite,
         {std::byte{0x12}, std::byte{0x34}}}));
    ASSERT_TRUE(slave_node->dictionary().add(
        {{0x3000, 0}, 16, false, canopen::ObjectAccess::ReadWrite, {std::byte{0}, std::byte{0}}}));
    ASSERT_TRUE(slave.attach(slave_network));
    ASSERT_TRUE(master.initialize());
    ASSERT_TRUE(slave_network.initialize());
    ASSERT_TRUE(master.start());
    ASSERT_TRUE(slave_network.start());

    slave_bus->on_frame = [&slave_network] { EXPECT_TRUE(slave_network.poll()); };
    auto read = master.sdo_client().upload(1, {0x2002, 0}, std::chrono::milliseconds{20});
    ASSERT_TRUE(read);
    ASSERT_EQ(read.value().size(), 2U);
    EXPECT_EQ(read.value()[0], std::byte{0x12});
    EXPECT_EQ(read.value()[1], std::byte{0x34});

    ASSERT_TRUE(master.network().send_nmt(canopen::NmtCommand::Start, 1));
    EXPECT_EQ(slave_node->nmt_state(), canopen::NmtState::Operational);

    ASSERT_TRUE(slave_node->process_image().write(0, 0x1234));
    ASSERT_TRUE(master.network().send_sync());
    ASSERT_TRUE(slave_network.send_synchronous_tpdos());
    ASSERT_TRUE(master.network().poll());
    EXPECT_EQ(master.network().node(1)->process_image().read(0).value(), 0x1234U);

    ASSERT_TRUE(slave_network.tick(std::chrono::steady_clock::now()));
    ASSERT_TRUE(master.network().poll());
    EXPECT_TRUE(master.network().node(1)->status().heartbeat_alive);
}
