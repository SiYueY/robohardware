#pragma once

#include "can/interface.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace can {

class Socket final : public Interface {
public:
    struct Options {
        std::string interface;
        bool error_frames{true};
        bool receive_own{false};
        std::vector<Filter> filters;
    };

    static Result<Socket> open(Options options);

    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&&) noexcept;
    Socket& operator=(Socket&&) noexcept;

    Result<void> send(const Frame& frame) noexcept override;
    Result<bool> receive(Frame& frame, RxInfo& info) noexcept override;
    bool try_pop_event(Event& event) noexcept override;

    State state() const noexcept;
    Stats stats() const noexcept;
    int fd() const noexcept;

private:
    explicit Socket(int fd) noexcept;

    static constexpr std::size_t kEventCapacity = 16;

    int fd_{-1};
    std::atomic<State> state_{State::Unknown};
    std::atomic<std::uint64_t> rx_frames_{0};
    std::atomic<std::uint64_t> tx_frames_{0};
    std::atomic<std::uint64_t> rx_errors_{0};
    std::atomic<std::uint64_t> tx_errors_{0};
    std::atomic<std::uint64_t> error_frames_{0};
    std::atomic<std::uint64_t> rx_overruns_{0};
    std::atomic<std::uint64_t> dropped_events_{0};
    std::array<Event, kEventCapacity + 1> events_{};
    std::atomic<std::size_t> event_read_{0};
    std::atomic<std::size_t> event_write_{0};
};

}  // namespace can
