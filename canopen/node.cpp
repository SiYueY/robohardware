#include "canopen/node.hpp"
namespace canopen {
Node::Node(NodeConfig config) : config_(std::move(config)) { status_.node_id = config_.node_id; }
Result<void> Node::initialize() {
    if (config_.node_id == 0 || config_.node_id > 127)
        return Error{ErrorCode::InvalidNodeId, config_.node_id};
    if (config_.rpdos.size() > pending_rpdos_.size())
        return Error{ErrorCode::InvalidPdoMapping, config_.node_id};
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
    pending_image_ = image_;
    status_.nmt_state = NmtState::PreOperational;
    status_.communication_ok = true;
    return {};
}
Result<void> Node::receive_rpdo(
    std::size_t i, const can::Frame& f, MonotonicTimestamp now) noexcept {
    if (i >= rpdos_.size()) return Error{ErrorCode::InvalidArgument};
    auto& target = rpdos_[i].synchronous() ? pending_image_ : image_;
    auto r = rpdos_[i].decode(f, target);
    if (r && rpdos_[i].synchronous()) pending_rpdos_[i] = true;
    if (r) status_.last_pdo = now;
    return r;
}
Result<void> Node::commit_synchronous_rpdos() noexcept {
    for (std::size_t i = 0; i < rpdos_.size(); ++i) {
        if (!pending_rpdos_[i]) continue;
        auto result = rpdos_[i].commit(pending_image_, image_);
        if (!result) return result.error();
        pending_rpdos_[i] = false;
    }
    return {};
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
bool Node::receive_nmt(NmtCommand c, MonotonicTimestamp now) noexcept {
    if (c == NmtCommand::Start)
        status_.nmt_state = NmtState::Operational;
    else if (c == NmtCommand::Stop)
        status_.nmt_state = NmtState::Stopped;
    else if (c == NmtCommand::EnterPreOperational)
        status_.nmt_state = NmtState::PreOperational;
    else if (c == NmtCommand::ResetCommunication || c == NmtCommand::ResetNode) {
        if (c == NmtCommand::ResetNode && reset_node_hook_) reset_node_hook_(reset_node_context_);
        reset_communication(now);
        return true;
    }
    return false;
}
void Node::reset_communication(MonotonicTimestamp now) noexcept {
    status_.nmt_state = NmtState::PreOperational;
    status_.heartbeat_alive = false;
    status_.communication_ok = true;
    status_.last_heartbeat = now;
    status_.last_pdo = {};
    status_.emcy_active = false;
    last_produced_ = now;
    pending_rpdos_.fill(false);
    pending_image_ = image_;
}
bool Node::heartbeat_timed_out(MonotonicTimestamp now) const noexcept {
    return config_.heartbeat && heartbeat_monitoring_ && status_.communication_ok &&
           now - status_.last_heartbeat > config_.heartbeat->timeout;
}
void Node::arm_heartbeat_monitor(MonotonicTimestamp now) noexcept {
    last_produced_ = now;
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
