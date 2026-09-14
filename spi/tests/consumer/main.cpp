#include <spi/device.hpp>

int main() {
  const spi::Config config{spi::Mode::Mode0, 1'000'000, 8, spi::BitOrder::MsbFirst};
  spi::Device device;
  return config.max_speed_hz == 1'000'000 && !device.is_open() ? 0 : 1;
}
