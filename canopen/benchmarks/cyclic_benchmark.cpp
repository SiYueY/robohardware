#include "canopen/network.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

namespace {
class BenchmarkCan final : public can::Interface {
public:
    can::Result<void> send(const can::Frame&) noexcept override {
        ++tx_count;
        return {};
    }
    can::Result<bool> receive(can::Frame& frame, can::RxInfo& info) noexcept override {
        if (!pending_) return false;
        frame = frame_;
        pending_ = false;
        info.received_at = std::chrono::steady_clock::now();
        return true;
    }
    bool try_pop_event(can::Event&) noexcept override { return false; }
    void inject(const can::Frame& frame) noexcept {
        frame_ = frame;
        pending_ = true;
    }
    std::uint64_t tx_count{0};

private:
    can::Frame frame_{};
    bool pending_{false};
};
}  // namespace

int main() {
    constexpr std::size_t kNodes = 7;
    constexpr std::size_t kCycles = 2000;
    auto bus = std::make_shared<BenchmarkCan>();
    canopen::Network network(bus);
    for (std::uint8_t id = 1; id <= kNodes; ++id) {
        const canopen::ObjectKey key{static_cast<std::uint16_t>(0x2000 + id), 0};
        canopen::PdoConfig rpdo{static_cast<std::uint32_t>(0x200 + id), 1, {{key, 32, true}}};
        canopen::PdoConfig tpdo{static_cast<std::uint32_t>(0x180 + id), 1, {{key, 32, true}}};
        auto node = std::make_unique<canopen::Node>(
            canopen::NodeConfig{id, {rpdo}, {tpdo}, {}, std::chrono::milliseconds{100}});
        if (!node->dictionary().add(
                {key,
                 32,
                 true,
                 canopen::ObjectAccess::ReadWrite,
                 {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}}) ||
            !network.add_slave_node(std::move(node)))
            return 1;
    }
    if (!network.initialize() || !network.start()) return 1;

    std::array<std::chrono::nanoseconds, kCycles> samples{};
    for (std::size_t cycle = 0; cycle < kCycles; ++cycle) {
        const auto begin = std::chrono::steady_clock::now();
        for (std::uint8_t id = 1; id <= kNodes; ++id) {
            can::Frame frame{};
            frame.id = 0x200 + id;
            frame.size = 4;
            bus->inject(frame);
            if (!network.poll()) return 1;
        }
        can::Frame sync{};
        sync.id = 0x80;
        bus->inject(sync);
        if (!network.poll()) return 1;
        samples[cycle] = std::chrono::steady_clock::now() - begin;
    }
    std::sort(samples.begin(), samples.end());
    const auto total = samples.back();
    const auto p99 = samples[(kCycles * 99) / 100];
    std::cout << "nodes=" << kNodes << " cycles=" << kCycles
              << " p99_us=" << std::chrono::duration<double, std::micro>(p99).count()
              << " max_us=" << std::chrono::duration<double, std::micro>(total).count()
              << " tx_frames=" << bus->tx_count << '\n';
    return 0;
}
