#include <array>
#include <cstddef>
#include <iostream>

namespace {

// This is an independent finite-state model of the Queue ring protocol from
// docs/realtime/implementation.md §15. It deliberately does not include queue.hpp.
constexpr std::size_t kPhysicalCapacity = 3;

struct State {
  std::array<bool, kPhysicalCapacity> live{};
  std::array<int, kPhysicalCapacity> values{};
  std::size_t read_index{2};
  std::size_t write_index{0};
  int copied_value{0};
};

struct Producer {
  std::size_t write{0};
  std::size_t next_write{0};
  bool has_reservation{false};
};

struct Consumer {
  std::size_t read{0};
  bool has_observation{false};
};

[[nodiscard]] constexpr std::size_t next(std::size_t index) noexcept {
  return index + 1 == kPhysicalCapacity ? 0 : index + 1;
}

bool producer_step(State& state, Producer& producer, unsigned int step) {
  switch (step) {
    case 0:
      producer.write = state.write_index;
      producer.next_write = next(producer.write);
      producer.has_reservation = producer.next_write != state.read_index;
      return producer.has_reservation;
    case 1:
      if (!producer.has_reservation || state.live[producer.write]) {
        return false;
      }
      state.values[producer.write] = 2;
      state.live[producer.write] = true;
      return true;
    case 2:
      if (!producer.has_reservation || !state.live[producer.write]) {
        return false;
      }
      state.write_index = producer.next_write;
      return true;
    default:
      return false;
  }
}

bool consumer_step(State& state, Consumer& consumer, unsigned int step) {
  switch (step) {
    case 0:
      consumer.read = state.read_index;
      consumer.has_observation = consumer.read != state.write_index;
      return consumer.has_observation;
    case 1:
      if (!consumer.has_observation || !state.live[consumer.read]) {
        return false;
      }
      state.copied_value = state.values[consumer.read];
      return true;
    case 2:
      if (!consumer.has_observation || !state.live[consumer.read]) {
        return false;
      }
      state.live[consumer.read] = false;
      return true;
    case 3:
      if (!consumer.has_observation || state.live[consumer.read]) {
        return false;
      }
      state.read_index = next(consumer.read);
      return true;
    default:
      return false;
  }
}

bool enumerate(
    State state,
    Producer producer,
    Consumer consumer,
    unsigned int producer_step_index,
    unsigned int consumer_step_index) {
  if (producer_step_index == 3 && consumer_step_index == 4) {
    return state.copied_value == 1 && state.live[0] && !state.live[2] &&
           state.values[0] == 2 && state.read_index == 0 && state.write_index == 1;
  }

  bool valid = true;
  if (producer_step_index < 3) {
    State next_state = state;
    Producer next_producer = producer;
    valid &= producer_step(next_state, next_producer, producer_step_index) &&
             enumerate(next_state,
                       next_producer,
                       consumer,
                       producer_step_index + 1,
                       consumer_step_index);
  }
  if (consumer_step_index < 4) {
    State next_state = state;
    Consumer next_consumer = consumer;
    valid &= consumer_step(next_state, next_consumer, consumer_step_index) &&
             enumerate(next_state,
                       producer,
                       next_consumer,
                       producer_step_index,
                       consumer_step_index + 1);
  }
  return valid;
}

}  // namespace

int main() {
  State initial;
  initial.live[2] = true;
  initial.values[2] = 1;

  if (!enumerate(initial, {}, {}, 0, 0)) {
    std::cerr << "Queue state model found an invalid producer/consumer interleaving\n";
    return 1;
  }
  return 0;
}
