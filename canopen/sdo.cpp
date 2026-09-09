#include "canopen/sdo.hpp"

#include <algorithm>
#include <chrono>

namespace canopen {
namespace {
void key_from(const can::Frame& f, ObjectKey& k) noexcept {
    k.index = static_cast<std::uint16_t>(
        static_cast<std::uint8_t>(f.data[1]) |
        (static_cast<std::uint16_t>(static_cast<std::uint8_t>(f.data[2])) << 8));
    k.subindex = static_cast<std::uint8_t>(f.data[3]);
}
void abort_frame(can::Frame& f, std::uint8_t id, ObjectKey k, std::uint32_t code) noexcept {
    f = {};
    f.id = 0x580 + id;
    f.size = 8;
    f.data[0] = std::byte{0x80};
    f.data[1] = static_cast<std::byte>(k.index);
    f.data[2] = static_cast<std::byte>(k.index >> 8);
    f.data[3] = static_cast<std::byte>(k.subindex);
    for (std::size_t i = 0; i < 4; ++i) f.data[4 + i] = static_cast<std::byte>(code >> (8 * i));
}
std::uint32_t u32(const can::Frame& f) noexcept {
    std::uint32_t r = 0;
    for (std::size_t i = 0; i < 4; ++i)
        r |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(f.data[4 + i])) << (8 * i);
    return r;
}
Error abort_error(std::uint8_t n, ObjectKey k, const can::Frame& f) noexcept {
    return {ErrorCode::SdoAbort, n, k.index, k.subindex, u32(f)};
}
}  // namespace

SdoClient::TransactionGuard::~TransactionGuard() { network_.end_sdo(node_id_); }

