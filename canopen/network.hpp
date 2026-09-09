#pragma once
#include "can/interface.hpp"
#include "canopen/node.hpp"
#include <array>
#include <memory>
#include <vector>
namespace canopen {
class SdoServer;
enum class RouteKind : std::uint8_t {
    None,
    Nmt,
    SdoRequest,
    SdoResponse,
    Rpdo,
    Tpdo,
    Heartbeat,
    Emcy,
    Sync
};
struct Route {
    RouteKind kind{RouteKind::None};
    std::uint8_t node_id{0};
    std::uint8_t index{0};
};
class RouteTable {
public:
    Result<void> add(std::uint16_t cob_id, Route route) noexcept;
    Result<void> freeze() noexcept {
        frozen_ = true;
        return {};
    }
    const Route* find(std::uint32_t cob_id) const noexcept {
        return cob_id < routes_.size() && routes_[cob_id].kind != RouteKind::None ? &routes_[cob_id]
                                                                                  : nullptr;
    }

private:
    std::array<Route, 2048> routes_{};
    bool frozen_{false};
};
class Network {
public:
    explicit Network(std::shared_ptr<can::Interface> interface);
    ~Network();
    Result<void> add_node(std::unique_ptr<Node> node);
    /// Registers a locally hosted slave endpoint, including its SDO server route.
    Result<void> add_slave_node(std::unique_ptr<Node> node);
    Result<void> initialize();
    Result<void> start() noexcept;
    Result<void> stop() noexcept;
    /// Executes bounded housekeeping in the caller's time domain; it never waits.
    Result<void> tick(MonotonicTimestamp now) noexcept;
    /// Non-RT receive path. It can dispatch SDO and must not be used by a cyclic RT loop.
    Result<bool> poll() noexcept;
    Result<void> send(const can::Frame& frame) noexcept;
    Result<void>
    send_sync() noexcept;  /// Sends all type-1 TPDOs; no thread or scheduler is created.
    Result<void> send_synchronous_tpdos() noexcept;
    Result<void> send_nmt(NmtCommand command, std::uint8_t node_id = 0) noexcept;
    Node* node(std::uint8_t id) noexcept;
    const Node* node(std::uint8_t id) const noexcept;
    bool take_sdo_response(std::uint8_t node_id, can::Frame& frame) noexcept;
    bool try_pop_emcy(EmcyEvent& event) noexcept;
    Status status() const noexcept;
    Stats stats() const noexcept { return stats_; }
    bool running() const noexcept { return running_; }
    const RouteTable& routes() const noexcept { return routes_; }

private:
    friend class SdoClient;
    Result<void> dispatch(const can::Frame&, MonotonicTimestamp) noexcept;
    std::shared_ptr<can::Interface> interface_;
    std::vector<std::unique_ptr<Node>> nodes_;
    std::array<bool, 128> serves_sdo_{};
    RouteTable routes_;
    std::array<can::Frame, 128> sdo_responses_{};
    std::array<bool, 128> has_sdo_response_{};
    std::vector<std::unique_ptr<SdoServer>> sdo_servers_;
    std::array<SdoServer*, 128> sdo_server_by_node_{};
    static constexpr std::size_t kEmcyCapacity = 16;
    std::array<EmcyEvent, kEmcyCapacity> emcy_events_{};
    std::size_t emcy_read_{0}, emcy_write_{0};
    std::uint64_t sync_count_{0};
    Stats stats_{};
    bool initialized_{false}, running_{false};
};
}  // namespace canopen
