#include <spi/device.hpp>

int main() {
  spi::Device device;
  return device.is_open() ? 1 : 0;
}
