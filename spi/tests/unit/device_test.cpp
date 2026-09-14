#include <spi/device.hpp>

#include "controlled_spidev_adapter.hpp"

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <initializer_list>
#include <limits>
#include <linux/spi/spi.h>
#include <string>
#include <utility>

namespace {

spi::Config config(
    spi::Mode mode = spi::Mode::Mode3,
    std::uint32_t max_speed_hz = 10'000'000,
    std::uint8_t bits_per_word = 8,
    spi::BitOrder bit_order = spi::BitOrder::MsbFirst) {
  return {mode, max_speed_hz, bits_per_word, bit_order};
}

void assert_error(const std::error_code& actual, const std::error_code& expected) {
  assert(actual == expected);
}

void assert_operations(std::initializer_list<spi::test::Operation> expected) {
  assert(spi::test::operation_count() == expected.size());
  std::size_t index = 0;
  for (const auto operation : expected) {
    assert(spi::test::operation_at(index++) == operation);
  }
}

void test_config_aggregate() {
  const spi::Config configured{
      spi::Mode::Mode2, 2'000'000, 16, spi::BitOrder::LsbFirst};
  assert(configured.mode == spi::Mode::Mode2);
  assert(configured.max_speed_hz == 2'000'000);
  assert(configured.bits_per_word == 16);
  assert(configured.bit_order == spi::BitOrder::LsbFirst);
}

void test_lifecycle_and_configuration_sequence() {
  using namespace spi;
  test::reset_adapter();

  Device device;
  assert(!device.is_open());
  assert_error(device.open("/dev/controlled", config()), {});
  assert(device.is_open());
  assert((test::open_flags() & (O_RDWR | O_CLOEXEC)) == (O_RDWR | O_CLOEXEC));
  assert((test::open_flags() & O_NONBLOCK) == 0);
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteSpeed, test::Operation::ReadSpeed});
  assert_error(device.open("/dev/controlled", config()),
               std::make_error_code(std::errc::device_or_resource_busy));

  Device moved(std::move(device));
  assert(!device.is_open());
  assert(moved.is_open());
  test::set_result(test::Operation::Close, -1, EIO);
  assert_error(moved.close(), std::error_code(EIO, std::system_category()));
  assert(!moved.is_open());
  assert_error(moved.close(), {});
  assert(test::close_count() == 1);

  test::reset_adapter();
  {
    Device destructed;
    assert_error(destructed.open("/dev/controlled", config()), {});
  }
  assert(test::close_count() == 1);
}

void test_open_validation() {
  using namespace spi;
  test::reset_adapter();
  Device device;
  assert_error(device.open("", config()), std::make_error_code(std::errc::invalid_argument));
  assert_error(device.open(std::string("/dev/controlled\0suffix", 22), config()),
               std::make_error_code(std::errc::invalid_argument));
  assert_error(device.open("/dev/controlled", config(static_cast<Mode>(99))),
               std::make_error_code(std::errc::invalid_argument));
  assert_error(device.open("/dev/controlled", config(Mode::Mode0, 0)),
               std::make_error_code(std::errc::invalid_argument));
  assert_error(device.open("/dev/controlled", config(Mode::Mode0, 1, 0)),
               std::make_error_code(std::errc::invalid_argument));
  assert_error(device.open("/dev/controlled", config(Mode::Mode0, 1, 8,
                                                        static_cast<BitOrder>(99))),
               std::make_error_code(std::errc::invalid_argument));
  assert(test::operation_count() == 0);

  test::set_result(test::Operation::Open, -1, EACCES);
  assert_error(device.open("/dev/controlled", config()),
               std::error_code(EACCES, std::system_category()));
  assert_operations({test::Operation::Open});
}

void test_capture_failures() {
  using namespace spi;
  test::reset_adapter();
  test::set_result(test::Operation::ReadMode, -1, ENOTTY);
  Device mode;
  assert_error(mode.open("/dev/controlled", config()),
               std::make_error_code(std::errc::inappropriate_io_control_operation));
  assert(!mode.is_open());
  assert_operations({test::Operation::Open, test::Operation::ReadMode, test::Operation::Close});

  test::reset_adapter();
  test::set_result(test::Operation::ReadBits, -1, EIO);
  Device bits;
  assert_error(bits.open("/dev/controlled", config()), std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::Close});

  test::reset_adapter();
  test::set_result(test::Operation::ReadSpeed, -1, EIO);
  Device speed;
  assert_error(speed.open("/dev/controlled", config()), std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::Close});
}

