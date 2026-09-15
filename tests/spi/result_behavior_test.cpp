#include <spi/device.hpp>
#include <cassert>
int main() {
    spi::Device device;
    auto transfer = device.transfer(nullptr, nullptr, 1);
    assert(!transfer && transfer.error() == spi::Error::NotOpen);
    auto close = device.close();
    assert(close);
}
