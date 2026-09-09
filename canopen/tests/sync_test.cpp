#include "canopen/network.hpp"

#include <deque>
#include <gtest/gtest.h>

namespace {
class QueueCan final : public can::Interface {
public:
    can::Result<void> send(const can::Frame& frame) noexcept override {
        tx.push_back(frame);
        return {};
    }
    can::Result<bool> receive(can::Frame& frame, can::RxInfo& info) noexcept override {
        if (rx.empty()) return false;
        frame = rx.front();
        rx.pop_front();
        info.received_at = std::chrono::steady_clock::now();
        return true;
    }
    bool try_pop_event(can::Event&) noexcept override { return false; }
    std::deque<can::Frame> rx;
    std::deque<can::Frame> tx;
};
}  // namespace

TEST(Sync, CommitsRpdoAndSchedulesTpdoOnTheSameProtocolEvent) {
    auto bus = std::make_shared<QueueCan>();
    canopen::Network network(bus);
    const canopen::ObjectKey value{0x2000, 0};
    const canopen::PdoConfig rpdo{0x201, 1, {{value, 16, false}}};
    const canopen::PdoConfig tpdo{0x181, 2, {{value, 16, false}}};
    auto node = std::make_unique<canopen::Node>(canopen::NodeConfig{1, {rpdo}, {tpdo}, {}, {}});
    ASSERT_TRUE(node->dictionary().add(
        {value, 16, false, canopen::ObjectAccess::ReadWrite, {std::byte{0}, std::byte{0}}}));
    auto* node_ptr = node.get();
    ASSERT_TRUE(network.add_slave_node(std::move(node)));
    ASSERT_TRUE(network.initialize());
    ASSERT_TRUE(network.start());
    bus->tx.clear();

    can::Frame frame{};
    frame.id = 0x201;
    frame.size = 2;
    frame.data[0] = std::byte{0x34};
    frame.data[1] = std::byte{0x12};
    bus->rx.push_back(frame);
    ASSERT_TRUE(network.poll());
    auto slot = node_ptr->process_image().find(value);
    ASSERT_TRUE(slot);
    EXPECT_EQ(node_ptr->process_image().read(*slot).value(), 0U);

    frame = {};
    frame.id = 0x80;
    bus->rx.push_back(frame);
    ASSERT_TRUE(network.poll());
    EXPECT_EQ(node_ptr->process_image().read(*slot).value(), 0x1234U);
    EXPECT_TRUE(bus->tx.empty());

    bus->rx.push_back(frame);
    ASSERT_TRUE(network.poll());
    ASSERT_EQ(bus->tx.size(), 1U);
    EXPECT_EQ(bus->tx.front().id, 0x181U);
}

TEST(Nmt, StartEmitsBootupAndResetCommunicationEmitsAnotherBootup) {
    auto bus = std::make_shared<QueueCan>();
    canopen::Network network(bus);
    auto node = std::make_unique<canopen::Node>(canopen::NodeConfig{1, {}, {}, {}, {}});
    auto* node_ptr = node.get();
    ASSERT_TRUE(network.add_slave_node(std::move(node)));
    ASSERT_TRUE(network.initialize());
    ASSERT_TRUE(network.start());
    ASSERT_EQ(bus->tx.size(), 1U);
    EXPECT_EQ(bus->tx.front().id, 0x701U);
    EXPECT_EQ(bus->tx.front().data[0], std::byte{0});
    bus->tx.clear();

    can::Frame reset{};
    reset.id = 0;
    reset.size = 2;
    reset.data[0] = static_cast<std::byte>(canopen::NmtCommand::ResetCommunication);
    reset.data[1] = std::byte{1};
    bus->rx.push_back(reset);
    ASSERT_TRUE(network.poll());
    EXPECT_EQ(node_ptr->nmt_state(), canopen::NmtState::PreOperational);
    ASSERT_EQ(bus->tx.size(), 1U);
    EXPECT_EQ(bus->tx.front().id, 0x701U);
}
