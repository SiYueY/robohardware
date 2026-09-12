#include <array>
#include <cstdint>
#include <iostream>

namespace {

// This is an independent finite-state model of the Buffer four-slot protocol
// from docs/realtime/implementation.md §16. It deliberately does not include buffer.hpp.
struct State {
  std::array<std::array<bool, 2>, 2> live{};
  std::array<std::array<int, 2>, 2> values{};
  std::uint32_t reading_pair{0};
  std::uint32_t published_pair{0};
  std::array<std::uint32_t, 2> published_slot{{0, 0}};
  unsigned int pair_publication_count{0};
};

struct Writer {
  std::uint32_t target_pair{0};
  std::uint32_t target_slot{0};
  bool selected{false};
};

struct Reader {
  std::uint32_t pair{0};
  int copied_value{0};
  bool selected{false};
};

bool writer_step(State& state, Writer& writer, unsigned int step) {
  switch (step) {
    case 0:
      writer.target_pair = 1U - state.reading_pair;
      writer.target_slot = 1U - state.published_slot[writer.target_pair];
      writer.selected = true;
      return true;
    case 1:
      if (!writer.selected || state.live[writer.target_pair][writer.target_slot]) {
        return false;
      }
      state.live[writer.target_pair][writer.target_slot] = true;
      state.values[writer.target_pair][writer.target_slot] = 2;
      return true;
    case 2:
      if (!writer.selected || !state.live[writer.target_pair][writer.target_slot]) {
        return false;
      }
      state.published_slot[writer.target_pair] = writer.target_slot;
      return true;
    case 3:
      if (!writer.selected ||
          state.published_slot[writer.target_pair] != writer.target_slot) {
        return false;
      }
      state.published_pair = writer.target_pair;
      ++state.pair_publication_count;
      return true;
    default:
      return false;
  }
}

bool reader_step(State& state, Reader& reader, unsigned int step) {
  switch (step) {
    case 0:
      reader.pair = state.published_pair;
      reader.selected = true;
      return true;
    case 1:
      if (!reader.selected) {
        return false;
      }
      state.reading_pair = reader.pair;
      return true;
    case 2: {
      if (!reader.selected) {
        return false;
      }
      const std::uint32_t slot = state.published_slot[reader.pair];
      if (!state.live[reader.pair][slot]) {
        return false;
      }
      reader.copied_value = state.values[reader.pair][slot];
      return reader.copied_value == 1 || reader.copied_value == 2;
    }
    default:
      return false;
  }
}

bool enumerate(
    State state,
    Writer writer,
    Reader reader,
    unsigned int writer_step_index,
    unsigned int reader_step_index) {
  if (writer_step_index == 4 && reader_step_index == 3) {
    return state.pair_publication_count == 1 &&
           (reader.copied_value == 1 || reader.copied_value == 2);
  }

  bool valid = true;
  if (writer_step_index < 4) {
    State next_state = state;
    Writer next_writer = writer;
    valid &= writer_step(next_state, next_writer, writer_step_index) &&
             enumerate(next_state,
                       next_writer,
                       reader,
                       writer_step_index + 1,
                       reader_step_index);
  }
  if (reader_step_index < 3) {
    State next_state = state;
    Reader next_reader = reader;
    valid &= reader_step(next_state, next_reader, reader_step_index) &&
             enumerate(next_state,
                       writer,
                       next_reader,
                       writer_step_index,
                       reader_step_index + 1);
  }
  return valid;
}

bool verify_scenario(std::uint32_t initially_published_pair) {
  State initial;
  initial.published_pair = initially_published_pair;
  initial.published_slot[initially_published_pair] = 0;
  initial.live[initially_published_pair][0] = true;
  initial.values[initially_published_pair][0] = 1;

  return enumerate(initial, {}, {}, 0, 0);
}

}  // namespace

int main() {
  if (!verify_scenario(0) || !verify_scenario(1)) {
    std::cerr << "Buffer state model found an invalid writer/reader interleaving\n";
    return 1;
  }
  return 0;
}