void test_mode_ownership() {
  using namespace spi;
  constexpr std::uint32_t kUnowned = SPI_CS_HIGH | SPI_3WIRE;
  const Mode modes[] = {Mode::Mode0, Mode::Mode1, Mode::Mode2, Mode::Mode3};
  const std::uint32_t expected[] = {0, SPI_CPHA, SPI_CPOL, SPI_CPOL | SPI_CPHA};
  for (std::size_t index = 0; index < 4; ++index) {
    test::reset_adapter();
    test::set_mode(kUnowned | SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST);
    Device device;
    assert_error(device.open("/dev/controlled", config(modes[index], 1'000'000, 8,
                                                          BitOrder::MsbFirst)), {});
    assert(test::current_mode() == (kUnowned | expected[index]));
  }

  test::reset_adapter();
  test::set_mode(kUnowned);
  Device lsb;
  assert_error(lsb.open("/dev/controlled", config(Mode::Mode1, 1'000'000, 8,
                                                     BitOrder::LsbFirst)), {});
  assert(test::current_mode() == (kUnowned | SPI_CPHA | SPI_LSB_FIRST));
}

void test_readback_mismatches_and_rollback() {
  using namespace spi;
  test::reset_adapter();
  test::ignore_mode_writes();
  Device mode;
  assert_error(mode.open("/dev/controlled", config()), std::make_error_code(std::errc::io_error));
  assert(!mode.is_open());
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteMode, test::Operation::Close});

  test::reset_adapter();
  test::ignore_bits_per_word_writes();
  Device bits;
  assert_error(bits.open("/dev/controlled", config(Mode::Mode3, 10'000'000, 16)),
               std::make_error_code(std::errc::io_error));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteBits, test::Operation::WriteMode,
                     test::Operation::Close});

  test::reset_adapter();
  test::ignore_mode_writes();
  test::set_result_on_call(test::Operation::WriteMode, 2, -1, EIO);
  Device rollback_failure;
  assert_error(rollback_failure.open("/dev/controlled", config()),
               std::make_error_code(std::errc::io_error));
  assert(!rollback_failure.is_open());

  test::reset_adapter();
  test::ignore_max_speed_hz_writes();
  Device speed;
  assert_error(speed.open("/dev/controlled", config()), std::make_error_code(std::errc::io_error));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteSpeed, test::Operation::ReadSpeed,
                     test::Operation::WriteSpeed, test::Operation::WriteBits,
                     test::Operation::WriteMode, test::Operation::Close});

  test::reset_adapter();
  test::set_result(test::Operation::WriteSpeed, -1, EIO);
  Device write_failure;
  assert_error(write_failure.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert(!write_failure.is_open());
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteSpeed, test::Operation::WriteSpeed,
                     test::Operation::WriteBits, test::Operation::WriteMode,
                     test::Operation::Close});
}

void test_configuration_stage_failures() {
  using namespace spi;
  test::reset_adapter();
  test::set_result(test::Operation::WriteMode, -1, EIO);
  Device mode_write;
  assert_error(mode_write.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::WriteMode,
                     test::Operation::Close});

  test::reset_adapter();
  test::set_result_on_call(test::Operation::ReadMode, 2, -1, EIO);
  Device mode_read;
  assert_error(mode_read.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteMode, test::Operation::Close});

  test::reset_adapter();
  test::set_result(test::Operation::WriteBits, -1, EIO);
  Device bits_write;
  assert_error(bits_write.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::WriteBits,
                     test::Operation::WriteMode, test::Operation::Close});

  test::reset_adapter();
  test::set_result_on_call(test::Operation::ReadBits, 2, -1, EIO);
  Device bits_read;
  assert_error(bits_read.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteBits, test::Operation::WriteMode,
                     test::Operation::Close});

  test::reset_adapter();
  test::set_result(test::Operation::WriteSpeed, -1, EIO);
  Device speed_write;
  assert_error(speed_write.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteSpeed, test::Operation::WriteSpeed,
                     test::Operation::WriteBits, test::Operation::WriteMode,
                     test::Operation::Close});

  test::reset_adapter();
  test::set_max_speed_hz(1'000'000);
  test::set_result_on_call(test::Operation::ReadSpeed, 2, -1, EIO);
  Device speed_read;
  assert_error(speed_read.open("/dev/controlled", config()),
               std::error_code(EIO, std::system_category()));
  assert(test::current_max_speed_hz() == 1'000'000);
  assert(!speed_read.is_open());
  assert_operations({test::Operation::Open, test::Operation::ReadMode,
                     test::Operation::ReadBits, test::Operation::ReadSpeed,
                     test::Operation::WriteMode, test::Operation::ReadMode,
                     test::Operation::WriteBits, test::Operation::ReadBits,
                     test::Operation::WriteSpeed, test::Operation::ReadSpeed,
                     test::Operation::WriteSpeed, test::Operation::WriteBits,
                     test::Operation::WriteMode, test::Operation::Close});
}

