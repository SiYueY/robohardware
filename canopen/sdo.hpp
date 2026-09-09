#pragma once
#include "canopen/network.hpp"
namespace canopen {
class SdoServer {
public:
    explicit SdoServer(Node& node) : node_(node) {}
    Result<void> process(const can::Frame& request, can::Frame& response) noexcept;
    void reset() noexcept {
        transfer_ = Transfer::Idle;
        data_.clear();
        offset_ = 0;
        toggle_ = false;
    }

private:
    enum class Transfer { Idle, Upload, Download };
    Node& node_;
    Transfer transfer_{Transfer::Idle};
    ObjectKey key_{};
    std::vector<std::byte> data_;
    std::size_t offset_{0};
    bool toggle_{false};
};
class SdoClient {
public:
    explicit SdoClient(Network& network) : network_(network) {}
    Result<std::vector<std::byte>> upload(std::uint8_t node_id, ObjectKey key, Duration timeout);
    Result<void> download(
        std::uint8_t node_id, ObjectKey key, const std::vector<std::byte>& value, Duration timeout);

private:
    class TransactionGuard {
    public:
        TransactionGuard(Network& network, std::uint8_t node_id) noexcept
        : network_(network), node_id_(node_id) {}
        ~TransactionGuard();

    private:
        Network& network_;
        std::uint8_t node_id_;
    };
    Result<can::Frame> transact(std::uint8_t node_id, const can::Frame& request, Duration timeout);
    Network& network_;
};
}  // namespace canopen
