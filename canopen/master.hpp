#pragma once
#include "canopen/network.hpp"
#include "canopen/sdo.hpp"
namespace canopen {
class Master {
public:
    explicit Master(std::shared_ptr<can::Interface> bus) : network_(std::move(bus)) {}
    Network& network() noexcept { return network_; }
    const Network& network() const noexcept { return network_; }
    SdoClient sdo_client() noexcept { return SdoClient(network_); }
    Result<void> add_node(std::unique_ptr<Node> node) { return network_.add_node(std::move(node)); }
    Result<void> initialize() { return network_.initialize(); }
    Result<void> start() noexcept { return network_.start(); }
    Result<void> stop() noexcept { return network_.stop(); }

private:
    Network network_;
};
}  // namespace canopen
