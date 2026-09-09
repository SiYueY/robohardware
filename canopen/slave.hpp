#pragma once
#include "canopen/network.hpp"
#include <memory>
namespace canopen {
class Slave {
public:
    explicit Slave(NodeConfig config) : node_(std::make_unique<Node>(std::move(config))) {}
    Node& node() noexcept { return *node_; }
    const Node& node() const noexcept { return *node_; }
    /// Transfers the configured protocol node to the network that owns the CAN interface.
    std::unique_ptr<Node> release_node() noexcept { return std::move(node_); }
    Result<void> attach(Network& network) { return network.add_slave_node(release_node()); }

private:
    std::unique_ptr<Node> node_;
};
}  // namespace canopen
