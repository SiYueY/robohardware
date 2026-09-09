#pragma once
#include "canopen/nmt.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
namespace canopen {
using MonotonicTimestamp =
    std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds>;
using Duration = std::chrono::nanoseconds;
struct NodeStatus {
    std::uint8_t node_id{0};
    NmtState nmt_state{NmtState::Initializing};
    bool heartbeat_alive{false};
    bool communication_ok{false};
    MonotonicTimestamp last_heartbeat{};
    MonotonicTimestamp last_pdo{};
    bool emcy_active{false};
};
struct Status {
    bool running{false};
    std::size_t configured_nodes{0};
    std::size_t operational_nodes{0};
    std::size_t degraded_nodes{0};
};
struct Stats {
    std::uint64_t rx_frames{0}, tx_frames{0}, rpdo_received{0}, tpdo_sent{0}, sdo_requests{0},
        sdo_aborts{0}, sdo_timeouts{0}, heartbeat_received{0}, heartbeat_timeouts{0},
        emcy_received{0}, dropped_events{0}, sync_received{0}, sync_sent{0}, decode_errors{0};
};
struct HeartbeatConsumerConfig {
    Duration timeout{};
};
struct EmcyEvent {
    std::uint8_t node_id{0};
    std::uint16_t error_code{0};
    std::uint8_t error_register{0};
    std::array<std::byte, 5> manufacturer_data{};
    MonotonicTimestamp received_at{};
};
}  // namespace canopen
