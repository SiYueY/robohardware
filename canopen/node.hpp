#pragma once
#include "canopen/nmt.hpp"
#include "canopen/object_dictionary.hpp"
#include "canopen/pdo.hpp"
#include "canopen/status.hpp"
#include <array>
#include <optional>
#include <vector>
namespace canopen {
struct NodeConfig {
    std::uint8_t node_id{0};
    std::vector<PdoConfig> rpdos;
    std::vector<PdoConfig> tpdos;
    std::optional<HeartbeatConsumerConfig> heartbeat;
    Duration producer_heartbeat{};
};
class Node {
public:
    using ResetNodeHook = void (*)(void*) noexcept;
    explicit Node(NodeConfig config);
    Result<void> initialize();
    std::uint8_t id() const noexcept { return config_.node_id; }
    ObjectDictionary& dictionary() noexcept { return dictionary_; }
    const ObjectDictionary& dictionary() const noexcept { return dictionary_; }
    ProcessImage& process_image() noexcept { return image_; }
    const ProcessImage& process_image() const noexcept { return image_; }
    NodeStatus status() const noexcept { return status_; }
    const std::vector<PdoPlan>& rpdos() const noexcept { return rpdos_; }
    const std::vector<PdoPlan>& tpdos() const noexcept { return tpdos_; }
    /// Encodes a frozen TPDO plan; callers choose the cyclic schedule.
    Result<void> encode_tpdo(std::size_t index, can::Frame& frame) const noexcept {
        return index < tpdos_.size() ? tpdos_[index].encode(image_, frame)
                                     : Result<void>{Error{ErrorCode::InvalidArgument}};
    }
    Result<void> receive_rpdo(
        std::size_t index, const can::Frame& frame, MonotonicTimestamp now) noexcept;
    Result<void> receive_tpdo(
        std::size_t index, const can::Frame& frame, MonotonicTimestamp now) noexcept;
    Result<void> commit_synchronous_rpdos() noexcept;
    void receive_heartbeat(NmtState state, MonotonicTimestamp now) noexcept;
    void receive_emcy(const can::Frame& frame, MonotonicTimestamp now) noexcept;
    bool receive_nmt(NmtCommand command, MonotonicTimestamp now) noexcept;
    void reset_communication(MonotonicTimestamp now) noexcept;
    bool heartbeat_timed_out(MonotonicTimestamp now) const noexcept;
    void arm_heartbeat_monitor(MonotonicTimestamp now) noexcept;
    void mark_heartbeat_timeout() noexcept {
        status_.heartbeat_alive = false;
        status_.communication_ok = false;
    }
    bool should_produce_heartbeat(MonotonicTimestamp now) noexcept;
    NmtState nmt_state() const noexcept { return status_.nmt_state; }
    /// Installs a non-owning, non-throwing hook before the node starts running.
    void set_reset_node_hook(ResetNodeHook hook, void* context = nullptr) noexcept {
        reset_node_hook_ = hook;
        reset_node_context_ = context;
    }
    Result<void> sdo_read(ObjectKey key, std::byte* data, std::size_t& size) const noexcept {
        return dictionary_.read(key, data, size);
    }
    Result<void> sdo_write(ObjectKey key, const std::byte* data, std::size_t size) noexcept {
        return dictionary_.write(key, data, size);
    }

private:
    NodeConfig config_;
    ObjectDictionary dictionary_;
    ProcessImage image_;
    ProcessImage pending_image_;
    std::vector<PdoPlan> rpdos_, tpdos_;
    std::array<bool, 8> pending_rpdos_{};
    NodeStatus status_{};
    MonotonicTimestamp last_produced_{};
    bool heartbeat_monitoring_{false};
    ResetNodeHook reset_node_hook_{nullptr};
    void* reset_node_context_{nullptr};
};
}  // namespace canopen
