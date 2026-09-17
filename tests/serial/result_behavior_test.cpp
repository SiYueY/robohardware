#include <serial/port.hpp>
#include <cassert>
int main() {
    serial::Port port;
    auto read = port.read(nullptr, 0);
    assert(!read && read.error() == serial::Error::NotOpen);
    auto close = port.close();
    assert(close);
}
