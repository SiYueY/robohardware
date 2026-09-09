#include "can/socket.hpp"
#include "canopen/network.hpp"
#include "canopen/sdo.hpp"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>

TEST(CanopenVcan, ExchangesBootupNmtAndSdoAcrossSocketCan) {
    auto master_socket = can::Socket::open({"vcan0", true, false, {}});
    if (!master_socket) GTEST_SKIP() << "vcan0 is unavailable";
    auto slave_socket = can::Socket::open({"vcan0", true, false, {}});
    if (!slave_socket) GTEST_SKIP() << "vcan0 is unavailable";

    auto master_bus = std::make_shared<can::Socket>(std::move(master_socket.value()));
    auto slave_bus = std::make_shared<can::Socket>(std::move(slave_socket.value()));
    canopen::Network master(master_bus);
    canopen::Network slave(slave_bus);
    ASSERT_TRUE(
        master.add_node(std::make_unique<canopen::Node>(canopen::NodeConfig{1, {}, {}, {}, {}})));
    auto local = std::make_unique<canopen::Node>(canopen::NodeConfig{1, {}, {}, {}, {}});
    auto* local_node = local.get();
    ASSERT_TRUE(local->dictionary().add(
        {{0x2000, 0},
         16,
         false,
         canopen::ObjectAccess::ReadWrite,
         {std::byte{0x34}, std::byte{0x12}}}));
    ASSERT_TRUE(slave.add_slave_node(std::move(local)));
    ASSERT_TRUE(master.initialize());
    ASSERT_TRUE(slave.initialize());
    ASSERT_TRUE(master.start());
    ASSERT_TRUE(slave.start());

    std::atomic<bool> running{true};
    std::thread slave_rx([&] {
        while (running.load(std::memory_order_relaxed)) {
            const auto result = slave.poll();
            if (!result || !result.value())
                std::this_thread::sleep_for(std::chrono::microseconds{50});
        }
    });

    canopen::SdoClient client(master);
    auto uploaded = client.upload(1, {0x2000, 0}, std::chrono::milliseconds{100});
    auto nmt_result = master.send_nmt(canopen::NmtCommand::Start, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    running.store(false, std::memory_order_relaxed);
    slave_rx.join();

    ASSERT_TRUE(uploaded);
    ASSERT_EQ(uploaded.value().size(), 2U);
    EXPECT_EQ(uploaded.value()[0], std::byte{0x34});
    EXPECT_EQ(uploaded.value()[1], std::byte{0x12});
    ASSERT_TRUE(nmt_result);
    EXPECT_EQ(local_node->nmt_state(), canopen::NmtState::Operational);
}
