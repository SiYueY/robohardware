#pragma once

#include "can/filter.hpp"
#include "can/interface.hpp"

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
};

}  // namespace can