Result<void> SdoServer::process(const can::Frame& q, can::Frame& r) noexcept {
    if (q.size != 8) return Error{ErrorCode::InvalidSdoResponse, node_.id()};
    const auto cs = static_cast<std::uint8_t>(q.data[0]);
    if (cs == 0x40) {
        key_from(q, key_);
        auto entry = node_.dictionary().find(key_);
        if (!entry) {
            abort_frame(r, node_.id(), key_, 0x06020000);
            return {};
        }
        data_ = entry.value()->value;
        offset_ = 0;
        toggle_ = false;
        if (data_.size() <= 4) {
            r = {};
            r.id = 0x580 + node_.id();
            r.size = 8;
            r.data[0] = static_cast<std::byte>(0x43 | ((4 - data_.size()) << 2));
            r.data[1] = q.data[1];
            r.data[2] = q.data[2];
            r.data[3] = q.data[3];
            for (std::size_t i = 0; i < data_.size(); ++i) r.data[4 + i] = data_[i];
            return {};
        }
        transfer_ = Transfer::Upload;
        r = {};
        r.id = 0x580 + node_.id();
        r.size = 8;
        r.data[0] = std::byte{0x41};
        r.data[1] = q.data[1];
        r.data[2] = q.data[2];
        r.data[3] = q.data[3];
        for (std::size_t i = 0; i < 4; ++i)
            r.data[4 + i] = static_cast<std::byte>(data_.size() >> (8 * i));
        return {};
    }
    if ((cs & 0xE0) == 0x20) {
        key_from(q, key_);
        if (cs & 0x02) {
            const auto n = static_cast<std::size_t>(4 - ((cs >> 2) & 3));
            auto w = node_.sdo_write(key_, q.data.data() + 4, n);
            if (!w) {
                abort_frame(r, node_.id(), key_, 0x06010002);
                return {};
            }
            r = {};
            r.id = 0x580 + node_.id();
            r.size = 8;
            r.data[0] = std::byte{0x60};
            r.data[1] = q.data[1];
            r.data[2] = q.data[2];
            r.data[3] = q.data[3];
            return {};
        }
        if (!(cs & 1)) {
            abort_frame(r, node_.id(), key_, 0x05040001);
            return {};
        }
        data_.clear();
        data_.reserve(u32(q));
        offset_ = 0;
        toggle_ = false;
        transfer_ = Transfer::Download;
        r = {};
        r.id = 0x580 + node_.id();
        r.size = 8;
        r.data[0] = std::byte{0x60};
        r.data[1] = q.data[1];
        r.data[2] = q.data[2];
        r.data[3] = q.data[3];
        return {};
    }
    if (transfer_ == Transfer::Upload && (cs & 0xEF) == 0x60) {
        if (((cs & 0x10) != 0) != toggle_) {
            abort_frame(r, node_.id(), key_, 0x05030000);
            transfer_ = Transfer::Idle;
            return {};
        }
        const auto count = std::min<std::size_t>(7, data_.size() - offset_);
        const bool last = count == data_.size() - offset_;
        r = {};
        r.id = 0x580 + node_.id();
        r.size = 8;
        r.data[0] =
            static_cast<std::byte>((toggle_ ? 0x10 : 0) | ((7 - count) << 1) | (last ? 1 : 0));
        for (std::size_t i = 0; i < count; ++i) r.data[1 + i] = data_[offset_ + i];
        offset_ += count;
        toggle_ = !toggle_;
        if (last) transfer_ = Transfer::Idle;
        return {};
    }
    if (transfer_ == Transfer::Download && (cs & 0xE0) == 0) {
        if (((cs & 0x10) != 0) != toggle_) {
            abort_frame(r, node_.id(), key_, 0x05030000);
            transfer_ = Transfer::Idle;
            return {};
        }
        const auto count = static_cast<std::size_t>(7 - ((cs >> 1) & 7));
        const bool last = (cs & 1) != 0;
        data_.insert(data_.end(), q.data.begin() + 1, q.data.begin() + 1 + count);
        r = {};
        r.id = 0x580 + node_.id();
        r.size = 8;
        r.data[0] = static_cast<std::byte>(0x20 | (toggle_ ? 0x10 : 0));
        toggle_ = !toggle_;
        if (last) {
            auto w = node_.sdo_write(key_, data_.data(), data_.size());
            transfer_ = Transfer::Idle;
            if (!w) abort_frame(r, node_.id(), key_, 0x06010002);
        }
        return {};
    }
    abort_frame(r, node_.id(), key_, 0x05040001);
    transfer_ = Transfer::Idle;
    return {};
}
Result<can::Frame> SdoClient::transact(std::uint8_t id, const can::Frame& q, Duration timeout) {
    auto s = network_.send(q);
    if (!s) return s.error();
    return network_.wait_sdo_response(id, timeout);
}
Result<std::vector<std::byte>> SdoClient::upload(std::uint8_t id, ObjectKey k, Duration timeout) {
    auto active = network_.begin_sdo(id);
    if (!active) return active.error();
    TransactionGuard transaction{network_, id};
    can::Frame q{};
    q.id = 0x600 + id;
    q.size = 8;
    q.data[0] = std::byte{0x40};
    q.data[1] = static_cast<std::byte>(k.index);
    q.data[2] = static_cast<std::byte>(k.index >> 8);
    q.data[3] = static_cast<std::byte>(k.subindex);
    auto r = transact(id, q, timeout);
    if (!r) return r.error();
    auto cs = static_cast<std::uint8_t>(r.value().data[0]);
    if (cs == 0x80) {
        ++network_.stats_.sdo_aborts;
        return abort_error(id, k, r.value());
    }
    if ((cs & 0xE3) == 0x43) {
        auto n = static_cast<std::size_t>(4 - ((cs >> 2) & 3));
        return std::vector<std::byte>(r.value().data.begin() + 4, r.value().data.begin() + 4 + n);
    }
    if (cs != 0x41) return Error{ErrorCode::InvalidSdoResponse, id, k.index, k.subindex};
    const auto total = u32(r.value());
    std::vector<std::byte> out;
    out.reserve(total);
    bool toggle = false;
    while (out.size() < total) {
        q = {};
        q.id = 0x600 + id;
        q.size = 8;
        q.data[0] = static_cast<std::byte>(0x60 | (toggle ? 0x10 : 0));
        r = transact(id, q, timeout);
        if (!r) return r.error();
        cs = static_cast<std::uint8_t>(r.value().data[0]);
        if (cs == 0x80) return abort_error(id, k, r.value());
        if (((cs & 0x10) != 0) != toggle)
            return Error{ErrorCode::InvalidSdoResponse, id, k.index, k.subindex};
        auto n = static_cast<std::size_t>(7 - ((cs >> 1) & 7));
        if (out.size() + n > total)
            return Error{ErrorCode::InvalidSdoResponse, id, k.index, k.subindex};
        out.insert(out.end(), r.value().data.begin() + 1, r.value().data.begin() + 1 + n);
        toggle = !toggle;
        if (cs & 1) return out;
    }
    return Error{ErrorCode::InvalidSdoResponse, id, k.index, k.subindex};
}
Result<void> SdoClient::download(
    std::uint8_t id, ObjectKey k, const std::vector<std::byte>& v, Duration timeout) {
    auto active = network_.begin_sdo(id);
    if (!active) return active.error();
    TransactionGuard transaction{network_, id};
    can::Frame q{};
    q.id = 0x600 + id;
    q.size = 8;
    q.data[1] = static_cast<std::byte>(k.index);
    q.data[2] = static_cast<std::byte>(k.index >> 8);
    q.data[3] = static_cast<std::byte>(k.subindex);
    if (v.size() <= 4) {
        q.data[0] = static_cast<std::byte>(0x23 | ((4 - v.size()) << 2));
        for (std::size_t i = 0; i < v.size(); ++i) q.data[4 + i] = v[i];
        auto r = transact(id, q, timeout);
        if (!r) return r.error();
        return static_cast<std::uint8_t>(r.value().data[0]) == 0x60
                   ? Result<void>{}
                   : Result<void>{abort_error(id, k, r.value())};
    }
    q.data[0] = std::byte{0x21};
    for (std::size_t i = 0; i < 4; ++i) q.data[4 + i] = static_cast<std::byte>(v.size() >> (8 * i));
    auto r = transact(id, q, timeout);
    if (!r) return r.error();
    if (static_cast<std::uint8_t>(r.value().data[0]) != 0x60) return abort_error(id, k, r.value());
    bool toggle = false;
    for (std::size_t pos = 0; pos < v.size();) {
        auto n = std::min<std::size_t>(7, v.size() - pos);
        bool last = pos + n == v.size();
        q = {};
        q.id = 0x600 + id;
        q.size = 8;
        q.data[0] = static_cast<std::byte>((toggle ? 0x10 : 0) | ((7 - n) << 1) | (last ? 1 : 0));
        for (std::size_t i = 0; i < n; ++i) q.data[1 + i] = v[pos + i];
        r = transact(id, q, timeout);
        if (!r) return r.error();
        if (static_cast<std::uint8_t>(r.value().data[0]) !=
            static_cast<std::uint8_t>(0x20 | (toggle ? 0x10 : 0)))
            return abort_error(id, k, r.value());
        toggle = !toggle;
        pos += n;
    }
    return {};
}
}  // namespace canopen
