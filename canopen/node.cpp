#include "canopen/node.hpp"
namespace canopen {
Node::Node(NodeConfig config) : config_(std::move(config)) { status_.node_id = config_.node_id; }
Result<void> Node::initialize() {
    if (config_.node_id == 0 || config_.node_id > 127)
        return Error{ErrorCode::InvalidNodeId, config_.node_id};
    for (const auto& c : config_.rpdos) {
        PdoPlan p;
        auto r = p.build(c, dictionary_, image_);
        if (!r) return r.error();
        rpdos_.push_back(std::move(p));
    }
    for (const auto& c : config_.tpdos) {
        PdoPlan p;
        auto r = p.build(c, dictionary_, image_);
        if (!r) return r.error();
        tpdos_.push_back(std::move(p));
    }
    image_.freeze();
    status_.nmt_state = NmtState::PreOperational;
    status_.communication_ok = true;
    return {};
}
Result<void> Node::receive_rpdo(
    std::size_t i, const can::Frame& f, MonotonicTimestamp now) noexcept {
    if (i >= rpdos_.size()) return Error{ErrorCode::InvalidArgument};
    auto r = rpdos_[i].decode(f, image_);
    if (r) status_.last_pdo = now;
    return r;
}
Result<void> Node::receive_tpdo(
    std::size_t i, const can::Frame& f, MonotonicTimestamp now) noexcept {
    if (i >= tpdos_.size()) return Error{ErrorCode::InvalidArgument};
    auto r = tpdos_[i].decode(f, image_);
    if (r) status_.last_pdo = now;
    return r;
}
void Node::receive_heartbeat(NmtState s, MonotonicTimestamp now) noexcept {
    status_.nmt_state = s;
    status_.last_heartbeat = now;
    status_.heartbeat_alive = true;
    status_.communication_ok = true;
}
void Node::receive_emcy(const can::Frame&, MonotonicTimestamp) noexcept {
    status_.emcy_active = true;
}
void Node::receive_nmt(NmtCommand c) noexcept {
    if (c == NmtCommand::Start)
        status_.nmt_state = NmtState::Operational;
    else if (c == NmtCommand::Stop)
        status_.nmt_state = NmtState::Stopped;
    else if (c == NmtCommand::EnterPreOperational)
        status_.nmt_state = NmtState::PreOperational;
    else
        status_.nmt_state = NmtState::Initializing;
}
bool Node::heartbeat_timed_out(MonotonicTimestamp now) const noexcept {
    return config_.heartbeat && heartbeat_monitoring_ && status_.communication_ok &&
           now - status_.last_heartbeat > config_.heartbeat->timeout;
}
void Node::arm_heartbeat_monitor(MonotonicTimestamp now) noexcept {
    if (!config_.heartbeat) return;
    heartbeat_monitoring_ = true;
    status_.last_heartbeat = now;
}
bool Node::should_produce_heartbeat(MonotonicTimestamp now) noexcept {
    if (config_.producer_heartbeat.count() == 0 ||
        now - last_produced_ < config_.producer_heartbeat)
        return false;
    last_produced_ = now;
    return true;
}
}  // namespace canopen
