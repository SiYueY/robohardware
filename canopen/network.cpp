#include "canopen/network.hpp"
#include "canopen/sdo.hpp"
#include <chrono>
#include <thread>
namespace canopen {
namespace {
bool valid_nmt_command(NmtCommand command) noexcept {
    switch (command) {
        case NmtCommand::Start:
        case NmtCommand::Stop:
        case NmtCommand::EnterPreOperational:
        case NmtCommand::ResetNode:
        case NmtCommand::ResetCommunication:
            return true;
    }
    return false;
}
}  // namespace
Result<void> RouteTable::add(std::uint16_t id, Route route) noexcept {
    if (frozen_ || id > 0x7FF || routes_[id].kind != RouteKind::None)
        return Error{ErrorCode::InvalidCobId, route.node_id};
    routes_[id] = route;
    return {};
}
Network::Network(std::shared_ptr<can::Interface> i) : interface_(std::move(i)) {}
Network::~Network() = default;
Result<void> Network::add_node(std::unique_ptr<Node> node) {
    if (!node) return Error{ErrorCode::InvalidState};
    const auto id = node->id();
    if (id == 0 || id > 127) return Error{ErrorCode::InvalidNodeId, id};
    if (initialized_ || nodes_by_id_[id]) return Error{ErrorCode::InvalidState, id};
    nodes_.push_back(std::move(node));
    nodes_by_id_[id] = nodes_.back().get();
    return {};
}
Result<void> Network::add_slave_node(std::unique_ptr<Node> node) {
    if (!node) return Error{ErrorCode::InvalidState};
    const auto id = node->id();
    if (id == 0 || id > 127) return Error{ErrorCode::InvalidNodeId, id};
    if (initialized_ || nodes_by_id_[id]) return Error{ErrorCode::InvalidState, id};
    nodes_.push_back(std::move(node));
    serves_sdo_[id] = true;
    nodes_by_id_[id] = nodes_.back().get();
    return {};
}
Result<void> Network::initialize() {
    if (initialized_ || !interface_) return Error{ErrorCode::InvalidState};
    for (auto& n : nodes_) {
        auto r = n->initialize();
        if (!r) return r.error();
        if (serves_sdo_[n->id()]) {
            sdo_servers_.push_back(std::make_unique<SdoServer>(*n));
            sdo_server_by_node_[n->id()] = sdo_servers_.back().get();
        }
        for (std::size_t i = 0; serves_sdo_[n->id()] && i < n->rpdos().size(); ++i) {
            r = routes_.add(
                static_cast<std::uint16_t>(n->rpdos()[i].cob_id()),
                {RouteKind::Rpdo, n->id(), static_cast<std::uint8_t>(i)});
            if (!r) return r.error();
        }
        for (std::size_t i = 0; !serves_sdo_[n->id()] && i < n->tpdos().size(); ++i) {
            r = routes_.add(
                static_cast<std::uint16_t>(n->tpdos()[i].cob_id()),
                {RouteKind::Tpdo, n->id(), static_cast<std::uint8_t>(i)});
            if (!r) return r.error();
        }
        r = routes_.add(
            static_cast<std::uint16_t>(0x700 + n->id()), {RouteKind::Heartbeat, n->id(), 0});
        if (!r) return r.error();
        r = routes_.add(static_cast<std::uint16_t>(0x80 + n->id()), {RouteKind::Emcy, n->id(), 0});
        if (!r) return r.error();
        if (serves_sdo_[n->id()]) {
            r = routes_.add(
                static_cast<std::uint16_t>(0x600 + n->id()), {RouteKind::SdoRequest, n->id(), 0});
            if (!r) return r.error();
        } else {
            r = routes_.add(
                static_cast<std::uint16_t>(0x580 + n->id()), {RouteKind::SdoResponse, n->id(), 0});
            if (!r) return r.error();
        }
    }
    auto r = routes_.add(0x000, {RouteKind::Nmt, 0, 0});
    if (!r) return r.error();
    r = routes_.add(0x080, {RouteKind::Sync, 0, 0});
    if (!r) return r.error();
    routes_.freeze();
    initialized_ = true;
    return {};
}
Result<void> Network::start() noexcept {
    if (!initialized_ || running_) return Error{ErrorCode::InvalidState};
    const auto now = std::chrono::steady_clock::now();
    sync_.reset();
    for (const auto& node : nodes_) {
        node->arm_heartbeat_monitor(now);
        if (serves_sdo_[node->id()]) {
            auto result = send_bootup(*node);
            if (!result) return result.error();
        }
    }
    running_ = true;
    return {};
}
Result<void> Network::stop() noexcept {
    running_ = false;
    sync_.reset();
    has_sdo_response_.fill(false);
    sdo_active_.fill(false);
    for (auto& server : sdo_servers_) server->reset();
    return {};
}
Result<void> Network::tick(MonotonicTimestamp now) noexcept {
    if (!running_) return Error{ErrorCode::InvalidState};
    for (const auto& n : nodes_) {
        if (n->heartbeat_timed_out(now)) {
            n->mark_heartbeat_timeout();
            ++stats_.heartbeat_timeouts;
        }
        if (serves_sdo_[n->id()] && n->should_produce_heartbeat(now)) {
            can::Frame frame{};
            frame.id = 0x700 + n->id();
            frame.size = 1;
            frame.data[0] = static_cast<std::byte>(n->nmt_state());
            auto result = send(frame);
            if (!result) return result.error();
        }
    }
    return {};
}
Result<void> Network::send(const can::Frame& f) noexcept {
    auto r = interface_->send(f);
    if (!r) return Error{ErrorCode::TransportError};
    ++stats_.tx_frames;
    return {};
}
Result<void> Network::send_sync() noexcept {
    can::Frame f{};
    f.id = 0x80;
    auto r = send(f);
    if (r) ++stats_.sync_sent;
    return r;
}
Result<void> Network::send_nmt(NmtCommand c, std::uint8_t id) noexcept {
    if (id > 127) return Error{ErrorCode::InvalidNodeId, id};
    if (!valid_nmt_command(c)) return Error{ErrorCode::NmtError, id};
    can::Frame f{};
    f.id = 0;
    f.size = 2;
    f.data[0] = static_cast<std::byte>(c);
    f.data[1] = static_cast<std::byte>(id);
    return send(f);
}
Result<void> Network::send_synchronous_tpdos() noexcept {
    for (const auto& n : nodes_)
        if (serves_sdo_[n->id()])
            for (std::size_t i = 0; i < n->tpdos().size(); ++i)
                if (sync_.due(n->tpdos()[i].transmission_type())) {
                    can::Frame frame;
                    auto r = n->encode_tpdo(i, frame);
                    if (!r) return r.error();
                    r = send(frame);
                    if (!r) return r.error();
                    ++stats_.tpdo_sent;
                }
    return {};
}
Node* Network::node(std::uint8_t id) noexcept {
    return id < nodes_by_id_.size() ? nodes_by_id_[id] : nullptr;
}
const Node* Network::node(std::uint8_t id) const noexcept {
    return id < nodes_by_id_.size() ? nodes_by_id_[id] : nullptr;
}
bool Network::take_sdo_response(std::uint8_t id, can::Frame& f) noexcept {
    if (id > 127 || !has_sdo_response_[id]) return false;
    f = sdo_responses_[id];
    has_sdo_response_[id] = false;
    return true;
}
Result<void> Network::begin_sdo(std::uint8_t id) noexcept {
    if (!running_) return Error{ErrorCode::InvalidState, id};
    if (id == 0 || id > 127 || !node(id)) return Error{ErrorCode::InvalidNodeId, id};
    if (sdo_active_[id]) return Error{ErrorCode::SdoBusy, id};
    sdo_active_[id] = true;
    has_sdo_response_[id] = false;
    ++stats_.sdo_requests;
    return {};
}
void Network::end_sdo(std::uint8_t id) noexcept {
    if (id <= 127) sdo_active_[id] = false;
}
Result<can::Frame> Network::wait_sdo_response(std::uint8_t id, Duration timeout) noexcept {
    const auto until = std::chrono::steady_clock::now() + timeout;
    can::Frame response;
    while (std::chrono::steady_clock::now() < until) {
        if (take_sdo_response(id, response)) return response;
        auto result = poll();
        if (!result) return result.error();
        if (!result.value()) std::this_thread::sleep_for(std::chrono::microseconds{50});
    }
    ++stats_.sdo_timeouts;
    return Error{ErrorCode::Timeout, id};
}
bool Network::try_pop_emcy(EmcyEvent& event) noexcept {
    if (emcy_read_ == emcy_write_) return false;
    event = emcy_events_[emcy_read_];
    emcy_read_ = (emcy_read_ + 1) % kEmcyCapacity;
    return true;
}
Result<bool> Network::poll() noexcept {
    if (!running_) return Error{ErrorCode::InvalidState};
    can::Frame f;
    can::RxInfo info;
    auto r = interface_->receive(f, info);
    if (!r) return Error{ErrorCode::TransportError};
    if (!r.value()) return false;
    ++stats_.rx_frames;
    auto x = dispatch(f, info.received_at);
    if (!x) return x.error();
    return true;
}
Result<void> Network::dispatch(const can::Frame& f, MonotonicTimestamp now) noexcept {
    if (f.format != can::FrameFormat::Standard || f.type != can::FrameType::Data)
        return Error{ErrorCode::InvalidArgument};
    auto route = routes_.find(f.id);
    if (!route) return {};
    if (route->kind == RouteKind::Nmt) {
        if (f.size < 2) return Error{ErrorCode::NmtError};
        auto command = static_cast<NmtCommand>(static_cast<std::uint8_t>(f.data[0]));
        auto target = static_cast<std::uint8_t>(f.data[1]);
        if (!valid_nmt_command(command) || target > 127) return Error{ErrorCode::NmtError, target};
        for (auto& n : nodes_)
            if (serves_sdo_[n->id()] && (target == 0 || n->id() == target) &&
                n->receive_nmt(command, now)) {
                sync_.reset();
                if (sdo_server_by_node_[n->id()]) sdo_server_by_node_[n->id()]->reset();
                auto result = send_bootup(*n);
                if (!result) return result.error();
            }
        return {};
    }
    if (route->kind == RouteKind::Sync) {
        ++stats_.sync_received;
        return on_sync();
    }
    auto* n = node(route->node_id);
    if (!n) return Error{ErrorCode::InvalidNodeId, route->node_id};
    if (route->kind == RouteKind::SdoRequest) {
        can::Frame response;
        // An SDO abort is a protocol response, not a transport-dispatch failure.
        auto result = sdo_server_by_node_[n->id()]->process(f, response);
        if (!result) return result.error();
        return send(response);
    }
    if (route->kind == RouteKind::SdoResponse) {
        sdo_responses_[route->node_id] = f;
        has_sdo_response_[route->node_id] = true;
        return {};
    }
    if (route->kind == RouteKind::Rpdo) {
        ++stats_.rpdo_received;
        return n->receive_rpdo(route->index, f, now);
    }
    if (route->kind == RouteKind::Tpdo) return n->receive_tpdo(route->index, f, now);
    if (route->kind == RouteKind::Heartbeat) {
        if (f.size < 1) return Error{ErrorCode::InvalidArgument};
        n->receive_heartbeat(static_cast<NmtState>(static_cast<std::uint8_t>(f.data[0])), now);
        ++stats_.heartbeat_received;
        return {};
    }
    if (route->kind == RouteKind::Emcy) {
        n->receive_emcy(f, now);
        ++stats_.emcy_received;
        if (f.size < 3) return Error{ErrorCode::InvalidArgument, n->id()};
        const auto next = (emcy_write_ + 1) % kEmcyCapacity;
        if (next != emcy_read_) {
            auto& event = emcy_events_[emcy_write_];
            event.node_id = n->id();
            event.error_code = static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(f.data[0]) |
                (static_cast<std::uint16_t>(static_cast<std::uint8_t>(f.data[1])) << 8));
            event.error_register = static_cast<std::uint8_t>(f.data[2]);
            for (std::size_t i = 0; i < event.manufacturer_data.size() && i + 3 < f.size; ++i)
                event.manufacturer_data[i] = f.data[i + 3];
            event.received_at = now;
            emcy_write_ = next;
        } else
            ++stats_.dropped_events;
    }
    return {};
}
Result<void> Network::on_sync() noexcept {
    sync_.on_event();
    for (const auto& node : nodes_) {
        if (!serves_sdo_[node->id()]) continue;
        auto result = node->commit_synchronous_rpdos();
        if (!result) return result.error();
    }
    return send_synchronous_tpdos();
}
Result<void> Network::send_bootup(const Node& node) noexcept {
    can::Frame frame{};
    frame.id = 0x700 + node.id();
    frame.size = 1;
    frame.data[0] = std::byte{0};
    return send(frame);
}
Status Network::status() const noexcept {
    Status s{};
    s.running = running_;
    s.configured_nodes = nodes_.size();
    for (const auto& n : nodes_) {
        if (n->nmt_state() == NmtState::Operational) ++s.operational_nodes;
        if (!n->status().communication_ok) ++s.degraded_nodes;
    }
    return s;
}
}  // namespace canopen
