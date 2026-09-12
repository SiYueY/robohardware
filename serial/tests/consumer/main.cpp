#include <serial/port.hpp>

int main() {
  serial::Port port;
  return port.is_open() ? 1 : 0;
}