void test_transfer() {
  using namespace spi;
  test::reset_adapter();
  std::byte bytes[4]{};
  Device device;
  assert_error(device.transfer(bytes, nullptr, sizeof(bytes)),
               std::make_error_code(std::errc::bad_file_descriptor));
  assert_error(device.open("/dev/controlled", config()), {});
  const auto calls_after_open = test::operation_count();
  assert_error(device.transfer(nullptr, nullptr, 0), {});
  assert(test::operation_count() == calls_after_open);
  test::set_result(test::Operation::Transfer, sizeof(bytes));
  assert_error(device.transfer(nullptr, bytes, sizeof(bytes)), {});
  const auto& rx_only = test::last_transfer();
  assert(rx_only.tx_buf == 0);
  assert(rx_only.rx_buf == reinterpret_cast<std::uintptr_t>(bytes));
  assert(test::operation_count() == calls_after_open + 1);
  assert_error(device.transfer(nullptr, nullptr, sizeof(bytes)),
               std::make_error_code(std::errc::invalid_argument));
  assert(test::operation_count() == calls_after_open + 1);
  assert_error(device.transfer(bytes, nullptr, std::numeric_limits<std::size_t>::max()),
               std::make_error_code(std::errc::value_too_large));
  assert(test::operation_count() == calls_after_open + 1);
  constexpr auto kMaximumTransferSize =
      static_cast<std::size_t>(std::numeric_limits<int>::max());
  test::set_result(test::Operation::Transfer,
                   static_cast<int>(kMaximumTransferSize));
  assert_error(device.transfer(bytes, nullptr, kMaximumTransferSize), {});
  assert(test::operation_count() == calls_after_open + 2);
  assert_error(device.transfer(bytes, nullptr, kMaximumTransferSize + 1),
               std::make_error_code(std::errc::value_too_large));
  assert(test::operation_count() == calls_after_open + 2);

  test::set_result(test::Operation::Transfer, sizeof(bytes));
  assert_error(device.transfer(bytes, nullptr, sizeof(bytes)), {});
  const auto& tx_only = test::last_transfer();
  assert(tx_only.tx_buf == reinterpret_cast<std::uintptr_t>(bytes));
  assert(tx_only.rx_buf == 0);
  assert(tx_only.len == sizeof(bytes));
  assert(tx_only.speed_hz == 0 && tx_only.bits_per_word == 0 && tx_only.delay_usecs == 0);
  assert(tx_only.cs_change == 0 && tx_only.tx_nbits == 0 && tx_only.rx_nbits == 0);
  assert(tx_only.word_delay_usecs == 0);
  assert(test::operation_count() == calls_after_open + 3);

  test::set_result(test::Operation::Transfer, sizeof(bytes));
  assert_error(device.transfer(bytes, bytes, sizeof(bytes)), {});
  const auto& duplex = test::last_transfer();
  assert(duplex.tx_buf == reinterpret_cast<std::uintptr_t>(bytes));
  assert(duplex.rx_buf == reinterpret_cast<std::uintptr_t>(bytes));

  const auto before_failure = test::operation_count();
  test::set_result(test::Operation::Transfer, -1, EIO);
  assert_error(device.transfer(bytes, nullptr, sizeof(bytes)),
               std::error_code(EIO, std::system_category()));
  assert(test::operation_count() == before_failure + 1);
  test::set_result(test::Operation::Transfer, 1);
  assert_error(device.transfer(bytes, nullptr, sizeof(bytes)),
               std::make_error_code(std::errc::io_error));
}

}  // namespace

int main() {
  test_config_aggregate();
  test_lifecycle_and_configuration_sequence();
  test_open_validation();
  test_capture_failures();
  test_mode_ownership();
  test_readback_mismatches_and_rollback();
  test_configuration_stage_failures();
  test_transfer();
}
